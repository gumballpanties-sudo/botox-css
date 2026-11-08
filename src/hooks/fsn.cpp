#include "../globals/common.h"
#include "../globals/globals.h"
#include "fsn.h"
#include "crashlog.h"   // CrashSpan: stutter probe per FSN module
#include "../hacks/visuals/flipworld.h"
#include "../hacks/visuals/visuals.h"    // Rain_Tick -- weather entity
#include "../hacks/movement/recorder.h"   // Recorder_FrameStart
#include "../hacks/misc/game_extras.h"    // GameExtras_Frame -- unit game, game-thread tick
#include "../hacks/web/websurface.h"      // Web_MainTick -- FSN stage 0, main menu too
#include "../hacks/botoxnet/botoxnet.h"   // g_botox_net.on_frame_stage_notify (includes aim/css_bridge.h, legal outside aim)
#include "../hacks/lua/lua.h"             // g_lua.on_frame_stage -- botox frame_stage_notify.cpp:35

// Engine FrameStageNotify, shared by several units (MinHook allows only one hook per target).
// Used to live in hacks/visuals/skins.cpp (claimed the hook first); moved here unchanged when
// the CSS skinchanger was removed -- there are no skins in CSS, see report.
using FrameStageNotify_t = void(__fastcall*)(int);
static FrameStageNotify_t o_FrameStageNotify = nullptr;

static void __fastcall h_FrameStageNotify(int stage)
{
    MiscHookGuard _g;

    // Weather entity before the original: its OnRenderStart runs ProcessOnDataChangedEvents, which
    // delivers the OnDataChanged(CREATED) Rain_Tick's PreDataUpdate queued (lost moving FSN out of skins.cpp).
    // Runs on eject too: that is its Release, on this thread (thread.cpp waits on Precip_IsLive).
    if (stage == 5) { CrashSpan _s("fsn/rain"); Rain_Tick(); }

    o_FrameStageNotify(stage);
    g_lua.on_frame_stage(stage);      // botox frame_stage_notify.cpp:35 -- every stage, in-game or not
    if (stage == 0) Web_MainTick();   // botox frame_stage_notify.cpp:43, FRAME_START -- main menu too, so before the in-game returns
    { CrashSpan _s("fsn/extras"); GameExtras_Frame(stage); }   // unit game: chat translator print, bot ping, performance, cvar list

    if (g_eject) { FlipWorld_Release(); return; }

    if (stage == 5) { FlipWorld_Tick(); FlipWorld_RenderStart(); }   // RenderView follows: W2S stays true for the 3D pass

    if (!g_engine || !g_entityList) return;
    g_botox_net.on_frame_stage_notify(stage);   // botox :69, in-game part only

    // 11-03 join crash: local m_ModelName (+0x218, C_BaseEntity::GetModelName in the live
    // client.dll) gets a handle-looking value -- watch it, crash.log names the writer.
    if (stage == 4) {
        static void* s_watched = nullptr;
        const int idx = GetLocalPlayerIndex();
        void* p = idx > 0 ? GetClientEntity(idx) : nullptr;
        if (p != s_watched) { s_watched = p; CrashLog_WatchWrite(p ? (char*)p + 0x218 : nullptr); }
    }

    if (stage == 3) FakeMove_DemoTick();        // demo playback jump anim/sound off recorded velocity
    if (stage == 0) Recorder_FrameStart();       // movement recorder camera, per frame not per tick
    if (stage == 0) EdgeBug_FrameStart();        // botox frame_stage_notify.cpp:77 donor_frame_start -> hw_frame_start
}

bool Fsn_Install()
{
    HMODULE hEngine = GetModuleHandleA("engine.dll");
    if (!hEngine) return false;
    return InstallPatternHook(hEngine, "fsn: FrameStageNotify", "4C 8B DC 56 48 83 EC",
                              (void*)h_FrameStageNotify, (void**)&o_FrameStageNotify);
}
