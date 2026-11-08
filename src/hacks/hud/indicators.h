#pragma once
#include "../../globals/globals.h"

// botox hacks/indicators/indicators.cpp 1:1 (the movement half): velocity indicator, velocity
// graph, stamina indicator, keybind indicators (6 styles + detection particles + custom labels),
// keystrokes. Vars = botox variables.h, same defaults. Stamina source: CCSPlayer::m_flStamina
// (css-community-master cs_gamemovement.cpp, STAMINA_RECOVER_RATE=19) via CssBridge_GetStaminaFraction
// (src/hacks/aim/css_bridge.h/.cpp) -- 0 fresh .. 1 spent, scaled back to botox's 0..100 "stamina" unit.
// Everything draws on the render thread straight into the background list (botox queues on the
// game thread and replays; here the imgui frame build IS the paint pass).

// ---- botox variables.h enums ----------------------------------------------------------------
enum IndStyle { IND_STYLE_DEFAULT = 0, IND_STYLE_STATIC, IND_STYLE_SMOOTH, IND_STYLE_MITOSIS, IND_STYLE_KAMIDERE, IND_STYLE_SQUASH, IND_STYLE_KAMIBEBRA };
inline constexpr const char* kIndStyleNames = "default\0static\0smooth\0mitosis\0kamidere\0squash & stretch\0kamibebra\0";

enum IndSmoothOrigin { SMOOTH_ORIGIN_AUTO = 0, SMOOTH_ORIGIN_RIGHT, SMOOTH_ORIGIN_BOTTOM };
inline constexpr const char* kSmoothOriginNames = "auto\0right\0bottom\0";

enum IndPreLayout { PRE_LAYOUT_RIGHT = 0, PRE_LAYOUT_ABOVE, PRE_LAYOUT_BELOW };
inline constexpr const char* kPreLayoutNames = "right\0above\0below\0";

enum IndPreAlign { PRE_ALIGN_BOTTOM = 0, PRE_ALIGN_TOP };
inline constexpr const char* kPreAlignNames = "bottom\0top\0";

inline constexpr float kIndScaleMin = 0.1f, kIndScaleMax = 4.f;

// botox e_keybind_indicators, TF2 subset in botox order (no ladders, fireman, zeus, air freeze,
// one hop, edge surf: none exist here). Bit i of g_kiShow = row i of "displayed keybinds".
// New entries always go at the END, right before KI_COUNT -- bit i of g_kiShow is a
// saved config value, so inserting mid-list would silently reassign everyone's saved toggles.
enum KiToggle { KI_EB = 0, KI_PS, KI_EJ, KI_LJ, KI_DH, KI_MJ, KI_JB, KI_TB, KI_AS, KI_WC, KI_ST, KI_PSA,
                KI_AC, KI_AB, KI_ES, KI_HSW, KI_PH, KI_COUNT };
inline constexpr const char* kKiToggleNames[KI_COUNT] = {
    "edgebug", "pixelsurf", "edgejump", "longjump", "delayhop", "minijump", "jumpbug", "texturebug",
    "airstuck", "wallclimb", "autostrafe", "ps assist", "auto crouch", "auto bounce", "edge skip",
    "half sideways", "pixelhop" };

// botox e_keybind_labels, same subset, botox push order = draw order. { menu name, default label }
enum KiLabel { LBL_EB = 0, LBL_PS, LBL_EJ, LBL_LJ, LBL_DH, LBL_MJ, LBL_JB, LBL_TB, LBL_AIR, LBL_WC, LBL_AS,
               LBL_AST, LBL_AC, LBL_AB, LBL_ES, LBL_HSW, LBL_PH, LBL_COUNT };
inline constexpr const char* kKiLabels[LBL_COUNT][2] = {
    { "edgebug", "eb" },     { "pixelsurf", "ps" },       { "edgejump", "ej" },     { "longjump", "lj" },
    { "delayhop", "dh" },    { "minijump", "mj" },        { "jumpbug", "jb" },      { "texturebug", "tb" },
    { "airstuck", "air" },   { "wallclimb", "wc" },       { "autostrafe", "as" },   { "ps assist", "ast" },
    { "auto crouch", "ac" }, { "auto bounce", "ab" },
    { "edge skip", "es" },   { "half sideways", "hsw" },
    { "pixelhop", "ph" },
};

// ---- shared ---------------------------------------------------------------------------------
inline float g_indKamiShadowColor[4] = { 252 / 255.f, 193 / 255.f, 1.f, 1.f };   // m_indicator_kami_shadow_color
inline float g_indKamibebraAccent[4] = { 252 / 255.f, 194 / 255.f, 1.f, 1.f };   // m_indicator_kamibebra_accent

