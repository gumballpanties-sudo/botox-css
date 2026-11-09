// wallengine.inl -- textually included by movement.cpp after wallclimb.inl (same TU). The CS:S wall engine:
//   texture bug  wall find, sim, duck retry, fine pass, stall veto, apply -> TextureBug when its key is held
//   wall stuck   candidate search + sim + apply                          -> air stuck + pixel surf engine
//   pixel hop    air plan + apply                                        -> pixel hop + wall climb landings
// All sims start from this cmd's START (RestoreToFrame) on the real game movement (Pred_Simulate), the
// CPrediction::RunCommand between a save and RestoreEntityToPredictedFrame shape. Pure rules and
// tables: wallengine_pure.h (tests/wallengine_test.cpp). Logs: `WETB:`, `WEWS:`, `WEPH:`.
#include "wallengine_pure.h"

static float WeHalfGrav() { return CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f; }

// one engine tick of `c`; attack bits never ride a probe
static void WeSimTick(void* pLocal, UserCmd_t c)
{
    c.buttons &= ~(IN_ATTACK | IN_ATTACK2);
    c.hasbeenpredicted = false;
    Pred_Simulate(pLocal, &c);
}

static bool WeOnLadderOrNoclip(void* pLocal) { return EntMoveType(pLocal) != MOVETYPE_WALK; }

// max(|mins|, |maxs|) per axis: the hull's half extent the gap traces subtract
static Vec3 WeExtent(void* pLocal)
{
    const Vec3 mn = EntV3(pLocal, g_offMins), mx = EntV3(pLocal, g_offMaxs);
    return Vec3((std::max)(fabsf(mn.x), fabsf(mx.x)), (std::max)(fabsf(mn.y), fabsf(mx.y)), (std::max)(fabsf(mn.z), fabsf(mx.z)));
}

// line traces out of the hull centre, 2u past its edge, at 5 heights feet -> head, 16 directions
// starting at `startAng` (radians). First direction with a vertical face (|nz| <= 0.05) facing back at us wins.
struct WeWall { bool ok = false; Vec3 n{}; float ang = 0.f; float yaw = 0.f; };
static WeWall WeFindWallLines(void* pLocal, const Vec3& o, float startAng)
{
    WeWall w;
    const Vec3 mn = EntV3(pLocal, g_offMins), mx = EntV3(pLocal, g_offMaxs);
    const float ex = (std::max)(fabsf(mn.x), fabsf(mx.x)), ey = (std::max)(fabsf(mn.y), fabsf(mx.y));
    const float z0 = o.z + mn.z, z1 = o.z + mx.z;
    TraceFilterWorldOnly fil; fil.Setup();
    for (float a = startAng; a < startAng + 6.2831855f; a += 0.3926991f)
    {
        const float c = cosf(a), s = sinf(a);
        const float d = (std::min)(ex / (std::max)(fabsf(c), 1e-6f), ey / (std::max)(fabsf(s), 1e-6f));
        float bestFrac = 1.f;
        bool hit = false;
        for (int i = 0; i < 5; ++i)
        {
            const float z = (float)i * (z1 - z0) * 0.25f + z0;
            GameTrace_t tr{};
            TraceLine(Vec3(o.x, o.y, z), Vec3(o.x + (d + 2.f) * c, o.y + (d + 2.f) * s, z), MASK_PLAYERSOLID, &fil, &tr);
            if (tr.fraction < 1.f && fabsf(tr.plane.normal.z) <= 0.05f && tr.plane.normal.y * s + tr.plane.normal.x * c <= -0.1f
                && tr.fraction < bestFrac)
            {
                bestFrac = tr.fraction;
                w.n = tr.plane.normal;
                hit = true;
            }
        }
        if (hit)
        {
            w.ok  = true;
            w.ang = a;
            w.yaw = RAD2DEGF(atan2f(-w.n.y, -w.n.x));
            return w;
        }
    }
    return w;
}

// hull sweeps out to `reach`, 16 directions; nearest non-floor face (|nz| < 0.7)
static WeWall WeFindWallHull(void* pLocal, const Vec3& o, float reach)
{
    WeWall w;
    const Vec3 mn = EntV3(pLocal, g_offMins), mx = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    float best = 2.f;
    for (int k = 0; k < 16; ++k)
    {
        const float a = (float)k * 22.5f * 0.017453292f;
        GameTrace_t tr{};
        TraceHull(o, Vec3(cosf(a) * reach + o.x, sinf(a) * reach + o.y, o.z), mn, mx, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.fraction < 1.f && !tr.allsolid && tr.fraction < best && fabsf(tr.plane.normal.z) < 0.7f)
        {
            best  = tr.fraction;
            w.ok  = true;
            w.n   = tr.plane.normal;
            w.yaw = RAD2DEGF(atan2f(-w.n.y, -w.n.x));
        }
    }
    return w;
}

// ====================================================================================================
// WALL STUCK. A pin = an air tick whose vz did not fall a free step and sits on -h
// (we::PinStep, wide window). Each cmd: count the pin steps between consecutive STARTs. Pinned now = hold
// search (2-tick horizon, keep the speed); else, a face within reach = catch search (16 ticks, soonest).
// Candidates: your move (both stances), no wish (both stances), then wishes at yaw offsets off the face (or
// off your velocity) x sizes x stance. Each holds its cmd over the horizon; a pin step followed by another =
// sustained. Air stuck and pixel surf both run this engine (air stuck keeps the SLOWEST hold instead).
// ====================================================================================================
using WeWsCand = we::WsCand;
using WeWsState = we::WsState;
using WeWsOut = we::WsOut;

// ride = pixel surf: a pin step only counts while the hull still slides (we::k_rideMinXY), never a stuck
static we::WsRes WeWsSim(void* pLocal, int nBase, const UserCmd_t& base, const WeWsCand& k, int horizon, bool stuck,
                         float h, WeWsState& st, bool ride = false)
{
    we::WsRes r;
    RestoreToFrame(nBase - 1);
    UserCmd_t c = base;
    c.buttons &= ~IN_JUMP;
    if (!k.keep)
    {
        we::WishToCmd(k.yaw, k.move, c.viewangles.y, c.forwardmove, c.sidemove);
        c.upmove = 0.f;
    }
    if (k.duck == 1) c.buttons |= IN_DUCK;
    else if (k.duck == 2) c.buttons &= ~(IN_DUCK | IN_JUMP);
    bool  prevGround = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    float prevVz     = EntV3(pLocal, g_offVelocity).z;
    for (int i = 0; i < horizon; ++i)
    {
        if (prevGround || !AT_CanSim()) break;
        WeSimTick(pLocal, c);
        ++st.sims;
        if (WeOnLadderOrNoclip(pLocal)) break;
        const bool  g = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        const Vec3  v = EntV3(pLocal, g_offVelocity);
        if (g) break;
        if (we::PinStep(false, prevVz, false, v.z, h) && (!ride || v.Length2D() >= we::k_rideMinXY))
        {
            if (r.found) { r.held = true; r.speed = v.Length2D(); break; }
            r.found = true; r.tick = i; r.z = EntV3(pLocal, g_offOrigin).z;
        }
        else if (r.found || stuck) break;
        prevGround = g; prevVz = v.z;
    }
    RestoreToFrame(nBase - 1);
    return r;
}

