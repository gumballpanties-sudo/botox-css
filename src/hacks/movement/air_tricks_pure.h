#pragma once
// Pure half of the 11-07 texture bug / air stuck / wall climb rewrite (texturebug.inl, airstuck.inl,
// wallclimb.inl). No engine types. Every rule cites css-community mp/src/game/shared gamemovement.cpp
// or cstrike/cs_gamemovement.cpp. Gate test: tests/airtricks_test.cpp.
//
// The one engine fact all three ride on: an airborne tick is StartGravity (vz -= h), AirAccelerate,
// TryPlayerMove, CategorizePosition, FinishGravity (vz -= h), h = sv_gravity * dt / 2. When
// TryPlayerMove hands back vz == 0 the tick ENDS on vz == -h exactly (the "pin"):
//   ride  = vz zeroed, xy kept: two planes whose crease is horizontal (:2773, wall + steep floor /
//           lip top), or the first-plane clip off a lip top (:2727). Texture bug.
//   stuck = whole vector zeroed: allsolid (:2629), stuck re-test (:2649), >2 planes (:2770), crease
//           against the primal velocity (:2787), every bump fraction 0 (:2795). Air stuck.
// Live CSS ClipVelocity also leaves DIST_EPSILON (0.03125 u/s) along the plane normal on every clip
// (user, in game 11-07; not in the leak's :3145 body). Two consequences: a pin that came off a CLIP
// ends at -h + 1/32 (floor / lip) or -h - 1/32 (ceiling), only a ZEROED velocity ends exactly on -h
// (cssm::PinnedVz takes all three); and a hull flush on a face drifts off it unless the tick's
// AirAccelerate puts more than that back into the face: every held tick must press in.
#include <algorithm>
#include <cmath>
#include "css_moves_pure.h"   // cssm::PinnedVz, cssm::k_clipNudge

namespace atk
{
    constexpr float k_distEpsilon = cssm::k_clipNudge;   // DIST_EPSILON, the clip's push along the normal per clip
    constexpr float k_airCap      = 30.f;       // GetAirSpeedCap, AirAccelerate :1722
    constexpr float k_pinTol      = 0.01f;      // pin vz is exact per kind; free fall END vz never sits within this twice running
    constexpr float k_rideXy      = 1.f;        // ride vs stuck split (a stuck hull keeps only the 0.03125 push)
    constexpr float k_standNz     = 0.7f;       // CategorizePosition :3844, standable floor
    constexpr float k_nonJumpVz   = 140.f;      // NON_JUMP_VELOCITY :3810, no ground above this vz

    enum Pin { pin_none = 0, pin_ride = 1, pin_stuck = 2 };

    // END state of one simulated tick -> what the pin kind is
    inline Pin Classify(bool ground, bool walk, float vz, float vxy, float pinVz)
    {
        if (ground || !walk || !std::isfinite(vz) || !std::isfinite(vxy)) return pin_none;
        if (!cssm::PinnedVz(vz, -pinVz, k_pinTol)) return pin_none;
        return vxy >= k_rideXy ? pin_ride : pin_stuck;
    }

    // smallest wishspeed whose AirAccelerate (:1736 accel * wish * dt * friction) still beats the
    // DIST_EPSILON push off the face. Presses under this only drift away slower
    inline float MinPressWish(float airaccel, float dt, float friction)
    {
        const float a = airaccel * dt * friction;
        return a > 0.f ? k_distEpsilon / a : 450.f;
    }

    // press magnitudes ("rungs") into a face, strongest first. 450 = the whole cap every tick (30 u/s
    // into the face), then finer presses that close the last fraction of a unit slower, which is what
    // moves the crossing tick's position against a lip / crease. Floor = 2x the DIST_EPSILON break-even
    inline int Rungs(float airaccel, float dt, float friction, float* out, int cap)
    {
        static constexpr float k_mags[] = { 450.f, 30.f, 8.f, 2.f, 0.5f };
        const float floorWish = 2.f * MinPressWish(airaccel, dt, friction);
        int n = 0;
        for (float m : k_mags)
            if (n < cap && m >= floorWish) out[n++] = m;
        if (n < cap && (n == 0 || out[n - 1] > floorWish * 1.01f)) out[n++] = floorWish;
        return n;
    }

    // unit world wish: `inDeg` degrees off straight-into-face (n = face's outward normal, 2D),
    // turned toward along-face sign `along` (+1 / -1). 0 deg = straight in, 90 = pure along
    inline void PressDir(float nx, float ny, float inDeg, float along, float& dx, float& dy)
    {
        const float l = std::sqrt(nx * nx + ny * ny);
        if (l < 1e-6f) { dx = dy = 0.f; return; }
        nx /= l; ny /= l;
        const float tx = -ny * along, ty = nx * along;   // along-face tangent
        const float r = inDeg * 0.017453292519943295f;
        dx = -nx * std::cos(r) + tx * std::sin(r);
        dy = -ny * std::cos(r) + ty * std::sin(r);
    }

    // which along-face way to go: your keys when they pick one, else keep the slide you already have
    inline float AlongSign(float wishAlong, float velAlong)
    {
        const float d = std::fabs(wishAlong) > 1.f ? wishAlong : velAlong;
        return d < 0.f ? -1.f : 1.f;
    }

    // wall climb: a variant that grounds is a climb only above the ground you left (+2 = not the same
    // floor through a seam), and with prevent slow on it may not halve a real xy speed
    inline bool ClimbOk(float landZ, float groundZ, float xyBefore, float xyAfter, bool preventSlow)
    {
        if (!(landZ > groundZ + 2.f)) return false;
        if (preventSlow && xyBefore > 200.f && xyAfter < 0.5f * xyBefore) return false;
        return true;
    }

    // wall climb hop: an air win lands on cmd `landCmd` (its END grounded); the hop is the next cmd,
    // whose START is that ground. Valid only on that START and one tick of slack (server jitter)
    struct Hop
    {
        int startCmd = -1;   // command_number whose START must be the planned ground
        void Arm(int landCmd) { startCmd = landCmd + 1; }
        void Reset() { startCmd = -1; }
        // grounded START on cmd `cmd`: fire?
        bool Fire(int cmd, bool groundStart)
        {
            if (startCmd < 0) return false;
            if (cmd > startCmd + 1) { startCmd = -1; return false; }
            if (!groundStart || cmd < startCmd) return false;
            startCmd = -1;
            return true;
        }
        bool Pending(int cmd) const { return startCmd >= 0 && cmd <= startCmd + 1; }
    };

    // CSS jump bug window (cs_gamemovement.cpp CanUnduck :750 + CategorizePosition 2u :3798): a ducked
    // falling hull fires only when its feet sit 8.5..10.5 over the floor at a tick START. True when the
    // tick-boundary feet heights of a fall from `h` (feet over floor, ducked) at vz (END convention)
    // put one of the next `ticks` boundaries inside it. Used for the JB log only (engine sims decide)
    inline bool JbWindowAhead(float h, float vz, float grav, float dt, int ticks)
    {
        for (int k = 0; k <= ticks; ++k)
        {
            if (h >= 8.5f && h <= 10.5f) return true;
            vz -= grav * dt * 0.5f;
            h  += vz * dt;
            vz -= grav * dt * 0.5f;
            if (h < 0.f) return false;
        }
        return false;
    }
}
