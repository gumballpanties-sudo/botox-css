#pragma once
// Pure half of the CS:S wall engine (wallengine.inl). No engine types.
// Gate test: tests/wallengine_test.cpp.
//
// h = sv_gravity * frametime * 0.5 (halfgrav, 6.0 at 800 / 66.67 tick). The engine never tests a pin
// with an exact float: its windows are wide enough to swallow the CSS clip nudge (+-1/32) and the
// client/server float drift, which is why its holds survive online.
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace we
{
    // wrap to [-180, 180]
    inline float NormYaw(float a)
    {
        if (!std::isfinite(a)) return 0.f;
        if (a < -180.f || a > 180.f)
        {
            const float n = std::round(std::fabs(a * (1.f / 360.f)));
            a = a >= 0.f ? a - n * 360.f : a + n * 360.f;
        }
        return a;
    }

    // (wallstuck): the step between two consecutive air ticks is a pin. Both ticks off
    // the ground, the tick did NOT fall a free step (prev - h < now), and now sits on -h within
    // clamp(0.08 h, 0.125, 0.75) -- 0.48 at 66 tick
    inline float PinTol(float h) { return std::clamp(h * 0.08f, 0.125f, 0.75f); }
    inline bool PinStep(bool prevGround, float prevVz, bool ground, float vz, float h)
    {
        if (prevGround || ground) return false;
        return prevVz - h < vz && std::fabs(h + vz) <= PinTol(h);
    }

    // (texture bug sim): END vz + h inside (-1/32, 0.3) = pinned. Lower edge is the
    // ceiling-clip nudge, upper edge swallows the floor-clip nudge and server drift
    inline bool TbPinVz(float vz, float h)
    {
        const float d = vz + h;
        return d > -0.03125f && d < 0.3f;
    }

    // result + the comparator every TB loop shares /
    //   pinned beats unpinned; both pinned: speed beats speed by > 0.5, else the SMALLER press wins;
    //   neither pinned: the smaller gap to the face wins
    struct TbRes
    {
        bool  found  = false;   // the sim ran
        float fmove  = 0.f;     // wish size along `yaw`
        float yaw    = 0.f;     // world yaw of the wish
        float gap    = FLT_MAX; // hull gap to the face at END
        float speed  = 0.f;     // END xy speed
        int   pins   = 0;       // pinned ticks (1 + extension ticks)
        bool  duck   = false;   // the crouched variant won (send IN_DUCK)
        bool  duckMore = false; // ...because it pinned MORE ticks (the planned-crouch case)
        bool  rise   = false;   // START vz > 0 and END vz == -h within 0.01 (flag3 of
    };
    inline bool TbBetter(const TbRes& c, const TbRes& b)
    {
        if (!c.found) return false;
        if (!b.found) return true;
        const bool cp = c.pins > 0, bp = b.pins > 0;
        if (cp != bp) return cp;
        if (cp)
        {
            if (std::fabs(c.speed - b.speed) > 0.5f) return c.speed > b.speed;
            return c.fmove < b.fmove;
        }
        return c.gap < b.gap;
    }
    // does the crouched retry replace the standing result. more = it pins more ticks
    // without ending further off the face; only = standing never pins, crouched does off a rising START
    inline bool TbDuckMore(int standPins, float standGap, int duckPins, float duckGap)
    {
        return standPins > 0 && duckPins > 0 && duckPins > standPins && duckGap <= standGap + 0.05f;
    }
    inline bool TbDuckOnly(int standPins, int duckPins, bool duckRise) { return standPins < 1 && duckPins > 0 && duckRise; }

    // tables, ref .rdata
    constexpr float k_tbRefineOff[]   = { 0.f, -0.5f, 0.5f, -1.25f, 1.25f, -2.25f, 2.25f };
    constexpr float k_tbRefineScale[] = { 1.f, 0.6f, 1.6f };
    constexpr float k_tbYaw[]         = { 6.f, 8.f, 10.f, 13.f, 17.f, 22.f, 30.f, 40.f,
                                          55.f, 70.f, 80.f, 83.f, 85.f, 87.f, 89.f, 89.999f };
    constexpr float k_tbMove[]        = { 0.05f, 0.25f, 1.f, 15.f };
    constexpr float k_tbFine[]        = { 0.001f, 0.01f, 0.05f, 0.25f, 0.75f, 1.f, 5.f, 15.f };
    constexpr float k_tbMinMove = 0.001f, k_tbMaxMove = 30.f, k_tbMinYaw = 1.f, k_tbMaxYaw = 89.999f;

    // the cmd would bleed a moving hull (no keys, >= 40 u/s) down to a crawl
    inline bool TbStalls(bool hasInput, float speedNow, float speedAfter)
    {
        if (hasInput || speedNow < 40.f) return false;
        return speedAfter >= 1.f && speedAfter <= 8.f && speedAfter <= speedNow * 0.2f;
    }

    // (wallstuck): one sim's outcome and its comparator
    struct WsRes
    {
        bool  found = false;     // a pin step happened inside the horizon
        bool  held  = false;     // ...and the very next step pinned too (sustained)
        int   tick  = 0;         // first pin step
        float speed = 0.f;       // xy speed on the sustained step
        float z = 0.f;           // origin z at the END of the first pin step (the catch height)
    };
    // stuck = we are pinned right now: the speed kept matters, not how soon
    inline bool WsBetter(const WsRes& c, const WsRes& b, bool stuck)
    {
        if (!c.found) return false;
        if (!b.found) return true;
        if (c.held != b.held) return c.held;
        if (!stuck && c.tick != b.tick) return c.tick < b.tick;
        return c.speed > b.speed + 0.5f;
    }
    // CSS addition (not ref), pixel surf CATCH only. A held surf does not hold forever on this build:
    // every pinned tick the clip nudge (+1/32 u/s up) lifts the hull ~0.0005u, and the ride ends when
    // the feet rise out of the catch band (log 11-08: every ride ended at one z, -383.9685; latch 0.002
    // below it = 4 ticks, 0.02 below = 33). So between catches in the SAME band (z within k_wsBand),
    // the deeper one rides longer and wins; across bands (different lips) the old soonest rule stands
    constexpr float k_wsBand = 0.25f;
    // a RIDE keeps its speed; a pin with the velocity zeroed (xy ~1/32, the clip nudge) is an air stuck.
    // Pixel surf and auto align's hold count a pin step only above this (air stuck wants the opposite)
    constexpr float k_rideMinXY = 0.25f;
    inline bool WsBetterDeep(const WsRes& c, const WsRes& b)
    {
        if (!c.found) return false;
        if (!b.found) return true;
        if (c.held != b.held) return c.held;
        const float dz = c.z - b.z;
        if (std::fabs(dz) < k_wsBand && std::fabs(dz) > 0.0005f) return dz < 0.f;
        if (c.tick != b.tick) return c.tick < b.tick;
        return c.speed > b.speed + 0.5f;
    }
    // search stops early: a sustained pin by your own cmd, or (already pinned) one that keeps your speed
    inline bool WsDone(const WsRes& r, bool ownCmd, bool stuck, float speedNow)
    {
        return r.held && (ownCmd || (stuck && r.speed >= speedNow - 1.f));
    }
    constexpr float k_wsYawStuck[] = { 89.9f, 75.f, 60.f, 45.f, 30.f, 15.f, 0.f, -15.f, -45.f };
    constexpr float k_wsMoveStuck[] = { 450.f, 30.f, 5.f, 1.f };
    constexpr float k_wsYawCatch[] = { 0.f, 45.f, 80.f, -45.f };
    constexpr float k_wsMoveCatch[] = { 450.f, 10.f };
    // one wall stuck candidate: keep = your move, else a wish (world yaw, size); duck 0 yours / 1 on / 2 off
    struct WsCand { bool keep = true; float yaw = 0.f, move = 0.f; int duck = 0; };
    // per feature (air stuck / pixel surf / wall climb each own one)
    struct WsState
    {
        bool   have = false; bool prevGround = true; float prevVz = 0.f;
        int    count = 0;            // consecutive pin steps ending at this START
        bool   lastCustom = false;   // last sent was a wish we wrote (DAT_1800eb3dc)
        WsCand last{};               // ...and which one (DAT_1800befd0)
        int    sims = 0, holds = 0, catches = 0, misses = 0;
    };
    struct WsOut { bool wrote = false; bool held = false; int count = 0; int tick = -1; bool duck = false; float z = 0.f; };

    // wall-find reach for the catch horizon
    inline float WsReach(int ticks, float speed, float dt) { return std::min(192.f, (float)ticks * speed * dt + 16.f); }

    // (pixel hop): the 384-step rolling search, 12 yaw offsets x 8 sizes x side x duck
    constexpr float k_phYaw[]  = { 0.5f, 3.f, 8.f, 15.f, 22.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 89.9f };
    constexpr float k_phMove[] = { 0.001f, 0.05f, 0.25f, 1.f, 5.f, 15.f, 60.f, 450.f };
    constexpr int   k_phSteps  = 384;
    struct PhStep { float yawOff; float move; bool flipSide; bool flipDuck; };
    // ref order is side / duck OUTERMOST (i/96, i/192). Our pins last 1 tick and the clock stops at
    // ~170 steps = duck half never flown (log 11-08). So side + duck are the lowest bits here: any prefix
    // of 4k steps flies k (yaw, size) pairs in all 4 quadrants. Same 384 set, once each
    inline PhStep PhAt(int i)
    {
        i = ((i % k_phSteps) + k_phSteps) % k_phSteps;
        const int r = i >> 2;   // 0..95: yaw inner, size outer (ref)
        PhStep s;
        s.flipSide = (i & 1) != 0;
        s.flipDuck = (i & 2) != 0;
        s.yawOff   = k_phYaw[r % 12];
        s.move     = k_phMove[(r / 12) & 7];
        return s;
    }
    // the rolling search only runs while hung on a pin: |vz + h| <= 0.05
    inline bool PhPinned(float vz, float h) { return std::fabs(vz + h) <= 0.05f; }

    // ---- ref pixel hop / planner 1800098c0 / sim 180008f40) -- the PH key's engine.
    // ref's PH above stays for wall climb's landings only
    // the cmd + IN_JUMP sim took the jump = END vz beats max(START vz, 0) by 100
    inline bool PhJumpTook(float vz0, float vz1) { return vz1 > std::max(vz0, 0.f) + 100.f; }
    // one plan (DAT_180099200 keeps the last). keep = your own move; duck 0 yours / 1 on / 2 off
    struct PhPlan { bool found = false, keep = true; float yaw = 0.f, move = 0.f; int duck = 0; float speed = 0.f; int ticks = 1; };
    // 1-tick passes: faster landing wins (by > 0.01). 3-tick fallback (byTicks): sooner first, then faster
    inline bool PhBetter(const PhPlan& c, const PhPlan& b, bool byTicks)
    {
        if (!c.found) return false;
        if (!b.found) return true;
        if (byTicks && c.ticks != b.ticks) return c.ticks < b.ticks;
        return b.speed + 0.01f < c.speed;
    }
    // the search stops once a landing keeps your speed within 0.5
    inline bool PhDone(const PhPlan& b, float speed0) { return b.found && speed0 - 0.5f <= b.speed; }
    // not ref: a steered land on the floor your OWN move lands on anyway (log 11-08: face3 move 0.05 for
    // 3 ticks, z 17 -> 0) is a plain bhop that ate your strafe. A pixel hop is a surface you'd miss.
    // 11-09: anything BELOW your own floor too (steering off the ledge you'd land on is never a hop)
    inline bool PhSameFloor(bool ownFound, float ownZ, float landZ) { return ownFound && landZ < ownZ + 1.f; }
    // sim prune: falling with feet already under own floor + 1 = every later landing fails PhSameFloor.
    // Valid after tick 1: CSS air duck/unduck moves the origin on its FIRST tick only (FinishDuck :820), no
    // air step-up, so z only drops while vz <= 0
    inline bool PhCantRise(bool ownFound, float ownZ, float z, float vz) { return ownFound && vz <= 0.f && z < ownZ + 1.f; }
    // CPU skip (not ref, same answer): CategorizePosition reads vz after the half-gravity step and never grounds
    // above NON_JUMP_VELOCITY 140 (gamemovement.cpp:3810). Last check of an n-tick horizon = vz0 - (2n-1)h
    inline bool PhCanLand(float vz0, float h, int horizon = 3) { return vz0 - (float)(2 * horizon - 1) * h <= 140.f; }

    // reach (not ref, 11-09): no land within 3 ticks = fly a held wish for up to N ticks at yaw offsets off your
    // velocity, least turn first, and steer onto the first one that grounds above your own floor. Grid order:
    // offset ascending, + side, - side, your stance then flipped. Rolled across cmds like the 384 roll
    constexpr float k_phReachOff[] = { 0.f, 8.f, 16.f, 25.f, 35.f, 45.f, 60.f, 75.f, 90.f, 110.f, 135.f };
    constexpr int   k_phReachN     = (int)(sizeof(k_phReachOff) / sizeof(k_phReachOff[0]));
    constexpr int   k_phReachSteps = k_phReachN * 4;   // x side x stance (0 has one side: its - twin = itself)
    struct PhReachStep { float off; bool flipDuck; bool skip; };
    // skip = past the max turn, or offset 0's duplicate - side
    inline PhReachStep PhReachAt(int i, float maxOff)
    {
        i = ((i % k_phReachSteps) + k_phReachSteps) % k_phReachSteps;
        const int   r   = i >> 2;
        const float off = k_phReachOff[r];
        PhReachStep s;
        s.off      = (i & 1) ? -off : off;
        s.flipDuck = (i & 2) != 0;
        s.skip     = off > maxOff + 1e-3f || (off == 0.f && (i & 1));
        return s;
    }
    // world wish yaw = face yaw turned AWAY from your along-face velocity (ref sign; ref turns with it)
    inline float PhwSign(float velYaw, float wallYaw) { return NormYaw(velYaw - wallYaw) < 0.f ? 1.f : -1.f; }
    constexpr float k_phwVelOff[]  = { 0.f, -20.f, 20.f, -45.f, 45.f, -70.f, 70.f, -90.f, 90.f };              // (no face)
    constexpr float k_phwVelMove[] = { 450.f, 30.f, 5.f };
    constexpr float k_phwOff[]     = { 1.f, 6.f, 13.f, 22.f, 30.f, 45.f, 60.f, 75.f, 85.f, 89.999f };          // (face)
    constexpr float k_phwMove[]    = { 0.001f, 0.05f, 0.25f, 1.f, 5.f, 15.f, 30.f, 450.f };
    constexpr float k_phwOff3[]    = { 6.f, 22.f, 45.f, 75.f, 89.999f };                                       // (3-tick)
    constexpr float k_phwMove3[]   = { 0.05f, 1.f, 15.f, 450.f };
    constexpr int   k_phwDuck[]    = { 2, 1 };                                                                 // unduck first

    // world wish (yaw, size) -> cmd forward/side for view yaw /
    inline void WishToCmd(float wishYaw, float size, float viewYaw, float& fwd, float& side)
    {
        const float d = NormYaw(wishYaw - viewYaw) * 0.017453292f;
        fwd  = std::cos(d) * size;
        side = -std::sin(d) * size;
    }

    // auto align "hold pixel surfs" latch (not ref -- CSS addition on top of the wall stuck hold).
    // One pin step at START latches. Held while START keeps pinning; let go on ground, your duck-key bail,
    // k_holdMiss unpinned STARTs in a row (the surf ended) or k_holdAway ticks of keys steering off the face
    // (botox texture_bug's own away-check: two ticks running = you meant it)
    constexpr int k_holdMiss = 2, k_holdAway = 2;
    struct HoldLatch
    {
        enum Why { w_none, w_ground, w_bail, w_lost, w_away };
        bool on = false; int miss = 0, away = 0;
        bool block = false;   // steered off: no relatch until a START that does not pin (you left it)
        void Off() { on = false; miss = away = 0; }
        // one START. pin = a surf pin (caller: pin step AND still sliding, xy >= 1 -- a zeroed velocity
        // is an air stuck). Returns why a live latch let go this tick (w_none = still on, or never was)
        Why Step(bool ground, bool bail, bool pin, bool steerAway)
        {
            const bool was = on;
            if (ground || bail) { Off(); block = false; return was ? (ground ? w_ground : w_bail) : w_none; }
            if (!pin) block = false;
            if (pin && !block) { on = true; miss = 0; }
            else if (on && !pin && ++miss >= k_holdMiss) { Off(); return w_lost; }
            if (!on) return w_none;
            away = steerAway ? away + 1 : 0;
            if (away >= k_holdAway) { Off(); block = true; return w_away; }
            return w_none;
        }
    };
}
