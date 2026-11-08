#pragma once
#include "../../globals/globals.h"

// Unit E's single piggyback point on the FireEvent hook (killfeed.cpp owns the install).
// Routes player_hurt/player_death to hit marker, hit sound and death particles -- one
// dispatcher so killfeed.cpp gets one call line instead of several. TF2's
// teamplay_point_captured/teamplay_flag_event routing (CTF/CP gamemodes) removed --
// CS:S has bomb_planted/bomb_defused/hostage events instead, not wired here (no botox
// CS:GO analog asked for it).
void UnitE_OnGameEvent(void* pEvent);
