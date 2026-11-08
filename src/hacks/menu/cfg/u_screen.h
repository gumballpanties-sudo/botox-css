#pragma once
#include <string>
// unit screen vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// The enable bools (g_sharpenEnable, g_vignetteEnable, ...) and the depth channel shift knobs
// predate this unit and stay in globals.h / dllmain.cpp; these are botox's sub-options.

// removals: "disable post processing" (TF2 has no mat_postprocess_enable: bloom + colour correction off)
inline bool  g_sfxDisablePostProcessing = false;

// third person (botox m_third_person_key = key_bind_t( 0, 1 ): no key, hold)
inline int   g_sfxThirdPersonKey       = 0;
inline int   g_sfxThirdPersonKeyMode   = 1;
inline bool  g_sfxThirdPersonCollision = true;

// aspect ratio
inline bool  g_sfxAspectHud          = false;
inline bool  g_sfxAspectCrosshair    = false;   // botox m_aspect_ratio_overlay
inline bool  g_sfxAspectCheatOverlay = false;

// sharpen / film grain / vignette / invert / posterize / deband / channel shift (color_correction.cpp)
inline float g_sfxSharpenStrength  = 0.65f;
inline float g_sfxSharpenThreshold = 0.f;
inline float g_sfxGrainIntensity   = 0.05f;
inline float g_sfxGrainSize        = 1.6f;
inline float g_sfxGrainRespPos     = 0.5f;
inline float g_sfxGrainRespRange   = 0.5f;
inline float g_sfxGrainRespMin     = 0.f;
inline float g_sfxVignetteColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline float g_sfxVignetteRadius   = 0.45f;
inline float g_sfxVignetteFeather  = 0.5f;
inline float g_sfxVignetteAspect   = 1.f;
inline float g_sfxVignetteBlur     = 0.f;
inline int   g_sfxInvertMode       = 0;
inline float g_sfxInvertStrength   = 1.f;
inline int   g_sfxPosterizeLevels  = 6;
inline float g_sfxPosterizeDither  = 0.f;
inline float g_sfxPosterizeStrength= 1.f;
inline int   g_sfxDebandIterations = 2;
inline float g_sfxDebandStrength   = 10.f;
inline float g_sfxDebandRadius     = 14.f;
inline float g_sfxDebandGrain      = 16.f;
inline float g_sfxShiftRedX   = 3.f,  g_sfxShiftRedY   = 0.f;
inline float g_sfxShiftGreenX = 0.f,  g_sfxShiftGreenY = 0.f;
inline float g_sfxShiftBlueX  = -3.f, g_sfxShiftBlueY  = 0.f;

// reflections (botox reflections.cpp; intensity / distance stay g_reflIntensity / g_reflDistance)
inline float g_sfxReflBlur          = 0.5f;
inline float g_sfxReflFalloff       = 0.f;
inline float g_sfxReflScale         = 1.f;
inline int   g_sfxReflQuality       = 2;
inline bool  g_sfxReflSmoothNormals = true;
