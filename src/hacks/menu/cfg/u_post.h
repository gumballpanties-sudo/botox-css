#pragma once
// unit post vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.

// depth of field (botox variables.h:901-925)
inline float g_dofFocusX      = 0.5f;    // auto focus aim point, 0..1 of the screen
inline float g_dofFocusY      = 0.5f;
inline bool  g_dofSeeThrough  = false;   // focus trace ignores glass / grates / debris
inline int   g_dofQuality     = 2;       // tap ceiling 16 << n (16/32/64/128)
inline bool  g_dofDebugDepth  = false;   // show depth: linear depth, white at g_dofDebugWhite
inline float g_dofDebugWhite  = 2000.f;
inline bool  g_dofDebugNear   = false;   // show near bleed: the blurred near coc field

// shared depth source: z swap ("universal depth", botox m_depth_force_live)
inline bool  g_fxUniversalDepth = false;

// motion blur = the engine's own mat_motion_blur_* (botox variables.h:979-986)
inline bool  g_mbEnable    = false;
inline bool  g_mbForward   = false;
inline float g_mbStrength  = 2.f;
inline float g_mbRotation  = 1.f;
inline float g_mbFalling   = 0.f;
inline float g_mbFallMin   = 5.f;
inline float g_mbFallMax   = 10.f;

// true motion blur extras (botox variables.h:992-1000)
inline float g_fbObject       = 0.f;     // player trail: history blend, x shutter
inline float g_fbVmSway       = 0.f;     // viewmodel sway: the gun's lag behind a turn
inline float g_fbGamma        = 2.f;     // blur gamma: 2 = the old look

// bloom (botox variables.h:1007)
inline float g_bloomAspect = 0.f;        // + = wide, - = tall