static void WeWsApply(UserCmd_t* cmd, const WeWsCand& k)
{
    if (!k.keep)
    {
        we::WishToCmd(k.yaw, k.move, cmd->viewangles.y, cmd->forwardmove, cmd->sidemove);
        cmd->upmove = 0.f;
        cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    }
    if (k.duck == 1) cmd->buttons |= IN_DUCK;
    else if (k.duck == 2) cmd->buttons &= ~IN_DUCK;
}

// . true = a candidate pinned and was written. ps = pixel surf: pins must slide (ride), and
// on a catch the deepest one in a band wins (we::WsBetterDeep) -- no stop at the first held one (shallow)
static bool WeWsSearch(void* pLocal, UserCmd_t* cmd, bool stuck, const WeWall& wall, bool slow, int catchTicks, float h,
                       WeWsState& st, WeWsOut& out, bool ps = false)
{
    const bool deep = ps && !stuck && !slow;
    const int   nBase    = PredCommandsPredicted();
    const Vec3  v0       = s_prePredVel;
    const float speedNow = v0.Length2D();
    const int   flip     = (cmd->buttons & IN_DUCK) ? 2 : 1;
    const int   horizon  = stuck ? 2 : std::clamp(catchTicks, 1, 64);

    WeWsCand best{}; we::WsRes bestR{}; bool done = false;
    const auto better = [&](const we::WsRes& c, const we::WsRes& b) {
        if (slow && stuck && c.found && b.found && c.held == b.held)
            return c.speed + 0.5f < b.speed;   // air stuck: the hold that keeps the least speed
        if (deep) return we::WsBetterDeep(c, b);
        return we::WsBetter(c, b, stuck);
    };
    const auto isDone = [&](const we::WsRes& r, bool own) {
        if (slow) return r.held && (own || stuck);
        if (deep) return false;
        return we::WsDone(r, own, stuck, speedNow);
    };
    const auto tryCand = [&](const WeWsCand& k) {
        if (done) return;
        const we::WsRes r = WeWsSim(pLocal, nBase, *cmd, k, horizon, stuck, h, st, ps);
        if (better(r, bestR)) { bestR = r; best = k; }
        if (bestR.found && isDone(bestR, best.keep)) done = true;
    };

    tryCand(WeWsCand{ true, 0.f, 0.f, 0 });
    tryCand(WeWsCand{ true, 0.f, 0.f, flip });
    if (stuck && st.lastCustom) tryCand(st.last);   // a hold repeats last tick's written wish first
    tryCand(WeWsCand{ false, 0.f, 0.f, 0 });
    tryCand(WeWsCand{ false, 0.f, 0.f, flip });

    const float velYaw = RAD2DEGF(atan2f(v0.y, v0.x));
    const float baseYaw = wall.ok ? wall.yaw : velYaw;
    const float sign = we::NormYaw(velYaw - baseYaw) < 0.f ? -1.f : 1.f;
    const float* yaws  = stuck ? we::k_wsYawStuck : we::k_wsYawCatch;
    const int    nYaw  = stuck ? 9 : 4;
    const float* moves = stuck ? we::k_wsMoveStuck : we::k_wsMoveCatch;
    const int    nMove = stuck ? 4 : 2;
    for (int y = 0; y < nYaw && !done; ++y)
        for (int m = 0; m < nMove && !done; ++m)
            for (int d = 0; d < 2 && !done; ++d)
                tryCand(WeWsCand{ false, we::NormYaw(sign * yaws[y] + baseYaw), moves[m], d == 0 ? 0 : flip });

    if (!bestR.found) return false;
    WeWsApply(cmd, best);
    st.lastCustom = !best.keep;
    st.last = best;
    out.wrote = true;
    out.held  = bestR.held;
    out.tick  = bestR.tick;
    out.z     = bestR.z;
    out.duck  = (cmd->buttons & IN_DUCK) != 0;
    return true;
}

// . Call once per cmd while the feature's key is held (it keeps the START pin count).
static WeWsOut WeWallStuck(void* pLocal, UserCmd_t* cmd, WeWsState& st, bool slow, const char* tag)
{
    WeWsOut out;
    const float h      = WeHalfGrav();
    const bool  ground = (s_prePredFlags & FL_ONGROUND) != 0;
    const float vz     = s_prePredVel.z;
    // pixel surf: a START pinned with the velocity zeroed is an air stuck, not a ride -- counting it put the
    // hold search on it and held you stuck on the wall (log 11-08: PSU latch spd=0.0, holds=67/s for 10s+)
    const bool ps = tag[0] == 'p';
    const bool pin = we::PinStep(st.prevGround, st.prevVz, ground, vz, h)
                  && (!ps || s_prePredVel.Length2D() >= we::k_rideMinXY);
    const int count = (st.have && pin) ? st.count + 1 : 0;
    st.have = true; st.prevGround = ground; st.prevVz = vz; st.count = count;
    out.count = count;
    if (ground || WeOnLadderOrNoclip(pLocal)) { st.lastCustom = false; return out; }

    constexpr int k_catchTicks = 16;   // wallstuck_ticks default (ref
    const float speed = s_prePredVel.Length2D();
    const int   ticks = count > 0 ? 2 : k_catchTicks;
    RestoreToFrame(PredCommandsPredicted() - 1);
    const WeWall wall = WeFindWallHull(pLocal, s_prePredOrigin, we::WsReach(ticks, speed, TickInterval()));
    if (!wall.ok && count < 1) { st.lastCustom = false; return out; }

    if (count >= 1 && WeWsSearch(pLocal, cmd, true, wall, slow, k_catchTicks, h, st, out, ps)) ++st.holds;
    else
    {
        st.lastCustom = false;
        if (WeWsSearch(pLocal, cmd, false, wall, slow, k_catchTicks, h, st, out, ps)) ++st.catches;
        else ++st.misses;
    }
    RestoreToFrame(PredCommandsPredicted() - 1);

    static int s_sec[3] = { -1, -1, -1 };
    const int slot = tag[0] == 'a' ? 0 : tag[0] == 'p' ? 1 : 2;
    const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
    if (sec != s_sec[slot])
    {
        s_sec[slot] = sec;
        if (g_debugLogEnable && (st.holds || st.catches || st.misses))
            DebugLog("WEWS: %s holds=%d catches=%d misses=%d sims=%d", tag, st.holds, st.catches, st.misses, st.sims);
        st.holds = st.catches = st.misses = st.sims = 0;
    }
    return out;
}

