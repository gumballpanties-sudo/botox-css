#pragma once
#include <cmath>
// Unit A2 port: the pure-math decisions out of movement_edge.inl (edge skip + blockbot),
// pulled out so they're testable without the engine (see tests/movementedge_test.cpp). No
// engine types, no includes -- plain in/out, same split as movement_ex_pure.h.

// Edge skip: botox's n_tick::ticks(n) -- a tick count written for a 64-tick reference, scaled
// to the game's real rate. Matches movement_edge.inl's EsTicks (same formula, tickRate = 1/dt
// passed in here instead of read from the engine). Never returns under 1.
inline int EsTicksPure(int ticksAt64, float tickRate)
{
    int out = (int)(((float)ticksAt64 * tickRate / 64.f) + 0.5f);
    return out < 1 ? 1 : out;
}

// Edge skip: botox perfect_angle(), the yaw offset off velocity for a SIDE wish (side +-450 is
// already 90 deg off the view). cosGain = (cap - accel) / speed (airlean::Solve), 0 when the air
// cap binds -- always, in TF2 -- and then the side wish sits square to velocity: offset 0.
inline float EsSideWishOffsetDeg(float cosGain)
{
    const float c = cosGain < -1.f ? -1.f : (cosGain > 1.f ? 1.f : cosGain);
    return fabsf(90.f - acosf(c) * 57.29577951f);
}

// Edge skip: has the search drifted more than maxBend degrees off its original course?
// maxBend >= 180 means the bend check is off (botox bent(), matches movement_edge.inl's EsBent,
// angleDelta is NormalizeAngle(currentYaw - courseYaw) already wrapped to +-180 by the caller).
inline bool EsBentPure(float angleDelta, float maxBend)
{
    if (maxBend >= 180.f) return false;
    return fabsf(angleDelta) > maxBend;
}

// Blockbot head mode: match the target's velocity 2x minus our own, rotated into cmd space.
// Matches botox movement.cpp:2273 (`want*2 - lv`) before the forward/side yaw rotation.
inline float BlockBotHeadWish(float targetVel, float myVel)
{
    return targetVel * 2.f - myVel;
}

// Blockbot strafe mode: which way to step given the yaw delta to the target (deadzone +-0.25
// degrees, matches botox movement.cpp:2286). Returns -1 (step left/negative side), +1
// (step right/positive side), or 0 (inside the deadzone, no input).
inline int BlockBotStrafeSign(float yawDelta)
{
    if (yawDelta > 0.25f) return -1;
    if (yawDelta < -0.25f) return 1;
    return 0;
}
