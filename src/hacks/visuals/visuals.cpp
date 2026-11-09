#include "visuals.h"
#include "../../hooks/crashlog.h"   // CrashLog_Note: EMIT probe
#include "../movement/prediction.h"   // Vec3, EntF/EntI/EntV3/EntAlive, g_globalVars, TickInterval
#include "lagres.h"       // lag resolver: smoothed transform around the original SetupBones
#include "../screen/screenfx.h"     // FrameBlend_CollectBones -- the gun's bones, taken at the draw
#include "viewmodel_fps.h"          // g_vmFps.Apply -- low fps viewmodel, swaps the gun's bones
#include "flipmath.h"     // flipslot:: -- IMaterialSystem FindMaterial/DecRef/IsErrorMaterial slots
#include "../esp/esp_fx.h"   // Unit C2: sound-ping ring, written from h_EmitSound below
#include "chams_api.h"    // 10-31: chams + glow passes (aim/chams.cpp)
#include "lsd_chams.h"    // WeaponSheen_Begin/End -- botox weapon_sheen.cpp layer
#include "world_fx.h"     // WorldFx_Tick -- unit D: fullbright + disable interp pokes
#include "world_fx2.h"    // WorldFx2_Tick/_OnFindMaterial -- unit D2: dlight, sun angle, tints
#include "mc_world.h"     // MCWorld_OnSkyBoxDrawn -- minecraft sky behind the 2D sky draw
#include "../misc/game_extras.h"   // ChatTranslator_OnUserMessage -- unit game, DispatchUserMessage
#include "../misc/game_pure.h"     // n_game::SayMsg/ParseSayText(2)/PickChatLine -- pure parsing, no SDK types
#include "../aim/css_bridge.h"     // CssBridge_DispatchChat (SDK2/BRIDGE 11-03)
#include <intrin.h>       // _ReturnAddress -- net_graph must still read the real lerp

// ConVar-string redirect scratch, file-local because nothing outside this file has ever
// touched it. It lived in dllmain.cpp/globals.h next to the real settings, which is how the
// g_offMins duplicate-definition bug got in -- one owner per symbol.
//   *Buf     -- the buffer the ConVar's m_pszString is pointed at
//   *OrigPtr -- the engine's own string, put back when the feature goes off
static char  s_fogColorBuf[32]     = {};
static char* s_fogColorOrigPtr     = nullptr;
static int   s_fogColorOrigLen     = 0;
static char  s_fogColorSkyBuf[32]  = {};
static char* s_fogColorSkyOrigPtr  = nullptr;
static int   s_fogColorSkyOrigLen  = 0;
static char  s_skyNameBuf[128]     = {};

// Weather + no-hats scratch, same rule: this file is the only reader. thread.cpp asks
// Precip_IsLive() at eject instead of reaching for the two entity pointers.
static void* s_precipClass        = nullptr;
static void* s_precipEntity       = nullptr;
static void* s_precipNetworkable  = nullptr;
static int   s_offPrecipType      = -1;
static int   s_lastAppliedMode    = -99;

// ==== Interface + CVar helpers ==================================================
void* GetInterface(const char* module, const char* name) {
    HMODULE mod = GetModuleHandleA(module);
    if (!mod) return nullptr;
    using CI_t = void*(*)(const char*, int*);
    auto ci = (CI_t)GetProcAddress(mod, "CreateInterface");
    return ci ? ci(name, nullptr) : nullptr;
}

void CVar_ForceFloat(ConVar* cv, float val) {
    if (cv) cv->ForceFloat(val);
}

void CVar_ForceInt(ConVar* cv, int val) {
    if (cv) cv->ForceInt(val);
}

