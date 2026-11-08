#pragma once
// DETECTIONS, the pure half (botox movement.cpp run_detections). Engine free:
// tests/detections_test.cpp covers it. Fed the cmd START state (what the engine ran on the last
// sent cmd), never a feature's sim or plan -- a detection is a trick PERFORMED.
#include <cmath>
#include <cstdlib>

struct DetStart { bool air = false, landedWalk = false; float vz = 0.f, speed = 0.f; int tick = 0; };

// physical edgebug: lip reflect zeroes vz, FinishGravity leaves -g, xy kept. Candidate = a falling
// airborne start that suddenly reads ~-g; confirmed next start if landed or falling again.
struct DetEb
{
    bool  prevAir = false;
    float prevVz = 0.f, prevSpeed = 0.f;
    int   prevTick = 0, candTick = 0, lastBugTick = 0;
    float candVz = 0.f;
};

// g = sv_gravity * 0.5 * interval. tb/ps = those tricks own this tick (veto, their pins look the
// same). Returns the tick the bug happened in, 0 = none.
inline int DetEbStep(DetEb& s, const DetStart& st, float g, bool tb, bool ps)
{
    const bool prevOk = s.prevAir && s.prevTick == st.tick - 1;
    int bug = 0;

    if (s.candTick && st.tick == s.candTick + 1 && !tb
        && (st.landedWalk || (st.air && st.vz < s.candVz - g * 1.5f)))
        bug = s.candTick - 1;
    s.candTick = 0;

    if (st.air && prevOk && s.prevVz < -15.f && st.vz < 1.f - g
        && st.vz > -g - std::fmax(1.f, s.prevVz * -0.25f)
        && st.speed > 1.f && st.speed >= s.prevSpeed * 0.5f && !tb && !ps)
    {
        s.candTick = st.tick;
        s.candVz   = st.vz;
    }

    if (bug && s.lastBugTick && std::abs(bug - s.lastBugTick) <= 2) bug = 0;   // dup
    if (bug) s.lastBugTick = bug;

    s.prevAir = st.air; s.prevVz = st.vz; s.prevSpeed = st.speed; s.prevTick = st.tick;
    return bug;
}

// botox `fresh`: a held trick announces once, again only after 8 ticks off
inline bool DetFresh(bool now, int& off)
{
    const bool f = now && off >= 8;
    off = now ? 0 : (off < 1 << 30 ? off + 1 : off);
    return f;
}
