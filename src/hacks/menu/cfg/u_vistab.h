#pragma once
// unit vistab vars (11-03 botox-csgo tabs round): `inline bool g_x = false;`, defaults = botox csgo variables.h. Primitive types only.

// SCREEN sub-unit (visuals > screen page), 11-04 round.
inline bool g_vsKeepKillfeed   = false;  // botox m_keep_killfeed (variables.h:728) -- pins CSS hud_deathnotice_time
inline bool g_vsAoSmokeFade    = false;  // botox "fade under smoke" (ambient_occlusion.cpp smoke_from_entity) -- CSS smoke grenades
// Smoke centre/radius live in HudSnap (hud/hud_snap.h), not here: the producer (hud_aim.cpp
// CollectSmoke, game thread) and consumer (ssao.cpp, render thread) cross threads the same way
// every other entity-derived HUD number in this build does. This file holds the menu toggle only.
