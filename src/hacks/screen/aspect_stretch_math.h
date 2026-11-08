#pragma once
// botox globals/render/render.cpp overlay stretch math + resolution_spoof.cpp aspect_scale(),
// pure (tests/aspect_stretch_test.cpp).

// screen aspect / wanted aspect, clamped like botox. 1 = nothing to stretch.
inline float AsScale(float screenW, float screenH, float wanted)
{
    if (wanted <= 0.f || screenW <= 0.f || screenH <= 0.f) return 1.f;
    const float s = (screenW / screenH) / wanted;
    return s < 0.25f ? 0.25f : (s > 4.f ? 4.f : s);
}

// botox stretch_pivot: a block keeps its share of the free room on either side, so a panel on
// the left edge grows rightwards, one on the right edge leftwards, a centred one both ways.
inline float AsPivot(float lo, float hi, float screenW)
{
    const float room = screenW - (hi - lo);
    if (room <= 1.f) return lo + (hi - lo) * 0.5f;
    float t = lo / room;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return lo + (hi - lo) * t;
}

inline float AsStretchX(float x, float pivot, float scale) { return pivot + (x - pivot) * scale; }
