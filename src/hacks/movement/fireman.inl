// ==== fireman (ladder catch) -- CSS rebuild 11-07, from css-community gamemovement.cpp ====
//
// The CSS engine rules the whole feature stands on:
//   * LadderMove (gamemovement.cpp:2854) runs at the top of EVERY PlayerMove, before WalkMove.
//     Not attached: needs fwd or side != 0, wishdir = m_vecForward*fwd + m_vecRight*side with the
//     FULL view (pitch included), 2u hull trace along it (LadderDistance, cs_gamemovement.cpp:54),
//     LadderMask, OnLadder = CONTENTS_LADDER or climbable surfaceprop, normal.z != 1.
//   * IN_JUMP on that tick -> SetMoveType(WALK) + velocity = 270 * plane.normal (:2934-2939). The
//     grab AND the fling are ONE tick: the whole fall is deleted, no cooldown, no ride.
//   * Attached without jump: velocity is rebuilt from BUTTONS every tick (:2921-3010) -- no keys =
//     dead hang (Init), IN_BACK facing the face = 200 u/s straight down (MAX_CLIMB_SPEED).
//   * CCSGameMovement::CheckForLadders (cs_gamemovement.cpp:511, from AirMove) SNAPS you onto a
//     ladder you just dropped past (within 64u of where you left the ground, at or below it):
//     velocity Init, MOVETYPE_LADDER. Walking off a ladder-top ledge always lands here.
//
// So fireman is three states, all gated on the key + hands empty (your own move keys = you steer):
//   WALK, falling past a ladder (post-prediction, ENGINE SIMS decide):
//       steer -- station-keep the hull 1u off the face. Steer view pitch is 89: AirMove flattens
//                forward (full horizontal accel) but LadderMove's 2u grab trace points straight
//                down, so steering can never latch mid-shaft (the old build's dead hang).
//       grab  -- the last tick before the floor (or the last tick the ladder still reaches): view
//                (0, face, 0), forward 1 (any nonzero latches; 1 u/s barely brakes the fling) +
//                IN_JUMP. Only sent when a sim from START says it flings.
//   LADDER, attached by a fall (engine snap or ours): IN_FORWARD + IN_MOVERIGHT at pitch 89 with
//       the right vector on the face normal = 400 down (IN_BACK is 200; fireman::LadderVel). Ride
//       into the floor, then IN_JUMP inside the 2u ground touch = 270 fling + jump hop same tick.
//       Ladder ends above the floor -> fling on its last tick (vz reset to 0).
//   launch -- after either fling: jump on every touchdown for 64 ticks (fresh press each time,
//       CheckJumpButton refuses a held jump).
// Options ported from botox csgo fireman.cpp (11-09):
//   drop in    -- down-rays find a ladder column under / beside you (jump over it, walk off onto it),
//                 its face becomes the steer target, rising ticks too. "lock movement" eats your
//                 move keys + duck from the tick the column is seen until the fling (never jump).
//   sides only -- (own key, mode 0 = always) walk the face to its ends, pick the side whose park
//                 is nearest the braked drift, park there, and a grab counts only if its fling goes
//                 out of that side (dot > 0.9).
//   NOT ported: "push off early". CS:GO waits out a 0.2 s IGNORE_JUMP_TIME at the floor and pushes
//   off with IN_BACK; CSS LadderMove flings on the IN_JUMP tick itself (gamemovement.cpp:2933-2939,
//   no cooldown), and the ride already jumps inside the 2u floor touch -- nothing left to wait out.
// Log lines: "FM:".

static constexpr unsigned k_contentsLadderMv     = 0x20000000;   // bspflags.h CONTENTS_LADDER
static constexpr unsigned k_contentsPlayerClipMv = 0x10000;      // bspflags.h CONTENTS_PLAYERCLIP
// cs_gamemovement.cpp:61-64 LadderMask(), verbatim
static constexpr unsigned k_ladderMaskMv = MASK_PLAYERSOLID & ~k_contentsPlayerClipMv;
static constexpr int k_fmMoveBtns = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
static constexpr int k_fmLaunchTicks = 64;

bool g_fireMan = false; int g_fireManKey = 0; int g_fireManMode = 0;
bool  g_firemanApproach   = true;   // steer toward the ladder while falling
float g_firemanReach      = 320.f;  // ladder search radius, clamp 64-640
bool  g_firemanCrouchJump = true;   // IN_DUCK with the launch jumps
// botox 2026-08-15 "drop in": a ladder column under / beside you (jumped over, walked off onto) is
// found by down-rays, the drift is braked to a stop beside its face, rising ticks included
bool  g_firemanDropIn     = false;
bool  g_firemanDropInLock = true;   // drop in eats move keys + duck while it owns the fall (never jump)
// botox 2026-09-25 "sides only": park at the ladder's SIDE face, grab only it (fling goes sideways)
bool  g_firemanSideOnly   = false;
int   g_firemanSideOnlyKey = 0; int g_firemanSideOnlyMode = 0;

struct FiremanState_t
{
    int   fallTick = -1000;     // last tick airborne in WALK with the key held
    bool  onLadder = false;     // saw MOVETYPE_LADDER last ladder tick
    bool  fromFall = false;     // this attach came out of a fall -> ours to ride
    int   launchLeft = 0;
    bool  jumpLast = false;     // last cmd we wrote held IN_JUMP
    bool  owns = false;        // this cmd's move is ours: FiremanReassert re-applies it
    Vec3  view{}; float fwd = 0.f, side = 0.f, up = 0.f; int buttons = 0;
    // drop in
    bool  dropLock = false; int lockLost = 0, lockTicks = 0;
    bool  rideLock = false;     // attach came out of a locked drop: the ride eats keys too
    bool  dropColValid = false; Vec3 dropCol{};   // last column hit, re-probed first (a 1u strip)
    // sides only: picked once per ladder
    bool  sideValid = false; int sideGround = 0;
    Vec3  sidePoint{}, sideN{}, sideU{}; float sideHalf = 0.f, sideBack = 0.f; bool sidePress = false; float sideOv = 0.f;
};
static FiremanState_t s_fm{};
static float s_fmLastGroundZ = 0.f;   // outlives key resets: a key pressed mid-fall still knows the ledge
static bool s_firemanHit = false;   // indicator: grabbed / riding / flung this cmd

static int FmTick() { return g_globalVars ? g_globalVars->tickcount : 0; }

static bool FmHandsEmpty()
{
    return s_rawFwd == 0.f && s_rawSide == 0.f && !(s_rawButtons & k_fmMoveBtns);
}

