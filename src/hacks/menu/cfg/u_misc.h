#pragma once
// unit misc vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// botox menu_tab_settings.cpp overlays group, media player .. displayed logs. The older g_mp* / g_hm* /
// g_dp* / g_ptsEnable vars stay in dllmain.cpp + globals.h.

// media player settings (m_media_player_*)
inline bool  g_mpVisualizer        = false;   // m_media_player_visualizer ("waveform")
inline int   g_mpVisBarWidth       = 3;       // m_media_player_visualizer_bar_width
inline float g_mpVisHeight         = 18.f;    // m_media_player_visualizer_height
inline bool  g_mpVisSmooth         = false;   // m_media_player_visualizer_smooth
inline bool  g_mpVisCustomColor    = false;   // m_media_player_visualizer_custom_color
inline float g_mpVisColor[4]       = { 1.f, 1.f, 1.f, 1.f };   // m_media_player_visualizer_color
inline bool  g_mpLyrics            = false;   // m_media_player_lyrics
inline int   g_mpLyricsRows        = 3;       // m_media_player_lyrics_rows
inline float g_mpLyricsOffset      = 0.f;     // m_media_player_lyrics_offset (ms)
inline bool  g_mpIngameControl     = false;   // m_media_player_ingame_control
inline int   g_mpPrevKey           = 0;       // m_media_player_previous_key
inline int   g_mpToggleKey         = 0;       // m_media_player_toggle_key
inline int   g_mpNextKey           = 0;       // m_media_player_next_key

// points settings (m_points_*)
inline bool  g_ptsBigFont          = false;   // m_points_big_font
inline float g_ptsAppearSpeed      = 1.f;     // m_points_appear_speed
inline float g_ptsBobSpeed         = 1.f;     // m_points_speed
inline float g_ptsOffsetX          = 0.f;     // m_points_x_offset
inline float g_ptsOffsetY          = 0.f;     // m_points_y_offset

inline bool  g_fpsWarning          = true;    // m_fps_warning

// hit marker settings (m_hit_marker_*)
inline float g_hmSpeed             = 1.f;     // m_hit_marker_speed
inline float g_hmThickness         = 1.f;     // m_hit_marker_thickness
inline bool  g_hmOutline           = false;   // m_hit_marker_outline
inline float g_hmOutlineColor[4]   = { 0.f, 0.f, 0.f, 1.f };   // m_hit_marker_outline_color
inline float g_hmOutlineThickness  = 1.f;     // m_hit_marker_outline_thickness

// healthshot effect (m_healthshot_*): bit 0 kill, 1 edgebug, 2 pixelsurf (botox all on)
inline bool  g_hshotEnable         = false;
inline int   g_hshotTriggers       = 7;
inline float g_hshotDuration       = 1.f;

// death particles extras (m_death_particles_*)
inline bool  g_dpModelColors       = false;   // m_death_particles_model_colors
inline bool  g_dpGlow              = true;    // m_death_particles_glow
inline bool  g_dpStars             = false;   // m_death_particles_soul_stars

// console color (m_console_color_enable / m_console_color): tints the console window
inline bool  g_consoleTintOn       = false;
inline float g_consoleTint[4]      = { 102.f / 255.f, 111.f / 255.f, 102.f / 255.f, 1.f };

// displayed logs (m_log_types): bit 0 damage, 1 votes
inline int   g_logTypes            = 0;
