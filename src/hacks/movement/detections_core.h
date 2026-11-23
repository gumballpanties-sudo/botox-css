#pragma once
// DETECTIONS, the pure half (botox movement.cpp run_detections). Engine free:
// tests/detections_test.cpp covers it. Fed the cmd START state (what the engine ran on the last
// sent cmd), never a feature's sim or plan -- a detection is a trick PERFORMED.
#include <cmath>
#include <cstdlib>
#include <cstring>

struct DetStart { bool air = false, landedWalk = false; float vz = 0.f, speed = 0.f; int tick = 0; };

// physical edgebug: lip reflect zeroes vz, FinishGravity leaves -g, xy kept. Candidate = a falling
// airborne start that suddenly reads ~-g; confirmed next start if landed or falling again.
struct DetEb
{
    bool  prevAir = false;
    float prevVz = 0.f, prevSpeed = 0.f;
    int   prevTick = 0, candTick = 0, lastBugTick = 0, tbTick = 0;
    float candVz = 0.f;
};

// TB pin shows in the START one cmd after its hit, and rides go pin/park/re-pin (park = owns, no
// hit), so a tb flag on this cmd alone misses most rides -> veto while TB acted within this window
constexpr int k_detTbWindow = 3;

// g = sv_gravity * 0.5 * interval. tb = TB hit or owned this cmd, ps = PS owns it (veto, their pins
// look the same). Returns the tick the bug happened in, 0 = none.
inline int DetEbStep(DetEb& s, const DetStart& st, float g, bool tbNow, bool ps)
{
    const bool prevOk = s.prevAir && s.prevTick == st.tick - 1;
    if (tbNow) s.tbTick = st.tick;
    const bool tb = s.tbTick && st.tick - s.tbTick >= 0 && st.tick - s.tbTick <= k_detTbWindow;
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

// botox run_detections edge surf: phys = this START pinned and the last cmd sent a proven edge surf pin, held = edge surf owned
// the last cmd ( its approach ). One print an approach: owned ticks keep the gap counter at 0, so the air duck lift's second pin
// is the same surf. 1 = fired ( 125 + "edgesurfed" ), 2 = a ride tick ( 25 ), 0 = nothing
struct DetEs { int off = 1 << 30; bool fired = false; };
inline int DetEsStep(DetEs& s, bool phys, bool held, int gapTicks = 8)
{
    if (phys || held) s.off = 0;
    else if (s.off < (1 << 30) && ++s.off >= gapTicks) s.fired = false;
    if (phys && !s.fired) { s.fired = true; return 1; }
    return phys ? 2 : 0;
}

// botox push_notice pick_prefix, random mode: row index to print, -1 = none. Never the same text
// as `last` while another non-empty row exists (empty rows skipped). r = any random number.
// count <= rows in the array ( NameList: kMaxNames ).
inline int DetPickPrefix(const char (*rows)[64], int count, const char* last, unsigned r)
{
    int pool[64], n = 0, any = -1;
    count = count < 0 ? 0 : (count > 64 ? 64 : count);
    for (int i = 0; i < count; ++i)
    {
        if (!rows[i][0]) continue;
        any = i;
        if (std::strcmp(rows[i], last) != 0) pool[n++] = i;
    }
    return n ? pool[r % (unsigned)n] : any;
}