static void FmOwn(const UserCmd_t* c)
{
    s_fm.owns = true;
    s_fm.view = c->viewangles; s_fm.fwd = c->forwardmove; s_fm.side = c->sidemove; s_fm.up = c->upmove;
    s_fm.buttons = c->buttons;
}

// re-applied at the end of the pipeline: a later feature rewriting the move would send the server
// a cmd that never latches (or latches mid-shaft)
static void FiremanReassert(UserCmd_t* pCmd)
{
    if (!s_fm.owns) return;
    constexpr int mask = k_fmMoveBtns | IN_JUMP | IN_DUCK;
    pCmd->viewangles  = s_fm.view;
    pCmd->forwardmove = s_fm.fwd;
    pCmd->sidemove    = s_fm.side;
    pCmd->upmove      = s_fm.up;
    pCmd->buttons     = (pCmd->buttons & ~mask) | (s_fm.buttons & mask);
}

// floor under the hull, MASK_PLAYERSOLID. -1 = start solid, 4096 = nothing below
static float FmFloorDepth(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(origin, origin - Vec3(0.f, 0.f, 4096.f), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
    if (tr.startsolid) return -1.f;
    return tr.fraction * 4096.f;
}

static bool FmIsLadderHit(const GameTrace_t& tr)
{
    if (tr.fraction >= 1.f || tr.startsolid || !(tr.contents & k_contentsLadderMv)) return false;
    const Vec3& n = tr.plane.normal;
    return fireman::CanFling(n.z, n.x * n.x + n.y * n.y + n.z * n.z);
}

// nearest ladder face around us: 16 yaws x up to 3 look-down slices (a shaft's ladder is often
// below+beside, invisible at feet height until too late). Slices never start under the floor.
static bool FmFindLadder(void* pLocal, const Vec3& origin, float floorZ, Vec3& outP, Vec3& outN, float* outNz = nullptr)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    const float reach = std::clamp(g_firemanReach, 64.f, 640.f);
    float best = 1e9f;
    for (int s = 0; s < 3; ++s)
    {
        Vec3 start = origin; start.z += 8.f - s * 96.f;
        if (start.z < floorZ + 4.f) break;
        for (int i = 0; i < 16; ++i)
        {
            const float yaw = DEG2RADF(i * 22.5f);
            GameTrace_t tr{};
            TraceLine(start, start + Vec3(cosf(yaw), sinf(yaw), 0.f) * reach, k_ladderMaskMv, &fil, &tr);
            if (!FmIsLadderHit(tr)) continue;
            const float d = tr.fraction * reach;
            if (d < best) { best = d; outP = tr.endpos; outN = tr.plane.normal; }
        }
    }
    if (best > 1e8f) return false;
    if (outNz) *outNz = outN.z;
    outN.z = 0.f;
    const float l = outN.Length2D();
    if (l < 1e-3f) return false;
    outN = outN * (1.f / l);
    return true;
}

// the face we are attached to: hull sweeps at 8 yaws over the attached LadderDistance (10u)
static bool FmLadderFace(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs, Vec3& outN)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    for (int i = 0; i < 8; ++i)
    {
        const float yaw = DEG2RADF(i * 45.f);
        GameTrace_t tr{};
        TraceHull(origin, origin + Vec3(cosf(yaw), sinf(yaw), 0.f) * 10.f, mins, maxs, k_ladderMaskMv, &fil, &tr);
        if (FmIsLadderHit(tr)) { outN = tr.plane.normal; return true; }
    }
    return false;
}

// ---- botox fireman.cpp geometry ports (drop in / sides only / side strips) ----
// botox traces these with mask CONTENTS_LADDER alone: the ladder brush, not the wall behind it
static void FmLadderLine(void* pLocal, const Vec3& a, const Vec3& b, GameTrace_t& tr)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    tr = GameTrace_t{};
    TraceLine(a, b, k_contentsLadderMv, &fil, &tr);
}
static void FmSolidHull(void* pLocal, const Vec3& a, const Vec3& b, const Vec3& mins, const Vec3& maxs, GameTrace_t& tr)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    tr = GameTrace_t{};
    TraceHull(a, b, mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
}
static bool FmPointSolid(void* pLocal, const Vec3& p)
{
    GameTrace_t tr; FmSolidHull(pLocal, p, p, Vec3(-0.1f, -0.1f, -0.1f), Vec3(0.1f, 0.1f, 0.1f), tr);
    return tr.startsolid || tr.allsolid;
}
static Vec3 FmYawDir(float yaw) { return Vec3(cosf(DEG2RADF(yaw)), sinf(DEG2RADF(yaw)), 0.f); }

// fireman_face_gap: hull along yaw to the ladder brush, probe = nothing in the way
static float FmFaceGap(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs, float yaw, float probe)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(origin, origin + FmYawDir(yaw) * probe, mins, maxs, k_contentsLadderMv, &fil, &tr);
    return tr.fraction >= 1.f ? probe : tr.fraction * probe;
}

// fireman_side_face: hull sweep along `dir` meets a ladder face that faces back at us, not behind a wall.
// Normal fireman: 16 centre LINE rays cannot cross a 4-8u deep brush's side strip (botox 2026-09-xx)
static bool FmSideFace(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs, const Vec3& dir,
                       float reach, Vec3& outN, float& outGap)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t t{};
    TraceHull(origin, origin + dir * reach, mins, maxs, k_contentsLadderMv, &fil, &t);
    if (t.startsolid || t.allsolid || t.fraction >= 1.f) return false;
    const Vec3& n = t.plane.normal;
    if (fabsf(n.z) >= 0.3f || n.x * dir.x + n.y * dir.y > -0.7f) return false;
    outGap = t.fraction * reach;
    GameTrace_t s; FmSolidHull(pLocal, origin, origin + dir * reach, mins, maxs, s);
    if (s.startsolid || s.fraction * reach < outGap - 2.f) return false;
    const float l = sqrtf(n.x * n.x + n.y * n.y);
    outN = Vec3(n.x / l, n.y / l, 0.f);
    return true;
}

// fireman_face_span: how far the face runs each way along `tangent` (8u steps, 48u cap)
static void FmFaceSpan(void* pLocal, const Vec3& p, const Vec3& n, float& lo, float& hi)
{
    const Vec3 t(-n.y, n.x, 0.f);
    lo = hi = 0.f;
    for (int side = 0; side < 2; ++side)
    {
        const float sign = side == 0 ? 1.f : -1.f;
        for (int i = 1; i <= 6; ++i)
        {
            const float off = sign * 8.f * i;
            const Vec3 a = p + n * 4.f + t * off;
            GameTrace_t tr; FmLadderLine(pLocal, a, a - n * 12.f, tr);
            if (tr.fraction >= 1.f) break;
            (sign > 0.f ? hi : lo) = off;
        }
    }
}

