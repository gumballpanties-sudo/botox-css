#pragma once
// airstuck_pure.h -- pure math shared by airstuck.inl / pixelsurf.inl (botox air_stuck.cpp +
// tick_scale.h ports). No engine, so tests/airstuck_test.cpp can run it.
#include <algorithm>
#include <cmath>
#include "css_moves_pure.h"   // cssm::PinnedVz (CSS clip nudge)

namespace as
{

constexpr float k_pi = 3.14159265358979323846f;

// botox stamp_rotate: a move proven in the `yaw` frame, re-expressed in the `view_yaw` frame
inline void StampRotate(float view_yaw, float yaw, float fwd, float side, float& out_fwd, float& out_side)
{
    const float delta = std::remainder(view_yaw - yaw, 360.0f) * (k_pi / 180.f);
    const float cos_d = std::cos(delta);
    const float sin_d = std::sin(delta);
    out_fwd  = cos_d * fwd - sin_d * side;
    out_side = sin_d * fwd + cos_d * side;
}

// botox set_move_toward_yaw: world yaw + magnitude -> forward/side in the cmd's view frame
inline void MoveTowardYaw(float view_yaw, float target_yaw, float forward, float& out_fwd, float& out_side)
{
    const float rotation = (target_yaw - view_yaw) * (k_pi / 180.f);
    out_fwd  = std::cos(rotation) * forward;
    out_side = -std::sin(rotation) * forward;
}

// botox n_tick::scale / n_tick::ticks: counts tuned at 64 tick, scaled to the live interval
inline float TickScale(float interval) { return std::clamp(64.f * interval, 0.25f, 4.f); }
inline int TicksScaled(int ticks_at_64, float interval)
{
    const float s = TickScale(interval);
    if (s <= 0.f) return ticks_at_64;
    const int out = static_cast<int>(std::round(static_cast<float>(ticks_at_64) / s));
    return out < 1 ? 1 : out;
}

// botox air_stuck clarity grid: forward per step so one step moves wishvel 1/32u
// ( AirAccelerate: accelspeed = airaccel * wishspeed * ipt * friction, wishspeed = fwd/100*.. )
inline float GridStep(float airaccel, float ipt, float friction, float duck_mul)
{
    return (0.03125f / (airaccel * ipt * friction * duck_mul * ipt)) / 100.0f;
}

// botox air_stuck clarity (air_stuck.cpp:437-455): a slanted wall's two cardinal creep yaws.
// axis = exact-axis brush (clarity skips it, lumi owns those); off = wall yaw past its nearest
// cardinal; yaws[0] = the cardinal 90 deg round toward the face, yaws[1] = the nearest one;
// first = 1 when off == 0 (only the nearest cardinal makes sense)
struct ClarityYaws { bool axis = false, nearAxis = false; float off = 0.f, yaws[2] = { 0.f, 0.f }; int first = 0; };
inline ClarityYaws Clarity(float nx, float ny)
{
    ClarityYaws c;
    c.axis = std::fabs(nx) == 1.f || std::fabs(ny) == 1.f;
    const float wallYaw = std::atan2(-ny, -nx) * (180.f / k_pi);
    const float base    = std::round(wallYaw / 90.f) * 90.f;
    c.off      = std::remainder(wallYaw - base, 360.f);
    c.nearAxis = std::fabs(c.off) < 10.f;
    c.yaws[0]  = std::remainder(base + (c.off > 0.f ? 90.f : -90.f), 360.f);
    c.yaws[1]  = std::remainder(base, 360.f);
    c.first    = c.off == 0.f ? 1 : 0;
    return c;
}

// botox tb_wallstrafe_yaw: the along-wall strafe yaw that keeps the most speed under the 30 u/s
// air wish cap. FLT_MAX-style sentinel returned as `ok = false`
inline bool WallStrafeYaw(float nx_in, float ny_in, float vx, float vy, float cap, float& out_yaw)
{
    if (std::sqrt(vx * vx + vy * vy) < 1.f) return false;
    const float nl = std::sqrt(nx_in * nx_in + ny_in * ny_in);
    if (nl <= 0.f) return false;
    const float nx = nx_in / nl, ny = ny_in / nl;
    float tx = -ny, ty = nx;
    if (tx * vx + ty * vy < 0.f) { tx = -tx; ty = -ty; }
    const float s = tx * vx + ty * vy;
    float c = 1.f, sn = 0.f;
    if (s >= cap * 0.5f)
    {
        c  = std::clamp(cap / (s * 2.f), -1.f, 1.f);
        sn = std::sqrt(1.f - c * c);
    }
    out_yaw = std::atan2(c * ty - sn * ny, c * tx - sn * nx) * (180.f / k_pi);
    return true;
}

// park brake: wish magnitude straight against xy that takes xy to EXACTLY 0 in one AirAccelerate
// tick when it can ( accelspeed = airaccel * wish * ipt ), else the full press. Never reverses:
// a maxspeed crop below the result only brakes less
inline float BrakeWish(float xy, float airaccel, float ipt, float cap = 450.f)
{
    const float a = airaccel * ipt;
    if (a <= 0.f || xy <= 0.f) return 0.f;
    return std::min(cap, xy / a);
}

// botox pixel_surf `pinned`: vz on the pin AND still sliding ( xy < 1 = a wall hang, not a surf ).
// pin = TargetPredictZVel (< 0). CSS: a clipped vz sits at pin +-1/32 (clip nudge), botox's exact
// |vz - pin| < 0.01 never matched one -- why the 11-07 port of this never latched
inline bool SurfPinned(float vz, float vxy, float pin) { return cssm::PinnedVz(vz, -pin, 0.01f) && vxy >= 1.f; }
// wedged on the face: on the pin ( zeroed = exact, clipped = +-1/32 ) and not sliding
inline bool WallHang(float vz, float vxy, float pin) { return cssm::PinnedVz(vz, -pin, 0.01f) && vxy < 1.f; }

}   // namespace as