// ====================================================================================================
// PIXEL HOP (key) -- ref (createmove 18000c0b0 runs it only when the TB key is NOT held,
// and skips wall stuck while it is). Every tick, from START:
//   1. jump test: cmd + IN_JUMP flown one tick; END vz > max(START vz, 0) + 100 = the jump took -> send it
//   2. else the planner (1800098c0, sim 180008f40): a cmd that GROUNDS you within the horizon, ranked by the
//      xy speed it lands with, stop once within 0.5 of yours. Candidates: your cmd, your cmd crouch-flipped,
// last tick's plan, then a face (WeFindWallLines): offsets turned away from your
//      along-face velocity x sizes x unduck/duck (1 tick), and if none lands, a coarse grid over 3 ticks
//      (sooner wins). No face: offsets off your velocity yaw x {450, 30, 5}.
// 3. no landing at all: texture bug (ref calls its TB here), skipped only while AA hold rides a surf.
// Pixel surf stands down while the key is held (s_pixelHopOn, ref skips wall stuck): its 8-tick catch
// ducks / presses you off the ledge PH would land on. Let go of the key = PS back on.
// Hop gate (CSS addition, user 11-08 "no auto jump"): step 1 runs while you hold jump, or on the first ground
// tick after a land PH steered itself (not your own move) -- the hop off a pixel surf needs no space.
// Land cmds make PS / WC / AS / auto bounce yield (s_pixelHopHit).
// ref's PH , WePixelHopAir below) is wall climb's landing search only now.
// ====================================================================================================
// per-second diag (WEPH line): air = air ticks planned, lands = plans sent (own = your own cmd landed),
// steer = landed on a PH wish, nowall = face find failed, tb = TB fallback wrote, hops = jumps sent
// reach = steered onto a land > 3 ticks out (reach pass), rcut = its clock ran out mid-grid
static struct { bool hopped = false; int roll = 0, reachRoll = 0; bool hit = false; bool armed = false; we::PhPlan last{};
                int sims = 0, lands = 0, hops = 0, air = 0, pin = 0, nowall = 0, steps = 0, pinLands = 0,
                    own = 0, steer = 0, tb = 0, cut = 0, floor = 0, reach = 0, rcut = 0; } s_wePh;
static const char* s_wePhStage = "";   // which candidate grounded (land log)
// last land sent: the cmd as PH wrote it + the END its sim gave. Next cmd checks the engine's copy and START
static struct { int cmd = -1; UserCmd_t c{}; Vec3 endO{}; float endVz = 0.f; } s_wePhSent;
static Vec3  s_wePhEndO{};   // END of the last grounding test (WeLandsNow)
static float s_wePhEndVz = 0.f;
// ref gives its roll 1.5 ms; ref's planner has no clock. Worst ref plan = 3 + 160 + 40x3 = 283 ticks
// (~2.5 ms at ~9 us/sim), so this cap only bites on a perf spike.
// ponytail: 5 ms safety cap. `WEPH: ... cut=` > 0 = it bit, raise it
constexpr long long k_wePhClockUs = 5000;
// ponytail: reach grid 2 ms/cmd (~60 sims at the logged ~30 us), rolled across cmds. `rcut=` every tick
// with reach=0 = the grid never finishes a lap in time, raise it
constexpr long long k_wePhReachUs = 2000;
static const char* const k_wePhStageSticky = "steer last";   // reach/face3 plan re-flown (land log stays quiet)

static void WeSetMoveKeys(UserCmd_t& c)
{
    c.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    if (c.forwardmove > 0.01f) c.buttons |= IN_FORWARD; else if (c.forwardmove < -0.01f) c.buttons |= IN_BACK;
    if (c.sidemove > 0.01f) c.buttons |= IN_MOVERIGHT; else if (c.sidemove < -0.01f) c.buttons |= IN_MOVELEFT;
}

// (+ an optional land test, wall climb's "higher than the ground you left").
// pooled = draw from the shared per-cmd sim pool (wall climb). Pixel hop is not: ref gives it its
// own 1.5 ms clock, and late in the pipeline the pool is often drained = every test false
// Leaves the entity at the test's END: WePixelHopAir restores once on the way out, not twice per test
static bool WeLandsNow(void* pLocal, int nBase, const UserCmd_t& c, float* landZ, int& sims, bool pooled)
{
    RestoreToFrame(nBase - 1);
    if (pooled && !AT_CanSim()) return false;
    WeSimTick(pLocal, c);
    ++sims;
    const bool ok = !WeOnLadderOrNoclip(pLocal) && (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    if (ok)
    {
        s_wePhEndO  = EntV3(pLocal, g_offOrigin);
        s_wePhEndVz = EntV3(pLocal, g_offVelocity).z;
        if (landZ) *landZ = s_wePhEndO.z;
    }
    return ok;
}

template <class Accept>
static bool WePixelHopAirImpl(void* pLocal, UserCmd_t* cmd, Accept accept, int& sims, bool pooled);

// the air half: true = `cmd` now grounds this tick. `accept` vets the landing height (nullptr = any).
// Entity is back at START on return
template <class Accept>
static bool WePixelHopAir(void* pLocal, UserCmd_t* cmd, Accept accept, int& sims, bool pooled)
{
    const int nBase = PredCommandsPredicted();
    const bool ok = WePixelHopAirImpl(pLocal, cmd, accept, sims, pooled);
    RestoreToFrame(nBase - 1);
    return ok;
}

template <class Accept>
static bool WePixelHopAirImpl(void* pLocal, UserCmd_t* cmd, Accept accept, int& sims, bool pooled)
{
    const int   nBase = PredCommandsPredicted();
    const float h     = WeHalfGrav();
    float z = 0.f;
    const auto test = [&](const UserCmd_t& c, const char* tag) {
        if (!WeLandsNow(pLocal, nBase, c, &z, sims, pooled) || !accept(z)) return false;
        s_wePhStage = tag;
        return true;
    };
    const auto flipDuck = [](UserCmd_t c) { c.buttons ^= IN_DUCK; WeSetMoveKeys(c); return c; };

    UserCmd_t c = *cmd;
    c.buttons &= ~IN_JUMP;
    if (test(c, "own")) { *cmd = c; return true; }
    UserCmd_t f = flipDuck(c);
    if (test(f, "own duck")) { *cmd = f; return true; }

    RestoreToFrame(nBase - 1);
    const WeWall wall = WeFindWallLines(pLocal, s_prePredOrigin, 0.f);
    if (!wall.ok) { if (!pooled) ++s_wePh.nowall; return false; }
    const Vec3  v      = s_prePredVel;
    const float velYaw = RAD2DEGF(atan2f(v.y, v.x));
    const float sign   = we::NormYaw(velYaw - wall.yaw) < 0.f ? -1.f : 1.f;
    for (const float off : { 0.f, 45.f })
    {
        UserCmd_t w = c;
        we::WishToCmd(we::NormYaw(sign * off + wall.yaw), 450.f, w.viewangles.y, w.forwardmove, w.sidemove);
        w.upmove = 0.f;
        if (test(w, off == 0.f ? "face" : "face45")) { *cmd = w; return true; }
        UserCmd_t wf = flipDuck(w);
        if (test(wf, off == 0.f ? "face duck" : "face45 duck")) { *cmd = wf; return true; }
    }

    const float dt = TickInterval();
    if (dt <= 0.f || !we::PhPinned(v.z, h)) return false;   // roll kept: see k_wePhClockUs
    if (!pooled) ++s_wePh.pin;
    // ref: QPC 1.5 ms of its own (freq * 15 / 10000); ours k_wePhClockUs. Pooled callers stay inside the cmd's clock
    LARGE_INTEGER f0{}, q0{}; QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&q0);
    long long span = f0.QuadPart * k_wePhClockUs / 1000000ll;
    if (pooled) span = (std::min)(span, TbBudgetLeftUs() * f0.QuadPart / 1000000ll);
    const auto expired = [&]() { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return q.QuadPart - q0.QuadPart > span; };
    for (int n = 0; n < we::k_phSteps && !expired(); ++n)
    {
        const int i = s_wePh.roll;
        s_wePh.roll = (s_wePh.roll + 1) % we::k_phSteps;
        const we::PhStep s = we::PhAt(i);
        UserCmd_t w = c;
        we::WishToCmd(we::NormYaw((s.flipSide ? -sign : sign) * s.yawOff + wall.yaw), s.move, w.viewangles.y, w.forwardmove, w.sidemove);
        w.upmove = 0.f;
        if (s.flipDuck) { w.buttons ^= IN_DUCK; WeSetMoveKeys(w); }   // ref :276 re-keys only with the duck flip
        if (!pooled) ++s_wePh.steps;
        if (test(w, "roll")) { s_wePh.roll = i; *cmd = w; return true; }   // same step leads next tick
    }
    return false;
}