// fireman_catch_point: the face point moved down to where the hull will be at the catch (floor +
// 0.3 hull), clamped to where the ladder still exists. Side ends are walked at THIS height
static Vec3 FmCatchPoint(void* pLocal, const Vec3& p, const Vec3& n, const Vec3& mins, const Vec3& maxs)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    const Vec3 a = p + n * (maxs.x + 1.f);
    GameTrace_t t{};
    TraceLine(a, a - Vec3(0.f, 0.f, 1024.f), MASK_PLAYERSOLID, &fil, &t);
    if (t.startsolid || t.fraction >= 1.f) return p;
    const float want = t.endpos.z + mins.z + (maxs.z - mins.z) * 0.3f;
    if (want >= p.z) return p;
    const Vec3 in(p.x - n.x * 0.5f, p.y - n.y * 0.5f, want);
    GameTrace_t s; FmLadderLine(pLocal, in, Vec3(in.x, in.y, p.z), s);
    if (s.startsolid) return Vec3(p.x, p.y, want);
    if (s.fraction >= 1.f) return p;
    return Vec3(p.x, p.y, (std::min)(s.endpos.z + 4.f, p.z));
}

// fireman_face_end 1:1: walk the face along sign*tangent in 8u steps until it ends, then trace back
// from 0.1u inside the corner = the adjacent face. open = that corner is not in solid (wall)
static fireman::FaceEnd FmFaceEnd(void* pLocal, const Vec3& p, const Vec3& n, float sign)
{
    fireman::FaceEnd out{};
    const Vec3 t(-n.y * sign, n.x * sign, 0.f);
    float miss = -1.f;
    for (int i = 1; i <= 16 && miss < 0.f; ++i)
    {
        const float off = 8.f * i;
        const Vec3 a = p + n * 4.f + t * off;
        GameTrace_t tr; FmLadderLine(pLocal, a, a - n * 12.f, tr);
        if (tr.fraction >= 1.f) miss = off;
    }
    if (miss < 0.f) return out;
    const Vec3 corner = p - n * 0.1f + t * miss;
    GameTrace_t tr; FmLadderLine(pLocal, corner, corner - t * 9.f, tr);
    const Vec3& m = tr.plane.normal;
    if (tr.startsolid || tr.fraction >= 1.f || fabsf(m.z) >= 0.3f || m.x * t.x + m.y * t.y < 0.7f) return out;
    const float l = sqrtf(m.x * m.x + m.y * m.y);
    out.found = true;
    out.open  = !FmPointSolid(pLocal, corner);
    out.px = tr.endpos.x; out.py = tr.endpos.y; out.pz = tr.endpos.z;
    out.nx = m.x / l; out.ny = m.y / l;
    out.at = (tr.endpos.x - p.x) * t.x + (tr.endpos.y - p.y) * t.y;
    return out;
}

// fireman_side_targets 1:1: wide hit face (>= 20) = the front, its open ends are the sides;
// narrow = the hit IS a side, its open end the front
static int FmSideTargets(void* pLocal, const Vec3& p, const Vec3& n, fireman::SideTarget out[2])
{
    const fireman::FaceEnd ends[2] = { FmFaceEnd(pLocal, p, n, 1.f), FmFaceEnd(pLocal, p, n, -1.f) };
    if (!ends[0].found || !ends[1].found) return 0;
    if (ends[0].at + ends[1].at < 20.f)
    {
        if (fireman::SideGeometry(p.x, p.y, p.z, n.x, n.y, ends, nullptr, out[0])) return 1;
        if (!ends[0].open || !ends[1].open) return 0;
        int c = 0;
        for (const auto& e : ends)
        {
            const float f[2] = { e.nx, e.ny };
            if (fireman::SideGeometry(p.x, p.y, p.z, n.x, n.y, ends, f, out[c])) ++c;
        }
        return c;
    }
    int c = 0;
    for (const auto& e : ends)
    {
        if (!e.open) continue;
        const Vec3 ep(e.px, e.py, e.pz), en(e.nx, e.ny, 0.f);
        const fireman::FaceEnd se[2] = { FmFaceEnd(pLocal, ep, en, 1.f), FmFaceEnd(pLocal, ep, en, -1.f) };
        const float f[2] = { n.x, n.y };
        if (fireman::SideGeometry(ep.x, ep.y, ep.z, en.x, en.y, se, f, out[c])) ++c;
    }
    return c;
}

// fireman_side_back_room 1:1: slide in along -u from the free spot until the hull meets the wall.
// back = how far short of the centred park that stops; press = < 1u of side left exposed
static bool FmSideBackRoom(void* pLocal, const Vec3& mins, const Vec3& maxs, const fireman::SideTarget& st,
                           float& outBack, float& outOv, bool& outPress)
{
    outOv = -1.f; outPress = false;
    const Vec3 p(st.px, st.py, st.pz), s(st.nx, st.ny, 0.f), u(st.ux, st.uy, 0.f);
    const float supS = maxs.x * (fabsf(s.x) + fabsf(s.y)), supU = maxs.x * (fabsf(u.x) + fabsf(u.y));
    const float uBase = -st.half + 0.5f + supU, uFree = st.half + supU + 2.f, uCap = (std::min)(0.f, uBase);
    const auto at = [&](float uu) { return p + s * (supS + 1.f) + u * uu; };
    GameTrace_t t; FmSolidHull(pLocal, at(uFree), at(uCap), mins, maxs, t);
    if (t.startsolid || t.allsolid) return false;
    float uStop = t.fraction >= 1.f ? uCap : uFree - t.fraction * (uFree - uCap) + 0.05f;
    outOv = st.half - (std::max)(uStop - supU, -st.half);
    if (outOv < 0.1f) return false;
    const auto floorZ = [&](const Vec3& q) {
        GameTrace_t d; FmSolidHull(pLocal, q, q - Vec3(0.f, 0.f, 1024.f), mins, maxs, d);
        return d.endpos.z;
    };
    if (uStop < uBase && floorZ(at(uStop)) > floorZ(at(uBase)) + 8.f) uStop = uBase;
    else outPress = t.fraction < 1.f && outOv < 1.f;
    outBack = uBase - uStop;
    return true;
}

struct FmDropTarget
{
    bool  found = false, overhead = false, deep = false;
    float yaw = 0.f, lateral = 0.f, depth = 0.f, floorDepth = 0.f;
    Vec3  hit{};
};

