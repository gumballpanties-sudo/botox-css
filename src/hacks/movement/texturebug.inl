// texturebug.inl -- textually included by movement.cpp (above airstuck.inl / wallclimb.inl), not its own TU.
// Texture bug = botox csgo texture_bug() port again (11-07 night): the 17:56 "two-plane crease" rewrite
// modeled the wrong mechanism (log: hit=0 on every search). User 11-07: TB is the SIMD box-brush
// collision bug -- the hull inside the DIST_EPSILON band of a wall gets the brush TOP plane as its clip,
// vz -> 0, then FinishGravity's half step = END vz -halfgrav. Only a gap-exact press (ladder below)
// lands in that band; coarse wish rungs never do. Pin test is IsTargetVz (CSS clip nudge aware).
// The shared engine-search helpers (AT_*) below are airstuck.inl's and wallclimb.inl's.
// HeadSurf / HeadBounce (bottom of the file) are their own features on the TB bind, unchanged.
//
// Uses movement.cpp file-statics (same TU): Pred_Simulate / RestoreToFrame / PredCommandsPredicted /
// SimsLeft / TbBudgetExpired / TraceHull / TraceLine / EntV3 / EntI / EntMoveType / EntAlive / CvarFloat /
// TickInterval / On / MV_WriteWishWorld / PsWishWorld / s_textureBugHit / s_tbOwnsCmd / s_tbRideHold.
#include "tick_scale.h"        // n_tick:: -- later includes of this TU lean on it
#include "texturebug_pure.h"   // tbw:: -- wallstrafe candidates/aligned_wish/direction (tests/texturebug_test.cpp)
#include "airstuck_pure.h"     // as:: -- k_pi, WallStrafeYaw
#include "air_tricks_pure.h"   // atk:: -- pin kinds, press rungs, climb test (tests/airtricks_test.cpp)

static float AsSurfaceFriction(void* pLocal);   // airstuck.inl (included after this file, same TU)

// ==== kept 1:1 -- shared with HeadSurf / HeadBounce below, do not remove ====

// negative half-gravity per tick (TB naming kept for the 1:1 ports)
static float TB_HalfGravityPerTick() { return TargetPredictZVel(); }
// |vz + flHalf| < tol with flHalf = -6 tests vz == +6. +halfgrav is the RIDE accept
// (head_surf, flat brush bottom); head_bounce wants -halfgrav but is not ported.
static bool TB_IsPositiveHalfGravity(float vz, float flHalf, float tol) { return fabsf(vz + flHalf) < tol; }

// xy inset for the flush-face retry below. Used by BOTH overhead detects
static constexpr float k_tbOverInset = 2.f;

