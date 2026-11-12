#include "../../globals/globals.h"
#include "flipworld.h"
#include "../screen/screenfx.h"   // ScreenFx_FindClientMode
#include "visuals.h"    // Chams_LevelInit/LevelUnload -- piggybacked on this file's LevelInit hook
#include "world_fx3.h"  // WorldFx3_LevelShutdown
#include "../hud/avatar_cache.h"   // unit E5: Avatar_OnMapChange piggybacks the same hook
#include "../web/websurface.h"     // Web_LevelShutdown
#include "../botoxnet/botoxnet.h"  // g_botox_net.on_level_init (includes aim/css_bridge.h, legal outside aim)

// Port of fpsoverlay's flipworld.cpp (itself ref's CFlipWorld + CMisc::FlipWorldInput).
// Every engine number used here is a flipslot:: constant (flipmath.h); fpsoverlay's
// tools/check_vtables.py asserts each against the installed game and refuses a raw slot number in this
// file. The decisions are FlipCoreTick (flipmath.h), driven by tests/flipworld_test.cpp.

using namespace flipslot;

static void* s_flipTex    = nullptr;   // ITexture*  copy of the frame
static void* s_flipMat    = nullptr;   // IMaterial* UnlitGeneric, $basetexture = the RT, $ignorez 1
static bool  s_flipReady  = false;
static bool  s_vmtWritten = false;     // the VMT goes to disk once per session, not per map

// Names of our own so dih and fpsoverlay can never collide on one RT.
static constexpr const char* k_rtName  = "BotoxFlipWorldTexture";
static constexpr const char* k_matName = "botox_flipworld/flip";

// All of FlipCore is game-thread state; eject's fallback tick from its own thread takes the same lock.
// Other threads (render-thread W2S callers, EndScene) read only the published atomics below.
static FlipCore s_core;
static SRWLOCK  s_coreLock = SRWLOCK_INIT;
static bool     s_active   = false;              // wanted && owner, as of the last tick
static std::atomic<bool>     s_pubMirroring{false};
static std::atomic<bool>     s_pubInWorldPass{false};
static std::atomic<DWORD>    s_gameTid{0};
static std::atomic<uint64_t> s_lastTickMs{0};

struct FlipLock {
    FlipLock()  { AcquireSRWLockExclusive(&s_coreLock); }
    ~FlipLock() { ReleaseSRWLockExclusive(&s_coreLock); }
};

// ---- one owner per process, shared with fpsoverlay -------------------------------------------------
// Two copies would mirror the frame and the HUD twice (no mirror) and fight over cl_flipviewmodels
// every tick. A named mapping per process id holds the owner's token and whether the owner's frame is
// mirrored, which dih's depth passes need when fpsoverlay is the one mirroring.

struct FlipShared { LONG owner; LONG frameMirrored; };
static constexpr LONG k_flipToken = 0x31484944;   // "DIH1" (fpsoverlay uses "FPS1")
static HANDLE      s_sharedMap = nullptr;
static FlipShared* s_shared    = nullptr;

static void FlipSharedOpen()
{
    if (s_shared) return;
    char name[64];
    sprintf_s(name, "Local\\CSSFlipWorld-%lu", GetCurrentProcessId());
    s_sharedMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FlipShared), name);
    if (s_sharedMap) s_shared = static_cast<FlipShared*>(MapViewOfFile(s_sharedMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FlipShared)));
    if (!s_shared) DebugLog("flipworld: shared owner mapping FAILED -- flip world stays off");
}

static void FlipPublish()
{
    const bool m = s_active && s_core.track.mirrored;
    s_pubMirroring.store(m);
    s_pubInWorldPass.store(s_core.track.inWorldPass);
    if (s_shared && s_core.owner) InterlockedExchange(&s_shared->frameMirrored, m ? 1 : 0);
}

// ---- material system helpers ---------------------------------------------------