// fireman_find_drop_target: footprint (5 down-rays at the feet, = overhead), then the remembered
// column, then a ring 8 dirs x 3 radii. Each hit checked against the floor at ITS x/y (the ladder
// mask walks through floors). deep = the column runs 64u+ under its top.
// ponytail: botox also scans every CONTENTS_LADDER brush off the bsp lumps for the exact nearest
// column; the ring + remembered column cover it until a "drop in misses a thin ladder" report.
static bool FmFindDropTarget(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs,
                             float maxDepth, float maxLat, FmDropTarget& out, const Vec3* remembered)
{
    out = FmDropTarget{};
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    const float feetZ = origin.z + mins.z + 1.f;
    const auto probe = [&](float ox, float oy) {
        const Vec3 a(origin.x + ox, origin.y + oy, feetZ);
        GameTrace_t t; FmLadderLine(pLocal, a, a - Vec3(0.f, 0.f, maxDepth), t);
        return t.fraction >= 1.f ? -1.f : t.fraction * maxDepth;
    };
    const auto floorAt = [&](float ox, float oy) {
        const Vec3 a(origin.x + ox, origin.y + oy, feetZ);
        GameTrace_t t{}; TraceLine(a, a - Vec3(0.f, 0.f, maxDepth), MASK_PLAYERSOLID, &fil, &t);
        return t.fraction >= 1.f ? maxDepth : t.fraction * maxDepth;
    };
    const auto fill = [&](float ox, float oy, float d, float f) {
        out.found = true; out.depth = d; out.floorDepth = f;
        out.hit = Vec3(origin.x + ox, origin.y + oy, feetZ - d);
        GameTrace_t t; FmLadderLine(pLocal, out.hit - Vec3(0.f, 0.f, 64.f), out.hit - Vec3(0.f, 0.f, 65.f), t);
        out.deep = t.startsolid;
    };

    const float xs[5] = { 0.f, maxs.x - 2.f, mins.x + 2.f, maxs.x - 2.f, mins.x + 2.f };
    const float ys[5] = { 0.f, maxs.y - 2.f, mins.y + 2.f, mins.y + 2.f, maxs.y - 2.f };
    for (int i = 0; i < 5; ++i)
    {
        const float d = probe(xs[i], ys[i]);
        if (d < 0.f || (out.found && d >= out.depth)) continue;
        const float f = floorAt(xs[i], ys[i]);
        if (d > f + 8.f) continue;
        fill(xs[i], ys[i], d, f);
        out.overhead = true;
    }
    if (out.found) return true;

    if (remembered)
    {
        const float ox = remembered->x - origin.x, oy = remembered->y - origin.y, lat = sqrtf(ox * ox + oy * oy);
        if (lat <= maxLat + 32.f)
        {
            const float d = probe(ox, oy);
            if (d >= 0.f && d <= floorAt(ox, oy) + 8.f)
            {
                fill(ox, oy, d, floorAt(ox, oy));
                out.overhead = fabsf(ox) < maxs.x && fabsf(oy) < maxs.y;
                out.yaw = RAD2DEGF(atan2f(oy, ox)); out.lateral = lat;
                return true;
            }
        }
    }

    const float radii[3] = { maxLat * 0.34f, maxLat * 0.67f, maxLat };
    for (int ri = 0; ri < 3; ++ri)
        for (int i = 0; i < 8; ++i)
        {
            const float a = i * (2.f * MV_PI / 8.f), ox = cosf(a) * radii[ri], oy = sinf(a) * radii[ri];
            const float d = probe(ox, oy);
            if (d < 0.f) continue;
            const float f = floorAt(ox, oy);
            if (d > f + 8.f) continue;
            fill(ox, oy, d, f);
            out.yaw = RAD2DEGF(a); out.lateral = radii[ri];
            return true;
        }
    return false;
}

// fireman_column_face: the column's catchable face, 8 rays from 48u out at 8u under its top.
// Nearest wins, a wide face beats a narrow end strip (a 1u strip is never caught)
// ponytail: botox's fireman_face_is_stub (trigger-top / stub reject) skipped, add on "catches nothing at a ledge top"
static bool FmColumnFace(void* pLocal, const Vec3& origin, const Vec3& hit, Vec3& outP, Vec3& outN)
{
    const Vec3 end(hit.x, hit.y, hit.z - 8.f);
    bool found = false, outNarrow = true; float best = 1e9f;
    for (int i = 0; i < 8; ++i)
    {
        const float a = i * (2.f * MV_PI / 8.f);
        const Vec3 start(end.x + cosf(a) * 48.f, end.y + sinf(a) * 48.f, end.z);
        GameTrace_t lt; FmLadderLine(pLocal, start, end, lt);
        if (lt.startsolid || lt.fraction >= 1.f) continue;
        const Vec3& n = lt.plane.normal;
        if (fabsf(n.z) >= 0.3f || (n.x == 0.f && n.y == 0.f)) continue;
        TraceFilterWorldProps fil; fil.Setup(pLocal);
        GameTrace_t st{}; TraceLine(start, end, MASK_PLAYERSOLID, &fil, &st);
        if (st.startsolid || st.fraction < lt.fraction - 2.f / 48.f) continue;   // wall in front of it
        const float dist = (lt.endpos - origin).Length2D();
        const float l = sqrtf(n.x * n.x + n.y * n.y);
        const Vec3 nh(n.x / l, n.y / l, 0.f);
        float lo, hi; FmFaceSpan(pLocal, lt.endpos, nh, lo, hi);
        const bool narrow = hi - lo < 8.f;
        if (found && ((narrow && !outNarrow) || (narrow == outNarrow && dist >= best))) continue;
        found = true; outNarrow = narrow; best = dist;
        outP = lt.endpos; outN = nh;
    }
    return found;
}

