#pragma once
// unit world vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// Primitive types only: included by globals.h AND directly by world_fx3.cpp.

// ---- world group (botox variables.h :660-765) ----
inline float g_wdWorldFov = 0.f;                                   // m_world_fov: ADDED to the view fov (-30..60)

inline bool  g_wdFlipGunOtherSide = false;                         // m_flip_world_mirror_hand

inline bool  g_wdDlightLocal = false;                              // m_dlight_local
inline float g_wdDlightLocalColor[4] = { 120 / 255.f, 160 / 255.f, 1.f, 1.f };
inline int   g_wdDlightBrightness = 5;                             // m_dlight_brightness (dlight exponent)

inline int   g_wdRagdollGravity = 0;                               // m_ragdoll_gravity: off / away / fly up
inline float g_wdRagdollGravityStrength = 1.f;

inline float g_wdWorldModBright = 100.f;                           // m_world_modulation_brightness (%)
inline float g_wdPropModBright  = 100.f;                           // m_prop_modulation_brightness
inline bool  g_wdSkyMod = false;                                   // m_skybox_modulation
inline float g_wdSkyModColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_wdSkyModBright = 100.f;

inline bool  g_wdWorldTex = false;                                 // m_world_texture
inline int   g_wdWorldTexPreset[7] = {};                           // per category, world_fx3_math.h k_texPresets index
inline char  g_wdWorldTexCustom[7][128] = {};                      // "custom": materials/ path, no .vtf
inline bool  g_wdWorldTexDecals = false;                           // m_world_texture_remove_decals
inline bool  g_wdPropTex = false;                                  // m_prop_texture
inline int   g_wdPropTexPreset = 0;
inline char  g_wdPropTexCustom[128] = {};

inline bool  g_wdTracers = false;                                 // m_bullet_tracers
inline bool  g_wdTracersLocal = true;
inline bool  g_wdTracersEnemy = false;
inline bool  g_wdTracersTeam  = false;
inline float g_wdTracersLocalColor[4] = { 150 / 255.f, 130 / 255.f, 1.f, 1.f };
inline float g_wdTracersEnemyColor[4] = { 1.f, 80 / 255.f, 80 / 255.f, 1.f };
inline float g_wdTracersTeamColor[4]  = { 80 / 255.f, 160 / 255.f, 1.f, 1.f };
inline int   g_wdTracersSprite = 0;
inline float g_wdTracersLife  = 3.f;
inline float g_wdTracersWidth = 2.f;

inline bool  g_wdTrail = false;                                    // m_movement_trail
inline float g_wdTrailColor[4]       = { 150 / 255.f, 130 / 255.f, 1.f, 1.f };
inline float g_wdTrailCrouchColor[4] = { 1.f, 130 / 255.f, 200 / 255.f, 1.f };
inline bool  g_wdTrailRainbow = false;
inline bool  g_wdTrailAirOnly = true;
inline int   g_wdTrailSprite = 1;
inline float g_wdTrailLife  = 2.5f;
inline float g_wdTrailWidth = 3.f;

inline bool  g_wdImpacts = false;                                  // m_bullet_impacts (client colour only: TF2 has no server feed)
inline float g_wdImpactsClientColor[4] = { 1.f, 0.f, 0.f, 127 / 255.f };
inline float g_wdImpactsSize = 2.f;
inline float g_wdImpactsDuration = 4.f;

inline bool  g_wdHitMarker = false;                                // m_world_hit_marker
inline float g_wdHitMarkerColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_wdHitMarkerSize = 6.f;
inline float g_wdHitMarkerDuration = 1.f;

// botox :823-849 custom smoke/blood colour -- CSS-real (css-community-master
// game/shared/cstrike/weapon_smokegrenade.cpp, game/client/cstrike/fx_cs_blood.cpp), tinted via
// the same FindMaterial + ColorModulate hook world_fx2.cpp already uses for rain/snow (vtable
// slot 28, world_fx2.cpp:254). Custom molotov colour: N/A, no molotov/incendiary weapon in CS:S
// (grepped -i "molotov|incendiary" over css-community-master/game/shared/cstrike, 0 hits).
inline bool  g_customSmokeEnable = false;                          // m_custom_smoke
inline float g_customSmokeColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_customBloodEnable = false;                          // m_custom_blood
inline float g_customBloodColor[4] = { 1.f, 0.f, 0.f, 1.f };

