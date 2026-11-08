#pragma once
// unit spec vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// Spectator list popup ("spectators settings"), botox variables.h m_spectators_*. The older g_spec*
// vars (enable/style/pos/show target/colours/...) stay in dllmain.cpp + globals.h.

inline int   g_specListType          = 0;       // m_spectators_list_type: 0 all spectators, 1 local spectators
inline bool  g_specAvatar            = false;   // m_spectators_avatar
inline bool  g_specClarityAlwaysShow = false;   // m_spectators_always_show (clarity v2's own)
inline float g_specChillwareMaxWidth = 300.f;   // m_spectators_chillware_max_width
inline bool  g_specInterwebzBg       = true;    // m_spectators_list_interwebz_background
inline int   g_specInterwebzLabelX   = 0;       // m_spectators_list_interwebz_label_x
inline bool  g_specInbaRainbow       = false;   // m_spectators_inba_rainbow
inline float g_specInbaRainbowSpeed  = 0.6f;    // m_spectators_inba_rainbow_speed
inline int   g_specLumiType          = 0;       // m_spectators_lumi_type: default / new / interwebz
inline bool  g_specLumiHideTv        = true;    // m_spectators_lumi_hide_gotv (SourceTV in TF2)
inline bool  g_specLumiBlur          = false;   // m_spectators_lumi_blur
inline bool  g_specLumiGlow          = false;   // m_spectators_lumi_glow
inline bool  g_specLumiMascot        = false;   // m_spectators_lumi_mascot
inline char  g_specLumiMascotPath[260] = "";    // m_spectators_lumi_mascot_path (file or http link)
inline float g_specLumiMascotSize    = 60.f;    // m_spectators_lumi_mascot_size
inline float g_specLumiMascotX       = 0.f;     // m_spectators_lumi_mascot_x (menu drag)
inline float g_specLumiMascotY       = 0.f;     // m_spectators_lumi_mascot_y
inline bool  g_specKamiHideTv        = true;    // m_spectators_kami_hide_gotv
inline bool  g_specKamiBlur          = false;   // m_spectators_kami_blur
inline bool  g_specKamiCircle        = true;    // m_spectators_kami_circle
inline float g_specKamiOpacity       = 80.f;    // m_spectators_kami_opacity (%)
inline bool  g_specKbAnimate         = false;   // m_spectators_kamibebra_animate
inline bool  g_specKbDynamic         = false;   // m_spectators_kamibebra_dynamic
inline bool  g_specKbAlwaysShow      = false;   // m_spectators_kamibebra_always_show
inline bool  g_specKbBold            = false;   // m_spectators_kamibebra_bold
inline bool  g_specKbWindowShadow    = false;   // m_spectators_kamibebra_window_shadow
inline float g_specKbLocalColor[4]   = { 1.f, 1.f, 1.f, 1.f };
inline float g_specKbOtherColor[4]   = { 60 / 255.f, 60 / 255.f, 60 / 255.f, 1.f };
inline bool  g_specKbLocalShadow     = false;
inline float g_specKbLocalShadowColor[4] = { 102 / 255.f, 210 / 255.f, 184 / 255.f, 1.f };
inline bool  g_specKbOtherShadow     = false;
inline float g_specKbOtherShadowColor[4] = { 0.f, 0.f, 0.f, 128 / 255.f };
inline bool  g_specKbCustomTitle     = false;   // m_spectators_kamibebra_custom_title (kamibebra + kamidere)
inline char  g_specKbTitle[64]       = "Spectators";
inline bool  g_specKbGif             = false;
inline int   g_specKbGifType         = 0;       // original / shit / custom
inline char  g_specKbGifPath[260]    = "";
inline float g_specKbGifSize         = 64.f;
inline float g_specEyesSize          = 28.f;    // m_spectators_eyes_size
inline bool  g_specEyesEmoji         = false;   // m_spectators_eyes_emoji
inline float g_specAirplaneTitleColor[4]    = { 254 / 255.f, 254 / 255.f, 254 / 255.f, 254 / 255.f };
inline float g_specAirplaneGradientColor[4] = { 39 / 255.f, 39 / 255.f, 39 / 255.f, 254 / 255.f };
inline float g_specAirplaneLocalColor[4]    = { 0.f, 254 / 255.f, 0.f, 254 / 255.f };
inline float g_specAirplaneOtherColor[4]    = { 254 / 255.f, 254 / 255.f, 254 / 255.f, 254 / 255.f };
inline bool  g_specAirplaneOutline   = false;
inline int   g_specAirplaneTitleCase = 0;       // Spectators / spectators / SPECTATORS
inline bool  g_specBhopMode          = false;   // m_spectators_bhopcheat_mode (bhop cheat + kamidere "observer mode")
inline float g_specHavocColor[4]     = { 115 / 255.f, 155 / 255.f, 1.f, 1.f };
inline float g_specInteriumBg[4]     = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_specGamesenseBar      = true;    // m_spectators_gamesense_bar
inline bool  g_specSkebobBg          = true;    // m_spectators_skebob_background
inline bool  g_specSkebobTitle       = true;    // m_spectators_skebob_title
// botox m_watermark_evolve_alpha -- the donor shares ONE var between the ev0lve watermark and the
// ev0lve spectator list; own name here so it can't collide with the watermark unit's.
// chillware v2 window position (ImGui window, imgui.ini is off in this dll): -1 = donor's first-use spot
inline float g_specCv2X              = -1.f;
inline float g_specCv2Y              = -1.f;