// ref a plan onto a cmd. Wish -> forward/side off the view yaw (keys / upmove untouched, as ref)
static void WePhwApply(UserCmd_t& c, const we::PhPlan& p)
{
    if (!p.keep) we::WishToCmd(p.yaw, p.move, c.viewangles.y, c.forwardmove, c.sidemove);
    if (p.duck == 1) c.buttons |= IN_DUCK;
    else if (p.duck == 2) c.buttons &= ~IN_DUCK;
}

static Vec3  s_wePhSimO{};   // END of the last grounded WePhwSim (the planner keeps the best one's in s_wePhEndO)
static float s_wePhSimVz = 0.f;

// ref fly plan `p` from START for up to `horizon` ticks; found = FL_ONGROUND on tick `ticks`,
// speed = xy speed there. Leaves the entity wherever the sim stopped (the caller restores).
// ownFound/ownZ (not ref): stop once the hull can only land at or under your own floor (we::PhCantRise)
static bool WePhwSim(void* pLocal, int nBase, const UserCmd_t& base, we::PhPlan& p, int horizon,
                     bool ownFound = false, float ownZ = 0.f)
{
    p.found = false;
    RestoreToFrame(nBase - 1);
    UserCmd_t c = base;
    c.buttons &= ~IN_JUMP;
    WePhwApply(c, p);
    for (int t = 1; t <= horizon; ++t)
    {
        WeSimTick(pLocal, c);
        ++s_wePh.sims;
        if (WeOnLadderOrNoclip(pLocal)) break;
        if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
        {
            p.found = true;
            p.speed = EntV3(pLocal, g_offVelocity).Length2D();
            p.ticks = t;
            s_wePhSimO = EntV3(pLocal, g_offOrigin); s_wePhSimVz = EntV3(pLocal, g_offVelocity).z;
            break;
        }
        if (t < horizon && we::PhCantRise(ownFound, ownZ, EntV3(pLocal, g_offOrigin).z, EntV3(pLocal, g_offVelocity).z)) break;
    }
    return p.found;
}

// ref the landing planner. Candidate order and tables are ref's (wallengine_pure.h k_phw*)
static we::PhPlan WePhwPlan(void* pLocal, int nBase, const UserCmd_t& cmd)
{
    const Vec3  v      = s_prePredVel;
    const float speed0 = v.Length2D();
    we::PhPlan best;
    LARGE_INTEGER f0{}, q0{}; QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&q0);
    const long long span = f0.QuadPart * k_wePhClockUs / 1000000ll;
    bool cut = false;
    const auto expired = [&]() { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return cut = q.QuadPart - q0.QuadPart > span; };
    bool ownFound = false; float ownZ = 0.f;   // where your own move lands (set after the own passes)
    // true = stop searching (a landing keeps your speed)
    const auto offer = [&](we::PhPlan k, int horizon, bool byTicks, const char* tag) {
        if (WePhwSim(pLocal, nBase, cmd, k, horizon, ownFound, ownZ))
        {
            if (we::PhSameFloor(ownFound, ownZ, s_wePhSimO.z)) ++s_wePh.floor;
            else if (we::PhBetter(k, best, byTicks)) { best = k; s_wePhStage = tag; s_wePhEndO = s_wePhSimO; s_wePhEndVz = s_wePhSimVz; }
        }
        return we::PhDone(best, speed0);
    };
    const auto wish = [](float yaw, float move, int duck) {
        we::PhPlan k; k.keep = false; k.yaw = we::NormYaw(yaw); k.move = move; k.duck = duck; return k;
    };

    we::PhPlan own;
    bool done = offer(own, 1, false, "own");
    own.duck = (cmd.buttons & IN_DUCK) ? 2 : 1;
    if (!done) done = offer(own, 1, false, "own duck");
    if (!done && s_wePh.last.found) done = offer(s_wePh.last, 1, false, "last");
    if (done) return best;
    const bool reach  = g_phReach;
    const int  reachN = std::clamp(g_phReachTicks, 4, 32);
    // own move flown 6 ticks (> the 3-tick pass; reach: past its horizon): its floor is what every steered
    // plan must beat, and the floor the steered sims prune against
    {
        we::PhPlan ref;
        ownFound = WePhwSim(pLocal, nBase, cmd, ref, reach ? reachN + 6 : 6);
        ownZ = s_wePhSimO.z;
    }
    // reach sticky: last cmd's multi-tick steer still lands = keep flying it, skip every grid
    if (reach && s_wePh.last.found && s_wePh.last.ticks > 1)
    {
        offer(s_wePh.last, reachN, true, k_wePhStageSticky);
        if (best.found) return best;
    }

    // ref passes (land now / within 3). Rising too fast to ground inside 3 ticks = only reach can land
    const float velYaw = RAD2DEGF(atan2f(v.y, v.x));
    const auto nearPasses = [&]() {
        RestoreToFrame(nBase - 1);
        const WeWall wall = WeFindWallLines(pLocal, s_prePredOrigin, 0.f);
        if (!wall.ok)
        {
            ++s_wePh.nowall;
            for (const float off : we::k_phwVelOff) for (const float mv : we::k_phwVelMove) for (const int d : we::k_phwDuck)
                if (expired() || offer(wish(velYaw + off, mv, d), 1, false, "vel")) return;
            return;
        }
        const float sign = we::PhwSign(velYaw, wall.yaw);
        for (const float off : we::k_phwOff) for (const float mv : we::k_phwMove) for (const int d : we::k_phwDuck)
            if (expired() || offer(wish(wall.yaw + sign * off, mv, d), 1, false, "face")) return;
        if (best.found) return;
        for (const float off : we::k_phwOff3) for (const float mv : we::k_phwMove3) for (const int d : we::k_phwDuck)
        {
            if (expired()) return;
            offer(wish(wall.yaw + sign * off, mv, d), 3, true, "face3");
        }
    };
    if (we::PhCanLand(v.z, WeHalfGrav(), 3)) { nearPasses(); s_wePh.cut += cut; }
    if (best.found || !reach) return best;

    // reach: held wish off your velocity (view yaw when too slow to have one), least turn first, first land
    // above your own floor wins. Own clock: the grids above may have spent theirs; the grid rolls on next cmd
    LARGE_INTEGER q1{}; QueryPerformanceCounter(&q1);
    const long long rspan  = f0.QuadPart * k_wePhReachUs / 1000000ll;
    const float baseYaw = speed0 >= 30.f ? velYaw : cmd.viewangles.y;
    const int   flip    = (cmd.buttons & IN_DUCK) ? 2 : 1;
    for (int n = 0; n < we::k_phReachSteps; ++n)
    {
        LARGE_INTEGER q{}; QueryPerformanceCounter(&q);
        if (q.QuadPart - q1.QuadPart > rspan) { ++s_wePh.rcut; break; }
        const int i = s_wePh.reachRoll;
        s_wePh.reachRoll = (i + 1) % we::k_phReachSteps;
        const we::PhReachStep s = we::PhReachAt(i, g_phReachAngle);
        if (s.skip) continue;
        offer(wish(baseYaw + s.off, 450.f, s.flipDuck ? flip : 0), reachN, true, "reach");
        if (best.found) { s_wePh.reachRoll = 0; ++s_wePh.reach; break; }   // next search starts at least turn
    }
    return best;
}

