#pragma once
// Jumpstats core, botox csgo movement.cpp:4966 jump_stats 1:1, game-free (tests/jumpstats_test.cpp).
// One step per cmd: START = cmd start (pre-pred), END = cmd end (predicted, after every mover).
// Takeoff = ground -> air, or a jump bug (air, falling at START, rising at END). Landing back on
// the takeoff height (2u, jb 16u) with no trick in between = one stat line.
#include <algorithm>
#include <cmath>

struct JsIn
{
    int   tick = 0;
    bool  walk = true;                         // END movetype == walk
    bool  startGround = false, endGround = false;
    float startX = 0.f, startY = 0.f, startZ = 0.f, startVz = 0.f, startSpeed = 0.f;   // speed = 2d
    float endX = 0.f, endY = 0.f, endZ = 0.f, endVz = 0.f, endSpeed = 0.f;
    bool  duck = false;                        // cmd IN_DUCK
    int   mouseDx = 0;
    bool  trick = false;                       // eb queued / tb / as / wc / ps this cmd
    bool  showFails = false;
};

struct JsResult
{
    const char* name = "";
    float distance = 0.f, pre = 0.f, max = 0.f, height = 0.f;
    int   strafes = 0, sync = 0, tier = 0;     // tier 0..5 -> JsTierColor
};

struct JsState
{
    bool  active = false, jb = false, voided = false, ducked = false, duckOpen = false;
    int   chain = 0, groundTicks = 0, duckTicks = 0, lastTick = 0;
    int   strafes = 0, lastDir = 0, syncTicks = 0, gainTicks = 0;
    float pre = 0.f, max = 0.f, peak = 0.f;
    float sx = 0.f, sy = 0.f, sz = 0.f;
};

// botox k_colors: grey, blue, green, red, gold, purple (tier 0 = grey)
inline const char* JsTierColor(int tier)
{
    static constexpr const char* k[6] = { "BEBEBE", "4B6AF9", "41FE3F", "FC0301", "E0AD37", "D22CE4" };
    return k[tier < 0 ? 0 : tier > 5 ? 5 : tier];
}

// true = landed a stat jump, `out` filled
inline bool JsStep(JsState& s, const JsIn& in, JsResult& out)
{
    if (in.tick != s.lastTick + 1) s.active = false;
    s.lastTick = in.tick;

    if (!in.walk) { s.active = false; return false; }

    s.groundTicks = in.startGround ? s.groundTicks + 1 : 0;
    if (s.groundTicks > 2) s.chain = 0;

    const auto takeOff = [&](bool jb) {
        s.active = true; s.jb = jb; s.voided = false;
        s.sx = in.startX; s.sy = in.startY; s.sz = in.startZ;
        s.peak = in.startZ;
        s.pre = s.max = in.startSpeed;
        s.strafes = s.lastDir = s.syncTicks = s.gainTicks = 0;
        s.ducked = s.duckOpen = in.duck;
        s.duckTicks = 0;
    };

    if (in.startGround && !in.endGround)
    {
        s.chain = s.groundTicks == 1 ? s.chain + 1 : 0;
        takeOff(false);
    }
    else if (!in.startGround && !in.endGround && in.startVz <= 0.f && in.endVz > 0.f)
        takeOff(true);

    if (!s.active) return false;

    if (!in.endGround)
    {
        s.max  = std::max(s.max, in.endSpeed);
        s.peak = std::max(s.peak, in.endZ);
        if (in.endSpeed > 50.f)
        {
            ++s.syncTicks;
            if (in.endSpeed > in.startSpeed + 0.000001f) ++s.gainTicks;
        }
        const int dir = in.mouseDx > 0 ? 1 : in.mouseDx < 0 ? -1 : 0;
        if (dir && dir != s.lastDir) { ++s.strafes; s.lastDir = dir; }
        if (s.duckOpen && in.duck) ++s.duckTicks;
        else                       s.duckOpen = false;
        if (in.trick) s.voided = true;
        return false;
    }

    s.active = false;
    if (s.voided || std::fabs(in.endZ - s.sz) >= (s.jb ? 16.f : 2.f)) return false;

    const float dx = in.endX - s.sx, dy = in.endY - s.sy;
    const float distance = std::sqrt(dx * dx + dy * dy) + 32.f;
    const float minDistance = in.showFails ? 150.f : 220.f;
    if (distance <= minDistance || distance >= 280.f) return false;

    struct Type { const char* name; float tiers[5]; };
    static constexpr Type kJump  = { "J",   { 227.f, 232.f, 237.f, 241.f, 243.f } };
    static constexpr Type kLong  = { "LJ",  { 230.f, 235.f, 240.f, 244.f, 246.f } };
    static constexpr Type kMini  = { "MJ",  { 220.f, 225.f, 230.f, 234.f, 236.f } };
    static constexpr Type kBhop  = { "BH",  { 230.f, 234.f, 235.f, 238.f, 240.f } };
    static constexpr Type kMulti = { "MBH", { 235.f, 237.f, 242.f, 245.f, 247.f } };
    static constexpr Type kJb    = { "JB",  { 250.f, 255.f, 260.f, 265.f, 268.f } };
    const Type& type = s.jb             ? kJb
                     : s.chain > 1      ? kMulti
                     : s.chain == 1     ? kBhop
                     : !s.ducked        ? kJump
                     : s.duckTicks <= 1 ? kMini
                                        : kLong;
    int tier = 0;
    while (tier < 5 && distance >= type.tiers[tier]) ++tier;

    out.name = type.name; out.distance = distance; out.strafes = s.strafes;
    out.pre = s.pre; out.max = s.max; out.height = s.peak - s.sz; out.tier = tier;
    out.sync = s.syncTicks ? static_cast<int>(s.gainTicks * 100.f / s.syncTicks) : 0;
    return true;
}
