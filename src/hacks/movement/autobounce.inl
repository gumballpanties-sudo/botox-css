// AUTO BOUNCE = botox hacks/movement/auto_bounce.cpp 1:1 (+ the n_air flight from air_rollout.h).
// Textually included by movement.cpp after airstuck.inl (uses AsClock, AsSetOrigin) and before
// movement_tricks2.inl (bounce assist still lives there). Runs post prediction, in the probe block.
//
// The trick: falling onto a sloped face, ClipVelocity turns the fall speed into slide speed
// (|out| = vz * nz * sqrt(1 - nz^2) straight down). The planner scans faces under / ahead of the
// hull, picks the landing spot that pays the most over your own xy, flies it tick by tick with the
// real AirAccelerate cap, steers there, and on the landing tick jumps so the walkable-face clip
// becomes a bhop instead of a friction stop.
//
// css deltas vs botox (cs:go), see autobounce_pure.h header for verified source lines:
//   stamina: real IN_JUMP press lets the engine's own CheckJumpButton apply the ratio, nothing to port here.
//   air duck wish crop: unavoidable (CheckParameters clamps wish to maxspeed before Duck() crops it, no
//     wishspeed left to pre-compensate with) -- see autobounce_pure.h header, not a modeling gap
//   bhop crop 1.1x maxspeed, ground snap vz <= 140  -> autobounce_pure.h
//   air duck lifts 8.5u to a 45u hull (css g_CSViewVectors, cs_gamerules.cpp:75-79)
//   no ladders / fireman: yield = the TF2-era owners (TB/HS/HB/AS/WC/PS), unchanged by this pass
//   budget = AsClock (botox c_sim_budget port), 0.25 tick share like botox
//   engine tick = RestoreToFrame + Pred_Simulate on the edited cmd (botox engine_tick)
#include "autobounce_pure.h"

// lumi mode knobs (botox variables.h m_auto_bounce_lumi_*), declared extern movement.h
int   g_abLumiRays     = 16;
float g_abLumiRadius   = 20.f;
float g_abLumiDepth    = 1024.f;
float g_abLumiMinAngle = 10.f;
float g_abLumiMaxAngle = 60.f;
float g_abLumiSmooth   = 0.5f;

namespace abk
{
    constexpr float kAirVeerCap   = 30.f;
    constexpr float kFaceGroupZ   = 8.f;
    constexpr float kArriveFrac   = 0.5f;
    constexpr int   kArmWindow    = 16;
    constexpr float kClipVzEps    = 1.f;
    constexpr int   kSettleCmds   = 4;
    constexpr float kChordTime    = 0.0625f;
    constexpr float kSweepTime    = 1.f;
    constexpr float kPlanHyst     = 10.f;
    constexpr float kSameFace     = 0.99f;
    constexpr float kBudgetShare  = 0.25f;
    constexpr float kFlySlack     = 0.25f;
    constexpr float kFlyMax       = 2.f;
    constexpr float kCoastChord   = 0.03125f;
    constexpr float kScanRadius   = 24.f;
    constexpr int   kScanRays     = 8;
    constexpr float kScanInnerR   = 10.f;
    constexpr int   kScanInnerRays = 6;
    constexpr float kScanDepth    = 1024.f;
    constexpr float kSupportOffsets[] = { 3.f, 1.5f, 0.f, -1.5f, -3.f, -6.f, -9.f };
    constexpr int   kSupportCoarse[]  = { 0, 2, 4, 5, 6 };
    constexpr int   kRefineSteps  = 6;
    constexpr float kRefineDz     = 1.f;
    constexpr int   kRefineUnder  = 6;
    constexpr int   kRefineWide   = 8;
    constexpr float kDuckLift     = 8.5f;  // css air duck: 0.5*(62-45) (cs_gamemovement.cpp FinishDuck, g_CSViewVectors)
    constexpr float kDuckHullZ    = 45.f;  // css duck hull max.z (g_CSViewVectors, cs_gamerules.cpp:79)
    constexpr int   kAlignSlides  = 4;
    constexpr float kWallNz       = 0.1f;
    constexpr float kLookaheadTime = 1.f;
    constexpr float kLookaheadStep = 0.03125f;
    constexpr float kWideRings[]   = { 36.f, 54.f, 72.f };
    constexpr int   kWideRingRays[] = { 16, 20, 24 };
    constexpr int   kWideMax      = 16 + 20 + 24;
    constexpr float kAimSpan      = 8.f;
    constexpr int   kAimSteps     = 6;
    constexpr float kSideMax      = 20.f;
    constexpr int   kRefuseRelease = 3;
    constexpr float kExitTime     = 0.0625f;
    constexpr float kExitMin      = 4.f;
    constexpr float kSnapReach    = 2.25f;
    constexpr float kPhaseShifts[] = { 1.5f, -1.5f, 3.f, -3.f, 4.5f, -4.5f, 6.f, -6.f };
    constexpr float kMinFaceNz    = 0.99f;
    constexpr int   kVizMax       = 300;
    constexpr int   IN_SPEED_AB   = 1 << 17;
}

// ---- per-session state (botox auto_bounce_data_t) ----
struct AbData_t
{
    bool  ownsCmd = false, found = false, keep = false;
    int   armedTick = 0;
    float entrySpeed = 0.f, gain = 0.f, angle = 0.f, result = 0.f;
    Vec3  target{}, normal{};
    int   lastCmd = 0, clipCmd = 0;
    float lastVz = 0.f, lastSpeed = 0.f;
    bool  jumpOwed = false;
    int   refused = 0;
    float lumiHoldUntil = 0.f;   // botox m_lumi_hold_until, mode==3 only

    void Disarm()
    {
        ownsCmd = found = keep = false;
        armedTick = 0;
        entrySpeed = gain = angle = result = 0.f;
        target = normal = Vec3();
        refused = 0;
    }
    void Reset()
    {
        Disarm();
        lastCmd = clipCmd = 0;
        lastVz = lastSpeed = 0.f;
        jumpOwed = false;
        lumiHoldUntil = 0.f;
    }
};
static AbData_t s_ab;
static bool AutoBounceOwnsCmd() { return s_ab.ownsCmd; }
static void* s_abLocal = nullptr;   // trace filter skip, set once per AutoBounce call
static bool  s_abAlign = false;     // botox g_ab_align, per cmd

// viz snapshot (movement thread -> render thread), same contract as EB_GetDrawData
static std::mutex s_abVizLock;
static std::vector<Vec3> s_abVizPath;
static Vec3 s_abVizLand{}, s_abVizNormal{};
static bool s_abVizActive = false;

void AB_GetDrawData(AbDrawData& out)
{
    std::lock_guard<std::mutex> l(s_abVizLock);
    out.active = s_abVizActive && s_abVizPath.size() >= 2;
    out.path   = s_abVizPath;
    out.land   = s_abVizLand;
    out.normal = s_abVizNormal;
}

static void AbVizSet(const Vec3* path, int n, const Vec3& land, const Vec3& normal)
{
    std::lock_guard<std::mutex> l(s_abVizLock);
    s_abVizPath.assign(path, path + n);
    s_abVizLand = land; s_abVizNormal = normal;
    s_abVizActive = n >= 2;
}
static void AbVizClear()
{
    std::lock_guard<std::mutex> l(s_abVizLock);
    s_abVizActive = false;
}

// ---- small vector bits Vec3 lacks ----
static inline Vec3 AbDiv(const Vec3& v, float f) { return Vec3(v.x / f, v.y / f, v.z / f); }
static inline float AbYaw(const Vec3& v) { return RAD2DEGF(atan2f(v.y, v.x)); }

static float AbDt()   { const float t = TickInterval(); return t > 0.f ? t : 0.015f; }
static float AbGrav() { return CvarFloat("sv_gravity", 800.f); }

