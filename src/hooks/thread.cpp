#include "../globals/common.h"
#include "../globals/globals.h"
#include "../hacks/visuals/visuals.h"
#include "../hacks/visuals/chams_api.h"   // h_ForcedMaterialOverride (glow)
#include "../render/render.h"
#include "../hacks/hud/killfeed.h"
#include "../hacks/menu/config.h"
#include "../hacks/movement/movement.h"
#include "../hacks/hud/crosshair.h"
#include "../hacks/network/netregion.h"
#include "../hacks/hud/scoreboard.h"
#include "../hacks/botoxnet/botoxnet.h"
#include "../hacks/web/websurface.h"
#include "../hacks/lua/lua.h"   // g_lua.shutdown() on eject
#include "crashlog.h"
#include "../globals/pinned_thread.h"
#include "../hacks/screen/colorcorrect.h"
#include "../hacks/screen/bloom.h"
#include "../hacks/screen/screenfx.h"
#include "../hacks/visuals/world_fx.h"
#include "../hacks/visuals/world_fx2.h"
#include "../hacks/visuals/world_fx3.h"   // unit world: eject restore (ragdoll proxy, cvars, modulation)
#include "fsn.h"
#include "../hacks/visuals/flipworld.h"
#include "../hacks/hud/mediaplayer.h"
#include "../hacks/misc/misc_overlays.h"   // unit misc: ConsoleTint_Install
#include "../hacks/misc/discord_ex.h"   // unit E: discord rich presence, its own IPC thread
#include "../hacks/misc/game_extras.h"  // unit game: bot names GetPlayerInfo hook + eject restore
#include "../hacks/misc/names.h"        // custom names: C_PlayerResource::GetPlayerName hook
#include "../hacks/aim/aim.h"
#include "../hacks/misc/boot_screen.h"   // eject splash: begin before g_eject, end before unhook

// ==== sv_pure bypass ============================================================
using CL_CheckForPureServerWhitelist_t = void(__fastcall*)(void** pFilesToReload);
static CL_CheckForPureServerWhitelist_t o_CL_CheckForPureServerWhitelist = nullptr;

// Unconditional, as it was on 2026-09-13 (and in fpsoverlay). A matchmade-
// server gate was added after that and made the bypass do nothing on casual -- removed.
static void __fastcall h_CL_CheckForPureServerWhitelist(void** pFilesToReload)
{
    MiscHookGuard _g;
    // botox sv_pure.cpp: safe mode turns the bypass off (menu shows it unticked, safe_checkbox)
    const bool bypass = g_pureBypass && !g_safeMode;
    DebugLog("pure: whitelist check -- bypass=%d safe=%d -> %s", (int)g_pureBypass, (int)g_safeMode,
             bypass ? "SKIPPED" : "ran");
    if (bypass) return;
    o_CL_CheckForPureServerWhitelist(pFilesToReload);
}

static void* s_presentAddr = nullptr;    // d3d9 Device::Present entry we patched

// Who owns the Present entry? Last hook installed wins it and runs FIRST, so this decides
// whether our h_Present draw lands before obs's capture (recorded) or after it (hidden) --
// and it has flipped between sessions. Read the jump a detour left at the entry rather
// than inferring the answer from a recording.
// SEH leaf: the jump target at addr, or null if there is no jump there.
static __declspec(noinline) const void* JumpTargetAt(const void* addr)
{
    if (!addr) return nullptr;
    const uint8_t* p = (const uint8_t*)addr;
    const void* target = nullptr;
    __try {
        if (p[0] == 0xE9)                                    // rel32 jmp
            target = (const void*)(p + 5 + *(const int32_t*)(p + 1));
        else if (p[0] == 0xFF && p[1] == 0x25)               // jmp [rip+rel32]
            target = *(void* const*)(p + 6 + *(const int32_t*)(p + 2));
    } __except (EXCEPTION_EXECUTE_HANDLER) { target = nullptr; }
    return target;
}

static const char* ModuleNameOf(const void* addr, char* buf, int cb)
{
    HMODULE hm = nullptr;
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)addr, &hm) && hm)
    {
        char path[MAX_PATH];
        if (GetModuleFileNameA(hm, path, MAX_PATH))
        {
            const char* b = strrchr(path, '\\');
            lstrcpynA(buf, b ? b + 1 : path, cb);
            return buf;
        }
    }
    lstrcpynA(buf, "(none)", cb);
    return buf;
}

