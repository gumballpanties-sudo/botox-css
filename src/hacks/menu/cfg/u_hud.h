#pragma once
// unit hud vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// botox menu_tab_visuals.cpp "hud" group. Colours = float[4] RGBA 0..1 (botox c_color / 255).


// aimbot fov circle (botox m_aimbot_fov_circle_*)
inline bool  g_hudFovCircle             = false;
inline float g_hudFovCircleColor[4]     = { 1.f, 1.f, 1.f, 140.f / 255.f };
inline float g_hudFovCircleThick        = 1.f;
inline bool  g_hudFovCircleKeyOnly      = false;

// custom net graph (botox m_net_graph_*). Items = botox's 11-entry vector<bool>, as a bitmask.
inline bool  g_hudNg                    = false;
inline int   g_hudNgItems               = 0x7FF;
inline bool  g_hudNgInOut               = false;
inline bool  g_hudNgLerp                = false;
inline int   g_hudNgX                   = -1;   // -1 = stock position
inline int   g_hudNgY                   = -1;
inline float g_hudNgColor[4]            = { 229.f / 255.f, 229.f / 255.f, 178.f / 255.f, 1.f };
inline bool  g_hudNgWarn                = true;
inline float g_hudNgWarn1[4]            = { 1.f, 1.f, 31.f / 255.f, 1.f };
inline float g_hudNgWarn2[4]            = { 1.f, 125.f / 255.f, 31.f / 255.f, 1.f };
inline float g_hudNgWarn3[4]            = { 1.f, 31.f / 255.f, 31.f / 255.f, 1.f };
inline int   g_hudNgTextStyle           = 2;    // none / shadow / outline
inline float g_hudNgOutlineColor[4]     = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_hudNgBg                  = false;
inline float g_hudNgBgColor[4]          = { 0.f, 0.f, 0.f, 120.f / 255.f };
// fonts tab > net graph (botox m_net_graph_font_settings "lucon" 14, flags all off)
inline char  g_hudNgFontName[64]        = "lucon";
inline int   g_hudNgFontSize            = 14;
inline int   g_hudNgFontFlags           = 0;