static ab::World AbReadWorld(void* pLocal)
{
    ab::World w;
    w.dt    = AbDt();
    w.g     = std::max(AbGrav(), 1.f);
    w.accel = CvarFloat("sv_airaccelerate", 10.f);
    const float ms = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    w.maxSpeed = ms > 1.f ? std::min(ms, 450.f) : 300.f;
    return w;
}

struct AbWorld_t
{
    ab::World w;
    Vec3  mins{}, maxs{};
    int   wallSlides = 0;
    float snap = 0.f;
};

static float AbKeep(float result, float nz, const AbWorld_t& W) { return ab::KeepSpeed(result, nz, W.w.maxSpeed); }

struct AbHit_t     { Vec3 pos{}, normal{}; float angle = 0.f, shape = 0.f; };
struct AbContact_t { Vec3 pos{}, normal{}; float time = 0.f; };
struct AbState_t   { Vec3 origin{}, velocity{}; int k = 0; };

struct AbPlan_t
{
    bool  valid = false, keep = false;
    Vec3  target{}, normal{};
    float drop = 0.f, vzHit = 0.f, ttc = 0.f, result = 0.f, floor = 0.f;
    int   lookahead = 0;
    float place = 99.f;
    bool  flown = false;
    float pred = -1.f;
    int   predTicks = -1;
    bool  firstOk = false;
    AbState_t first{};
    int   slides = 0;
    float aim = 0.f, side = -1.f, room = -1.f;
};

struct AbResult_t
{
    bool valid = false, hit = false, snapped = false;
    int  ticks = 0, wrote = 0, slides = 0;
    Vec3 in{}, out{};
    GameTrace_t trace{};
    AbState_t first{};
};

static float AbBounce(const Vec3& xy, float vzHit, const Vec3& n, const AbWorld_t& W)
{
    return ab::Bounce(xy.x, xy.y, vzHit, n.x, n.y, n.z, W.w.maxSpeed);
}

static Vec3 AbGlideXY(const Vec3& origin, const Vec3& at, float ttc, const Vec3& n)
{
    const Vec3 to(at.x - origin.x, at.y - origin.y, 0.f);
    if (!ab::Glides(to.x, to.y, to.Length2D(), n.x, n.y)) return Vec3();
    const float t = std::max(ttc, AbDt());
    return Vec3(to.x / t, to.y / t, 0.f);
}

static Vec3 AbKeepVelocity(const Vec3& v, const Vec3& n)
{
    Vec3 o; ab::KeepVelocity(v.x, v.y, n.x, n.y, o.x, o.y);
    return o;
}

static float AbSideDeg(const Vec3& out, const Vec3& n) { return ab::SideDeg(out.x, out.y, n.x, n.y); }

static void AbTraceHull(const Vec3& a, const Vec3& b, const Vec3& mins, const Vec3& maxs, GameTrace_t& tr)
{
    TraceFilterWorldProps f; f.Setup(s_abLocal);
    tr = GameTrace_t{};
    TraceHull(a, b, mins, maxs, MASK_PLAYERSOLID, &f, &tr);
}
static void AbTraceRay(const Vec3& a, const Vec3& b, GameTrace_t& tr)
{
    TraceFilterWorldProps f; f.Setup(s_abLocal);
    tr = GameTrace_t{};
    TraceLine(a, b, MASK_PLAYERSOLID, &f, &tr);
}

// fly ends at contact: a wall / crease right past it eats the slide. fraction of the exit run the hull gets (1 = clear)
static float AbExitRoom(const GameTrace_t& hit, const Vec3& out, const Vec3& mins, const Vec3& maxs)
{
    const float spd = out.Length2D();
    if (spd < 1.f) return 1.f;
    const float len = std::max(spd * abk::kExitTime, abk::kExitMin);
    const Vec3 from(hit.endpos.x, hit.endpos.y, hit.endpos.z + 0.125f);
    GameTrace_t tr;
    AbTraceHull(from, Vec3(from.x + out.x / spd * len, from.y + out.y / spd * len, from.z), mins, maxs, tr);
    if (tr.startsolid) return 0.f;
    return tr.fraction < 1.f && tr.plane.normal.z < ab::kWalkableNz ? tr.fraction : 1.f;
}

static AbHit_t AbMakeHit(const GameTrace_t& tr)
{
    const float nz = std::clamp(tr.plane.normal.z, -1.f, 1.f);
    AbHit_t h;
    h.pos    = tr.endpos;
    h.normal = tr.plane.normal;
    h.angle  = RAD2DEGF(acosf(nz));
    h.shape  = nz * sqrtf(std::max(1.f - nz * nz, 0.f));
    return h;
}

// down ray: z of what it lands on (miss = bottom), flat = may straddle a lip with a neighbour
struct AbRay_t { Vec3 start{}; float z = 0.f; bool flat = false; };

static AbRay_t AbRayDown(const Vec3& start, float depth, GameTrace_t& tr)
{
    AbTraceRay(start, Vec3(start.x, start.y, start.z - depth), tr);
    AbRay_t r{ start, start.z - depth, true };
    if (tr.fraction <= 0.f) { r.z = start.z; r.flat = false; }
    else if (tr.fraction < 1.f) { r.z = tr.endpos.z; r.flat = tr.plane.normal.z >= 0.99f; }
    return r;
}

// ledge lip: two flat neighbours at different heights straddle an edge. halve toward it, a bevel answers sloped
static bool AbRefine(AbRay_t a, AbRay_t b, float depth, AbHit_t& out)
{
    for (int i = 0; i < abk::kRefineSteps; ++i) {
        GameTrace_t tr;
        const AbRay_t m = AbRayDown((a.start + b.start) * 0.5f, depth, tr);
        if (tr.fraction > 0.f && tr.fraction < 1.f && tr.plane.normal.z > 0.f && tr.plane.normal.z < 0.99f) {
            out = AbMakeHit(tr);
            return true;
        }
        (fabsf(m.z - a.z) < fabsf(m.z - b.z) ? a : b) = m;
    }
    return false;
}

static bool AbLip(const AbRay_t& a, const AbRay_t& b) { return a.flat && b.flat && fabsf(a.z - b.z) > abk::kRefineDz; }

// lumi FUN_1014a700 1:1 (botox auto_bounce.cpp ab_lumi): ring of down rays, closest slope in the angle window = target,
// steer 450 at it blended with a brake by smooth, <= 30 u/s = zero input + 1s hands off, duck < 32u over it.
// mode==3 only, pending menu (see report MENU): sliders not wired yet, using botox's own slider defaults as constants.
namespace abk {
    // promoted to real vars (movement.h) per botox variables.h -- menu/config unit wires the
    // sliders (see report MENU/CONFIG): rays 4-64, radius 5-100, depth 50-4096 log, min angle
    // 5-60, max angle 20-90, smooth 0-1.
    constexpr float kLumiStopSpeed = 30.f;
    constexpr float kLumiHoldTime  = 1.f;
    constexpr float kLumiDuckOver  = 32.f;
}
// MASK_PLAYERSOLID_BRUSHONLY (bspflags.h): SOLID|MOVEABLE|WINDOW|PLAYERCLIP|GRATE, no CONTENTS_MONSTER -- skips players/npcs
static constexpr unsigned kMaskPlayerSolidBrushonly = 0x1 | 0x4000 | 0x2 | 0x10000 | 0x8;

