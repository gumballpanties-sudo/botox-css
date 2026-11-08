#pragma once
#include <cmath>

// Two-blob metaball envelope -- the membrane the "mitosis" movement indicator draws between a tag
// and the tag it splits off from (or merges back into).
//
// Every 2D metaball connector out there (varun.ca/metaballs, Hiroyuki Sato's paper.js metaball,
// the Illustrator "metaball connector" script) solves this with four tangent points and four
// cubic beziers. Not done here: imgui 1.89.6 has no concave fill, so a bezier outline would have
// to be tessellated and split into convex pieces anyway. Slicing the field along the axis gives
// the same curve in one quadratic per slice, and every slice quad is convex by construction.
//
// Field: the classic inverse-square blend, iso surface at F = 1
//     F(p) = r1^2 / |p-c1|^2 + r2^2 / |p-c2|^2
// With both centres on one axis (c1 at 0, c2 at d) and u = h^2 the half width at axial pos s:
//     r1^2/(A+u) + r2^2/(B+u) = 1,   A = s^2,  B = (s-d)^2
//  => u^2 + u(A + B - r1^2 - r2^2) + A*B - r1^2*B - r2^2*A = 0
// Larger root = the outer surface. u <= 0 means s is past the tip (or in the gap once the neck
// has pinched), so the lobes separate on their own as d grows -- no extra "break" parameter.
namespace mb
{

// Half width of the surface at axial position s (s = 0 at centre 1, s = d at centre 2).
// 0 = nothing there.
inline float HalfWidth(float s, float d, float r1, float r2)
{
    const float A = s * s, B = (s - d) * (s - d);
    const float b = A + B - r1 * r1 - r2 * r2;
    const float c = A * B - r1 * r1 * B - r2 * r2 * A;
    const float disc = b * b - 4.f * c;
    if (disc <= 0.f) return 0.f;
    const float u = 0.5f * (-b + sqrtf(disc));
    return u > 0.f ? sqrtf(u) : 0.f;
}

// How far past a centre the surface can reach: a lone blob ends at r, two coincident blobs of
// radius r reach r*sqrt(2). 1.5 covers the worst case with a slice to spare.
inline float Pad(float r) { return r * 1.5f; }

}   // namespace mb