// ---- velocity indicator ---------------------------------------------------------------------
inline bool  g_viEnable       = false;
inline bool  g_viPreSpeed     = false;
inline bool  g_viFadeAlpha    = false;
inline bool  g_viCustomColor  = false;
inline int   g_viPadding      = 125;
inline int   g_viStyle        = 0;
inline int   g_viSmoothOrigin = 0;
inline bool  g_viShadow       = true;
inline float g_viShadowBlur   = 0.f;
inline float g_viShadowColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_viOutline      = false;
inline float g_viOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline int   g_viOutlinePx    = 1;
inline int   g_viShadowX      = 1;
inline int   g_viShadowY      = 1;
inline float g_viScale        = 1.f;
inline int   g_viPreLayout    = 0;
inline float g_viPreScale     = 1.f;
inline int   g_viPreAlign     = 0;
inline float g_viColor1[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_viColor2[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_viColor3[4] = { 255 / 255.f, 199 / 255.f,  89 / 255.f, 1.f };   // same speed
inline float g_viColor4[4] = { 255 / 255.f, 119 / 255.f, 119 / 255.f, 1.f };   // losing speed
inline float g_viColor5[4] = {  30 / 255.f, 255 / 255.f, 109 / 255.f, 1.f };   // gaining speed

// ---- stamina indicator ------------------------------------------------------------------------
// botox m_stamina_indicator*, same defaults (globals/config/variables.h:1414-1494). Only 2 blend
// colors (no speed-based 3/4/5 set, botox stamina has no "gaining/losing" tint).
inline bool  g_siEnable       = false;
inline int   g_siStyle        = 0;
inline int   g_siSmoothOrigin = 0;
inline float g_siColor1[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_siColor2[4] = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_siPreSpeed     = false;
inline int   g_siPreLayout    = 0;
inline float g_siPreScale     = 1.f;
inline int   g_siPreAlign     = 0;
inline bool  g_siFadeAlpha    = false;
inline bool  g_siShadow       = true;
inline float g_siShadowBlur   = 0.f;
inline float g_siShadowColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline int   g_siShadowX      = 1;
inline int   g_siShadowY      = 1;
inline bool  g_siOutline      = false;
inline float g_siOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline int   g_siOutlinePx    = 1;
inline float g_siScale        = 1.f;
inline int   g_siPadding      = 125;

// ---- velocity graph -------------------------------------------------------------------------
inline bool  g_vgEnable      = false;
inline int   g_vgWidth       = 300;
inline int   g_vgHeight      = 60;
inline int   g_vgPosition    = 80;
inline int   g_vgOffsetX     = 0;
inline int   g_vgLength      = 200;
inline float g_vgMax         = 0.f;   // 0 = auto
inline float g_vgThickness   = 1.5f;
inline float g_vgColor[4]     = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_vgGainColors  = false;
inline float g_vgColorGain[4] = {  30 / 255.f, 1.f, 109 / 255.f, 1.f };
inline float g_vgColorLoss[4] = { 1.f, 119 / 255.f, 119 / 255.f, 1.f };
inline bool  g_vgFadeSides   = true;
inline float g_vgFade        = 0.2f;
inline bool  g_vgFill        = false;
inline float g_vgFillColor[4] = { 1.f, 1.f, 1.f, 40 / 255.f };
inline bool  g_vgJumpSpeeds  = true;
inline float g_vgTextColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_vgTextScale   = 0.75f;
inline bool  g_vgTextShadow  = true;

// ---- keybind indicators ---------------------------------------------------------------------
inline bool  g_kiEnable       = false;
inline int   g_kiStyle        = 0;
inline int   g_kiSmoothOrigin = 0;
inline float g_kiStaticFade   = 0.f;
inline float g_kiColor[4]        = { 1.f, 1.f, 1.f, 1.f };
inline float g_kiColorSuccess[4] = { 0.f, 1.f, 0.f, 1.f };
inline float g_kiColorPredict[4] = { 0.f, 1.f, 0.f, 1.f };
inline bool  g_kiHorizontal   = false;
inline int   g_kiSpacing      = 0;
inline bool  g_kiShadow       = true;
inline float g_kiShadowBlur   = 0.f;
inline float g_kiShadowColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_kiShadowDetect = false;
inline int   g_kiShadowX      = 1;
inline int   g_kiShadowY      = 1;
inline bool  g_kiOutline      = false;
inline float g_kiOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline int   g_kiOutlinePx    = 1;
inline float g_kiScale        = 1.f;
inline bool  g_kiParticles    = false;
inline bool  g_kiParticleRandom   = false;
inline float g_kiParticleColor[4] = { 175 / 255.f, 210 / 255.f, 170 / 255.f, 1.f };
inline int   g_kiParticleAmount   = 12;
inline float g_kiParticleSize     = 3.f;
inline float g_kiParticleSpeed    = 140.f;
inline float g_kiParticleGravity  = 90.f;
inline float g_kiParticleLife     = 0.8f;
inline float g_kiParticleCooldown = 0.25f;
inline int   g_kiPosition     = 100;
inline int   g_kiShow         = 0;       // bit per KiToggle; botox m_key_indicators, all off
inline bool  g_kiCustomLabels = false;
inline char  g_kiLabels[LBL_COUNT][16] = {};   // empty = stock label (botox buffer 16)

// ---- keystroke indicators -------------------------------------------------------------------
inline bool  g_kpEnable       = false;
inline int   g_kpLayout       = 0;
inline int   g_kpReleased     = 0;
inline bool  g_kpDuckJump     = true;
inline bool  g_kpMouse        = false;
inline float g_kpColor[4]         = { 1.f, 1.f, 1.f, 1.f };
inline float g_kpColorReleased[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_kpFade         = 0.f;
inline int   g_kpGapX         = 10;
inline int   g_kpGapY         = 4;
inline int   g_kpPosition     = 200;
inline int   g_kpOffsetX      = 0;
inline bool  g_kpShadow       = true;
inline float g_kpShadowBlur   = 0.f;
inline bool  g_kpOutline      = false;
inline float g_kpOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline int   g_kpOutlinePx    = 1;
inline int   g_kpShadowX      = 1;
inline int   g_kpShadowY      = 1;
inline float g_kpScale        = 1.f;

// botox on_paint_traverse order: velocity, graph, keybinds, particles, keystrokes
void RenderIndicators();

// botox release_textures: the per-frame texture pool (DEFAULT pool on a refused MANAGED). h_Reset / device rebind
void Indicators_ReleaseTextures();

// botox g_render.indicator_font: the indicator face baked at `scale` when the atlas holds that
// size, else the base face; fs = base size * scale either way (render.cpp owns the atlas)
ImFont* IndicatorFont(float scale, float& fs);
