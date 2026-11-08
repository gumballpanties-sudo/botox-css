#pragma once
#include "../../globals/globals.h"

// Self-only killfeed filter -- IGameEventManager2::FireEvent hook (slot 8).
using FireEvent_t = bool(__fastcall*)(void*, void*, bool);
extern FireEvent_t o_FireEvent;
bool __fastcall h_FireEvent(void* thisptr, void* pEvent, bool bDontBroadcast);

// "keep killfeed" (botox m_keep_killfeed) -- pins hud_deathnotice_time so kill notices never
// expire while on. No tick hook of its own; piggybacked off RenderCrosshair's once-a-frame
// call (crosshair.cpp), same "piggyback, no new hook" convention as UnitE_OnGameEvent above.
void KeepKillfeed_Tick();
void KeepKillfeed_Restore();   // eject: hand the cvar back
