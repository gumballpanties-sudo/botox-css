#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Unit hud pure logic, shared by hud_extra.cpp (render) and aim/hud_aim.cpp (game thread).
// Gate test: tests/hudmath_test.cpp.
namespace hudm
{
// CNetGraphPanel::GetColorFromVariance: 0 = normal colour, 1/2/3 = warn1/2/3
inline int NgVariance(float value, float base, float v1, float v2, float v3)
{
    if (base == 0.f) return 0;
    const float d = std::fabs(base - value) / base;
    return d > v3 ? 3 : d > v2 ? 2 : d > v1 ? 1 : 0;
}

// botox rate_color: under the tick rate grades by variance, over it is always the worst warn
inline int NgRateLevel(float rate, float tickRate)
{
    if (rate < tickRate) return NgVariance(rate, tickRate, 0.f, 0.2f, 0.5f);
    return rate > tickRate ? 3 : 0;
}

// CNetGraphPanel::GraphGetXY x for net_graphpos 0 left / 1 right / 2 centre / n = XRES(n)
inline int NgGraphX(int pos, int sw, int w)
{
    return pos == 0 ? 5 : pos == 1 ? sw - 5 - w : pos == 2 ? (sw - 10 - w) / 2
                                                            : std::clamp(pos * sw / 640, 5, std::max(5, sw - w - 5));
}

// GraphGetXY y with ComputeNetgraphHeight: max(lines * font, net_graphheight) above the bottom
inline int NgGraphY(int sh, int lines, int fontH, int graphHeight)
{
    return sh - 24 - 5 - std::max(lines * fontH, graphHeight);
}
}
