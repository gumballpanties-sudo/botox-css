#pragma once
#include "../../globals/globals.h"

// TF2-only "reveal scoreboard" (class icon / domination exploit on
// CTFClientScoreBoardDialog, no CS:S analog) REMOVED 11-03 CSS port.
// "Steam History" + "Add to Alias List" scoreboard right-click menu REMOVED 11-03:
// N/A on CS:S -- its scoreboard dialog has no per-row context menu to hook at all
// (checked cstrikeclientscoreboard.cpp + ClientScoreBoardDialog.cpp, no
// RightRelease/AddMenuItem/OnCommand anywhere). See scoreboard.cpp for the full note.
// See report NEEDS FROM OTHERS: g_revealScoreboard / g_steamHistory still declared in
// globals.h / menu.cpp, needs removal there too.
//
// All that is left in this file: CVGui::RunFrame, kept only for the main-thread
// hitch trace (CrashLog_Frame).
void Scoreboard_Install();   // resolve sigs + install hooks (poll thread)
void Scoreboard_LogInstallOnce();
