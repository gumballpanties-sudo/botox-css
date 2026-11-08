#pragma once
// UNIT D2/settings: the 4 missing-botox-feature port (11-08). `inline bool g_x = false;` style,
// same convention every other cfg/u_*.h uses. Primitive types only (aim TU safe -- included
// directly by css_bridge.cpp/world_fx2.cpp, not just globals.h).

// ---- sun angle (botox menu_tab_visuals.cpp:762-777, edicts.cpp:858 sun_angle()) ----
// CSS sink is env_shadow_control's netvars, applied via css_bridge.h's CssBridge_SunAngleSet/
// Restore -- see that header's banner for why (no cl_csm_* convars on this engine branch).
inline bool  g_sunAngleEnable          = false;    // m_sun_angle
inline float g_sunAnglePitch           = 0.f;      // -180..180
inline float g_sunAngleYaw             = 0.f;      // -180..180
inline float g_sunAngleRoll            = 0.f;      // -180..180
inline float g_sunAngleSpeed           = 0.f;      // 0..90 deg/s, yaw drift
inline float g_sunAngleShadowDistance  = 0.f;      // 0..3000 u, 0 = map default (botox: 0 = unset)

// ---- third person offset side / up (botox menu_tab_settings.cpp:2211-2214) ----
// Applied in world_fx.cpp's existing third-person camera trace (h_OverrideView), alongside
// g_thirdPersonDistance (globals.h/dllmain.cpp, pre-existing).
inline float g_thirdPersonOffsetSide = 0.f;        // m_third_person_offset_side, -60..60
inline float g_thirdPersonOffsetUp   = 0.f;        // m_third_person_offset_up,   -60..60