static void AbLumi(UserCmd_t* cmd, AbData_t& data, const Vec3& origin, const Vec3& velocity, bool crouch)
{
    const float now = g_globalVars ? g_globalVars->curtime : 0.f;
    if (data.lumiHoldUntil > 0.f) {
        if (now < data.lumiHoldUntil) return;
        data.lumiHoldUntil = 0.f;
    }

    TraceFilterWorldProps f; f.Setup(s_abLocal);
    float bestFrac = 1.f;
    AbHit_t best{};
    bool found = false;
    const int rays = std::clamp(g_abLumiRays, 4, 64);
    for (int i = 0; i < rays; ++i) {
        const float a = (float)i / (float)rays * 6.2831853f;
        const Vec3 start(origin.x + cosf(a) * g_abLumiRadius, origin.y + sinf(a) * g_abLumiRadius, origin.z + 2.f);
        GameTrace_t tr{};
        TraceLine(start, Vec3(start.x, start.y, start.z - g_abLumiDepth), kMaskPlayerSolidBrushonly, &f, &tr);
        if (tr.fraction <= 0.f || tr.fraction >= 1.f) continue;
        const float angle = RAD2DEGF(acosf(std::clamp(tr.plane.normal.z, -1.f, 1.f)));
        if (angle < g_abLumiMinAngle || angle > g_abLumiMaxAngle || tr.fraction >= bestFrac) continue;
        bestFrac = tr.fraction;
        best     = AbMakeHit(tr);
        found    = true;
    }
    if (!found) return;

    data.found    = true;
    data.ownsCmd  = true;
    data.jumpOwed = true;

    const float speed = velocity.Length2D();
    if (speed <= abk::kLumiStopSpeed) {
        data.lumiHoldUntil = now + abk::kLumiHoldTime;
        cmd->forwardmove = cmd->sidemove = cmd->upmove = 0.f;
        DebugLog("AB: LUMI STOP ang=%.1f drop=%.1f spd=%.0f", best.angle, origin.z - best.pos.z, speed);
        return;
    }

    const float viewYaw = cmd->viewangles.y;
    float brakeF = 0.f, brakeS = 0.f;
    if (speed > 1.f) {
        const float db = DEG2RADF(AbYaw(velocity) - viewYaw);
        brakeF = -cosf(db);
        brakeS = sinf(db);
    }

    const Vec3 to(best.pos.x - origin.x, best.pos.y - origin.y, 0.f);
    const float dist = to.Length2D();
    cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    if (dist <= 1.f) {
        cmd->forwardmove = brakeF * 450.f;
        cmd->sidemove    = brakeS * 450.f;
    } else {
        const float d  = DEG2RADF(AbYaw(to) - viewYaw);
        const float smooth = std::clamp(g_abLumiSmooth, 0.f, 1.f);
        const float f2 = smooth * smooth;
        cmd->forwardmove = cosf(d) * 450.f * f2 + brakeF * 450.f * (1.f - smooth);
        cmd->sidemove    = -sinf(d) * 450.f * f2 + brakeS * 450.f * (1.f - smooth);
    }

    const float over = origin.z - best.pos.z;
    if (crouch && over > 0.f && over < abk::kLumiDuckOver) cmd->buttons |= IN_DUCK;

    DebugLog("AB: LUMI ang=%.1f drop=%.1f dist=%.1f spd=%.0f vz=%.0f fwd=%.0f side=%.0f%s", best.angle, over, dist, speed,
             velocity.z, cmd->forwardmove, cmd->sidemove, (cmd->buttons & IN_DUCK) ? " duck" : "");
}

static bool AbScanBelow(const Vec3& at, float radius, int rays, float depth, AbHit_t& out, Vec3* patchAvg, const AsClock* clock)
{
    constexpr float twoPi  = 6.28318530717958647692f;
    constexpr int kMaxRays = 64;

    AbHit_t hits[kMaxRays + abk::kRefineUnder];
    AbRay_t starts[kMaxRays];
    int count = 0;
    float topZ = -1e9f;

    const int ring  = std::clamp(rays, 1, kMaxRays - 1 - abk::kScanInnerRays);
    const int total = 1 + abk::kScanInnerRays + ring;
    for (int i = 0; i < total; ++i) {
        Vec3 start = at;
        start.z += 2.f;
        if (i > 0) {
            const bool inner = i <= abk::kScanInnerRays;
            const int n      = inner ? abk::kScanInnerRays : ring;
            const int j      = inner ? i - 1 : i - 1 - abk::kScanInnerRays;
            const float r    = inner ? abk::kScanInnerR : radius;
            const float ang  = (float)j / (float)n * twoPi;
            start.x += cosf(ang) * r;
            start.y += sinf(ang) * r;
        }
        GameTrace_t tr;
        starts[i] = AbRayDown(start, depth, tr);
        if (tr.fraction >= 1.f || tr.fraction <= 0.f || tr.plane.normal.z <= 0.f) continue;
        hits[count++] = AbMakeHit(tr);
        topZ = std::max(topZ, tr.endpos.z);
    }

    // centre-inner pairs first (a lip right under the hull), then each ring's neighbours
    int nRef = 0;
    const auto pair = [&](int i, int j) {
        if (nRef >= abk::kRefineUnder || !AbLip(starts[i], starts[j]) || (clock && clock->Expired())) return;
        ++nRef;
        if (AbRefine(starts[i], starts[j], depth, hits[count])) {
            topZ = std::max(topZ, hits[count].pos.z);
            ++count;
        }
    };
    for (int j = 0; j < abk::kScanInnerRays; ++j) pair(0, 1 + j);
    for (int j = 0; j < abk::kScanInnerRays; ++j) pair(1 + j, 1 + (j + 1) % abk::kScanInnerRays);
    for (int j = 0; j < ring; ++j) pair(1 + abk::kScanInnerRays + j, 1 + abk::kScanInnerRays + (j + 1) % ring);

    if (count <= 0) return false;

    int best = -1;
    for (int i = 0; i < count; ++i)
        if (hits[i].pos.z >= topZ - abk::kFaceGroupZ && (best < 0 || hits[i].shape > hits[best].shape))
            best = i;
    if (best < 0) return false;
    out = hits[best];

    if (patchAvg) {
        Vec3 sum; int n = 0;
        for (int i = 0; i < count; ++i)
            if (hits[i].pos.z >= topZ - abk::kFaceGroupZ && hits[i].normal.Dot(out.normal) >= abk::kSameFace) { sum += hits[i].pos; ++n; }
        *patchAvg = AbDiv(sum, (float)n);
    }
    return true;
}

static bool AbHullDown(const Vec3& from, float depth, const Vec3& mins, const Vec3& maxs, AbContact_t& out)
{
    GameTrace_t tr;
    AbTraceHull(from, Vec3(from.x, from.y, from.z - depth), mins, maxs, tr);
    if (tr.startsolid || tr.fraction <= 0.f || tr.fraction >= 1.f || tr.plane.normal.z <= 0.f) return false;
    out.pos = tr.endpos; out.normal = tr.plane.normal;
    return true;
}

static bool AbSweepFall(const Vec3& origin, const Vec3& xy, float vz, float depth, const Vec3& mins, const Vec3& maxs,
                        const AsClock& clock, AbContact_t& out)
{
    const float g = AbGrav();
    Vec3 prev = origin;
    for (float t = abk::kChordTime; t < abk::kSweepTime + 0.001f; t += abk::kChordTime) {
        if (clock.Expired()) return false;
        const Vec3 p(origin.x + xy.x * t, origin.y + xy.y * t, origin.z + vz * t - 0.5f * g * t * t);
        GameTrace_t tr;
        AbTraceHull(prev, p, mins, maxs, tr);
        if (tr.startsolid) return false;
        if (tr.fraction < 1.f) {
            if (tr.fraction <= 0.f || tr.plane.normal.z <= 0.f) return false;
            out.pos = tr.endpos; out.normal = tr.plane.normal;
            out.time = t - abk::kChordTime * (1.f - tr.fraction);
            return true;
        }
        if (origin.z - p.z > depth) return false;
        prev = p;
    }
    return false;
}