static void WePixelHop(void* pLocal, UserCmd_t* cmd)
{
    s_pixelHopHit = false;
    {
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != s_sec)
        {
            s_sec = sec;
            // printed on any search, so "never lands" still leaves a line saying why
            if (g_debugLogEnable && (s_wePh.lands || s_wePh.hops || s_wePh.air))
                DebugLog("WEPH: lands=%d own=%d steer=%d pinLands=%d hops=%d air=%d pin=%d nowall=%d tb=%d cut=%d floor=%d reach=%d rcut=%d sims=%d",
                         s_wePh.lands, s_wePh.own, s_wePh.steer, s_wePh.pinLands, s_wePh.hops, s_wePh.air, s_wePh.pin,
                         s_wePh.nowall, s_wePh.tb, s_wePh.cut, s_wePh.floor, s_wePh.reach, s_wePh.rcut, s_wePh.sims);
            s_wePh.lands = s_wePh.own = s_wePh.steer = s_wePh.pinLands = s_wePh.hops = s_wePh.air = s_wePh.pin = 0;
            s_wePh.nowall = s_wePh.tb = s_wePh.cut = s_wePh.floor = s_wePh.reach = s_wePh.rcut = s_wePh.sims = 0;
        }
    }
    const bool key = g_pixelHop && MovementKeyOK(g_pixelHopKey, g_pixelHopMode) && EntAlive(pLocal);
    s_phSyncWant = key;   // armed on the key, before the jump: cl_pred_optimize only bites from the next packet
    // last cmd's land: did the engine send what PH proved, and did it ground? took=0 same=1 = sim vs real
    // state differ; same=0 = a writer after PH changed the cmd (d* = what changed)
    if (s_wePhSent.cmd >= 0 && s_wePhSent.cmd == cmd->command_number - 1)
    {
        const UserCmd_t* s = AsUserCmd(s_wePhSent.cmd);
        const UserCmd_t& p = s_wePhSent.c;
        const bool same = s && s->forwardmove == p.forwardmove && s->sidemove == p.sidemove && s->buttons == p.buttons
                       && s->viewangles.x == p.viewangles.x && s->viewangles.y == p.viewangles.y;
        DebugLog("WEPH: verify took=%d same=%d dfwd=%.4g dside=%.4g dyaw=%.4g btn=%x/%x | sim z=%.4f vz=%.2f got z=%.4f vz=%.2f",
                 (s_prePredFlags & FL_ONGROUND) ? 1 : 0, same ? 1 : 0, s ? s->forwardmove - p.forwardmove : 0.f,
                 s ? s->sidemove - p.sidemove : 0.f, s ? s->viewangles.y - p.viewangles.y : 0.f, s ? s->buttons : 0, p.buttons,
                 s_wePhSent.endO.z, s_wePhSent.endVz, s_prePredOrigin.z, s_prePredVel.z);
    }
    s_wePhSent.cmd = -1;
    // ref 18000c0b0: the TB key wins (PH never runs with it); ref 180016480: ladder / noclip drop the plan
    if (!key || On(g_textureBug, g_textureBugKey, g_textureBugMode) || WeOnLadderOrNoclip(pLocal))
    {
        s_pixelHopOn = false;
        s_wePh.armed = false;
        s_wePh.last = {};
        return;
    }
    s_pixelHopOn = true;
    const int  nBase    = PredCommandsPredicted();
    const bool grounded = (s_prePredFlags & FL_ONGROUND) != 0;
    // grounded START + IN_JUMP without your space = another feature's sim-proven takeoff (edge jump,
    // LJ / MJ edge, fireman launch), all post-pred before this. PH owns YOUR space, not their press:
    // stripping it here was "LJ edge jump never jumps" whenever PH shares the bind (11-08)
    if (grounded && (cmd->buttons & IN_JUMP) && !s_rawJumpHeld)
    {
        s_wePh.armed = false;
        s_wePh.last = {};
        return;
    }
    cmd->buttons &= ~IN_JUMP;

    // 1. jump test
    if (s_rawJumpHeld || s_wePh.armed)
    {
        RestoreToFrame(nBase - 1);
        UserCmd_t j = *cmd;
        j.buttons |= IN_JUMP;
        WeSimTick(pLocal, j);
        ++s_wePh.sims;
        const bool took = we::PhJumpTook(s_prePredVel.z, EntV3(pLocal, g_offVelocity).z);
        RestoreToFrame(nBase - 1);
        if (took)
        {
            if (s_wePh.armed) DebugLog("WEPH: hop z=%.3f xy=%.1f by=%s", s_prePredOrigin.z, s_prePredVel.Length2D(), s_rawJumpHeld ? "space" : "land");
            cmd->buttons |= IN_JUMP;
            s_pixelHopHit = true;
            s_wePh.armed = false;
            ++s_wePh.hops;
            AT_Stamp(*cmd, "ph");
            return;
        }
    }
    if (grounded) { s_wePh.armed = false; s_wePh.last = {}; return; }   // standing: your move, nothing to land

    // 2. land plan
    ++s_wePh.air;
    const bool pinned = we::PhPinned(s_prePredVel.z, WeHalfGrav());
    if (pinned) ++s_wePh.pin;
    // rising too fast to ground inside the horizon (reach: its N ticks, a jump onto a box top)
    if (!we::PhCanLand(s_prePredVel.z, WeHalfGrav(), g_phReach ? std::clamp(g_phReachTicks, 4, 32) : 3)) return;
    const we::PhPlan plan = WePhwPlan(pLocal, nBase, *cmd);
    RestoreToFrame(nBase - 1);
    if (plan.found)
    {
        WePhwApply(*cmd, plan);
        s_wePh.last = plan.keep ? we::PhPlan{} : plan;   // ref stores a kept own move as "no plan"
        // a land you steered into yourself (a plain fall) is not a hop: space still gates that one.
        // Armed on the land cmd only: a reach steer that later misses must not auto-hop you off the floor
        s_wePh.armed = s_wePh.armed || ((!plan.keep || pinned) && plan.ticks == 1);
        s_pixelHopHit = true;
        ++s_wePh.lands;
        ++(plan.keep ? s_wePh.own : s_wePh.steer);
        if (pinned) ++s_wePh.pinLands;
        if (!plan.keep || pinned) AT_Stamp(*cmd, "ph");
        if ((!plan.keep || pinned) && s_wePhStage != k_wePhStageSticky)   // a held steer logs once, at its find
        {
            DebugLog("WEPH: land by=%s t=%d pin=%d z=%.3f vz=%.2f xy=%.1f -> %.1f yaw=%.2f move=%.3f duck=%d",
                     s_wePhStage, plan.ticks, pinned ? 1 : 0, s_prePredOrigin.z, s_prePredVel.z, s_prePredVel.Length2D(),
                     plan.speed, plan.yaw, plan.move, plan.duck);
        }
        if (plan.ticks == 1)
        {
            s_wePhSent.cmd = cmd->command_number;
            s_wePhSent.c = *cmd;
            s_wePhSent.endO = s_wePhEndO;
            s_wePhSent.endVz = s_wePhEndVz;
        }
        return;
    }
    // 3. nothing lands: ref falls back to its texture bug -- the face hug it writes is what walks the hull
    // onto the ledge corner (log 11-08: tb= > 0 every working hop). AA hold keeps a live surf instead
    if (s_aaHolding) return;
    WeTextureBugTick(pLocal, cmd);
    if (s_tbOwnsCmd) { s_pixelHopHit = true; ++s_wePh.tb; }
}

