#pragma once
#include <atomic>
// unit wm vars: botox variables.h m_watermark_* (1594-1659), botox defaults.
// Already in globals.h/dllmain.cpp and reused 1:1: g_wmEnable (m_watermark), g_wmStyle,
// g_wmLabel (m_watermark_text, brand), g_wmShowFPS (_fps), g_wmShowTime (_time), g_wmShowPing (_ping).

inline bool  g_wmTickrate   = true;          // m_watermark_tickrate
inline bool  g_wmVelocity   = false;         // m_watermark_velocity
inline bool  g_wmUser       = false;         // m_watermark_user
inline char  g_wmUserName[33] = "dev";       // m_watermark_clarity_user
inline int   g_wmLabelType  = 0;             // m_watermark_label: 0 time, 1 fps, 2 user, 3 none

inline float g_wmEvolveAlpha    = 100.f;                                 // m_watermark_evolve_alpha (%)
inline float g_wmHavocColor[4]  = { 115 / 255.f, 155 / 255.f, 1.f, 1.f };  // m_watermark_havoc_color
inline float g_wmInteriumBg[4]  = { 0.f, 0.f, 0.f, 1.f };                // m_watermark_interium_bg

inline float g_wmGsGameColor[4]  = { 1.f, 1.f, 1.f, 1.f };                    // m_watermark_gamesense_game_color
inline float g_wmGsSenseColor[4] = { 149 / 255.f, 184 / 255.f, 6 / 255.f, 1.f }; // m_watermark_gamesense_sense_color
inline bool  g_wmGsBar = true;                                                // m_watermark_gamesense_bar
inline float g_wmGsX = 0.f, g_wmGsY = 0.f;                                    // m_watermark_gamesense_x / _y

inline int   g_wmLumiDesign      = 0;        // 0 original, 1 new (rain)
inline bool  g_wmLumiAnimTitle   = false;
inline bool  g_wmLumiNoBorder    = false;
inline float g_wmLumiTitleColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_wmLumiX = 0.f, g_wmLumiY = 0.f;
inline bool  g_wmLumiGlow        = false;
inline float g_wmLumiGlowRadius  = 5.f;
inline float g_wmLumiGlowOpacity = 0.45f;
inline bool  g_wmLumiShadowColor = false;

inline bool  g_wmSkebobTyping    = false;

// boxed watermark offset x / y: fraction of the screen, default 0.01
inline float g_wmBoxedX = 0.01f, g_wmBoxedY = 0.01f;

inline bool  g_wmDnaGlow         = true;
inline int   g_wmDnaGlowTicks    = 15;
inline bool  g_wmDnaGlowSpotted  = false;    // TF2: "spotted" = an enemy our eye can see (aim/wm_aim.cpp)

inline bool  g_wmKamiOutline      = false;
inline float g_wmKamiOutlineColor[4] = { 252 / 255.f, 194 / 255.f, 1.f, 1.f };
inline bool  g_wmKamiRgb          = false;
inline float g_wmKamiRgbSpeed     = 0.5f;
inline bool  g_wmKamiBold         = false;
inline bool  g_wmKamiCustomText   = false;
inline char  g_wmKamiText[21]     = "botox";
inline bool  g_wmKamiMikudere     = false;
inline bool  g_wmKamiTextLikeStroke = false;
inline bool  g_wmKamiRounding     = false;
inline float g_wmKamiRoundingValue = 0.f;
inline int   g_wmKamiShowFlags    = 0;        // 1 user, 2 time, 4 fps
inline bool  g_wmKamiTyping       = false;
inline int   g_wmKamiTypingSpeed  = 5;
inline int   g_wmKamiDeletingSpeed = 8;
inline float g_wmKamiTypingHold   = 2.f;      // m_watermark_kami_typing_inactive
inline float g_wmKamiDeletingHold = 0.5f;     // m_watermark_kami_deleting_inactive

inline bool  g_wmGif        = false;
inline int   g_wmGifType    = 0;              // 0 original, 1 shit, 2 custom
inline char  g_wmGifPath[260] = "";
inline int   g_wmGifSide    = 0;              // 0 auto, 1 left, 2 right
inline float g_wmGifScale   = 100.f;
inline float g_wmGifRotation = 0.f;
inline float g_wmGifGap     = 4.f;
inline float g_wmGifOffsetX = 0.f;
inline float g_wmGifOffsetY = 0.f;
inline float g_wmGifAlpha   = 100.f;
inline bool  g_wmGifFlip    = false;

// runtime, not saved. botox g_trick_time (points_on_trick): GetTickCount64 of the last detected
// trick, written by detections.cpp on the movement thread. g_wmEnemySpotted: written by the aim
// Paint hook (aim/wm_aim.cpp), read by the dna watermark on the render thread.
inline std::atomic<unsigned long long> g_wmTrickMs{ 0 };
inline std::atomic<bool>               g_wmEnemySpotted{ false };
