#pragma once
#include <cmath>

// Pure angle/decompose math for the non-fireman ladder features: fast_ladder, ladder_glide,
// ladder_freelook_climb, anti_ladder, auto_one_hop_onladder. No engine reads, no globals --
// tests/ladder_test.cpp. Shares fireman_pure.h's CanFling face test (same engine rule, same
// grab) rather than duplicating it.
namespace ladderfx
{

inline constexpr float kPi = 3.14159265358979323846f;

inline float NormalizeDeg(float a)
{
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

// forward/side pair (view space) that points the wish at targetYawDeg, magnitude speed.
// csgo-sdk movement.cpp auto_one_hop's air-strafe decompose -- pure geometry, not engine
// specific, so it ports unchanged: forward = cos(rot)*speed, side = -sin(rot)*speed,
// rot = targetYaw - viewYaw.
struct ForwardSide { float forward, side; };
inline ForwardSide WishToYaw(float targetYawDeg, float viewYawDeg, float speed)
{
    const float rot = (targetYawDeg - viewYawDeg) * (kPi / 180.f);
    return { std::cos(rot) * speed, -std::sin(rot) * speed };
}

// fast_ladder / ladder_glide: nudge the view a degree at a time toward the ladder face while
// strafing (csgo-sdk movement.cpp fast_ladder/ladder_glide -- same decompose both features use,
// pure sign logic, no engine read).
inline float LadderViewNudge(bool sideLeft, bool sideRight, bool fwdHeld, bool backHeld)
{
    float d = 0.f;
    if (fwdHeld)  d += sideRight ? -1.f : (sideLeft ? 1.f : 0.f);
    if (backHeld) d += sideRight ? 1.f : (sideLeft ? -1.f : 0.f);
    return d;
}

// auto_one_hop_onladder's climb buttons: CSS LadderMove reads BUTTONS (IN_FORWARD/IN_BACK/
// IN_MOVELEFT/IN_MOVERIGHT), not forward/side floats, once already on the ladder
// (gamemovement.cpp:2922-2932, CCSGameMovement::LadderMove just calls BaseClass::LadderMove(),
// cs_gamemovement.cpp:462 -- no CS:GO-specific override). Nothing to compute here, kept as a
// named constant so engine glue doesn't re-derive which bits matter: IN_FORWARD climbs up the
// face, IN_BACK climbs down, pure button bits.

// A feature that only writes forward/side FLOATS while attached (cmd->forwardmove/sidemove)
// moves nothing -- the climb branch above ignores them. This derives the button bits a wish
// vector needs so steering code that computes forward/side (WishToYaw et al.) still does
// something once MOVETYPE_LADDER is live. Mirrors botox anti_ladder's own 8 literal button
// combos (movement.cpp:2572-2599: forward/back/strafe/idle/diag), just phrased as a sign test.
struct LadderBtns { bool fwd = false, back = false, left = false, right = false; };
inline LadderBtns WishToButtons(float forward, float side, float deadband = 1.f)
{
    LadderBtns b;
    b.fwd   = forward >  deadband;
    b.back  = forward < -deadband;
    b.right = side    >  deadband;
    b.left  = side    < -deadband;
    return b;
}

} // namespace ladderfx
