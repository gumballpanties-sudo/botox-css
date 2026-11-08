#pragma once
// botox globals/render/render.cpp dpi math, pure (tests/dpi_test.cpp). render/dpi.cpp is the layer
// that drives it.
#include <cmath>

// settings slider percent -> scale. botox wanted_dpi_scale: NaN = 100 %, clamped 25-200 %, within
// 0.1 % of 100 = exactly 1 (every "== 1" fast path below keys off that).
inline float DpiWantedScale(float percent)
{
    if (!std::isfinite(percent)) percent = 100.f;
    percent = percent < 25.f ? 25.f : (percent > 200.f ? 200.f : percent);
    const float scale = percent / 100.f;
    return std::fabs(scale - 1.f) < 0.001f ? 1.f : scale;
}

// botox dpi_pivot: per axis from the block's centre. outer 20 % of the screen = that edge, so
// corner-stacked panels grow as one and never overlap; middle 60 % slides edge to edge. slope
// 1 / 0.6 < 2 keeps the shown position monotonic up to 200 %.
constexpr float kDpiEdgeZone = 0.2f;

inline float DpiPivot(float centre, float screen)
{
    if (screen <= 0.f) return 0.f;
    const float lo = screen * kDpiEdgeZone, hi = screen * (1.f - kDpiEdgeZone);
    float t = (centre - lo) / (hi - lo);
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * screen;
}

// layout lo (size wide) -> where the scaled panel's lo lands on screen
inline float DpiAxisShown(float lo, float size, float screen, float scale)
{
    const float pivot = DpiPivot(lo + size * 0.5f, screen);
    return pivot + (lo - pivot) * scale;
}

// inverse of DpiAxisShown (a drag: where the mouse put the panel -> its layout position)
inline float DpiAxisLayout(float shown, float size, float screen, float scale)
{
    const float half = size * 0.5f, lo = screen * kDpiEdgeZone, hi = screen * (1.f - kDpiEdgeZone);
    if (shown <= DpiAxisShown(lo - half, size, screen, scale)) return shown / scale;
    if (shown >= DpiAxisShown(hi - half, size, screen, scale)) return screen + (shown - screen) / scale;
    const float slope = screen / (hi - lo);
    return (shown - slope * (half - lo) * (1.f - scale)) / (scale + slope * (1.f - scale));
}

// botox unscaled_axis_*: a 1:1 window ("dpi scale panels" off) keeps its share of the free room,
// laid out in logical px (screen / scale), shown in screen px
inline float DpiUnscaledShown(float lo, float size, float screen, float scale)
{
    const float roomLogical = screen / scale - size, roomScreen = screen - size;
    return roomLogical > 1.f && roomScreen > 0.f ? lo * roomScreen / roomLogical : lo * scale;
}

inline float DpiUnscaledLayout(float shown, float size, float screen, float scale)
{
    const float roomLogical = screen / scale - size, roomScreen = screen - size;
    return roomLogical > 1.f && roomScreen > 0.f ? shown * roomLogical / roomScreen : shown / scale;
}

// botox: pivot x (1 - scale) whole, so twin glyphs snapped to whole scaled px land on whole
// screen px (<= 0.5 px shift)
inline float DpiSnapPivot(float pivot, float scale)
{
    const float k = 1.f - scale;
    if (k == 0.f) return pivot;
    const float v = pivot * k + 0.5f;
    const float f = (float)((v >= 0 || (float)(int)v == v) ? (int)v : (int)v - 1);   // ImFloorSigned
    return f / k;
}
