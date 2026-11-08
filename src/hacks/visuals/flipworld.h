#pragma once
#include "flipmath.h"

// flip world (visuals tab). Ported from fpsoverlay's flipworld.cpp, same design:
//
//   WHERE   The 3D frame is copied and drawn back with reversed x texcoords at the end of the post
//           slot (ClientMode::DoPostScreenSpaceEffects, viewrender.cpp:2157), after the depth passes and
//           the line before DrawViewModels: world and particles flip inside the CViewSetup's rectangle,
//           the viewmodel, its particle group and the HUD draw later and stay unmirrored.
//   HAND    cl_flipviewmodels' string is inverted with a real SetValue while the mirror draws, so the
//           server (tf_gamerules.cpp:10281) spawns rockets and stickies on the other side and the
//           mirror carries them back to the side the player uses. The client's int is poked back to
//           the player's own hand (FlipClientHand), so the gun draws there, unflipped.
//           config.cfg still gets the player's own hand (FlipCfgWrite, flipmath.h).
//   SWAY    CTFViewModel::CalcViewModelLag is hooked and its offset reflected across the view's right
//           axis (FlipMirrorSway): with mouse yaw inverted the gun would sway against the turn.
//   HUD     IVEngineClient::WorldToScreenMatrix (slot 36) returns a mirrored copy outside the 3D
//           pass: damage numbers, on-screen callouts and dih's own world overlays follow it.
//           Damage arcs, off-screen medic/flag/annotation callouts, the flag compass arrows and
//           PASS Time's ball arrow are placed from the view angles and get byte patches instead
//           (k_flipHudPatches).
//   INPUT   sidemove + IN_MOVELEFT/RIGHT mirrored before ClientModeShared::CreateMove (taunt turning
//           and kart steering read it there), mouse yaw via
//           the m_yaw sign (owned: a negative m_yaw of the player's own is never touched).
//
// Everything but the frame itself follows the mirror only while the frame on screen went through it
// (FlipFrameTrack), and one DLL per process owns flip world (a shared mapping; fpsoverlay carries it
// too). The decisions are FlipCoreTick (flipmath.h); tests/flipworld_test.cpp drives them.

void FlipWorld_Install();                     // PollThread: hooks, HUD patch sites (after ScreenFx_Install)
bool FlipWorld_Mirroring();                   // the frame on screen went through dih's mirror (any thread)
bool FlipWorld_FrameMirroredAnywhere();       // ... or through fpsoverlay's
void FlipWorld_RenderStart();                 // FRAME_RENDER_START: the 3D pass begins
void FlipWorld_WorldPass();                   // DoPostScreenSpaceEffects: this frame draws the world
void FlipWorld_Render(const void* pViewSetup);  // mirror the frame drawn so far (end of the post slot)
void FlipWorld_Tick();                        // game thread: m_yaw, HUD patches, cl_flipviewmodels
void FlipWorld_Release();                     // eject: all back (the cvar write only on the game thread)
bool FlipWorld_Holding();                     // anything of the game's still changed by us
void FlipWorld_Unload();                      // after the eject drain: give the owner token back
