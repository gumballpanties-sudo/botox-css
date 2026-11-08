#pragma once
#include "../../globals/globals.h"

// botox kill_effects.cpp 1:1 port (hacks/misc/kill_effects.cpp, csgo-sdk). "player_death" is a
// confirmed CS:S event too (modevents.res). botox's own rule: never draws on
// the LOCAL player's own death (skipped at the top of on_death_particles) -- this is feedback
// about OTHER deaths, now possible world-anchored at the victim's origin via render.h's
// W2S_Project (pass 1 could only do a screen-space local effect; that version is gone).
//
// Styles (g_dpStyle, botox's m_death_particles_type):
//   0 blood      -> real engine particle, CTE_DispatchEffect("ParticleEffect", ...) "blood_spray_red_01"
//                   (each engine style tries a name list, botox try_names(); unprecached -> blood)
//   1 sparks     -> IEffects001::Sparks (slot 3, sdk IEffects.h); unprecached picks -> EnergySplash (7)
//   2 confetti   -> engine particle "bday_confetti"
//   3 smoke      -> own billboard puffs (no hitbox resolver here -> origin + stacked offsets,
//                   botox's OWN no-bones fallback path, same as its code when bones are missing)
//   4 explosion  -> engine particle "ExplosionCore_Wall"
//   5 soul       -> own billboard particles, 4 sub-styles (fountain/rise/burst/converge)
//   6 balls      -> own billboard bouncy balls, attacker==local only (botox's own gate).
//                   No hull trace here: floor-clamped at spawn Z (ponytail: real IEngineTrace
//                   bounce needs a thread-safety read this pass chose not to risk; upgrade path
//                   = TraceLine the same way movement.cpp/dof.cpp already do, from the game
//                   thread, if an uneven-floor bounce is ever worth the risk).
//   7 melt       -> BLOCKED. botox's real version is a worker-thread fluid sim + marching-cubes
//                   mesh + model vvd/vtx skin + 4 compiled shaders (11 revisions, still flagged
//                   UNTESTED in its own project after all of them). Porting that 1:1 this pass
//                   is not a safe single-session change -- falls back to blood.
//   8 mc         -> minecraft crit x's + xp orbs (botox sprites), player hull as the OBB
//                   (CS:S hull dims via aim/css.h once SDK unit publishes it)
// 11-01: step / alpha / glow / stars / sprite shapes = botox kill_effects_paint 1:1, radii projected
// from world units; model colors = one back buffer read (killeffects_world.cpp CaptureColors).
// Body points stay botox's no-bones fallback (8 points up the victim), the visibility trace runs
// at spawn on the game thread.
void KillEffects_OnDeath(int victimIdx, int attackerIdx, bool localDied);
void RenderKillEffects();   // ticks + draws the custom (non-engine) particle styles

// Round 2 lua port (fx.particle/fx.sparks): exported wrappers around the same
// DispatchEngineParticle/FxSparks this file already calls for the "blood"/"sparks" death
// styles above -- same CTE_DispatchEffect / IEffects001::Sparks path, real verified sigs,
// not a new pattern scan.
struct Vec3;
bool KillEffects_DispatchParticle(const char* name, const Vec3& origin, const Vec3& angles);
void KillEffects_Sparks(const Vec3& at);
