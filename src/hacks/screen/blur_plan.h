#pragma once
#include <cmath>

// lua render.blur strength -> blur_rect passes ( BlurRectSmooth ). Same bytes in botox-tf2 + botox-css, gate:
// botox csgo-sdk tools/lua_blur_check.py ( simulates the passes, measures the radius they reach ).
// Pass k = 1..n is fx_bloom_blur.hlsl's 9 tap gaussian with its taps k * f texels apart. One pass at spacing s adds
// variance 2.679 s^2 ( the taps ) + ~0.175 ( the linear filter between texels, exact at s = 1 ), passes add up.
// n = fewest passes that reach the radius at f <= 1 ( a tap never jumps past the blur it samples ), f then lands it.
struct BlurPlan { int passes; float fx, fy; };

inline constexpr float k_blurStrengthPx = 34.f;   // strength 1 = botox csgo screen_blur_rect_smooth amount 1 ( menu blur at 100 )
inline constexpr float k_blurTapVar     = 2.679f;
inline constexpr float k_blurFilterVar  = 0.175f;
inline constexpr int   k_blurMaxPasses  = 24;     // 114 texels at f = 1, past that f grows

// radius per axis in scratch texels
inline BlurPlan BlurPlanFor(float sigmaX, float sigmaY)
{
    const float want = sigmaX > sigmaY ? sigmaX : sigmaY;
    int n = 0;
    float taps = 0.f;   // 1 + 4 + .. + n^2
    do
    {
        ++n;
        taps += static_cast<float>(n * n);
    } while (k_blurTapVar * taps + k_blurFilterVar * n < want * want && n < k_blurMaxPasses);

    const auto spacing = [&](float sigma) { return std::sqrt((std::fmax)(sigma * sigma - k_blurFilterVar * n, 0.f) / (k_blurTapVar * taps)); };
    return { n, spacing(sigmaX), spacing(sigmaY) };
}
