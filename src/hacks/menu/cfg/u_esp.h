#pragma once
// unit esp vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// Included by globals.h AND directly by the aim TU (aim/esp.cpp, aim/chams.cpp, aim/player_stencil.cpp).

// ---- players group (botox variables.h :511-558) ----
inline bool  g_esxStencil = false;                                      // m_players_stencil
inline float g_esxStencilVisColor[4]   = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxStencilInvisColor[4] = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 1.f };
inline float g_esxStencilThickness = 1.5f;

inline bool  g_esxInnerGlow = false;                                    // m_inner_glow
inline float g_esxInnerGlowVisColor[4]   = { 1.f, 1.f, 1.f, 200 / 255.f };
inline float g_esxInnerGlowInvisColor[4] = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 200 / 255.f };
inline float g_esxInnerGlowSize = 12.f;
inline float g_esxInnerGlowIntensity = 1.5f;
inline bool  g_esxInnerGlowBones = true;
inline float g_esxInnerGlowBoneWidth = 50.f;

// botox menu_tab_visuals.cpp:439 "lag compensated trail" (players group). Draws a white 1px dot
// per backtrack record at the head bone, players.cpp:1517-1539 1:1 (aim/backtrack.h ring).
inline bool  g_espBacktrackTrail = false;                               // m_players_backtrack_trail

// botox menu_tab_visuals.cpp:237-242 "weapon icon" (players group, next to weapon name). No CS:GO
// csd icon font / CSS vgui weapon-select sprite sheet wired into the ESP draw queue yet -- same
// solid-square placeholder esx*Icon rows below already use (esp.cpp DrawThrown), not a new look.
inline bool  g_espWeaponIcon = false;                                   // m_weapon_icon
inline float g_espWeaponIconColor[4] = { 1.f, 1.f, 1.f, 1.f };

// ---- edicts group (botox variables.h :628-659) ----
inline bool  g_esxDropped = false;                                      // m_dropped_weapons
inline bool  g_esxDroppedBox = false;
inline bool  g_esxDroppedBoxCorner = false;
inline bool  g_esxDroppedBoxOutline = false;
inline float g_esxDroppedBoxColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxDroppedBoxOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_esxDroppedName = false;
inline float g_esxDroppedNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_esxDroppedIcon = false;                                  // m_dropped_weapons_icon
inline float g_esxDroppedIconColor[4] = { 1.f, 1.f, 1.f, 1.f };

inline bool  g_esxThrown = false;                                       // m_thrown_objects
inline bool  g_esxThrownName = false;
inline float g_esxThrownNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_esxThrownIcon = false;                                   // m_thrown_objects_icon
inline float g_esxThrownIconColor[4] = { 1.f, 1.f, 1.f, 1.f };

inline bool  g_esxGrenadePath = false;                                  // m_grenade_path
inline float g_esxGrenadePathColor[4]         = { 90 / 255.f, 160 / 255.f, 1.f, 1.f };
inline float g_esxGrenadePathBounceColor[4]   = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxGrenadePathDetonateColor[4] = { 1.f, 90 / 255.f, 90 / 255.f, 1.f };
inline float g_esxGrenadePathThickness = 1.5f;
inline bool  g_esxGrenadePathRadius = true;

// g_esxAmmo (TF2 ammo pack/health kit edict) removed 11-03: no pickup edicts in CS:S's
// normal-competitive gameplay (CS:GO botox has no row for them either).
