// ============================================================================
//  edgebug.inl -- EDGE BUG. 1:1 port of botox hacks/movement/edgebug.cpp (n_edgebug::impl_t)
//  onto TF2's sims. Included from movement.cpp, same translation unit: uses its Pred_Simulate /
//  RestoreEntityToPredicted, TraceHull, TraceFilterWorldProps, CorrectMovement and the s_*Hit
//  yield flags.
//
//  Entry points (Movement_RunImpl):
//    EdgeBugPre   before prediction -- snapshots velocity/flags/user move (PrePredictionEdgeBug)
//    EdgeBugPost  after Pred_End    -- search, plan pick, replay (EdgeBugPostPredict / donor_post)
//  EB_GetDrawData publishes the latched plan to the render thread.
//  EdgeBug_MouseInput is IClientMode::OverrideMouseInput: mouse steer input + mouse lock.
//
//  CSS differences from botox (CS:GO), each forced by cs_gamemovement / gamemovement:
//   - air duck moves the origin 8.5u = 0.5 * (62 - 45) (cs_gamemovement.cpp:826 FinishDuck, :800
//     FinishUnDuck), CS:GO's idiom with CSS hulls (CS:GO 0.5 * (72 - 54) = 9): EbDuckShiftZ.
//   - CSS ticks at 66.67: botox's 64-tick counts go through EbTicks, per-tick tolerances through
//     EbScale, hotwheels' -6 / -7 cuts are written off the half gravity step (MV_EbHw*).
//   - CSS HAS ladders (CS:GO does too): move_type_ladder (live, pre-pred, or a sim that grabs
//     one) drops the plan / ends the run, botox edgebug.cpp:634 / :1064.
//   - crouch-jump is legal (no FL_DUCKING gate in CCSGameMovement::CheckJumpButton), so a ducked
//     bug is as good as a standing one: no TF2 "eats the next jump" fallback.
// ============================================================================

static constexpr int k_ebMaxTicks = 256;   // botox k_predicted_cmd_max (2 s at 128)
static constexpr int k_ebPlanMax  = 8;
static constexpr float k_ebStandable = 0.7f;   // CategorizePosition's walkable cut (route calc reads it)

struct EbCmd_t { Vec3 view; float fwd; float side; int buttons; Vec3 origin; };
struct EbPlan_t
{
    EbCmd_t cmds[k_ebMaxTicks];
    int ticks = 0, variant = 0, brake = -1;
    float bend = 0.f;
    Vec3 end{};
    bool ducked = false;
};

// ---- live state (botox impl_t members) ----
static bool  s_ebDetected = false;        // m_found
static int   s_ebCurrentTick = 0;         // m_replay_hits
static int   s_ebLockTicks = 0;           // m_prediction_ticks
static int   s_ebFoundTick = 0;           // m_found_tick
static Vec3  s_ebVelBackup{};             // m_backup_velocity
static int   s_ebFlags = 0;               // m_backup_flags
static int   s_ebMoveType = 0;            // m_backup_move_type
static float s_ebUserFwd = 0.f, s_ebUserSide = 0.f;
static int   s_ebUserButtons = 0;
static EbCmd_t  s_ebCmds[k_ebMaxTicks] = {};   // m_predicted_cmds
static EbCmd_t  s_ebRun[k_ebMaxTicks]  = {};   // m_run_cmds
static EbPlan_t s_ebPlans[k_ebPlanMax];
static int   s_ebPlanCount = 0;
static Vec3  s_ebFollowEnd{};
static bool  s_ebDucked = false;          // m_ducked: the plan's duck is held past its replay until the fall restarts
static float s_ebMouseX = 0.f;            // OverrideMouseInput x, drained each EdgeBugPost
static float s_ebSteerSince = 0.f;
static int   s_ebLastSearchTick = -1, s_ebLastContactTick = -1;
static float s_ebStrafeSide = 1.f, s_ebStrafeLastYaw = 0.f;
static Vec3  s_ebEdgeTarget{};
static bool  s_ebEdgeTargetValid = false;
static float s_ebCourseYaw = 0.f;
static bool  s_ebCourseValid = false;
static int   s_ebLastVariant = 0;
static float s_ebLastBend = 0.f;
static bool  s_esActive = false;   // edge skip plan flying (movement_edge.inl owns it)
static int   s_esHit = 0, s_esLast = 0;   // edge skip plan ticks flown / last tick (movement_edge.inl owns them; mouse lock ramp reads them)

// ---- prefix reuse (TF2 port, not botox): sweep/release runs start from a base run's state ----
// eb_stance.h MV_EbFamily says which ticks of a run equal its pair's base run. Base runs save a
// Pred_Snap per tick from just before the earliest press window on; a press at k loads tick k
// instead of re-flying ticks 0..k-1. Index = pair * 2 + fam. Edge skip owns slots 0..64.
static constexpr int k_ebSnapBase = 80;
static constexpr int k_ebFamRing  = 16;   // last 16 family ticks per family (window <= 7 wide)
struct EbResume_t { int tick = -1; UserCmd_t c{}; Vec3 cur{}; float side = 1.f, lastYaw = 0.f; };
static EbResume_t s_ebResume[8][k_ebFamRing];
static EbCmd_t    s_ebFamCmds[8][k_ebMaxTicks];   // the family's recorded cmds, for the plan prefix
static int        s_ebFamCmdN[8] = {};

// ---- bend prefix + cursor (TF2 port, not botox): eb_stance.h MV_EbBend* ----
// bend B saves its state at tick |B| (slot per variant + sign); a bigger bend of the same pair
// starts there. Far contact: the bend pass resumes its order next search (s_ebBendCursor).
static constexpr int k_ebBend[] = { 1, 2, 3, 4, 6, 8, 12 };   // botox pass 2
static constexpr int k_ebBendMax = 12;
static constexpr int k_ebBendSnap = k_ebSnapBase + 8 * k_ebFamRing;   // 16 slots after the families
static EbResume_t s_ebBendRes[16];
static EbCmd_t    s_ebBendCmds[16][k_ebBendMax];
static int        s_ebBendCursor = 0;

// ---- tick rules (botox tick_scale.h) ----
static float EbScale() { return std::clamp(64.f * TickInterval(), 0.25f, 4.f); }
static int   EbTicks(int ticksAt64) { return EsTicksPure(ticksAt64, 1.f / TickInterval()); }
static float EbHalfGravTick(float grav) { return grav * 0.5f * TickInterval(); }

// ---- the clock budget (shared with edge skip) ----
struct EbBudget_t
{
    long long qpcStart = 0, qpcFreq = 0, capUs = 0;
    void Start(float flSeconds)
    {
        static const long long s_freq = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
        LARGE_INTEGER c; QueryPerformanceCounter(&c);
        qpcFreq = s_freq; qpcStart = c.QuadPart;
        capUs = static_cast<long long>(flSeconds * 1000000.f);
    }
    long long UsedUs() const
    {
        if (qpcFreq <= 0) return 0;
        LARGE_INTEGER c; QueryPerformanceCounter(&c);
        return ((c.QuadPart - qpcStart) * 1000000ll) / qpcFreq;
    }
    bool Expired() const { return UsedUs() >= capUs; }
    bool CannotFit(long long worstUs) const { return UsedUs() + worstUs >= capUs; }
};

// EbTimeGuard (tick-time window for sims outside Pred_Start) lives in movement.cpp

// air duck teleport: CCSGameMovement::FinishDuck in air adds 0.5 * (hullNormal - hullCrouch)
// (cs_gamemovement.cpp:826/843), FinishUnDuck takes it back (:800). CSS stand/duck heights are
// 62/45 (g_CSViewVectors, cs_gamerules.cpp:72-80): 0.5 * 17 = 8.5 (CS:GO's 9 is 0.5 * (72 - 54)).
// The 17 is the GROUND duck (VEC_DUCK_HULL_MIN - VEC_HULL_MIN, :839), never an air one.
// Scaled with the player's model scale -> read off the live hull
static float EbDuckShiftZ(void* pLocal)
{
    const float flHeight = EntV3(pLocal, g_offMaxs).z - EntV3(pLocal, g_offMins).z;
    if (flHeight < 1.f) return k_mvEbAirDuckShift;
    const float flNominal = (EntI(pLocal, g_offFlags) & FL_DUCKING) ? k_mvEbDuckH : k_mvEbStandH;
    return k_mvEbAirDuckShift * (flHeight / flNominal);
}

static Vec3 EbClampAng(Vec3 a)
{
    a.x = std::clamp(NormalizeAngle(a.x), -89.f, 89.f);
    a.y = NormalizeAngle(a.y);
    a.z = 0.f;
    return a;
}

// ---- the latched plan, for the world overlay (movement thread -> render thread) ----
static std::mutex s_ebVizLock;
static std::vector<Vec3> s_ebVizPath;
static int  s_ebVizFlown = 0;
static bool s_ebVizActive = false;

// gate moved to the CALLER (edgebug's own finds check g_edgeBugViz before calling; edge skip's
// (movement_edge.inl EsSearch) checks its own borrowed/own visualize knob) -- so a plan latched
// with edge bug's toggle off but edge skip's own visualize on still gets drawn. See report NEEDS
// FROM OTHERS: render.cpp RenderEdgeBugOverlay's `if (!g_edgeBug || ...)` gate needs the same split.
static void EbVizSet(const Vec3* pts, int n)
{
    std::lock_guard<std::mutex> l(s_ebVizLock);
    s_ebVizPath.assign(pts, pts + std::max(0, n));
    s_ebVizFlown = 0;
}

static void EbVizPublish(bool bActive, int iFlown)
{
    std::lock_guard<std::mutex> l(s_ebVizLock);
    s_ebVizActive = bActive;
    s_ebVizFlown = iFlown;
    if (!s_ebVizActive && !s_ebVizPath.empty()) s_ebVizPath.clear();
}

void EB_GetDrawData(EbDrawData& out)
{
    std::lock_guard<std::mutex> l(s_ebVizLock);
    out.path.clear();
    out.active = s_ebVizActive && (int)s_ebVizPath.size() >= 2;
    if (!out.active) return;
    const int iFrom = std::clamp(s_ebVizFlown, 0, (int)s_ebVizPath.size() - 2);
    out.path.assign(s_ebVizPath.begin() + iFrom, s_ebVizPath.end());
    out.at = s_ebVizPath.back();
}

// ---- reach sweep (botox eb_make_hull / eb_first_reach_tick) ----
// NOTHING IN REACH = NO SEARCH: one conservative hull sweep along the window's ballistic arc
// (3 chords, grown 32u in xy for strafe variants) can never hide an edgebug. Ducked hull grows
// the air-unduck drop down for the release family.
struct EbHull_t { Vec3 mins{}, maxs{}; float grav = 800.f; bool valid = false; };

static EbHull_t EbMakeHull(void* pLocal, float flGrav, float grow = 32.f)
{
    EbHull_t h;
    h.mins = EntV3(pLocal, g_offMins);
    h.maxs = EntV3(pLocal, g_offMaxs);
    h.mins.x -= grow; h.mins.y -= grow;
    h.maxs.x += grow; h.maxs.y += grow;
    h.mins.z -= 1.f;
    if (EntI(pLocal, g_offFlags) & FL_DUCKING) h.mins.z -= EbDuckShiftZ(pLocal);
    h.grav = flGrav;
    h.valid = true;
    return h;
}

static int EbFirstReachTick(void* pLocal, const EbHull_t& hull, const Vec3& origin, const Vec3& vel,
                            int windowTicks, int segs = 3)
{
    if (!hull.valid || windowTicks <= 0) return 0;
    const float ipt = TickInterval();
    const float window = windowTicks * ipt;
    const float segT = window / (float)segs;
    Vec3 maxs = hull.maxs;
    maxs.z += hull.grav * segT * segT * 0.125f;   // arc sag under a chord
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    Vec3 prev = origin;
    for (int seg = 1; seg <= segs; seg++)
    {
        const float t0 = segT * (seg - 1), t = segT * seg;
        Vec3 p = origin + vel * t;
        p.z = origin.z + vel.z * t - 0.5f * hull.grav * t * t;
        GameTrace_t tr{};
        TraceHull(prev, p, hull.mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
        if (tr.DidHit())
            return std::clamp((int)((t0 + (t - t0) * tr.fraction) / ipt), 0, windowTicks - 1);
        prev = p;
    }
    return -1;
}

// heading error of start -> end vs aim yaw; 90 = path too short to have a heading
static float EbAimErr(const Vec3& a, const Vec3& b, float aimYaw)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    if (dx * dx + dy * dy < 4.f) return 90.f;
    return fabsf(NormalizeAngle(RAD2DEGF(atan2f(dy, dx)) - aimYaw));
}

// ebanat ring: 16 vertical rays 32u out; a face under 0.6 nz = a ramp clipped the fall, not a lip
static bool EbSteepRing(void* pLocal)
{
    const Vec3 o = EntV3(pLocal, g_offOrigin);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    for (float a = 0.f; a < 6.2831855f; a += 0.3926991f)
    {
        const float x = o.x + cosf(a) * 32.f, y = o.y + sinf(a) * 32.f;
        GameTrace_t tr{};
        TraceLine(Vec3(x, y, o.z + 200.f), Vec3(x, y, o.z - 100.f), MASK_PLAYERSOLID, &filter, &tr);
        if (tr.fraction < 1.f && tr.plane.normal.z < 0.6f) return true;
    }
    return false;
}

// slope filter (SimpleVisual EdgeBugSurfaceUsable): sweep the hull one tick along vel from the
// candidate's pre-sim origin. A face under 0.7 nz never grounds (CategorizePosition,
// gamemovement.cpp:3864) so it can't reset gravity -- ClipVelocity shaving vz down a ramp is a
// slide, not a bug. Open air = usable (sim decides). Disp + brush same rule: nz >= 0.7.
static bool EbSurfaceUsable(void* pLocal, const Vec3& from, const Vec3& vel)
{
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(from, from + vel * TickInterval(), EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
              MASK_PLAYERSOLID, &filter, &tr);
    return tr.fraction >= 1.f || tr.plane.normal.z >= k_ebStandable;
}

// lookahead is a DISTANCE (2-3 ticks of travel at 64): stepped in 64-tick units at any rate.
// edge = floor ahead more than k_mvEbLipDrop down, MV_EbDropAhead
static bool EbIsNearEdge(void* pLocal)
{
    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    const float speed = vel.Length2D();
    if (speed < 1.f) return false;
    const Vec3 dir(vel.x / speed, vel.y / speed, 0.f);
    const int steps = speed < 120.f ? 2 : 3;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    for (int s = 1; s <= steps; s++)
    {
        const Vec3 p = origin + dir * (speed * (1.f / 64.f) * s);
        GameTrace_t tr{};
        TraceLine(Vec3(p.x, p.y, origin.z + 2.f), Vec3(p.x, p.y, origin.z - 64.f), MASK_PLAYERSOLID, &filter, &tr);
        if (MV_EbDropAhead(tr.fraction < 1.f, tr.endpos.z, origin.z, k_mvEbLipDrop)) return true;
    }
    return false;
}