// botox sidepick: pick the side whose park is closest to where the drift stops if braked now
// (the side we already drift toward is the faster one); the hull must fit at the catch spot.
// Picked once per ladder, kept in s_fm.side*
static void FmPickSide(void* pLocal, const Vec3& origin, const Vec3& vel, const Vec3& mins, const Vec3& maxs,
                       const Vec3& p, const Vec3& n, float airaccel, float maxspeed)
{
    const Vec3 at = FmCatchPoint(pLocal, p, n, mins, maxs);
    fireman::SideTarget sides[2];
    const int count = FmSideTargets(pLocal, at, n, sides);
    // brake stop distance v^2 / 2a at botox fireman_brake_margin 0.9 of full air authority
    const float h = vel.Length2D(), a = 0.9f * airaccel * maxspeed;
    const float stop = h > 1.f && a > 0.f ? h * h / (2.f * a) : 0.f;
    const Vec3 rest = h > 1.f ? origin + Vec3(vel.x / h, vel.y / h, 0.f) * stop : origin;

    float best = 1e9f; int pick = -1, buried = 0, blocked = 0;
    float pickBack = 0.f, pickOv = 0.f; bool pickPress = false;
    for (int i = 0; i < count; ++i)
    {
        const auto& sd = sides[i];
        float back = 0.f, ov = 0.f; bool press = false;
        if (!FmSideBackRoom(pLocal, mins, maxs, sd, back, ov, press)) { ++buried; continue; }
        float x, y, fx, fy; bool staged, unused;
        fireman::SidePark(origin.x, origin.y, sd.px, sd.py, sd.nx, sd.ny, sd.ux, sd.uy, sd.half, 1.f, back, maxs.x, x, y, staged);
        fireman::SidePark(sd.px, sd.py, sd.px, sd.py, sd.nx, sd.ny, sd.ux, sd.uy, sd.half, 1.f, back, maxs.x, fx, fy, unused);
        // hull fits where it catches (pipe / wall beside the ladder), thin box at the ladder's height
        GameTrace_t fit; const Vec3 fp(fx, fy, sd.pz);
        FmSolidHull(pLocal, fp, fp, Vec3(mins.x, mins.y, -2.f), Vec3(maxs.x, maxs.y, 2.f), fit);
        if (fit.startsolid || fit.allsolid) { ++blocked; continue; }
        float d = sqrtf((x - rest.x) * (x - rest.x) + (y - rest.y) * (y - rest.y));
        if (staged) d += sqrtf((fx - x) * (fx - x) + (fy - y) * (fy - y));
        if (d < best) { best = d; pick = i; pickBack = back; pickOv = ov; pickPress = press; }
    }
    static int s_pickLog = -1000;
    if (pick >= 0 || DiagOk(s_pickLog, false))
        DebugLog("FM: sidepick sides=%d pick=%d buried=%d blocked=%d half=%.1f back=%.1f press=%d cost=%.0f stop=%.0f",
                 count, pick, buried, blocked, pick >= 0 ? sides[pick].half : 0.f, pickBack, pickPress ? 1 : 0,
                 pick >= 0 ? best : -1.f, stop);
    if (pick < 0) return;
    const auto& sd = sides[pick];
    s_fm.sideValid = true;
    s_fm.sidePoint = Vec3(sd.px, sd.py, sd.pz);
    s_fm.sideN = Vec3(sd.nx, sd.ny, 0.f); s_fm.sideU = Vec3(sd.ux, sd.uy, 0.f);
    s_fm.sideHalf = sd.half; s_fm.sideBack = pickBack; s_fm.sidePress = pickPress; s_fm.sideOv = pickOv;
}

// botox side steer target: the picked side's park. Thin side (press) leans 0.5u into the wall
// unless over the strip's top; a wall that ends in a ledge (nuke -155) keeps 2u off it; a straight
// line that already clears the front face skips the staged waypoint
static void FmSideParkPoint(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs, float& tx, float& ty)
{
    const Vec3& sp = s_fm.sidePoint; const Vec3& sn = s_fm.sideN; const Vec3& su = s_fm.sideU;
    bool press = s_fm.sidePress;
    if (press)
    {
        const float midU = s_fm.sideHalf - (std::max)(s_fm.sideOv, 0.f) * 0.5f;
        const Vec3 top(sp.x - sn.x * 0.5f + su.x * midU, sp.y - sn.y * 0.5f + su.y * midU, origin.z + maxs.z);
        GameTrace_t t; FmLadderLine(pLocal, top, top - Vec3(0.f, 0.f, 512.f), t);
        press = t.startsolid || t.fraction >= 1.f || origin.z + mins.z <= t.endpos.z;
    }
    const float supS = maxs.x * (fabsf(sn.x) + fabsf(sn.y));
    const float touchU = -s_fm.sideHalf + 0.5f + maxs.x * (fabsf(su.x) + fabsf(su.y)) - s_fm.sideBack - 0.05f;
    const Vec3 ledgeAt(sp.x + sn.x * (supS + 1.f) + su.x * (touchU - 0.5f), sp.y + sn.y * (supS + 1.f) + su.y * (touchU - 0.5f), origin.z);
    GameTrace_t ledge; FmSolidHull(pLocal, ledgeAt, ledgeAt - Vec3(0.f, 0.f, 1024.f), mins, maxs, ledge);
    const bool overLedge = !ledge.startsolid && ledge.fraction < 1.f && ledge.endpos.z + mins.z > sp.z + maxs.z;
    float back = s_fm.sideBack + (press && !overLedge ? 0.5f : 0.f);
    if (overLedge) back = (std::min)(back, s_fm.sideBack + 0.05f - 2.f);
    bool staged;
    fireman::SidePark(origin.x, origin.y, sp.x, sp.y, sn.x, sn.y, su.x, su.y, s_fm.sideHalf, 1.f, back, maxs.x, tx, ty, staged);
    if (staged)
    {
        float fx, fy; bool unused;
        fireman::SidePark(sp.x, sp.y, sp.x, sp.y, sn.x, sn.y, su.x, su.y, s_fm.sideHalf, 1.f, back, maxs.x, fx, fy, unused);
        if (fireman::SideLineClear(origin.x, origin.y, fx, fy, sp.x, sp.y, sn.x, sn.y, su.x, su.y, s_fm.sideHalf, maxs.x, 0.75f))
        { tx = fx; ty = fy; }
    }
}