// align: a landing spot the hull can't fit at (wall in reach) slides flush along the wall it hits flying there level
static bool AbFlushToWall(const Vec3& origin, const Vec3& patch, const Vec3& mins, const Vec3& maxs, Vec3& out)
{
    const Vec3 to(patch.x, patch.y, origin.z);
    GameTrace_t tr;
    AbTraceHull(origin, to, mins, maxs, tr);
    if (tr.startsolid || tr.fraction >= 1.f || fabsf(tr.plane.normal.z) >= abk::kWallNz) return false;
    const Vec3& n = tr.plane.normal;
    const Vec3 left(to.x - tr.endpos.x, to.y - tr.endpos.y, 0.f);
    const float into = left.x * n.x + left.y * n.y;
    const Vec3 start(tr.endpos.x + n.x * 0.03125f, tr.endpos.y + n.y * 0.03125f, tr.endpos.z);
    GameTrace_t slide;
    AbTraceHull(start, Vec3(start.x + left.x - n.x * into, start.y + left.y - n.y * into, start.z), mins, maxs, slide);
    if (slide.startsolid) return false;
    out = slide.endpos;
    return true;
}

static bool AbPlanStopAt(const Vec3& origin, const Vec3& velocity, const Vec3& patch, float probe, float need, float maxTtc,
                         const AbWorld_t& W, AbPlan_t& out, bool gain = false)
{
    AbContact_t c;
    Vec3 flush;
    if (!AbHullDown(Vec3(patch.x, patch.y, origin.z), probe, W.mins, W.maxs, c)
        && !(s_abAlign && AbFlushToWall(origin, patch, W.mins, W.maxs, flush)
             && AbHullDown(flush, probe + (origin.z - flush.z), W.mins, W.maxs, c)))
        return false;

    out.keep   = false;
    out.target = c.pos;
    out.normal = c.normal;
    out.drop   = std::max(origin.z - c.pos.z, 0.f);
    out.vzHit  = sqrtf(velocity.z * velocity.z + 2.f * AbGrav() * out.drop);
    out.ttc    = ab::TimeToContact(velocity.z, out.drop, AbGrav());
    out.floor  = AbBounce(Vec3(), out.vzHit, c.normal, W);
    out.result = gain ? AbBounce(AbGlideXY(origin, c.pos, out.ttc, c.normal), out.vzHit, c.normal, W) : out.floor;

    const Vec3 to(c.pos.x - origin.x, c.pos.y - origin.y, 0.f);
    const float dist   = to.Length2D();
    const float toward = dist > 1.f ? std::max((velocity.x * to.x + velocity.y * to.y) / dist, 0.f) : 0.f;
    const bool reachable = dist <= (toward + abk::kAirVeerCap) * out.ttc + 1.f
                        || (gain && dist <= ab::GainReach(velocity.Length2D(), out.ttc, W.w));
    out.valid = out.result >= need && out.ttc <= maxTtc && reachable;
    return out.valid;
}

static float AbRayScore(const Vec3& origin, const Vec3& velocity, const AbHit_t& hit, bool glide, const AbWorld_t& W)
{
    const float rayDrop = std::max(origin.z - hit.pos.z, 0.f);
    const float vzHit   = sqrtf(velocity.z * velocity.z + 2.f * AbGrav() * rayDrop);
    const Vec3 xy = glide ? AbGlideXY(origin, hit.pos, ab::TimeToContact(velocity.z, rayDrop, AbGrav()), hit.normal) : Vec3();
    return AbBounce(xy, vzHit, hit.normal, W);
}

static bool AbPlanStop(const Vec3& origin, const Vec3& velocity, const AbHit_t& hit, const Vec3& patch, float need, float maxTtc,
                       const AbWorld_t& W, AbPlan_t& out, bool gain = false)
{
    out.result = AbRayScore(origin, velocity, hit, gain, W);
    if (out.result < need) return false;

    const float rayDrop = std::max(origin.z - hit.pos.z, 0.f);
    const float flat    = sqrtf(hit.normal.x * hit.normal.x + hit.normal.y * hit.normal.y);

    if (flat >= 0.01f) {
        constexpr int n = (int)(sizeof(abk::kSupportOffsets) / sizeof(abk::kSupportOffsets[0]));
        const Vec3 up(-hit.normal.x / flat, -hit.normal.y / flat, 0.f);
        const float support = W.maxs.x * fabsf(up.x) + W.maxs.y * fabsf(up.y);
        const float probe = rayDrop + 64.f + (support - abk::kSupportOffsets[n - 1]) * flat / std::max(hit.normal.z, 0.1f);

        const auto planAt = [&](float k, AbPlan_t& p) {
            const float back = support - k;
            const Vec3 at(hit.pos.x - up.x * back, hit.pos.y - up.y * back, hit.pos.z);
            AbPlanStopAt(origin, velocity, at, probe, need, maxTtc, W, p, gain);
            p.place = k;
            return p.normal.Dot(hit.normal) >= abk::kSameFace;
        };

        AbPlan_t plans[n];
        bool onFace[n];
        for (int i = 0; i < n; ++i) onFace[i] = planAt(abk::kSupportOffsets[i], plans[i]);

        // lowest 3u slot whose neighbours land on the face too (room for a stop error)
        constexpr int nc = (int)(sizeof(abk::kSupportCoarse) / sizeof(abk::kSupportCoarse[0]));
        for (int j = nc - 2; j >= 1; --j) {
            const int i = abk::kSupportCoarse[j];
            if (plans[i].valid && onFace[abk::kSupportCoarse[j - 1]] && onFace[abk::kSupportCoarse[j + 1]]) {
                out = plans[i];
                return true;
            }
        }

        // thin face (ledge lip): middle of the widest valid run, its lowest slot if the middle misses
        int lo = -1, hi = -1;
        for (int i = 0; i < n;) {
            if (!plans[i].valid) { ++i; continue; }
            int j = i;
            while (j + 1 < n && plans[j + 1].valid) ++j;
            if (lo < 0 || j - i > hi - lo) { lo = i; hi = j; }
            i = j + 1;
        }
        if (lo >= 0) {
            AbPlan_t mid;
            out = hi > lo && planAt((abk::kSupportOffsets[lo] + abk::kSupportOffsets[hi]) * 0.5f, mid) && mid.valid ? mid : plans[hi];
            return true;
        }
    }
    return AbPlanStopAt(origin, velocity, patch, rayDrop + 64.f, need, maxTtc, W, out, gain);
}

// drop in: rings beside us, each ray hit its own patch. hull must fly there level (no patch through a wall).
// best analytic first, first whose flight pays (accept) wins
template <class A>
static bool AbPlanWide(const Vec3& origin, const Vec3& velocity, float need, float reach, const AbWorld_t& W,
                       const AsClock& clock, A&& accept, AbPlan_t& out)
{
    constexpr float twoPi = 6.28318530717958647692f;
    constexpr int kRings  = (int)(sizeof(abk::kWideRings) / sizeof(abk::kWideRings[0]));

    struct Cand_t { AbHit_t hit; float score; };
    static Cand_t cands[abk::kWideMax + abk::kRefineWide];
    int count = 0, nRef = 0;

    const auto offer = [&](const AbHit_t& hit) {
        const float score = AbRayScore(origin, velocity, hit, true, W);
        if (score >= need) cands[count++] = { hit, score };
    };

    for (int ri = 0; ri < kRings && abk::kWideRings[ri] <= reach; ++ri) {
        const float r  = abk::kWideRings[ri];
        const int rays = abk::kWideRingRays[ri];
        AbRay_t ring[24];
        int cast = 0;
        for (; cast < rays && !clock.Expired(); ++cast) {
            const float ang = ((float)cast + ((ri & 1) ? 0.5f : 0.f)) / (float)rays * twoPi;
            const Vec3 start(origin.x + cosf(ang) * r, origin.y + sinf(ang) * r, origin.z + 2.f);
            GameTrace_t tr;
            ring[cast] = AbRayDown(start, abk::kScanDepth, tr);
            const float nz = tr.plane.normal.z;
            if (tr.fraction >= 1.f || tr.fraction <= 0.f || nz <= 0.f || nz >= 0.99f) continue;
            offer(AbMakeHit(tr));
        }
        for (int j = 0; j < rays && nRef < abk::kRefineWide && cast == rays && !clock.Expired(); ++j) {
            const int k = (j + 1) % rays;
            if (!AbLip(ring[j], ring[k])) continue;
            ++nRef;
            AbHit_t hit;
            if (AbRefine(ring[j], ring[k], abk::kScanDepth, hit)) offer(hit);
        }
    }

    std::sort(cands, cands + count, [](const Cand_t& a, const Cand_t& b) { return a.score > b.score; });

    for (int i = 0; i < count && !clock.Expired(); ++i) {
        AbPlan_t cand;
        if (!AbPlanStop(origin, velocity, cands[i].hit, cands[i].hit.pos, need, 1e9f, W, cand, true)) continue;
        GameTrace_t wall;
        AbTraceHull(origin, Vec3(cand.target.x, cand.target.y, origin.z), W.mins, W.maxs, wall);
        const bool slideOk = s_abAlign && !wall.startsolid && fabsf(wall.plane.normal.z) < abk::kWallNz;
        if (wall.startsolid || (wall.fraction < 1.f && !slideOk) || !accept(cand)) continue;
        out = cand;
        out.lookahead = -1;
        return true;
    }
    return false;
}

