#pragma once
#include "../../globals/globals.h"

// botox overlays group, the small ones (unit misc):
//  - healthshot effect: botox sets CS:GO's m_flHealthShotBoostExpirationTime and the engine draws
//    its effects/healthboost screen overlay. TF2 has neither the netvar nor the material, so the
//    overlay is drawn here (edge vignette + pulse) for the same duration. Triggers = botox's:
//    0 kill (player_death, attacker = you), 1 edgebug, 2 pixelsurf (detections.cpp).
//  - low fps warning: botox indicators.cpp fps_warning 1:1 (fps under the tickrate).
//  - displayed logs: botox g_logger (top-left feed) + console line, off player_hurt / vote_cast.
//  - console color: botox draw_set_color.cpp -- ISurface::DrawSetColor tinted while the console
//    panel paints.
void Healthshot_Trigger(int trigger);                    // any thread
void RenderMiscOverlays();                               // imgui frame build
void MiscLogs_OnGameEvent(void* pEvent, const char* name, int localIdx);   // FireEvent thread

// hit sound (botox hitmarker.cpp on_hit_sound): fired off player_hurt (you hit someone),
// "play <path>" through the verified ClientCmd_Unrestricted vt[106] call (fakemove.inl /
// practice.inl / game_extras.cpp's own copies). Volume stored only, see misc_overlays.cpp.
void HitSound_Trigger();   // FireEvent thread

// auto revive (botox fire_event_intern player_death 1:1): "sm_respawn #<userid>", needs a
// SourceMod-admin server. userId = the raw player_death "userid" key (GetPlayerForUserID's input).
// Modes 2/3 ("only targets"/"yourself and targets") read the real target picks now: misc >
// player list's "auto revive" column (names.cpp Names_IsReviveTarget, account-keyed, persisted).
void AutoRevive_OnPlayerDeath(int victimIdx, int localIdx, int userId);   // FireEvent thread

// auto buy (botox misc.cpp auto_buy() 1:1 gating/ordering, CSS weapon set scoped down):
// round_start arms it, AutoBuy_Tick (every frame, RenderMiscOverlays) does the real buy-zone
// gated retry (3 tries, 0.5s apart, resets on death) using m_bInBuyZone / m_ArmorValue /
// m_bHasHelmet / m_bHasDefuser (local netvar resolve in misc_overlays.cpp). Only gap: no
// per-weapon "already own it" skip (needs a GetClientClass() vtable slot this build hasn't
// verified) -- documented as a harmless simplification (server-idempotent buy command).
void AutoBuy_OnRoundStart(int localIdx);   // FireEvent thread
void AutoBuy_Tick();                       // render thread, every frame

void ConsoleTint_Install();                              // PollThread, after vgui is up
bool ConsoleTint_Begin(void* panels, uintptr_t vpanel);  // PaintTraverse, before the original
void ConsoleTint_End();                                  // ... and after it, when Begin said true
