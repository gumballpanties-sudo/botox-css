#pragma once

// Unit hud, game thread half (hud_aim.cpp): fills hud/hud_snap.h for the botox hud group.
class CCSPlayer;

void HudAim_Collect(CCSPlayer* pLocal);                 // hooks.cpp IEngineVGui_Paint, every paint
void HudAim_Restore();                                 // aim.cpp Aim_Unload: net_graphtext back