// keep: sweep current xy, strip the into-face part, re-sweep (must meet the same face).
// contact under a tick out = no plan (would arm on a slide already riding)
static bool AbPlanKeep(const Vec3& origin, const Vec3& velocity, float need, float depth, const AbWorld_t& W,
                       const AsClock& clock, AbPlan_t& out)
{
    Vec3 xy(velocity.x, velocity.y, 0.f);
    AbContact_t c;
    if (!AbSweepFall(origin, xy, velocity.z, depth, W.mins, W.maxs, clock, c) || c.time < AbDt()) return false;

    const Vec3 kept = AbKeepVelocity(xy, c.normal);
    if ((kept - xy).Length2D() > 1.f) {
        const Vec3 first = c.normal;
        xy = kept;
        if (!AbSweepFall(origin, xy, velocity.z, depth, W.mins, W.maxs, clock, c) || c.time < AbDt()
            || c.normal.Dot(first) < abk::kSameFace)
            return false;
    }

    const float g     = AbGrav();
    const float drop  = origin.z - c.pos.z;
    const float vzMag = sqrtf(std::max(velocity.z * velocity.z + 2.f * g * drop, 0.f));
    const float vzHit = velocity.z - g * c.time < 0.f ? -vzMag : vzMag;

    out.keep   = true;
    out.target = c.pos;
    out.normal = c.normal;
    out.drop   = drop;
    out.vzHit  = vzMag;
    out.ttc    = c.time;
    out.result = AbKeep(ab::ClipXY(xy.x, xy.y, vzHit, c.normal.x, c.normal.y, c.normal.z), c.normal.z, W);
    out.floor  = out.result;
    out.valid  = out.result >= need;
    return out.valid;
}

// botox ab_steer: one tick of exact air wish toward `want`, silent (view kept)
static void AbSteer(UserCmd_t* cmd, const Vec3& velocity, const Vec3& want, const AbWorld_t& W)
{
    const Vec3 delta(want.x - velocity.x, want.y - velocity.y, 0.f);
    const float need = delta.Length2D();
    cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    if (need > 1.f) {
        const float rot  = DEG2RADF(AbYaw(delta) - cmd->viewangles.y);
        const float wish = ab::ExactWish(need, W.w);
        cmd->forwardmove = cosf(rot) * wish;
        cmd->sidemove    = -sinf(rot) * wish;
    } else {
        cmd->forwardmove = 0.f;
        cmd->sidemove    = 0.f;
    }
}

static Vec3 AbStopWant(const Vec3& origin, const Vec3& velocity, const Vec3& target, const Vec3& normal, bool dropIn,
                       const AbWorld_t& W, float* outDist = nullptr, float* outWant = nullptr)
{
    const Vec3 toT(target.x - origin.x, target.y - origin.y, 0.f);
    const float dist = toT.Length2D();
    const float ttc  = ab::TimeToContact(velocity.z, std::max(origin.z - target.z, 0.f), AbGrav());

    Vec3 want;
    float wantSpeed = 0.f;
    if (dist > 0.5f) {
        const bool glide    = dropIn && ab::Glides(toT.x, toT.y, dist, normal.x, normal.y);
        const float arrive  = glide ? std::max(ttc, AbDt()) : dropIn ? AbDt() : std::max(ttc * abk::kArriveFrac, AbDt());
        const float profile = std::min(dist / arrive, ab::StopSpeed(dist, W.w));
        const float speed   = velocity.Length2D();
        wantSpeed = std::min(profile, speed + abk::kAirVeerCap);
        want = Vec3(toT.x / dist * wantSpeed, toT.y / dist * wantSpeed, 0.f);

        // aimed + under the profile: straight adds nothing past 30, a perp push grows |v|^2 by a^2 a tick
        const float perpGain = std::min(ab::AccelSpeed(W.w), abk::kAirVeerCap);
        if (dropIn && speed >= 28.f && profile >= sqrtf(speed * speed + perpGain * perpGain)
            && velocity.x * toT.x + velocity.y * toT.y >= 0.94f * speed * dist) {
            const float cross = velocity.x * toT.y - velocity.y * toT.x;
            const Vec3 perp = cross >= 0.f ? Vec3(-velocity.y / speed, velocity.x / speed, 0.f)
                                           : Vec3(velocity.y / speed, -velocity.x / speed, 0.f);
            want = Vec3(velocity.x + perp.x * 450.f, velocity.y + perp.y * 450.f, 0.f);
            wantSpeed = want.Length2D();
        }
    }
    if (outDist) *outDist = dist;
    if (outWant) *outWant = wantSpeed;
    return want;
}

// n_air::fly: tick-by-tick air flight (exact AirAccelerate cap), hull chord traces, ground snap, wall slides
template <class P>
static AbResult_t AbFlyRaw(AbState_t s, const AbWorld_t& W, float maxTime, float coastChord, float deadband,
                           const AsClock* clock, P&& policy)
{
    AbResult_t r;
    const float dt = W.w.dt;
    const float h  = W.w.g * dt * 0.5f;
    const int maxTicks   = std::max((int)(maxTime / dt + 0.5f), 1);
    const int chordTicks = std::max((int)(coastChord / dt + 0.5f), 1);

    for (int k = 0; k < maxTicks;) {
        if (clock && clock->Expired()) return r;

        Vec3 v = s.velocity;
        Vec3 want;
        bool wrote = false;
        s.k = k;
        if (policy(s, want)) {
            ab::AirStep(v.x, v.y, want.x, want.y, deadband, W.w, wrote);
            if (wrote) ++r.wrote;
        }

        const int n     = (k == 0 || wrote || W.snap > 0.f) ? 1 : std::min(chordTicks, maxTicks - k);
        const float T   = (float)n * dt;
        const float vz0 = v.z;
        Vec3 end(s.origin.x + v.x * T, s.origin.y + v.y * T, s.origin.z + vz0 * T - h * dt * (float)(n * n));

        // wall_slides: TryPlayerMove's bump. the chord's rest runs on from the wall along the clipped xy, z keeps its fall
        Vec3 from = s.origin;
        float done = 0.f;
        for (;;) {
            GameTrace_t tr;
            AbTraceHull(from, end, W.mins, W.maxs, tr);
            if (tr.startsolid || tr.allsolid) return r;
            if (tr.fraction >= 1.f) break;

            const Vec3& wn = tr.plane.normal;
            const float at = done + (1.f - done) * tr.fraction;
            if (r.slides < W.wallSlides && fabsf(wn.z) < 0.1f) {
                const float into = std::min(v.x * wn.x + v.y * wn.y, 0.f);
                v.x -= wn.x * into;
                v.y -= wn.y * into;
                ++r.slides;
                done = at;
                from = Vec3(tr.endpos.x + wn.x * 0.03125f, tr.endpos.y + wn.y * 0.03125f, tr.endpos.z);
                end  = Vec3(from.x + v.x * T * (1.f - done), from.y + v.y * T * (1.f - done), end.z);
                continue;
            }

            const int i = std::min((int)(at * (float)n), n - 1);
            r.valid = r.hit = true;
            r.ticks = k + i;
            r.in    = Vec3(v.x, v.y, vz0 - 2.f * h * (float)i - h);
            r.out   = r.in - tr.plane.normal * r.in.Dot(tr.plane.normal);
            r.trace = tr;
            return r;
        }

        const float vz1 = vz0 - 2.f * h * (float)n;
        if (W.snap > 0.f && vz1 <= ab::kNonJumpVz) {
            GameTrace_t gt;
            AbTraceHull(end, Vec3(end.x, end.y, end.z - W.snap), W.mins, W.maxs, gt);
            if (!gt.startsolid && gt.fraction < 1.f && gt.plane.normal.z >= ab::kWalkableNz) {
                r.valid = r.hit = r.snapped = true;
                r.ticks = k + n - 1;
                r.in    = Vec3(v.x, v.y, vz1);
                r.out   = Vec3(v.x, v.y, 0.f);
                r.trace = gt;
                return r;
            }
        }

        s.origin   = end;
        s.velocity = Vec3(v.x, v.y, vz1);
        if (k == 0) r.first = s;
        k += n;
    }
    r.valid = true;
    return r;
}

