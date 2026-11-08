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

struct FiremanState_t
{
    int   fallTick = -1000;     // last tick airborne in WALK with the key held
    bool  onLadder = false;     // saw MOVETYPE_LADDER last ladder tick
    bool  fromFall = false;     // this attach came out of a fall -> ours to ride
    int   launchLeft = 0;
    bool  jumpLast = false;     // last cmd we wrote held IN_JUMP
    bool  owns = false;        // this cmd's move is ours: FiremanReassert re-applies it
    Vec3  view{}; float fwd = 0.f, side = 0.f, up = 0.f; int buttons = 0;
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
static bool FmFindLadder(void* pLocal, const Vec3& origin, float floorZ, Vec3& outP, Vec3& outN)
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
        DebugLog("FM: attached %s", s_fm.fromFall ? "from a fall -> ride" : "by a climb -> not ours");
    }
    if (!s_fm.fromFall) return false;
    if (!FmHandsEmpty()) return false;   // you are climbing it yourself

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
    if (!ground) s_fm.fallTick = s_prePredTick;

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

    if (ground || vel.z >= 0.f) return false;
    if (!FmHandsEmpty())
    {
        if (DiagOk(s_logTick, false)) DebugLog("FM: idle -- move keys held (fireman needs hands empty)");
        return false;
    }
    if (!s_predicted) return false;

    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float depth = FmFloorDepth(pLocal, origin, mins, maxs);
    if (depth < 0.f) return false;
    const float floorZ = origin.z - depth;
    // a hop lands where it took off; only a real drop (floor under us lower than the one we left) counts
    if (floorZ > s_fmLastGroundZ - 18.f) return false;

    Vec3 p{}, n{};
    if (!FmFindLadder(pLocal, origin, floorZ, p, n))
    {
        if (DiagOk(s_logTick, false)) DebugLog("FM: no ladder in %.0fu, vz=%.0f depth=%.0f", g_firemanReach, vel.z, depth);
        return false;
    }

    const float dt = TickInterval(), g = CvarFloat("sv_gravity", 800.f);
    const float faceYaw = RAD2DEGF(atan2f(-n.y, -n.x));

    UserCmd_t grab = *pCmd;
    grab.viewangles  = Vec3(0.f, faceYaw, 0.f);
    grab.forwardmove = 1.f;   // any nonzero move latches (:2876); 1 u/s wish brakes the fling by ~0.15
    grab.sidemove = grab.upmove = 0.f;
    grab.buttons = (pCmd->buttons & ~k_fmMoveBtns) | IN_JUMP;
    UserCmd_t zero = *pCmd;
    zero.forwardmove = zero.sidemove = zero.upmove = 0.f;
    zero.buttons &= ~(k_fmMoveBtns | IN_JUMP);

    const auto flung = [&] {
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return EntMoveType(pLocal) == MOVETYPE_WALK && fireman::IsFlung(v.x, v.y, v.z, g, dt);
    };
    const auto onGround = [&] { return (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0; };
    const auto snapped  = [&] { return EntMoveType(pLocal) == MOVETYPE_LADDER; };

    // ask the engine: does the grab fling NOW, would it still fling NEXT tick, does plain fall land
    // this tick. Grab = the LAST latchable tick (floor this tick, or the ladder gone next tick):
    // every tick earlier is fall height the fling's vz=0 has to drop again before the launch hop.
    // A predicted CheckForLadders snap is left alone: that is the top of the shaft, and the snap
    // hands us to the ladder ride (a grab there flings us out)
    bool latchNow = false, latchNext = false, land1 = false, snap1 = false;
    if (SimsLeft() >= 4 && !TbBudgetExpired())
    {
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

    if (latchNow && !snap1 && (land1 || !latchNext))
    {
        pCmd->viewangles  = grab.viewangles;
        pCmd->forwardmove = grab.forwardmove;
        pCmd->sidemove    = grab.sidemove;
        pCmd->upmove      = grab.upmove;
        pCmd->buttons     = grab.buttons;
        s_fm.launchLeft = k_fmLaunchTicks;
        s_fm.jumpLast = true;
        s_firemanHit = true;
        DebugLog("FM: GRAB (%s) vz=%.1f depth=%.1f face=%.0f", land1 ? "floor now" : "ladder ends",
                 vel.z, depth, faceYaw);
        FmOwn(pCmd);
        resimEnd();
        return true;
    }
    if (land1 && DiagOk(s_logTick, true))
        DebugLog("FM: MISS -- landing with the ladder out of grab reach, vz=%.1f", vel.z);

    if (!g_firemanApproach) { resimEnd(); return false; }

    // steer: station-keep 1u off the face (inside the 2u grab reach)
    const float airaccel = CvarFloat("sv_airaccelerate", 10.f);
    const float maxspeed = (g_offMaxspeed > 0 && EntF(pLocal, g_offMaxspeed) > 1.f) ? EntF(pLocal, g_offMaxspeed) : 250.f;
    const float extent = maxs.x * (fabsf(n.x) + fabsf(n.y));   // AABB half-width along the normal
    const float rx = p.x + n.x * (extent + 1.f) - origin.x, ry = p.y + n.y * (extent + 1.f) - origin.y;
    const float fallTicks = fireman::FallTicks(-vel.z, depth, g, dt);
    const int parkIn = fireman::ParkTicks(rx, ry, vel.x, vel.y, airaccel, maxspeed, dt,
                                          (int)std::min(fallTicks, 512.f), 1.f);
    if (parkIn < 0 && !latchNow)
    {
        if (DiagOk(s_logTick, false))
            DebugLog("FM: far -- ladder %.0fu off, %.0f ticks of fall left", sqrtf(rx * rx + ry * ry), fallTicks);
        resimEnd();
        return false;
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
        RestoreToFrame(nBase - 1); Pred_Simulate(pLocal, &steer);
        if (EntMoveType(pLocal) == MOVETYPE_LADDER) steer = zero;
    }
    pCmd->viewangles  = steer.viewangles;
    pCmd->forwardmove = steer.forwardmove;
    pCmd->sidemove    = 0.f;
    pCmd->upmove      = 0.f;
    pCmd->buttons     = steer.buttons;
    if (DiagOk(s_logTick, false))
        DebugLog("FM: steer dist=%.1f park=%d fall=%.0f vz=%.0f inReach=%d ws=%.0f",
                 sqrtf(rx * rx + ry * ry), parkIn, fallTicks, vel.z, latchNow ? 1 : 0, ws);
    FmOwn(pCmd);
    resimEnd();
    return true;
}