// ====================================================================================================
// TEXTURE BUG. Air (or the jump tick) beside a vertical face found by WeFindWallLines.
// A candidate = wish at world yaw (offset off "straight into the face", turned against your along-face
// velocity) x wish size, flown one tick; pinned = END vz + h in (-1/32, 0.3). Search order:
//   refine   (last pin < 3 cmds ago) last offset +-{0,.5,1.25,2.25} x last size *{1,.6,1.6}; a pin ends it
//   sweep    offsets 6..89.999 x sizes {.05,.25,1,15}, then sizes .001..15 at the best offset
//   repeat   the refine's exact last cmd wins if it pins within 1.5 u/s of the best
//   golden   4 golden-section steps on the size in [.5x, 1.5x]
//   final    re-fly with 2 extension ticks (pin count), a 1-pin result tries the yaw +-0.6
// Crouch: a crouched retry replaces a result that pins MORE (planned: 2 ticks standing, then crouched).
// Send: no pin = the best approach (closest gap) still goes out; a pin eases from the last sent cmd toward
// the new one (largest step toward it that still pins). Moving >= 40 with no keys and a cmd that would
// stall you to 1..8 u/s = hands off.
// ====================================================================================================
struct WeTbCtx
{
    void* p = nullptr; int nBase = 0; UserCmd_t cmd{}; int buttons = 0; float viewYaw = 0.f;
    float h = 0.f; Vec3 n{}; float wallYaw = 0.f; float sign = 1.f; bool ducking = false; bool hasInput = false;
    float speed = 0.f; float vz = 0.f; Vec3 ext{};
};
static struct
{
    float wallAng = 0.f;                                 // DAT_1800eb388 (radians)
    int   pinCmd = -1000; float lastOff = 0.f, lastMove = 0.f;    // DAT_1800bef9c / eb38c / eb390
    int   sendCmd = -1000; float sendRel = 0.f, sendMove = 0.f;   // DAT_1800befa0 / eb394 / eb398
    bool  hold = false, plan = false; int planLeft = 0;  // DAT_1800e967e / e967f / eb39c
    float planYaw = 0.f, planMove = 0.f; int planBtnA = 0, planBtnB = 0;   // eb3a0 / eb3a4 / eb3ac / eb3b0
    int   sims = 0, pins = 0, approach = 0, stalls = 0, none = 0;
} s_weTb;

static void WeTbResetPlan() { s_weTb.hold = s_weTb.plan = false; s_weTb.planLeft = 0; s_weTb.planYaw = s_weTb.planMove = 0.f; s_weTb.planBtnA = s_weTb.planBtnB = 0; }


static bool WeTbSim(const WeTbCtx& x, float yaw, float fmove, bool duck, bool extend, we::TbRes& r)
{
    r = we::TbRes{};
    RestoreToFrame(x.nBase - 1);
    if (!AT_CanSim()) return false;
    UserCmd_t c = x.cmd;
    c.buttons = x.buttons | (duck ? IN_DUCK : 0);
    we::WishToCmd(yaw, fmove, x.viewYaw, c.forwardmove, c.sidemove);
    c.upmove = 0.f;
    WeSimTick(x.p, c);
    ++s_weTb.sims;
    r.found = true; r.fmove = fmove; r.yaw = yaw; r.gap = 0.f; r.duck = duck;
    const Vec3 v = EntV3(x.p, g_offVelocity);
    r.speed = v.Length2D();
    if (r.speed >= 1.f)
    {
        const Vec3 o = EntV3(x.p, g_offOrigin);
        TraceFilterWorldOnly fil; fil.Setup();
        GameTrace_t tr{};
        TraceLine(o, Vec3(o.x - x.n.x * 32.f, o.y - x.n.y * 32.f, o.z), 0xFFFFFFFFu, &fil, &tr);
        r.gap = tr.fraction * 32.f - (fabsf(x.n.y) * x.ext.y + fabsf(x.n.x) * x.ext.x);
        if (we::TbPinVz(v.z, x.h))
        {
            r.pins = 1;
            r.rise = x.vz > 0.f && fabsf(v.z + x.h) < 0.01f;
            for (int k = 0; extend && k < 2 && AT_CanSim(); ++k)
            {
                WeSimTick(x.p, c);
                ++s_weTb.sims;
                if (!we::TbPinVz(EntV3(x.p, g_offVelocity).z, x.h)) break;
                ++r.pins;
            }
        }
    }
    RestoreToFrame(x.nBase - 1);
    return true;
}

// standing first, crouched retry when it could matter
static we::TbRes WeTbRun(const WeTbCtx& x, float fmove, float yaw, bool extend)
{
    we::TbRes r0;
    if (!WeTbSim(x, yaw, fmove, false, extend, r0)) return we::TbRes{};
    r0.duck = false;
    r0.duckMore = false;
    if (!x.ducking && (extend || r0.pins < 1))
    {
        we::TbRes r1;
        if (WeTbSim(x, yaw, fmove, true, extend, r1))
        {
            const bool more = we::TbDuckMore(r0.pins, r0.gap, r1.pins, r1.gap);
            const bool only = we::TbDuckOnly(r0.pins, r1.pins, r1.rise);
            if (more || only)
            {
                r0.gap = r1.gap; r0.pins = r1.pins; r0.speed = r1.speed; r0.rise = r1.rise;
                r0.duck = true;
                r0.duckMore = more;
            }
        }
    }
    return r0;
}

