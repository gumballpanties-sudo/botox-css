#pragma once
#include "../../globals/common.h"
#include "../../globals/globals.h"   // g_lagResolver + the netvar offsets the transform is derived from

// ==== Lag resolver (visual anti-twitch) ==================================================
// Smooths how a NON-LOCAL player is DRAWN when his updates arrive late or not at all.
// Nothing is sent, nothing is aimed -- the writes live for the duration of one
// SetupBones call and are restored before it returns.
//
// LagRes_Begin writes the smoothed transform into the entity, LagRes_End puts the real
// one back. They MUST be paired (use __try/__finally at the call site) and they nest.

bool LagRes_Begin(void* pEnt, int idx, float flCurrentTime);
void LagRes_End();
void LagRes_LogInstallOnce();     // replay the resolution line when "debug log" is ticked
// interpolated (drawn) origin of a player; false = offsets not resolved, use the netvar origin
struct Vec3;   // hacks/movement/movement.h
bool LagRes_RenderOrigin(void* pEnt, Vec3& out, bool bResolve);
// No map-change reset: a track whose last sample is over half a second old, or whose origin
// moved more than a teleport, resets itself -- which covers a level change, a respawn and a
// reused player slot without a hook that would have exactly one caller.