// botox eb_on_slant: hull 2u down from the sim'd origin, a steep face (0 < nz < 0.7) under the
// feet = the fall slid on a ramp, not a lip
static bool EbOnSlant(void* pLocal)
{
    const Vec3 o = EntV3(pLocal, g_offOrigin);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(o, Vec3(o.x, o.y, o.z - 2.f), EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
              MASK_PLAYERSOLID, &filter, &tr);
    return !tr.startsolid && tr.fraction < 1.f && tr.plane.normal.z > 0.f && tr.plane.normal.z < k_ebStandable;
}

static bool EbCheck(void* pLocal, float vz0, float vz, float g, bool onGround, int mode)
{
    const float s = EbScale();
    return MV_EbCheck(mode, vz0, vz, g, s, vz0 < -6.f * s, onGround,
                      [&] { return EbIsNearEdge(pLocal); }, [&] { return EbSteepRing(pLocal); },
                      [&] { return EbOnSlant(pLocal); });
}

// botox CorrectMovement + its button fixup (move keys follow the folded wish)
static void EbCorrect(void* pLocal, UserCmd_t* c, const Vec3& wish, const Vec3& frame)
{
    if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND) && frame.z != 0.f) c->sidemove = 0.f;
    CorrectMovement(c, wish, frame);
    c->buttons &= ~(IN_MOVERIGHT | IN_MOVELEFT | IN_BACK | IN_FORWARD);
    if (c->sidemove > 0.f) c->buttons |= IN_MOVERIGHT; else if (c->sidemove < 0.f) c->buttons |= IN_MOVELEFT;
    if (c->forwardmove > 0.f) c->buttons |= IN_FORWARD; else if (c->forwardmove < 0.f) c->buttons |= IN_BACK;
}

// silent fold (botox start/end_movement_fix): move read in `from`'s frame, sent in `to`'s
static void EbFold(UserCmd_t* c, const Vec3& from, const Vec3& to)
{
    CorrectMovement(c, from, to);
    c->viewangles = to;
}

// BEND CONE: max path bend caps how far the FLIGHT PATH bends off s_ebCourseYaw, never the push.
// At the cone edge an outward push is MIRRORED across the velocity. 180 = no cone.
static void EbCommitStrafe(void* pLocal, UserCmd_t* c, Vec3 wish)
{
    const float cap = std::clamp(g_edgeBugStrafeAngleMax, 0.f, 180.f);
    if (cap < 180.f && s_ebCourseValid)
    {
        const Vec3 vel = EntV3(pLocal, g_offVelocity);
        if (vel.Length2D() > 1.f)
        {
            const float velYaw = RAD2DEGF(atan2f(vel.y, vel.x));
            const float dev = NormalizeAngle(velYaw - s_ebCourseYaw);
            const float moveOff = RAD2DEGF(atan2f(-c->sidemove, c->forwardmove));
            const float rel = NormalizeAngle(wish.y + moveOff - velYaw);
            if (fabsf(dev) >= cap && ((dev > 0.f) == (rel > 0.f)))
                wish.y = velYaw - rel - moveOff;
        }
    }
    EbCorrect(pLocal, c, wish, c->viewangles);
}

static void EbApplyAutoStrafe(void* pLocal, UserCmd_t* c, bool duck, float sideSpeed)
{
    if (duck) c->buttons |= IN_DUCK; else c->buttons &= ~IN_DUCK;
    s_ebStrafeSide = -s_ebStrafeSide;
    const float side = s_ebStrafeSide;
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    Vec3 wish = c->viewangles;
    const float speed = vel.Length2D();
    const float ideal = std::clamp(RAD2DEGF(atanf(15.f / speed)), 0.f, 90.f);
    c->forwardmove = 0.f;
    const float yawDelta = NormalizeAngle(wish.y - s_ebStrafeLastYaw);
    const float absDelta = fabsf(yawDelta);
    s_ebStrafeLastYaw = wish.y;
    if (absDelta <= ideal || absDelta >= 30.f)
    {
        const float velYaw = VectorAngles(vel).y;
        const float velDelta = NormalizeAngle(wish.y - velYaw);
        const float retrack = std::clamp(RAD2DEGF(atanf(30.f / speed)), 0.f, 90.f) * 2.f;
        if (velDelta <= retrack || speed <= 15.f)
        {
            if (-retrack <= velDelta || speed <= 15.f) { wish.y += side * ideal; c->sidemove = sideSpeed * side; }
            else { wish.y = velYaw - retrack; c->sidemove = sideSpeed; }
        }
        else { wish.y = velYaw + retrack; c->sidemove = -sideSpeed; }
        EbCommitStrafe(pLocal, c, wish);
    }
    else
    {
        c->sidemove = yawDelta > 0.f ? -sideSpeed : sideSpeed;
        EbCommitStrafe(pLocal, c, c->viewangles);
    }
}

// lip target, once per real tick (geometry is static during the sim): at the ballistic point near
// first contact lay 33 lanes across the path (+-64u, 4u apart), HULL-trace each straight down,
// take the lip-side lane nearest the path (MV_EbPickLip) inside the cone and the lateral reach.
// Hull, not point rays: a lip thinner than the lane gap still shows, and the target centre leaves
// the hull overlapping the lip by <= 4u (a bug) instead of sitting on the lip line (a landing).
static bool EbFindEdgeTarget(void* pLocal, int reachTick, int windowTicks, float grav)
{
    s_ebEdgeTargetValid = false;
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    const float speed = vel.Length2D();
    if (speed < 30.f) return false;
    const float ipt = TickInterval();
    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const int probe = std::clamp(reachTick >= 0 ? reachTick + 1 : windowTicks / 3, 1, std::max(1, windowTicks));
    const float t = probe * ipt;
    Vec3 p = origin + vel * t;
    p.z = origin.z + vel.z * t - 0.5f * grav * t * t;
    const Vec3 right(vel.y / speed, -vel.x / speed, 0.f);
    constexpr int k_lanes = 33;
    constexpr float k_lane = 4.f, k_stepUp = k_mvEbLipDrop;
    // start one tick of fall above p: reach is a chord estimate, p can already sit under the lip top
    const float top = p.z + 16.f + std::max(0.f, -(vel.z - grav * t)) * ipt;
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    MvEbLane lanes[k_lanes];
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    for (int i = 0; i < k_lanes; i++)
    {
        const Vec3 c = p + right * ((i - k_lanes / 2) * k_lane);
        GameTrace_t tr{};
        TraceHull(Vec3(c.x, c.y, top), Vec3(c.x, c.y, p.z - 128.f), mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
        lanes[i].kind = (tr.startsolid || tr.allsolid) ? kMvLaneWall : tr.fraction < 1.f ? kMvLaneFloor : kMvLaneVoid;
        lanes[i].z = tr.endpos.z;
        lanes[i].nz = tr.plane.normal.z;
    }
    const float cap = std::clamp(g_edgeBugStrafeAngleMax, 0.f, 180.f);
    const float n = (float)(probe + 1);
    const float maxLat = 30.f * ipt * n * (n + 1.f) * 0.5f + 16.f;
    auto offOf = [&](int i) { return (i - k_lanes / 2) * k_lane; };
    const int lip = MV_EbPickLip(lanes, k_lanes, k_stepUp, k_ebStandable, [&](int i)
    {
        const float off = offOf(i);
        if (fabsf(off) > maxLat) return false;
        if (cap < 180.f && s_ebCourseValid)
        {
            const Vec3 q = p + right * off;
            if (fabsf(NormalizeAngle(RAD2DEGF(atan2f(q.y - origin.y, q.x - origin.x)) - s_ebCourseYaw)) > cap) return false;
        }
        return true;
    });
    if (lip < 0) return false;
    s_ebEdgeTarget = p + right * offOf(lip);
    s_ebEdgeTargetValid = true;
    return true;
}

static void EbApplyToEdge(void* pLocal, UserCmd_t* c, bool duck, float sideSpeed)
{
    if (!s_ebEdgeTargetValid) { EbApplyAutoStrafe(pLocal, c, duck, sideSpeed); return; }
    if (duck) c->buttons |= IN_DUCK; else c->buttons &= ~IN_DUCK;
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    const float speed = vel.Length2D();
    if (speed < 1.f) { c->forwardmove = 0.f; c->sidemove = 0.f; return; }
    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const Vec3 dir(vel.x / speed, vel.y / speed, 0.f);
    const Vec3 to(s_ebEdgeTarget.x - origin.x, s_ebEdgeTarget.y - origin.y, 0.f);
    // only the part ACROSS the path can be bought (addspeed = 30 - dot(vel, wishdir))
    const float along = to.x * dir.x + to.y * dir.y;
    const Vec3 lat(to.x - dir.x * along, to.y - dir.y * along, 0.f);
    const float latLen = lat.Length2D();
    if (latLen < 1.f) { EbApplyAutoStrafe(pLocal, c, duck, sideSpeed); return; }
    const Vec3 target(0.f, RAD2DEGF(atan2f(lat.y / latLen, lat.x / latLen)), 0.f);
    c->forwardmove = 450.f;
    c->sidemove = 0.f;
    EbCommitStrafe(pLocal, c, target);
}

static void EbDropPlan(const char* why)
{
    if (s_ebDetected) DebugLog("EB: drop why=%s at %d/%d", why, s_ebCurrentTick, s_ebLockTicks);
    s_ebDetected = false;
    s_ebCurrentTick = 0;
    s_ebSteerSince = 0.f;
    s_ebDucked = false;
    s_ebBendCursor = 0;
}

// ============================================================================
//  donor styles (botox donor_*): 1 = delusional, 2 = dna
// ============================================================================
struct EbDonor_t
{
    int ticksLeft = 0, ebLength = 0, detectTick = 0, edgebugTick = 0;
    bool crouched = false, strafing = false;
    float fwd = 0.f, side = 0.f, yawDelta = 0.f, startYaw = 0.f;
    short mousedx = 0;
};
static EbDonor_t s_ebDonor;

static void EbDonorReset()
{
    s_ebDonor = {};
    s_ebDetected = false;
    s_ebCurrentTick = 0;
}

// TF2 mousedx is AFTER sensitivity (in_mouse.cpp:602 is written from the scaled mouse_x), and
// ApplyMouse turns the view by -m_yaw * x: so the hand's yaw is -m_yaw * mousedx
static float EbDonorMouseYaw(const UserCmd_t* c)
{
    return std::clamp(-(float)c->mousedx * CvarFloat("m_yaw", 0.022f), -30.f, 30.f);
}

static void EbDonorFixMove(void* pLocal, UserCmd_t* c, const Vec3& angle)
{
    if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND) && c->viewangles.z != 0.f && (c->buttons & IN_ATTACK))
        c->sidemove = 0.f;
    const float len = sqrtf(c->forwardmove * c->forwardmove + c->sidemove * c->sidemove);
    if (len == 0.f) return;
    const float yaw = DEG2RADF(RAD2DEGF(atan2f(c->sidemove, c->forwardmove)) + c->viewangles.y - angle.y);
    float fwd = cosf(yaw) * len;
    if (c->viewangles.x < -90.f || c->viewangles.x > 90.f) fwd = -fwd;
    c->forwardmove = std::clamp(fwd, -450.f, 450.f);
    c->sidemove = std::clamp(sinf(yaw) * len, -450.f, 450.f);
    c->upmove = std::clamp(c->upmove, -320.f, 320.f);
    c->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT);
}

// vel = this sim tick, start = the cmd's pre-prediction state (botox g_prediction.backup_data)
static bool EbDonorCheck(void* pLocal, UserCmd_t* c, bool& brk, float grav, float shift)
{
    if (const int mt = EntMoveType(pLocal); mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP) { brk = true; return false; }
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    const float ipt = TickInterval();
    const float gravVelo = -(grav * 0.5f) * ipt;
    const int fl = EntI(pLocal, g_offFlags);
    if (roundf(vel.z) >= 0.f || (fl & FL_ONGROUND) || roundf(vel.Length2D()) == 0.f) { brk = true; return false; }
    if (s_prePredVel.z < 0.f && vel.z > s_prePredVel.z && vel.z < 0.f)
    {
        // the origin compare reads an air duck's 8.5u teleport as a rise: take it out
        float rise = EntV3(pLocal, g_offOrigin).z - s_prePredOrigin.z;
        const bool dNow = (fl & FL_DUCKING) != 0, dStart = (s_prePredFlags & FL_DUCKING) != 0;
        if (dNow != dStart) rise += dNow ? -shift : shift;
        if (rise > 0.f) return false;
        const int zVel = (int)vel.z;
        Pred_Simulate(pLocal, c);
        const float rounded = roundf(-grav * ipt) + (float)zVel;
        const float nextVz = roundf(EntV3(pLocal, g_offVelocity).z);
        if (rounded == nextVz || (nextVz == 0.f && (EntI(pLocal, g_offFlags) & FL_ONGROUND))) return true;
        brk = true;
        return false;
    }
    return s_prePredVel.z < gravVelo && roundf(vel.z) == roundf(gravVelo);
}

static void EbDonorPre(UserCmd_t* pCmd)
{
    if (s_ebDetected && s_ebDonor.crouched && s_ebDonor.ticksLeft) pCmd->buttons |= IN_DUCK;
    if (s_ebDetected && s_ebDonor.ticksLeft && s_ebDonor.strafing)
    {
        pCmd->forwardmove = s_ebDonor.fwd;
        pCmd->sidemove = s_ebDonor.side;
    }
}