static void* FlipGetRC()
{
    using F = void*(__fastcall*)(void*);
    void* pRC = nullptr;
    __try { pRC = ((F)(*(void***)g_matSystem)[matGetRenderContext])(g_matSystem); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return pRC;
}

static void FlipGetScreen(int* w, int* h)
{
    *w = 0; *h = 0;
    using GD = void(__fastcall*)(void*, int*, int*);
    __try { ((GD)(*(void***)g_matSystem)[matGetBackBufferDimensions])(g_matSystem, w, h); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static bool FlipCopyFrame(void* pRC, void* pTex)
{
    using F = void(__fastcall*)(void*, void*);
    __try { ((F)(*(void***)pRC)[ctxCopyRenderTargetToTex])(pRC, pTex); return true; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// The view rectangle with its source texels reversed left-right (FlipMirrorSrcRect). The copy is
// the whole backbuffer, so texel = pixel and the texture size handed over is the backbuffer's.
static bool FlipDrawMirror(void* pRC, void* mat, int x, int y, int w, int h, int bbW, int bbH)
{
    using F = void(__fastcall*)(void*, void*, int, int, int, int,
                                float, float, float, float, int, int, void*, int, int);
    float src[4];
    FlipMirrorSrcRect(x, y, w, h, src);
    __try {
        ((F)(*(void***)pRC)[ctxDrawScreenSpaceRect])(pRC, mat, x, y, w, h, src[0], src[1], src[2], src[3], bbW, bbH, nullptr, 1, 1);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// The CViewSetup RenderView is drawing (mat_viewportscale < 1 = a sub-rectangle that
// viewrender.cpp:2263 stretches to the screen after this point). Implausible = full backbuffer.
static void FlipViewRect(const void* setup, int bbW, int bbH, int& x, int& y, int& w, int& h)
{
    x = 0; y = 0; w = bbW; h = bbH;
    if (!setup) return;
    int vx = 0, vy = 0, vw = 0, vh = 0;
    __try {
        const uintptr_t p = reinterpret_cast<uintptr_t>(setup);
        vx = *reinterpret_cast<const int*>(p + vsX);
        vy = *reinterpret_cast<const int*>(p + vsY);
        vw = *reinterpret_cast<const int*>(p + vsWidth);
        vh = *reinterpret_cast<const int*>(p + vsHeight);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (vx >= 0 && vy >= 0 && vw > 0 && vh > 0 && vx + vw <= bbW && vy + vh <= bbH) {
        x = vx; y = vy; w = vw; h = vh;
    }
}

static void FlipRef(void* p, int slot)
{
    using F = void(__fastcall*)(void*);
    __try { ((F)(*(void***)p)[slot])(p); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void* FlipFindMaterial(const char* name)
{
    using Find_t  = void*(__fastcall*)(void*, const char*, const char*, bool, const char*);
    using IsErr_t = bool(__fastcall*)(void*);
    void* m = nullptr;
    __try {
        m = ((Find_t)(*(void***)g_matSystem)[matFindMaterial])(g_matSystem, name, "", false, nullptr);
        if (m && ((IsErr_t)(*(void***)m)[matIsErrorMaterial])(m)) m = nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { m = nullptr; }
    return m;
}

// VMTs are read from <folder of the exe>\cstrike\materials, not the process CWD -- CS:S's mod
// folder is "cstrike" (sibling of "bin", see AGENT_BRIEF live-binary path), NOT TF2's "tf".
static void FlipWriteVMT()
{
    char root[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, root, MAX_PATH);
    char* slash = (n > 0 && n < MAX_PATH) ? strrchr(root, '\\') : nullptr;
    if (slash) *slash = '\0'; else GetCurrentDirectoryA(MAX_PATH, root);

    char p[MAX_PATH];
    sprintf_s(p, "%s\\cstrike", root);                           _mkdir(p);
    sprintf_s(p, "%s\\cstrike\\materials", root);                _mkdir(p);
    sprintf_s(p, "%s\\cstrike\\materials\\botox_flipworld", root); _mkdir(p);
    sprintf_s(p, "%s\\cstrike\\materials\\botox_flipworld\\flip.vmt", root);
    if (FILE* f = fopen(p, "w")) {
        fprintf(f, "\"UnlitGeneric\"\n{\n\t$basetexture \"%s\"\n\t$ignorez \"1\"\n}", k_rtName);
        fclose(f);
    }
}

// ---- lifecycle (CViewRender::LevelInit / LevelShutdown, game thread, nothing drawing) -----------

static void FlipLevelUnload()
{
    s_flipReady = false;
    if (s_flipMat) { FlipRef(s_flipMat, matDecRef); s_flipMat = nullptr; }
    if (s_flipTex) { FlipRef(s_flipTex, texDecRef); s_flipTex = nullptr; }
}

// botox csgo-sdk's flip_world.cpp (impl_t::execute, build()) allocates its copy render target
// lazily on first draw -- not at a level-load callback -- checking size each call and rebuilding
// only on change. CreateNamedRenderTargetTextureEx (imaterialsystem.h) needs no
// Begin/EndRenderTargetAllocation bracket for an ad-hoc single RT: that bracket is the engine's own
// batch (re)allocation pass (AllocateStandardRenderTargets, mirrors an alt-tab release/restore of
// EVERY material) and CTextureReference::InitRenderTarget (MaterialSystemUtil.cpp:183) -- the SDK's
// generic any-time RT helper -- calls the same function with no such bracket either. So, 1:1 with
// botox: build on first need, not only at LevelInit. A mid-map inject (this DLL's common case) then
// gets flip world working the same frame it's turned on, instead of requiring a map reload.
static void FlipEnsureReady()
{
    if (s_flipReady || !g_matSystem) return;
    static uint64_t s_nextTry = 0;          // retry at most 1/sec so a real failure doesn't spam
    const uint64_t now = GetTickCount64();
    if (now < s_nextTry) return;
    s_nextTry = now + 1000;

    int w = 0, h = 0;
    FlipGetScreen(&w, &h);
    if (w <= 0 || h <= 0) { w = 1280; h = 720; }

    // fpsoverlay's proven call: literal w x h (8), IMAGE_FORMAT_RGB888 (2), shared depth (0),
    // CLAMPS|CLAMPT, HDR RT flag.
    using CreateRT = void*(__fastcall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned);
    __try {
        s_flipTex = ((CreateRT)(*(void***)g_matSystem)[matCreateNamedRTEx])(g_matSystem, k_rtName, w, h, 8, 2, 0, 0x200Cu, 1u);
    } __except(EXCEPTION_EXECUTE_HANDLER) { s_flipTex = nullptr; }
    if (!s_flipTex) { DebugLog("flipworld: RT creation FAILED (lazy init)"); return; }
    FlipRef(s_flipTex, texIncRef);

    if (!s_vmtWritten) { FlipWriteVMT(); s_vmtWritten = true; }
    s_flipMat = FlipFindMaterial(k_matName);
    if (s_flipMat) FlipRef(s_flipMat, matIncRef);

    s_flipReady = s_flipTex && s_flipMat;
    DebugLog("flipworld: RT ready %dx%d (lazy, no map reload needed) ok=%d tex=%p mat=%p", w, h, (int)s_flipReady, s_flipTex, s_flipMat);
}

// Map load still refreshes the RT (screen size can change, e.g. a resolution switch mid-session):
// drop the old one and let FlipEnsureReady rebuild it the next time it's actually needed.
static void FlipLevelInit()
{
    if (!g_matSystem) return;
    FlipLevelUnload();
}

// g_eject is in here, not in each caller: after it is set every tick releases instead of holding.
// Game thread only (IsInGame is an engine call).
static bool FlipWanted()
{
    return !g_eject.load() && g_flipWorld && s_flipReady && s_shared != nullptr && IsInGame();
}

bool FlipWorld_Mirroring()
{
    return s_pubMirroring.load();
}

bool FlipWorld_FrameMirroredAnywhere()
{
    return s_shared ? s_shared->frameMirrored != 0 : s_pubMirroring.load();
}

// ---- frame events (game thread) -------------------------------------------------------------------

static bool OnGameThread() { const DWORD t = s_gameTid.load(); return t && GetCurrentThreadId() == t; }

void FlipWorld_RenderStart()
{
    if (!OnGameThread()) return;
    FlipLock _l;
    FlipTrackFrameStart(s_core.track);
    FlipPublish();
}

void FlipWorld_WorldPass()
{
    if (!OnGameThread()) return;
    FlipLock _l;
    FlipTrackWorldPass(s_core.track);
}

// IVRenderView::Push2DView hook (below): ends the 3D pass for W2S.
using Push2D_t = void(__fastcall*)(void*, const void*, int, void*, void*);
static Push2D_t o_FlipPush2D = nullptr;

// End of the post slot: world + particles drawn, DrawViewModels next (viewrender.cpp:2157/2160).
void FlipWorld_Render(const void* pViewSetup)
{
    if (!g_eject && g_flipWorld) FlipEnsureReady();   // lazy build: works the frame it's turned on
    if (!s_active || !g_matSystem || !s_flipTex || !s_flipMat) return;

    void* pRC = FlipGetRC();
    if (!pRC) return;
    int bbW = 0, bbH = 0;
    FlipGetScreen(&bbW, &bbH);
    if (bbW <= 0 || bbH <= 0) return;
    int x, y, w, h;
    FlipViewRect(pViewSetup, bbW, bbH, x, y, w, h);

    if (!FlipCopyFrame(pRC, s_flipTex)) return;
    if (!FlipDrawMirror(pRC, s_flipMat, x, y, w, h, bbW, bbH)) return;
    {
        FlipLock _l;
        FlipTrackMirror(s_core.track);
        if (!o_FlipPush2D) FlipTrackTwoD(s_core.track);   // no 2D push hook: the gun's beams get the mirrored W2S
        FlipPublish();
    }

    static int s_lastRect[4] = { -1, -1, -1, -1 };
    if (s_lastRect[0] != x || s_lastRect[1] != y || s_lastRect[2] != w || s_lastRect[3] != h) {
        s_lastRect[0] = x; s_lastRect[1] = y; s_lastRect[2] = w; s_lastRect[3] = h;
        DebugLog("flipworld: mirroring before DrawViewModels (world + particles, gun unmirrored), view %d,%d %dx%d%s",
                 x, y, w, h, o_FlipPush2D ? "" : " -- no Push2DView hook, W2S world pass ends at the mirror");
    }
}

// ---- angle-placed HUD patches (flipmath.h k_flipHudPatches) --------------------------------------

struct FlipPatchSite { uint8_t* at; bool mirrored; };
static FlipPatchSite s_patch[k_flipHudPatchCount ? k_flipHudPatchCount : 1] = {};   // k_flipHudPatchCount is 0 on CS:S (DE-TF2); keep storage valid

// 1 = window reads `want`, 0 = it reads the other state, -1 = neither / unreadable.
static int FlipPatchState(const uint8_t* at, const FlipHudPatch& p, bool want)
{
    __try {
        if (memcmp(at, FlipHudPatchBytes(p, want), p.len) == 0)  return 1;
        if (memcmp(at, FlipHudPatchBytes(p, !want), p.len) == 0) return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static bool FlipPatchWrite(uint8_t* at, const FlipHudPatch& p, bool mirrored)
{
    const unsigned char* bytes = FlipHudPatchBytes(p, mirrored);
    bool ok = false;
    __try {
        DWORD old = 0;
        if (VirtualProtect(at, p.len, PAGE_EXECUTE_READWRITE, &old)) {
            for (int i = 0; i < p.len; ++i) if (at[i] != bytes[i]) at[i] = bytes[i];
            VirtualProtect(at, p.len, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, p.len);
            ok = true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static void FlipHudPatchesResolve()
{
    const HMODULE client = GetModuleHandleA("client.dll");
    int found = 0;
    for (int i = 0; i < k_flipHudPatchCount; ++i) {
        const FlipHudPatch& p = k_flipHudPatches[i];
        uint8_t* hit = client ? PatternScan(client, p.sig) : nullptr;
        uint8_t* at  = hit ? hit + p.at : nullptr;
        if (at && FlipPatchState(at, p, false) < 0) at = nullptr;   // neither shipped nor patched: refuse
        s_patch[i] = { at, false };
        if (at) ++found;
        else DebugLog("flipworld: HUD patch '%s' NOT FOUND -- that element stays unmirrored", p.what);
    }
    DebugLog("flipworld: %d/%d angle-placed HUD patches resolved", found, k_flipHudPatchCount);
}

// vgui paint and think, the code these bytes live in, run on the game thread and nowhere else. The core
// only calls this from the game thread, or after the game thread has not ticked for a second.
static void FlipHudPatchesSet(bool mirrored)
{
    for (int i = 0; i < k_flipHudPatchCount; ++i) {
        FlipPatchSite& s = s_patch[i];
        if (!s.at || s.mirrored == mirrored) continue;
        const FlipHudPatch& p = k_flipHudPatches[i];
        const int st = FlipPatchState(s.at, p, mirrored);
        if (st < 0 || (st == 0 && !FlipPatchWrite(s.at, p, mirrored))) {
            DebugLog("flipworld: HUD patch '%s' could not be %s -- dropped", p.what, mirrored ? "applied" : "restored");
            s.at = nullptr;
            continue;
        }
        s.mirrored = mirrored;
    }
}

// ---- the engine side of FlipCoreTick ----------------------------------------------------------------

static ConVar* s_cvYaw     = nullptr;
static ConVar* s_cvFlipVM  = nullptr;
static int     s_offFlipVM = 0;         // DT_TFPlayer m_bFlipViewModels: 0 unresolved, -1 missing

using WriteVars_t = void(__fastcall*)(void*, void*, bool);
static WriteVars_t o_FlipWriteVars = nullptr;

// TF2 networked CTFPlayer::m_bFlipViewModels (DT_TFPlayer RecvPropBool) so spectators/demos
// saw the correct hand. CS:S has no such netvar (grepped css-community-master: no FlipViewModel/
// cl_flip_viewmodels anywhere) -- flip viewmodels is local-render-only here, there is nothing to
// read off another player. UNVERIFIED for CSS: this still answers "the LOCAL client's own flip
// cvar", never another player's hand. -1 = unknown.
static int FlipNetworkedHand()
{
    if (s_offFlipVM == 0) {
        if (RecvTable* t = FindNetworkTable("DT_CSPlayer")) {      // null = tables not up yet, retry
            const int off = FindPropOffset(t, "m_bFlipViewModels");
            s_offFlipVM = off > 0 ? off : -1;
            DebugLog("flipworld: m_bFlipViewModels offset 0x%X", s_offFlipVM);
        }
    }
    if (s_offFlipVM <= 0) return -1;
    const int idx = GetLocalPlayerIndex();
    void* p = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!p) return -1;
    int v = -1;
    __try { v = *reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(p) + s_offFlipVM) ? 1 : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}

static bool FlipEngineBool(int slot)
{
    bool r = false;
    __try { r = VCall<bool>(g_engine, slot); } __except(EXCEPTION_EXECUTE_HANDLER) { r = false; }
    return r;
}

// m_pszString of a cvar whose string is a lone digit, else null.
static char* FlipCvarString(ConVar* cv)
{
    char* s = nullptr;
    __try {
        s = *reinterpret_cast<char**>(ConVarParent(cv) + cvString);
        if (s && (s[0] == 0 || s[1] != 0)) s = nullptr;   // touch both bytes here, under SEH
    } __except(EXCEPTION_EXECUTE_HANDLER) { s = nullptr; }
    return s;
}

static void FlipSetStringDigit(char* s, char digit)
{
    __try { s[0] = digit; } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The hand m_pszString holds (what the server and config.cfg see). m_nValue is poked while held, so it is
// never the source; -1 = unreadable, the caller falls back to it.
static int FlipCvarStringHand(ConVar* cv)
{
    int h = -1;
    __try {
        if (const char* s = *reinterpret_cast<char**>(ConVarParent(cv) + cvString)) h = atof(s) != 0.0 ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { h = -1; }
    return h;
}

static int FlipCvarInt(ConVar* cv)
{
    int v = 0;
    __try { v = *reinterpret_cast<const int*>(ConVarParent(cv) + cvInt); } __except(EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

struct FlipEnv {
    bool TakeOwner()
    {
        if (!s_shared) return false;
        const LONG was = InterlockedCompareExchange(&s_shared->owner, k_flipToken, 0);
        if (was == 0 || was == k_flipToken) { DebugLog("flipworld: this DLL owns flip world"); return true; }
        static uint64_t s_nextLog = 0;
        if (GetTickCount64() >= s_nextLog) {
            s_nextLog = GetTickCount64() + 60000;
            DebugLog("flipworld: another DLL in this process owns flip world -- this copy stays off");
        }
        return false;
    }
    void DropOwner()
    {
        if (!s_shared) return;
        InterlockedExchange(&s_shared->frameMirrored, 0);
        InterlockedCompareExchange(&s_shared->owner, 0, k_flipToken);
        DebugLog("flipworld: flip world ownership released");
    }
    bool  HasYaw()           { return s_cvYaw != nullptr; }
    float Yaw()              { return s_cvYaw->GetFloat(); }
    void  SetYaw(float v)    { s_cvYaw->ForceFloat(v); }   // never the string: config.cfg keeps the player's
    void  SetHudPatches(bool on) { FlipHudPatchesSet(on); }
    bool  HasHand()          { return s_cvFlipVM != nullptr; }
    int   Hand()             { const int h = FlipCvarStringHand(s_cvFlipVM); return h >= 0 ? h : (FlipCvarInt(s_cvFlipVM) ? 1 : 0); }
    int   ClientHand()       { return FlipCvarInt(s_cvFlipVM); }
    // Raw int + float only (ConVar::ForceInt): no callback, so the server keeps hearing the string.
    void  SetClientHand(int value)
    {
        s_cvFlipVM->ForceInt(value);
        DebugLog("flipworld: cl_flipviewmodels client int -> %d (string %d)", value, Hand());
    }
    // SetValue, not a raw write: engine.dll's global cvar change callback is what sends FCVAR_USERINFO to
    // the server (NET_SetConVar), and the console's FCVAR_NOT_CONNECTED refusal never sees this path.
    bool SetHand(int value, int cur, int prefAfter, int target)
    {
        const bool ok = s_cvFlipVM->SetValueInt("cl_flipviewmodels", value);
        static bool s_failLogged = false;
        if (ok || !s_failLogged) {
            DebugLog("flipworld: cl_flipviewmodels %d -> %d (%s, player's own %d)%s", cur, value,
                     prefAfter < 0 ? "released" : target < 0 ? "hold, live" : "hold, demo's recorded hand",
                     prefAfter >= 0 ? prefAfter : value, ok ? "" : " SETVALUE FAILED, retrying");
            s_failLogged = !ok;
        }
        return ok;
    }
    bool IsHLTV()       { return FlipEngineBool(engIsHLTV); }
    bool IsDemo()       { return FlipEngineBool(engIsPlayingDemo); }
    int  RecordedHand() { return FlipNetworkedHand(); }
    // Eject whose game thread never released the hand: the hold (and the server's copy) stays for this
    // session, but the string -- the one thing config.cfg reads -- gets the player's own digit now,
    // because the config writer hook is about to come off.
    void SaveOwnHandForConfig(int own)
    {
        if (char* s = FlipCvarString(s_cvFlipVM)) FlipSetStringDigit(s, own ? '1' : '0');
        DebugLog("flipworld: eject could not release cl_flipviewmodels on the game thread -- it stays at %d "
                 "for this session, config.cfg will save the player's own %d", own ? 0 : 1, own);
    }
};

static void FlipRunTick(bool onGameThread)
{
    const uint64_t now = GetTickCount64();
    if (onGameThread) {
        s_gameTid.store(GetCurrentThreadId());
        s_lastTickMs.store(now);
        if (g_cvar) {
            if (!s_cvYaw)    s_cvYaw    = g_cvar->FindVarWalk("m_yaw");
            if (!s_cvFlipVM) s_cvFlipVM = g_cvar->FindVarWalk("cl_flipviewmodels");
        }
    }
    const uint64_t last = s_lastTickMs.load();
    const bool wanted = onGameThread && FlipWanted();
    const FlipCoreIn in = { wanted, o_FlipWriteVars != nullptr, onGameThread,
                            !onGameThread && (last == 0 || now - last >= 1000), g_wdFlipGunOtherSide };
    FlipLock _l;
    FlipEnv env;
    FlipCoreTick(s_core, env, in);
    s_active = wanted && s_core.owner;
    FlipPublish();
}

void FlipWorld_Tick()
{
    FlipRunTick(true);
}

// Eject: the FSN g_eject branch runs it on the game thread; thread.cpp's own call is the fallback.
void FlipWorld_Release()
{
    FlipRunTick(OnGameThread());
}

bool FlipWorld_Holding()
{
    FlipLock _l;
    return FlipHolding(s_core);
}

// After every hook is off and drained: the token must not outlive this DLL.
void FlipWorld_Unload()
{
    if (!s_shared) return;
    InterlockedCompareExchange(&s_shared->owner, 0, k_flipToken);
    if (s_core.owner) InterlockedExchange(&s_shared->frameMirrored, 0);
    UnmapViewOfFile(s_shared);
    CloseHandle(s_sharedMap);
    s_shared = nullptr; s_sharedMap = nullptr;
}

using LevelFn_t = void(__fastcall*)(void*);
static LevelFn_t o_FlipLevelInit     = nullptr;
static LevelFn_t o_FlipLevelShutdown = nullptr;

static void __fastcall h_FlipLevelInit(void* rcx)
{
    MiscHookGuard _g;
    o_FlipLevelInit(rcx);
    if (!g_eject) FlipLevelInit();
    // Chams (visuals.cpp) piggybacks this same CViewRender::LevelInit -- its material needs
    // re-finding every map too, and this is the one hook already on that vtable slot.
    if (!g_eject) Chams_LevelInit();
    // Glow (visuals.cpp) rides the same hook for the same reason.
    if (!g_eject) Glow_LevelInit();
    // unit E5: old map's steamids are stale -- drop the avatar cache next Avatar_Tick.
    if (!g_eject) Avatar_OnMapChange();
    if (!g_eject) g_botox_net.on_level_init();   // botox level_init_pre_entity.cpp:25
}

// Map change and disconnect. FRAME_RENDER_START does not tick through a map load, so without this
// the hold would ride straight into the next map.
static void __fastcall h_FlipLevelShutdown(void* rcx)
{
    MiscHookGuard _g;
    if (!g_eject && OnGameThread()) {
        s_flipReady = false;
        FlipRunTick(true);
    }
    if (OnGameThread()) WorldFx3_LevelShutdown();   // lamp list belongs to the old map
    Chams_LevelUnload();
    Glow_LevelUnload();
    Web_LevelShutdown();   // botox level_shutdown.cpp:27
    o_FlipLevelShutdown(rcx);
}

// ---- CCvarUtilities::WriteVariables (engine, config.cfg) ------------------------------------------
// FlipCfgWrite (flipmath.h): while a hand is held, config.cfg gets the player's own digit and
// everything else keeps reading the held one.

static void __fastcall h_FlipWriteVars(void* rcx, void* buffer, bool allVars)
{
    MiscHookGuard _g;
    int pref = -1;
    { FlipLock _l; pref = s_core.vmPref; }
    char* s = (pref >= 0 && s_cvFlipVM) ? FlipCvarString(s_cvFlipVM) : nullptr;
    FlipCfgWrite(s, pref, [&] { o_FlipWriteVars(rcx, buffer, allVars); });
}

// ---- IVRenderView::Push2DView: the 3D pass is over ----------------------------------------------------

static void __fastcall h_FlipPush2D(void* rcx, const void* view, int flags, void* rt, void* frustum)
{
    MiscHookGuard _g;
    if (OnGameThread()) {
        FlipLock _l;
        FlipTrackTwoD(s_core.track);
        FlipPublish();
    }
    o_FlipPush2D(rcx, view, flags, rt, frustum);
}

// ---- CalcViewModelLag: weapon sway under the mirror (FlipMirrorSway) -------------------------
// UNVERIFIED 11-08: this byte pattern is TF2's CTFViewModel::CalcViewModelLag override. sig_check.md:75
// claiming a 1/1 CS:S hit is stale/wrong -- debug.log shows "pattern NOT FOUND" at inject, and it
// cannot be the same compiled body: CS:S's CalcViewModelLag is CBaseViewModel::CalcViewModelLag
// (css-community-master game\shared\baseviewmodel_shared.cpp:467, guarded by #if CSTRIKE_DLL, called
// from CalcViewModelView at line 404), a different, still-virtual function with a different
// implementation (two AngleVectors calls + VectorMA chain, no TF2-specific override). Re-deriving a
// CS:S-correct signature needs real disassembly (string/constant xref or vtable-slot RE against the
// live client.dll) -- attempted 11-08 via a float-constant scan (0.035f/0.03f/0.02f used in the
// function's last 3 VectorMA terms) and found no unique match in the time available: those constants
// are reused elsewhere in the binary and the pdata function-range scan turned up only false positives
// (unrelated code, e.g. sound/particle helpers). Left as a no-op failure (InstallPatternHook already
// just logs NOT FOUND and skips the hook -- no crash) rather than guess a slot. Cosmetic-only: without
// it, viewmodel sway simply doesn't mirror; the world/HUD mirror itself does not depend on this hook.
// NEEDS FROM OTHERS: nothing -- this file owns it; flagging as a follow-up RE task.

static constexpr const char* k_flipSwaySig =
    "48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 80 00 00 00 48 8B F1 49 8B F8 48 8B 0D ? ? ? ? 48 8B EA 48 8B 01 FF 50 70 84 C0 0F 85 ? ? ? ? 48 8B 05 ? ? ? ? 0F 57 C0 0F 2F 40 54";
using VmLag_t = void(__fastcall*)(void*, float*, float*, float*);
static VmLag_t o_FlipVmLag = nullptr;

__declspec(noinline) static bool FlipReadVec3(const float* p, float out[3])
{
    __try { out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; return true; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

__declspec(noinline) static void FlipSwayAt(const float before[3], float* origin, const float* angles)
{
    __try { FlipMirrorSway(before, origin, angles); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void __fastcall h_FlipVmLag(void* rcx, float* origin, float* angles, float* original)
{
    MiscHookGuard _g;
    float before[3];
    const bool mirror = !g_eject && FlipWorld_Mirroring() && origin && angles && FlipReadVec3(origin, before);
    o_FlipVmLag(rcx, origin, angles, original);
    if (mirror) FlipSwayAt(before, origin, angles);
    static bool s_was = false;
    if (s_was != mirror) {
        s_was = mirror;
        DebugLog("flipworld: weapon sway %s", mirror ? "mirrored" : "normal");
    }
}

// ---- ClientModeShared::CreateMove ---------------------------------------------------------------

using FlipCreateMove_t = bool(__fastcall*)(void*, float, void*);
static FlipCreateMove_t o_FlipCreateMove = nullptr;

static void FlipMirrorCmdAt(void* pCmd)   // SEH leaf (C2712)
{
    __try {
        const uintptr_t p = reinterpret_cast<uintptr_t>(pCmd);
        FlipMirrorCmd(*reinterpret_cast<float*>(p + cmdSidemove), *reinterpret_cast<int*>(p + cmdButtons));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// BEFORE the original: ClientModeShared::CreateMove runs ahead of CInput::CreateMove (which
// checksums the command) and CHLClient::CreateMove (movement.cpp), so mirroring sidemove/buttons
// here is what every later reader, including the net channel, ends up seeing.
static bool __fastcall h_FlipCreateMove(void* rcx, float flInputSampleTime, void* pCmd)
{
    MiscHookGuard _g;
    if (!g_eject) {
        FlipWorld_Tick();
        if (pCmd && FlipWorld_Mirroring()) FlipMirrorCmdAt(pCmd);
    }
    return o_FlipCreateMove(rcx, flInputSampleTime, pCmd);
}

// ---- IVEngineClient::WorldToScreenMatrix --------------------------------
// Game thread (HUD paint, the 3D pass) and render thread (dih's world overlays) both call it: one
// copy per thread, read by the caller straight away just as it would read the engine's own.

using FlipW2S_t = const float*(__fastcall*)(void*);
static FlipW2S_t o_FlipW2S = nullptr;

__declspec(noinline) static bool FlipCopyMatrix(float dst[4][4], const float* src)
{
    __try { memcpy(dst, src, sizeof(float) * 16); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static const float* __fastcall h_FlipW2S(void* rcx)
{
    MiscHookGuard _g;
    const float* m = o_FlipW2S(rcx);
    if (!m || !FlipW2SMirrored(FlipWorld_Mirroring(), OnGameThread(), s_pubInWorldPass.load())) return m;

    thread_local float t[4][4];
    if (!FlipCopyMatrix(t, m)) return m;
    if (!FlipIsProjection(t)) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("flipworld: WorldToScreenMatrix returned a non-projection matrix -- HUD left unmirrored"); }
        return m;
    }
    FlipMirrorW2S(t);
    return t[0];
}

// ---- install --------------------------------------------------------------------

void FlipWorld_Install()
{
    FlipSharedOpen();

    InstallVTableHook(g_engine, engWorldToScreenMatrix, "flipworld: WorldToScreenMatrix",
                      (void*)h_FlipW2S, (void**)&o_FlipW2S);
    InstallVTableHook(g_renderView, rvPush2DView, "flipworld: IVRenderView::Push2DView",
                      (void*)h_FlipPush2D, (void**)&o_FlipPush2D);

    if (void* mode = ScreenFx_FindClientMode())
        SwapVTableHook(mode, cmCreateMove, "flipworld: ClientModeShared::CreateMove",
                       (void*)h_FlipCreateMove, (void**)&o_FlipCreateMove);

    // CCvarUtilities::WriteVariables(CUtlBuffer&, bool): `%s "%s"\n` of every FCVAR_ARCHIVE cvar.
    InstallPatternHook(GetModuleHandleA("engine.dll"), "flipworld: config writer",
                       "40 57 41 54 41 56 41 57 48 83 EC 48 48 8B 0D ? ? ? ? 45 33 E4 4C 89 A4 24 88 00 00 00 45 0F B6 F0 4C 8B FA",
                       (void*)h_FlipWriteVars, (void**)&o_FlipWriteVars);

    InstallPatternHook(GetModuleHandleA("client.dll"), "flipworld: CalcViewModelLag",
                       k_flipSwaySig, (void*)h_FlipVmLag, (void**)&o_FlipVmLag);

    FlipHudPatchesResolve();

    // g_pViewRender off ref's call-site sig (proven in fpsoverlay): mov rcx,[rip+disp32], +7.
    HMODULE hClient = GetModuleHandleA("client.dll");
    void* view = nullptr;
    if (uint8_t* at = hClient ? PatternScan(hClient, "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 03 48 8D 54 24") : nullptr) {
        __try {
            const int32_t disp = *reinterpret_cast<const int32_t*>(at + 3);
            view = *reinterpret_cast<void**>(at + 7 + disp);
        } __except(EXCEPTION_EXECUTE_HANDLER) { view = nullptr; }
    }
    if (reinterpret_cast<uintptr_t>(view) < 0x10000) {
        DebugLog("flipworld: IViewRender not found -- no RT, flip world stays off");
        return;
    }
    InstallVTableHook(view, vrLevelInit, "flipworld: CViewRender::LevelInit",
                      (void*)h_FlipLevelInit, (void**)&o_FlipLevelInit);
    InstallVTableHook(view, vrLevelShutdown, "flipworld: CViewRender::LevelShutdown",
                      (void*)h_FlipLevelShutdown, (void**)&o_FlipLevelShutdown);
    DebugLog("flipworld: RT builds lazily on first use (FlipWorld_Render) -- no map reload needed after a mid-map inject");
}
