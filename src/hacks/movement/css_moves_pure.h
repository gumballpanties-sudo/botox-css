#pragma once
// Pure pieces of the 11-07 rewrite of auto align / pixel surf / long jump / mini jump / jump bug.
// No engine types. Every number is css-community mp/src/game/shared (cs_gamemovement.cpp +
// gamemovement.cpp). Gate test: tests/cssmoves_test.cpp.
#include <cmath>

namespace cssm
{
    // VEC_HULL_MAX.z 62 / VEC_DUCK_HULL_MAX.z 45: a duck or unduck in the air moves the feet by half
    // the difference (FinishDuck :826/843 +, FinishUnDuck :800 -), the head stays put
    constexpr float k_hullStand    = 62.f;
    constexpr float k_hullDuck     = 45.f;
    constexpr float k_airDuckShift = 0.5f * (k_hullStand - k_hullDuck);   // 8.5

    // CSS x64 ClipVelocity adds normal * DIST_EPSILON to every clipped velocity (user 11-07, matches
    // the log: a pinned surf tick ends at -halfgrav + 1/32, not -halfgrav)
    constexpr float k_clipNudge = 0.03125f;

    // vz after a pinned tick: velocity clipped to 0 by a floor/lip (+ nudge) or a ceiling (- nudge),
    // or zeroed outright (stuck, no clip, no nudge), then FinishGravity's half step. Any counts.
    inline bool PinnedVz(float vz, float halfGrav, float eps)
    {
        const float pin = -halfGrav;
        return std::fabs(vz - pin) <= eps || std::fabs(vz - (pin + k_clipNudge)) <= eps
            || std::fabs(vz - (pin - k_clipNudge)) <= eps;
    }

    constexpr float k_degToRad = 3.14159265358979323846f / 180.f;

    // cmd move (view relative) -> world 2D wish. AirMove (:1762): wishvel = fwd * forward + side * right,
    // right = (sin yaw, -cos yaw) for a flat view
    inline void CmdToWorld(float yawDeg, float fwd, float side, float& wx, float& wy)
    {
        const float c = std::cos(yawDeg * k_degToRad), s = std::sin(yawDeg * k_degToRad);
        wx = fwd * c + side * s;
        wy = fwd * s - side * c;
    }

    inline void WorldToCmd(float yawDeg, float wx, float wy, float& fwd, float& side)
    {
        const float c = std::cos(yawDeg * k_degToRad), s = std::sin(yawDeg * k_degToRad);
        fwd  = wx * c + wy * s;
        side = wx * s - wy * c;
    }

    // AirAccelerate (:1707) cap: addspeed = 30 - vel.wishdir, nothing added at <= 0. The cap is on the
    // WHOLE wish, not per half: moving along a face, a wish with any along part spends it on the
    // along speed you already have.
    constexpr float k_airCap  = 30.f;
    constexpr float k_minGain = 5.f;    // u/s into the face one tick must add: >> the 1/32 clip nudge

    // into-face speed one air tick adds with unit wish w at velocity v (n = unit outward normal).
    // ponytail: assumes accelspeed (10 * maxspeed * dt = 37.5 at 250) >= addspeed; a sub-200 maxspeed
    // (scoped awp) adds a bit less, still >> k_minGain
    inline float IntoGain(float vx, float vy, float wx, float wy, float nx, float ny)
    {
        const float add = k_airCap - (vx * wx + vy * wy);
        const float in  = -(wx * nx + wy * ny);
        return add > 0.f && in > 0.f ? add * in : 0.f;
    }

    // Unit wish that presses into a face (n = its outward normal), keeping the player's own
    // along-face intent when that still pushes in. A pixel surf only lives while every tick's
    // AirAccelerate adds speed into the face (TryPlayerMove's clip takes it back off the same tick,
    // and the clip nudge walks the hull 1/32 u/s off it). A 45 degree mix at > ~32 u/s along the face
    // has vel.wish > 30 - k_minGain = no real press = the ride falls off; then straight in
    // (vel.(-n) ~ 0, full 30 u/s in, along speed untouched). No along-face intent = straight in.
    inline void PressIntoDir(float wx, float wy, float nx, float ny, float vx, float vy, float& dx, float& dy)
    {
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl < 1e-6f) { dx = dy = 0.f; return; }
        nx /= nl; ny /= nl;
        dx = -nx; dy = -ny;
        const float d = wx * nx + wy * ny;
        float tx = wx - nx * d, ty = wy - ny * d;
        const float tl = std::sqrt(tx * tx + ty * ty);
        if (tl < 1.f) return;
        tx /= tl; ty /= tl;
        const float mx = (tx - nx) * 0.70710678f, my = (ty - ny) * 0.70710678f;
        if (IntoGain(vx, vy, mx, my, nx, ny) >= k_minGain) { dx = mx; dy = my; }
    }

    // Pixel surf ride estimate for a planned catch, used to rank planner hits. A held PS on CSS
    // ends at a FIXED z (log 11-08/11-09: every ride dies at floor(lip) + 1/32): the clip nudge
    // walks the pinned hull up 1/32 u/s, so ride ticks = (lipTop - catch z) / (dt/32) and the press
    // can not extend it. Ride distance ~ depth * along-face speed. Floors: a catch rides its own
    // tick at least (depth 1 tick), a stalled hull (xy < 0.25 = air stuck) still ranks, last.
    inline float PsRideScore(float lipTop, float arriveZ, float xy)
    {
        const float depth = lipTop - arriveZ;
        return (depth > 0.0005f ? depth : 0.0005f) * (xy > 0.25f ? xy : 0.25f);
    }

    // Long jump duck hold, asked once per cmd after the takeoff tick. `held` = duck ticks already
    // sent, the takeoff tick counts as 1. The second tick is never dropped: released there,
    // m_bDucking (set on the takeoff tick, :943) is still pending and the unduck branch runs
    // FinishUnDuck as soon as the 8.5 drop is clear (:985-994) -- the feet go DOWN 8.5 without ever
    // having gone up (that is the mini jump, not a long jump). Landed = always stop.
    inline bool LjHoldDuck(int held, bool landed, bool adaptive, bool cancel)
    {
        if (landed) return false;
        if (held < 2) return true;
        return adaptive && !cancel;
    }
}