// botox :1052-1072 remove smoke / remove flash -- CSS-real, same FindMaterial technique
// cssourcex64/dllmain.cpp:45-55 verifies on the live game (MATERIAL_VAR_NO_DRAW on
// "particle/particle_smokegrenade1" / "effects/flashbang" / "effects/flashbang_white").
// "wireframe" mode: no verified wireframe material-var toggle for this engine branch (only
// NO_DRAW is confirmed, world_fx3.cpp:533), so it renders identically to "disabled" -- logged in
// the VISTAB report, not silently merged.
inline bool  g_removeSmokeEnable = false;                          // m_remove_smoke
inline int   g_removeSmokeMode = 0;                                // 0 wireframe (== disabled here), 1 disabled
inline bool  g_removeFlashEnable = false;                          // m_remove_flash
inline float g_removeFlashOpacity = 100.f;                         // m_remove_flash_opacity (%), ColorModulate scale

// botox hacks/visuals/weapon_sheen.cpp "weapon sheen" -- round-5 fix: round-3/4's note here was
// wrong. botox's real mechanism IS a DIP-shader intercept (forced-material-override carrier +
// envmap cubemap + animated mask texture + model-space UV sweep), the SAME shape as lsd_chams.cpp's
// lsd/two-tone chams layers, not a plain $envmaptint poke on the weapon's own material (that never
// produced a visible travelling highlight -- see docs/port report). Real port: lsd_chams.cpp
// WeaponSheen_Begin/End (third DIP layer), fx_sheen_vs/ps.hlsl.
inline bool  g_wpnSheenEnable = false;                             // m_weapon_sheen
inline float g_wpnSheenColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_wpnSheenDelay = 3.f;                                // m_weapon_sheen_delay (0..10s) -- sweep period
inline float g_wpnSheenWidth = 0.3f;                               // m_weapon_sheen_width (0.1..1) -- sweep band width
inline bool  g_wpnSheenNormalBlend = false;                        // m_weapon_sheen_normal -- blend with stock white tint instead of replacing it
inline float g_wpnSheenEnv = 5.f;                                  // m_weapon_sheen_env (0..10) -- 10 = full tint strength

// ---- removals group (botox variables.h :710-730) ----
// m_disable_post_processing = unit screen's g_sfxDisablePostProcessing (u_screen.h, world_fx.cpp)
inline bool  g_wdNo3dSky = false;                                 // m_remove_3d_skybox
inline bool  g_wdNoHud = false;                                    // m_remove_hud_elements
inline constexpr int k_wdHudCount = 14;                            // world_fx3_math.h k_hudElems
inline bool  g_wdNoHudElems[k_wdHudCount] = {};                    // m_removed_hud_elements

// ---- viewmodel group (botox variables.h :1008-1031) ----
inline float g_wdVmRoll = 0.f;                                     // m_viewmodel_roll
inline bool  g_wdVmFovEnable = false;                              // m_viewmodel_fov_enable
inline float g_wdVmFov = 68.f;                                     // m_viewmodel_fov

// botox :1142-1143 "bob changer" shift left/right -- round-3: CS:S has the same live weapon
// inaccuracy CS:GO drives this with (GetInaccuracy/GetSpread/UpdateAccuracyPenalty, vtable
// 382/383/384, session memory ref-css-weapon-spread-slots), applied as a right-vector eye nudge
// in h_CalcViewModelView (visuals.cpp), same spot gun-lower/landing-dip already nudge from.
inline float g_wdVmShiftLeft  = 0.f;                               // m_viewmodel_shift_left  (0..10)
inline float g_wdVmShiftRight = 0.f;                               // m_viewmodel_shift_right (0..10)
