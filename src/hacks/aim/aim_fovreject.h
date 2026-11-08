#pragma once
#include <cmath>

// BOUNDING-SPHERE FOV REJECT -- the cheap "can this target possibly be in FOV" test that runs
// before CAimbotGlobal::PlayerBoneInFOV spends a full bone rebuild on an enemy.
//
// Pure scalars on purpose: no Vec3, no SDK, so tests/aimfov_test.cpp can include it and
// brute-force the claim below against real sampled geometry.
//
// THE CLAIM, and the only reason this is safe: with the eye outside a sphere of radius r at
// distance d, NO point of that sphere deviates from the direction to the centre by more than
// asin(r/d). So the nearest any sphere point can come to the view axis is
// (angle to centre) - asin(r/d), and if THAT is still outside the FOV limit the whole sphere
// is. A sphere cannot be rotated out from under this -- a bounding BOX can, which is why the
// bound is a sphere.
//
// Everything is degrees except where noted. Callers pad the radius before calling (hitboxes
// reach a little past the collision bbox); the padding is the caller's business, not this
// function's -- it answers exactly the question it is asked.
namespace aimfov
{

// true = every point of the sphere is strictly outside the FOV cone, skip the target.
// false = the sphere may reach inside (or the eye is inside it), do the real work.
inline bool SphereOutsideFov(float flFovToCenter, float flDistToCenter, float flRadius, float flFovLimit)
{
    // eye inside or on the sphere: no angular bound exists (a point can sit in any direction)
    if (!(flDistToCenter > flRadius))
        return false;
    // guards a degenerate radius as well as the ratio: r < d is already established above
    const float flRatio = flRadius / flDistToCenter;
    if (!(flRatio >= 0.f) || flRatio >= 1.f)
        return false;
    const float flHalfAngle = std::asin(flRatio) * (180.f / 3.14159265358979323846f);
    return flFovToCenter - flHalfAngle > flFovLimit;
}

}