static AbResult_t AbFly(const AbState_t& from, const AbWorld_t& W, const AbPlan_t& plan, bool dropIn, const AsClock* clock,
                        Vec3* path = nullptr, int* pathN = nullptr)
{
    const float horizon = std::min(plan.ttc + abk::kFlySlack, abk::kFlyMax);
    return AbFlyRaw(from, W, horizon, abk::kCoastChord, 1.f, clock, [&](const AbState_t& s, Vec3& want) {
        if (path && *pathN < abk::kVizMax) path[(*pathN)++] = s.origin;
        want = plan.keep ? AbKeepVelocity(s.velocity, plan.normal)
                         : AbStopWant(s.origin, s.velocity, plan.target, plan.normal, dropIn, W);
        return true;
    });
}

static float AbFlySpeed(const AbResult_t& r, const AbWorld_t& W)
{
    if (!r.valid || !r.hit) return -1.f;
    const float nz = r.trace.plane.normal.z;
    return nz > 0.f ? AbKeep(r.out.Length2D(), nz, W) : -1.f;
}

// gates: once a second, why nothing armed (botox ab_gates_report)
struct AbGates_t
{
    int yield = 0, off = 0, ground = 0, settle = 0, noscan = 0, noplan = 0, refused = 0, armed = 0, hold = 0, side = 0, blk = 0, snap = 0;
    float best = -1.f, need = 0.f, spd = 0.f, ang = -1.f, rang = -1.f;
    unsigned long long next = 0ull;
};
static AbGates_t s_abGates;

static void AbGatesReport()
{
    const unsigned long long now = GetTickCount64();
    if (now < s_abGates.next) return;
    auto& g = s_abGates;
    // botox gates this on m_debug_log; DebugLog always writes here, so idle counters (off/ground/yield) never trigger a line alone
    if (g.settle || g.noscan || g.noplan || g.refused || g.armed || g.hold)
        DebugLog("AB: gates yield=%d off=%d ground=%d settle=%d noscan=%d noplan=%d refused=%d side=%d blk=%d snap=%d armed=%d hold=%d best=%.0f need=%.0f spd=%.0f ang=%.1f rang=%.1f",
                 g.yield, g.off, g.ground, g.settle, g.noscan, g.noplan, g.refused, g.side, g.blk, g.snap, g.armed, g.hold, g.best, g.need, g.spd, g.ang, g.rang);
    g = AbGates_t{};
    g.next = now + 1000ull;
}

struct AbVerify_t
{
    bool engine = false;
    float pred = -1.f;
    int ticks = -1;
    float err = -1.f;
    bool land = false;
    Vec3 landPos{}, landNormal{};
};

// botox n_air::engine_tick + ab_verify: the edited cmd through the real engine, then fly the rest.
// Leaves the entity on the edited cmd's end state (Pred_End puts the cmd-start backup back anyway).
static AbVerify_t AbVerify(void* pLocal, const UserCmd_t* cmd, const AbState_t& start, const AbWorld_t& W, const AbPlan_t& plan,
                           bool dropIn, const AsClock& clock, Vec3* path, int* pathN)
{
    AbVerify_t v;
    if (!g_frameReady || !cmd) return v;

    RestoreToFrame(PredCommandsPredicted() - 1);
    AsSetOrigin(pLocal, start.origin);
    EntSetV3(pLocal, g_offVelocity, start.velocity);
    UserCmd_t sim = *cmd;
    Pred_Simulate(pLocal, &sim);
    AbState_t end;
    end.origin   = EntV3(pLocal, g_offOrigin);
    end.velocity = EntV3(pLocal, g_offVelocity);
    const bool ground  = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    const bool contact = ground || end.velocity.z > start.velocity.z - W.w.g * W.w.dt + 1.f;

    v.engine = true;
    if (plan.firstOk) v.err = (end.origin - plan.first.origin).Length();
    if (path && *pathN < abk::kVizMax) path[(*pathN)++] = start.origin;

    if (contact) {
        v.pred = AbKeep(end.velocity.Length2D(), plan.normal.z, W);
        v.ticks = 0;
        v.land = true;
        v.landPos = end.origin;
        v.landNormal = plan.normal;
        if (path && *pathN < abk::kVizMax) path[(*pathN)++] = end.origin;
        return v;
    }

    const AbResult_t r = AbFly(end, W, plan, dropIn, &clock, path, pathN);
    if (r.hit) {
        v.land = true;
        v.landPos = r.trace.endpos;
        v.landNormal = r.trace.plane.normal;
        if (path && *pathN < abk::kVizMax) path[(*pathN)++] = r.trace.endpos;
    }
    if (r.valid) {
        v.pred = AbFlySpeed(r, W);
        v.ticks = r.ticks + 1;
    }
    return v;
}

