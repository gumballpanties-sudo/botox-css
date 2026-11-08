#pragma once
#include "../../globals/globals.h"

// Port round 11-01, unit world: botox world/removals/viewmodel rows that need new engine paths.
//   bullets   "bullet_impact" game event (CCSPlayer::FireBullet fires it, cs_player_shared.cpp)
//             -> tracer beams (IViewRenderBeams) + client impact boxes; player_hurt -> world
//             hit marker. Routed through unitE_events.cpp's FireEvent dispatch, not a FireBullet
//             function hook (CS:S's CCSPlayer::FireBullet has a different signature than TF2's
//             CBasePlayer::FireBullet; see world_fx3.cpp's bullets-section comment).
//   trail     movement trail beams, per frame from OverrideView (main thread).
//   dlight    enemies + local player, per frame (botox edicts.cpp dlights 1:1).
//   ragdoll   DT_CSRagdoll::m_vecForce recv proxy swap (botox kill_effects.cpp ragdoll_force_proxy).
//   hud       remove hud elements: IPanel::PaintTraverse skipped for the named TF2 vgui panels.
//   cvars     r_3dsky (raw value poke, the string -- what config.cfg saves -- is never written).
//   vmfov     ClientModeCSNormal::GetViewModelFOV, vtable slot 32 swapped (cvar poke is a no-op
//             in this build -- the live function returns a baked .rdata constant, never reads
//             the cvar; see world_fx3.cpp's viewmodel-fov section comment).

void WorldFx3_Install();                    // PollThread (from WorldFx2_InstallHooks)
void WorldFx3_Tick();                       // 100ms tick (from WorldFx2_Tick)
void WorldFx3_Frame();                      // main thread, once a frame (from h_OverrideView)
void WorldFx3_OnPlayerHurt(int victimIdx);  // player_hurt, attacker == local (unitE_events.cpp)
void WorldFx3_OnBulletImpact(int shooterIdx, float x, float y, float z);  // bullet_impact (unitE_events.cpp)
bool WorldFx3_SkipPanel(void* panels, uintptr_t vpanel);   // h_PaintTraverse: true = don't paint it
void RenderWorldFx3();                      // BuildImGuiFrame: impact boxes + world hit markers
void WorldFx3_Release();                    // eject: proxy + cvars back