// ==== Per-tick cvar pushes ======================================================
void UpdateVisualsCvars()
{
    if (!g_cvar) return;

    // sv_cheats: force 1 while fog or cheats-bypass needs it, restore 0 once on release.
    // CS:S has no Valve matchmaking (no competitive/casual queue like CS:GO) -- botox csgo-sdk's
    // own sv_cheats bypass (globals/convars/convars.cpp: strip FCVAR_CHEAT off the ConCommandBase
    // list, unconditional, no server-type gate) is the reference; this force-to-1 path matches
    // that unconditionally too, same as any community CS:S server.
    {
        static CachedCVar sv_cheats;
        static bool       s_wasOn = false;
        bool wantOn = (g_cheatsBypass || g_fogEnable);
        if (wantOn)          CVar_ForceInt(sv_cheats.Get("sv_cheats"), 1);
        else if (s_wasOn)    CVar_ForceInt(sv_cheats.Get("sv_cheats"), 0);
        s_wasOn = wantOn;
    }

    // remove ragdolls: cl_ragdoll_physics_enable off (perf -- csgo-sdk frame_stage_notify.cpp:152,
    // verified real convar: c_cs_player.cpp:56 "Enable/disable ragdoll physics."). Visuals themselves
    // are skipped in h_DrawModelExecute_impl; this just stops simulating what never draws.
    {
        static CachedCVar cc_ragdoll_physics;
        static bool       s_forced = false;
        ConVar* cv = cc_ragdoll_physics.Get("cl_ragdoll_physics_enable");
        if (g_removeRagdolls && !g_eject) { CVar_ForceInt(cv, 0); s_forced = true; }
        else if (s_forced)                { CVar_ForceInt(cv, 1); s_forced = false; }
    }

    // fog_override + fog params
    {
        static CachedCVar cc_fog_override, cc_fog_enable, cc_fog_enableskybox;
        static CachedCVar cc_fog_start, cc_fog_end, cc_fog_startskybox, cc_fog_endskybox;
        static CachedCVar cc_fog_maxdensity, cc_fog_maxdensityskybox;
        static CachedCVar cc_fog_color, cc_fog_colorskybox;
        ConVar* cv_fog_override         = cc_fog_override.Get("fog_override");
        ConVar* cv_fog_enable           = cc_fog_enable.Get("fog_enable");
        ConVar* cv_fog_enableskybox     = cc_fog_enableskybox.Get("fog_enableskybox");
        ConVar* cv_fog_start            = cc_fog_start.Get("fog_start");
        ConVar* cv_fog_end              = cc_fog_end.Get("fog_end");
        ConVar* cv_fog_startskybox      = cc_fog_startskybox.Get("fog_startskybox");
        ConVar* cv_fog_endskybox        = cc_fog_endskybox.Get("fog_endskybox");
        ConVar* cv_fog_maxdensity       = cc_fog_maxdensity.Get("fog_maxdensity");
        ConVar* cv_fog_maxdensityskybox = cc_fog_maxdensityskybox.Get("fog_maxdensityskybox");
        ConVar* cv_fog_color            = cc_fog_color.Get("fog_color");
        ConVar* cv_fog_colorskybox      = cc_fog_colorskybox.Get("fog_colorskybox");

        CVar_ForceInt(cv_fog_override, g_fogEnable ? 1 : 0);

        // Redirect ConVar m_pszString to our 32-byte buffer, write "R G B" each tick.
        auto WriteFogColorStr = [](ConVar* cv, char* buf, char** origPtr, int* origLen, const char* val)
        {
            if (!val) return;
            ConVarStr s = ConVarStrSlot(cv);
            if (!s.ppStr) return;
            __try {
                if (!*origPtr && *s.ppStr) {
                    *origPtr = *s.ppStr;
                    *origLen = *s.pLen;
                }
                strncpy(buf, val, 31);
                buf[31] = '\0';
                *s.ppStr = buf;
                *s.pLen  = 32;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        };
        auto RestoreFogColorStr = [](ConVar* cv, char** origPtr, int* origLen)
        {
            if (!*origPtr) return;
            ConVarStr s = ConVarStrSlot(cv);
            if (s.ppStr) {
                __try {
                    *s.ppStr = *origPtr;
                    *s.pLen  = *origLen;
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
            *origPtr = nullptr;
        };

        static bool s_fogWasEnabled = false;
        if (g_fogEnable)
        {
            CVar_ForceInt(cv_fog_enable,       1);
            CVar_ForceInt(cv_fog_enableskybox, 1);
            CVar_ForceFloat(cv_fog_start,            g_fogStart);
            CVar_ForceFloat(cv_fog_end,              g_fogEnd);
            CVar_ForceFloat(cv_fog_startskybox,      g_fogStart);
            CVar_ForceFloat(cv_fog_endskybox,        g_fogEnd);
            CVar_ForceFloat(cv_fog_maxdensity,       g_fogDensity);
            CVar_ForceFloat(cv_fog_maxdensityskybox, g_fogDensity);

            char colorStr[32];
            snprintf(colorStr, sizeof(colorStr), "%d %d %d",
                (int)(g_fogColor[0] * 255.f), (int)(g_fogColor[1] * 255.f), (int)(g_fogColor[2] * 255.f));
            WriteFogColorStr(cv_fog_color,       s_fogColorBuf,    &s_fogColorOrigPtr,    &s_fogColorOrigLen,    colorStr);
            WriteFogColorStr(cv_fog_colorskybox, s_fogColorSkyBuf, &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen, colorStr);
            s_fogWasEnabled = true;
        }
        else if (s_fogWasEnabled)
        {
            RestoreFogColorStr(cv_fog_color,       &s_fogColorOrigPtr,    &s_fogColorOrigLen);
            RestoreFogColorStr(cv_fog_colorskybox, &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen);
            s_fogWasEnabled = false;
        }
    }

    // viewmodel sway
    {
        static CachedCVar cc_sway_scale, cc_sway_interp;
        ConVar* cv_sway_scale  = cc_sway_scale.Get("cl_wpn_sway_scale");
        ConVar* cv_sway_interp = cc_sway_interp.Get("cl_wpn_sway_interp");
        CVar_ForceFloat(cv_sway_scale,  g_vmSwayEnable ? g_vmSwayScale  : 0.f);
        CVar_ForceFloat(cv_sway_interp, g_vmSwayEnable ? g_vmSwayInterp : 0.f);
    }

    // remove viewmodel bob
    {
        static CachedCVar cc_bobcycle, cc_bobup;
        static bool       s_lastBob = false;
        ConVar* cv_bobcycle = cc_bobcycle.Get("cl_bobcycle");
        ConVar* cv_bobup    = cc_bobup.Get("cl_bobup");

        // bob changer = CS:GO bob replaces CSS's (vm_csgo_hook.inl); cl_bobup 0 = CSS bob off (weapon_csbase.cpp:1396)
        const bool killNative = g_vmRemoveBob || g_vmCsBob;
        if (killNative) {
            CVar_ForceFloat(cv_bobcycle, 999.f);
            CVar_ForceFloat(cv_bobup,    0.f);
        } else if (s_lastBob) {
            CVar_ForceFloat(cv_bobcycle, 0.8f);
            CVar_ForceFloat(cv_bobup,    0.5f);
        }
        s_lastBob = killNative;
    }

    // NOTE: the game "crosshair" convar is owned solely by ChSuppressGameCrosshair
    // (crosshair.cpp). This block used to force it to g_chEnable ? 0 : 1 every 100ms,
    // which (a) fought that pin every 100ms whenever only the CSGO crosshair was on,
    // and (b) forced crosshair back to 1 for players who run it at 0 with no feature
    // of ours enabled at all. One writer only.

    WorldFx_Tick();   // unit D: fullbright + disable interp pokes, same 100ms tick
    WorldFx2_Tick();  // unit D2: dlight + sun angle, same 100ms tick
}

// ==== Net stats ==================================================================
#define FLOW_OUTGOING 0
#define FLOW_INCOMING 1

static int g_netInfoIndex = -1;

static void DetectNetChannelIndex() {
    if (!g_engine) return;
    int candidates[] = {72,74,76,78,80,81,82,84};
    for (int idx : candidates) {
        __try {
            auto nci = VCall<INetChannelInfo*>(g_engine, idx);
            if (!nci) continue;
            float test = VCall<float>(nci, 9, 0);
            if (test >= 0.f && test < 5.f) { g_netInfoIndex = idx; break; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void UpdateNetStats() {
    if (!g_engine) return;

    bool inGame = false;
    __try { inGame = VCall<bool>(g_engine, 26); }
    __except(EXCEPTION_EXECUTE_HANDLER) { g_conn.store(false); return; }

    if (!inGame) { g_conn.store(false); return; }

    if (g_netInfoIndex < 0) {
        DetectNetChannelIndex();
        if (g_netInfoIndex < 0) return;
    }

    __try {
        auto nci = VCall<INetChannelInfo*>(g_engine, g_netInfoIndex);
        if (!nci) { g_conn.store(false); return; }

        float in   = VCall<float>(nci, 9, FLOW_INCOMING);
        float out  = VCall<float>(nci, 9, FLOW_OUTGOING);
        float loss = VCall<float>(nci, 11, FLOW_INCOMING); // GetAvgLoss, 0..1

        int rawPing = (int)((in + out) * 1000.f);
        g_ping.store(rawPing > k_pingSubtract ? rawPing - k_pingSubtract : 0);
        g_loss.store((int)(loss * 100.f));
        g_conn.store(true);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_netInfoIndex = -1;
        g_conn.store(false);
    }
}

void UpdateFPS() {
    static LARGE_INTEGER freq{}, last{};
    static int frames = 0;
    static float acc = 0.f;

    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (last.QuadPart) {
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        acc += dt;
        frames++;
        if (acc >= 0.25f) {
            g_fps.store((float)frames / acc);
            frames = 0;
            acc    = 0.f;
        }
    }
    last = now;
}

// ==== Eject restore ================================================================
void ResetFogCvars()
{
    if (!g_cvar) return;
    auto RestorePtr = [](const char* name, char** origPtr, int* origLen) {
        if (!*origPtr) return;
        ConVarStr s = ConVarStrSlot(g_cvar->FindVarWalk(name));
        if (s.ppStr) {
            __try {
                *s.ppStr = *origPtr;
                *s.pLen  = *origLen;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        *origPtr = nullptr;
    };
    RestorePtr("fog_color",       &s_fogColorOrigPtr,    &s_fogColorOrigLen);
    RestorePtr("fog_colorskybox", &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen);
    CVar_ForceInt(g_cvar->FindVarWalk("fog_override"),     0);
    CVar_ForceInt(g_cvar->FindVarWalk("fog_enable"),       0);
    CVar_ForceInt(g_cvar->FindVarWalk("fog_enableskybox"), 0);
    CVar_ForceInt(g_cvar->FindVarWalk("sv_cheats"),        0);
    // "crosshair" is restored by Crosshair_Restore() -- forcing 1 here overwrote the
    // player's own value on every eject.
}

// NO eject restore for sv_skyname, and none is needed: h_R_DrawSkyBox is stateless per draw
// (SkyInstall then SkyRestore around the original), and eject drains g_miscHookDepth to zero
// before it tears anything down, so the restore has always run by then. RestoreSkyNamePtr and
// its four saved-pointer globals used to sit here; nothing ever wrote them, so it was a no-op
// call at eject. Do not re-add a cross-frame save either -- a map change reassigns
// m_pszString, so a stale "original" written back at eject is worse than doing nothing.

// ==== Skybox hook ====================================================================
R_DrawSkyBox_t o_R_DrawSkyBox = nullptr;

// Snapshot of engine's sv_skyname string slot, so we can restore it post-draw.
struct SkySave { char** ppStr; int* pLen; char* savedPtr; int savedLen; };

// Save engine's current sv_skyname, overwrite with g_skyName. SEH kept in its own
// function (h_R_DrawSkyBox has an unwind object -> can't host __try, C2712).
__declspec(noinline) static SkySave SkyInstall()
{
    ConVarStr slot = ConVarStrSlot(g_cv_sv_skyname);
    SkySave s{ slot.ppStr, slot.pLen, nullptr, 0 };
    if (!s.ppStr) return s;
    __try {
        s.savedPtr = *s.ppStr;
        s.savedLen = *s.pLen;

        strncpy(s_skyNameBuf, g_skyName, sizeof(s_skyNameBuf) - 1);
        s_skyNameBuf[sizeof(s_skyNameBuf) - 1] = '\0';
        *s.ppStr = s_skyNameBuf;
        *s.pLen  = sizeof(s_skyNameBuf);
    } __except(EXCEPTION_EXECUTE_HANDLER) { s.ppStr = nullptr; }
    return s;
}

__declspec(noinline) static void SkyRestore(const SkySave& s)
{
    if (!s.ppStr) return;
    __try {
        *s.ppStr = s.savedPtr;
        *s.pLen  = s.savedLen;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Stateless per-frame: save engine's current sv_skyname string, install ours,
// call original, restore engine's. No cross-frame caching -> survives map change
// (map load reassigns m_pszString; we re-read + restore fresh every draw).
void __fastcall h_R_DrawSkyBox(float zFar, int nDrawFlags)
{
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    if (!g_cv_sv_skyname || g_eject || !g_skyEnable)
        o_R_DrawSkyBox(zFar, nDrawFlags);
    else
    {
        SkySave s = SkyInstall();
        o_R_DrawSkyBox(zFar, nDrawFlags);
        SkyRestore(s);
    }
    // minecraft sky: right behind this view's 2D sky draw (botox mc_clouds' point), z-tested so
    // it only lands where nothing opaque is, whichever side of the world the engine draws sky
    if (!g_eject && g_mcWorldEnable) MCWorld_OnSkyBoxDrawn();
}

// ==== CS:GO viewmodel bob / gun lower / landing dip =====================================
#define VMCS_REMOVE_BOB g_vmRemoveBob
#include "vm_csgo_hook.inl"
void VmCsgo_Install(HMODULE client) { VmCs_Install(client); }

// ==== Viewmodel offset hook ============================================================
CalcViewModelView_t o_CalcViewModelView = nullptr;

__declspec(noinline) static bool CalcViewModel_Copy(const void* pEyePosition, const void* pEyeAngles,
                                                    float eye[3], float ang[3])
{
    __try {
        memcpy(eye, pEyePosition, sizeof(float) * 3);
        memcpy(ang, pEyeAngles, sizeof(float) * 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Writes eye+offset into out[3]. Never touches the caller's Vector -- see h_ below.
__declspec(noinline) static bool CalcViewModel_ApplyOffset(const void* pEyePosition,
                                                           const void* pEyeAngles,
                                                           float out[3])
{
    __try {
        constexpr float kDeg2Rad = 3.14159265f / 180.f;
        const float* ang = reinterpret_cast<const float*>(pEyeAngles);
        float pitch = ang[0], yaw = ang[1];

        float sy = sinf(yaw   * kDeg2Rad), cy = cosf(yaw   * kDeg2Rad);
        float sp = sinf(pitch * kDeg2Rad), cp = cosf(pitch * kDeg2Rad);

        float fwd[3]   = {  cp*cy,  cp*sy, -sp };
        float right[3] = {  sy,    -cy,     0.f };
        float up[3]    = {  sp*cy,  sp*sy,  cp  };

        const float* pos = reinterpret_cast<const float*>(pEyePosition);
        out[0] = pos[0] + right[0]*g_vmOffsetX + fwd[0]*g_vmOffsetY + up[0]*g_vmOffsetZ;
        out[1] = pos[1] + right[1]*g_vmOffsetX + fwd[1]*g_vmOffsetY + up[1]*g_vmOffsetZ;
        out[2] = pos[2] + right[2]*g_vmOffsetX + fwd[2]*g_vmOffsetY + up[2]*g_vmOffsetZ;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// CBasePlayer::CalcViewModelView loops MAX_VIEWMODELS(2) and hands EVERY viewmodel the
// SAME `const Vector& eyeOrigin` -- view.cpp's own local. Offsetting in place therefore
// leaked into the next iteration: vm0 (weapon) moved 1x, vm1 moved 2x. Spy's watch is
// vm1 (tf_weapon_invis.cpp: SetViewModelIndex(1)), so the watch drifted off the weapon --
// same for grenade/offhand v_models. Offset a COPY, pass the copy: one shift each, equal.
void __fastcall h_CalcViewModelView(void* rcx, void* owner, void* pEyePosition, void* pEyeAngles)
{
    if (g_eject) { o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles); return; }
    // ItemPostFrame (baseplayer_shared.cpp:233) calls this every predicted usercmd. Base skips bob
    // there (baseviewmodel_shared.cpp:398); stepping ours on tickbase curtime + old velocities
    // made the render frame reuse it = vm jitters up/down at tick rate. Render call only.
    if (g_prediction && reinterpret_cast<const unsigned char*>(g_prediction)[kPredOff_InPrediction])
    { o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles); return; }
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    // Round-3: no longer gated on o_VmCsBobHelper -- that hook targets TF2's compiled
    // CalcViewModelBobHelper, which doesn't exist in CS:S's client.dll (see vm_csgo_hook.inl
    // banner). Bob/lower/dip/shift are all computed inside VmCs_EyeAdjustSafe now, hook-free.
    const bool csAdjust = g_vmCsGunLower || g_vmCsLandDip || (g_vmCsBob && !g_vmRemoveBob) ||
                          g_wdVmShiftLeft > 0.f || g_wdVmShiftRight > 0.f;
    if ((g_vmOffsetEnable || csAdjust) && pEyePosition && pEyeAngles)
    {
        // CS:GO order: pitch/dip on the eye first, then the offset along the adjusted angles
        float eye[3], ang[3];
        if (CalcViewModel_Copy(pEyePosition, pEyeAngles, eye, ang))
        {
            if (csAdjust) VmCs_EyeAdjustSafe(owner, eye, ang);
            if (g_vmOffsetEnable) ang[2] += g_wdVmRoll;   // botox viewmodel_offset roll: the copy only
            float moved[3] = { eye[0], eye[1], eye[2] };
            if (!g_vmOffsetEnable || CalcViewModel_ApplyOffset(eye, ang, moved))
            {
                o_CalcViewModelView(rcx, owner, moved, ang);
                return;
            }
        }
    }

    o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles);
}

// ==== No hats ============================================================================
// THE MODEL-INDEX NAME MEMO IS DELETED, do not re-add. A model INDEX is not stable inside a
// map on TF2: cosmetics are DYNAMIC models, loaded and dropped as players come and go, and a
// freed slot is handed to the next model. It also cached FAILURES -- an index queried while
// its dynamic model was still loading resolved to nothing and stayed "nothing" for the rest
// of the map. Both faults show up the same way on screen: a hat that is hidden one moment and
// drawn the next, i.e. the flicker. Everything below keys on the model_t* instead (stable for
// as long as the model is loaded, and a different model is a different pointer), and NOTHING
// caches an unresolved name -- a failed resolve is simply retried next tick.

// IVModelInfo::GetModel = slot 1, GetModelName = slot 3.
static const void* ModelByIndex(int nModelIndex)
{
    if (!g_modelInfo || nModelIndex <= 0) return nullptr;
    const void* pModel = VCall<const void*>(g_modelInfo, 1, nModelIndex);
    return (reinterpret_cast<uintptr_t>(pModel) < 0x10000) ? nullptr : pModel;
}

// CS:S player models carry no attached cosmetic-item submodels (no TF2 "models/player/items"
// loadout layer -- CS skins are texture swaps on the same model), so there is nothing for a
// no-hats / hat-changer system to classify or hide. Removed with hats.cpp/hats_inv.cpp.

static const char* ModelNameOf(const void* pMdl)
{
    if (!g_modelInfo || reinterpret_cast<uintptr_t>(pMdl) < 0x10000) return nullptr;
    const char* n = VCall<const char*>(g_modelInfo, 3, pMdl);
    return (reinterpret_cast<uintptr_t>(n) < 0x10000) ? nullptr : n;
}

// Resolve the name at most once, and only if something asks. Every memo in this hook keys on
// model_t*, so a cache hit never needs the string. Trivially destructible on purpose: a local
// with a destructor cannot share a function with __try (C2712).
struct LazyModelName
{
    const void* mdl  = nullptr;
    const char* name = nullptr;
    bool        done = false;
    const char* Get() { if (!done) { done = true; name = ModelNameOf(mdl); } return name; }
};

// h_DrawModelExecute asks this for every model drawn, hundreds of times a frame, and each
// answer walks a path with strstr. model_t* is stable for the life of a map, so memoize on
// it: direct-mapped, 512 slots, one pointer compare on a hit. A map load frees model_t's and
// the heap hands the same addresses back, so Visuals_FlushModelCache wipes this on every
// re-entry to a game (render.cpp slow tick).
// ONE memo, one bitmask, every answer no-hats needs about a model. The draw hook reads the
// first two bits; the 100ms sweep reads the rest, which is what took the whole strstr chain
// (and, before it, a GetModelName per entity per tick) out of the sweep.
// (no-hats model-kind cache removed with the hat changer -- see ModelNameOf above)

// Same direct-mapped memo, for the transparent-viewmodel path.
// 0 = not a viewmodel model
// 1 = the viewmodel entity's OWN model: classic v_ models, and the c_ arms the econ
//     viewmodel draws. These paths exist nowhere but the viewmodel.
// 2 = a c_/w_ weapon model. Identical file whether it is in your hands or on someone
//     else's back, so it only counts as a viewmodel when entity_index is -1 -- see
//     IsViewmodelDraw below.
static const void*   s_vmCacheKey[512] = {};
static unsigned char s_vmCacheVal[512] = {};

// THE WEAPON BUG: kind 2 was strstr(p, "models/weapons/") -- the STOCK dir only. TF2 ships
// community weapons under models/workshop{,_partner}/weapons/, and CEconEntity builds the
// viewmodel attachment from pItem->GetPlayerDisplayModel() (econ_entity.cpp:1167), i.e.
// exactly those paths. So the arms went transparent (kind 1, never touched this test) and the
// weapon stayed opaque unless it was stock. The two tests below match all three roots.
static unsigned char ViewmodelModelKind(const char* p)
{
    if (!p) return 0;
    // CSS: the viewmodel is models/weapons/v_*.mdl, gun + hands in one mesh, drawn by the
    // networked C_PredictedViewModel under a REAL entity index (never -1). 3 = viewmodel weapon
    // regardless of index. w_ models (same dir, worn/dropped) fall through to 2.
    if (strstr(p, "/weapons/v_")) return 3;
    if (strstr(p, "/v_models/")) return 1;
    if (strstr(p, "_arms.mdl"))  return 1;
    if (strstr(p, "/c_models/")) return 2;   // models/{,workshop/,workshop_partner/}weapons/c_models/
    if (strstr(p, "/weapons/"))  return 2;   // v_/w_ leftovers under any of the same roots
    return 0;
}

// Defined further down with the rest of the flat-alpha path.
void VmFlatAlpha_FlushCache();

void Visuals_FlushModelCache()
{
    memset(s_vmCacheKey,  0, sizeof(s_vmCacheKey));
    memset(s_vmCacheVal,  0, sizeof(s_vmCacheVal));
    VmFlatAlpha_FlushCache();
}

// C_BaseViewModel::DrawModel (c_baseviewmodel.cpp:316) SetBlends then draws, through
// CEconEntity::DrawOverriddenViewmodel, BOTH the c_ weapon attachment and the arms -- each
// arriving here as its own DrawModelExecute call, so each needs re-blending. The engine reads
// r_blend inside DrawModelExecute, which is why setting it immediately before the original
// takes effect.
// The weapon attachment is a C_ViewmodelAttachmentModel, built through
// InitializeAsClientEntityByIndex, whose first line is `index = -1` (c_baseentity.cpp:1160).
// That -1 separates the weapon in your hands from the same c_ model worn in the world, which
// always draws under a real networked index.
// 0 = not the viewmodel, 1 = the arms stack, 2 = the weapon in your hands. Frame blending needs
// the two apart: they are separate draws sharing sixteen bone slots, and one pick over both
// leaves the gun with two or three proxies for its whole length (hacks/screen/fbbones.h).
static unsigned char ViewmodelDrawKind(const void* pMdl, LazyModelName& nm, int entIdx)
{
    unsigned char kind;
    bool fresh = false;
    if (!pMdl) {
        kind = ViewmodelModelKind(nm.Get());
    } else {
        const size_t i = (reinterpret_cast<uintptr_t>(pMdl) >> 4) & 511u;
        if (s_vmCacheKey[i] == pMdl) {
            kind = s_vmCacheVal[i];
        } else {
            kind = ViewmodelModelKind(nm.Get());
            s_vmCacheKey[i] = pMdl;
            s_vmCacheVal[i] = kind;
            fresh = true;
        }
    }
    (void)fresh;
    if (kind == 1) return 1;
    if (kind == 3) return 2;
    if (kind == 2 && entIdx < 0) return 2;
    return 0;
}

static bool IsViewmodelDraw(const void* pMdl, LazyModelName& nm, int entIdx)
{
    return ViewmodelDrawKind(pMdl, nm, entIdx) != 0;
}

// IVRenderView slots -- public/ivrenderview.h order is DrawBrushModel, DrawIdentityBrushModel,
// TouchLight, Draw3DDebugOverlays, SetBlend, GetBlend, SetColorModulation, GetColorModulation.
// Hand-rolled rather than VCall: a call that could not be made must read as 1.f (the
// no-op blend the restore falls back on), and VCall's zero return cannot be told apart
// from a genuine blend of 0.
static float RenderView_GetBlend()
{
    float r = 1.f;
    if (!g_renderView) return r;
    __try {
        void** vt = *reinterpret_cast<void***>(g_renderView);
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000 && vt[5])
            r = (reinterpret_cast<float(__fastcall*)(void*)>(vt[5]))(g_renderView);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

static void RenderView_SetBlend(float blend)
{
    if (g_renderView) VCall<void>(g_renderView, 4, blend);
}

// ==== Flat viewmodel alpha ($blendtintbybasealpha) =================================
// WHY THE WEAPON GOES NEARLY INVISIBLE AT 0.50: SetBlend feeds the modulation alpha, and
// every model shader then multiplies the BASE TEXTURE's alpha into it -- which on a TF2
// weapon is the phong mask, mostly dark. So 0.50 renders as 0.50 * mask, per pixel, which
// no slider value can compensate for. Same multiply in all three paths:
//   dx9 phong/skin  skin_ps20b.fxc:373   alpha = lerp( baseColor.a * alpha, alpha, ... )
//   dx9 non-phong   vertexlit_and_unlit_generic_ps2x.fxc:376   alpha *= baseColor.a
//   dx8             VertexLitGeneric.psh  mul r0, t0, c3   ; r0.a = t0.a * c3.a
//
// $blendtintbybasealpha is the ONE switch that removes it from all three -- skin_ps20b.fxc:371
// and ..._ps2x.fxc:375 guard the multiply on it, and vertexlitgeneric_dx8.cpp:463 picks
// VertexLitGeneric_BlendTint.psh, whose last line is `+mov r0.a, c3`. Works phong or not, and
// unlike $basemapalphaphongmask (the first attempt) it does not flatten the normal map. Its
// other half -- tint by base alpha -- is identity here: both forms lerp between X and
// X * modulation.rgb, and modulation.rgb is white because SetBlend only writes alpha.
//
// COST: it is a STATIC decision (shader pick on dx8, SET_STATIC_PIXEL_SHADER_COMBO on dx9),
// so the material must RecomputeStateSnapshots() after the var moves -- never inside
// DrawModelExecute. VmFlatAlpha_Tick does it from the render slow tick, so the flat alpha
// arrives up to 100ms after a weapon is first drawn.
//
// WHERE THE MATERIALS COME FROM: NOT GetModelMaterialCount -- it returned 0 for every model
// tried (c_spy_arms, c_sapper, c_ambassador, v_watch_pocket_spy), so GetModelMaterials never
// ran and the checkbox did nothing. DrawModelExecute is handed the materials already: pState
// is a DrawModelState_t (ivmodelrender.h:38) whose m_pStudioHWData carries
// studioloddata_t::ppMaterials (studio.h:1611), the exact array about to be bound. Plain
// struct reads -- no vtable, no interface version. x64 layout, unchanged since this branch:
//   DrawModelState_t : m_pStudioHdr 0x00, m_pStudioHWData 0x08, m_pRenderable 0x10,
//                      m_pModelToWorld 0x18, m_decals 0x20, m_drawFlags 0x28, m_lod 0x2C
//   studiohwdata_t   : m_RootLOD 0x00, m_NumLODs 0x04, m_pLODs 0x08, m_NumStudioMeshes 0x10
//   studioloddata_t  : m_pMeshData 0x00, m_SwitchPoint 0x08, numMaterials 0x0C,
//                      ppMaterials 0x10, pMaterialFlags 0x18 -- stride 0x30
static const size_t kLodStride = 0x30;

// IMaterial: GetName(0), FindVar(11), RecomputeStateSnapshots(16).
// IMaterialVar: GetTextureValue(0), GetName(1), GetNameAsSymbol(2), SetFloatValue(3),
// SetIntValue(4), IsDefined(17); m_intVal follows the vtable ptr and m_pStringVal (+0x10).
static const char* Material_GetName(void* pMat) { return VCall<const char*>(pMat, 0); }

// $blendtintbybasealpha is baked into the shader pick / static combo at snapshot time,
// so a material that does not re-snapshot never reads the change.
static void Material_RecomputeStateSnapshots(void* pMat) { VCall<void>(pMat, 16); }

static void* Material_FindVar(void* pMat, const char* name)
{
    bool found = false;
    void* v = VCall<void*>(pMat, 11, name, &found, false);
    return found ? v : nullptr;
}

static int MatVar_GetInt(void* pVar)
{
    int v = -1;
    __try { v = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pVar) + 0x10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}

static void MatVar_SetInt(void* pVar, int val)   { VCall<void>(pVar, 4, val); }
static bool MatVar_IsDefined(void* pVar)         { return VCall<bool>(pVar, 17); }

// One entry per viewmodel model_t*: every material of that model, its $blendtintbybasealpha
// var, and the value the var held when we found it. Direct-mapped like the other two model
// memos and wiped by the same Visuals_FlushModelCache, because model_t* addresses get
// recycled per map -- and the IMaterial*/IMaterialVar* behind them die with the map too,
// so the cache is what bounds their lifetime. n == 0 is a cached "nothing here".
static const int kVmMatSlots = 64;
static const int kVmMatMax   = 24;
struct VmMatEntry {
    const void*   key;
    void*         mat[kVmMatMax];    // IMaterial*, for the re-snapshot
    void*         var[kVmMatMax];    // its $blendtintbybasealpha
    unsigned char orig[kVmMatMax];
    int           n;
    bool          applied;           // is this entry currently flipped to 1?
    bool          evict;             // a different model wants this slot -- restore, then free it
};
static VmMatEntry s_vmMat[kVmMatSlots] = {};

// What the hook wants vs what the tick has done. The write and its RecomputeStateSnapshots
// happen only in VmFlatAlpha_Tick, never inside a draw call.
static bool s_vmFlatWant    = false;
static bool s_vmFlatPatched = false;
// A slot collision asked for an eviction. Part of the tick's early-out so a pending restore
// still runs with the feature already switched off.
static bool s_vmFlatEvict   = false;

// Walk the cache and make every entry match s_vmFlatWant. Runs from render.cpp's slow tick
// -- the same render thread as the draw, but outside DrawModelExecute, which is what makes
// the re-snapshot safe.
void VmFlatAlpha_Tick()
{
    // Derived HERE, not pushed in from the draw hook. The four terms are all globals, so the
    // per-draw VmFlatAlpha_SetWanted() call was a store executed hundreds of times a frame to
    // hand this function a value it can read directly.
    s_vmFlatWant = g_vmAlphaEnable && g_vmAlpha < 0.999f && g_renderView != nullptr && g_vmFlatAlpha;

    if (!s_vmFlatWant && !s_vmFlatPatched && !s_vmFlatEvict) return;
    s_vmFlatEvict = false;

    bool anyOn = false;
    for (int s = 0; s < kVmMatSlots; ++s) {
        VmMatEntry& e = s_vmMat[s];
        if (!e.key || e.n == 0) { if (e.evict) e = VmMatEntry{}; continue; }

        // An evicting entry is going back to its ORIGINAL values whatever the toggle says --
        // this is the last moment anything holds a pointer to those materials, so the restore
        // has to happen here or they stay flat-alpha for the rest of the map with nothing left
        // to undo them. Done on this tick (render thread, outside any draw) and not in
        // VmFlatAlpha_SetFor, which runs INSIDE DrawModelExecute where the write and its
        // RecomputeStateSnapshots are not safe.
        const bool want = e.evict ? false : s_vmFlatWant;
        if (e.applied != want) {
            for (int i = 0; i < e.n; ++i) {
                MatVar_SetInt(e.var[i], want ? 1 : e.orig[i]);
                Material_RecomputeStateSnapshots(e.mat[i]);
            }
            e.applied = want;
        }
        if (e.evict) { e = VmMatEntry{}; continue; }   // slot free -- the new model takes it next draw
        anyOn = anyOn || e.applied;
    }
    s_vmFlatPatched = anyOn;
}

void VmFlatAlpha_FlushCache()
{
    // No restore pass here on purpose: this runs on re-entry to a game, by which point
    // the previous map's materials may already be freed and every pointer below dangling.
    memset(s_vmMat, 0, sizeof(s_vmMat));
    s_vmFlatPatched  = false;
    s_vmFlatEvict    = false;
}

// Take one material: resolve its $blendtintbybasealpha, keep it.
// btba == -1 means the var does not exist on this material, i.e. its shader does not
// declare it and the VMT does not set it -- nothing to flip, so the material is skipped.
static void VmFlatAlpha_TakeMaterial(VmMatEntry* e, void* pMat)
{
    if (reinterpret_cast<uintptr_t>(pMat) < 0x10000 || e->n >= kVmMatMax) return;
    for (int i = 0; i < e->n; ++i)          // LODs repeat materials
        if (e->mat[i] == pMat) return;

    void* pVar = Material_FindVar(pMat, "$blendtintbybasealpha");
    const int  cur = pVar ? MatVar_GetInt(pVar) : -1;

    if (!pVar) return;
    e->orig[e->n] = (cur == 1) ? 1u : 0u;
    e->mat[e->n]  = pMat;
    e->var[e->n]  = pVar;
    ++e->n;
}

static VmMatEntry* VmFlatAlpha_SetFor(const void* pMdl, const void* pState, const char* mdlName)
{
    if (!pMdl || !pState) return nullptr;
    const size_t slot = (reinterpret_cast<uintptr_t>(pMdl) >> 4) & (kVmMatSlots - 1);
    VmMatEntry* e = &s_vmMat[slot];
    if (e->key == pMdl) return e;

    // SLOT COLLISION. The occupant used to be overwritten right here, and if it was applied
    // its materials kept $blendtintbybasealpha at 1 with no pointer left to put them back --
    // a viewmodel that stays flat-alpha for the rest of the map, feature off or not. The
    // restore cannot run from inside a draw call, so hand the entry to VmFlatAlpha_Tick and
    // leave this model unclaimed; it takes the slot on a later draw, once the tick has freed it.
    if (e->key && e->applied) { e->evict = true; s_vmFlatEvict = true; return nullptr; }

    e->key     = pMdl;
    e->n       = 0;
    e->applied = false;   // fresh entry: the tick has not flipped it yet
    e->evict   = false;

    int nLods = 0;
    __try {
        const uint8_t* st = reinterpret_cast<const uint8_t*>(pState);
        const uint8_t* hw = *reinterpret_cast<const uint8_t* const*>(st + 0x08);
        if (reinterpret_cast<uintptr_t>(hw) >= 0x10000)
        {
            nLods = *reinterpret_cast<const int*>(hw + 0x04);
            const uint8_t* lods = *reinterpret_cast<const uint8_t* const*>(hw + 0x08);
            if (nLods > 0 && nLods <= 16 && reinterpret_cast<uintptr_t>(lods) >= 0x10000)
            {
                // Every LOD, not just m_lod: a viewmodel sits on LOD 0 in practice, but the
                // array is per-LOD and the same IMaterial* repeats across them, so walking
                // all of them costs nothing (TakeMaterial dedups on the var pointer) and
                // cannot miss the one that is about to be bound.
                for (int l = 0; l < nLods; ++l) {
                    const uint8_t* lod = lods + static_cast<size_t>(l) * kLodStride;
                    const int nMat = *reinterpret_cast<const int*>(lod + 0x0C);
                    void** pp = *reinterpret_cast<void** const*>(lod + 0x10);
                    if (nMat <= 0 || nMat > 64 || reinterpret_cast<uintptr_t>(pp) < 0x10000)
                        continue;
                    for (int i = 0; i < nMat; ++i)
                        VmFlatAlpha_TakeMaterial(e, pp[i]);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return e;
}

// IClientEntityList::GetHighestEntityIndex = slot 6 (icliententitylist.h order:
// GetClientNetworkable 0, ...FromHandle 1, GetClientUnknownFromHandle 2, GetClientEntity 3,
// ...FromHandle 4, NumberOfEntities 5, GetHighestEntityIndex 6). The sweep walked to 2048
// unconditionally; a live TF2 server is usually a few hundred entities, and every slot past
// the top costs a virtual call to be told "nothing here".
static int EntityList_HighestIndex()
{
    if (!g_entityList) return 0;
    const int hi = VCall<int>(g_entityList, 6);
    if (hi < 33)   return 32;      // nothing above the players
    if (hi > 2048) return 2048;
    return hi;
}

// No-hats (cosmetic particle suppression) and the cosmetic-bodygroup rebuild (CleanBody,
// per-class TF2 bodygroup tables) had no CS:S equivalent -- CS player models carry no
// attached-item layer to suppress or rebuild -- removed with hats.cpp/hats_inv.cpp, the
// CParticlePropCreate(+Pt) hooks, and the unusual-particle-name sweep machinery they fed
// (FindParticlePropOff/CalibrateBitfieldOffset/SetEntityParticlesSimulate -- the last of those
// had no callers left anywhere in src/, confirmed dead). NoHats_Sweep is gone with its
// render.cpp call site (orchestrator, DE-TF2 pass). NoHats_BodyTick stays a stub: skins.cpp
// (unit SKIN, not this unit's scope) still calls it -- drop that call site to delete this too.

// ==== Chams + glow (10-31 rework) ====================================================
// The passes, materials and settings live in aim/chams.cpp (chams_api.h): botox chams
// structure + ref material/override flow, materials created from KeyValues with one
// IncrementReferenceCount each. The old FindMaterial'd botox_chams/botox_glow VMT files are gone
// -- they were never IncRef'd but DecRef'd every map change ("bad reference count 0" spam).
// Lifecycle still rides flipworld.cpp's CViewRender::LevelInit/LevelShutdown hook.
void Chams_LevelUnload() { Chams_ReleaseMaterials(); }
void Chams_LevelInit()   {}   // rebuilt lazily on the first chams draw of the map
// Glow_LevelInit / Glow_LevelUnload: aim/chams.cpp (render targets for the ref glow pass)

// ==== DrawModelExecute hook (no hats + transparent viewmodel) ======================
DrawModelExecute_t o_DrawModelExecute = nullptr;

// Low fps viewmodel: the one game read (studiohdr_t::numbones off DrawModelState_t's first
// pointer, fbbones.h offsets) in its own __try, then the pure Core picks the array to draw with.
// Falls back to the live bones on any doubt -- a wrong count would index past the slot.
static const void* VmLowFps_Bones(const void* pState, const void* pBones, const void* pMdl)
{
    int count = 0;
    __try {
        const uint8_t* hdr = *reinterpret_cast<const uint8_t* const*>(pState);
        if (reinterpret_cast<uintptr_t>(hdr) >= 0x10000)
            count = *reinterpret_cast<const int*>(hdr + VmFps::kStudioNumBones);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return pBones; }

    if (count <= 0 || count > VmFps::kMaxBones) return pBones;
    return g_vmFps.Apply(pMdl, reinterpret_cast<const VmFps::Mat34*>(pBones), count);
}

// body kept in _impl: it uses __try, so the MiscHookGuard cannot live here (C2712)
static void __fastcall h_DrawModelExecute_impl(void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    // 1.f is a no-op blend, so treat it as off and keep the hook on its cheap path.
    // These read nothing but globals, so VmFlatAlpha_Tick derives s_vmFlatWant itself now --
    // the hook used to store it on every single model draw to tell the 100ms tick something
    // the tick could see for itself.
    const bool wantVmAlpha = g_vmAlphaEnable && g_vmAlpha < 0.999f && g_renderView != nullptr;
    const bool wantVmFlat  = wantVmAlpha && g_vmFlatAlpha;

    // Frame blending wants the viewmodel's BONE MATRICES, and this is the only place they
    // exist: the gun's animation is invisible to a depth reprojection, which can only ever see
    // what the camera did. See hacks/screen/fbbones.h.
    const bool wantFbBones = g_fbEnable && g_fbViewmodel;
    const bool wantChams   = Chams_AnyActive();   // chams or glow (aim/chams.cpp)
    // OnFrame: OverrideView fed this frame's camera (world_fx.cpp). Without it the hold would freeze.
    const bool wantVmLowFps = g_vmLowFps && g_globalVars != nullptr && g_vmFps.OnFrame(g_globalVars->framecount);
    const bool wantNoRagdoll = g_removeRagdolls && !g_eject;
    const bool wantSheen     = g_wpnSheenEnable;

    // Hat changer / no-hats removed (hats.cpp, hats_inv.cpp): CS:S player models carry no
    // attached-item layer to hide or rebuild. g_noHats itself (dllmain.cpp/globals.h, not this
    // unit's) is confirmed orphaned now that this file's only reader of it is gone -- see report.
    if (g_eject || (!wantVmAlpha && !wantFbBones && !wantChams && !wantVmLowFps && !wantNoRagdoll && !wantSheen)) {
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        return;
    }

    const uint8_t* pInfoB = pInfo ? reinterpret_cast<const uint8_t*>(pInfo) : nullptr;
    const void*    pMdl   = nullptr;
    if (pInfoB) {
        pMdl = *reinterpret_cast<const void* const*>(pInfoB + 0x20);
        if (reinterpret_cast<uintptr_t>(pMdl) < 0x10000) pMdl = nullptr;
    }
    LazyModelName nm; nm.mdl = pMdl;
    // ModelRenderInfo_t::entity_index (+0x44) -- one read, shared by fb-bones/chams below.
    const int entIdx = pInfoB ? *reinterpret_cast<const int*>(pInfoB + 0x44) : -1;

    // Remove ragdolls (botox csgo-sdk draw_model_execute.cpp should_skip_ragdoll, 1:1): a ragdoll
    // is its own networked entity (DT_CSRagdoll, c_cs_player.cpp:188), so unlike botox's
    // index-less-draw fallback (CS:GO gibs/body pieces with no entity) CSS's ragdoll always has a
    // valid entity_index -- Css_ClassnameIs covers the one case that matters here.
    if (wantNoRagdoll && entIdx > 0 && Css_ClassnameIs(entIdx, "CCSRagdoll"))
        return;   // skip the draw entirely, same as should_skip_ragdoll's return path

    // Glow pass (aim/chams.cpp Glow_Render) drawing its silhouettes through CBaseEntity::DrawModel:
    // after no-hats (a hidden hat must not glow), before everything else -- the engine's own draw,
    // under the glow material override. ref's RenderHandler does the same.
    if (Glow_Rendering()) {
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        return;
    }

    // THE BONES, BEFORE THE DRAW: pBones is the array the renderer is about to use, and the
    // pose it goes with is in pInfo -- taking both here is what keeps the gun's rigid ride out
    // of the streak, because the two can never come from different frames. Taken before the
    // chams branch so a chammed viewmodel still feeds frame blending.
    unsigned char vmKind = 0;
    if ((wantFbBones || wantChams || wantVmLowFps || wantSheen) && pInfoB) vmKind = ViewmodelDrawKind(pMdl, nm, entIdx);

    // botox weapon sheen: one extra draw of the gun in hands on top of whatever drew it
    // (lsd_chams.cpp WeaponSheen_Begin forces the additive carrier + arms the DIP shader swap).
    const auto sheenPass = [&]() {
        if (wantSheen && vmKind == 2 && WeaponSheen_Begin(nm.Get())) {
            o_DrawModelExecute(rcx, pState, pInfo, pBones);
            WeaponSheen_End();
        }
    };

    // Motion blur bones BEFORE the low fps hold: the blur measures the live animation, and the
    // gun run (frameblend.cpp FbBuildBones) holds that blur with the gun between steps -- low fps
    // lands on the already-blurred gun. Chams and blend below still see the held pose.
    if (wantFbBones && pInfoB && pBones && vmKind)
        FrameBlend_CollectBones(pState, pInfo, pBones, vmKind == 2);

    if (wantVmLowFps && pState && pBones && pMdl && vmKind)
        pBones = VmLowFps_Bones(pState, pBones, pMdl);

    // Chams + glow (aim/chams.cpp, chams_api.h): every pass goes through o_DrawModelExecute,
    // never this hook, so there is no recursion. A viewmodel keeps the transparent-viewmodel
    // blend: chams reads the live blend as its alpha scale.
    if (wantChams)
    {
        const bool vmBlend = wantVmAlpha && vmKind;
        const float prev = vmBlend ? RenderView_GetBlend() : 1.f;
        if (vmBlend) RenderView_SetBlend(g_vmAlpha);
        bool drew = false;
        __try {
            drew = Chams_DrawModel(o_DrawModelExecute, rcx, pState, pInfo, pBones, entIdx, nm.Get(), vmKind);
        } __except(EXCEPTION_EXECUTE_HANDLER) { drew = false; }
        if (vmBlend) RenderView_SetBlend(prev);
        if (drew) { sheenPass(); return; }
    }

    if (wantVmAlpha && pInfoB && IsViewmodelDraw(pMdl, nm, entIdx))
    {
        // Discovery only -- resolve this model's materials into the cache the first time
        // it is drawn. The tick picks them up and flips them.
        if (wantVmFlat)
            VmFlatAlpha_SetFor(pMdl, pState, nm.Get());

        // Put the blend back afterwards -- the engine keeps r_blend as file-scope state and
        // every model drawn after this one would inherit our alpha otherwise.
        const float prev = RenderView_GetBlend();
        RenderView_SetBlend(g_vmAlpha);
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        sheenPass();
        RenderView_SetBlend(prev);
        return;
    }

    o_DrawModelExecute(rcx, pState, pInfo, pBones);
    sheenPass();
}

void __fastcall h_DrawModelExecute(void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    MiscHookGuard _g;
    h_DrawModelExecute_impl(rcx, pState, pInfo, pBones);
}

// ==== Weather (CPrecipitation, rain=precipType 0, snow=precipType 1) ================
// IClientEntityList::GetClientNetworkable -- slot 0 (GetClientEntity is slot 3).
static void* GetClientNetworkable(int idx)
{
    return g_entityList ? VCall<void*>(g_entityList, 0, idx) : nullptr;
}

// g_offMins / g_offMaxs are netvars.cpp's (0 = unresolved, -1 = not found, >0 = offset),
// so every test here is against 0, not -1. They were a second definition of the same
// symbol before, folded by /Gw, which left this block's sentinel a coin flip.
static void Weather_FindOffsets()
{
    if (s_offPrecipType > 0 && g_offMins > 0 && g_offMaxs > 0) return;

    if (s_offPrecipType < 0) {
        RecvTable* t = FindNetworkTable("DT_Precipitation");
        if (!t && s_precipClass)
            t = reinterpret_cast<ClientClass*>(s_precipClass)->m_pRecvTable;
        if (t)
            s_offPrecipType = FindPropOffset(t, "m_nPrecipType");
    }
    if (g_offMins <= 0 || g_offMaxs <= 0) {
        RecvTable* t = FindNetworkTable("DT_BaseEntity");
        if (t) {
            g_offMins = FindPropOffset(t, "m_vecMins");
            g_offMaxs = FindPropOffset(t, "m_vecMaxs");
        }
    }
}

// True only for an entity WE created with the client class's CreateFn. Release() destroys, so it
// must never run on a precipitation entity the map itself owned and we merely adopted.
static bool s_precipOurs = false;

// The precipType Precache() actually LATCHED into the live entity (material + ramp + speed),
// -1 = nothing latched / not ours. This is NOT s_lastAppliedMode: parking for "off" leaves the
// latch alone, so switching back on to the SAME mode needs no new entity. Only a real rain<->snow
// flip does. See Precip_Park / Precip_TearDown.
static int s_precipLatchedType = -1;

// IClientNetworkable update order: type -> PreDataUpdate -> OnPreDataChanged ->
// bounds -> OnDataChanged -> PostDataUpdate
__declspec(noinline) static void Weather_UpdateChain(void* pNet, void* pEnt,
    int precipType, bool bSetBounds)
{
    if (!pNet || !pEnt) return;
    __try {
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*, int);

        if (s_offPrecipType > 0)
            *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + s_offPrecipType)
                = precipType;

        ((Fn)vt[6])(pNet, 0); // PreDataUpdate
        ((Fn)vt[4])(pNet, 0); // OnPreDataChanged

        if (g_offMins > 0 && g_offMaxs > 0) {
            float* mins = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offMins);
            float* maxs = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offMaxs);
            if (bSetBounds) {
                mins[0] = mins[1] = mins[2] = -8192.f;
                maxs[0] = maxs[1] = maxs[2] =  8192.f;
            } else {
                memset(mins, 0, 12); memset(maxs, 0, 12);
            }
        }

        ((Fn)vt[5])(pNet, 0); // OnDataChanged
        ((Fn)vt[7])(pNet, 0); // PostDataUpdate
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_precipEntity = s_precipNetworkable = nullptr;
        s_lastAppliedMode = -99;
        s_precipOurs      = false; // whatever is at the slot is not a thing we may destroy now
    }
}

// C_Precipitation::Render draws EVERY live particle in one mb.Begin(MATERIAL_QUADS, Count()), and
// one dynamic mesh holds INDEX_BUFFER_SIZE 32768 indices = 5461 quads (imesh.h). Live count =
// emit rate (radius^2 * m_flDensity 0.001, c_effects.cpp EmitParticles) * fall time (spawn at
// player +512, dies on entering solid, so ~512u+ / r_rainspeed). Default snow (1500u, 80u/s)
// is ~16k flakes -- 3x over, the crash. Rain at 600u/s is ~2k. 768u = 512 + slack for slopes
// and displacements GetPointContents falls through; 5000 keeps headroom under 5461.
// ponytail: estimate, not a count -- a deep pit under the player can still outlive it.
static float Precip_RadiusCap(float radius, float speed)
{
    constexpr float kMaxLive = 5000.f, kFall = 768.f, kDensity = 0.001f;
    return std::min(radius, sqrtf(kMaxLive * std::max(speed, 1.f) / (kDensity * kFall)));
}

static void Precip_ApplyConvars()
{
    if (!g_cvar) return;
    // CachedCVar re-resolves by itself when g_cvar changes, which is what the hand-rolled
    // s_lastCvar reset used to do -- and it also stops re-walking the command list for a
    // name this build does not have.
    static CachedCVar cc_alpha, cc_speed, cc_width, cc_length, cc_radius, cc_sidevel;
    ConVar* cv_alpha   = cc_alpha.Get("r_rainalpha");
    ConVar* cv_speed   = cc_speed.Get("r_rainspeed");
    ConVar* cv_width   = cc_width.Get("r_rainwidth");
    ConVar* cv_length  = cc_length.Get("r_rainlength");
    ConVar* cv_radius  = cc_radius.Get("r_RainRadius");
    ConVar* cv_sidevel = cc_sidevel.Get("r_RainSideVel");

    if (g_weatherMode == 1) {
        if (cv_alpha)   cv_alpha->ForceFloat(g_rainAlpha);
        if (cv_speed)   cv_speed->ForceFloat(g_rainSpeed);
        if (cv_width)   cv_width->ForceFloat(g_rainWidth);
        if (cv_length)  cv_length->ForceFloat(g_rainLength);
        if (cv_radius)  cv_radius->ForceFloat(Precip_RadiusCap(g_rainRadius, g_rainSpeed));
        if (cv_sidevel) cv_sidevel->ForceFloat(g_rainSideVel);
    } else if (g_weatherMode == 2) {
        if (cv_alpha)   cv_alpha->ForceFloat(g_snowAlpha);
        if (cv_speed)   cv_speed->ForceFloat(g_snowSpeed);
        if (cv_width)   cv_width->ForceFloat(g_snowWidth);
        // r_rainlength used to be forced to 0 here for "round flakes". It is not a streak switch:
        // RenderParticle does delta *= GetLength() * m_Ramp and hands the result to Tracer_Draw,
        // which builds the quad from cross(delta, start). A zero delta makes that cross zero,
        // Tracer_ComputeVerts bails on sqLength < 1e-3, and EVERY flake is dropped before the
        // mesh -- snow simulated fine and drew nothing at any slider setting. Short, not zero.
        if (cv_length)  cv_length->ForceFloat(g_snowLength);
        if (cv_radius)  cv_radius->ForceFloat(Precip_RadiusCap(g_snowRadius * std::max(0.1f, g_snowDensity), g_snowSpeed));
        if (cv_sidevel) cv_sidevel->ForceFloat(g_snowSideVel);
    }
}

// C_Precipitation keeps its material in the ENTITY -- `IMaterial* m_MatHandle` -- and Precache()
// is gated on `if (!m_MatHandle)`. That same call is what picks m_Speed and m_InitialRamp off
// m_nPrecipType, so once it has run the entity is locked to that mode's sprite and ramp and
// flipping the netvar afterwards changes nothing visual. There is no client.dll static to
// invalidate (the 16-bit handle pattern scan that used to live here was hunting a symbol this
// build does not have -- it is a member, not a global), so a mode change destroys the entity and
// the next full tick builds a fresh one with the right type already set.
static void Precip_Release(void* pNet)
{
    // s_precipOurs is dropped on EVERY path, including the two early-outs. It used to survive
    // a null pNet -- which is exactly what a level change hands us, because the engine has
    // already destroyed our slot-2047 entity by then. The flag then stayed true into the next
    // map, where Rain_Tick's adopt branch would pick up whatever entity that map put at 2047
    // and a later rain<->snow flip would call Release() -- `delete this` -- on an entity the
    // MAP owns. That is the one thing the comment on this flag forbids.
    const bool bOurs = s_precipOurs;
    s_precipOurs = false;
    if (!pNet || !bOurs) return;
    __try {
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*);
        ((Fn)vt[1])(pNet); // IClientNetworkable::Release
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// PARK -- the "off" path, and the entity stays ALIVE. Bounds to zero: ComputeEmissionArea
// intersects the entity box with the box around the player, so a zero box either fails the
// cull or yields size 0 and EmitParticles makes nothing.
// DO NOT make "off" tear down instead. That was `delete this` on a live engine entity from
// inside h_Present; the __except here swallowed the AV and the game died later, on something
// still holding the freed entity. Off needs no fresh entity -- Precache latches on the type,
// and the type is not changing.
void Precip_Park(int kSlot)
{
    void* pEnt = GetClientEntity(kSlot);
    void* pNet = GetClientNetworkable(kSlot);
    // Keep the latched type: nothing about the entity's identity changes, only its bounds.
    int keepType = (s_precipLatchedType >= 0) ? s_precipLatchedType : 0;
    if (pEnt && pNet) Weather_UpdateChain(pNet, pEnt, keepType, false);
}

// "Is there a precipitation entity to tear down?" -- the one thing eject needs to know, so
// the two pointers behind it stay this file's.
bool Precip_IsLive() { return s_precipEntity != nullptr || s_precipNetworkable != nullptr; }

// DESTROY -- only for a real rain<->snow flip, and for eject. No update chain first: PreDataUpdate
// with DATA_UPDATE_CREATED re-runs Spawn(), re-queues a g_DataChangedEvents entry keyed on
// &m_DataChangeEventRef and re-adds the entity to the leaf system, all on an object that is freed
// two lines later. Release() is the whole job.
void Precip_TearDown(int kSlot)
{
    void* pNet = GetClientNetworkable(kSlot);
    Precip_Release(pNet);
    s_precipEntity      = nullptr;
    s_precipNetworkable = nullptr;
    s_precipLatchedType = -1;
}

// A level change destroys every client entity, ours included, and leaves this file holding
// three stale pointers and an ownership flag about an object that no longer exists. Nothing
// used to clear them -- Visuals_FlushModelCache covers the model/material memos and not this.
// Called from the in-game edge in render.cpp's slow tick. NO Release() here: the entity is
// already gone, and calling through a dangling networkable is the crash this prevents.
void Precip_OnLevelChange()
{
    s_precipEntity      = nullptr;
    s_precipNetworkable = nullptr;
    s_precipOurs        = false;   // whatever lands at slot 2047 next is not ours to destroy
    s_precipLatchedType = -1;
    s_lastAppliedMode   = -99;     // force a full re-apply against the new map's entity
}

// -- Snow sprite: FindMaterial redirect ---------------------------------------------
// The material lives in the ENTITY (IMaterial* m_MatHandle), so there is nothing global to
// reset -- Precip_Release above is what makes Precache() re-run. This only redirects the
// lookup for engine builds whose Precache asks for the rain sprite in snow mode.
FindMaterial_t o_FindMaterial = nullptr;


void* __fastcall h_FindMaterial(
    void* rcx, const char* pName, const char* pTextureGroup, bool bComplain, const char* pComplainPrefix)
{
    MiscHookGuard _g;
    // Snow mode: swap rain streak sprite for round soft flake
    if (!g_eject && g_weatherEnable && g_weatherMode == 2 && pName)
    {
        if (strcmp(pName, "particle/rain") == 0 || strcmp(pName, "particles/rain") == 0)
            pName = "particle/snow";
    }
    void* pMat = o_FindMaterial(rcx, pName, pTextureGroup, bComplain, pComplainPrefix);
    WorldFx2_OnFindMaterial(pMat, pTextureGroup, pName);   // unit D2: world/prop/weather tint
    return pMat;
}

// -- Unit C2: Sound ESP -----------------------------------------------------------------
// Overlays only the leading fields of the real engine EmitSound_t (ref's
// Hooks/CSoundEmitterSystem_EmitSound.cpp has the full struct: int m_nChannel then
// char const* m_pSoundName) -- channel(4)+pad(4)+pSoundName(8) is all this hook reads, so a
// partial struct is safe: nothing here takes its sizeof or copies past m_pSoundName.
// m_flVolume follows m_pSoundName (source-sdk-2013 shareddefs.h EmitSound_t: int, pad, ptr, float).
struct EmitSoundPartial_t { int m_nChannel; const char* m_pSoundName; float m_flVolume; };

EmitSound_t o_EmitSound = nullptr;

static void __fastcall h_EmitSound_impl(void* rcx, void* filter, int entindex, const void* epRaw)
{
    // 10-31: sound chams (aim/chams.cpp) stamps the same peek; gated on its own list being set
    // esp "only on sound" and the sound arrows read the same stamps (botox sound_esp is one store)
    const bool wantChamsSound = entindex <= 64 && (g_chamsEnable && g_chamsSoundCfg[0]
        || g_espEnable && g_espSoundOnly || g_espEnable && g_espOofArrows && g_espOofType != 0);
    if (!g_eject && (g_espSoundPings || wantChamsSound) && entindex > 0 && epRaw && g_offOrigin > 0)
    {
        __try {
            auto* ep = reinterpret_cast<const EmitSoundPartial_t*>(epRaw);
            if (ep->m_pSoundName) {
                void* pEnt = GetClientEntity(entindex);
                if (pEnt) {
                    const uintptr_t addr = reinterpret_cast<uintptr_t>(pEnt) + g_offOrigin;
                    if (addr >= 0x10000) {
                        const float* o = reinterpret_cast<const float*>(addr);
                        if (g_espSoundPings) EspFx_PushSoundPing(o[0], o[1], o[2]);
                        if (wantChamsSound)  Chams_OnSound(entindex, ep->m_flVolume, o);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // 11-03 join crash probe: the original reads STRING(ent->GetModelName()) (+0x218, live
    // client.dll) for the actor gender -- name the entity when that pointer is junk.
    if (entindex > 0) {
        __try {
            if (void* pEnt = GetClientEntity(entindex)) {
                const uintptr_t name = *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(pEnt) + 0x218);
                static int s_noted = 0;
                if (name < 0x10000 && s_noted < 8) {
                    ++s_noted;
                    CrashLog_Note("EMIT: ent %d (%p) m_ModelName=%IX local=%d", entindex, pEnt, name, GetLocalPlayerIndex());
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Never drop a sound -- always calls through, SND_STOP included. This hook only peeks.
    o_EmitSound(rcx, filter, entindex, epRaw);
}

void __fastcall h_EmitSound(void* rcx, void* filter, int entindex, const void* epRaw)
{
    MiscHookGuard _g;
    h_EmitSound_impl(rcx, filter, entindex, epRaw);
}

// Game thread: skins.cpp h_FrameStageNotify stage 5, BEFORE the original (hats' spot -- its
// OnRenderStart delivers the OnDataChanged our update chain queues). It ran from h_Present
// before, i.e. the RENDER thread under mat_queue_mode 2, so the rain->snow flip's Release()
// freed the entity while the main thread was simulating it: "snow crashes the game".
void Rain_Tick()
{
    constexpr int kSlot = 2047;
    if (!g_entityList) return;
    // Eject: Release here, on this thread. thread.cpp waits for Precip_IsLive() to drop.
    if (g_eject) { if (Precip_IsLive()) Precip_TearDown(kSlot); return; }
    // Out of game the engine owns the teardown (level change / disconnect deletes every client
    // entity, ours too). Forget it, never Release it -- and never CreateFn in the main menu.
    if (!IsInGame()) { if (Precip_IsLive() || s_lastAppliedMode != -99) Precip_OnLevelChange(); return; }

    // The checkbox is the on/off; the combo shows "rain" for anything that is not snow. A fresh
    // config (mode 0) or one saved by the old combo binding (0 = "rain" picked) means rain.
    if (g_weatherEnable && g_weatherMode != 1 && g_weatherMode != 2) g_weatherMode = 1;

    if (!g_weatherEnable || g_weatherMode == 0) {
        if (s_lastAppliedMode == 0) return;
        // -99 (just spawned, never applied) used to fall through BOTH arms here: no teardown, and
        // s_lastAppliedMode went to 0, so the entity was orphaned live at the slot. Park covers it.
        Precip_Park(kSlot);
        s_lastAppliedMode = 0;
        return;
    }

    // Throttle entity management to every 6 frames; convar pushes stay per-frame.
    static int s_tickSkip = 0;
    bool bFullTick = (++s_tickSkip >= 6);
    if (bFullTick) s_tickSkip = 0;

    if (!bFullTick) {
        if (s_lastAppliedMode == g_weatherMode)
            Precip_ApplyConvars();
        return;
    }

    if (!s_precipClass) {
        for (auto* p = GetAllClasses(); p; p = p->m_pNext) {
            if (p->m_pNetworkName && strcmp(p->m_pNetworkName, "CPrecipitation") == 0) {
                s_precipClass = p;
                break;
            }
        }
        if (!s_precipClass) { DebugLog("weather: CPrecipitation client class not found"); return; }
        DebugLog("weather: CPrecipitation client class found at %p", s_precipClass);
    }
    Weather_FindOffsets();
    if (s_offPrecipType < 0 || g_offMins <= 0 || g_offMaxs <= 0) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("weather: offsets unresolved (precipType %d, mins %d, maxs %d)", s_offPrecipType, g_offMins, g_offMaxs); }
        return;
    }

    void* pEnt = GetClientEntity(kSlot);
    if (!pEnt) {
        auto* cls = reinterpret_cast<ClientClass*>(s_precipClass);
        if (!cls->m_pCreateFn) return;
        __try {
            using CreateFn = void*(__cdecl*)(int, int);
            s_precipNetworkable = reinterpret_cast<CreateFn>(cls->m_pCreateFn)(kSlot, 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            s_precipNetworkable = nullptr; return;
        }
        pEnt = GetClientEntity(kSlot);
        if (!pEnt) { s_precipNetworkable = nullptr; return; }
        s_precipEntity      = pEnt;
        s_precipNetworkable = GetClientNetworkable(kSlot);
        s_precipOurs        = true;
        s_precipLatchedType = -1;   // Precache has not run yet -- nothing latched
        s_lastAppliedMode   = -99;
        DebugLog("weather: created CPrecipitation at slot %d (ent %p, net %p)", kSlot, pEnt, s_precipNetworkable);
    } else {
        // ADOPTING an entity that was already at the slot. It is not ours, so it may never be
        // Released -- and this branch is reached after a level change too, where the flag from
        // the previous map's entity would otherwise still read true.
        if (s_precipEntity != pEnt) {
            s_precipEntity      = pEnt;
            s_precipNetworkable = nullptr;
            s_precipOurs        = false;
            s_precipLatchedType = -1;
            DebugLog("weather: adopted existing entity at slot %d (ent %p, not ours)", kSlot, pEnt);
        }
        if (!s_precipNetworkable)
            s_precipNetworkable = GetClientNetworkable(kSlot);
    }

    Precip_ApplyConvars();

    const int precipType = (g_weatherMode == 2) ? 1 : 0;

    // rain <-> snow on a LIVE entity: Precache() has latched the old type's material and ramp
    // and is gated on a null m_MatHandle, so re-poking m_nPrecipType leaves the wrong sprite up
    // forever. Replace the entity; the next full tick spawns one whose type is set first.
    // The test is the LATCHED type, NOT s_lastAppliedMode -- parking for "off" sets that to 0
    // without touching the entity, so testing it made snow -> off -> snow look like a mode
    // change and freed a good entity, i.e. the crash above one toggle later. -1 = nothing
    // latched (freshly spawned, or the map's own entity, which is not ours to destroy).
    if (s_precipLatchedType >= 0 && s_precipLatchedType != precipType)
    {
        DebugLog("weather: rain<->snow flip, tearing down latched type %d for %d", s_precipLatchedType, precipType);
        Precip_TearDown(kSlot);
        s_lastAppliedMode = 0;
        return;
    }

    if (s_lastAppliedMode != g_weatherMode)
    {
        Weather_UpdateChain(s_precipNetworkable, s_precipEntity, precipType, true);
        DebugLog("weather: applied mode %d (precipType %d) to ent %p", g_weatherMode, precipType, s_precipEntity);
        s_lastAppliedMode = g_weatherMode;
        if (s_precipOurs) s_precipLatchedType = precipType;
    }
}

// ==== Remove ragdolls (see h_DrawModelExecute_impl's should_skip_ragdoll) ============
// ==== Remove prop fade =================================================================
// Field layout mirrors engine CStaticProp (offsets only) -- Vector/QAngle are plain float
// triples w/ no vtable, handle typedefs are all unsigned short, CBaseHandle is one 4B index.
struct CStaticProp_Layout
{
    unsigned char pad[20];
    float m_Origin[3];
    float m_Angles[3];
    void* m_pModel;
    unsigned short m_Partition;
    unsigned short m_ModelInstance;
    unsigned char m_Alpha;
};

CStaticPropMgr_ComputePropOpacity_t o_ComputePropOpacity = nullptr;

void __fastcall h_ComputePropOpacity(void* rcx, void* pProp)
{
    MiscHookGuard _g;
    if (g_removePropFade && !g_eject && pProp)
    {
        ((CStaticProp_Layout*)pProp)->m_Alpha = 255;
        return; // skip original -> no distance-fade opacity applied
    }
    o_ComputePropOpacity(rcx, pProp);
}

// ==== Remove screen effects: swallow Shake/Fade/Rumble user messages ================================
// CHLClient::DispatchUserMessage (VClient slot 36, confirmed same slot in CSS by direct disasm --
// orchestrator 11-03). msg_type is NOT shared across mods though: it is the 0-based index CS's OWN
// RegisterUserMessages() (grepped css-community-master/game/shared/cstrike/cs_usermessages.cpp)
// registers names in, game-specific -- the TF2 enum (Shake=10/Fade=11/Rumble=13) carried over from
// the TF2 port was simply wrong for CSS. Real CSS order: Geiger=0, Train=1, HudText=2, SayText=3,
// SayText2=4, TextMsg=5, HudMsg=6, ResetHUD=7, GameTitle=8, ItemPickup=9, ShowMenu=10, Shake=11,
// Fade=12, VGUIMenu=13, Rumble=14. SayText/SayText2 already confirmed 3/4 independently by
// ChatTranslator_OnUserMessage's own `type != 3 && type != 4` gate (game_extras.cpp), matching.
DispatchUserMessage_t o_DispatchUserMessage = nullptr;

// bf_read layout (bf_read::m_pData at +0, m_nDataBytes at +8), same offsets ChatTranslator's own
// CopyUserMessage (game_extras.cpp) already reads from this same hook's msgData.
__declspec(noinline) static int CopyUserMessageBytes(void* bf, uint8_t* out, int cap)
{
    __try {
        const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(bf);
        int bytes = *reinterpret_cast<const int*>(static_cast<uint8_t*>(bf) + 8);
        if (!data || bytes <= 0) return 0;
        if (bytes > cap) bytes = cap;
        memcpy(out, data, bytes);
        return bytes;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// SDK2/BRIDGE: feeds the chat-line subscribers (mc hud, chud, chat translator's own future
// switch-over) through css_bridge.h instead of each one hooking DispatchUserMessage itself.
static void DispatchChatBridge(int type, void* msgData)
{
    if (type != 3 && type != 4) return;   // SayText / SayText2 only; TextMsg(5) is HUD text, not chat
    uint8_t data[2048];
    const int bytes = CopyUserMessageBytes(msgData, data, sizeof(data));
    if (bytes <= 0) return;

    n_game::SayMsg say;
    const bool ok = type == 4 ? n_game::ParseSayText2(data, bytes, say) : n_game::ParseSayText(data, bytes, say);
    if (!ok || !say.chat) return;

    std::string name, text;
    if (!n_game::PickChatLine(type, say, std::string(), name, text) || text.empty()) return;
    CssBridge_DispatchChat(text.c_str(), say.ent);
}

bool __fastcall h_DispatchUserMessage(void* rcx, int type, void* msgData)
{
    MiscHookGuard _g;
    ChatTranslator_OnUserMessage(type, msgData);   // unit game: reads SayText/SayText2 before the original does
    DispatchChatBridge(type, msgData);             // SDK2/BRIDGE: SayText/SayText2 -> CssBridge_DispatchChat
    if (g_removeScreenEffects && !g_eject && (type == 11 || type == 12 || type == 14))
        return true; // swallow -> effect never plays
    return o_DispatchUserMessage(rcx, type, msgData);
}

// CViewRender::PerformScreenOverlay -- fullscreen material overlays (underwater, nightvision).
// CS:S has no jarate/milk/uber/dead-ringer feign (TF2-only); underwater is the real CSS user of
// this pass. Skip original when enabled -> m_ScreenOverlayMaterial never drawn.
PerformScreenOverlay_t o_PerformScreenOverlay = nullptr;

void __fastcall h_PerformScreenOverlay(void* rcx, int x, int y, int w, int h)
{
    MiscHookGuard _g;
    if (g_removeScreenEffects && !g_eject)
        return; // don't draw overlay
    o_PerformScreenOverlay(rcx, x, y, w, h);
}

// ==== FPS boost: bones optimization ====================================================
// SetupBones dedup: every cosmetic/weapon bonemerge re-triggers the owner's full bone
// setup, so a hatted player gets 3-6 recomputes per frame. First call per player per
// frame runs the original (fills engine m_CachedBoneData), repeats within the same frame
// (same currentTime, no new mask bits) get a cache memcpy instead. Entity itself must be
// the player -- no root walk, never serve owner bones into a cosmetic's buffer.
// rcx = renderable this (entity + 8). m_CachedBoneData = CUtlVector: base @+0, count @+16;
// matrix3x4 = 48 bytes.
SetupBones_t o_SetupBones = nullptr;

static float s_boneTime[65] = {}; // currentTime of last original run per player slot
static int   s_boneMask[65] = {}; // accumulated boneMask covered by cache this frame

// ---- eligibility memo -------------------------------------------------------------------
// The same renderable arrives here 3-6 times per frame (each bonemerged cosmetic and weapon
// re-triggers its owner's setup) and "which player slot is this" is the same answer every
// time -- but it cost three interface calls per repeat, inside the optimisation itself.
// Keyed on the renderable pointer and dropped when currentTime moves, the frame boundary the
// bone cache already trusts. A freed entity whose address is reused inside one frame would
// answer from its previous owner, so the memo is only consulted on an exact frame match.
// THREAD_LOCAL: SetupBones runs on TF2's threaded bone job as well as the render thread, and
// the key/value pair is written unsynchronised. A reader matching a key that was already
// stored against a value that was not yet gets the WRONG player slot -- which is the index
// the lag resolver's per-player track and the bone cache are both keyed by. Per-thread the
// pair can never be torn, and the memo is a one-frame cache anyway.
static thread_local const void* s_boneIdxKey[128]  = {};
static thread_local short       s_boneIdxVal[128]  = {};
static thread_local float       s_boneIdxFrame     = -1.f;
static thread_local int         s_boneLocalIdx     = -1;

// Every C_TFPlayer shares one IClientRenderable vtable, and rcx IS that renderable, so a
// pointer compare rejects everything that is not a player in two loads. That matters because
// SetupBones runs for EVERY animating thing in the scene -- props, ragdolls, gibs, buildings,
// projectiles -- and each of them used to pay the full resolve (entindex + GetClientEntity,
// two interface calls) once per frame just to be told "not a player". Learned from the first
// player that resolves; until then the filter is simply off. It lives in client.dll, so it
// stays valid across maps.
static const void* s_playerRenderVt = nullptr;

static const void* RenderableVTable(void* rcx)
{
    const void* vt = nullptr;
    __try { vt = *reinterpret_cast<const void* const*>(rcx); }
    __except(EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
    return vt;
}

// TC2's SetupBones LOD mask promotion was ported here 2026-08-30 and MEASURED DEAD in TF2:
// 0 saved on ~15k LOD calls / 2 s, evict 0 (the table never collided, so that 0 is real).
// The mask histogram says why -- TF2 only ever passes 400 (BONE_USED_BY_VERTEX_LOD0),
// 40000 (BONE_MERGE), 200 (ATTACHMENT), 100 (HITBOX). No coarse-LOD request exists to
// promote, and sdk c_baseanimating.cpp:2924 already does boneMask |= m_iPrevBoneMask, so
// the engine coalesces mask changes itself one frame later. Do not re-add.

static void BonesOpt_NewFrame(float currentTime)
{
    if (s_boneIdxFrame == currentTime) return;
    s_boneIdxFrame = currentTime;
    memset(s_boneIdxKey, 0, sizeof(s_boneIdxKey));
    s_boneLocalIdx = GetLocalPlayerIndex();   // once a frame, not once a bone setup
}

// -1 = not eligible; else player slot index (non-local, entity itself is the player)
static __declspec(noinline) int BonesOpt_PlayerIdxUncached(void* rcx)
{
    int r = -1;
    __try {
        uintptr_t ent = reinterpret_cast<uintptr_t>(rcx) - 8;
        void** ntw = *reinterpret_cast<void***>(ent + 16); // IClientNetworkable vtable
        if (reinterpret_cast<uintptr_t>(ntw) < 0x10000) return -1;
        using EntIndex_t = int(__fastcall*)(void*);
        int idx = ((EntIndex_t)ntw[9])(reinterpret_cast<void*>(ent + 16)); // entindex slot 9
        if (idx < 1 || idx > 64) return -1;
        if (idx == s_boneLocalIdx) return -1;
        if (reinterpret_cast<uintptr_t>(GetClientEntity(idx)) != ent) return -1;
        int cls = *reinterpret_cast<int*>(ent + g_offClass);
        if (cls < 1 || cls > 9) return -1;
        r = idx;
    } __except(EXCEPTION_EXECUTE_HANDLER) { r = -1; }
    return r;
}

static int BonesOpt_PlayerIdx(void* rcx)
{
    // Cheap reject first: not the player renderable vtable => not a player, no memo slot
    // spent on it and no interface call made. The LOCAL player passes this and is rejected
    // by the index test below, same as before.
    if (s_playerRenderVt && RenderableVTable(rcx) != s_playerRenderVt) return -1;

    const size_t i = (reinterpret_cast<uintptr_t>(rcx) >> 4) & 127u;
    if (s_boneIdxKey[i] == rcx) return s_boneIdxVal[i];
    const int idx = BonesOpt_PlayerIdxUncached(rcx);
    if (idx > 0 && !s_playerRenderVt) s_playerRenderVt = RenderableVTable(rcx);
    s_boneIdxKey[i] = rcx;
    s_boneIdxVal[i] = (short)idx;
    return idx;
}

// 0 = cache unusable, 1 = served (return true), 2 = buffer too small (return false)
static __declspec(noinline) int BonesOpt_ServeCache(void* rcx, void* pOut, int nMaxBones)
{
    int r = 0;
    __try {
        uintptr_t vec  = reinterpret_cast<uintptr_t>(rcx) - 8 + g_offCachedBones;
        void*     base = *reinterpret_cast<void**>(vec);
        int       cnt  = *reinterpret_cast<int*>(vec + 16);
        if (!base || cnt <= 0 || cnt > 128) return 0; // empty/garbage -> recompute
        if (pOut) {
            if (nMaxBones >= cnt) memcpy(pOut, base, 48ull * (unsigned)cnt);
            else return 2;
        }
        r = 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { r = 0; }
    return r;
}

// The bones are built from the entity's transform, so the lag resolver's smoothed transform
// has to be in place for the ORIGINAL call and gone again the moment it returns -- __finally
// so it is restored on an exception too. Every path through this hook that can run the
// original goes through here.
static bool LrRunOriginal(void* rcx, void* pOut, int nMaxBones, int boneMask, float currentTime)
{
    bool bWrote = false;
    if (g_lagResolver && !g_eject && g_offClass > 0)
    {
        // rcx is the renderable (entity + 8); the resolver reads/writes entity fields
        bWrote = LagRes_Begin(reinterpret_cast<char*>(rcx) - 8,
                              BonesOpt_PlayerIdx(rcx), currentTime);
    }
    __try { return o_SetupBones(rcx, pOut, nMaxBones, boneMask, currentTime); }
    __finally { if (bWrote) LagRes_End(); }
}

bool __fastcall h_SetupBones(void* rcx, void* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
    MiscHookGuard _g;   // hottest hook in the DLL and it was never drained on eject
    if (g_lagResolver && !g_eject && g_netvarsReady && g_offClass > 0)
        BonesOpt_NewFrame(currentTime);   // resolver needs the local index too
    if (g_fpsBoost && !g_eject && g_netvarsReady && g_offClass > 0 && g_offCachedBones > 0)
    {
        BonesOpt_NewFrame(currentTime);   // drops the eligibility memo when the frame moves
        int idx = BonesOpt_PlayerIdx(rcx);
        if (idx > 0)
        {
            bool sameFrame = (s_boneTime[idx] == currentTime);
            bool maskOK    = sameFrame && (boneMask & ~s_boneMask[idx]) == 0;
            if (maskOK)
            {
                int r = BonesOpt_ServeCache(rcx, pBoneToWorldOut, nMaxBones);
                if (r == 1) return true;
                if (r == 2) return false;
            }
            bool ok = LrRunOriginal(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
            if (ok)
            {
                s_boneTime[idx] = currentTime;
                s_boneMask[idx] = sameFrame ? (s_boneMask[idx] | boneMask) : boneMask;
            }
            return ok;
        }
    }
    return LrRunOriginal(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
}