static void EbDonorPost(void* pLocal, UserCmd_t* pCmd, int style)
{
    const bool del = style == 1;
    const int mt = EntMoveType(pLocal);
    const bool key = On(g_edgeBug, g_edgeBugKey, g_edgeBugMode);
    if (del)
    {
        if ((s_prePredFlags & FL_ONGROUND) || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER || mt == MOVETYPE_LADDER)
        { EbDonorReset(); return; }
        if (!key && (!s_ebDonor.ticksLeft || s_ebDonor.ticksLeft > 2)) { EbDonorReset(); return; }
    }
    else if (!key || (s_prePredFlags & FL_ONGROUND) || mt != MOVETYPE_WALK) { EbDonorReset(); return; }
    if (s_ebMoveType == MOVETYPE_LADDER) { EbDonorReset(); return; }   // botox m_backup_move_type gate

    const Vec3 viewBackup = pCmd->viewangles;
    const float grav = CvarFloat("sv_gravity", 800.f);
    const float shift = EbDuckShiftZ(pLocal);
    if (!s_ebDonor.ticksLeft)
    {
        const float yawdelta = EbDonorMouseYaw(pCmd);
        const float ipt = TickInterval();
        const int ticklimit = std::clamp(del ? g_edgeBugDelTicks : (int)(0.5f + g_edgeBugDnaScan / ipt), 0, k_ebMaxTicks - 1);
        const bool advanced = del ? g_edgeBugDelAdvanced : g_edgeBugDnaAdvanced;
        const int predRounds = (advanced && yawdelta != 0.f) ? 4 : 2;
        const int search = del ? g_edgeBugDelSearch : std::max(g_edgeBugDnaRange, 1);
        const int divisor = del || advanced ? search : 2;
        const float delLimit = g_edgeBugDelAngleLimit;
        const bool dnaCustom = g_edgeBugDnaCustomAngle;
        const float dnaLimit = g_edgeBugDnaAngleLimit;
        const UserCmd_t base = *pCmd;
        const Vec3 orig = base.viewangles;
        RestoreEntityToPredicted();
        const Vec3 endVel = EntV3(pLocal, g_offVelocity);
        s_ebDonor.startYaw = orig.y;
        const int tickCount = g_globalVars->tickcount;
        static Vec3 path[k_ebMaxTicks + 1];
        int sims = 0;
        const bool dnaRising = !del && (endVel.z > 0.f || s_prePredVel.z > 0.f);
        const bool strafeRuns = !dnaRising && divisor != 0 && (del ? delLimit != 0.f : dnaLimit == 0.f);
        bool stillRuns = !dnaRising && ticklimit > 0;
        int reach = -1;
        if (stillRuns)
        {
            const int window = ticklimit + 1;
            reach = EbFirstReachTick(pLocal, EbMakeHull(pLocal, grav), EntV3(pLocal, g_offOrigin), endVel, window,
                                     std::clamp((int)std::ceil(window * ipt / 0.1f), 3, 16));
            stillRuns = reach >= 0;
        }
        // contact far (or none): search every 2nd tick, a far bug found 1 tick later is the same bug
        static int s_lastSearch = -1;
        const bool farSkip = !dnaRising && ticklimit > 0 && (reach < 0 || reach > EbTicks(16)) && s_lastSearch >= 0
                          && tickCount > s_lastSearch && tickCount - s_lastSearch < EbTicks(2);
        if (farSkip) stillRuns = false; else s_lastSearch = tickCount;
        float fan[2][64]; int fanN[2] = {};
        if (strafeRuns && predRounds > 2 && !farSkip)
        {
            float yd = yawdelta;
            for (int r = 0; r < 2; ++r)
            {
                const float maxDelta = yd;
                for (yd = maxDelta / divisor; fabsf(yd) <= fabsf(maxDelta) && fanN[r] < 64; yd += maxDelta / divisor)
                    fan[r][fanN[r]++] = yd;
            }
        }
        EbBudget_t clock; clock.Start(ipt * 0.2f);
        auto run = [&](int round, float yd)
        {
            const bool crouched = round == 0 || round == 2, strafing = round > 1;
            UserCmd_t c = base;
            if (crouched) c.buttons |= IN_DUCK; else c.buttons &= ~IN_DUCK;
            if (!strafing) { c.forwardmove = 0.f; c.sidemove = 0.f; if (!del) c.upmove = 0.f; }
            else if (!del) c.sidemove = (round == 2 && orig.y <= 0.f) ? base.sidemove * -450.f : base.sidemove * 450.f;
            RestoreEntityToPredicted();
            int pathN = 0;
            path[pathN++] = EntV3(pLocal, g_offOrigin);
            for (int t = 1; t <= ticklimit; ++t)
            {
                if (strafing)
                {
                    c.viewangles.y = NormalizeAngle(orig.y + yd * (float)t);
                    const float turned = fabsf(c.viewangles.y - s_ebDonor.startYaw);
                    if (del) { if (delLimit == 0.f || turned > delLimit) break; }
                    else if (((turned > (dnaCustom ? 1.f : 0.f)) ? dnaLimit : 1.f) != 0.f) break;
                }
                Pred_Simulate(pLocal, &c);
                sims++;
                const Vec3 vel = EntV3(pLocal, g_offVelocity);
                if (EntMoveType(pLocal) == MOVETYPE_LADDER
                    || (del ? (vel.z > 0.f || vel.Length2D() == 0.f) : (endVel.z > 0.f || s_prePredVel.z > 0.f))) break;
                if (pathN <= k_ebMaxTicks) path[pathN++] = EntV3(pLocal, g_offOrigin);
                bool br = false;
                if (EbDonorCheck(pLocal, &c, br, grav, shift))
                {
                    EbDonor_t& d = s_ebDonor;
                    d.ticksLeft = t; d.ebLength = t; d.edgebugTick = tickCount + t; d.detectTick = tickCount;
                    d.fwd = c.forwardmove; d.side = c.sidemove; d.yawDelta = yd;
                    d.crouched = crouched; d.strafing = strafing; d.mousedx = base.mousedx;
                    EbVizSet(path, pathN);
                    break;
                }
                if (br) break;
            }
        };
        int foundRound = -1;
        for (int round = 0; round < 2 && stillRuns && foundRound < 0; ++round)
        {
            run(round, yawdelta);
            if (s_ebDonor.ticksLeft) foundRound = round;
        }
        // strafe fan over 0.2 tick of clock (rounds 0/1 never cut): a cut fan resumes next search
        static int s_fanResume = 0;
        const int fanTotal = fanN[0] + fanN[1];
        bool fanCut = false;
        if (foundRound < 0 && fanTotal > 0)
        {
            const int first = s_fanResume % fanTotal;
            int i = 0;
            for (; i < fanTotal; ++i)
            {
                if (clock.Expired()) break;
                const int k = (first + i) % fanTotal, r = k < fanN[0] ? 0 : 1;
                run(2 + r, fan[r][r ? k - fanN[0] : k]);
                if (s_ebDonor.ticksLeft) { foundRound = 2 + r; break; }
            }
            fanCut = foundRound < 0 && i < fanTotal;
            s_fanResume = fanCut ? (first + i) % fanTotal : 0;
        }
        RestoreEntityToPredicted();
        if (s_ebDonor.ticksLeft)
            DebugLog("EB: style=%d find round=%d t=%d duck=%d strafe=%d yd=%.2f fwd=%.0f side=%.0f sims=%d lim=%d vz=%.2f cut=%d",
                     style, foundRound, s_ebDonor.ebLength, s_ebDonor.crouched ? 1 : 0, s_ebDonor.strafing ? 1 : 0,
                     s_ebDonor.yawDelta, s_ebDonor.fwd, s_ebDonor.side, sims, ticklimit, s_prePredVel.z, fanCut ? 1 : 0);
    }

    if (!s_ebDonor.ticksLeft) { s_ebDetected = false; return; }
    s_ebDetected = true;
    s_ebDucked = false;
    s_ebFoundTick = s_ebDonor.edgebugTick;
    s_ebLockTicks = s_ebDonor.ebLength;
    const float yaw = NormalizeAngle(s_ebDonor.startYaw + s_ebDonor.yawDelta * (float)(s_ebDonor.ebLength - (s_ebDonor.ticksLeft - 1)));
    if (s_ebDonor.crouched) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
    if (s_ebDonor.strafing)
    {
        pCmd->forwardmove = s_ebDonor.fwd;
        pCmd->sidemove = s_ebDonor.side;
        if (del)
        {
            const Vec3 newView(pCmd->viewangles.x, yaw, pCmd->viewangles.z);
            pCmd->viewangles.y = yaw;
            if (g_edgeBugDelSilent) { pCmd->viewangles = viewBackup; EbDonorFixMove(pLocal, pCmd, newView); }
            else { Vec3 v = newView; EngineSetViewAngles(v); }   // botox donor_frame_start, per tick
        }
        else
        {
            pCmd->mousedx = s_ebDonor.mousedx;
            const Vec3 ang(viewBackup.x + 18.f, yaw, 0.f);
            pCmd->viewangles.y = yaw;
            EbDonorFixMove(pLocal, pCmd, ang);
            Vec3 v(viewBackup.x, yaw, 0.f); EngineSetViewAngles(v);
        }
    }
    else { pCmd->forwardmove = 0.f; pCmd->sidemove = 0.f; }
    s_ebDonor.ticksLeft--;
    s_ebCurrentTick = s_ebDonor.ebLength - s_ebDonor.ticksLeft;
}

// ============================================================================
//  style 3 = hotwheels (botox hw_post / hw_detect / hw_frame_start / hw_mouse,
//  edgebug.cpp:1984-2186). "start" = the cmd's pre-prediction state (botox
//  g_prediction.backup_data): s_prePredVel / s_prePredFlags / s_prePredOrigin / s_ebMoveType /
//  s_ebStartView.
// ============================================================================
struct EbHw_t
{
    bool will = false, fail = false, strafing = false, ducking = false;
    float yawDelta = 0.f, startYaw = 0.f, side = 0.f, fwd = 0.f, savedMouseDx = 0.f;
    int ticksToStop = 0, lastTick = 0;
};
static EbHw_t s_ebHw;
static Vec3  s_ebStartView{};       // cmd view at prediction start
static float s_ebLastTickYaw = 0.f; // last cmd's view yaw (botox g_ctx.m_last_tick_yaw)

static void EbHwReset()
{
    s_ebHw.ducking = false;
    s_ebHw.lastTick = 0;
    s_ebHw.ticksToStop = 0;
    s_ebHw.will = false;
    s_ebHw.fail = false;
}

static bool EbHwMoveOff(int mt)
{
    return mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_FLY || mt == MOVETYPE_OBSERVER;
}

static void EbHwDetect(void* pLocal, UserCmd_t* sim, float grav)
{
    const int mt = EntMoveType(pLocal);
    if (s_prePredVel.z > 0.f || mt == MOVETYPE_LADDER || mt == MOVETYPE_OBSERVER || mt == MOVETYPE_NOCLIP)
    {
        s_ebHw.will = false; s_ebHw.fail = true;
        return;
    }
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    const float g = EbHalfGravTick(grav);
    if (roundf(vel.z) == 0.f || (s_prePredFlags & FL_ONGROUND))
    {
        s_ebHw.will = false; s_ebHw.fail = true;
    }
    else if (MV_EbHwFalling(s_prePredVel.z, vel.z, g) && !(EntI(pLocal, g_offFlags) & FL_ONGROUND)
             && s_prePredOrigin.z > EntV3(pLocal, g_offOrigin).z)
    {
        if (MV_EbHwSnap(s_prePredVel.z, vel.z, g) && vel.Length2D() >= s_prePredVel.Length2D())
        {
            s_ebHw.will = true; s_ebHw.fail = false;
        }
        else
        {
            const float prev = vel.z;
            Pred_Simulate(pLocal, sim);
            const float expected = roundf(-grav * TickInterval() + prev);
            s_ebHw.will = expected == roundf(EntV3(pLocal, g_offVelocity).z);
            s_ebHw.fail = !s_ebHw.will;
        }
    }
}