// ---- LADDER state: pre-prediction, from the MOVETYPE_LADDER call site (bMoveBlocked skips the rest)
static bool FiremanLadderTick(void* pLocal, UserCmd_t* pCmd)
{
    s_fm.owns = false;
    s_firemanHit = false;
    if (!On(g_fireMan, g_fireManKey, g_fireManMode)) { s_fm = FiremanState_t{}; return false; }
    if (g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0) return false;
    const int tick = FmTick();   // pre-Pred_Start here, same clock as FireMan's s_prePredTick
    if (!s_fm.onLadder)
    {
        s_fm.onLadder = true;
        s_fm.fromFall = tick - s_fm.fallTick <= 4;
        // botox: the ride is its own lock -- a key pressed mid-ride is LadderMove's climb (into the
        // face = stuck), so a locked drop keeps eating keys until the fling
        s_fm.rideLock = s_fm.fromFall && s_fm.dropLock;
        DebugLog("FM: attached %s%s", s_fm.fromFall ? "from a fall -> ride" : "by a climb -> not ours",
                 s_fm.rideLock ? " (locked)" : "");
    }
    if (!s_fm.fromFall) return false;
    if (!FmHandsEmpty() && !s_fm.rideLock) return false;   // you are climbing it yourself

    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);

    Vec3 n{};
    const bool face = FmLadderFace(pLocal, origin, mins, maxs, n);
    // fast descent view + this tick's step (fireman_pure.h LadderVel: 400 on a vertical face)
    const float yaw = face ? fireman::FastDescentYaw(n.x, n.y) : 0.f;
    float lvx, lvy, lvz;
    fireman::LadderVel(fireman::kDescentPitch, yaw, 200.f, 200.f, n.x, n.y, n.z, lvx, lvy, lvz);
    const float step = (std::max)(-lvz, 0.f) * TickInterval();

    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t down{};
    const float reach = step + 4.f;
    TraceHull(origin, origin - Vec3(0.f, 0.f, reach), mins, maxs, MASK_PLAYERSOLID, &fil, &down);
    const bool floorBelow = down.fraction < 1.f && !down.startsolid && down.plane.normal.z >= 0.7f;
    // CategorizePosition runs on every ladder tick (gamemovement.cpp:4583) with a 2u touch: inside
    // it, this tick's IN_JUMP is LadderMove's 270 fling AND CheckJumpButton's hop (ground set, old
    // buttons clear) -- the fall is gone and the jump comes out at floor height. Never at the ledge
    // we walked off (a snap at the top can still touch it).
    const bool onFloor = floorBelow && down.fraction * reach <= 2.f && origin.z < s_fmLastGroundZ - 18.f;
    bool ladderEnds = !face;   // no face found = nothing to slide on: get off rather than hang
    if (face && !floorBelow)
    {
        GameTrace_t tr{};
        const Vec3 low = origin - Vec3(0.f, 0.f, step);
        TraceHull(low, low - Vec3(n.x, n.y, 0.f) * 10.f, mins, maxs, k_ladderMaskMv, &fil, &tr);
        ladderEnds = !FmIsLadderHit(tr);
    }

    pCmd->forwardmove = pCmd->sidemove = pCmd->upmove = 0.f;
    if (onFloor || ladderEnds)
    {
        // :2934 -- WALK + 270 * normal this tick (+ the hop when on the floor)
        pCmd->buttons = (pCmd->buttons & ~(k_fmMoveBtns | IN_DUCK)) | IN_JUMP;
        s_fm.fromFall = false;
        s_fm.rideLock = s_fm.dropLock = false;
        s_fm.launchLeft = k_fmLaunchTicks;
        s_fm.jumpLast = true;
        DebugLog("FM: fling off ladder (%s) z=%.1f", onFloor ? "floor + hop" : face ? "ladder ends" : "no face", origin.z);
    }
    else
    {
        // IN_FORWARD + IN_MOVERIGHT, pitch 89, right vector on the face normal: the into-face part
        // turns into straight down (:2961-2999) on top of the pitched forward = 400 down, not
        // IN_BACK's 200. A step that reaches the floor stops on it (TryPlayerMove); next tick
        // is the onFloor fling above. No ducking: ClimbSpeed * 0.34
        pCmd->viewangles = Vec3(fireman::kDescentPitch, yaw, 0.f);
        pCmd->buttons = (pCmd->buttons & ~(k_fmMoveBtns | IN_JUMP | IN_DUCK)) | IN_FORWARD | IN_MOVERIGHT;
        s_fm.jumpLast = false;
        static int s_rideLog = -1000;
        if (DiagOk(s_rideLog, false))
            DebugLog("FM: ride vz=%.0f floor=%s z=%.1f", lvz, floorBelow ? "near" : "far", origin.z);
    }
    s_firemanHit = true;
    FmOwn(pCmd);
    return true;
}