// Who is ABOVE and BELOW us in the Present chain. Owning the entry is not the question --
// what matters is whether obs's capture runs before our draw or after it:
//   obs ABOVE us (owns the entry)         -> it captures, then calls us  => menu hidden
//   obs BELOW us (we own it, or it hooked -> we draw, then it captures   => menu RECORDED
//                 first and our trampoline leads to it)
// Only the module names separate those: "OTHER" alone cannot, because the steam overlay
// hooks Present too and reads the same.
static void LogPresentHookOwner()
{
    if (!s_presentAddr) { DebugLog("present entry: address unknown"); return; }
    char m[64];

    const void* target = JumpTargetAt(s_presentAddr);
    const bool ours = (target == (const void*)&h_Present);
    DebugLog("present entry: owner=%s target=%p (%s)", ours ? "BOTOX" : "OTHER",
             target, ModuleNameOf(target, m, sizeof(m)));

    // Our trampoline holds whatever bytes were at the entry when WE hooked. If a detour was
    // already there, those bytes are its jump -- so this names the hooker directly BELOW us.
    const void* below = JumpTargetAt((const void*)o_Present);
    DebugLog("below botox: %p (%s)", below, ModuleNameOf(below, m, sizeof(m)));

    static const wchar_t* kHookers[] = { L"graphics-hook64.dll", L"gameoverlayrenderer64.dll",
                                         L"nvspcap64.dll", nullptr };
    for (int i = 0; kHookers[i]; ++i)
        DebugLog("  capture module %S: %s", kHookers[i],
                 GetModuleHandleW(kHookers[i]) ? "loaded" : "not loaded");
}

// ==== Client init gate =================================================================
// Every install below runs ONCE. Loaded at game start, client.dll is not even mapped for the
// first several seconds, and once it is, CHLClient::Init still has to create what the installs
// read: g_pClientMode (screenfx, flipworld), `view` (flipworld), convars, the HUD. A miss
// there is permanent, so wait for `view` -- cdll_client_int.cpp sets it after ClientMode,
// gHUD and IGameSystem init, just before input/vgui panels/user messages.
// Same mov rcx,[rip+disp32] site flipworld reads g_pViewRender from.
static constexpr const char* k_viewRenderSig =
    "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 03 48 8D 54 24";