static void EbHwPost(void* pLocal, UserCmd_t* pCmd, bool bTaken)
{
    if (!EntAlive(pLocal)) { EbHwReset(); s_ebDetected = false; return; }
    if (EbHwMoveOff(s_ebMoveType)) return;   // botox: latch kept, nothing applied
    if (!On(g_edgeBug, g_edgeBugKey, g_edgeBugMode) || bTaken) { EbHwReset(); s_ebDetected = false; return; }

    const int ticks = std::clamp(g_edgeBugHwTicks, 0, k_ebMaxTicks - 2);
    const float lim = 180.f / (float)std::max(ticks, 1);
    // botox subtracts raw yaws; normalized here so a turn across +-180 is not a 358 deg flick
    const float yawDelta = std::clamp(NormalizeAngle(s_ebStartView.y - s_ebLastTickYaw), -lim, lim);
    const float origFwd = pCmd->forwardmove, origSide = pCmd->sidemove;
    const Vec3 origView = pCmd->viewangles;
    const int tickCount = g_globalVars->tickcount;
    const bool wasFound = s_ebHw.will;
    const float grav = CvarFloat("sv_gravity", 800.f);
    static Vec3 path[k_ebMaxTicks + 1];
    int sims = 0;

    auto loop = [&](bool ducked, bool strafe)
    {
        if (s_ebHw.will) return;
        RestoreEntityToPredicted();
        s_ebHw.startYaw = origView.y;
        int pathN = 0;
        path[pathN++] = EntV3(pLocal, g_offOrigin);
        for (int i = 0; i <= ticks; i++)
        {
            UserCmd_t sim = *pCmd;
            // botox also forces FL_DUCKING on the entity here (a hotwheels paste): that skips the
            // air FinishDuck's 8.5u lift the real cmd gets, so the sim and the sent cmd disagree.
            // Only the button goes in: the sim is exactly what the replayed cmd does
            if (ducked) sim.buttons |= IN_DUCK; else sim.buttons &= ~IN_DUCK;
            if (!strafe)
            {
                s_ebHw.strafing = false;
                sim.forwardmove = 0.f;
                sim.sidemove = 0.f;
                sim.buttons &= ~(IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
            }
            else
            {
                s_ebHw.strafing = true;
                sim.forwardmove = origFwd;
                sim.sidemove = origSide;
                sim.viewangles.y = NormalizeAngle(origView.y + yawDelta * (float)i);
            }
            Pred_Simulate(pLocal, &sim);
            sims++;
            if (pathN <= k_ebMaxTicks) path[pathN++] = EntV3(pLocal, g_offOrigin);
            if ((s_prePredFlags & FL_ONGROUND) || roundf(s_prePredVel.z) >= 0.f
                || (EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) == MOVETYPE_LADDER)
            {
                s_ebHw.will = false;
                break;
            }
            if (!s_ebHw.will) EbHwDetect(pLocal, &sim, grav);
            if (s_ebHw.will)
            {
                s_ebHw.savedMouseDx = fabsf((float)sim.mousedx);
                s_ebHw.ticksToStop = i + 1;
                s_ebHw.lastTick = tickCount;
                s_ebHw.ducking = ducked;
                if (strafe) { s_ebHw.yawDelta = yawDelta; s_ebHw.fwd = origFwd; s_ebHw.side = origSide; }
                else s_ebHw.fwd = s_ebHw.side = 0.f;
                if (g_edgeBugViz) EbVizSet(path, pathN);
                break;
            }
            if (s_ebHw.fail) { s_ebHw.fail = false; break; }
        }
        RestoreEntityToPredicted();
    };

    loop(false, false);
    loop(true, false);
    if (g_edgeBugHwAdvanced && yawDelta < g_edgeBugHwStrafeDeltaMax / 10000.f)
    {
        loop(false, true);
        loop(true, true);
    }
    if (!wasFound && s_ebHw.will)
        DebugLog("EB: style=3 find t=%d duck=%d strafe=%d yd=%.3f fwd=%.0f side=%.0f sims=%d lim=%d vz=%.2f",
                 s_ebHw.ticksToStop, s_ebHw.ducking ? 1 : 0, s_ebHw.strafing ? 1 : 0, s_ebHw.yawDelta,
                 s_ebHw.fwd, s_ebHw.side, sims, ticks, s_prePredVel.z);

    if (s_ebHw.will)
    {
        RestoreEntityToPredicted();
        if (tickCount < s_ebHw.ticksToStop + s_ebHw.lastTick + 1)
        {
            pCmd->buttons &= ~(IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
            if (s_ebHw.strafing)
            {
                pCmd->sidemove = s_ebHw.side;
                pCmd->forwardmove = s_ebHw.fwd;
                pCmd->viewangles.y = NormalizeAngle(s_ebHw.startYaw + s_ebHw.yawDelta * (float)(tickCount - s_ebHw.lastTick));
            }
            else { pCmd->sidemove = 0.f; pCmd->forwardmove = 0.f; }
            if (s_ebHw.ducking) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        }
        else EbHwReset();
    }

    s_ebDetected = s_ebHw.will;
    if (s_ebDetected)
    {
        s_ebDucked = false;
        s_ebFoundTick = s_ebHw.lastTick + s_ebHw.ticksToStop;
        s_ebLockTicks = s_ebHw.ticksToStop;
        s_ebCurrentTick = tickCount - s_ebHw.lastTick + 1;
    }
    else s_ebCurrentTick = 0;
}

static void EbLobFrameStart();   // defined below, with the rest of the lob ("lobotomy v2") port

// botox donor_frame_start: style 3 (hotwheels) -> hw_frame_start, style 4 (lobotomy v2) ->
// lob_frame_start, same dispatch order as edgebug.cpp:1950-1958. FrameStageNotify FRAME_START,
// src/hooks/fsn.cpp.
void EdgeBug_FrameStart()
{
    if (g_edgeBug && g_edgeBugStyle == 4) { EbLobFrameStart(); return; }
    if (!g_edgeBug || g_edgeBugStyle != 3 || !s_ebHw.will || !s_ebHw.strafing || !g_globalVars) return;
    if (EbHwMoveOff(s_ebMoveType) || s_ebHw.ticksToStop <= 0) return;
    // only inside the plan's own window: a latch left by a cmd that never came (disconnect,
    // death before the next create move) must not keep steering the view
    const int since = g_globalVars->tickcount - s_ebHw.lastTick;
    if (since < 0 || since > s_ebHw.ticksToStop + 1) return;
    const float ipt = TickInterval();
    Vec3 wish(s_ebStartView.x, s_ebHw.startYaw, 0.f);
    const float hitDelta = (float)s_ebHw.ticksToStop * ipt;
    const float curDelta = g_globalVars->curtime - (float)s_ebHw.lastTick * ipt;
    wish.y += NormalizeAngle(s_ebHw.yawDelta * ((float)s_ebHw.ticksToStop * (curDelta / hitDelta)));
    wish.y = NormalizeAngle(wish.y);
    EngineSetViewAngles(wish);
}

// botox hw_mouse: the turn is damped harder the closer the bug tick and the bigger the hand
static void EbHwMouse(float* x, float* y)
{
    if (!s_ebHw.will || !x || *x == 0.f || !g_globalVars) return;
    const float amt = g_edgeBugHwLockAmt;
    const int left = s_ebHw.lastTick + s_ebHw.ticksToStop - g_globalVars->tickcount;
    const float factor = 100.f / (powf((float)s_ebHw.ticksToStop, (float)left / *x) + 100.f + amt * s_ebHw.savedMouseDx);
    if (!std::isnan(factor)) *x *= factor;
    if (y && *y != 0.f) *y *= 1.f - (0.1f * amt * 10.f);
}

// delusional mouse fix: mousedx/dy agree with the view the cmd really sends
static void EbDonorMouseFix(UserCmd_t* pCmd)
{
    if (!g_edgeBug || g_edgeBugStyle != 1 || !g_edgeBugDelMouseFix) return;
    static Vec3 last{};
    Vec3 d = pCmd->viewangles - last;
    d.y = NormalizeAngle(d.y);
    last = pCmd->viewangles;
    auto counts = [](float v) { const int i = (int)std::clamp(v, -32768.f, 32767.f); return (short)(i == 0 ? 1 : i); };
    if (d.x != 0.f) pCmd->mousedy = counts(d.x / CvarFloat("m_pitch", 0.022f));
    if (d.y != 0.f) pCmd->mousedx = counts(-d.y / CvarFloat("m_yaw", 0.022f));
}

// ---- EDGE BUG / EDGE SKIP DETECTION SOUND -- botox's real combo (edgebug.cpp/edge_skip.cpp:
// m_edge_bug_sound/_custom/_volume, m_edge_skip_sound/_custom/_volume). The TF2-port's own pair
// (g_edgeBugSound bool + g_edgeBugSoundId, no "none"/custom/volume) lived in movement_tricks2.inl's
// EdgeBugSoundPre -- not this unit's file, so that function is now DEAD (report: NEEDS FROM OTHERS,
// movement_tricks2.inl lines 65-88 deleted, kEbSounds there removed). Detection + stock-sound
// mapping (none/arena switch/button/money/beep/custom -> lever7/button9/bell1/blip1, same files
// pixelsurf.inl's PsPlaySound already confirmed present) is ported here instead, where the sound
// settings var (g_edgeBugSoundMode/_Custom/_Volume) actually lives.
static void PrCmd(const char* fmt, ...);   // defined later in this TU by practice.inl (movement.cpp)

static const char* const kEbSounds[] = { "buttons/lever7.wav", "buttons/button9.wav",
    "buttons/bell1.wav", "buttons/blip1.wav" };

// "play" is a console cmd, no volume argument (Source engine) -- *Volume is read and stored but
// has no effect until routed through IEngineSound, same known gap pixelsurf.inl's PsPlaySound and
// movement_tricks2.inl's old EdgeBugSoundPre already shipped with (NEEDS FROM OTHERS: a shared
// IEngineSound::EmitSound path for all three, so the volume slider finally does something).
// botox play_trick_sound: emit_ambient_sound stacked per 1.0 for volume > 1. CSS engine.dll has the
// "playvol <sound> <vol>" console cmd (string "Play a sound at a specified volume" present), so no
// IEngineSound vtable slot is needed: one playvol per started 1.0 of volume.
static void PlaySoundVol(const char* path, float vol)
{
    for (float left = std::clamp(vol, 0.f, 2.f); left > 0.001f; left -= 1.f)
        PrCmd("playvol %s %.2f", path, left < 1.f ? left : 1.f);
}

static void EbPlaySound(int idx, const char* custom, float vol = 1.f)
{
    if (idx <= 0) return;
    const char* path = nullptr;
    if (idx >= 1 && idx <= 4) path = kEbSounds[idx - 1];
    else if (idx == 5) path = (custom && custom[0]) ? custom : nullptr;
    if (!path) return;
    PlaySoundVol(path, vol);
}

// botox: edge bug has no persistent "found" flag to hang a sound off once the replay ends, so the
// real detection (vz snapped exactly to -gravity-per-tick while airborne, one tick after a fall)
// substitutes -- same test movement_tricks2.inl's old EdgeBugSoundPre used, new var names. Edge
// skip needs none of this: it stays active and latched (s_esActive), so EsSearch below calls
// EbPlaySound directly on a real find.
static void EbSoundPre(void* pLocal)
{
    static float s_prevVz = 0.f;
    static bool  s_prevAir = false;
    static int   s_assistGrace = 0;   // cmds left since the plan last flew
    const int assisted = s_assistGrace;
    s_assistGrace = s_ebDetected ? 3 : (s_assistGrace > 0 ? s_assistGrace - 1 : 0);
    if (!pLocal || g_offVelocity <= 0 || g_offFlags <= 0) { s_prevAir = false; return; }
    const float vz  = EntV3(pLocal, g_offVelocity).z;
    const bool  air = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntAlive(pLocal);
    constexpr float kEps = 0.5f;
    const float flGravVel = -EbHalfGravTick(CvarFloat("sv_gravity", 800.f));
    const bool hit = s_prevAir && air && s_prevVz < flGravVel - kEps && fabsf(vz - flGravVel) <= kEps;
    s_prevVz = vz; s_prevAir = air;
    if (!hit || !assisted || g_edgeBugSoundMode <= 0) return;
    s_assistGrace = 0;   // one sound per assisted EB
    EbPlaySound(g_edgeBugSoundMode, g_edgeBugSoundCustom, g_edgeBugSoundVolume);
}

// ============================================================================
//  EDGEBUG LOB ("lobotomy v2", botox edgebug.cpp lob_pre/lob_check/lob_strafe/lob_post/
//  lob_frame_start/lob_mouse, style index 4). A donor-recorded-replay style distinct from the
//  main search (style 0) and the donor styles (1/2/3): each cmd it either SEARCHES (no plan
//  flying) or REPLAYS tick s.hits of the plan it already found, same shape as those donor styles.
//  botox menu_tab_movement.cpp:460 ("lobotomy v2") + :489-501 (advanced search / silent edgebug /
//  extra advanced search / autostrafe to edge / strafe angle / delta scale / edgebug ticks /
//  edgebug lock), vars botox globals/config/variables.h:1238-1245 -> u_movx.h/.inc here.
//  CSS deltas: gravity_vel via EbHalfGravTick (already CSS-tick-correct, this file's own helper);
//  MOVETYPE_LADDER/NOCLIP gate added (botox :2371 checks move_type_ladder; this port's CSS has
//  ladders too, same as botox's CS:GO); air-duck-crouch-jump legality needs no fallback (CSS
//  CheckJumpButton has no FL_DUCKING refusal, same note wallclimb.inl/edgebug.inl already make).
// ============================================================================
struct EbLobStrafeState { float side = 1.f, oldYaw = 0.f; };
struct EbLobCmd_t { Vec3 view{}; float fwd = 0.f, side = 0.f; int buttons = 0; Vec3 origin{}; };
static struct EbLob_t
{
    Vec3  velBackup{}, viewBackup{}, newAngle{}, aim{}, aimTo{};
    bool  found = false, ducked = false, brk = false;
    int   ticks = 0, lastType = 0, secLastType = 0, hits = 0, ebTick = 0, predTicks = 0, predStamp = 0;
    float mouseOffset = 0.f;
    EbLobStrafeState strafe[4];
    EbLobCmd_t cmds[128];
} s_ebLob;

static void EbLobPre(void* pLocal, UserCmd_t* pCmd)
{
    if (!EntAlive(pLocal)) return;
    s_ebLob.velBackup  = EntV3(pLocal, g_offVelocity);
    s_ebLob.viewBackup = pCmd->viewangles;
}

// botox lob_check: true on the exact "about to land" velocity frame (half-gravity snap, the same
// idiom EbSoundPre above already uses for CSS), or a 16-ray ground probe 200u above the fall when
// the next tick's velocity confirms free fall (nothing but gravity acting). sets s.brk when the
// probe hits a steep/non-standable surface (botox's signal to stop that variant outright).
static bool EbLobCheck(void* pLocal, UserCmd_t* pCmd)
{
    if (!EntAlive(pLocal)) return false;
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP) return false;
    const float grav       = CvarFloat("sv_gravity", 800.f);
    const float gravityVel = EbHalfGravTick(grav);
    const Vec3  vel        = EntV3(pLocal, g_offVelocity);
    if (s_ebLob.velBackup.z < -gravityVel && roundf(vel.z) == -roundf(gravityVel))
        return true;
    if (s_ebLob.velBackup.z < -6.f && vel.z > s_ebLob.velBackup.z && vel.z < -6.f)
    {
        const float vzBefore = vel.z;
        Vec3 checkOrigin = EntV3(pLocal, g_offOrigin);
        checkOrigin.z += 200.f;
        UserCmd_t t = *pCmd;
        Pred_Simulate(pLocal, &t);
        const float expected = roundf(-CvarFloat("sv_gravity", 800.f) * TickInterval() + vzBefore);
        if (expected == roundf(EntV3(pLocal, g_offVelocity).z))
        {
            constexpr float kPi = 3.14159265358979323846f;
            const float step = kPi * 2.f / 16.f;
            TraceFilterWorldProps filter; filter.Setup(pLocal);
            for (float a = 0.f; a < kPi * 2.f; a += step)
            {
                const Vec3 start(32.f * cosf(a) + checkOrigin.x, 32.f * sinf(a) + checkOrigin.y, checkOrigin.z);
                const Vec3 end = start - Vec3(0.f, 0.f, 300.f);
                GameTrace_t tr{};
                TraceLine(start, end, MASK_PLAYERSOLID, &filter, &tr);
                if (tr.fraction != 1.f && tr.plane.normal.z < 0.6f)
                {
                    s_ebLob.brk = true;
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

// botox lob_strafe: one of the donor's 4 copy-pasted autostrafe blocks, now one function + a
// per-slot side/oldYaw state (s_ebLob.strafe[]).
static void EbLobStrafe(void* pLocal, UserCmd_t* pCmd, float& side, float& oldYaw)
{
    side = -side;
    const Vec3 velocity = EntV3(pLocal, g_offVelocity);
    Vec3  wishAngle = pCmd->viewangles;
    const float speed = velocity.Length2D();
    const float safeSpeed = speed > 1e-4f ? speed : 1e-4f;
    const float idealStrafe = std::clamp(RAD2DEGF(atanf(15.f / safeSpeed)), 0.f, 90.f);
    pCmd->forwardmove = 0.f;
    const float sidespeed = CvarFloat("cl_sidespeed", 450.f);
    const float yawDelta = remainderf(wishAngle.y - oldYaw, 360.f);
    const float absYawDelta = fabsf(yawDelta);
    oldYaw = wishAngle.y;
    if (absYawDelta <= idealStrafe || absYawDelta >= 30.f)
    {
        const Vec3  velDir = VectorAngles(velocity);
        const float velDelta = remainderf(wishAngle.y - velDir.y, 360.f);
        const float retrack = std::clamp(RAD2DEGF(atanf(30.f / safeSpeed)), 0.f, 90.f) * 2.f;
        if (velDelta <= retrack || speed <= 15.f)
        {
            if (-retrack <= velDelta || speed <= 15.f)
            {
                wishAngle.y += side * idealStrafe;
                pCmd->sidemove = sidespeed * side;
            }
            else
            {
                wishAngle.y    = velDir.y - retrack;
                pCmd->sidemove = sidespeed;
            }
        }
        else
        {
            wishAngle.y    = velDir.y + retrack;
            pCmd->sidemove = -sidespeed;
        }
        CorrectMovement(pCmd, wishAngle, pCmd->viewangles);
    }
    else if (yawDelta > 0.f) pCmd->sidemove = -sidespeed;
    else pCmd->sidemove = sidespeed;
}

// botox lob_post, 1:1: no plan flying = search up to `search` variants (0/1 = plain duck/unduck
// drop, 2/3 = turn or autostrafe toward the wall both stances, 4/5 = mega's extra strafe pair),
// each held up to `ticks` ticks via a real Pred_Simulate chain (RestoreToFrame between variants,
// botox's ReStorePrediction); a plan flying = replay tick s.hits of what the search recorded.
static void EbLobPost(void* pLocal, UserCmd_t* pCmd, bool bTaken)
{
    auto& s = s_ebLob;
    const bool wasFound = s.found;
    int sims = 0, foundV = -1;

    [&]
    {
        if (!On(g_edgeBug, g_edgeBugKey, g_edgeBugMode))
        {
            s.found = false; s.ticks = 0; s.ducked = false;
            return;
        }
        if (!EntAlive(pLocal)) { s.found = false; s.ticks = 0; return; }

        const int mt = EntMoveType(pLocal);
        if (mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP || s_ebMoveType == MOVETYPE_LADDER
            || s_ebMoveType == MOVETYPE_NOCLIP || bTaken)
        {
            s.found = false; s.ticks = 0; s.ducked = false;
            return;
        }

        if (s.ducked)
        {
            if (!(s_ebFlags & FL_ONGROUND)) pCmd->buttons &= ~IN_JUMP;
            else pCmd->buttons |= IN_JUMP;
            pCmd->buttons |= IN_DUCK;
        }
        if (s_ebFlags & FL_ONGROUND) { s.ducked = false; return; }
        if (!s.found && s.ducked && s_ebVelBackup.z < -40.f) s.ducked = false;

        const bool mega = g_edgeBugLobMega;
        float strafeAngle = mega ? 179.f : g_edgeBugLobStrafeAngle;
        const auto isLargeAngleDiff = [&](float yaw1, float yaw2) {
            float diff = fabsf(yaw1 - yaw2);
            if (diff > 180.f) diff = 360.f - diff;
            return diff >= strafeAngle;
        };

        const int   buttonsBackup = pCmd->buttons;
        const float forwardBackup = pCmd->forwardmove;
        const float sideBackup    = pCmd->sidemove;
        const Vec3  viewBackup    = pCmd->viewangles;
        const bool  silent      = g_edgeBugLobSilent;
        const bool  autostrafe  = g_edgeBugLobAutostrafe;
        const int   deltaType   = g_edgeBugLobDeltaType;
        float deltaScale = deltaType == 0 ? 1.f : deltaType == 1 ? 0.8f : deltaType == 2 ? 0.5f : 0.25f;
        if (mega) deltaScale = 1.f;
        const Vec3 deltaAngle = (viewBackup - s.newAngle) * deltaScale;
        s.newAngle = pCmd->viewangles;
        int search = g_edgeBugLobAdvanced ? 4 : 2;
        if (mega) search = 6;
        const int ticks = std::clamp(g_edgeBugLobTicks, 0, 126);

        const int  nBase   = PredCommandsPredicted();
        const auto restore = [&]() { RestoreToFrame(nBase - 1); };

        if (!s.found)
        {
            for (int v = 0; v < search; v++)
            {
                if (s.lastType) { v = s.lastType; s.lastType = 0; }
                restore();
                s.velBackup       = EntV3(pLocal, g_offVelocity);
                pCmd->viewangles  = viewBackup;
                pCmd->forwardmove = forwardBackup;
                pCmd->sidemove    = sideBackup;
                pCmd->buttons     = buttonsBackup;
                Vec3 currentAngle = pCmd->viewangles;
                const auto viewTurn = [&]()
                {
                    pCmd->forwardmove = forwardBackup;
                    pCmd->sidemove    = sideBackup;
                    if (!isLargeAngleDiff(currentAngle.y, viewBackup.y))
                    {
                        currentAngle   = currentAngle + deltaAngle;
                        currentAngle.y = NormalizeAngle(currentAngle.y);
                    }
                    if (silent)
                        CorrectMovement(pCmd, currentAngle, viewBackup);   // sent view stays viewBackup
                    else
                        pCmd->viewangles = currentAngle;
                };
                for (int i = 0; i < ticks; i++)
                {
                    if (s.found || (EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntV3(pLocal, g_offVelocity).z > 0.f)
                        break;
                    if (v == 0 || v == 1)
                    {
                        if (v == 1) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                        pCmd->forwardmove = 0.f;
                        pCmd->sidemove    = 0.f;
                    }
                    else if (mega)
                    {
                        if (v == 3 || v == 5) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                        if (v == 2 || v == 3) viewTurn();
                        else if (v == 4 || v == 5)
                        {
                            auto& st = s.strafe[v - 4];
                            EbLobStrafe(pLocal, pCmd, st.side, st.oldYaw);
                        }
                    }
                    else if (v == 2 || v == 3)
                    {
                        if (v == 3) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                        if (autostrafe)
                        {
                            auto& st = s.strafe[v];
                            EbLobStrafe(pLocal, pCmd, st.side, st.oldYaw);
                        }
                        else viewTurn();
                    }
                    s.cmds[i] = { pCmd->viewangles, pCmd->forwardmove, pCmd->sidemove, pCmd->buttons, EntV3(pLocal, g_offOrigin) };
                    if (s.found || (EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntV3(pLocal, g_offVelocity).z > 0.f)
                        break;
                    Pred_Simulate(pLocal, pCmd);
                    ++sims;
                    s.found = EbLobCheck(pLocal, pCmd);
                    if (s.brk) { s.brk = false; break; }
                    s.velBackup = EntV3(pLocal, g_offVelocity);
                    if (s.found)
                    {
                        const int tickCount = g_globalVars ? g_globalVars->tickcount : 0;
                        s.ticks        = i;
                        s.lastType     = v;
                        s.secLastType  = v;
                        s.ebTick       = tickCount + (i + 1);
                        s.predTicks    = i;
                        s.predStamp    = tickCount;
                        s.mouseOffset  = fabsf((float)pCmd->mousedx);
                        foundV         = v;
                    }
                }
                pCmd->viewangles  = viewBackup;
                pCmd->forwardmove = forwardBackup;
                pCmd->sidemove    = sideBackup;
                pCmd->buttons     = buttonsBackup;
            }
        }
        if (s.found)
        {
            restore();
            const float dist = (EntV3(pLocal, g_offOrigin) - s.cmds[s.hits].origin).Length();
            if (s.hits > s.ticks || dist > 1.f)
            {
                s.hits = 0; s.found = false; s.secLastType = 0;
                return;
            }
            if (s.cmds[s.hits].buttons & IN_DUCK) { pCmd->buttons |= IN_DUCK; s.ducked = true; }
            else pCmd->buttons &= ~IN_DUCK;
            if (s_ebFlags & FL_ONGROUND) pCmd->buttons |= IN_JUMP; else pCmd->buttons &= ~IN_JUMP;
            pCmd->forwardmove = s.cmds[s.hits].fwd;
            pCmd->sidemove    = s.cmds[s.hits].side;
            pCmd->viewangles  = s.cmds[s.hits].view;
            if (silent)
            {
                CorrectMovement(pCmd, s.cmds[s.hits].view, viewBackup);
                pCmd->viewangles = viewBackup;
            }
            s.aim = pCmd->viewangles;
            s.hits += 1;
        }
    }();

    if (!wasFound && foundV >= 0)
        DebugLog("EBLOB: find v=%d t=%d duck=%d sims=%d ticks=%d search=%d vz=%.2f", foundV, s.ticks,
                 (s.cmds[s.ticks].buttons & IN_DUCK) != 0, sims, g_edgeBugLobTicks,
                 g_edgeBugLobMega ? 6 : (g_edgeBugLobAdvanced ? 4 : 2), s_ebVelBackup.z);

    s_ebDetected = s.found;
    if (s.found) s_ebDucked = false;
}

// botox lob_frame_start: a smooth turn toward s.aim every frame when the replay turned the view
// for real (not silent, not pure autostrafe, past the drop/unduck ticks) -- SetViewAngles runs on
// a per-frame cadence, not per-cmd. botox calls this from donor_frame_start(), itself called from
// hooks/functions/frame_stage_notify.cpp:77 on e_client_frame_stage::start (FRAME_START) -- the
// exact hook+stage this CSS port already uses for hotwheels' own hw_frame_start (EdgeBug_FrameStart
// below, called from src/hooks/fsn.cpp's `stage == 0` case). Matching that call site rather than
// IClientMode::OverrideView: botox's own frame_start lives in FrameStageNotify, not OverrideView,
// and this port already has the FrameStageNotify hook wired to the matching stage.
static void EbLobFrameStart()
{
    auto& s = s_ebLob;
    if (!s.found) { s.aimTo = s.viewBackup; return; }
    if (g_edgeBugLobSilent || s.secLastType <= 1 || g_edgeBugLobAutostrafe) return;
    constexpr float smoothing = 0.1f, maxDelta = 10.f;
    const auto wrap = [](float d) {
        if (!std::isfinite(d)) return 0.f;
        while (d > 180.f) d -= 360.f;
        while (d < -180.f) d += 360.f;
        return d;
    };
    const auto step = [&](float aim, float& to) {
        float d = wrap(aim - to);
        if (fabsf(d) > maxDelta) d = d > 0.f ? maxDelta : -maxDelta;
        to += d * smoothing;
    };
    step(s.aim.x, s.aimTo.x);
    step(s.aim.y, s.aimTo.y);
    step(s.aim.z, s.aimTo.z);
    s.aimTo.x = NormalizeAngle(s.aimTo.x);
    s.aimTo.y = NormalizeAngle(s.aimTo.y);
    s.aimTo.z = NormalizeAngle(s.aimTo.z);
    EngineSetViewAngles(s.aimTo);
}

// botox lob_mouse: scales the raw mouse delta while a plan flies, so turning the mouse doesn't
// fight the replay's own silent/visible turn. Called from EdgeBug_MouseInput below (style == 4).
static void EbLobMouseScale(float* x)
{
    if (!s_ebLob.found || !x || *x == 0.f) return;
    const int tickNow = g_globalVars ? g_globalVars->tickcount : 0;
    const float v3 = (float)(s_ebLob.predStamp + s_ebLob.predTicks - tickNow) / *x;
    const float v4 = 100.f / (atan2f((float)s_ebLob.predTicks, v3) + 100.f + g_edgeBugLobLock * s_ebLob.mouseOffset);
    if (std::isfinite(v4)) *x *= v4;
}

// ============================================================================
//  pre / post
// ============================================================================
static void EdgeBugPre(void* pLocal, UserCmd_t* pCmd)
{
    if (!EntAlive(pLocal)) return;
    if (g_offVelocity > 0) s_ebVelBackup = EntV3(pLocal, g_offVelocity);
    if (g_offFlags > 0)    s_ebFlags = EntI(pLocal, g_offFlags);
    s_ebMoveType   = EntMoveType(pLocal);
    s_ebUserFwd    = pCmd->forwardmove;
    s_ebUserSide   = pCmd->sidemove;
    s_ebUserButtons = pCmd->buttons;
    s_ebLastTickYaw = s_ebStartView.y;   // last cmd's view (silent features fold, the view stays)
    s_ebStartView  = pCmd->viewangles;
    if (g_edgeBugStyle == 2) EbDonorPre(pCmd);   // botox: dna only
    if (g_edgeBugStyle == 4) EbLobPre(pLocal, pCmd);   // botox: lobotomy v2 only
}

static void EdgeBugPost(void* pLocal, UserCmd_t* pCmd)
{
    // raw mouse, not view delta: mouse lock holds the view still while the hand still turns
    const float mouseYaw = -CvarFloat("m_yaw", 0.022f) * s_ebMouseX;
    s_ebMouseX = 0.f;

    if (!g_frameReady || g_offVelocity <= 0 || g_offFlags <= 0 || g_offOrigin <= 0
        || g_offMins <= 0 || g_offMaxs <= 0 || g_offTickBase <= 0 || !EntAlive(pLocal))
    {
        EbDropPlan("off");
        EbDonorReset();
        EbHwReset();
        return;
    }
    const EbTimeGuard timeGuard(EntI(pLocal, g_offTickBase));

    // a wall feature owns this cmd: EB runs LAST and a replay would stomp it
    // (botox: air stuck / wall climb / pixel surf / binds)
    const bool bTaken = s_textureBugHit || s_tbOwnsCmd || s_headSurfHit || s_headBounceHit
                     || s_airStuckHit || s_wallClimbHit || s_esActive
                     || s_shouldPixelSurf;   // a latched ps ride holds the stance; eb replay would flip it

    // botox edgebug.cpp:610: any style but 4 drops a flying lob plan outright
    if (g_edgeBugStyle != 4 && s_ebLob.found) { s_ebLob.found = false; s_ebLob.ticks = 0; s_ebLob.ducked = false; }

    if (const int style = g_edgeBugStyle; style != 0)
    {
        if (style == 4)
        {
            if (s_ebDonor.ebLength) EbDonorReset();
            if (s_ebHw.will) EbHwReset();
            EbLobPost(pLocal, pCmd, bTaken);
            return;
        }
        if (style == 3)
        {
            if (s_ebDonor.ebLength) EbDonorReset();
            EbHwPost(pLocal, pCmd, bTaken);
            return;
        }
        if (s_ebHw.will) EbHwReset();
        if (bTaken) { EbDonorReset(); return; }
        EbDonorPost(pLocal, pCmd, style);
        EbDonorMouseFix(pCmd);
        return;
    }
    if (s_ebHw.will) EbHwReset();
    if (s_ebDonor.ebLength) EbDonorReset();

    if (!On(g_edgeBug, g_edgeBugKey, g_edgeBugMode)) { EbDropPlan("key"); return; }
    const int mt = EntMoveType(pLocal);
    // ladder: live, or pre-pred (post-prediction a ladder tick can read back as walk), botox :634
    if (mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER
        || s_ebMoveType == MOVETYPE_LADDER || s_ebMoveType == MOVETYPE_NOCLIP) { EbDropPlan("move"); return; }
    if (bTaken) { EbDropPlan("taken"); return; }
    // m_ducked (botox :643-649): the plan's duck stays held past its replay, so the tick after the
    // bug does not unduck (air FinishUnDuck drops the feet 8.5u) under the confirm tick the search
    // proved. On the landing tick it also jumps, the duck still held (CSS crouch-jump)
    if (s_ebDucked)
    {
        if (s_ebFlags & FL_ONGROUND) pCmd->buttons |= IN_JUMP; else pCmd->buttons &= ~IN_JUMP;
        pCmd->buttons |= IN_DUCK;
    }
    if (s_ebFlags & FL_ONGROUND)
    {
        EbDropPlan("landed");   // a real bug never grounds
        s_ebLastSearchTick = -1;
        s_ebLastContactTick = -1;
        return;
    }
    // -40 is a real fall speed (botox :660): the fall restarted, the hold is over
    if (!s_ebDetected && s_ebDucked && s_ebVelBackup.z < -40.f) s_ebDucked = false;

    const int mode = std::clamp(g_edgeBugDetectMode, 0, 2);
    // botox m_edgebug_types: bit 0 ducked, bit 1 standing, none = both
    const int stance = g_edgeBugStance & 3;
    const bool allowDuck = stance == 0 || (stance & 1), allowStand = stance == 0 || (stance & 2);
    const bool steer = g_edgeBugMouseSteer;
    static Vec3 s_prevAngle{};
    Vec3 delta = pCmd->viewangles - s_prevAngle;
    s_prevAngle = pCmd->viewangles;
    delta.y = NormalizeAngle(delta.y);
    if (steer && mouseYaw != 0.f) delta.y = std::clamp(mouseYaw, -30.f, 30.f);
    auto isLargeDelta = [](float y1, float y2) { float d = fabsf(y1 - y2); if (d > 180.f) d = 360.f - d; return d >= 179.f; };

    const bool toEdge = g_edgeBugToEdge;
    const bool autostrafe = toEdge;   // to-edge IS the autostrafe switch
    int searchCount = mode == 1 ? 4 : 6;
    if (toEdge && searchCount < 8) searchCount = 8;
    if (!autostrafe && searchCount > 4) searchCount = 4;
    const float predictTime = std::clamp(g_edgeBugPredictTime, 0.02f, 2.f);
    const int maxTicks = std::clamp((int)std::round(predictTime / TickInterval()), 1, k_ebMaxTicks - 1);

    const Vec3 origView = pCmd->viewangles;
    const float userFwd = s_ebUserFwd, userSide = s_ebUserSide;
    const int userButtons = s_ebUserButtons & ~IN_JUMP;   // TF2: replay strips jump in air, so do the sims
    const float sideSpeed = CvarFloat("cl_sidespeed", 450.f);
    const float grav = CvarFloat("sv_gravity", 800.f);
    bool bSimmed = false;

    if (s_ebDetected && steer) s_ebSteerSince += delta.y;
    // re-plan: mouse turned >= 1 deg since the latch, never swap a bug 2 ticks out (botox :707)
    const bool replan = s_ebDetected && steer && fabsf(s_ebSteerSince) >= 1.f
                     && s_ebLockTicks - s_ebCurrentTick >= EbTicks(2);
    if (!s_ebDetected || replan) do
    {
        static int nVz = 0, nGap = 0, nAir = 0, nRun = 0, nHit = 0;
        if (static unsigned long long next = 0ull; GetTickCount64() >= next)
        {
            next = GetTickCount64() + 1000ull;
            if (nVz || nGap || nAir || nRun)
                DebugLog("EB: gates vz=%d gap=%d air=%d run=%d hit=%d", nVz, nGap, nAir, nRun, nHit);
            nVz = nGap = nAir = nRun = nHit = 0;
        }
        if (s_ebVelBackup.z >= (mode >= 1 ? -4.f : -10.f)) { nVz++; break; }

        // CADENCE (botox :723-732): every other tick at 128, back to every tick once a pass saw
        // ground within EbTicks(8). EbTicks(1) is 1 at CSS's 66.67, so CSS searches every tick
        const int nowTick = g_globalVars->tickcount;
        const bool everyTick = s_ebLastContactTick >= 0 && s_ebLastContactTick <= EbTicks(8);
        if (const int searchGap = EbTicks(1); searchGap > 1 && s_ebLastSearchTick >= 0
            && nowTick - s_ebLastSearchTick < searchGap && !everyTick)
        { nGap++; break; }
        s_ebLastSearchTick = nowTick;

        RestoreEntityToPredicted();
        bSimmed = true;
        const Vec3 liveOrigin = EntV3(pLocal, g_offOrigin);
        const Vec3 liveVel = EntV3(pLocal, g_offVelocity);
        const EbHull_t hull = EbMakeHull(pLocal, grav);
        const int reachTick = EbFirstReachTick(pLocal, hull, liveOrigin, liveVel, maxTicks);
        if (reachTick < 0) { s_ebLastContactTick = -1; nAir++; break; }
        const bool nearGeometry = everyTick || reachTick <= EbTicks(12);
        // a cadence-skipped tick's share goes to the tick that searches (botox gap_owned)
        const int gapOwned = everyTick ? 1 : std::clamp(EbTicks(1), 1, 2);

        s_ebSteerSince = 0.f;
        s_ebPlanCount = 0;
        const int wantPaths = steer ? std::clamp(g_edgeBugPaths, 1, k_ebPlanMax) : 1;
        auto enough = [&] { return s_ebPlanCount >= wantPaths; };
        if (liveVel.Length2D() > 30.f) { s_ebCourseYaw = RAD2DEGF(atan2f(liveVel.y, liveVel.x)); s_ebCourseValid = true; }
        else s_ebCourseValid = false;
        if (toEdge) EbFindEdgeTarget(pLocal, reachTick, maxTicks, grav);
        else s_ebEdgeTargetValid = false;
        nRun++;

        const int runTicks = std::min(maxTicks, reachTick + 1 + EbTicks(10));
        int totalPredictions = 0;
        // per-tick clock share (botox: 0.30 / 0.45 of a 64-tick tick, capped 0.60 / 0.75 of this
        // one), halved while geometry is far. search effort (botox m_edgebug_search_effort)
        // scales the ask and lifts the cap with it, overall clamped 0.9 (edgebug.cpp:762-766)
        const float effort = std::clamp(g_edgeBugSearchEffort, 0.25f, 3.f);
        const float wantS = (mode >= 1 ? 0.45f : 0.30f) * (1.f / 64.f) * (float)gapOwned * (nearGeometry ? 1.f : 0.5f) * effort;
        const float share = std::min(std::min(0.9f, (mode >= 1 ? 0.75f : 0.60f) * std::max(1.f, effort)),
                                     wantS / TickInterval());
        const int maxPredictions = std::clamp(std::max((int)(TickInterval() * 1000000.f * share / 8.f),
                                                       runTicks * (mode >= 1 ? 6 : 4)), 16, 2048);
        EbBudget_t budget; budget.Start(TickInterval() * share);
        long long worstRunUs = 0;
        // duck press window: CSS's 8.5u press vs a ~12u fall step at 66.67, 3 ticks of retry cover it
        const int duckRetry = std::clamp(EbTicks(3), 2, 8);
        int contactTick = -1;
        float contactZ = 0.f;   // origin.z at contactTick (botox contact_z, PASS 0b's lip-vs-lower-floor test)
        const float g = EbHalfGravTick(grav);
        const int probeEvery = std::clamp(EbTicks(2), 1, 8);
        int tailCut = 0, nStance = 0, nRejSurf = 0, nSlow = 0;
        const float airAccel = std::max(0.1f, CvarFloat("sv_airaccelerate", 10.f));
        constexpr float k_ebTurn = 4.f;
        const float ebCap = std::clamp(g_edgeBugStrafeAngleMax, 15.f, 180.f);
        const bool userMoveDead = fabsf(userFwd) < 0.1f && fabsf(userSide) < 0.1f
                               && fabsf(delta.y) < 0.01f && fabsf(delta.x) < 0.01f;
        const int approxSlack = std::max(2, EbTicks(2));

        // prefix reuse: no press window opens before reach + 1 - retry - slack (contact >= reach:
        // the reach hull is the grown one). fam 0 feeds presses, fam 1 releases
        Pred_SnapReserve(k_ebBendSnap + 16);
        for (auto& ring : s_ebResume) for (auto& e : ring) e.tick = -1;
        for (auto& e : s_ebBendRes) e.tick = -1;
        int bendResumes = 0, bendSkipped = 0;
        for (int& n : s_ebFamCmdN) n = 0;
        // brake window: one tick of air brake ~ accel * maxspeed * dt (~45 u/s at 66) moves the
        // contact point back ~0.7u per tick still to fly, so 8 ticks back ladder ~6u in sub-window
        // steps (single) and ~25u (held). Its resume states come from the same family ring
        const int brakeWin = std::clamp(EbTicks(8), 3, 14);
        const int famSaveFrom = std::max(1, reachTick + 1 - std::max(duckRetry, brakeWin) - approxSlack);
        const bool famWant[2] = { allowDuck, allowStand };
        int famSaves = 0, famResumes = 0, famSkipped = 0;
        const long long snapUs0 = Pred_SnapUs();

        // one pass. duckAt = press held from this tick (-1 = variant's own rule); undoAt = released
        // and left released (-1 = never); bend = first |bend| ticks pushed across the velocity;
        // brakeAt = tick j pushed against the velocity (hold = j to the end), -1 = never;
        // brakeAmt = its wish as a share of maxspeed (1 = full 450 push)
        bool lastRunFound = false;   // the last run met a bug (kept or not): bisect stops on it
        const float brakeMax = (g_offMaxspeed > 0 && EntF(pLocal, g_offMaxspeed) > 1.f) ? EntF(pLocal, g_offMaxspeed) : 450.f;
        auto runVariant = [&](int v, int duckAt, int undoAt = -1, float bend = 0.f, int brakeAt = -1, bool brakeHold = false,
                              float brakeAmt = 1.f)
        {
            const long long runStarted = budget.UsedUs();
            RestoreEntityToPredicted();
            s_ebStrafeSide = 1.f;
            s_ebStrafeLastYaw = origView.y;
            Vec3 searchVelBackup = EntV3(pLocal, g_offVelocity);
            UserCmd_t c = *pCmd;
            c.viewangles = origView; c.forwardmove = userFwd; c.sidemove = userSide; c.buttons = userButtons;
            Vec3 cur = origView;
            contactTick = -1; contactZ = 0.f;
            // family state: same restore-then-load as edge skip (movement_edge.inl EsGrab)
            const MvEbFam fam = MV_EbFamily(v, duckAt, undoAt, bend != 0.f ? 1 : 0, brakeAt);
            const int famIdx = fam.fam >= 0 ? (v >> 1) * 2 + fam.fam : -1;
            const bool famFeed = famIdx >= 0 && famWant[fam.fam];
            int i0 = 0;
            if (famIdx >= 0 && fam.resumeAt >= 1)
            {
                int best = -1;
                for (int r = 0; r < k_ebFamRing; r++)
                {
                    const int t = s_ebResume[famIdx][r].tick;
                    if (t >= 1 && t <= fam.resumeAt && t <= s_ebFamCmdN[famIdx]
                        && (best < 0 || t > s_ebResume[famIdx][best].tick)) best = r;
                }
                if (best >= 0 && Pred_SnapLoad(pLocal, k_ebSnapBase + famIdx * k_ebFamRing + best))
                {
                    const EbResume_t& e = s_ebResume[famIdx][best];
                    i0 = e.tick; c = e.c; cur = e.cur;
                    s_ebStrafeSide = e.side; s_ebStrafeLastYaw = e.lastYaw;
                    searchVelBackup = EntV3(pLocal, g_offVelocity);
                    memcpy(s_ebRun, s_ebFamCmds[famIdx], sizeof(EbCmd_t) * i0);
                    famResumes++; famSkipped += i0;
                }
            }
            // bend prefix: a smaller bend of this pair already flew the pushes up to its tick.
            // Whole bends only: a fractional one (lip refine bisect) pushes its last tick short
            const int bendWhole = MV_EbBendWhole(bend) ? (int)bend : 0;
            const int bendSlot = MV_EbBendSlot(v, bendWhole);
            if (bendSlot >= 0 && i0 == 0 && MV_EbBendCanResume(s_ebBendRes[bendSlot].tick, bendWhole)
                && Pred_SnapLoad(pLocal, k_ebBendSnap + bendSlot))
            {
                const EbResume_t& e = s_ebBendRes[bendSlot];
                i0 = e.tick; c = e.c; cur = e.cur;
                s_ebStrafeSide = e.side; s_ebStrafeLastYaw = e.lastYaw;
                searchVelBackup = EntV3(pLocal, g_offVelocity);
                memcpy(s_ebRun, s_ebBendCmds[bendSlot], sizeof(EbCmd_t) * i0);
                bendResumes++; bendSkipped += i0;
            }
            auto replayUserMove = [&](bool duck)
            {
                if (duck) c.buttons |= IN_DUCK; else c.buttons &= ~IN_DUCK;
                c.forwardmove = userFwd; c.sidemove = userSide;
                if (!isLargeDelta(cur.y, origView.y)) cur = EbClampAng(cur + delta);
                EbFold(&c, cur, origView);
            };
            auto strafe = [&](bool duck)
            {
                if (steer && !isLargeDelta(cur.y, origView.y)) { cur = EbClampAng(cur + delta); c.viewangles = cur; }
                EbApplyAutoStrafe(pLocal, &c, duck, sideSpeed);
            };
            auto turnMove = [&](float step, bool duck)
            {
                cur.y = NormalizeAngle(cur.y + step);
                if (duck) c.buttons |= IN_DUCK; else c.buttons &= ~IN_DUCK;
                c.forwardmove = userMoveDead ? 450.f : userFwd;
                c.sidemove = userSide;
                EbFold(&c, cur, origView);
            };
            auto sideWish = [&](float off, bool duck)
            {
                if (duck) c.buttons |= IN_DUCK; else c.buttons &= ~IN_DUCK;
                const Vec3 wish(0.f, NormalizeAngle(origView.y + off), 0.f);
                c.forwardmove = 0.f;
                c.sidemove = off > 0.f ? -450.f : 450.f;
                EbCommitStrafe(pLocal, &c, wish);
            };
            // an edgebug KEEPS FALLING: hold a candidate one tick and sim the next one
            int pending = -1;
            Vec3 pendingPos{};
            float pendingVz = 0.f;
            bool pendingDucked = false;
            bool runFound = false;
            auto latch = [&](int tickIndex, const Vec3& endPos, bool ducked)
            {
                runFound = true;
                if (!MV_EbSpeedOk(liveVel.Length2D(), EntV3(pLocal, g_offVelocity).Length2D())) { nSlow++; return; }
                // same variant landing within 4u of a kept plan = same path, not an alternative
                for (int p = 0; p < s_ebPlanCount; p++)
                    if (s_ebPlans[p].variant == v && s_ebPlans[p].end.DistTo(endPos) < 4.f) return;
                if (s_ebPlanCount >= k_ebPlanMax) return;
                EbPlan_t& pl = s_ebPlans[s_ebPlanCount++];
                memcpy(pl.cmds, s_ebRun, sizeof(EbCmd_t) * (tickIndex + 1));
                pl.ticks = tickIndex; pl.variant = v; pl.bend = bend; pl.brake = brakeAt; pl.end = endPos;
                pl.ducked = ducked;
            };
            for (int i = i0; i < runTicks || (pending >= 0 && i < maxTicks); i++)
            {
                {
                    const bool grounded = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
                    if (grounded || EntV3(pLocal, g_offVelocity).z > 0.f)
                    {
                        if (grounded) { contactTick = i; contactZ = EntV3(pLocal, g_offOrigin).z; }
                        pending = -1;
                        break;
                    }
                }
                // keep this tick's family state (never mid-confirm: pending is run-local)
                if (famFeed && pending < 0 && i >= famSaveFrom && i < fam.limit && i <= s_ebFamCmdN[famIdx])
                {
                    const int r = i % k_ebFamRing;
                    EbResume_t& e = s_ebResume[famIdx][r];
                    if (e.tick != i && Pred_SnapSave(pLocal, k_ebSnapBase + famIdx * k_ebFamRing + r))
                    {
                        e.tick = i; e.c = c; e.cur = cur; e.side = s_ebStrafeSide; e.lastYaw = s_ebStrafeLastYaw;
                        famSaves++;
                    }
                }
                // bend: state after all |bend| pushes, for the next bigger bend of the pair
                if (bendSlot >= 0 && pending < 0 && i == abs(bendWhole) && i <= k_ebBendMax
                    && s_ebBendRes[bendSlot].tick != i && Pred_SnapSave(pLocal, k_ebBendSnap + bendSlot))
                {
                    EbResume_t& e = s_ebBendRes[bendSlot];
                    e.tick = i; e.c = c; e.cur = cur; e.side = s_ebStrafeSide; e.lastYaw = s_ebStrafeLastYaw;
                    memcpy(s_ebBendCmds[bendSlot], s_ebRun, sizeof(EbCmd_t) * i);
                }
                switch (v)
                {
                case 0: c.buttons &= ~IN_DUCK; c.forwardmove = 0.f; c.sidemove = 0.f; break;
                case 1: c.buttons |= IN_DUCK;  c.forwardmove = 0.f; c.sidemove = 0.f; break;
                case 2: case 3: if (autostrafe) strafe(v == 3); else replayUserMove(v == 3); break;
                case 4: case 5: if (autostrafe) replayUserMove(v == 5); else strafe(v == 5); break;
                case 6: if (toEdge) EbApplyToEdge(pLocal, &c, false, sideSpeed); else strafe(false); break;
                case 7: if (toEdge) EbApplyToEdge(pLocal, &c, true, sideSpeed); else strafe(true); break;
                case 8:  turnMove(k_ebTurn, false); break;
                case 9:  turnMove(-k_ebTurn, true); break;
                case 10: turnMove(-k_ebTurn, false); break;
                case 11: turnMove(k_ebTurn, true); break;
                case 12: sideWish(ebCap, false); break;
                case 13: sideWish(-ebCap, true); break;
                case 14: sideWish(sinf((i + 1) * 0.85f) * ebCap * 0.65f, true); break;
                case 15: strafe((i & 1) != 0); break;
                }
                // square to velocity: +30 u/s lateral a tick, speed kept, cone capped. A fractional
                // last tick buys that fraction of the 30 (MV_EbBendPush, botox :1019-1032)
                if (const float push = MV_EbBendPush(bend, i, airAccel, TickInterval()); push >= 0.f)
                {
                    const Vec3 vel = EntV3(pLocal, g_offVelocity);
                    if (vel.Length2D() > 1.f)
                    {
                        const Vec3 wish(0.f, RAD2DEGF(atan2f(vel.y, vel.x)) + (bend > 0.f ? -90.f : 90.f), 0.f);
                        c.forwardmove = push; c.sidemove = 0.f;
                        EbCommitStrafe(pLocal, &c, wish);
                    }
                }
                // brake: wish against velocity (air accel takes the full wishspeed, not the 30 cap).
                // Not on the confirm tick: that one is the player's own cmd past the replay
                if (brakeAt >= 0 && pending < 0 && (i == brakeAt || (brakeHold && i > brakeAt)))
                {
                    const Vec3 vel = EntV3(pLocal, g_offVelocity);
                    if (vel.Length2D() > 1.f)
                    {
                        const Vec3 wish(0.f, NormalizeAngle(RAD2DEGF(atan2f(vel.y, vel.x)) + 180.f), 0.f);
                        c.forwardmove = brakeAmt >= 1.f ? 450.f : brakeAmt * brakeMax; c.sidemove = 0.f;
                        EbCommitStrafe(pLocal, &c, wish);
                    }
                }
                if (duckAt >= 0) { if (i < duckAt) c.buttons &= ~IN_DUCK; else c.buttons |= IN_DUCK; }
                // release = the -8.5u knob: airborne duck / unduck finish the same tick in CSS
                // (CCSGameMovement::Duck, cs_gamemovement.cpp:955-959 / :991-994), FinishUnDuck :800
                if (undoAt >= 0 && i >= undoAt) c.buttons &= ~IN_DUCK;
                // confirm tick is past the replay: real duck = plan's last bit (m_run_cmds[pending].buttons,
                // botox edgebug.cpp:1035-1042) OR the user's own, never this variant's overlay (a press here
                // would fake "still airborne"). CSS allows a ducked ground-jump too (crouch-jump, no duck
                // gate in CCSGameMovement::CheckJumpButton) so the TF2 excuse to drop the plan's hold doesn't apply.
                if (pending >= 0) { if ((s_ebRun[pending].buttons | userButtons) & IN_DUCK) c.buttons |= IN_DUCK; else c.buttons &= ~IN_DUCK; }
                s_ebRun[i] = { c.viewangles, c.forwardmove, c.sidemove, c.buttons, EntV3(pLocal, g_offOrigin) };
                if (famIdx >= 0 && pending < 0 && i < fam.limit && i <= s_ebFamCmdN[famIdx])
                {
                    s_ebFamCmds[famIdx][i] = s_ebRun[i];
                    s_ebFamCmdN[famIdx] = std::max(s_ebFamCmdN[famIdx], i + 1);
                }
                if (totalPredictions >= maxPredictions || budget.Expired()) break;
                Pred_Simulate(pLocal, &c);
                totalPredictions++;
                // ladder grab nulls vz like an edgebug: drop the variant before the verdict (botox :1064)
                if (const int smt = EntMoveType(pLocal); smt == MOVETYPE_LADDER || smt == MOVETYPE_NOCLIP || smt == MOVETYPE_OBSERVER)
                { pending = -1; break; }
                if (pending >= 0)
                {
                    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) { contactTick = i + 1; contactZ = EntV3(pLocal, g_offOrigin).z; }
                    else if (const float vzNow = EntV3(pLocal, g_offVelocity).z;
                             vzNow <= 0.f && (mode != 2 || roundf(vzNow) == roundf(pendingVz - 2.f * g)))
                        latch(pending, pendingPos, pendingDucked);
                    pending = -1;
                    break;
                }
                const Vec3 velNow = EntV3(pLocal, g_offVelocity);
                const int flNow = EntI(pLocal, g_offFlags);
                if (EbCheck(pLocal, searchVelBackup.z, velNow.z, g, (flNow & FL_ONGROUND) != 0, mode))
                {
                    // ramp scrape, not a lip: drop the candidate, run keeps falling (the real lip is
                    // usually a few ticks down). s_ebRun[i].origin + searchVelBackup = tick i pre-sim
                    const bool bugDuck = (flNow & FL_DUCKING) != 0;
                    if (!MV_EbStanceOk(stance, bugDuck)) nStance++;   // botox stance_rej: run keeps falling
                    else if (!EbSurfaceUsable(pLocal, s_ebRun[i].origin, searchVelBackup)) nRejSurf++;
                    else if (i + 1 >= maxTicks) { latch(i, EntV3(pLocal, g_offOrigin), (flNow & FL_DUCKING) != 0); break; }
                    else
                    {
                        pending = i;
                        pendingPos = EntV3(pLocal, g_offOrigin);
                        pendingVz = velNow.z;
                        pendingDucked = (flNow & FL_DUCKING) != 0;
                    }
                }
                searchVelBackup = velNow;
                // tail probe: past reach tick, nothing pending, ticks left worth saving
                if (pending < 0 && i > reachTick && i + probeEvery < runTicks && ((i - reachTick) % probeEvery) == 0
                    && EbFirstReachTick(pLocal, hull, EntV3(pLocal, g_offOrigin), searchVelBackup, runTicks - i - 1) < 0)
                { tailCut += runTicks - i - 1; break; }
            }
            if (!runFound && pending >= 0) latch(pending, pendingPos, pendingDucked);
            lastRunFound = runFound;
            if (const long long spent = budget.UsedUs() - runStarted; spent > worstRunUs) worstRunUs = spent;
        };

        int bestContact = -1;
        const int replayVariant = autostrafe ? 4 : 2;
        // BREADTH FIRST: every variant once, THEN the duck sweeps
        int sweepCenter[8]; for (int& x : sweepCenter) x = -1;
        int baseContact[8]; for (int& x : baseContact) x = -1;
        float baseContactZ[8]{};
        bool sweepApprox[8]{}, baseRan[8]{};
        int baseRuns = 0, sweepRuns = 0, releaseRuns = 0, bendRuns = 0, ebRuns = 0, refineRuns = 0;
        const int firstVariant = (s_ebLastVariant > 0 && s_ebLastVariant < searchCount) ? s_ebLastVariant : 0;
        auto variantAt = [&](int idx) { return idx == 0 ? firstVariant : (idx == firstVariant ? 0 : idx); };
        auto outOfBudget = [&] { return totalPredictions > 0 && (totalPredictions >= maxPredictions || budget.CannotFit(worstRunUs)); };
        // pass 0: every variant once
        for (int idx = 0; idx < searchCount && !enough(); idx++)
        {
            const int v = variantAt(idx);
            if (userMoveDead && (v == replayVariant || v == replayVariant + 1)) continue;
            if ((!toEdge || !s_ebEdgeTargetValid) && (v == 6 || v == 7)) continue;
            if (!allowDuck && (v & 1) && v < 8) continue;   // standing only: ducked twins land ducked
            if (outOfBudget()) break;
            runVariant(v, -1);
            baseRuns++;
            baseRan[v] = true;
            baseContact[v] = contactTick;
            baseContactZ[v] = contactZ;
            if (contactTick > bestContact) bestContact = contactTick;
            sweepCenter[v] = contactTick > 0 ? contactTick : (reachTick >= 0 ? reachTick + 1 : -1);
            sweepApprox[v] = contactTick <= 0;
        }
        // PASS 0b: LIP REFINE (botox edgebug.cpp:1180-1241). Zero-move (stand v=0 / duck v=1) pushed
        // square to the path for b ticks (reuses runVariant's own `bend` push, same as PASS 2's):
        // grid +-2/+-6/+-12 finds a landing vs a miss, then bisects the boundary `refineSteps` times.
        // steers = to-edge only (autostrafe), near geometry, no plan yet.
        if (g_edgeBugLipRefine && autostrafe && nearGeometry && s_ebPlanCount == 0)
        {
            const int steps = std::clamp(g_edgeBugRefineSteps, 1, 12);
            constexpr float k_refineGrid[] = { 2.f, -2.f, 6.f, -6.f, 12.f, -12.f };
            for (int v = 0; v < 2 && s_ebPlanCount == 0 && !outOfBudget(); v++)
            {
                if (!baseRan[v] || (v == 0 && !allowStand)) continue;   // v=0 stand skipped when "ducked only"
                float val[7], cz[7]; int con[7], n = 0;
                val[0] = 0.f; con[0] = baseContact[v]; cz[0] = baseContactZ[v]; n = 1;
                for (float gOff : k_refineGrid)
                {
                    if (s_ebPlanCount || outOfBudget() || fabsf(gOff) >= (float)runTicks) break;
                    runVariant(v, -1, -1, gOff);
                    refineRuns++;
                    val[n] = gOff; con[n] = contactTick; cz[n++] = contactZ;
                }
                int idx[7]; for (int j = 0; j < n; j++) idx[j] = j;
                std::sort(idx, idx + n, [&](int a, int b) { return val[a] < val[b]; });
                int order[6], no = 0;
                for (int j = 0; j + 1 < n; j++) order[no++] = j;
                std::sort(order, order + no, [&](int a, int b)
                {
                    return fabsf(val[idx[a]] + val[idx[a + 1]]) < fabsf(val[idx[b]] + val[idx[b + 1]]);
                });
                int nearLand = -1;
                for (int j = 0; j < n; j++)
                    if (con[j] >= 0 && (nearLand < 0 || fabsf(val[j]) < fabsf(val[nearLand]))) nearLand = j;
                const float lip = nearLand >= 0 ? cz[nearLand] : 0.f;
                auto onLip = [&](int j) { return con[j] >= 0 && fabsf(cz[j] - lip) < 1.f; };
                for (int q = 0; q < no && nearLand >= 0 && s_ebPlanCount == 0 && !outOfBudget(); q++)
                {
                    const int ja = idx[order[q]], jb = idx[order[q] + 1];
                    const bool la = onLip(ja), lb = onLip(jb);
                    if (la == lb) continue;
                    float land = la ? val[ja] : val[jb], miss = la ? val[jb] : val[ja];
                    for (int st = 0; st < steps && s_ebPlanCount == 0 && !outOfBudget(); st++)
                    {
                        const float mid = 0.5f * (land + miss);
                        runVariant(v, -1, -1, mid);
                        refineRuns++;
                        if (contactTick >= 0 && fabsf(contactZ - lip) < 1.f) land = mid; else miss = mid;
                    }
                }
            }
        }
        // PASS 1: DUCK TIMING, even variants, round robin on the offset (latest press first)
        int sweepFirst[8]{}, sweepLast[8]{};
        int maxOff = -1;
        if (g_edgeBugDuckTiming && !enough())
        {
            for (int v = 0; v < 8; v += 2)
            {
                const MvEbPress w = MV_EbSweepWindow(sweepCenter[v], sweepApprox[v], runTicks, baseRan[v + 1], duckRetry, approxSlack);
                sweepFirst[v] = w.first; sweepLast[v] = w.last;
                if (w.last >= 0 && w.last - w.first > maxOff) maxOff = w.last - w.first;
            }
            for (int off = 0; off <= maxOff && !enough(); off++)
            {
                for (int idx = 0; idx < searchCount && !enough(); idx++)
                {
                    const int v = variantAt(idx);
                    if (v & 1) continue;
                    const int k = sweepLast[v] - off;
                    if (sweepLast[v] < 0 || k < sweepFirst[v]) continue;
                    if (outOfBudget()) { off = maxOff; break; }
                    // a press lands ducked: standing only skips it, the release twin is the point
                    if (allowDuck) { runVariant(v, k); sweepRuns++; }
                    // release at k (-20u family): ducked from 0, released at k, never re-pressed
                    if (allowStand && !enough() && k >= 1 && !outOfBudget()) { runVariant(v, 0, k); releaseRuns++; }
                }
            }
        }
        // PASS 1b: FULL WINDOW (botox edgebug.cpp:1292-1312). Near ticks done (PASS 1's sweep); the
        // rest of the window, latest first, duck press/release only -- no steer. duck timing only.
        int wideRuns = 0;
        if (g_edgeBugDuckTiming && g_edgeBugFullWindow && !enough())
        {
            for (int k = runTicks - 1; k >= 1 && !enough() && !outOfBudget(); k--)
                for (int idx = 0; idx < searchCount && !enough() && !outOfBudget(); idx++)
                {
                    const int v = variantAt(idx);
                    if ((v & 1) || !baseRan[v] || (sweepLast[v] >= 0 && k >= sweepFirst[v] && k <= sweepLast[v])) continue;
                    if (allowDuck) { runVariant(v, k); wideRuns++; }
                    if (allowStand && !enough() && (baseRan[v + 1] || !allowDuck) && !outOfBudget())
                    { runVariant(v, 0, k); wideRuns++; }
                }
        }
        // deep passes (bend, ebanat) get the full share only with a base contact close; far / none
        // stop at half and the bend order carries over to the next search (TF2, eb_stance.h)
        const bool deepFull = MV_EbDeepFull(bestContact, EbTicks(6));
        auto deepOut = [&] { return outOfBudget() || (!deepFull && budget.UsedUs() + worstRunUs >= budget.capUs / 2); };
        // PASS 1b: BRAKE (TF2, not botox). Bend only moves the path ACROSS itself, so a lip that
        // crosses the path (a ledge you fall past, a crate top you fall onto) was only ever caught
        // when a base run happened to clip it. Braking moves the contact point back along the path:
        // Each run resumes its base run's state at j.
        // BISECT first: hold from the window start at strength a (a * maxspeed of wish, dv scales
        // with it). a = 0 is the base run, a = 1 the full brake; one lands and the other flies past =
        // the bug band lies between (fly past | bug | land) -> halve 7 times, ~0.3u of a ~37u reach.
        // Short falls need it: at -215 u/s a tick falls 3.2u and CategorizePosition grounds anything
        // within 2u of the lip, so the band is under ~1.7u wide, finer than any fixed ladder.
        // LADDERS after, with what is left: single = one tick at j (0.7u steps), hold = j to the end
        // (coarse). Latest j first = smallest shift first, variants round robin.
        int brakeRuns = 0, bisectRuns = 0;
        // half of what the deep passes may spend: bend finds what braking can't (lips along the path)
        auto brakeOut = [&] { return deepOut() || totalPredictions * 2 >= maxPredictions
                                  || (budget.UsedUs() + worstRunUs) * (deepFull ? 2 : 4) >= budget.capUs; };
        if (autostrafe && nearGeometry && !enough())
        {
            int brakeFirst[8]{}, brakeLast[8]{};
            int brakeOff = -1;
            for (int v = 0; v < 8; v++)
            {
                const MvEbPress w = baseRan[v] ? MV_EbSweepWindow(sweepCenter[v], sweepApprox[v], runTicks, false, brakeWin, approxSlack)
                                               : MvEbPress{ 0, -1 };
                brakeFirst[v] = w.first; brakeLast[v] = w.last;
                if (w.last >= 0 && w.last - w.first > brakeOff) brakeOff = w.last - w.first;
            }
            for (int idx = 0; idx < searchCount && !enough() && !brakeOut(); idx++)
            {
                const int v = variantAt(idx);
                if (v >= 8 || brakeLast[v] < 0) continue;
                const int j0 = brakeFirst[v];
                bisectRuns += MV_EbBisect(!sweepApprox[v], 7, [&](float a)
                {
                    runVariant(v, -1, -1, 0, j0, true, a);
                    return lastRunFound ? kEbOutBug : contactTick >= 0 ? kEbOutLand : kEbOutAir;
                }, [&] { return enough() || brakeOut(); });
            }
            for (int off = 0; off <= brakeOff && !enough() && !brakeOut(); off++)
                for (int idx = 0; idx < searchCount && !enough() && !brakeOut(); idx++)
                {
                    const int v = variantAt(idx);
                    if (v >= 8 || brakeLast[v] < 0 || brakeLast[v] - off < brakeFirst[v]) continue;
                    const int j = brakeLast[v] - off;
                    runVariant(v, -1, -1, 0, j, false);
                    brakeRuns++;
                    if (enough() || brakeOut()) break;
                    runVariant(v, -1, -1, 0, j, true);
                    brakeRuns++;
                }
        }
        // PASS 2: BEND, smallest first, near geometry + no usable plan yet
        int bendStart = -1;
        if (g_edgeBugPathBend && autostrafe && nearGeometry && s_ebPlanCount == 0)
        {
            const int total = 2 * (int)std::size(k_ebBend) * searchCount;
            bendStart = MV_EbBendStart(s_ebBendCursor, total, deepFull);
            int walked = 0;
            for (; walked < total && !enough() && !deepOut(); walked++)
            {
                const MvEbBendAt e = MV_EbBendEntry((bendStart + walked) % total, searchCount, k_ebBend);
                const int v = variantAt(e.idx);
                if (abs(e.bend) >= runTicks || !baseRan[v]) continue;
                runVariant(v, -1, -1, e.bend);
                bendRuns++;
            }
            s_ebBendCursor = MV_EbBendNext(bendStart, walked, total, enough());
        }
        // PASS 3: EBANAT SHAPES, zero usable plans only
        if (g_edgeBugEbanatShapes && autostrafe && nearGeometry && s_ebPlanCount == 0)
            for (int v = 8; v < 16 && !enough() && !deepOut(); v++)
            {
                if (!allowDuck && (v == 9 || v == 11 || v == 13 || v == 14)) continue;
                runVariant(v, -1);
                ebRuns++;
            }
        s_ebLastContactTick = bestContact >= 0 ? bestContact : reachTick;

        // pick: heading closest to where the mouse turns (aim = view + half the turn over its flight)
        auto aimAt = [&](int ticks) { return origView.y + std::clamp(delta.y * (ticks + 1) * 0.5f, -90.f, 90.f); };
        float err[k_ebPlanMax];
        for (int p = 0; p < s_ebPlanCount; p++)
            err[p] = EbAimErr(s_ebPlans[p].cmds[0].origin, s_ebPlans[p].end, aimAt(s_ebPlans[p].ticks));
        int sel = MV_EbPickPlan(err, s_ebPlanCount);
        if (replan && sel >= 0)
        {
            const float curErr = EbAimErr(s_ebCmds[std::min(s_ebCurrentTick, s_ebLockTicks)].origin, s_ebFollowEnd,
                                          aimAt(s_ebLockTicks - s_ebCurrentTick));
            const bool keep = MV_EbKeepPlan(curErr, err[sel]);
            DebugLog("EB: replan %s at %d/%d best=%.1f cur=%.1f of %d steer=%.2f",
                     keep ? "keep" : "swap", s_ebCurrentTick, s_ebLockTicks, err[sel], curErr, s_ebPlanCount, delta.y);
            if (keep) sel = -1;
        }
        if (sel >= 0)
        {
            const EbPlan_t& pl = s_ebPlans[sel];
            memcpy(s_ebCmds, pl.cmds, sizeof(EbCmd_t) * (pl.ticks + 1));
            s_ebDetected = true;
            s_ebLockTicks = pl.ticks;
            s_ebFoundTick = nowTick + pl.ticks + 1;
            s_ebCurrentTick = 0;
            s_ebFollowEnd = pl.end;
            s_ebLastVariant = pl.variant;
            s_ebLastBend = pl.bend;
            if (g_edgeBugViz)
            {
                static Vec3 pts[k_ebMaxTicks + 1];
                int n = 0;
                for (int p = 0; p <= pl.ticks; p++) pts[n++] = pl.cmds[p].origin;
                pts[n++] = pl.end;
                EbVizSet(pts, n);
            }
            nHit++;
        }
        // win = slider window, run = trimmed window, reach = first hull contact, edge = lip found,
        // base/sw/rel/bnd/ebv = runs per pass, sims = spent/ceiling, us = spent/share, con = closest
        // floor, stc = bug ticks the stance refused, surf = ramp scrapes refused (face nz < 0.7),
        // duck = latched hull, bnd = picked plan's bend (fraction = refine bisect),
        // pre = prefix snapshots saved / runs resumed / sim ticks skipped, snap = their copy cost us,
        // bre = bend runs resumed / ticks skipped, deep = 1 full / 0 half share for bend + ebanat,
        // bc = bend order start (-1 = pass skipped), brk / bis = brake ladder / bisect runs, bk = picked plan's brake tick,
        // slow = finds refused for bleeding 2D speed (MV_EbSpeedOk)
        DebugLog("EB: mode=%d win=%d run=%d reach=%d edge=%d vars=%d base=%d rf=%d sw=%d wd=%d rel=%d brk=%d bis=%d bnd=%d ebv=%d sims=%d/%d cut=%d "
                 "us=%lld/%lld con=%d vz=%.2f found=%d var=%d bk=%d at=%d pth=%d/%d stc=%d surf=%d duck=%d bend=%.2f "
                 "pre=%d/%d/%d snap=%lld bre=%d/%d deep=%d bc=%d slow=%d",
                 mode, maxTicks, runTicks, reachTick, s_ebEdgeTargetValid ? 1 : 0, searchCount, baseRuns, refineRuns, sweepRuns, wideRuns,
                 releaseRuns, brakeRuns, bisectRuns, bendRuns, ebRuns, totalPredictions, maxPredictions, tailCut, budget.UsedUs(), budget.capUs,
                 bestContact, s_ebVelBackup.z, s_ebDetected ? 1 : 0, s_ebLastVariant,
                 sel >= 0 ? s_ebPlans[sel].brake : -1, s_ebDetected ? s_ebLockTicks : -1,
                 s_ebPlanCount, wantPaths, nStance, nRejSurf,
                 sel >= 0 ? (s_ebPlans[sel].ducked ? 1 : 0) : -1, sel >= 0 ? s_ebPlans[sel].bend : 0.f,
                 famSaves, famResumes, famSkipped, Pred_SnapUs() - snapUs0,
                 bendResumes, bendSkipped, deepFull ? 1 : 0, bendStart, nSlow);
    } while (false);

    if (s_ebDetected)
    {
        RestoreEntityToPredicted();
        const bool spent = s_ebCurrentTick > s_ebLockTicks;
        const float dist = spent ? 0.f : EntV3(pLocal, g_offOrigin).DistTo(s_ebCmds[s_ebCurrentTick].origin);
        if (spent || dist > 1.f)
        {
            // dist = plan vs world disagree: the REPLAY diverged (another cmd writer or a correction).
            // s_ebFlags = START of this cmd = END of the bug cmd: real stance vs the sim's
            if (spent) DebugLog("EB: drop why=spent at %d/%d duck=%d hold=%d vz=%.2f", s_ebCurrentTick, s_ebLockTicks,
                                (s_ebFlags & FL_DUCKING) ? 1 : 0, s_ebDucked ? 1 : 0, s_ebVelBackup.z);
            else DebugLog("EB: replay drop at %d/%d dist=%.3f", s_ebCurrentTick, s_ebLockTicks, dist);
            s_ebDetected = false;
            s_ebCurrentTick = 0;
            return;
        }
        const EbCmd_t& e = s_ebCmds[s_ebCurrentTick];
        pCmd->buttons = (pCmd->buttons & ~(IN_DUCK | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))
                      | (e.buttons & (IN_DUCK | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT));
        pCmd->buttons &= ~IN_JUMP;   // airborne (landed returned above)
        s_ebDucked = (e.buttons & IN_DUCK) != 0;   // botox m_ducked: held past the replay, dropped with it
        pCmd->forwardmove = e.fwd;
        pCmd->sidemove = e.side;
        // always silent: the replay's angle folded into fwd/side, the real view handed back
        EbFold(pCmd, e.view, origView);
        s_ebCurrentTick++;
    }
    else if (bSimmed)
        RestoreEntityToPredicted();   // never leave a sim tick live
}

// ============================================================================
//  IClientMode::OverrideMouseInput (slot 20: SDK iclientmode.h, between the confirmed
//  OverrideView 16 and CreateMove 21). botox override_mouse_input.cpp: feed the mouse steer and
//  scale the turn while a plan flies.
// ============================================================================
void EdgeBug_MouseInput(float* x, float* y)
{
    if (!x) return;
    if (g_edgeBug)
    {
        s_ebMouseX = std::clamp(s_ebMouseX + *x, -100000.f, 100000.f);
        const int style = g_edgeBugStyle;
        const bool live = style == 1 ? s_ebDonor.ticksLeft != 0 : s_ebDetected;
        if (live && style == 3) EbHwMouse(x, y);   // botox: hotwheels always locks, its own curve
        else if (live && style == 4) EbLobMouseScale(x);   // botox lob_mouse: its own curve, not the generic combo lock
        else if (live && g_edgeBugMouseLock)
        {
            const float strength = std::clamp(g_edgeBugLockStrength, 0.f, 100.f) / 100.f;
            const int type = g_edgeBugLockType;
            if (style != 0)
            {
                const float keep = type == 1 ? 0.f : 1.f - strength;
                *x *= keep;
                if (style == 2 && type == 0 && y) *y *= keep;
            }
            else
            {
                const float progress = s_ebLockTicks > 0
                    ? std::clamp((float)(s_ebCurrentTick + 1) / (float)(s_ebLockTicks + 1), 0.f, 1.f) : 1.f;
                *x *= 1.f - strength * (type == 0 ? progress : 1.f);
            }
        }
    }
    // edge skip (botox n_edge_skip::impl_t::lock_amount, edge_skip.cpp:56-65): independent of edge
    // bug's own toggle -- borrows g_edgeBug* knobs when g_edgeSkipUseEb, else its own. type 0 =
    // smooth (ramps with ticks flown / ticks left), 1 = full (instant, matches botox's "full")
    if (g_edgeSkip && s_esActive)
    {
        const bool  lockOn   = g_edgeSkipUseEb ? g_edgeBugMouseLock : g_edgeSkipMouseLock;
        const int   lockType = g_edgeSkipUseEb ? g_edgeBugLockType : g_edgeSkipMouseLockType;
        const float strength = std::clamp(g_edgeSkipUseEb ? g_edgeBugLockStrength : g_edgeSkipMouseLockStrength,
                                          0.f, 100.f) / 100.f;
        if (lockOn)
        {
            const float ramp = lockType == 0
                ? std::clamp((float)(s_esHit + 1) / (float)(s_esLast + 1), 0.f, 1.f) : 1.f;
            *x *= 1.f - strength * ramp;
        }
    }
}

using OverrideMouseInput_t = void(__fastcall*)(void*, float*, float*);
static OverrideMouseInput_t o_OverrideMouseInput = nullptr;
static void __fastcall h_OverrideMouseInput(void* self, float* x, float* y)
{
    o_OverrideMouseInput(self, x, y);
    __try { EdgeBug_MouseInput(x, y); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try { Recorder_MouseInput(x, y); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // botox camera_lock
}

void* ScreenFx_FindClientMode();
void EdgeBug_InstallMouseHook()
{
    if (o_OverrideMouseInput) return;
    if (void* mode = ScreenFx_FindClientMode())
        SwapVTableHook(mode, 20, "edgebug: OverrideMouseInput",
                       (void*)h_OverrideMouseInput, (void**)&o_OverrideMouseInput);
}