// ceiling brush within flReach above the head.
// A hull already flush against a VERTICAL face (what a TB ride holds it against) reports
// frac 0 nz 0 whichever way the sweep points -- blind. Retry inset in xy so that face is
// off contact. Detect is a GATE: under-detecting kills the feature, so inset is the safe side
static bool TB_DetectCeiling(void* pLocal, const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                             float flReach, GameTrace_t& traceOut)
{
    Vec3 vEnd = vPos;
    vEnd.z += flReach;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(vPos, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    if (traceOut.DidHit() && traceOut.plane.normal.z < -0.7f)
        return true;
    // nothing touched at all -> genuinely clear overhead, no retry (and no second trace)
    if (traceOut.fraction >= 1.f && !traceOut.startsolid && !traceOut.allsolid)
        return false;
    Vec3 vInMins = vMins, vInMaxs = vMaxs;
    vInMins.x += k_tbOverInset; vInMins.y += k_tbOverInset;
    vInMaxs.x -= k_tbOverInset; vInMaxs.y -= k_tbOverInset;
    if (vInMaxs.x <= vInMins.x || vInMaxs.y <= vInMins.y)
        return false;
    TraceHull(vPos, vEnd, vInMins, vInMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    return traceOut.DidHit() && traceOut.plane.normal.z < -0.7f;
}

// HeadBounce's own detect (botox tb_detect_overhead, texturebug_common.h:310). NOT the
// ceiling test: a crease between two displacements has no flat face, so the -0.7 ride filter
// rejects exactly the geometry a bounce lives on. -0.1 = "something overhead", the crease
// line does the work. Same xy-inset retry as TB_DetectCeiling, same reason.
struct TbOverDbg_t { float frac0 = 1.f, nz0 = 0.f; bool ss0 = false, retried = false; };

static bool TB_DetectOverhead(void* pLocal, const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                              float flReach, GameTrace_t& traceOut, TbOverDbg_t& dbg)
{
    Vec3 vEnd = vPos;
    vEnd.z += flReach;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(vPos, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    dbg.frac0 = traceOut.fraction;
    dbg.nz0   = traceOut.plane.normal.z;
    dbg.ss0   = traceOut.startsolid || traceOut.allsolid;
    if (traceOut.fraction < 1.f && traceOut.plane.normal.z < -0.1f)
        return true;
    if (traceOut.fraction >= 1.f && !dbg.ss0)
        return false;
    Vec3 vInMins = vMins, vInMaxs = vMaxs;
    vInMins.x += k_tbOverInset; vInMins.y += k_tbOverInset;
    vInMaxs.x -= k_tbOverInset; vInMaxs.y -= k_tbOverInset;
    if (vInMaxs.x <= vInMins.x || vInMaxs.y <= vInMins.y)
        return false;
    dbg.retried = true;
    TraceHull(vPos, vEnd, vInMins, vInMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    return traceOut.fraction < 1.f && traceOut.plane.normal.z < -0.1f;
}
// botox air_accel_delta (texturebug_common.h:143-169): AirAccelerate's velocity delta for one
// tick, NO surface friction term (botox's own code omits it here -- texture_bug's main press
// uses a separate k_acc that DOES include friction; both kept distinct, faithfully). Used by
// HeadBounce's multi-yaw sweep.
static Vec3 TB_AirAccelDelta(const Vec3& base_vel, float world_yaw, float forward,
                             float accel, float dt)
{
    float wishspeed = forward, yaw = world_yaw;
    if (wishspeed < 0.f) { wishspeed = -wishspeed; yaw += 180.f; }
    if (wishspeed <= 0.f) return Vec3();

    const float rad = DEG2RADF(yaw);
    const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
    const float wishspd = fminf(wishspeed, 30.f);
    const float currentspeed = base_vel.x * wishdir.x + base_vel.y * wishdir.y;
    const float addspeed = wishspd - currentspeed;
    if (addspeed <= 0.f) return Vec3();
    float accelspeed = accel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    return wishdir * accelspeed;
}

// ==== kept 1:1 -- used by pixel surf assist / the whole-map scan (psassist.inl, psmap.inl),
// NOT by TextureBug below. Do not remove: psassist.inl:1227/1308 call TB_FaceSame,
// psassist.inl:1262 and psmap.inl:174 call TB_UlpStepIntoWall. ====

// no-header string compare -- surface.name is usually shared per material but not
// guaranteed, so pointer equality alone would split one brush
static bool TB_SameStr(const char* a, const char* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

// two face probes are the SAME BRUSH FACE. plane.dist cannot separate two flush brushes,
// so the texture name carries the boundary -- hence TEXTURE bug.
// tex=false asks SAME WALL instead (psassist.inl's park walk passes false: maps build long
// walls from differently skinned brushes, and asking the texture question there would stop
// the walk mid-wall)
static bool TB_FaceSame(const GameTrace_t& a, const GameTrace_t& b, bool tex = true)
{
    if (a.fraction >= 1.f || b.fraction >= 1.f) return false;
    if (a.plane.normal.x * b.plane.normal.x + a.plane.normal.y * b.plane.normal.y
      + a.plane.normal.z * b.plane.normal.z < 0.9995f) return false;
    if (fabsf(a.plane.dist - b.plane.dist) > 0.10f) return false;
    return !tex || TB_SameStr(a.surface.name, b.surface.name);
}

// SMALLEST MOVE INTO THE WALL THE ENGINE CAN MAKE.
// TryPlayerMove builds end = origin + dt*v in float and traces m_Delta = end - start, so the
// move into an axis-aligned wall is k * ulp(origin coord), never v*dt. Returns the step toward
// the wall (0 = not axis-aligned, caller keeps the old search). axis_out: 0 = x, 1 = y
static float TB_UlpStepIntoWall(const Vec3& normal, const Vec3& origin, int& axis_out)
{
    axis_out = -1;
    if (fabsf(normal.x) > 0.9999f)      axis_out = 0;
    else if (fabsf(normal.y) > 0.9999f) axis_out = 1;
    else return 0.f;
    const float c = axis_out == 0 ? origin.x : origin.y;
    const float n = axis_out == 0 ? normal.x : normal.y;
    // normal points OUT of the wall, so into the wall is -n
    return fabsf(nextafterf(c, n > 0.f ? -FLT_MAX : FLT_MAX) - c);
}

// per-cmd ceiling, so a pathological tick can never trade a sim spike for a trace spike.
// movement.cpp:2357 zeroes this once per cmd (analytic probe budget, its own pool, never
// spends TB's sims) -- kept here even though TextureBug below no longer reads it
static int s_tbFastProbes = 0;

// below this two variants move the player <1.6e-4u apart over a tick -- 200x under the
// 0.03125 surface extension that decides every collision here
static constexpr float k_tbDupVelEps = 0.01f;

// ONE dedup list per hull per tick (HeadBounce's sweep; TextureBug's own ladder search does
// not need one -- botox's doesn't either, see texture_bug's dx loop)
struct TbDedup_t
{
    static constexpr int k_max = 288;
    Vec3 seen[k_max];
    int  count = 0;

    bool SeenOrAdd(const Vec3& d)
    {
        for (int i = 0; i < count; ++i)
            if (fabsf(d.x - seen[i].x) < k_tbDupVelEps && fabsf(d.y - seen[i].y) < k_tbDupVelEps)
                return true;
        if (count < k_max) seen[count++] = d;
        return false;
    }
};

// ---- TB phase, for the on-screen indicator (hud/indicators.cpp, which has its own labels) ----
enum TbPhase_t { TBP_OFF = 0, TBP_SCAN, TBP_HUG, TBP_ARM, TBP_PIN, TBP_RIDE, TBP_PARK };
static TbPhase_t s_tbPhase   = TBP_OFF;
static int   s_tbRideLen = 0;   // pinned ticks in the current ride
static float s_tbIndD    = 0.f; // gap to the wall, for the HUD
static float s_tbIndVin  = 0.f; // v_in_now  -- live speed INTO the plane
static float s_tbIndAim  = 0.f; // press_target -- where the hold aims

void TB_GetInd(int& phase, int& rideLen, int& seams, float& D, float& vin, float& aim)
{
    phase = (int)s_tbPhase; rideLen = s_tbRideLen; seams = 0;
    D = s_tbIndD; vin = s_tbIndVin; aim = s_tbIndAim;
}

// diag throttles (DiagOk), one per body
static int s_tbDiagTick = -1000;
static int s_hsDiagTick = -1000;
static int s_hbDiagTick = -1000;

// ==== kept 1:1 -- HeadSurf / HeadBounce constants (unchanged from the prior CSS port) ====
static constexpr float k_hsRampNz    = 0.05f;
static constexpr float k_tbXyCreepFrac = 0.5f;
static constexpr int   k_hbChain     = 3;
static constexpr int   k_hbChainMax  = 6;
static constexpr float k_hbGapMargin = 2.f;
static constexpr float k_hbCutMin    = 1.f;
static constexpr int   k_hbSimCap    = 160;


// ============================================================================
// shared by TEXTURE BUG (below), AIR STUCK (airstuck.inl) and WALL CLIMB (wallclimb.inl) -- 11-07 rewrite.
// All three are engine searches from START: build a few cmds that differ only in the levers CSS
// gives a player in the air (wish direction, wish size, stance), fly each on the real game movement
// (Pred_Simulate) and send the first one whose END has the state the trick needs. The physics is
// in air_tricks_pure.h's header; nothing here models it, the sims are the model.
// ============================================================================

// nearest face a 16-way horizontal hull sweep from `o` reaches within `reach`. Floors (n.z >= 0.7, you
// would just land) never count; ceilings only when asked (air stuck's creases); a face with no
// horizontal part cannot be pressed into, so it is skipped too. gap = distance off it along the normal
struct AtFace { bool ok = false; Vec3 n{}; Vec3 n2{}; float gap = 1e9f; };

static AtFace AT_FindFace(void* pLocal, const Vec3& o, const Vec3& mins, const Vec3& maxs, float reach, bool ceilings)
{
    AtFace f;
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    for (int k = 0; k < 16; ++k)
    {
        const float a  = (float)k * (as::k_pi * 2.f / 16.f);
        const float cx = cosf(a), cy = sinf(a);
        GameTrace_t tr{};
        TraceHull(o, Vec3(o.x + cx * reach, o.y + cy * reach, o.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.startsolid || tr.fraction >= 1.f) continue;
        const Vec3& n = tr.plane.normal;
        if (n.z >= atk::k_standNz || (!ceilings && n.z <= -atk::k_standNz)) continue;
        const float nl = sqrtf(n.x * n.x + n.y * n.y);
        if (nl < 0.05f) continue;
        const Vec3  n2(n.x / nl, n.y / nl, 0.f);
        const float toward = -(cx * n2.x + cy * n2.y);
        if (toward <= 0.05f) continue;
        const float g = tr.fraction * reach * toward;
        if (g < f.gap) { f.gap = g; f.n = n; f.n2 = n2; f.ok = true; }
    }
    return f;
}

struct AtEnd { Vec3 org{}, vel{}; bool ground = false, walk = true; };
static AtEnd AT_Read(void* pLocal)
{
    AtEnd e;
    e.org    = EntV3(pLocal, g_offOrigin);
    e.vel    = EntV3(pLocal, g_offVelocity);
    e.ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    e.walk   = EntMoveType(pLocal) == MOVETYPE_WALK;
    return e;
}
static atk::Pin AT_Pin(const AtEnd& e) { return atk::Classify(e.ground, e.walk, e.vel.z, e.vel.Length2D(), TargetPredictZVel()); }

// a cmd that wishes world direction (dx, dy) at `mag` in `duck` stance; everything else stays yours
static UserCmd_t AT_Wish(const UserCmd_t& base, float dx, float dy, float mag, bool duck)
{
    UserCmd_t c = base;
    MV_WriteWishWorld(&c, Vec3(dx, dy, 0.f), mag);
    c.upmove  = 0.f;
    c.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    c.buttons = duck ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
    return c;
}
static UserCmd_t AT_Stance(UserCmd_t c, bool duck)
{
    c.buttons = duck ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
    return c;
}

// the cmd a trick proved this tick. The aimbot runs after the movement pipeline and re-expresses
// view / forward / side (silent aim), which moves the wish by float bits a pin lives on:
// Movement_AirStuckLastWriter puts it back (AS_FinalStamp, airstuck.inl). Keyed to the cmd number,
// so a pipeline path that skips the last writer can never stamp a later cmd
static struct { int cmd = -1; Vec3 view{}; float fwd = 0.f, side = 0.f; int buttons = 0; const char* who = ""; } s_atStamp;
static void AT_Stamp(const UserCmd_t& c, const char* who)
{
    s_atStamp.cmd = c.command_number; s_atStamp.view = c.viewangles;
    s_atStamp.fwd = c.forwardmove; s_atStamp.side = c.sidemove; s_atStamp.buttons = c.buttons; s_atStamp.who = who;
}

static bool AT_CanSim() { return SimsLeft() > 0 && !TbBudgetExpired(); }

// ============================================================================
// botox texturebug_common.h / texturebug_wallstrafe.h ports the TextureBug engine needs
// ============================================================================

// bspflags.h CONTENTS_LADDER (same constant fireman.inl uses for its own ladder mask;
// re-declared here since fireman.inl is included AFTER this file in movement.cpp)
static constexpr unsigned k_tbContentsLadder = 0x20000000;
static bool TB_IsLadderTrace(const GameTrace_t& tr) { return (tr.contents & k_tbContentsLadder) != 0; }

// botox tb_move_leaves_wall (texturebug_common.h:253-266): steering away from the wall AND
// your own air accel moves you off it this tick -- a wish inside the no-gain cone adds nothing
static bool TB_MoveLeavesWall(const UserCmd_t* pCmd, const Vec3& n, float fwd, float side,
                              const Vec3& vel, float maxSpeed, float crop, float accel, float dt)
{
    constexpr float k_leaveSpeed = 1.f;
    if (!TB_SteeringAwayFromWall(pCmd, n, fwd, side)) return false;
    const float nl = sqrtf(n.x * n.x + n.y * n.y);
    if (nl < 0.001f) return false;
    const float yaw   = pCmd->viewangles.y - RAD2DEGF(atan2f(side, fwd));
    const float speed = fminf(sqrtf(fwd * fwd + side * side) * crop, fmaxf(0.f, maxSpeed));
    const Vec3  v     = vel + TB_AirAccelDelta(vel, yaw, speed, accel, dt);
    return (v.x * n.x + v.y * n.y) / nl > k_leaveSpeed;
}

// botox tb_move_latches_ladder (texturebug_common.h:268-308): refuse a catch that would carry
// the hull onto a ladder brush (a grab kills the pin -- neither feature should fight the other)
static bool TB_MoveLatchesLadder(void* pLocal, const UserCmd_t* pCmd, const Vec3& org,
                                 const Vec3& vel, const Vec3& mins, const Vec3& maxs,
                                 bool checkCliff)
{
    if (!pCmd || (pCmd->forwardmove == 0.f && pCmd->sidemove == 0.f)) return false;
    constexpr unsigned k_ladderMask = MASK_PLAYERSOLID & ~0x10000u;   // & ~CONTENTS_PLAYERCLIP
    TraceFilterWorldProps fil; fil.Setup(pLocal);

    Vec3 fwd, right;
    AngleVectors(pCmd->viewangles, &fwd, &right, nullptr);
    Vec3 wish = fwd * pCmd->forwardmove + right * pCmd->sidemove;
    const float len = wish.Length();
    if (len > 1e-6f)
    {
        wish *= 1.f / len;
        GameTrace_t t{};
        TraceHull(org, org + wish * 2.f, mins, maxs, k_ladderMask, &fil, &t);
        if ((t.fraction < 1.f || t.startsolid) && t.plane.normal.z != 1.f && TB_IsLadderTrace(t))
            return true;
    }
    if (!checkCliff) return false;

    if (vel.z > 0.f || vel.z <= -50.f || vel.x == 0.f || vel.y == 0.f) return false;
    const float speed = vel.Length();
    if (speed < 0.01f) return false;
    const Vec3 from(org.x, org.y, org.z - 6.f);
    const Vec3 to = org - vel * (24.f / speed);
    GameTrace_t t{};
    TraceHull(from, to, mins, maxs, k_ladderMask, &fil, &t);
    return t.fraction < 1.f && !t.startsolid && t.plane.normal.z != 1.f && TB_IsLadderTrace(t);
}

// botox tb_head_wall_reachable (texturebug.cpp:242-258): after a confirmed surf, wallstrafe
// assistance needs a wall at HEAD height, not just beside the body
static bool TB_HeadWallReachable(void* pLocal, const Vec3& origin, const Vec3& end,
                                 const Vec3& mins, const Vec3& maxs, const Vec3& normal)
{
    constexpr float eps = 0.03125f;
    Vec3 headMins = mins, headMaxs = maxs;
    headMins.z = maxs.z - eps;
    headMaxs.z = maxs.z + eps;
    GameTrace_t wall{};
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(origin + normal * (2.f * eps), end, headMins, headMaxs, MASK_PLAYERSOLID, &filter, &wall);
    const float sameFace = wall.plane.normal.x * normal.x + wall.plane.normal.y * normal.y
                          + wall.plane.normal.z * normal.z;
    return !wall.startsolid && !wall.allsolid && wall.fraction < 1.f
        && fabsf(wall.plane.normal.z) < 0.1f && sameFace >= 0.98f && !TB_IsLadderTrace(wall);
}

static float TB_PlaneGap(const cplane_mv& plane, const Vec3& origin, const Vec3& mins, const Vec3& maxs);   // below

// EXACT nearest-hull-corner gap along a plane normal (botox hull_plane_dist +
// texture_bug's own "g = org.n - plane_d + corner support" line, texturebug.cpp:517-519 /
// texturebug_common.h:79-95 -- both forms are the same quantity, this is the center+support
// form). Positive = outside the face, 0 = touching, negative = inside (CSS-kept, used by
// both TextureBug below and HeadBounce's cost gate).
static float TB_PlaneGap(const cplane_mv& plane, const Vec3& origin,
                         const Vec3& mins, const Vec3& maxs)
{
    const Vec3 center(origin.x + (mins.x + maxs.x) * 0.5f,
                      origin.y + (mins.y + maxs.y) * 0.5f,
                      origin.z + (mins.z + maxs.z) * 0.5f);
    const Vec3 ext((maxs.x - mins.x) * 0.5f, (maxs.y - mins.y) * 0.5f,
                   (maxs.z - mins.z) * 0.5f);
    const float support = fabsf(ext.x * plane.normal.x) + fabsf(ext.y * plane.normal.y)
                        + fabsf(ext.z * plane.normal.z);
    return (center.x * plane.normal.x + center.y * plane.normal.y + center.z * plane.normal.z)
         - (plane.dist + support);
}

// ============================================================================
// TEXTURE BUG -- botox texture_bug(), ported whole (texturebug.cpp:341-1029). The trick
// (same PAPER 9.4.2 the old engine's comments cited): a box brush picks the side it collides
// with by the raw fraction BEFORE the max(0,..) clamp, highest wins. Wall-hugging puts the
// perpendicular gap inside the surface-extension band, so the TOP side wins and clips vz to
// -halfgravity. Pass order, and what each exists for (botox's own commit ladder):
//   user cmd   the player's own press already pins/bounces -> keep it (1 sim)
//   away-check steering off the wall two ticks running -> let go, no search at all
//   ladder     dx rungs from the smallest crossing press up to the head-pin bound, picked by
//              a short look-ahead rollout when a rung pins (does the hold actually SURVIVE)
//   park       step off the plane to the smallest gap that still keeps the catch
//   unstick    flush + dead stop -> step to a known-clear gap (0.03125 / 0.125 / 0.5)
//   wallstrafe candidate presses that gain speed along the wall while still catching (the
//              probe runs whenever gap is close OR you are already closing, botox's own gate)
//   drive-in   nothing above worked -> hand the cmd back with the user's own stance
// ============================================================================
// botox texturebug.cpp:205-214 + :426/:547-563 shift_comp. After tb simmed a cmd, resim the cmd as tb
// leaves it and keep the END; next cmd's START (the server's END once sync is armed) minus that, along
// the wall normal, is how far the server moves past our sim. Its median over unclipped ticks probes
// every rung at dx + shift ("our sim of dx + shift = the server's of dx"), while dx itself is sent.
// botox measured 1.3..2.3e-5 on a LOOPBACK listen server (client.dll vs server.dll float codegen):
// without it the 0.9-ulp hold windows sat between our rungs -- pins in our sim, falls on the server.
static int  s_tbSentCmd = -1;
static Vec3 s_tbSentOrg{};
static int  s_tbSimsThisCmd = 0;
static char s_tbLastAct = 'N';
static void TextureBugImpl(void* pLocal, UserCmd_t* pCmd);
static void TextureBug(void* pLocal, UserCmd_t* pCmd)
{
    const auto noteSent = [&]() {
        s_tbSentCmd = -1;
        if (s_tbSimsThisCmd > 0)
        {
            RestoreToFrame(PredCommandsPredicted() - 1);
            Pred_Simulate(pLocal, pCmd);
            s_tbSentCmd = pCmd->command_number;
            s_tbSentOrg = EntV3(pLocal, g_offOrigin);
        }
    };
    s_tbSimsThisCmd = 0;
    // TB key = ref engine first (wallengine.inl, ref , then the botox engine below on a
    // miss. A pin needs the hull ~1 ulp off the face AND a press of dx in [gap, (eps-gap)*dz/(H+eps)]
    // (~8e-5u at hold, botox texturebug.cpp:346/816). ref's fixed wish grid only ever sends "closest gap",
    // a hard hit parks at 1/32 and it never pins (log 11-08: WETB pins=0 every second). Botox parks the hull
    // at a sub-ulp gap (K) and ladders dx off it (P/F) -- exactly the half ref lacks
    if (On(g_textureBug, g_textureBugKey, g_textureBugMode) && EntAlive(pLocal))
    {
        s_tbSyncWant = true;
        const UserCmd_t user = *pCmd;
        const int rideLen0 = s_tbRideLen;
        const bool veto = WeTextureBugTick(pLocal, pCmd);
        const char* by = "sg";
        if (!s_textureBugHit && !veto)
        {
            const UserCmd_t weCmd = *pCmd;
            const bool weOwn = s_tbOwnsCmd;
            const TbPhase_t wePhase = s_tbPhase;
            *pCmd = user;   // botox reads the player's own cmd as `in`
            TextureBugImpl(pLocal, pCmd);
            if (s_textureBugHit || s_tbOwnsCmd)
            {
                by = "bx";
                AT_Stamp(*pCmd, "tb");   // replaces ref's approach stamp, else AS_FinalStamp restores it
            }
            else
            {
                *pCmd = weCmd; s_tbOwnsCmd = weOwn; s_tbPhase = wePhase;   // botox found nothing: ref's approach stands
            }
        }
        s_tbRideLen = s_textureBugHit ? rideLen0 + 1 : 0;
        if (s_textureBugHit && s_tbRideLen == 1)
            DebugLog("WETB: ride start by=%s z=%.3f vz=%.2f xy=%.1f duck=%d", by, s_prePredOrigin.z, s_prePredVel.z,
                     s_prePredVel.Length2D(), (pCmd->buttons & IN_DUCK) ? 1 : 0);
        noteSent();
        return;
    }
    TextureBugImpl(pLocal, pCmd);
    noteSent();
}

static void TextureBugImpl(void* pLocal, UserCmd_t* pCmd)
{
    constexpr float k_eps       = 0.03125f;
    constexpr float k_touch     = k_eps * 1.3f;
    constexpr float k_contact   = 0.05f;
    constexpr float k_ratio     = 1.4f;
    constexpr float k_riseRatio = 1.1f;
    constexpr int   k_ladderCap = 48;
    constexpr float k_duckCrop  = 0.34f;
    constexpr float k_wishMax   = 450.f;
    constexpr int   k_moveKeys  = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
    constexpr int   k_rideGrace = 12;
    static constexpr float k_keep[] = { 0.000001f, 0.0001f, 0.002f, 0.05f, 0.3f };

    static int   s_rideDuck = -1, s_rideUser = -1, s_miss = 0, s_run = 0, s_pinCmd = -1;
    static int   s_parkMargin = 0, s_parkCmd = -1;
    static float s_parkFrom = 0.f, s_parkTo = 0.f;
    static bool  s_rode = false;
    static int   s_away = 0;
    static int   s_touchCmd = -1;
    static int   s_rideGraceLeft = 0;

    const auto drop = [&]() {
        s_rode = false; s_rideDuck = -1; s_miss = 0; s_run = 0; s_parkMargin = 0; s_parkCmd = -1;
    };

    s_textureBugHit = false;
    s_tbOwnsCmd = false;

    const bool armed  = On(g_textureBug, g_textureBugKey, g_textureBugMode) && EntAlive(pLocal);
    // botox texturebug.cpp:387 sync_arm( k_sync_tb, armed ): online, cl_pred_optimize 2 lets the
    // client keep its own START when the server END differs < 0.0625u -- the pin window is 0.029u
    // and the rungs are ulps, so every plan was built on a START the server never had. AS_Sync
    // (runs after this in the same cmd) owns the cvar for as + tb + wc
    s_tbSyncWant = armed;
    // botox gates on backup_data (cmd START flags), not the predicted END
    if (!armed || (s_prePredFlags & FL_ONGROUND)
        || EntMoveType(pLocal) != MOVETYPE_WALK)
    {
        drop();
        s_tbPhase = TBP_OFF; s_tbRideLen = 0; s_rideGraceLeft = 0; s_tbRideHold = 0;
        return;
    }

    // botox tb_restore( frame ) BEFORE every read below: org/vel/duck/hull = cmd START, the
    // state every sim() restores to. Reading here unrestored got the predicted END (or the
    // last sim AutoAlign/PsSurfFix left), so gap/vIn/rising never matched the sims -> no pin.
    const int   frame   = PredCommandsPredicted() - 1;
    RestoreToFrame(frame);
    const Vec3  org      = EntV3(pLocal, g_offOrigin);
    const Vec3  vel      = EntV3(pLocal, g_offVelocity);
    const bool  ducked0  = EntDucking(pLocal);
    const float friction = AsSurfaceFriction(pLocal);
    const Vec3  mins     = EntV3(pLocal, g_offMins);
    const Vec3  maxs     = EntV3(pLocal, g_offMaxs);
    const float ipt      = TickInterval();
    const float pin      = TB_HalfGravityPerTick();
    const float sv_airaccel = CvarFloat("sv_airaccelerate", 10.f);
    const float k_acc     = sv_airaccel * ipt * friction;
    const bool  debug     = g_debugLogEnable;

    const UserCmd_t in = *pCmd;   // the player's own cmd, untouched, for the whole function
    const bool wallstrafeEnabled = g_tbWallstrafeOpt;
    int sims = 0;
    const bool rising = vel.z + pin > 1e-3f;

    struct EndT { bool ok = false; Vec3 org{}, vel{}; bool ride = false, bounce = false, ground = false; };
    const auto sim = [&](UserCmd_t c) -> EndT {
        EndT e{};
        if (SimsLeft() <= 0) return e;
        RestoreToFrame(frame);
        Pred_Simulate(pLocal, &c);
        ++sims; ++s_tbSimsThisCmd;
        e.ok     = true;
        e.org    = EntV3(pLocal, g_offOrigin);
        e.ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        e.vel    = EntV3(pLocal, g_offVelocity);
        const bool pinned = !e.ground && EntMoveType(pLocal) == MOVETYPE_WALK
                          && IsTargetVz(e.vel.z, 1e-3f) && e.vel.Length2D() >= 1.f;
        e.ride   = pinned && vel.z + pin < 0.f;
        e.bounce = pinned && rising && (!wallstrafeEnabled || tbw::RetainsSpeed(vel.Length2D(), e.vel.Length2D()));
        e.ok     = EntMoveType(pLocal) == MOVETYPE_WALK;   // a ladder latch is never ours
        return e;
    };
    const auto withDuck = [](UserCmd_t c, int duck) {
        c.buttons = duck ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
        return c;
    };
    const auto commit = [&](const UserCmd_t& c) {
        pCmd->forwardmove = c.forwardmove;
        pCmd->sidemove    = c.sidemove;
        pCmd->buttons     = c.buttons;
    };
    // botox move_fix_to_yaw -- rotate a (fwd,side) pair written at world `yaw` into pCmd's own
    // view frame, silently (view angles never move). Uses a THROWAWAY cmd so pCmd itself is
    // never touched before the caller's own commit() -- CorrectMovement's contract only needs
    // a UserCmd_t*, not pCmd specifically.
    const auto fixToYaw = [&](float yaw, UserCmd_t& c) {
        UserCmd_t tmp = c;
        tmp.viewangles = in.viewangles;
        Vec3 wish = in.viewangles; wish.y = yaw;
        CorrectMovement(&tmp, wish, in.viewangles);
        c.forwardmove = tmp.forwardmove;
        c.sidemove    = tmp.sidemove;
    };

    char act = 'N';
    float gap = -1.f, dxHit = 0.f, vIn = 0.f;
    char  pkWhy = '-';   // last park reject: o=sim/ground/speed h=head g=end gap t=touched
    float pkG1 = 0.f;
    bool flipped = false;
    float maxSpeed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    if (maxSpeed <= 1.f) maxSpeed = 450.f;

    // epilogue: every branch below calls this then returns, matching botox's own log()+return
    // pattern at every exit of texture_bug(). Sets the HUD indicator + contract flags + the
    // detection sound (botox m_texture_bug_sound family, EbPlaySound shared with edge bug).
    static bool s_tbSoundPrev = false;
    const auto TbFinish = [&]() {
        s_rideGraceLeft = (s_textureBugHit || s_tbOwnsCmd) ? k_rideGrace : (s_rideGraceLeft > 0 ? s_rideGraceLeft - 1 : 0);
        s_tbRideHold = s_rideGraceLeft;
        s_tbRideLen  = s_run;
        s_tbIndD     = gap;
        s_tbIndVin   = vIn;
        s_tbIndAim   = dxHit;
        s_tbPhase = (act == 'K' || act == 'H' || act == 'S') ? TBP_PARK
                  : (act == 'P' || act == 'R' || act == 'B' || act == 'F') ? (s_run >= 2 ? TBP_RIDE : TBP_PIN)
                  : (act == 'W' || act == 'U' || act == 'L') ? TBP_HUG
                  : TBP_SCAN;
        if (s_textureBugHit && !s_tbSoundPrev && g_tbSound > 0)
            EbPlaySound(g_tbSound, g_tbSoundCustom, g_tbSoundVolume);
        s_tbSoundPrev = s_textureBugHit;
        s_tbLastAct = act;
        // a pin lives on the wish's float bits: the aimbot's later re-expression must not move them
        if (s_textureBugHit) AT_Stamp(*pCmd, "tb");
        if (debug && DiagOk(s_tbDiagTick, act != 'N'))
            DebugLog("TB: act=%c gap=%.6f vz=%.2f xy=%.1f duck=%d run=%d flip=%d dx=%.3e sims=%d "
                     "hit=%d own=%d hold=%d vin=%.4f pk=%c pg=%.6f",
                     act, gap, vel.z, vel.Length2D(), (pCmd->buttons & IN_DUCK) ? 1 : 0, s_run,
                     (int)flipped, dxHit, sims, (int)s_textureBugHit, (int)s_tbOwnsCmd, s_tbRideHold,
                     vIn, pkWhy, pkG1);
    };

    const int userDuck = (in.buttons & IN_DUCK) ? 1 : 0;
    if (s_rideDuck >= 0 && userDuck != s_rideUser) s_rideDuck = -1;
    const int held = s_rideDuck >= 0 ? s_rideDuck : userDuck;

    bool allowDuck  = (g_tbTypesMask & 1) != 0;
    bool allowStand = (g_tbTypesMask & 2) != 0;
    if (!allowDuck && !allowStand) allowDuck = allowStand = true;   // botox: never both off
    // auto duck off: TB never crouches you, only keeps your own crouch
    if (!g_tbAutoDuck && !userDuck) { allowDuck = false; allowStand = true; }
    const auto allowed = [&](int duck) { return duck ? allowDuck : allowStand; };
    // rising = a bounce: your stance, never a flip, never kept for the fall
    int stance = rising ? held : (allowed(held) ? held : !held);
    const UserCmd_t user = withDuck(in, stance);

    // ================= 1. THE WALL =========================================
    const float reach = std::clamp(g_tbReach, 0.f, 48.f);
    Vec3  n{};
    float planeD = 0.f;
    const float grav = -2.f * pin / (ipt > 0.f ? ipt : 0.015625f);
    {
        GameTrace_t primary{};
        float foundAngle = 0.f;
        bool wallFound = MV_DetectWallLines(pLocal, org, mins, maxs, 0.f, primary, foundAngle,
                                            0.05f, reach, nullptr);
        if (wallFound)
        {
            n = primary.plane.normal;
            planeD = primary.plane.dist;
            gap = TB_PlaneGap(primary.plane, org, mins, maxs);
        }
        if (!wallFound)
        {
            if (debug && s_touchCmd == pCmd->command_number - 1)
                DebugLog("TB: off wall vz=%.2f org=%.7f,%.7f,%.7f", vel.z, org.x, org.y, org.z);
            drop();
            s_tbPhase = TBP_SCAN; s_rideGraceLeft = 0; s_tbRideHold = 0;
            return;
        }
        if (gap <= k_contact) s_touchCmd = pCmd->command_number;
    }
    vIn = -(vel.x * n.x + vel.y * n.y);

    // shift_comp (see TextureBug above): START - our sim of the sent cmd, along n
    float shiftComp = 0.f, divN = 0.f;
    {
        const bool divOk = s_tbSentCmd >= 0 && pCmd->command_number == s_tbSentCmd + 1;
        if (divOk)
            divN = (org.x - s_tbSentOrg.x) * n.x + (org.y - s_tbSentOrg.y) * n.y + (org.z - s_tbSentOrg.z) * n.z;
        constexpr int k_dn = 16;
        static float s_dn[k_dn];
        static int s_dnN = 0;
        if (divOk && (gap > 0.5f || ((s_tbLastAct == 'K' || s_tbLastAct == 'H') && gap > 0.f)) && fabsf(divN) < 1e-4f)
            s_dn[s_dnN++ % k_dn] = divN;
        if (s_dnN >= 6)
        {
            float d[k_dn];
            const int cnt = s_dnN < k_dn ? s_dnN : k_dn;
            std::copy(s_dn, s_dn + cnt, d);
            std::nth_element(d, d + cnt / 2, d + cnt);
            shiftComp = std::clamp(-d[cnt / 2], 0.f, 4e-5f);
        }
        static int s_dnLogged = 0;
        if (debug && s_dnN >= 6 && s_dnN / 16 != s_dnLogged)
        {
            s_dnLogged = s_dnN / 16;
            DebugLog("TB: shift samples=%d median dn=%.3e cp=%.2e", s_dnN, -shiftComp, shiftComp);
        }
    }
    const float yawIn = RAD2DEGF(atan2f(-n.y, -n.x));

    // one ulp of the coordinate the wall normal moves (botox texturebug.cpp:560-568) -- rungs
    // / park margins / the edge-surf band floor below all move the hull the same under it.
    // Hoisted here (botox computes it once, same scope as the stance pick AND the ladder/park
    // below) so sections 2, 6 and 7 all share one value
    float ulp = 1e-7f;
    {
        const float coord = fmaxf(fabsf(org.x * n.x), fabsf(org.y * n.y));
        if (coord > 0.f) { int ex = 0; frexpf(coord, &ex); ulp = ldexpf(1.f, ex - 24); }
    }

    // botox texturebug.cpp:643-651: last cmd's park did not land where planned (server/engine refused
    // the sub-ulp end) -> widen the park floor (margin doubles), else reset it
    if (s_parkCmd >= 0 && pCmd->command_number == s_parkCmd + 1)
    {
        if (s_parkFrom - gap < 0.5f * (s_parkFrom - s_parkTo) || (s_parkTo < 0.5f * k_eps && gap > 0.9f * k_eps))
        {
            s_parkMargin = s_parkMargin ? s_parkMargin * 2 : 2;
            if (debug)
                DebugLog("TB: park refused from=%.6f plan=%.6f got=%.6f vin=%.3f margin=%d", s_parkFrom, s_parkTo, gap, vIn, s_parkMargin);
        }
        else
            s_parkMargin = 0;
    }
    s_parkCmd = -1;

    // ================= 3. USER CMD (natural pin/bounce) ====================
    UserCmd_t raw = in;
    if (wallstrafeEnabled && TB_MoveLeavesWall(&raw, n, raw.forwardmove, raw.sidemove, vel,
                                               maxSpeed,
                                               (ducked0 || (raw.buttons & IN_DUCK)) ? k_duckCrop : 1.f,
                                               sv_airaccel, ipt))
    {
        drop();
        pCmd->forwardmove = raw.forwardmove;
        pCmd->sidemove    = raw.sidemove;
        act = 'S';
        TbFinish();
        return;
    }
    const EndT userEnd = sim(user);   // reused below for userAlongSpeed -- one sim, not two
    // botox :686 user_dies: YOUR move dead-stops on the wall (the next brush's lip under 0.03125).
    // Only then does UNSTICK step off the plane. Ungated, U fired after every park and backed the
    // hull out to 0.03125 every other tick -- never inside the < 0.031 pin window (K/U ping-pong)
    const float xy0 = vel.Length2D();
    const bool userDies = userEnd.ok && xy0 > 50.f && userEnd.vel.Length2D() < 0.2f * xy0;
    if (userEnd.ok && (userEnd.ride || userEnd.bounce))
    {
        commit(user);
        s_textureBugHit = true;
        s_miss = 0; s_rode = true; s_pinCmd = pCmd->command_number; ++s_run;
        act = userEnd.bounce ? 'B' : 'R';
        { TbFinish(); return; }
    }

    // ================= 4. STEERING-AWAY COUNTER =============================
    s_away = TB_MoveLeavesWall(&in, n, in.forwardmove, in.sidemove, vel, maxSpeed,
                              (ducked0 || userDuck) ? k_duckCrop : 1.f, sv_airaccel, ipt)
           ? s_away + 1 : 0;
    if (s_away > (s_run > 0 ? 1 : 0))
    {
        drop();
        act = 'S';
        { TbFinish(); return; }
    }

    // ================= 5. THE PRESS (botox press_from, texturebug.cpp:695-721) =============
    {
        const float nxy = sqrtf(n.x * n.x + n.y * n.y);
        const Vec3  tangent(-n.y / fmaxf(nxy, 1e-6f), n.x / fmaxf(nxy, 1e-6f), 0.f);
        const float along = vel.x * tangent.x + vel.y * tangent.y;
        const float direction = tbw::Direction(along, along);
        const float vOld = -(vel.x * n.x + vel.y * n.y);   // ulp (used by 6/7) hoisted above, section 2

        const auto pressFrom = [&](float vOldN, bool dk0, float vPlane, int duck, float vz,
                                   bool keepSpeed) -> UserCmd_t {
            const float vO = vOldN / fmaxf(nxy, 1e-6f);
            const float vT = (vPlane + (vz + pin) * n.z) / fmaxf(nxy, 1e-6f);
            const float crop = (duck || dk0) ? k_duckCrop : 1.f;
            float w = vT >= vO ? fmaxf((vT - vO) / k_acc, vT) : -fmaxf((vO - vT) / k_acc, -vT);
            w = std::clamp(w / crop, -k_wishMax, k_wishMax);
            UserCmd_t c = withDuck(in, duck);
            c.forwardmove = fabsf(w);
            c.sidemove    = 0.f;
            if (wallstrafeEnabled && rising && keepSpeed)
            {
                const float speed = fminf(fabsf(w) * crop, fmaxf(0.f, maxSpeed));
                const float sign  = w < 0.f ? -1.f : 1.f;
                const float delta = sign * fminf(k_acc * speed, fmaxf(0.f, fminf(speed, 30.f) - vO * sign));
                const float maxWish = fminf(k_wishMax * crop, fmaxf(0.f, maxSpeed));
                const tbw::Wish_t wish = tbw::AlignedWish(vO, along * direction, delta, maxWish, k_acc);
                if (wish.gain > 1e-4f)
                {
                    c.forwardmove = wish.into / crop;
                    c.sidemove    = wish.along * direction / crop;
                    fixToYaw(yawIn, c);
                    c.buttons &= ~k_moveKeys;
                    return c;
                }
            }
            fixToYaw(w >= 0.f ? yawIn : yawIn + 180.f, c);
            c.buttons &= ~k_moveKeys;
            return c;
        };
        const auto press = [&](float vT, int duck) { return pressFrom(vOld, ducked0, vT, duck, vel.z, true); };

        bool haveStrafe = false;
        UserCmd_t strafeCmd{};
        bool headAssistBlocked = false;
        float userAlongSpeed = -FLT_MAX;
        if (userEnd.ok && !userEnd.ground)
            userAlongSpeed = (userEnd.vel.x * -n.y + userEnd.vel.y * n.x) / fmaxf(nxy, 1e-6f);

        // ---- wallstrafe candidate probe (botox texturebug.cpp:725-793) ----
        if (wallstrafeEnabled && SimsLeft() > 3 && (gap <= k_touch || vIn > 1.f))
        {
            const float yawRad = DEG2RADF(raw.viewangles.y);
            const Vec3  wish(cosf(yawRad) * raw.forwardmove + sinf(yawRad) * raw.sidemove,
                             sinf(yawRad) * raw.forwardmove - cosf(yawRad) * raw.sidemove, 0.f);
            const float dir2 = tbw::Direction(wish.x * tangent.x + wish.y * tangent.y, along);
            const float crop = (stance || ducked0) ? k_duckCrop : 1.f;
            const float wishspeed = fminf(k_wishMax * crop, fmaxf(0.f, maxSpeed));
            const auto wishes = tbw::Candidates(vOld / fmaxf(nxy, 1e-6f), along * direction, wishspeed, k_acc);
            float bestSpeed = fmaxf(along * direction,
                                    userAlongSpeed == -FLT_MAX ? along * direction : userAlongSpeed * dir2);
            for (const auto& w : wishes)
            {
                if (w.gain <= 1e-4f || SimsLeft() <= 0) continue;
                UserCmd_t c = withDuck(in, stance);
                c.forwardmove = w.into * k_wishMax;
                c.sidemove    = w.along * dir2 * k_wishMax;
                fixToYaw(yawIn, c);
                c.buttons &= ~k_moveKeys;
                const EndT e = sim(c);
                if (!e.ok || e.ground || !tbw::RetainsSpeed(vel.Length2D(), e.vel.Length2D())) continue;

                GameTrace_t wall{};
                TraceFilterWorldProps filter; filter.Setup(pLocal);
                TraceHull(e.org, e.org - n * (reach + k_touch), mins, maxs, MASK_PLAYERSOLID, &filter, &wall);
                const float sameFace = wall.plane.normal.x * n.x + wall.plane.normal.y * n.y + wall.plane.normal.z * n.z;
                const float endGap = gap + (e.org.x - org.x) * n.x + (e.org.y - org.y) * n.y + (e.org.z - org.z) * n.z;
                if (wall.startsolid || wall.allsolid || wall.fraction >= 1.f || sameFace < 0.98f
                    || TB_IsLadderTrace(wall) || endGap > fmaxf(gap, k_touch) + k_touch)
                    continue;
                if (e.ride || e.bounce)
                {
                    commit(c);
                    s_textureBugHit = true;
                    s_rode = true;
                    s_miss = 0; s_rideDuck = stance; s_rideUser = userDuck; s_pinCmd = pCmd->command_number;
                    ++s_run;
                    act = 'P';
                    { TbFinish(); return; }
                }
                if (s_rode && !rising && !TB_HeadWallReachable(pLocal, e.org, e.org - n * (reach + k_touch), mins, maxs, n))
                    { headAssistBlocked = true; continue; }
                const float speed = (e.vel.x * tangent.x + e.vel.y * tangent.y) * dir2;
                if (speed > bestSpeed + 0.005f) { bestSpeed = speed; strafeCmd = c; haveStrafe = true; }
            }
        }
        const auto sendStrafe = [&]() { commit(strafeCmd); s_tbOwnsCmd = true; act = 'W'; };

        // ---- 6. LADDER SEARCH (botox texturebug.cpp:801-952) ----
        if (gap <= k_touch)
        {
            const float g0 = fmaxf(gap, 1e-7f);
            const float dz = fabsf((vel.z + pin) * ipt) + 1e-4f;
            const auto ladder = [&](int duck, UserCmd_t& out, bool allowThisDuck) -> bool {
                if (!allowThisDuck) return false;
                const float hp = (duck || ducked0) ? 45.f : 62.f;   // CSS hull heights (botox csgo 54/72)
                const float hi = fmaxf((k_eps - g0) * dz / (hp + k_eps), g0 * 1.5f);
                float runDx[k_ladderCap];
                bool  runPlain[k_ladderCap];
                int   runN = 0, k = 0;
                const float step = rising ? k_riseRatio : k_ratio;
                const float dx0  = g0 * 1.0005f + 1e-7f;
                for (float dx = dx0; dx <= hi && k < k_ladderCap; dx = fmaxf(dx * step, dx + ulp), ++k)
                {
                    UserCmd_t candidate = press((dx + shiftComp) / ipt, duck);
                    EndT e = sim(candidate);
                    bool plainUsed = false;
                    if (wallstrafeEnabled && rising && e.ok && !e.bounce && SimsLeft() > 0)
                    {
                        const UserCmd_t plain = pressFrom(vOld, ducked0, (dx + shiftComp) / ipt, duck, vel.z, false);
                        if (plain.forwardmove != candidate.forwardmove || plain.sidemove != candidate.sidemove)
                            { e = sim(plain); plainUsed = true; }
                    }
                    if (!e.ok && SimsLeft() <= 0) break;
                    if (e.ok && (e.ride || e.bounce)) { runDx[runN] = dx; runPlain[runN++] = plainUsed; }
                    else if (runN) break;
                }
                if (!runN) return false;
                dxHit = runDx[runN / 2];
                out = pressFrom(vOld, ducked0, dxHit / ipt, duck, vel.z, !runPlain[runN / 2]);
                return true;
            };
            UserCmd_t hit{};
            bool got = ladder(stance, hit, true);
            if (!got && !rising && allowed(!stance)) { got = ladder(!stance, hit, true); flipped = got; }

            // lookahead rollout (botox texturebug.cpp:848-931): does a rung that pins actually
            // SURVIVE a few more ticks, vs. just holding this tick's stance? Pred_SnapSave/Load
            // == botox's g_prediction.snapshot_save/load(0)
            if (got && !rising && ipt > 0.01f && pCmd->command_number != s_pinCmd + 1)
            {
                constexpr int kLook = 10, kLookCap = 100;
                constexpr float kLookRatio = 2.f;
                const auto lookOut = [&]() { return TbBudgetExpired(); };
                const auto pinnedNow = [&](float vzStart) {
                    const Vec3 v = EntV3(pLocal, g_offVelocity);
                    return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK
                        && IsTargetVz(v.z, 1e-3f) && v.Length2D() >= 1.f && vzStart + pin < 0.f;
                };
                Pred_SnapReserve(1);
                const auto rollout = [&](UserCmd_t first, int st) -> int {
                    const int s0 = sims;
                    if (lookOut()) return -1;
                    RestoreToFrame(frame);
                    Pred_Simulate(pLocal, &first);
                    ++sims;
                    int r = pinnedNow(vel.z) ? 1 : 0, last = 0;
                    for (int i = 0; i < kLook && sims - s0 < kLookCap; ++i)
                    {
                        if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK) break;
                        if (lookOut() || !Pred_SnapSave(pLocal, 0)) return -1;
                        const Vec3 o = EntV3(pLocal, g_offOrigin), v = EntV3(pLocal, g_offVelocity);
                        const bool dk = EntDucking(pLocal);
                        const float gk = fmaxf(gap + (o.x - org.x) * n.x + (o.y - org.y) * n.y + (o.z - org.z) * n.z, 1e-7f);
                        const float vo = -(v.x * n.x + v.y * n.y);
                        const float dzk = fabsf((v.z + pin) * ipt) + 1e-4f;
                        bool pinned = false;
                        for (int s = 0; s < 2 && !pinned && gk <= k_touch; ++s)
                        {
                            const int d = s ? !st : st;
                            if (s && !allowed(d)) break;
                            const float hi = fmaxf((k_eps - gk) * dzk / (((d || dk) ? 45.f : 62.f) + k_eps), gk * 1.5f);
                            for (float dx = gk * 1.0005f + 1e-7f; dx <= hi && sims - s0 < kLookCap; dx = fmaxf(dx * kLookRatio, dx + ulp))
                            {
                                if (lookOut()) return -1;
                                Pred_SnapLoad(pLocal, 0);
                                UserCmd_t step = pressFrom(vo, dk, (dx + shiftComp) / ipt, d, v.z, true);
                                Pred_Simulate(pLocal, &step);
                                ++sims;
                                if (pinnedNow(v.z)) { pinned = true; st = d; break; }
                            }
                        }
                        if (pinned) { ++r; if (last) return r + (kLook - 1 - i); last = 1; continue; }
                        if (lookOut()) return -1;
                        Pred_SnapLoad(pLocal, 0);
                        UserCmd_t holdCmd = withDuck(in, st);
                        Pred_Simulate(pLocal, &holdCmd);
                        ++sims;
                        last = 0;
                    }
                    return r;
                };
                const int lookA = rollout(hit, (hit.buttons & IN_DUCK) ? 1 : 0);
                const int lookB = lookA >= 0 ? rollout(withDuck(in, held), held) : -1;
                const int lk = lookA < 0 || lookB < 0 ? 2 : lookB > lookA ? 1 : 0;
                if (lk == 1)
                {
                    if (++s_miss > 1) { if (s_miss > 4) s_rideDuck = -1; s_run = 0; }
                    const UserCmd_t skip = withDuck(in, held);
                    commit(skip);
                    s_tbOwnsCmd = skip.buttons != in.buttons && s_miss <= 1;
                    act = 'L';
                    { TbFinish(); return; }
                }
            }
            if (got)
            {
                commit(hit);
                s_miss = 0;
                if (rising)
                {
                    // botox: a rising rung is a bounce, never kept for the fall (m_hb_hit in
                    // botox; folded into s_textureBugHit here, see file header)
                    s_textureBugHit = true;
                    act = 'B';
                    { TbFinish(); return; }
                }
                s_rideDuck = (hit.buttons & IN_DUCK) ? 1 : 0;
                s_rideUser = userDuck;
                ++s_run; s_rode = true; s_pinCmd = pCmd->command_number;
                s_textureBugHit = true;
                act = flipped ? 'F' : 'P';
                { TbFinish(); return; }
            }
        }

        if (++s_miss > 1) { if (gap > k_touch || s_miss > 4) s_rideDuck = -1; s_run = 0; }
        if (SimsLeft() <= 0) haveStrafe = false;
        if (haveStrafe && gap > k_touch) { sendStrafe(); { TbFinish(); return; } }

        // ---- 7. PARK (botox texturebug.cpp:967-1001) ----
        if ((reach > 0.f || gap <= k_touch) && gap > 0.f && s_parkMargin <= 16)
        {
            const float floorGap = fmaxf((float)s_parkMargin, 1.f) * ulp;
            float lastEnd = -1.f;
            for (const float keep : k_keep)
            {
                const float end = fmaxf(gap * keep, floorGap);
                if (end >= gap) break;
                if (end == lastEnd) continue;
                lastEnd = end;
                const UserCmd_t c = press((gap - end) / ipt, stance);
                const EndT e = sim(c);
                if (!e.ok || e.ground || (wallstrafeEnabled && rising && !tbw::RetainsSpeed(vel.Length2D(), e.vel.Length2D())))
                    { pkWhy = 'o'; continue; }
                if (wallstrafeEnabled && s_rode && !rising && !e.ride
                    && !TB_HeadWallReachable(pLocal, e.org, e.org - n * (reach + k_touch), mins, maxs, n))
                    { headAssistBlocked = true; pkWhy = 'h'; continue; }
                const float g1 = e.org.x * n.x + e.org.y * n.y + e.org.z * n.z - (org.x * n.x + org.y * n.y + org.z * n.z) + gap;
                const bool touched = -(e.vel.x * n.x + e.vel.y * n.y) <= 1e-3f;
                pkG1 = g1;
                pkWhy = touched ? 't' : 'g';
                if (g1 > 0.f && g1 < gap && !touched && g1 >= floorGap * 0.5f)
                {
                    commit(c);
                    s_tbOwnsCmd = true;
                    s_parkCmd = pCmd->command_number; s_parkFrom = gap; s_parkTo = g1;
                    act = 'K';
                    { TbFinish(); return; }
                }
            }
        }

        // ---- 7b. HOLD THE PARK (CSS only, not botox) ----
        // CSS ClipVelocity nudges every clip 1/32 u/s off the face = +16 ulp a tick. Botox W / U / your own
        // move at a touching gap hard-hit and nudge the hull out of the 1-ulp pin window (log 11-08:
        // K 16 ulp -> 1 ulp -> W -> 16 ulp, 1300 ticks, every other tick unpinnable). Already parked (no
        // tighter park): kill the normal speed and keep the gap, no contact, so the crossing tick finds 1 ulp
        if (gap > 0.f && gap <= 2.f * fmaxf((float)s_parkMargin, 1.f) * ulp && !rising && SimsLeft() > 0)
        {
            const UserCmd_t c = press(0.f, stance);
            const EndT e = sim(c);
            const float g1 = gap + (e.org.x - org.x) * n.x + (e.org.y - org.y) * n.y + (e.org.z - org.z) * n.z;
            if (e.ok && !e.ground && g1 > 0.f && g1 <= gap + 2.f * ulp && e.vel.Length2D() >= 0.2f * xy0)
            {
                commit(c);
                s_tbOwnsCmd = true;
                act = 'H';
                { TbFinish(); return; }
            }
        }

        // ---- 8. UNSTICK (botox texturebug.cpp:1002-1017) ----
        if (userDies && gap <= k_touch)
        {
            for (const float to : { k_eps, 0.125f, 0.5f })
            {
                const UserCmd_t c = press(-(to - gap) / ipt, stance);
                const EndT e = sim(c);
                if (e.ok && e.vel.Length2D() >= 0.2f * vel.Length2D()
                    && (!wallstrafeEnabled || !rising || tbw::RetainsSpeed(vel.Length2D(), e.vel.Length2D())))
                {
                    commit(c);
                    s_tbOwnsCmd = true;
                    act = 'U';
                    { TbFinish(); return; }
                }
            }
        }

        if (haveStrafe && SimsLeft() > 0) { sendStrafe(); { TbFinish(); return; } }

        // ---- 9. DRIVE-IN FALLBACK (botox texturebug.cpp:1022-1028) ----
        {
            const bool fell = s_rode && s_miss > 1;
            const UserCmd_t mine = withDuck(headAssistBlocked ? raw : in,
                                            held);
            commit(mine);
            s_tbOwnsCmd = mine.buttons != in.buttons && s_miss <= 1;
            act = fell ? 'F' : 'N';
            TbFinish();
            return;
        }
    }
}

// ============================================================================
// HEAD SURF -- coldtires head_surf(), ported whole
// ----------------------------------------------------------------------------
// The mirror of the wall pin: a flat brush BOTTOM above the head clips a rise and the engine
// holds real vz at +halfgravity while the surf lasts. No own toggle -- the TB bind drives it,
// it runs right after TextureBug and may TAKE OVER a TB-owned tick (a ceiling ride outranks a
// wall pin), which is the "stuck -> headsurf" transition.
// The +halfgrav accept is CORRECT, do not "fix" it: head_surf accepts +half, head_bounce
// lands on -half. Opposite on purpose
// ============================================================================
static void HeadSurf(void* pLocal, UserCmd_t* pCmd)
{
    static bool  s_hs_locked = false;
    static float s_hs_cached_forward = -1.f;
    static float s_hs_cached_angle_offset = 0.f;
    static bool  s_hs_cached_duck = false;
    static float s_hs_locked_forward = 0.f, s_hs_locked_side = 0.f;
    static bool  s_hs_locked_duck = false;
    static bool  s_hs_surfing_now = false;
    static bool  s_hs_prev_surf_vel = false;
    static int   s_hs_cold_skip = 0;
    static int   s_hs_surf_ticks = 0;     // consecutive hit ticks -- the coast gate
    static int   s_hs_coast_left = 0;

    s_hs_surf_ticks = s_headSurfHit ? s_hs_surf_ticks + 1 : 0;
    s_headSurfHit = false;

    if (!On(g_textureBug, g_textureBugKey, g_textureBugMode) || !pCmd || !EntAlive(pLocal))
        { s_hs_locked = false; s_hs_cached_forward = -1.f; s_hs_coast_left = 0; return; }

    // TB caught a wall surf this tick. Do NOT hard-return: if a ceiling is in reach and the pin
    // window is open, HS takes over. A miss restores TB's move untouched
    const bool tb_owned = s_textureBugHit;
    const bool debug    = g_debugLogEnable;
    const float half_grav = TB_HalfGravityPerTick();

    if (s_prePredFlags & FL_ONGROUND)
        { s_hs_cached_forward = -1.f; s_hs_surfing_now = false; s_hs_locked = false; return; }

    const int move_type = EntMoveType(pLocal);
    if (move_type == MOVETYPE_NOCLIP
        || move_type == MOVETYPE_FLY || move_type == MOVETYPE_OBSERVER)
        return;

    // read cmd START (what every try_config sim restores to), not the predicted END
    RestoreToFrame(PredCommandsPredicted() - 1);
    const Vec3 start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    // cold reach scales with rise speed: a fast rise crosses the catch window in 1-2 ticks
    const float vz_now = EntV3(pLocal, g_offVelocity).z;
    const float reach  = s_hs_surfing_now
        ? 12.f : fminf(8.f + fmaxf(vz_now, 0.f) * TickInterval() * 3.f, 30.f);

    GameTrace_t ceil_trace{};
    // 119th -- the same ramp rule as TextureBug, ceiling side. The base gate nz < -0.7 also
    // admits the UNDERSIDE of a 45 deg surf ramp, and grabbing one locks the cmd and eats the
    // strafe like the wall pin did. A head-surf brush bottom is flat (nz -1.00)
    const bool ceiling_ok = TB_DetectCeiling(pLocal, start_pos, mins, maxs, reach, ceil_trace)
                         && ceil_trace.plane.normal.z <= -(1.f - k_hsRampNz);
    if (debug && DiagOk(s_hsDiagTick, ceiling_ok))
        DebugLog("HS: ceil=%d nz=%.3f frac=%.3f reach=%.1f vz=%.1f gate=%d tbown=%d surft=%d "
                 "lock=%d coast=%d cold=%d",
                 (int)ceiling_ok, ceil_trace.plane.normal.z, ceil_trace.fraction, reach, vz_now,
                 (int)(vz_now > 3.f * half_grav), (int)tb_owned, s_hs_surf_ticks,
                 (int)s_hs_locked, s_hs_coast_left, s_hs_cold_skip);

    if (!ceiling_ok)
    {
        if (tb_owned) { s_hs_locked = false; return; }   // wall surf stands, nothing overhead
        // ceiling seam: an established surf coasts on the locked move -- scan gaps between
        // flush brushes dropped real surfs
        if (s_hs_surf_ticks >= 2 && s_hs_coast_left > 0)
        {
            --s_hs_coast_left;
            pCmd->forwardmove = s_hs_locked_forward;
            pCmd->sidemove    = s_hs_locked_side;
            if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            s_headSurfHit = true;
            return;
        }
        s_hs_cached_forward = -1.f;
        s_hs_surfing_now = false;
        s_hs_locked = false;
        s_hs_cold_skip = 0;
        return;
    }

    {
        const float vz = EntV3(pLocal, g_offVelocity).z;
        const bool surf_vel = TB_IsPositiveHalfGravity(vz, half_grav, 1.5f);
        s_hs_surfing_now   = surf_vel && s_hs_prev_surf_vel;
        s_hs_prev_surf_vel = surf_vel;
    }

    // a pin needs upward motion into the ceiling (or an active surf): vz below 3x halfgrav is
    // falling away and cannot pin, so skip the sweep budget on those ticks
    if (!s_hs_surfing_now && vz_now <= 3.f * half_grav)
    {
        if (!tb_owned) { s_hs_cached_forward = -1.f; s_hs_locked = false; }
        return;
    }

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    const int   original_buttons = pCmd->buttons;

    // locked: the engine pins real vz to +halfgravity while the surf holds -- replay verbatim
    if (s_hs_locked)
    {
        if (TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 3.f))
        {
            pCmd->viewangles  = original_view;
            pCmd->forwardmove = s_hs_locked_forward;
            pCmd->sidemove    = s_hs_locked_side;
            if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            s_headSurfHit   = true;
            s_textureBugHit = false;      // HS owns the tick
            s_hs_surfing_now = true;
            s_hs_coast_left  = 3;
            return;
        }
        s_hs_locked = false;    // slipped -- re-catch this tick
        s_hs_cold_skip = 0;     // never let a stale cooldown block the re-catch sweep
    }

    Vec3 ceil_angle = original_view;
    {
        const Vec3 n_xy(ceil_trace.plane.normal.x, ceil_trace.plane.normal.y, 0.f);
        if (n_xy.Length2D() > 0.05f)
        {
            Vec3 into_c = n_xy * -1.f;
            ceil_angle = VectorAngles(into_c);
            ClampAnglesMv(ceil_angle);
        }
    }

    const int nBasePredicted = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBasePredicted - 1); };

    // shared pool slice: a full hunt when free, a capped probe when TB already owns the tick
    int budget = SimsLeft();
    if (budget > 160) budget = 160;
    if (tb_owned && budget > 60) budget = 60;
    if (budget < 0) budget = 0;
    int sims_used = 0;

    const auto try_config = [&](float forward, float angle_offset, bool duck) -> bool
    {
        if (sims_used >= budget) return false;
        ++sims_used;
        RestoreOriginal();
        Vec3 wish_angle = original_view;
        wish_angle.y = ceil_angle.y + angle_offset;
        pCmd->viewangles = original_view;
        pCmd->buttons    = original_buttons;
        if (duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        pCmd->forwardmove = forward;
        pCmd->sidemove    = 0.f;
        CorrectMovement(pCmd, wish_angle, original_view);
        Pred_Simulate(pLocal, pCmd);
        return TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f);
    };

    const auto commit = [&](float cfg_forward, float cfg_offset, bool duck)
    {
        RestoreOriginal();     // pCmd already holds the winning silent move
        pCmd->viewangles = original_view;
        s_hs_cached_forward      = cfg_forward;
        s_hs_cached_angle_offset = cfg_offset;
        s_hs_cached_duck         = duck;
        s_hs_locked         = true;
        s_hs_locked_forward = pCmd->forwardmove;
        s_hs_locked_side    = pCmd->sidemove;
        s_hs_locked_duck    = duck;
        s_headSurfHit    = true;
        s_textureBugHit  = false;    // takeover: HS owns the tick now
        s_hs_surfing_now = true;
        s_hs_cold_skip   = 0;
        s_hs_coast_left  = 3;
        if (debug)
            DebugLog("HS LOCK: fwd=%.2f ang=%.1f duck=%d sims=%d/%d vz=%.1f",
                     cfg_forward, cfg_offset, (int)duck, sims_used, budget,
                     EntV3(pLocal, g_offVelocity).z);
    };

    // does the pin sustain N more sim ticks? (pCmd still holds the tested move after try_config)
    const auto sustain_ticks = [&](int n_max) -> int
    {
        int n = 0;
        for (; n < n_max && sims_used < budget; ++n)
        {
            ++sims_used;
            Pred_Simulate(pLocal, pCmd);
            if (!TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f)) break;
        }
        return n;
    };

    // natural pass: the user's own cmd already headsurf-pins -> keep it (manual play wins).
    // INITIAL catch only. On a TB-owned tick pCmd holds TB's wall move, not the user's
    if (!tb_owned && s_hs_surf_ticks == 0
        && (fabsf(original_forward) > 0.01f || fabsf(original_side) > 0.01f) && budget > 0)
    {
        RestoreOriginal();
        UserCmd_t natural = *pCmd;
        ++sims_used;
        Pred_Simulate(pLocal, &natural);
        const bool nat_pin = TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f);
        RestoreOriginal();
        if (nat_pin)
        {
            pCmd->viewangles  = original_view;
            pCmd->forwardmove = original_forward;
            pCmd->sidemove    = original_side;
            pCmd->buttons     = original_buttons;
            s_headSurfHit = true;
            s_hs_surfing_now = true;
            s_hs_locked = false;      // the user owns it -- no verbatim replay of user input
            s_hs_coast_left = 3;
            if (debug) DebugLog("HS: natural pin vz=%.1f", EntV3(pLocal, g_offVelocity).z);
            return;
        }
    }

    // fast path: last tick's winning config (1 sim)
    if (s_hs_cached_forward >= 0.f
        && try_config(s_hs_cached_forward, s_hs_cached_angle_offset, s_hs_cached_duck))
        { commit(s_hs_cached_forward, s_hs_cached_angle_offset, s_hs_cached_duck); return; }

    // cold skip only while falling -- rising = the catch window is 1-2 ticks out, and skipping
    // ate catches
    if (s_hs_cold_skip > 0 && vz_now <= 0.f)
    {
        --s_hs_cold_skip;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
        return;
    }

    const Vec3  vel_hs = EntV3(pLocal, g_offVelocity);
    const float vel_yaw_hs = RAD2DEGF(atan2f(vel_hs.y, vel_hs.x));
    const float rel_yaw_hs = NormalizeAngle(vel_yaw_hs - ceil_angle.y);
    const float hs_sign    = (rel_yaw_hs >= 0.f) ? -1.f : 1.f;

    static constexpr float k_hs_fwd[] =
        { 0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.125f, 2.875f, 5.f, 10.f, 20.f, 30.f };
    constexpr float coarse_step = 5.f, fine_step = 1.f;

    float sh_fwd = -1.f, sh_ang = 0.f;
    bool  sh_duck = false;
    int   sh_sustain = -1;
    float sh_move_fwd = 0.f, sh_move_side = 0.f;   // saved silent move -- commit must not need budget

    // mid-surf re-catch tries the CURRENT duck state first (first pin wins)
    const bool first_duck = (s_hs_surf_ticks > 0 || s_hs_locked) ? s_hs_locked_duck : false;
    for (const float fwd : k_hs_fwd)
    {
        if (sims_used >= budget) break;
        for (int i_duck = 0; i_duck <= 1 && sims_used < budget; ++i_duck)
        {
            const bool duck = (i_duck == 0) ? first_duck : !first_duck;
            for (float ang = 0.f; ang < 90.f && sims_used < budget; ang += coarse_step)
            {
                if (!try_config(fwd, ang * hs_sign, duck)) continue;

                // coarse hit -- save its silent move (try_config is deterministic from the
                // pinned restore frame, so no re-sim is needed after a failed refinement)
                const float coarse_fwd  = pCmd->forwardmove;
                const float coarse_side = pCmd->sidemove;

                // durability: the pin must survive 2 more ticks -- 1-tick flukes poison the lock
                // and the fast path (the same lesson as TB); the shallow best stays as a fallback
                const int sus = sustain_ticks(2);
                if (sus < 2)
                {
                    if (sus > sh_sustain)
                    {
                        sh_sustain = sus; sh_fwd = fwd; sh_ang = ang * hs_sign; sh_duck = duck;
                        sh_move_fwd = coarse_fwd; sh_move_side = coarse_side;
                    }
                    continue;
                }

                for (float d = fine_step; d <= coarse_step; d += fine_step)      // refine around it
                    for (int s = 0; s <= 1; ++s)
                    {
                        const float fa = ang + (s ? d : -d);
                        if (fa < 0.f) continue;
                        if (try_config(fwd, fa * hs_sign, duck))
                            { commit(fwd, fa * hs_sign, duck); return; }
                    }

                // refinement found nothing better -- replay the saved coarse move
                pCmd->forwardmove = coarse_fwd;
                pCmd->sidemove    = coarse_side;
                pCmd->buttons     = original_buttons;
                if (duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                commit(fwd, ang * hs_sign, duck);
                return;
            }
        }
    }

    // no durable config -> the best shallow pin still beats nothing. NOT on a TB-owned tick:
    // a takeover clears the wall hit, so a 1-tick HS fluke would trade an established wall surf
    // for nothing. Stealing TB's tick demands a DURABLE pin
    if (sh_sustain >= 0 && !tb_owned)
    {
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = sh_move_fwd;
        pCmd->sidemove    = sh_move_side;
        pCmd->buttons     = original_buttons;
        if (sh_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        commit(sh_fwd, sh_ang, sh_duck);
        return;
    }

    // established surf, momentary miss -> coast on the locked move (band drift re-pins)
    if (!tb_owned && s_hs_surf_ticks >= 2 && s_hs_coast_left > 0)
    {
        --s_hs_coast_left;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = s_hs_locked_forward;
        pCmd->sidemove    = s_hs_locked_side;
        pCmd->buttons     = original_buttons;
        if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        s_headSurfHit = true;
        return;
    }

    // full miss: hand movement back (on a TB-owned tick the originals ARE TB's committed move,
    // so the wall surf survives the failed takeover untouched), then cool down
    RestoreOriginal();
    pCmd->viewangles  = original_view;
    pCmd->forwardmove = original_forward;
    pCmd->sidemove    = original_side;
    pCmd->buttons     = original_buttons;
    s_hs_cached_forward = -1.f;
    s_hs_cold_skip = 1;     // the catch window is 1-2 ticks; a long cooldown ate headsurfs
    if (debug)
        DebugLog("HS MISS: sus=%d sims=%d/%d vz=%.1f nz=%.2f", sh_sustain, sims_used, budget,
                 vz_now, ceil_trace.plane.normal.z);
}

// ============================================================================
// HEAD BOUNCE -- coldtires head_bounce(), ported whole (2026-08-17 / 123rd)
// ----------------------------------------------------------------------------
// HeadSurf RIDES flat brush ceilings. A notch between two DISPLACEMENTS fails all its gates
// -- both faces are tilted, and one tilted face slides the rise off instead of pinning it.
// A CREASE is different: TryPlayerMove's two-plane branch projects velocity onto the crease
// line, a horizontal crease zeroes vz, and FinishGravity puts back one half step, landing on
// -halfgravity.
// So this is a CATCH only: no plane lock, no coast, no duck latch, no hold, every tick
// re-derived from scratch. A tick that verifies nothing hands the cmd back byte-identical.
// 123rd -- the accept is the CUT off free fall (any size), not the full crease, and the
// chain is COUNTED to the notch instead of fixed at 3
// ============================================================================
static void HeadBounce(void* pLocal, UserCmd_t* pCmd)
{
    s_headBounceHit = false;

    if (!On(g_textureBug, g_textureBugKey, g_textureBugMode)) return;
    if (!pCmd || !EntAlive(pLocal)) return;
    // a wall pin or a ceiling ride already owns the tick -- both outrank a one-tick clip
    if (s_textureBugHit || s_headSurfHit) return;
    // botox head_bounce: gate + start_pos + vz off backup_data (cmd START), never the predicted END
    if (s_prePredFlags & FL_ONGROUND) return;
    if (EntMoveType(pLocal) != MOVETYPE_WALK) return;

    const float half = TargetPredictZVel();   // NEGATIVE
    const float vz_now = s_prePredVel.z;
    // must be RISING into it -- a falling hull cannot have its rise cut. The one entry speed
    // where gravity alone fakes the bounce is filtered per chain tick instead (step_result)
    if (vz_now <= 0.f) return;

    RestoreToFrame(PredCommandsPredicted() - 1);   // START hull (duck state) for the overhead probe
    const Vec3  start_pos = s_prePredOrigin;
    const Vec3  mins = EntV3(pLocal, g_offMins);
    const Vec3  maxs = EntV3(pLocal, g_offMaxs);
    const float dt   = TickInterval();
    const bool  debug = g_debugLogEnable;

    // only as far as the chain could rise, plus a nub margin -- anything higher is out of
    // this search's reach and probing for it only costs traces
    const float reach = fminf(4.f + vz_now * dt * k_hbChainMax, 48.f);
    GameTrace_t over{};
    TbOverDbg_t odbg{};
    if (!TB_DetectOverhead(pLocal, start_pos, mins, maxs, reach, over, odbg))
    {
        if (debug && DiagOk(s_hbDiagTick, false))
            DebugLog("HB: none reach=%.1f vz=%.1f frac0=%.3f nz0=%.2f ss=%d retry=%d",
                     reach, vz_now, odbg.frac0, odbg.nz0, (int)odbg.ss0, (int)odbg.retried);
        return;
    }

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    const int   original_buttons = pCmd->buttons;

    const int nBase = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBase - 1); };

    int budget = SimsLeft();
    if (budget > k_hbSimCap) budget = k_hbSimCap;
    if (budget <= 0) return;
    int sims_used = 0;

    // the tick's own numbers, read off the restore base -- every variant starts here
    RestoreOriginal();
    const Vec3  base_vel = EntV3(pLocal, g_offVelocity);
    const float base_vz  = base_vel.z;
    const float xy_base  = base_vel.Length2D();
    // a bounce that costs half the speed is not a catch, it is a crash into the ceiling
    const float xy_floor = xy_base >= 1.f ? xy_base * k_tbXyCreepFrac : 0.f;
    const float vz_free  = base_vz + 2.f * half;
    const float sv_airaccel = CvarFloat("sv_airaccelerate", 10.f);

    // THE COST GATE. A wish cannot change vz, so whether the head reaches the notch is
    // decided before any sim: the hull rises (vz + half) * dt. DUCK cannot help either -- in
    // air FinishDuck moves the origin up by the hull delta and FinishUnDuck back down, so the
    // hull TOP is invariant and only the feet move. 123rd: the tick count is COUNTED, not
    // assumed (vz_i = base_vz + 2*half*(i-1), exact). Peaking below the notch returns 0
    const float gap = over.fraction * reach;
    const auto hb_ticks_to_gap = [&](float need) -> int
    {
        float climbed = 0.f, vz_i = base_vz;
        for (int i = 1; i <= k_hbChainMax; ++i)
        {
            const float step = (vz_i + half) * dt;
            if (step <= 0.f) return 0;
            climbed += step;
            if (climbed >= need) return i;
            vz_i += 2.f * half;
        }
        return 0;
    };
    const int need_ticks = hb_ticks_to_gap(gap - k_hbGapMargin);
    if (need_ticks <= 0)
    {
        if (debug && DiagOk(s_hbDiagTick, false))
            DebugLog("HB: far gap=%.2f nz=%.2f vz=%.1f reach=%.1f", gap,
                     over.plane.normal.z, base_vz, reach);
        return;
    }
    // one tick past the arrival tick: the contact happens ON the tick the head reaches it, and
    // the tick after is where a graze that only slowed the rise still shows up as a cut
    int chain = need_ticks + 1;
    if (chain < k_hbChain)    chain = k_hbChain;
    if (chain > k_hbChainMax) chain = k_hbChainMax;

    // diag: the BEST any variant did, not the last one tried. cut=0.00 on a miss = nothing
    // touched the notch at all; a cut over the floor with xy under it = a real bounce vetoed
    float best_dz = 1e9f, best_vz = 0.f, best_xy = 0.f, best_cut = 0.f;

    // one simulated tick, judged against the tick it started from. THE TEST IS THE CUT, not
    // the landing value: a wish cannot change vz, so a contact-free tick lands on
    // vz_in + 2*half exactly and anything under that was taken by something overhead
    const auto step_result = [&](float vz_in) -> int
    {
        const Vec3  v = EntV3(pLocal, g_offVelocity);
        const float free_vz = vz_in + 2.f * half;
        const float cut = free_vz - v.z;
        const float dz  = fabsf(v.z - half);
        if (cut > best_cut) { best_cut = cut; best_vz = v.z; best_xy = v.Length2D(); }
        if (dz < best_dz) best_dz = dz;
        // landing eats the rise the same way a ceiling does -- that is a floor, not a bounce
        if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return -1;
        if (cut > k_hbCutMin) return v.Length2D() >= xy_floor ? 1 : -1;
        // still climbing -> the chain may still reach the notch; falling -> it never will
        return v.z > 0.f ? 0 : -1;
    };

    const auto log_result = [&](const char* how)
    {
        if (!debug) return;
        DebugLog("HB %s: nz=%.2f gap=%.2f vz=%.1f->%.1f free=%.1f tgt=%.2f cut=%.2f dz=%.2f "
                 "xy=%.0f->%.0f/%.0f chain=%d/%d sims=%d/%d retry=%d",
                 how, over.plane.normal.z, gap, base_vz, best_vz, vz_free, half, best_cut,
                 best_dz, xy_base, best_xy, xy_floor, need_ticks, chain, sims_used, budget,
                 (int)odbg.retried);
    };

    // NATURAL FIRST -- the player's own cmd already clips the notch, so keep their input and
    // just publish the catch. The same rule HeadSurf uses: manual play wins
    {
        RestoreOriginal();
        UserCmd_t natural = *pCmd;
        bool nat = false;
        for (int i = 0; i < chain && sims_used < budget; ++i)
        {
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Pred_Simulate(pLocal, &natural);
            const int r = step_result(vz_in);
            if (r != 0) { nat = (r > 0); break; }
        }
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
        if (nat) { s_headBounceHit = true; log_result("natural"); return; }
    }

    // wish directions relative to TRAVEL, not to the ceiling normal: on a crease the useful nudge
    // is sideways off the path, and a near-flat underside has no usable xy normal at all
    const float base_yaw = (xy_base > 1.f) ? RAD2DEGF(atan2f(base_vel.y, base_vel.x))
                                           : original_view.y;
    static constexpr float k_hb_yaw[] =
        { 0.f, 15.f, -15.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 150.f, -150.f, 180.f };
    // 124th -- AirAccelerate caps only ADDSPEED at 30; accelspeed = accel * wishspeed * dt
    // uses the UNCAPPED wishspeed. So fwd is the whole authority, not a fine-tuning axis:
    // fwd 1 -> 0.19 u/s a tick, fwd 450 -> the entire addspeed in ONE tick. Descending, since
    // ascending order spent every sim on the 0.19 rows
    static constexpr float k_hb_fwd[] = { 450.f, 60.f, 15.f };
    // NO duck variant: an air duck moves the origin and the hull by the same delta, so the
    // head top does not move and the notch stays as far away (see the cost gate). The duck is
    // the edgebug/pixel lever -- it moves the FEET

    TbDedup_t dedup{};
    const auto try_variant = [&](float fwd, float yaw_off) -> bool
    {
        if (sims_used >= budget) return false;
        const float world_yaw = NormalizeAngle(base_yaw + yaw_off);
        const Vec3  delta = TB_AirAccelDelta(base_vel, world_yaw, fwd, sv_airaccel, dt);
        // 124th -- a provable NO-OP used to cost a whole chain: addspeed = 30 - vel.wishdir,
        // so at 198 u/s every yaw inside ~81 deg of travel returns a zero delta
        if (delta.Length2D() < k_tbDupVelEps) return false;
        if (dedup.SeenOrAdd(delta)) return false;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->buttons     = original_buttons;
        pCmd->forwardmove = fwd;
        pCmd->sidemove    = 0.f;
        Vec3 wish = original_view; wish.y = world_yaw;
        CorrectMovement(pCmd, wish, original_view);
        pCmd->viewangles = original_view;
        // hold the SAME cmd for the chain: the view is fixed across the sims, so replaying it
        // is the same world wish every tick (same as HeadSurf's sustain_ticks)
        for (int i = 0; i < chain && sims_used < budget; ++i)
        {
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Pred_Simulate(pLocal, pCmd);
            const int r = step_result(vz_in);
            if (r != 0) return r > 0;
        }
        return false;
    };

    for (const float fwd : k_hb_fwd)
        for (const float yaw_off : k_hb_yaw)
        {
            if (sims_used >= budget) break;
            if (!try_variant(fwd, yaw_off)) continue;
            // pCmd already holds the winning silent move -- restore the entity, commit as is
            RestoreOriginal();
            pCmd->viewangles = original_view;
            s_headBounceHit  = true;
            log_result("sweep");
            return;
        }

    // miss: the cmd goes back exactly as the player wrote it
    RestoreOriginal();
    pCmd->viewangles  = original_view;
    pCmd->forwardmove = original_forward;
    pCmd->sidemove    = original_side;
    pCmd->buttons     = original_buttons;
    log_result("miss");
}