// SEH leaf: the g_pViewRender slot inside client.dll, or null.
static __declspec(noinline) void** ViewRenderSlot(HMODULE hClient)
{
    uint8_t* at = PatternScan(hClient, k_viewRenderSig);
    if (!at) return nullptr;
    void** slot = nullptr;
    __try { slot = reinterpret_cast<void**>(at + 7 + *reinterpret_cast<const int32_t*>(at + 3)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { slot = nullptr; }
    return slot;
}

static __declspec(noinline) bool SlotSet(void** slot)
{
    bool set = false;
    __try { set = reinterpret_cast<uintptr_t>(*slot) >= 0x10000; }
    __except (EXCEPTION_EXECUTE_HANDLER) { set = false; }
    return set;
}

// Sleep in 50ms steps so eject never waits on this.
static bool SleepUnlessEject(int ms)
{
    for (int t = 0; t < ms && !g_eject; t += 50) Sleep(50);
    return !g_eject;
}

// False = ejected while waiting.
static bool WaitForClientInit()
{
    DWORD start = GetTickCount();
    bool waited = false;
    HMODULE hClient = nullptr;
    while (!(hClient = GetModuleHandleA("client.dll"))) {
        waited = true;
        if (!SleepUnlessEject(250)) return false;
    }

    // No slot = the sig broke on a game update. Do not hang the DLL on it: a flat grace
    // period covers Init on any sane machine, and only when we actually saw the load.
    void** slot = ViewRenderSlot(hClient);
    if (!slot) {
        DebugLog("poll_thread: g_pViewRender sig NOT FOUND -- %s",
                 waited ? "flat 10s wait for client init" : "assuming client already up");
        return waited ? SleepUnlessEject(10000) : !g_eject;
    }
    while (!SlotSet(slot)) {
        waited = true;
        if (!SleepUnlessEject(100)) return false;
    }
    // `view` lands a little before the end of Init (input, vgui panels, user messages follow).
    if (waited && !SleepUnlessEject(2000)) return false;
    DebugLog("poll_thread: client init %s (%lu ms)", waited ? "waited for" : "already done",
             (unsigned long)(GetTickCount() - start));
    return true;
}

// ==== Poll thread: interface + hook install (runs once) ==============================
static void PollThread()
{
    // A flat opening Sleep(3000) outlived eject's 2000ms join, so an eject in the first
    // few seconds left this thread installing hooks straight into MH_Uninitialize.
    for (int i = 0; i < 60 && !g_eject; ++i) Sleep(50);
    if (g_eject || !WaitForClientInit()) { DebugLog("poll_thread: ejected before install"); return; }

    // Trimmed to CSS-verified version strings only (docs/port/sig_check.md,
    // tools/css_sigcheck.py): VEngineClient015/VEngineCvar007/VEngineCvar003/
    // VModelInfoClient007/VEngineRenderView013/VClientEntityList002/VClient018/
    // VClient016 and the whole tier0.dll ICvar fallback never hit in the live
    // CSS dlls (0 hits -- never hit TF2's either, dead code there too).
    static const char* const kEngineNames[]  = {"VEngineClient014","VEngineClient013",nullptr};
    static const char* const kCvarNames[]    = {"VEngineCvar004",nullptr};
    static const char* const kModelInfoNames[]= {"VModelInfoClient006",nullptr};
    static const char* const kRenderViewNames[]={"VEngineRenderView014",nullptr};
    static const char* const kEntListNames[] = {"VClientEntityList003",nullptr};

    if (!g_engine) g_engine = (IEngineClient*)GetInterfaceAny("engine.dll", kEngineNames);
    if (!g_cvar) g_cvar = (ICvar*)GetInterfaceAny("vstdlib.dll", kCvarNames);
    if (!g_matSystem)  g_matSystem  = GetInterface("materialsystem.dll", "VMaterialSystem082");
    if (!g_modelRender)g_modelRender= GetInterface("engine.dll", "VEngineModel016");
    if (!g_modelInfo)  g_modelInfo  = GetInterfaceAny("engine.dll", kModelInfoNames);
    // IVRenderView -- SetBlend/GetBlend for the transparent viewmodel
    if (!g_renderView) g_renderView = GetInterfaceAny("engine.dll", kRenderViewNames);
    if (!g_entityList) g_entityList = (IClientEntityList*)GetInterfaceAny("client.dll", kEntListNames);

    // vgui::IPanel::PaintTraverse -- slot 41, counted off public/vgui/IPanel.h including the
    // destructor slot IBaseInterface contributes. This is where the crosshair draws: inside
    // the game's own UI pass, so it is in the backbuffer before anyone's Present hook runs.
    // Here and not in MainThread: at game start d3d9.dll can load before vgui2.dll does.
    // AFTER the interfaces above: the hook reads g_engine on its first paint (OvQueue ->
    // PsEngineW2S), and installing it first was a null-vtable AV every inject (crash.log).
    if (void* panels = VGui_GetPanelInterface())
        InstallVTableHook(panels, 41, "vgui: PaintTraverse", (void*)&h_PaintTraverse,
                          (void**)&o_PaintTraverse);
    else DebugLog("vgui: VGUI_Panel009 not found -- crosshair falls back to EndScene");
    ConsoleTint_Install();   // unit misc: botox console color (ISurface::DrawSetColor)

    InstallVTableHook(g_modelRender, 19, "nohats: DrawModelExecute",
                      (void*)h_DrawModelExecute, (void**)&o_DrawModelExecute);
    // IVModelRender::ForcedMaterialOverride slot 1 (ref IVModelRender.h, chams.cpp kMdlForcedMaterialOverride)
    InstallVTableHook(g_modelRender, 1, "glow: ForcedMaterialOverride",
                      (void*)h_ForcedMaterialOverride, (void**)&o_ForcedMaterialOverride);
    InstallVTableHook(g_matSystem,   71, "weather: FindMaterial",
                      (void*)h_FindMaterial, (void**)&o_FindMaterial);

    {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            // Sigs are ref's where noted, each re-scanned against tf/bin/x64/client.dll.
            struct ClientHook { const char* tag; const char* sig; void* hook; void** orig; };
            // CSS_STUB tf2 api: CParticleProperty::Create (x2) and GetWeaponCrosshairScale
            // removed -- base_analysis.md confirms neither exists in CS:S (no equivalent
            // particle-property class / per-weapon crosshair-scale system; CS:S crosshair
            // scale is cl_crosshairscale). h_CParticlePropCreate(+pt)/h_GetWeaponCrosshairScale
            // (aim half) are now unused by this install table; left defined in their owning
            // files (not mine) in case a later CSS-specific feature retargets the hook tag.
            // CreateTFRagdoll (remove ragdolls) is NOT a missing-sig stub like the two above:
            // botox csgo-sdk never hooked a ragdoll-create function at all (verified against
            // csgo-sdk/hooks/functions/draw_model_execute.cpp + frame_stage_notify.cpp). Ported
            // 1:1 as a DrawModelExecute skip + cl_ragdoll_physics_enable toggle instead -- see
            // visuals.cpp h_DrawModelExecute_impl / UpdateVisualsCvars. No client hook needed.
            static const ClientHook kClientHooks[] = {
                { "fpsboost: SetupBones",
                  "48 8B C4 44 89 40 ? 48 89 50 ? 55 53",
                  (void*)h_SetupBones,            (void**)&o_SetupBones },
                { "removals: PerformScreenOverlay",
                  "4C 8B DC 49 89 5B ? 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC",
                  (void*)h_PerformScreenOverlay,  (void**)&o_PerformScreenOverlay },
                { "nopush: AvoidPlayers",
                  "48 89 54 24 ? 55 41 56",
                  (void*)h_AvoidPlayers,          (void**)&o_AvoidPlayers },
                { "soundesp: CSoundEmitterSystem::EmitSound",   // ref sig, verified
                  "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 41 56 48 81 EC ? ? ? ? 49 8B D9",
                  (void*)h_EmitSound,             (void**)&o_EmitSound },
                { "names: C_PlayerResource::GetPlayerName",      // ref sig, 1 hit 11-03
                  "48 89 5C 24 ? 56 48 83 EC ? 48 63 F2",
                  (void*)h_GetPlayerName,         (void**)&o_GetPlayerName },
            };
            for (const auto& h : kClientHooks)
                InstallPatternHook(hClient, h.tag, h.sig, h.hook, h.orig);

            // Ported aimbot subsystem. Brought up BEFORE dih's own CreateMove hook: when it
            // succeeds it OWNS that hook (CHLClient_CreateMove, aim/hooks.cpp) and calls
            // Movement_Run itself -- MinHook allows one hook per target, and that one needs
            // the send-packet pointer off its own stack frame.
            Aim_Init();

            // CHLClient::CreateMove (VClient slot 21) -- bhop. Skipped when the aim half is
            // live (it installs the same hook and calls Movement_Run from it), but the rest
            // of the block -- input offsets, the movement port's interfaces/sigs,
            // DispatchUserMessage -- has to run either way.
            // o_CreateMove stays null when the aim half owns the hook, so it cannot serve as
            // this block's "already done" flag: ResolveInput/Movement_Install would rerun
            // every poll iteration.
            static bool s_bClientHooksDone = false;
            if (!s_bClientHooksDone) {
                s_bClientHooksDone = true;
                // VClient018/VClient016 never hit the live CSS client.dll (sig_check.md).
                static const char* const kVClientNames[] = {"VClient017",nullptr};
                if (void* pClient = GetInterfaceAny("client.dll", kVClientNames)) {
                    if (Aim_Active())
                        DebugLog("bhop: CreateMove owned by aim hook, Movement_Run called from there");
                    else
                        InstallVTableHook(pClient, 21, "bhop: CreateMove",
                                          (void*)h_CreateMove, (void**)&o_CreateMove);
                    // CHLClient::DispatchUserMessage (slot 36) -- remove screen effects
                    InstallVTableHook(pClient, 36, "removals: DispatchUserMessage",
                                      (void*)h_DispatchUserMessage, (void**)&o_DispatchUserMessage);
                }
                ResolveInput();
                Movement_Install(); // trace/prediction interfaces + sigs (movement port)
            }

            // C_BaseViewModel::CalcViewModelView -- viewmodel offset
            static const char* const kCalcVmvSigs[] = {
                "48 89 74 24 ? 55 41 56 41 57 48 8D AC 24",
                "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 8B F0 48 8B EA 48 8B F9 33 DB",
                nullptr
            };
            InstallPatternHookAny(hClient, "viewmodel: CalcViewModelView", kCalcVmvSigs,
                                  (void*)h_CalcViewModelView, (void**)&o_CalcViewModelView);
            VmCsgo_Install(hClient);   // CalcViewModelBobHelper -- CS:GO viewmodel bob

            // CSS_STUB tf2 api: Region_Install() removed -- ISteamNetworkingUtils
            // datacenter region selector was TF2 matchmaking-only; NetGraph_Install
            // (engine net_graph recolor/reposition hooks, TF2 client.dll sigs only)
            // removed with it 11-03 -- botox csgo's own net graph is hud_extra.cpp.

            // UpdatePlayerList / CTFPlayerPanel::GetTeam sigs -- reveal scoreboard
            Scoreboard_Install();

            // IVEngineClient::GetPlayerInfo (slot 8) -- unit game: bot names
            GameExtras_Install();

            // IClientMode::DoPostScreenSpaceEffects (slot 39) -- the view setup the depth
            // passes need, and the thread the game's own screen-space slot runs on.
            ScreenFx_Install();

            // IClientMode::OverrideView (slot 16) -- unit D: world fov + third person
            WorldFx_InstallViewHook();

            // IClientMode::OverrideMouseInput (slot 20) -- edge bug mouse steer + mouse lock
            EdgeBug_InstallMouseHook();

            // CBasePlayer::CalcView, pattern-hooked -- unit D2: remove visual recoil
            WorldFx2_InstallHooks();

            // Flip world: WorldToScreenMatrix (HUD), ClientModeShared::CreateMove (A/D + hand),
            // CViewRender::LevelInit (its RT). The mirror itself rides screenfx's two hooks above.
            FlipWorld_Install();

            // Shared engine FrameStageNotify hook (web/extras/flipworld/net/demo/recorder).
            // No skins in CSS: the skinchanger that used to own this hook is gone (see report).
            Fsn_Install();
        }
    }

    if (HMODULE engine = GetModuleHandleA("engine.dll"))
    {
        struct EngineHook { const char* tag; const char* sig; void* hook; void** orig; };
        static const EngineHook kEngineHooks[] = {
            { "skybox: R_DrawSkyBox",
              "48 8B C4 55 53 41 54 41 55",
              (void*)h_R_DrawSkyBox,      (void**)&o_R_DrawSkyBox },
            { "bypass: sv_pure",          // CL_CheckForPureServerWhitelist
              "40 56 48 83 EC ? 83 3D ? ? ? ? ? 48 8B F1 0F 8E",
              (void*)h_CL_CheckForPureServerWhitelist, (void**)&o_CL_CheckForPureServerWhitelist },
            { "removals: ComputePropOpacity",   // CStaticPropMgr, ref sig
              "48 89 5C 24 ? 48 89 7C 24 ? 41 54 41 56 41 57 48 83 EC ? 48 8B 05",
              (void*)h_ComputePropOpacity, (void**)&o_ComputePropOpacity },
        };
        for (const auto& h : kEngineHooks)
            InstallPatternHook(engine, h.tag, h.sig, h.hook, h.orig);

        // IGameEventManager2 is not exposed through GetInterface's module table -- ask
        // engine.dll's CreateInterface directly.
        if (!g_gameEventManager) {
            using CI_t = void*(*)(const char*, int*);
            if (auto ci = (CI_t)GetProcAddress(engine, "CreateInterface")) {
                static const char* const kGemNames[] = {"GAMEEVENTSMANAGER002","GAMEEVENTSMANAGER001",nullptr};
                for (int i = 0; kGemNames[i] && !g_gameEventManager; ++i)
                    g_gameEventManager = ci(kGemNames[i], nullptr);
            }
        }
    }

    if (g_cvar && !g_cv_sv_skyname)
        g_cv_sv_skyname = g_cvar->FindVarWalk("sv_skyname");

    // IGameEventManager2::FireEvent (slot 8) -- killfeed filter
    InstallVTableHook(g_gameEventManager, 8, "killfeed: FireEvent",
                      (void*)h_FireEvent, (void**)&o_FireEvent);

    DebugLog("poll_thread: hooks installed");
    ConsoleColorPrint((uint8_t)(g_consoleColor[0]*255.f), (uint8_t)(g_consoleColor[1]*255.f),
                       (uint8_t)(g_consoleColor[2]*255.f), "botox on\n");
}

// ==== Main thread ==============================================================================
static void MainThread()
{
    // No timeout on either wait. Loaded at game start (launcher injection), a cold boot with
    // shader/cache work can take far longer than the old 30s + 15s, after which this thread
    // quietly exited and the DLL did nothing for the session.
    while (!g_eject && !(GetModuleHandleW(L"d3d9.dll") && GetModuleHandleW(L"engine.dll")))
        Sleep(250);
    Sleep(1000);

    void* vt[100]{};
    for (int i = 0; !g_eject && !(GetDX9VTable(vt, 100) && vt[17]); ++i)
        Sleep(i < 30 ? 500 : 5000);   // a dummy device every 500ms forever is not free
    if (!vt[17] || g_eject) { ExitThread(0); return; }

    MH_Initialize();

    if (HMODULE hUser32 = GetModuleHandleA("user32.dll"))
    {
        void* pSCP = GetProcAddress(hUser32, "SetCursorPos");
        if (pSCP) MH_CreateHook(pSCP, h_SetCursorPos, (void**)&o_SetCursorPos);
    }

    s_presentAddr = vt[17];
    MH_CreateHook(vt[17], h_Present,  (void**)&o_Present);
    MH_CreateHook(vt[16], h_Reset,    (void**)&o_Reset);
    // slot 42, verified against d3d9.h with Reset=16/Present=17 as anchors. Fallback frame
    // build for frames where vgui never paints (loading screens). See render.cpp.
    MH_CreateHook(vt[42], h_EndScene, (void**)&o_EndScene);

    // Slot 57. Watches for the RESZ code the engine writes from its own
    // UpdateFullScreenDepthTexture, which is the instant opaque geometry finishes and no
    // particle has drawn yet -- where the pre-translucent frame copy is taken.
    ScreenFx_InstallRenderStateHook(vt[57]);
    ScreenFx_InstallDepthStencilHooks(vt[39], vt[40]);   // unit post: "universal depth" z swap (d3d9.h slots 39/40)

    // PaintTraverse moved to PollThread (vgui2.dll may not be loaded yet here).

    // Config BEFORE the hooks go live: LoadConfig is plain data writes (hat slots cleared and
    // refilled, strings, curves), and the first Present builds the font atlas off g_dpiScale.
    // Loaded after, the render thread read every one of those mid-write on its first frames.
    _mkdir(CONFIG_DIR);
    if (GetFileAttributesA(CONFIG_PATH) == INVALID_FILE_ATTRIBUTES) SaveConfig();
    LoadConfig();

    MH_EnableHook(MH_ALL_HOOKS);

    DebugLog("botox loaded, hooks installed");
    LogPresentHookOwner();

    g_hPollThread = CreatePinnedThread([] { PollThread(); });

    // Now-playing poll. Its own thread and not PollThread's: the winrt session calls block for
    // milliseconds, and PollThread is a one-shot installer that has already finished by then.
    // It idles until the feature is switched on, so an unused media player costs one sleeping
    // thread and no winrt at all.
    MediaPlayer_Start();

    // Discord rich presence: same idle-until-enabled shape, its own IPC thread, no hook.
    DiscordRpc_Start();

    while (!g_ejectRequest && !g_eject) {
        if (GetAsyncKeyState(VK_F11) & 1) g_ejectRequest.store(true);
        Sleep(50);
    }
    DebugLog("eject triggered");
    // botox main.cpp: splash covers the screen first, the teardown flag goes up under it
    BootScreen_EjectBegin();
    g_eject = true;
    g_lua.shutdown();   // runs every unload callback + closes every lua_State, game thread, before hooks come down
    ConsoleColorPrint((uint8_t)(g_consoleColor[0]*255.f), (uint8_t)(g_consoleColor[1]*255.f),
                       (uint8_t)(g_consoleColor[2]*255.f), "botox off\n");

    // Disable features so mid-flight hooks finish clean, start no new work
    g_weatherEnable = false;
    // Clearing this is what puts $blendtintbybasealpha back: the render slow tick sees the
    // feature off and reverts each material itself, on its own thread, before the hooks go
    // away. Writing those vars from here would race the render thread mid-draw.
    g_vmAlphaEnable = false;
    g_fogEnable     = false;
    g_skyEnable     = false;

    // Join PollThread FIRST. It calls MH_CreateHook/MH_EnableHook, so letting it live
    // past MH_DisableHook(MH_ALL_HOOKS) races MinHook's own state -- and it used to be
    // joined with a 2000ms wait against its own 3000ms opening sleep, i.e. never.
    // Not joined = it may still MH_CreateHook/MH_EnableHook after both disables below, a live
    // detour into an image about to unmap -- such an eject stays soft.
    bool pollJoined = true;
    if (g_hPollThread) {
        pollJoined = WaitForSingleObject(g_hPollThread, 6000) == WAIT_OBJECT_0;
        CloseHandle(g_hPollThread);
        g_hPollThread = nullptr;
    }

    // Flip world holds cl_flipviewmodels, m_yaw and a few HUD code bytes while it mirrors. Only the
    // game thread may put the hand back (the write sends userinfo), and FrameStageNotify does all of
    // it the frame after g_eject -- give it up to a second before the hooks come off. If no frame
    // comes, Release restores m_yaw and the HUD bytes from here and logs the hand it had to leave.
    for (int i = 0; i < 100 && FlipWorld_Holding(); ++i) Sleep(10);
    FlipWorld_Release();
    // hats.cpp (TF2 wearable/cosmetic system) removed -- no CS:S analog.
    // Weather entity: same, Rain_Tick (FrameStageNotify) Releases it on the game thread.
    for (int i = 0; i < 100 && Precip_IsLive(); ++i) Sleep(10);

    // World is back under the cover: "ejected", the game fades back in (<= 1.5s). Present
    // still draws the splash until here, so nothing below may touch d3d before it returns.
    BootScreen_EjectEnd();

    // Media poll: touches no hook and no engine state, only winrt and its own buffers, so it
    // joins here on its own. Drops the cover texture with it -- after the splash, which is the
    // render thread's last draw.
    MediaPlayer_Shutdown();
    DiscordRpc_Shutdown();

    // Aim subsystem: sets G::Unload (every aim hook early-outs on it), disables
    // its whitelisted hooks and restores the convars it unlocked. Before MH_DisableHook so the
    // aim pipeline stops feeding on half-torn-down state.
    Aim_Unload();

    MH_DisableHook(MH_ALL_HOOKS);
    VTableSwaps_RestoreAll();   // SwapVTableHook entries (ClientMode slots) back to the game's
    Sleep(50);

    // Drain in-flight hook calls before touching shared state. Every hook dih owns bumps one
    // of these counters (MiscHookGuard in globals.h). The AIM half's ~28 hooks do NOT -- see
    // the MH_Uninitialize note below for why that is no longer a use-after-free.
    for (int i = 0; i < 300 && g_hookDepth.load()     > 0; ++i) Sleep(10);
    for (int i = 0; i < 300 && g_miscHookDepth.load() > 0; ++i) Sleep(10);
    // A hook still inside its body after 3s would run unmapped code -- such an eject stays soft.
    const bool drained = g_hookDepth.load() == 0 && g_miscHookDepth.load() == 0;
    FlipWorld_Unload();   // flip world's owner token must not outlive this DLL

    // Safe here and not earlier: releases the D3D event query, and the render thread is
    // provably out of h_Present by this point.
    Perf_Shutdown();
    GameExtras_Shutdown();   // unit game: knobs/pings back if no frame did it, http aborted, workers joined
    g_botox_net.on_shutdown(); // botox network: Steam P2P esp/web-share worker stopped
    Web_Destroy();            // web surface: WebView2 teardown
    ScreenFx_RestoreQueueMode();   // hand mat_queue_mode back before the hooks come off

    // Fallback only: no frame came during the wait above (e.g. alt-tabbed, FSN not running).
    // Tear down CPrecipitation before nulling entity list -- live slot-2047
    // entity crashes next map load otherwise
    if (g_entityList && Precip_IsLive())
        Precip_TearDown(2047);

    ResetFogCvars();
    WorldFx3_Release();    // ragdoll m_vecForce proxy, r_3dsky / viewmodel_fov, material colours back
    Crosshair_Restore();   // hand "crosshair" back at the player's value
    FakeMove_Restore();    // sv_wallbug back (listen server keeps cvars across maps)
    // sv_skyname needs nothing here -- h_R_DrawSkyBox restores it inside every draw, and the
    // hook drain above guarantees the last one finished.

    // MH_Uninitialize() DELETED, DO NOT RE-ADD. It is the only thing that frees MinHook's
    // trampolines, and they live in MinHook's own VirtualAlloc pages, not in our image -- so
    // even the real unload at the bottom of this function leaves them valid. Freeing them buys
    // nothing and costs a use-after-free:
    //   * MH_DisableHook already put the original bytes back, so no NEW call can reach us.
    //   * MinHook's thread freeze only fixes instruction pointers. A thread parked in a hook
    //     BODY still has a return address into the trampoline on its stack, and no counter
    //     can drain what it does not count.
    //   * The aim half's ~28 hooks (features/aim/hooks.cpp, installed via MAKE_HOOK) bump
    //     neither depth counter, and the whitelist is a moving target -- so the drain above
    //     could never have covered them. Same for anything else that forgets a guard.
    // Keeping the trampoline pages mapped makes every one of those cases a harmless return
    // through dead-but-valid code. The cost is MinHook's own heap, leaked once, at eject.
    MH_DisableHook(MH_ALL_HOOKS);   // idempotent: catch anything armed after the first call

    if (g_showMenu) { g_showMenu = false; ShowCursor(FALSE); }
    ClipCursor(nullptr);

    // WndProc: put the game's back only if we are still the top of the chain. Anyone who
    // subclassed after us saved WndProcHook as THEIR previous proc and keeps calling it --
    // overwriting them breaks them, and unmapping under them is a crash on the next message.
    // Left in place, WndProcHook passes straight through at g_eject, and the eject goes soft.
    bool wndProcOff = true;
    if (g_imguiInit && g_origWndProc && g_hwnd && IsWindow(g_hwnd))
    {
        if ((WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) == WndProcHook)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        else {
            wndProcOff = false;
            DebugLog("eject: wndproc subclassed after us -- left chained");
        }
    }

    // Real unload unless something provably still runs in us: a hook that never drained, hats
    // the game thread never freed (FrameStageNotify did not tick), a poll thread that never
    // joined, or a wndproc we could not take back. Then soft, as before.
    const bool hard = drained && pollJoined && wndProcOff;

    if (g_imguiInit)
    {
        CsgoCrosshair_ReleaseTextures();
        ColorCorr_Destroy();
        Bloom_Destroy();
        ScreenFx_Destroy();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    if (hard) DebugLog("eject: unloading dll");
    else      DebugLog("eject: SOFT (hook depth %d/%d, hats live %d, poll joined %d, wndproc off %d) -- dll stays mapped, restart game to rebuild",
                       g_hookDepth.load(), g_miscHookDepth.load(), 0,
                       (int)pollJoined, (int)wndProcOff);

    DebugLog_Shutdown();   // last line written; hands C:\botox-css\debug.log back to the user

    if (!hard) ExitThread(0);

    CrashLog_Uninstall();  // VEH off before the pages it points at go away
    // The aim hooks, WndProcHook and the ragdoll recv proxy bump no depth counter. Anything
    // that entered them before the unhook/restore above returns well inside this.
    Sleep(500);

    // Drops the injector's ref. Workers (pinned_thread.h) hold their own, so the image unmaps
    // when the last of them exits -- the build can overwrite botox.dll from then on.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&g_eject), &self);
    FreeLibraryAndExitThread(self, 0);
}

// ==== DllMain ====================================================================================
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        // Before the thread, before a single hook: a crash during startup is still a crash, and
        // this is the only recorder that does not need anyone to have ticked "debug log".
        // Touches no file and no CRT here -- PE headers, a lock, AddVectoredExceptionHandler.
        CrashLog_Install(hMod);
        HANDLE t = CreateThread(nullptr, 0,
            [](LPVOID) -> DWORD { MainThread(); return 0; },
            nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
