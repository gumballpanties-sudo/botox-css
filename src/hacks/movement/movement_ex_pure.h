#pragma once
#include <cmath>
// Unit A port: the handful of pure-math decisions out of movement_ex.inl / LongJump's
// adaptive-cancel append, pulled out so they're testable without the engine (see
// tests/movementex_test.cpp). No engine types, no includes -- plain in/out.

// Hsw: which way the sideways press goes. side: 0 = right-biased, 1 = left-biased.
// Matches botox movement.cpp:570 (`s = (side==0?1:-1) * (grounded?1:-1)`).
inline int HswSign(int side, bool grounded)
{
    return (side == 0 ? 1 : -1) * (grounded ? 1 : -1);
}

// Delay hop: true once `wait` ticks of air time have passed since the last air tick.
// Matches botox movement.cpp:635 (`cmd->tick_count > last_air_tick + wait`).
inline bool DelayHopShouldJump(int tickNow, int lastAirTick, int wait)
{
    return tickNow > lastAirTick + wait;
}

// Long jump adaptive cancel: true the first tick any ENABLED stop reason fires.
// Matches botox movement.cpp:1710-1727 (none ticked = landed, the hard fallback that
// keeps the cancel from running forever is handled by the caller passing stopLanded=true
// when the vector has nothing else set).
// botox's "stop cancelling when" is ONE MultiCombo (m_long_jump_adaptive_stop, vector<bool>[3]) --
// ported here as a bitmask (g_longJumpAdaptiveStopMask, cfg/u_mvcore.h) instead of 3 separate
// bools, bit order matching the MultiCombo's { "let go", "landed", "time passes" } (11-03 tabs round).
inline constexpr int kLjStopLetGo = 1 << 0;
inline constexpr int kLjStopLanded = 1 << 1;
inline constexpr int kLjStopTimed = 1 << 2;

inline bool LjAdaptiveShouldCancel(int stopMask, bool keyUp, bool groundedNow, bool timeUp)
{
    return ((stopMask & kLjStopLetGo) && keyUp) || ((stopMask & kLjStopLanded) && groundedNow)
        || ((stopMask & kLjStopTimed) && timeUp);
}

// Fast stop cmd magnitude. One grounded CSS tick: Friction (gamemovement.cpp Friction:
// drop = max(speed, stopspeed) * friction * sf * dt) then Accelerate along -vel (addspeed =
// wish + speed never clamps, accelspeed = accel * dt * wish * sf). Returns the cmd move that
// lands the tick at 0: capped to maxspeed while fast, exact on the last tick. Ducked = engine
// scales the cmd by 1/3 (PLAYER_DUCKING_MULTIPLIER) first. 0 = friction alone stops it.
inline float FastStopBrakeWish(float speed, float dt, float friction, float stopspeed, float accel,
                               float sf, float maxspeed, bool ducked)
{
    const float control = speed < stopspeed ? stopspeed : speed;
    const float after = speed - control * friction * sf * dt;
    const float perWish = accel * dt * sf;
    if (after <= 0.f || perWish <= 0.f) return 0.f;
    float wish = after / perWish;
    if (wish > maxspeed) wish = maxspeed;
    if (ducked) wish *= 3.f;
    return wish > 450.f ? 450.f : wish;
}

// Fast stop direction: rotation = deg2rad((velocity*-1).yaw - view yaw);
//        forward_move = cos(rotation)*wish; side_move = -sin(rotation)*wish.
// velYawDeg/viewYawDeg: VectorAngles(velocity).y / cmd->viewangles.y, in degrees.
inline void FastStopWish(float velYawDeg, float viewYawDeg, float wish, float& fwd, float& side)
{
    const float rotation = (velYawDeg + 180.f - viewYawDeg) * (3.14159265358979323846f / 180.f);
    fwd  = cosf(rotation) * wish;
    side = -sinf(rotation) * wish;
}