// xy speed after one tick of the cmd with this move
static float WeSpeedAfter(const WeTbCtx& x, const UserCmd_t& c, float fwd, float side)
{
    RestoreToFrame(x.nBase - 1);
    if (!AT_CanSim()) return -1.f;
    UserCmd_t t = c;
    t.forwardmove = fwd; t.sidemove = side;
    WeSimTick(x.p, t);
    ++s_weTb.sims;
    const float s = EntV3(x.p, g_offVelocity).Length2D();
    RestoreToFrame(x.nBase - 1);
    return s;
}


static bool WeTbStalls(const WeTbCtx& x, float yaw, float fmove, int buttons)
{
    if (x.hasInput || x.speed < 40.f) return false;
    UserCmd_t c = x.cmd;
    c.buttons = buttons;
    float f = 0.f, s = 0.f;
    we::WishToCmd(yaw, fmove, x.viewYaw, f, s);
    const float after = WeSpeedAfter(x, c, f, s);
    return after >= 0.f && we::TbStalls(false, x.speed, after);
}


static void WeTbApply(const WeTbCtx& x, UserCmd_t* cmd, float yaw, float fmove, int buttons)
{
    we::WishToCmd(yaw, fmove, x.viewYaw, cmd->forwardmove, cmd->sidemove);
    cmd->upmove = 0.f;
    cmd->buttons = buttons;
    const float f = cmd->forwardmove, s = cmd->sidemove;
    if (fabsf(f) >= 0.01f || fabsf(s) >= 0.01f)
    {
        const float pos = WeSpeedAfter(x, *cmd, f, s);
        if (pos >= 0.f && pos < 1.f)
        {
            const float neg = WeSpeedAfter(x, *cmd, -f, -s);
            if (neg > pos + 0.01f) { cmd->forwardmove = -f; cmd->sidemove = -s; }
        }
    }
}