static void AutoBounceRun(void* pLocal, UserCmd_t* cmd, bool yield)
{
    auto& data = s_ab;
    AbGatesReport();
    if (yield) {
        if (g_autoBounce) ++s_abGates.yield;
        data.ownsCmd = false;
        return;
    }

    data.ownsCmd = false;
    data.found   = false;

    if (!cmd || !pLocal || !EntAlive(pLocal)) { AbVizClear(); return; }
    if (!On(g_autoBounce, g_autoBounceKey, g_autoBounceMode)) {
        if (g_autoBounce) ++s_abGates.off;
        data.Reset();
        AbVizClear();
        return;
    }
    if (EntMoveType(pLocal) != MOVETYPE_WALK) { data.Reset(); AbVizClear(); return; }

    s_abLocal = pLocal;
    const bool groundStart = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool groundLive  = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    const bool jumpOn = g_abJump;
    const bool crouch = g_abCrouch;

    if (groundStart) {
        ++s_abGates.ground;
        if (data.jumpOwed && jumpOn) {
            cmd->buttons |= IN_JUMP;
            if (crouch) cmd->buttons |= IN_DUCK;
            DebugLog("AB: JUMP spd=%.0f pred=%.0f entry=%.0f sd=%.1f%s", s_prePredVel.Length2D(), data.result,
                     data.entrySpeed, AbSideDeg(s_prePredVel, data.normal), crouch ? " CROUCH" : "");
        }
        data.Reset();
        AbVizClear();
        return;
    }

    // css CheckJumpButton needs the key up the tick before the landing press -- same pogo-stick
    // gate as TF2 (mv->m_nOldButtons & IN_JUMP, cs_gamemovement.cpp:640-641, verbatim)
    if (data.jumpOwed && jumpOn) cmd->buttons &= ~IN_JUMP;
    const bool lumi = g_abMode == 3;
    // air duck = feet up 8.5 at once: more fall onto the face, more vz to turn into speed. arm to ground only
    // (lumi has its own duck-over-target gate, not an unconditional hold -- excluded here)
    if (crouch && data.jumpOwed && !lumi) cmd->buttons |= IN_DUCK;

    if (groundLive) return;

    const Vec3 mins = EntV3(pLocal, g_offMins);
    Vec3 maxs       = EntV3(pLocal, g_offMaxs);
    Vec3 origin     = s_prePredOrigin;
    const Vec3 velocity = s_prePredVel;
    if (lumi) { AbLumi(cmd, data, origin, velocity, crouch); return; }
    // the duck lands inside the arming cmd's tick, before the move: plan from the ducked hull already
    if (crouch && !(s_prePredFlags & FL_DUCKING)) {
        origin.z += abk::kDuckLift;
        maxs.z = abk::kDuckHullZ;
    }
    const float speed2d = velocity.Length2D();

    // contact watch, cmd start to cmd start: a steep-face clip never grounds, so this is what ends the
    // arm. gaps owned by TB/AS/... are bridged with that many ticks of gravity
    const float prevVz    = data.lastVz;
    const float prevSpeed = data.lastSpeed;
    const int gap         = cmd->command_number - data.lastCmd;
    const bool chained    = data.lastCmd != 0 && gap >= 1 && gap <= abk::kSettleCmds;
    data.lastCmd   = cmd->command_number;
    data.lastVz    = velocity.z;
    data.lastSpeed = speed2d;

    if (chained && velocity.z > prevVz - AbGrav() * AbDt() * (float)gap + abk::kClipVzEps) {
        if (data.armedTick > 0)
            DebugLog("AB: CONTACT plan=%s spd=%.0f->%.0f predicted=%.0f entry=%.0f vz=%.0f->%.0f", data.keep ? "keep" : "stop",
                     prevSpeed, speed2d, data.result, data.entrySpeed, prevVz, velocity.z);
        data.Disarm();
        data.clipCmd = cmd->command_number;
        ++s_abGates.settle;
        AbVizClear();
        return;
    }
    if (data.clipCmd != 0 && cmd->command_number - data.clipCmd <= abk::kSettleCmds) { ++s_abGates.settle; return; }

    if (data.armedTick > 0 && cmd->tick_count - data.armedTick > abk::kArmWindow) {
        data.Disarm();
        data.jumpOwed = false;
    }
    const bool armed = data.armedTick > 0;

    // gain over your own xy (clarity)
    const float entry   = armed ? data.entrySpeed : speed2d;
    const float base    = entry;
    const float gainMin = std::max(g_abMinGain, 1.f);
    const float need    = base + gainMin;

    const float radius = abk::kScanRadius;
    const int   rays   = abk::kScanRays;
    const float depth  = abk::kScanDepth;
    const float reach  = std::max(g_abReach, 16.f);

    const int  mode   = g_abMode;
    const bool dropIn = mode == 0;
    const bool keepOn = mode == 1;
    const auto lockInput = [&]() {
        cmd->upmove = 0.f;
        cmd->buttons &= ~abk::IN_SPEED_AB;
        if (!crouch) cmd->buttons &= ~IN_DUCK;
    };
    const float stopMaxTtc = keepOn ? abk::kSweepTime : 1e9f;

    AsClock budget;
    budget.Start(abk::kBudgetShare, TickInterval());

    AbWorld_t world;
    world.w    = AbReadWorld(pLocal);
    world.mins = mins;
    world.maxs = maxs;
    s_abAlign  = g_abAlign;
    world.wallSlides = s_abAlign ? abk::kAlignSlides : 0;
    world.snap = abk::kSnapReach;
    AbState_t start;
    start.origin   = origin;
    start.velocity = velocity;

    static Vec3 vizPath[abk::kVizMax];
    const auto verify = [&](const AbPlan_t& p) {
        int n = 0;
        AbState_t engStart{ s_prePredOrigin, s_prePredVel, 0 };
        const AbVerify_t v = AbVerify(pLocal, cmd, engStart, world, p, dropIn, budget, g_abViz ? vizPath : nullptr, &n);
        if (g_abViz) AbVizSet(vizPath, n, v.land ? v.landPos : p.target, v.land ? v.landNormal : p.normal);
        return v;
    };

    int flown = 0, refused = 0;
    const auto onFace = [](const AbResult_t& r, const AbPlan_t& c) {
        return r.hit && r.trace.plane.normal.Dot(c.normal) >= abk::kSameFace;
    };
    const auto accept = [&](AbPlan_t& c) {
        // the face must pay from vz alone: a glide onto flat floor is air strafe, not a bounce
        if (!c.valid || c.floor < gainMin || c.normal.z > abk::kMinFaceNz || (c.target - origin).Length2D() > reach) return false;

        AbResult_t r = AbFly(start, world, c, dropIn, &budget);
        if (!r.valid) return c.floor >= need;

        // pixel strip: flight lands off the face -> bisect the target along the slope (landed higher = too far uphill)
        const float flat = sqrtf(c.normal.x * c.normal.x + c.normal.y * c.normal.y);
        if (!c.keep && r.hit && !onFace(r, c) && flat >= 0.01f) {
            const Vec3 up(-c.normal.x / flat, -c.normal.y / flat, 0.f);
            const Vec3 b = c.target;
            float lo = -abk::kAimSpan, hi = abk::kAimSpan;
            (r.trace.endpos.z > b.z + 0.1f ? hi : lo) = 0.f;
            for (int i = 0; i < abk::kAimSteps && !budget.Expired(); ++i) {
                AbPlan_t p = c;
                p.aim    = (lo + hi) * 0.5f;
                p.target = Vec3(b.x + up.x * p.aim, b.y + up.y * p.aim, b.z);
                const AbResult_t t = AbFly(start, world, p, dropIn, &budget);
                if (!t.valid) break;
                if (onFace(t, p)) { c = p; r = t; break; }
                (t.hit && t.trace.endpos.z > b.z + 0.1f ? hi : lo) = p.aim;
            }
        }

        // walkable face: a tick ending inside the 2u ground trace grounds you with no clip. slide the spot along the slope so
        // the last tick end sits above that band and the move itself hits the face
        if (r.snapped) {
            ++s_abGates.snap;
            if (!c.keep && flat >= 0.01f) {
                const Vec3 up(-c.normal.x / flat, -c.normal.y / flat, 0.f);
                const float rise = flat / std::max(c.normal.z, 0.1f);
                const Vec3 b = c.target;
                for (const float s : abk::kPhaseShifts) {
                    if (budget.Expired()) break;
                    AbPlan_t p = c;
                    p.aim    = c.aim + s;
                    p.target = Vec3(b.x + up.x * s, b.y + up.y * s, b.z + s * rise);
                    const AbResult_t t = AbFly(start, world, p, dropIn, &budget);
                    if (t.valid && t.hit && !t.snapped && onFace(t, p)) { c = p; r = t; break; }
                }
            }
        }

        ++flown;
        c.flown     = true;
        c.pred      = AbFlySpeed(r, world);
        c.predTicks = r.ticks;
        c.firstOk   = !r.hit || r.ticks > 0;
        c.first     = r.first;
        c.slides    = r.slides;
        c.side      = r.hit ? AbSideDeg(r.out, r.trace.plane.normal) : -1.f;
        const bool straight = c.side <= abk::kSideMax;
        c.room = r.hit ? AbExitRoom(r.trace, r.out, world.mins, world.maxs) : 1.f;
        const bool clear = c.room >= 1.f;
        if (c.pred >= need && straight && clear && !r.snapped && (c.keep || onFace(r, c))) return true;

        if (!straight) ++s_abGates.side;
        if (!clear) ++s_abGates.blk;
        if (r.hit) s_abGates.rang = RAD2DEGF(acosf(std::clamp(r.trace.plane.normal.z, -1.f, 1.f)));
        ++refused;
        c.valid = false;
        return false;
    };

    const auto score = [](const AbPlan_t& p) { return p.flown ? p.pred : p.result; };

    // unarmed: every source runs, best flown speed wins. analytic under the best (+ hyst) skips its flight
    AbPlan_t stop;
    const auto beats = [&](const AbPlan_t& c) { return !stop.valid || c.result + abk::kPlanHyst > score(stop); };
    const auto take  = [&](const AbPlan_t& c) { if (c.valid && (!stop.valid || score(c) > score(stop))) stop = c; };

    AbHit_t hit;
    Vec3 patch = origin;
    bool seenBelow = false;
    if (!armed) {
        seenBelow = AbScanBelow(origin, radius, rays, depth, hit, &patch, &budget);
        if (seenBelow && AbPlanStop(origin, velocity, hit, patch, need, stopMaxTtc, world, stop, dropIn))
            accept(stop);
    }
    const float bestBelow = stop.result;
    const float bestAng   = seenBelow ? hit.angle : -1.f;

    // armed: committed spot only, never re-picked. release after kRefuseRelease flights in a row say it can't pay
    bool heldRefused = false;
    if (armed && !data.keep) {
        if (AbPlanStopAt(origin, velocity, data.target, std::max(origin.z - data.target.z, 0.f) + 64.f, need, stopMaxTtc, world, stop, dropIn))
            accept(stop);
        heldRefused = stop.flown && !stop.valid;
    }

    if (!armed) {
        const float dt   = AbDt();
        const float grav = AbGrav();
        const int maxTicks = (int)(abk::kLookaheadTime / dt + 0.5f);
        const int step     = std::max((int)(abk::kLookaheadStep / dt + 0.5f), 1);
        for (int t = step; t <= maxTicks && !budget.Expired(); t += step) {
            const float time = (float)t * dt;
            if (speed2d * time > reach) break;
            const Vec3 p(origin.x + velocity.x * time, origin.y + velocity.y * time, origin.z + velocity.z * time - 0.5f * grav * time * time);
            AbPlan_t cand;
            if (AbScanBelow(p, radius, rays, depth, hit, &patch, &budget)
                && AbPlanStop(origin, velocity, hit, patch, need, stopMaxTtc, world, cand, dropIn) && beats(cand) && accept(cand)) {
                cand.lookahead = t;
                take(cand);
            }
        }
        if (dropIn) {
            AbPlan_t wide;
            if (AbPlanWide(origin, velocity, need, reach, world, budget, [&](AbPlan_t& c) { return beats(c) && accept(c); }, wide))
                take(wide);
        }
    }

    // armed keep stays keep, armed stop stays stop (no flip between the two mid-air)
    AbPlan_t keep;
    if (keepOn && (!armed || data.keep) && AbPlanKeep(origin, velocity, need, depth, world, budget, keep))
        accept(keep);

    const AbPlan_t* plan = keep.valid && (!stop.valid || score(keep) >= score(stop)) ? &keep : stop.valid ? &stop : nullptr;

    s_abGates.refused += refused;
    if (!plan) {
        if (!armed) {
            ++(seenBelow ? s_abGates.noplan : s_abGates.noscan);
            if (bestBelow > s_abGates.best) { s_abGates.spd = speed2d; s_abGates.ang = bestAng; }
            s_abGates.best = std::max({ s_abGates.best, bestBelow, keep.result });
            s_abGates.need = need;
            data.Disarm();
            AbVizClear();
            return;
        }
        // flight keeps saying the held drop misses / throws sideways, or pays under entry: braking on only eats speed
        const bool losing = !data.keep && !heldRefused && stop.result < data.entrySpeed;
        if (losing || (heldRefused && ++data.refused >= abk::kRefuseRelease)) {
            DebugLog("AB: RELEASE spd=%.0f vz=%.0f rang=%.1f sd=%.1f%s", speed2d, velocity.z, s_abGates.rang, stop.side, losing ? " LOSING" : "");
            data.Disarm();
            data.jumpOwed = false;
            AbVizClear();
            return;
        }
        ++s_abGates.hold;

        const float ttcHold = ab::TimeToContact(velocity.z, std::max(origin.z - data.target.z, 0.f), AbGrav());
        if (data.keep) AbSteer(cmd, velocity, AbKeepVelocity(velocity, data.normal), world);
        else           AbSteer(cmd, velocity, AbStopWant(origin, velocity, data.target, data.normal, dropIn, world), world);
        if (dropIn) lockInput();

        data.found = true;
        data.ownsCmd = true;

        AbPlan_t held;
        held.keep   = data.keep;
        held.target = data.target;
        held.normal = data.normal;
        held.ttc    = ttcHold;
        const AbVerify_t v = verify(held);
        if (v.ticks >= 0) data.result = v.pred;

        DebugLog("AB: HOLD %s ttc=%.3f spd=%.0f vz=%.0f fly=%d/%d eng=%.0f(%d) us=%lld%s", data.keep ? "KEEP" : dropIn ? "DROP" : "STOP",
                 ttcHold, speed2d, velocity.z, flown, refused, v.pred, v.ticks, budget.UsedUs(), heldRefused ? " REFUSED" : "");
        return;
    }

    if (!armed) data.entrySpeed = speed2d;
    ++s_abGates.armed;

    data.armedTick = cmd->tick_count;
    data.refused   = 0;
    data.jumpOwed  = true;
    data.found     = true;
    data.keep      = plan->keep;
    data.target    = plan->target;
    data.normal    = plan->normal;
    data.angle     = RAD2DEGF(acosf(std::clamp(plan->normal.z, -1.f, 1.f)));
    data.result    = score(*plan);
    data.gain      = data.result - base;

    float dist = (plan->target - origin).Length2D();
    float want = 0.f;
    if (plan->keep) {
        const Vec3 kept = AbKeepVelocity(velocity, plan->normal);
        want = kept.Length2D();
        AbSteer(cmd, velocity, kept, world);
    } else
        AbSteer(cmd, velocity, AbStopWant(origin, velocity, plan->target, plan->normal, dropIn, world, &dist, &want), world);

    if (jumpOn) cmd->buttons &= ~IN_JUMP;
    if (crouch) cmd->buttons |= IN_DUCK;
    if (dropIn) lockInput();

    data.ownsCmd = true;

    const AbVerify_t v = verify(*plan);
    if (v.ticks >= 0) data.result = v.pred;

    DebugLog("AB: %s ang=%.1f drop=%.1f vz=%.0f->%.0f res=%.0f flr=%.0f (keep=%.0f%s stop=%.0f%s) gain=%.0f base=%.0f entry=%.0f "
             "spd=%.0f dist=%.1f want=%.0f ttc=%.3f la=%d pl=%.2f aim=%.2f al=%d sd=%.1f rm=%.2f fwd=%.0f side=%.0f fly=%d/%d pred=%.0f(%d) eng=%.0f(%d) err=%.2f us=%lld%s%s",
             plan->keep ? "KEEP" : dropIn ? "DROP" : "STOP", data.angle, plan->drop, velocity.z, plan->vzHit, plan->result,
             plan->floor, keep.result, keep.valid ? "" : "x", stop.result, stop.valid ? "" : "x", data.gain, base,
             data.entrySpeed, speed2d, dist, want, plan->ttc, plan->lookahead, plan->place, plan->aim, s_abAlign ? plan->slides : -1,
             plan->side, plan->room, cmd->forwardmove, cmd->sidemove,
             flown, refused, plan->pred, plan->predTicks, v.pred, v.ticks, v.err, budget.UsedUs(), budget.Expired() ? " BUDGET" : "",
             crouch ? " CROUCH" : "");
}

// pipeline entry: botox yields when another feature owns the cmd (fireman / air_stuck there, the TF2 owners here)
static void AutoBounce(void* pLocal, UserCmd_t* pCmd)
{
    const bool yield = s_textureBugHit || s_headSurfHit || s_headBounceHit || s_airStuckHit || s_wallClimbHit
                    || s_psAssistHopActive || s_shouldPixelSurf || s_psRidingNow || s_pixelHopHit;
    AutoBounceRun(pLocal, pCmd, yield);
}
