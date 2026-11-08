#pragma once
#include <cmath>
#include <algorithm>

// THE BROADPHASE BOUND FOR MV_DetectWallLines (movement.cpp).
//
// That scan loops 16 directions and each one starts with a hull trace, so "there is no wall
// anywhere near me" costs 16 hull traces -- and TextureBug's ladder runs the scan twice on a
// full miss, so 32 traces per tick to conclude nothing is there. The fix is one zero-length
// hull trace, expanded to cover everything the 16 directions could possibly touch: miss it and
// the whole scan can return false immediately.
//
// This file is ONLY the expansion radius, kept pure so tests/mvscan_test.cpp can brute-force it
// against the loop's own per-direction arithmetic. Get it too small and the scan silently stops
// finding walls it used to find, which is exactly the kind of bug nothing in game points at.
//
// What the loop reaches, per direction (movement.cpp, MV_DetectWallLines):
//   lookahead   = max(0, dot(vel.xy, dir) * dt)              <= |vel.xy| * dt
//   support     = min(half_x / |dir.x|, half_y / |dir.y|)    <= hypot(half_x, half_y)
//   ray_length  = support + extra_reach + lookahead          (column sweep + every line trace)
//   pre_length  = extra_reach + lookahead                    (hull prefilter, swept from centre,
//                                                             so it also carries its own corner)
// Both families are bounded in xy by hypot(half_x, half_y) + extra_reach + |vel.xy| * dt.
//
// `support` reaching the hull DIAGONAL and not half_x is the part that is easy to get wrong:
// min(hx/|cos|, hy/|sin|) is largest where the two terms meet, at tan = hy/hx, and there it is
// hypot(hx, hy) -- 33.94 for a 24x24 player hull, not 24.
namespace mv
{

// slack for float error in the caller's own trig; a unit of over-reach costs nothing here
inline constexpr float k_scanBoundSlack = 1.f;

// half-extent, in x and in y, of the box that contains every trace the 16-direction scan can
// run. Centre it on the scan origin. The z range is the caller's business: it is the hull's own
// [mins.z - fall_look, maxs.z], which no direction widens.
inline float ScanReachXY(float flHalfX, float flHalfY, float flExtraReach, float flVel2D, float flDt)
{
    const float flHalfX2 = std::fabs(flHalfX), flHalfY2 = std::fabs(flHalfY);
    const float flDiag = std::sqrt(flHalfX2 * flHalfX2 + flHalfY2 * flHalfY2);
    const float flLook = std::max(0.f, flVel2D) * std::max(0.f, flDt);
    return flDiag + std::max(0.f, flExtraReach) + flLook + k_scanBoundSlack;
}

}
