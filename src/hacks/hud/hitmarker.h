#pragma once
#include "../../globals/globals.h"

// Hit marker + hit sound: fires off the real "player_hurt" event (attacker == local player),
// confirmed fields in css-community resource/modevents.res (userid, attacker, health, armor,
// weapon, dmg_health, dmg_armor, hitgroup -- CS has no crit/minicrit). No new hook -- called
// from killfeed.cpp's existing FireEvent piggyback via unitE_events.cpp.
void Hitmarker_OnHurt(int damage, bool killedTarget);
void RenderHitMarker();   // PaintTraverse frame build, screen-centre crosshair feedback