// returns: 0 = nothing sent, 1 = cmd written (approach / hold), hit = a pin went out
static int WeTextureBug(void* pLocal, UserCmd_t* cmd, bool& hit)
{
    hit = false;
    const int now = cmd->command_number;
    const auto resetAll = [&] { WeTbResetPlan(); };
    if ((s_prePredFlags & FL_ONGROUND) && !(cmd->buttons & IN_JUMP)) { resetAll(); return 0; }

    WeTbCtx x;
    x.p = pLocal; x.nBase = PredCommandsPredicted(); x.cmd = *cmd; x.buttons = cmd->buttons; x.viewYaw = cmd->viewangles.y;
    x.h = WeHalfGrav(); x.ducking = (cmd->buttons & IN_DUCK) != 0;
    x.hasInput = fabsf(cmd->forwardmove) > 0.01f || fabsf(cmd->sidemove) > 0.01f;
    x.speed = s_prePredVel.Length2D(); x.vz = s_prePredVel.z; x.ext = WeExtent(pLocal);

    RestoreToFrame(x.nBase - 1);
    const WeWall wall = WeFindWallLines(pLocal, s_prePredOrigin, s_weTb.wallAng);
    if (!wall.ok) { resetAll(); ++s_weTb.none; return 0; }
    s_weTb.wallAng = wall.ang;
    x.n = wall.n; x.wallYaw = we::NormYaw(wall.yaw);
    const float velYaw = RAD2DEGF(atan2f(s_prePredVel.y, s_prePredVel.x));
    x.sign = we::NormYaw(velYaw - x.wallYaw) < 0.f ? 1.f : -1.f;
    const int sincePin = now - s_weTb.pinCmd;
    const auto yawAt = [&](float off) { return we::NormYaw(x.sign * off + x.wallYaw); };
    const auto relOf = [&](float yaw) { return std::clamp(we::NormYaw(yaw - x.wallYaw) * x.sign, we::k_tbMinYaw, we::k_tbMaxYaw); };

    // ---- refine around the last pin ----
    we::TbRes best, repeat;
    if (sincePin < 3)
    {
        for (const float off : we::k_tbRefineOff)
        {
            const float yaw = yawAt(std::clamp(off + s_weTb.lastOff, we::k_tbMinYaw, we::k_tbMaxYaw));
            for (const float sc : we::k_tbRefineScale)
            {
                const we::TbRes r = WeTbRun(x, std::clamp(sc * s_weTb.lastMove, we::k_tbMinMove, we::k_tbMaxMove), yaw, false);
                if (off == 0.f && sc == 1.f) repeat = r;
                if (we::TbBetter(r, best)) best = r;
            }
        }
    }
    // ---- full sweep ----
    if (!(best.found && best.pins >= 1))
    {
        we::TbRes sweep;
        for (const float off : we::k_tbYaw)
            for (const float m : we::k_tbMove)
            {
                we::TbRes r;
                if (WeTbSim(x, yawAt(off), m, false, false, r) && we::TbBetter(r, sweep)) sweep = r;
            }
        if (sweep.found)
        {
            const float off = relOf(sweep.yaw);
            we::TbRes fine;
            for (const float m : we::k_tbFine)
            {
                const we::TbRes r = WeTbRun(x, m, yawAt(off), false);
                if (we::TbBetter(r, fine)) fine = r;
            }
            if (we::TbBetter(fine, sweep)) sweep = fine;
            if (!best.found || we::TbBetter(sweep, best)) best = sweep;
        }
    }
    // ---- the last cmd again, when it still pins about as well ----
    if (repeat.found && repeat.pins >= 1 && best.found && best.pins > 0 && best.speed - repeat.speed < 1.5f)
        best = repeat;
    // ---- golden section on the size ----
    if (best.found)
    {
        float a = std::clamp(best.fmove * 0.5f, we::k_tbMinMove, we::k_tbMaxMove);
        float b = std::clamp(best.fmove * 1.5f, we::k_tbMinMove, we::k_tbMaxMove);
        if (a < b)
        {
            constexpr float g = 0.618034f;
            float c = b - (b - a) * g, d = a + (b - a) * g;
            we::TbRes rc = WeTbRun(x, c, best.yaw, false), rd = WeTbRun(x, d, best.yaw, false);
            for (int it = 0; it < 4; ++it)
            {
                if (we::TbBetter(rc, rd)) { b = d; d = c; rd = rc; c = b - (b - a) * g; rc = WeTbRun(x, c, best.yaw, false); }
                else                      { a = c; c = d; rc = rd; d = a + (b - a) * g; rd = WeTbRun(x, d, best.yaw, false); }
            }
            if (we::TbBetter(rc, best)) best = rc;
            if (we::TbBetter(rd, best)) best = rd;
        }
        if (best.pins >= 1) { s_weTb.lastOff = relOf(best.yaw); s_weTb.lastMove = best.fmove; }
    }
    if (!(best.found && best.pins >= 1)) s_weTb.pinCmd = -1000;

    // ---- final: pin count with the extension ticks ----
    we::TbRes fin;
    float finYaw = x.wallYaw, finMove = cmd->forwardmove;
    int btnWin = x.buttons, btnStand = x.buttons, btnDuck = x.buttons | IN_DUCK;
    bool duckMore = false;
    int pins = 0;
    if (best.found)
    {
        fin = WeTbRun(x, best.fmove, best.yaw, true);
        if (fin.found)
        {
            if (fin.pins == 1)
            {
                const float rel = we::NormYaw(best.yaw - x.wallYaw);
                for (const float d : { -0.6f, 0.6f })
                {
                    const we::TbRes r = WeTbRun(x, best.fmove, x.sign * d + rel + x.wallYaw, true);
                    if (r.found && r.pins > fin.pins && r.speed >= fin.speed - 1.5f) fin = r;
                }
            }
            pins = fin.pins; finYaw = fin.yaw; finMove = fin.fmove; duckMore = fin.duckMore;
            btnWin = fin.duck ? (x.buttons | IN_DUCK) : x.buttons;
            if (pins > 0) { s_weTb.lastOff = relOf(fin.yaw); s_weTb.pinCmd = now; s_weTb.lastMove = fin.fmove; }
        }
    }
    const float vzOff = x.vz + x.h;
    const bool fallingFast = !(vzOff > -0.03125f && vzOff < 0.3f) && x.vz < -1.f;

    // ---- planned crouch / its hold ----
    if (!x.ducking)
    {
        if (s_weTb.hold)
        {
            const int btn = fallingFast ? s_weTb.planBtnA : s_weTb.planBtnB;
            if (WeTbStalls(x, s_weTb.planYaw, s_weTb.planMove, btn)) { resetAll(); ++s_weTb.stalls; return 0; }
            hit = true;
            WeTbApply(x, cmd, s_weTb.planYaw, s_weTb.planMove, btn);
            if (fallingFast) WeTbResetPlan();
            return 1;
        }
        if (s_weTb.plan)
        {
            if (WeTbStalls(x, s_weTb.planYaw, s_weTb.planMove, s_weTb.planBtnA)) { resetAll(); ++s_weTb.stalls; return 0; }
            hit = true;
            WeTbApply(x, cmd, s_weTb.planYaw, s_weTb.planMove, s_weTb.planBtnA);
            if (s_weTb.planLeft < 1) { s_weTb.hold = true; s_weTb.plan = false; }
            else --s_weTb.planLeft;
            return 1;
        }
    }
    else
    {
        if (pins > 0 && WeTbStalls(x, finYaw, finMove, btnWin)) { resetAll(); ++s_weTb.stalls; return 0; }
        WeTbResetPlan();
    }
    if (duckMore && !x.ducking)
    {
        if (pins > 0 && WeTbStalls(x, finYaw, finMove, btnStand)) { resetAll(); ++s_weTb.stalls; return 0; }
        WeTbResetPlan();
        s_weTb.plan = true;
        s_weTb.planYaw = finYaw; s_weTb.planMove = finMove; s_weTb.planBtnA = btnStand; s_weTb.planBtnB = btnDuck;
        hit = pins > 0;
        WeTbApply(x, cmd, finYaw, finMove, btnStand);
        DebugLog("WETB: crouch plan off=%.3f move=%.4f pins=%d", relOf(finYaw), finMove, pins);
        return 1;
    }
    WeTbResetPlan();
    if (pins > 0 && WeTbStalls(x, finYaw, finMove, btnWin)) { ++s_weTb.stalls; return 0; }
    hit = pins > 0;

    float sendYaw = finYaw, sendMove = finMove;
    int sendCmd = -1000;
    if (pins > 0)
    {
        // ease from the last sent cmd: the largest step toward the new one that still pins
        sendCmd = now;
        const float rel = we::NormYaw(finYaw - x.wallYaw);
        if (now - s_weTb.sendCmd < 3)
        {
            const bool duck = (btnWin & IN_DUCK) != 0 && !x.ducking;
            const auto at = [&](float f, float& yaw, float& mv) {
                mv  = std::clamp((finMove - s_weTb.sendMove) * f + s_weTb.sendMove, we::k_tbMinMove, we::k_tbMaxMove);
                yaw = we::NormYaw(rel - s_weTb.sendRel) * f + s_weTb.sendRel + x.wallYaw;
            };
            const auto pinsAt = [&](float f, float& yaw, float& mv) {
                at(f, yaw, mv);
                we::TbRes r;
                return WeTbSim(x, yaw, mv, duck, false, r) && r.pins >= 1;
            };
            float y1, m1;
            if (!pinsAt(1.f, y1, m1))
            {
                float okF = 1.f, lo = 0.35f, y = 0.f, m = 0.f;
                bool any = false;
                float f = 0.675f;
                for (int step = 0; step < 3; ++step)
                {
                    if (pinsAt(f, y, m)) { okF = f; sendYaw = y; sendMove = m; any = true; }
                    else lo = f;
                    f = (lo + okF) * 0.5f;
                }
                if (!any) { sendYaw = finYaw; sendMove = finMove; }
            }
        }
        s_weTb.sendRel = we::NormYaw(sendYaw - x.wallYaw);
        s_weTb.sendMove = sendMove;
        ++s_weTb.pins;
    }
    else
        ++s_weTb.approach;
    s_weTb.sendCmd = sendCmd;
    WeTbApply(x, cmd, sendYaw, sendMove, btnWin);
    return 1;
}

// TextureBug() entry when the TB key is held (texturebug.inl runs the botox engine after a miss).
// true = the stall veto fired: hands off, the fallback must not write either
static bool WeTextureBugTick(void* pLocal, UserCmd_t* pCmd)
{
    s_textureBugHit = false;
    s_tbOwnsCmd = false;
    if (EntMoveType(pLocal) != MOVETYPE_WALK) { WeTbResetPlan(); s_tbPhase = TBP_OFF; return false; }
    bool hit = false;
    const int stalls0 = s_weTb.stalls;
    const int wrote = WeTextureBug(pLocal, pCmd, hit);
    const bool veto = s_weTb.stalls != stalls0;
    RestoreToFrame(PredCommandsPredicted() - 1);
    s_textureBugHit = hit;
    s_tbOwnsCmd = wrote != 0;
    s_tbRideLen = hit ? s_tbRideLen + 1 : 0;
    s_tbPhase = hit ? TBP_RIDE : wrote ? TBP_HUG : TBP_SCAN;
    if (wrote) AT_Stamp(*pCmd, "tb");

    static int s_sec = -1;
    const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
    if (sec != s_sec)
    {
        s_sec = sec;
        if (g_debugLogEnable && (s_weTb.pins || s_weTb.approach || s_weTb.stalls))
            DebugLog("WETB: pins=%d approach=%d stall=%d nowall=%d sims=%d off=%.3f move=%.4f", s_weTb.pins, s_weTb.approach,
                     s_weTb.stalls, s_weTb.none, s_weTb.sims, s_weTb.lastOff, s_weTb.lastMove);
        s_weTb.pins = s_weTb.approach = s_weTb.stalls = s_weTb.none = s_weTb.sims = 0;
    }
    return veto;
}
