#pragma once
// movement > points page vars (botox variables.h m_points_* / m_point_*). One look per point
// kind, read by movement/points_style.h (render.cpp ps/pj/finder/calc draws + the menu preview).
// Per-kind arrays are indexed by PointStyleId (points_style.h): ps, hb, pj, calc.
inline bool  g_pointsRenderPs       = true;
inline bool  g_pointsRenderCalc     = true;
inline bool  g_pointsRenderFinder   = true;
inline float g_pointsOpacity        = 1.f;
inline float g_pointsMaxDist        = 1500.f;
inline bool  g_pointsRadiusColor    = false;
inline float g_pointsRadiusClr[4]   = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_pointsDelete         = true;
inline int   g_pointsDeleteKey      = 0x08;   // VK_BACK
inline float g_pointsSize           = 1.f;
inline float g_pointsThickness      = 1.f;
inline float g_pointsFill           = 0.4f;
inline float g_pointsHoverScale     = 1.5f;
inline bool  g_pointsDistanceFade   = true;
inline bool  g_pointsDistanceShrink = true;
inline bool  g_pointsLabels         = false;
inline float g_pointsDesignSize     = 0.8f;
inline bool  g_pointsGlow           = false;
inline bool  g_pointsPulse          = false;
inline int   g_pointGaps[4]         = { 0, 2, 2, 0 };          // ps, hb, pj, calc (bits 1 top 2 bottom 4 left 8 right)
inline int   g_pointShape[4]        = { 0, 0, 0, 0 };          // ps, hb, pj, calc
inline bool  g_pointDesign[3]       = { true, true, true };    // ps, hb, pj
inline int   g_pointDesignType[3]   = { 0, 0, 0 };             // ps, hb, pj
inline float g_pointClrPfPs[4]      = { 0.f, 1.f, 0.f, 1.f };
inline float g_pointClrPfHb[4]      = { 1.f, 0.f, 0.f, 1.f };
inline float g_pointClrPfPj[4]      = { 1.f, 220.f / 255.f, 0.f, 1.f };
inline float g_pointClrPfTb[4]      = { 0.f, 200.f / 255.f, 1.f, 1.f };