// ---- WALK state + launch window: post-prediction (s_prePred* = this cmd's START)
// true = fireman wrote this cmd: JumpBug & co. must not touch it
static bool FireMan(void* pLocal, UserCmd_t* pCmd)
{
    static int s_logTick = -1000;
    s_firemanHit = false;
    s_fm.owns = false;
    if (g_offVelocity <= 0 || g_offOrigin <= 0 || g_offFlags <= 0 || g_offMins <= 0 || g_offMaxs <= 0)
        return false;
    const bool  ground = (s_prePredFlags & FL_ONGROUND) != 0;
    const Vec3  origin = s_prePredOrigin, vel = s_prePredVel;
    // tracked above every gate (botox rule: per-tick state a gate reads must not hide behind it)
    if (ground) s_fmLastGroundZ = origin.z;
    if (!On(g_fireMan, g_fireManKey, g_fireManMode)) { s_fm = FiremanState_t{}; return false; }

    // only called with START movetype WALK (bMoveBlocked). The live entity is already at the cmd's
    // END here, so its movetype/flags are not this tick's -- s_prePred* and s_prePredTick are
    s_fm.onLadder = false;
    s_fm.rideLock = false;
    if (!ground) s_fm.fallTick = s_prePredTick;
    // grounded clears the drop-in lock + column; the side pick outlives a 1-tick hop touchdown (botox 0.25 s)
    if (ground)
    {
        s_fm.dropLock = false; s_fm.lockLost = s_fm.lockTicks = 0; s_fm.dropColValid = false;
        if (s_fm.sideValid && ++s_fm.sideGround * TickInterval() > 0.25f) s_fm.sideValid = false;
    }
    else
        s_fm.sideGround = 0;
    const bool lockArmed = g_firemanDropIn && g_firemanDropInLock;
    if (!lockArmed) { s_fm.dropLock = false; s_fm.lockLost = s_fm.lockTicks = 0; }
    const bool sideOnly = g_firemanSideOnly && MovementKeyOK(g_firemanSideOnlyKey, g_firemanSideOnlyMode);
    if (!sideOnly) s_fm.sideValid = false;

    const int nBase = s_predicted ? PredCommandsPredicted() : 0;
    const auto resimEnd = [&] { if (s_predicted) { RestoreToFrame(nBase - 1); Pred_Simulate(pLocal, pCmd); } };

    // launch window: a fresh jump press on every touchdown
    if (s_fm.launchLeft > 0)
    {
        --s_fm.launchLeft;
        if (ground && !s_fm.jumpLast)
        {
            pCmd->buttons |= IN_JUMP;
            if (g_firemanCrouchJump) pCmd->buttons |= IN_DUCK;
            s_fm.jumpLast = true;
            DebugLog("FM: launch hop xy=%.1f", vel.Length2D());
        }
        else
        {
            pCmd->buttons &= ~IN_JUMP;
            s_fm.jumpLast = false;
        }
        FmOwn(pCmd);
        resimEnd();
        return true;
    }
    s_fm.jumpLast = false;

    if (ground) return false;

    if (s_fm.dropLock && ++s_fm.lockTicks > 512) { s_fm.dropLock = false; DebugLog("FM: lock expired"); }
    const bool hands = FmHandsEmpty();
    // keys held + no lock: only the drop-in scan may run -- it is what arms the lock (botox
    // probe_only: holding W over the shaft must not make the lock unreachable)
    if (!hands && !s_fm.dropLock && !lockArmed)
    {
        if (DiagOk(s_logTick, false)) DebugLog("FM: idle -- move keys held (fireman needs hands empty)");
        return false;
    }
    const auto eatKeys = [&] {   // lock: fwd/side + duck (resizes the hull mid-grab), never jump
        pCmd->forwardmove = pCmd->sidemove = pCmd->upmove = 0.f;
        pCmd->buttons &= ~(k_fmMoveBtns | IN_DUCK);
    };
    if (!s_predicted)
    {
        if (!s_fm.dropLock) return false;
        eatKeys(); FmOwn(pCmd);
        return true;
    }

    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float depth = FmFloorDepth(pLocal, origin, mins, maxs);
    const float floorZ = origin.z - (depth < 0.f ? 0.f : depth);
    // a hop lands where it took off; only a real drop (floor under us lower than the one we left) counts
    const bool realDrop = depth >= 0.f && floorZ <= s_fmLastGroundZ - 18.f;

    // ---- drop in (botox 2026-08-15): a ladder column under / beside us, rising ticks included --
    // the air brake is ~airaccel*maxspeed u/s^2, so a full stop needs the whole jump, not the last tick
    FmDropTarget target{};
    bool dropUsable = false;
    if (g_firemanDropIn && depth >= 0.f)
    {
        dropUsable = FmFindDropTarget(pLocal, origin, mins, maxs, 512.f, 72.f, target,
                                      s_fm.dropColValid ? &s_fm.dropCol : nullptr) && target.deep;
        if (dropUsable && !target.overhead)
        {
            // ring hit through a wall: LOS at OUR height (the air the approach must cross)
            GameTrace_t los; FmSolidHull(pLocal, origin, origin + FmYawDir(target.yaw) * target.lateral, mins, maxs, los);
            dropUsable = los.fraction >= 0.99f;
        }
        if (dropUsable)
        {
            s_fm.lockLost = 0; s_fm.dropCol = target.hit; s_fm.dropColValid = true;
            if (lockArmed && !s_fm.dropLock)
            {
                s_fm.dropLock = true; s_fm.lockTicks = 0;
                DebugLog("FM: lock on over=%d lat=%.0f depth=%.0f", target.overhead ? 1 : 0, target.lateral, target.depth);
            }
        }
        else
        {
            s_fm.dropColValid = false;
            if (s_fm.dropLock && ++s_fm.lockLost > 16) { s_fm.dropLock = false; DebugLog("FM: lock lost depth=%.0f", depth); }
        }
    }
    const bool locked = s_fm.dropLock;
    if (!hands && !locked) return false;   // probe only: the player steers
    if (locked) eatKeys();

    bool simmed = false;
    // a locked tick that writes nothing of its own still sends no keys
    const auto pass = [&] {
        if (simmed || locked) resimEnd();
        if (!locked) return false;
        FmOwn(pCmd);
        return true;
    };
    if (depth < 0.f || (!realDrop && !dropUsable)) return pass();

    const float dt = TickInterval(), g = CvarFloat("sv_gravity", 800.f);
    const float airaccel = CvarFloat("sv_airaccelerate", 10.f);
    const float maxspeed = (g_offMaxspeed > 0 && EntF(pLocal, g_offMaxspeed) > 1.f) ? EntF(pLocal, g_offMaxspeed) : 250.f;
    const float reach = std::clamp(g_firemanReach, 64.f, 640.f);

    // ---- the ladder: the hunt's nearest, or the drop-in column's face when that is far (botox column_hunt)
    Vec3 p{}, n{}; float nz = 0.f;
    bool found = realDrop && FmFindLadder(pLocal, origin, floorZ, p, n, &nz);
    if (dropUsable && (!found || (p - origin).Length2D() > 64.f))
    {
        Vec3 cp{}, cn{};
        if (FmColumnFace(pLocal, origin, target.hit, cp, cn))
        {
            p = cp; n = cn; nz = 0.f; found = true;
            static int s_colLog = -1000;
            if (DiagOk(s_colLog, false))
                DebugLog("FM: drop in face dist=%.0f depth=%.0f over=%d lat=%.0f", (p - origin).Length2D(), target.depth,
                         target.overhead ? 1 : 0, target.lateral);
        }
    }

    // ---- sides only: the picked side replaces the face (botox 2026-09-25)
    bool sidePick = false;
    if (sideOnly && !found && s_fm.sideValid && (s_fm.sidePoint - origin).Length2D() <= reach)
    {
        p = s_fm.sidePoint; n = s_fm.sideN; nz = 0.f; found = true;
    }
    if (sideOnly && found)
    {
        if (s_fm.sideValid && (p - s_fm.sidePoint).Length2D() > 160.f) s_fm.sideValid = false;   // another ladder
        if (!s_fm.sideValid && fabsf(nz) < 0.1f) FmPickSide(pLocal, origin, vel, mins, maxs, p, n, airaccel, maxspeed);
        if (s_fm.sideValid) { p = s_fm.sidePoint; n = s_fm.sideN; sidePick = true; }
    }
    else if (found)
    {
        // beside the ladder (the straight path to its front is clear): sweep sideways for the side
        // strip, centre line rays cannot see a 4-8u deep brush's side (botox side_face)
        const float probe = (p - origin).Length2D() + 32.f;
        if (FmFaceGap(pLocal, origin, mins, maxs, RAD2DEGF(atan2f(-n.y, -n.x)), probe) >= probe - 0.5f)
        {
            const Vec3 t(-n.y, n.x, 0.f);
            const Vec3 dir = t * ((p.x - origin.x) * t.x + (p.y - origin.y) * t.y >= 0.f ? 1.f : -1.f);
            Vec3 sn{}; float gap = 0.f;
            if (FmSideFace(pLocal, origin, mins, maxs, dir, 64.f, sn, gap))
            {
                p = origin + dir * gap - sn * (maxs.x * (fabsf(sn.x) + fabsf(sn.y)));
                n = sn;
                static int s_sideLog = -1000;
                if (DiagOk(s_sideLog, false)) DebugLog("FM: side strip gap=%.1f vz=%.0f", gap, vel.z);
            }
        }
    }

    if (!found && !dropUsable)
    {
        if (DiagOk(s_logTick, false)) DebugLog("FM: no ladder in %.0fu, vz=%.0f depth=%.0f", reach, vel.z, depth);
        return pass();
    }

    UserCmd_t zero = *pCmd;
    zero.forwardmove = zero.sidemove = zero.upmove = 0.f;
    zero.buttons &= ~(k_fmMoveBtns | IN_JUMP);

    // ---- grab: falling only (a rising grab flings at height). Ask the engine: does the grab fling
    // NOW, would it still fling NEXT tick, does plain fall land this tick. Grab = the LAST latchable
    // tick (floor this tick, or the ladder gone next tick): every tick earlier is fall height the
    // fling's vz=0 has to drop again before the launch hop. A predicted CheckForLadders snap is left
    // alone: that is the top of the shaft, and the snap hands us to the ladder ride. Sides only:
    // a fling off any face but the picked side is no latch at all
    const float faceYaw = RAD2DEGF(atan2f(-n.y, -n.x));
    bool latchNow = false, latchNext = false, land1 = false, snap1 = false;
    UserCmd_t grab = *pCmd;
    if (found && vel.z < 0.f)
    {
        grab.viewangles  = Vec3(0.f, faceYaw, 0.f);
        grab.forwardmove = 1.f;   // any nonzero move latches (:2876); 1 u/s wish brakes the fling by ~0.15
        grab.sidemove = grab.upmove = 0.f;
        grab.buttons = (pCmd->buttons & ~k_fmMoveBtns) | IN_JUMP;

        const auto flung = [&] {
            const Vec3 v = EntV3(pLocal, g_offVelocity);
            return EntMoveType(pLocal) == MOVETYPE_WALK && fireman::IsFlung(v.x, v.y, v.z, g, dt)
                && (!sidePick || fireman::SideOk(v.x, v.y, n.x, n.y));
        };
        const auto onGround = [&] { return (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0; };
        const auto snapped  = [&] { return EntMoveType(pLocal) == MOVETYPE_LADDER; };

        if (SimsLeft() >= 4 && !TbBudgetExpired())
        {
            simmed = true;
            RestoreToFrame(nBase - 1); Pred_Simulate(pLocal, &grab);
            latchNow = flung();
            if (latchNow)
            {
                RestoreToFrame(nBase - 1); Pred_Simulate(pLocal, &zero);
                snap1 = snapped();
                land1 = !snap1 && onGround();
                if (!land1 && !snap1)
                {
                    Pred_Simulate(pLocal, &grab);
                    latchNext = flung();
                }
            }
        }
    }

    if (latchNow && !snap1 && (land1 || !latchNext))
    {
        pCmd->viewangles  = grab.viewangles;
        pCmd->forwardmove = grab.forwardmove;
        pCmd->sidemove    = grab.sidemove;
        pCmd->upmove      = grab.upmove;
        pCmd->buttons     = grab.buttons;
        s_fm.launchLeft = k_fmLaunchTicks;
        s_fm.jumpLast = true;
        s_fm.dropLock = false;
        s_firemanHit = true;
        DebugLog("FM: GRAB (%s) vz=%.1f depth=%.1f face=%.0f side=%d drop=%d", land1 ? "floor now" : "ladder ends",
                 vel.z, depth, faceYaw, sidePick ? 1 : 0, dropUsable ? 1 : 0);
        FmOwn(pCmd);
        resimEnd();
        return true;
    }
    if (land1 && DiagOk(s_logTick, true))
        DebugLog("FM: MISS -- landing with the ladder out of grab reach, vz=%.1f", vel.z);

    if (!g_firemanApproach && !dropUsable) return pass();

    // ---- steer: station-keep 1u off the face (inside the 2u grab reach) / the picked side's park /
    // over the column (drop in with no face yet: overhead = full stop)
    float tx = origin.x, ty = origin.y;
    if (sidePick)
        FmSideParkPoint(pLocal, origin, mins, maxs, tx, ty);
    else if (found)
    {
        const float extent = maxs.x * (fabsf(n.x) + fabsf(n.y));   // AABB half-width along the normal
        tx = p.x + n.x * (extent + 1.f); ty = p.y + n.y * (extent + 1.f);
    }
    else if (!target.overhead)
    {
        tx = target.hit.x; ty = target.hit.y;
    }
    const float rx = tx - origin.x, ry = ty - origin.y;
    const float fallTicks = fireman::FallTicks(-vel.z, depth, g, dt);
    const int parkIn = fireman::ParkTicks(rx, ry, vel.x, vel.y, airaccel, maxspeed, dt,
                                          (int)std::min(fallTicks, 512.f), 1.f);
    if (parkIn < 0 && !latchNow)
    {
        if (DiagOk(s_logTick, false))
            DebugLog("FM: far -- target %.0fu off, %.0f ticks of fall left", sqrtf(rx * rx + ry * ry), fallTicks);
        return pass();
    }

    float wx = 0.f, wy = 0.f;
    const float ws = fireman::KeepWish(rx, ry, vel.x, vel.y, airaccel, maxspeed, dt, wx, wy);
    UserCmd_t steer = zero;
    if (ws > 0.f)
    {
        // pitch 89: AirMove flattens forward (full horizontal accel), LadderMove's 2u trace points down
        steer.viewangles  = Vec3(89.f, RAD2DEGF(atan2f(wy, wx)), 0.f);
        steer.forwardmove = ws;
        // flush against the face even 0.035u of horizontal reach can latch: the sim decides
        simmed = true;
        RestoreToFrame(nBase - 1); Pred_Simulate(pLocal, &steer);
        if (EntMoveType(pLocal) == MOVETYPE_LADDER) steer = zero;
    }
    pCmd->viewangles  = steer.viewangles;
    pCmd->forwardmove = steer.forwardmove;
    pCmd->sidemove    = 0.f;
    pCmd->upmove      = 0.f;
    pCmd->buttons     = steer.buttons;
    if (DiagOk(s_logTick, false))
        DebugLog("FM: steer dist=%.1f park=%d fall=%.0f vz=%.0f inReach=%d ws=%.0f side=%d drop=%d lock=%d",
                 sqrtf(rx * rx + ry * ry), parkIn, fallTicks, vel.z, latchNow ? 1 : 0, ws, sidePick ? 1 : 0,
                 dropUsable ? 1 : 0, locked ? 1 : 0);
    FmOwn(pCmd);
    resimEnd();
    return true;
}
