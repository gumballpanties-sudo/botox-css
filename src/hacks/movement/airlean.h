#pragma once
#include <cmath>
// Air-accel geometry for the strafe features. Pure math, no engine reads, no globals --
// tests/airlean_test.cpp brute-forces Solve() against NewSpeed() below.
//
// One air tick, wish direction at angle theta off velocity (c = cos theta, s = sin theta),
// straight off CGameMovement::AirAccelerate:
//     wishspd  = min(wishSpeed, airCap)          // "Cap speed", airCap = GetAirSpeedCap() = 30
//     addspeed = wishspd - v*c                   // currentspeed = velocity dot wishdir
//     bail if addspeed <= 0
//     a        = min(accel * wishSpeed * dt, addspeed)
//     v'^2     = v^2 + 2*v*a*c + a^2
//
// wishSpeed is the cmd's move magnitude AFTER the engine clamps it: CTFGameMovement::AirMove
// scales the wish down to mv->m_flMaxSpeed BEFORE calling AirAccelerate, and CheckParameters
// has already pinned m_flMaxSpeed to m_flClientMaxSpeed == the player's m_flMaxspeed netvar.
// Feeding the raw 450/636 in here (what the old AutoStrafe did) overstates a by ~1.6x, which
// only mattered for the lean -- pass the clamped value.
namespace airlean
{

struct Lean
{
    float cosGain;   // cos(theta) of the wish that gains the most speed this tick
    float cosTurn;   // furthest leaned-BACK cos(theta) that still costs zero speed
};

// the engine's answer for one tick, used by the test as ground truth
inline float NewSpeed(float v, float c, float wishSpeed, float accel, float dt, float airCap)
{
    const float cap = wishSpeed < airCap ? wishSpeed : airCap;
    const float add = cap - v * c;
    if (add <= 0.f) return v;                       // AirAccelerate returns, velocity untouched
    float a = accel * wishSpeed * dt;
    if (a > add) a = add;
    const float s2 = 1.f - c * c;
    const float s = s2 > 0.f ? sqrtf(s2) : 0.f;
    const float fwd = v + a * c;
    return sqrtf(fwd * fwd + (a * s) * (a * s));
}

inline Lean Solve(float v, float wishSpeed, float accel, float dt, float airCap)
{
    const float cap = wishSpeed < airCap ? wishSpeed : airCap;
    const float a = accel * wishSpeed * dt;
    if (v < 1.f) v = 1.f;

    Lean o;

    // GAIN. Where the add cap binds (a >= addspeed) the tick is v'^2 = v^2 + cap^2 - (v*c)^2,
    // biggest at c = 0 -- perpendicular, which is TF2's normal case (a = 60 at scout speed vs
    // a 30 cap). Where the engine's own accel is the smaller of the two the wish can lean INTO
    // velocity by exactly the slack, (cap - a)/v, and gain more than perpendicular would.
    o.cosGain = (a >= cap) ? 0.f : (cap - a) / v;
    if (o.cosGain > 1.f) o.cosGain = 1.f;
    if (o.cosGain < 0.f) o.cosGain = 0.f;

    // TURN. v'^2 == v^2 has one non-trivial root, and it is the same expression in both regions:
    //   a binding   (a <= 2*cap): 2*v*a*c + a^2 = 0        -> c = -a/(2v)
    //   add binding (a >  2*cap): cap^2 - (v*c)^2 = 0, c<0 -> c = -cap/v
    // so c = -min(a/2, cap)/v. Past it the strafe is buying rotation with speed; short of it it
    // is leaving rotation on the table. At scout speed (v 400, a 60, cap 30) it is -0.075: the
    // wish sits 94.3 deg off velocity, takes the FULL 60u of accel instead of 30u, and rotates
    // 8.6 deg/tick against 4.3 deg/tick at perpendicular -- for free.
    const float half = a * 0.5f;
    const float free = half < cap ? half : cap;
    o.cosTurn = -free / v;
    if (o.cosTurn < -0.9f) o.cosTurn = -0.9f;       // slow speeds: keep sin(theta) alive
    if (o.cosTurn > o.cosGain) o.cosTurn = o.cosGain;
    return o;
}

} // namespace airlean
