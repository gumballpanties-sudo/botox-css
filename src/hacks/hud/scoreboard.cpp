#include "scoreboard.h"
#include "../../globals/common.h"
#include "../../hooks/crashlog.h"  // main-thread hitch trace
#include <intrin.h>

// TF2-only "reveal scoreboard" (CTFClientScoreBoardDialog class-icon/domination
// rewrite + CTFPlayerPanel/CTFTeamStatusPlayerPanel match-hud fill) REMOVED 11-03.
// No CS:GO or CS:S analog -- CS:S scoreboard has no class icons or nemesis column.
// mmfix (TF2 GC matchmaker unstick) also removed with it -- no Valve GC matchmaking
// on CS:S. g_revealScoreboard still declared in globals.h / menu.cpp: see report.

// "Steam History" + "Add to Alias List" scoreboard right-click menu REMOVED 11-03: N/A
// on CS:S, not UNVERIFIED. TF2's CTFClientScoreBoardDialog::OnScoreBoardMouseRightRelease
// (+ AddMenuItem/OnCommand plumbing it hooked into) has no CS:S counterpart -- checked
// both CS:S scoreboard dialog classes in the css-community source
// (game/client/cstrike/VGUI/cstrikeclientscoreboard.cpp, game/client/game_controls/
// ClientScoreBoardDialog.cpp) for RightRelease/RightClick/ContextMenu/AddMenuItem/
// OnCommand: none exist. CS:S's scoreboard is read-only vgui, no per-row context menu at
// all, so there is no call site to hang this feature on. g_steamHistory and the custom
// names alias list (misc/names.cpp, Names_HasAlias/Names_AddAlias) stay unused here;
// the alias list itself still works everywhere else names.cpp reads it (player list,
// hud, chat). See report NEEDS FROM OTHERS for the now-dead g_steamHistory menu item.

using VGuiRunFrame_t = void(__fastcall*)(void*);
static VGuiRunFrame_t o_VGuiRunFrame = nullptr;

// ---- CVGui::RunFrame (vgui2.dll) ---------------------------------------------------
// Kept only for the main-thread hitch trace now that reveal-scoreboard and mmfix are gone.
static void __fastcall h_VGuiRunFrame(void* thisptr)
{
    MiscHookGuard _g;
    CrashLog_Frame();
    o_VGuiRunFrame(thisptr);
}

// ---- install -----------------------------------------------------------------------
void Scoreboard_Install()
{
    HMODULE hVgui = GetModuleHandleA("vgui2.dll");

    if (hVgui)
        InstallPatternHook(hVgui, "scoreboard: CVGui::RunFrame", "48 8B C4 53 48 81 EC",
                           (void*)h_VGuiRunFrame, (void**)&o_VGuiRunFrame);

    DebugLog("scoreboard: runframe=%d", o_VGuiRunFrame != nullptr);
}

void Scoreboard_LogInstallOnce() {}   // nothing deferred left to replay
