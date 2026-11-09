// wallclimb.inl -- botox-tf2 wallclimb.inl (HEAD 031ef24, the sweep remake) ported to CSS. Replaces the 11-03 csgo pin
// port: CSS grounds on displacement facets exactly like TF2 (same base gamemovement CategorizePosition + dispcoll), and
// the pin port's 11-07 log read land=0 at ~120 ms of sims a second. CSS differences, each from css-community source:
//   rise cut 140 (NON_JUMP_VELOCITY, gamemovement.cpp:3810), not TF2's 250 -- wallclimb_pure.h k_rise_vz
//   the hop keeps the duck bit: CSS CheckJumpButton has no FL_DUCKING refusal (TF2's tf_gamemovement.cpp:1221 had one)
//   air duck lift 8.5 (cs_gamemovement.cpp:826), jump 302 -- comments only, nothing here models them
//   ladders exist: MOVETYPE_WALK gate below already hands every ladder tick to fireman / ladder.inl
//   pins: CSS ClipVelocity leaves +1/32 along the normal, so a wall hull hangs on pixel surf pins (vz -h +-1/32) a lot.
// No yield to the bare ride bit (s_psRidingNow): a pinned START first tries ref's pixel hop land search
// (ref , wallengine.inl WePixelHopAir) climb-only, then the sweep below as usual
// 11-08 restored over the 11-07 CSS-src rewrite (log: search=22 win=0, 13 cut) and the ref wall stuck catch
// (WEWS wc catches=5 holds=0 at ~5k sims/s, never a climb).
// Textually included by movement.cpp right after airstuck.inl (same TU: Pred_Simulate / RestoreToFrame / SimsLeft /
// MV_DetectWallLines / MV_SupportDistance / TB_SteeringAwayFromWall / AsClock / s_prePred* / s_raw* are statics above).
//
// A wall climb is ground -> hop -> sweep -> ground, repeated. Airborne beside a displacement wall this sweeps silent
// into-wall and along-wall moves (yaw +-87, both sides), each HELD up to k_depth ticks (iterative deepening off snapshot
// slots), and commits tick 1 of the first that goes FL_ONGROUND on that face; next tick re-finds it one shallower. The win
// arms a hop that the real grounded tick stamps (CheckJumpButton reads the ground state a cmd STARTS on, so the press
// cannot go on the cmd that grounds). Three proofs stand between the scan and any write, all engine facts:
//   FACE    a displacement first (trace.dispFlags: facets, crests, quadrant catches). CSS 11-09: else the nearest BRUSH
//           face (botox csgo wall_climb catches brush lips too, its hug alone is disp-only; the TF2 disp-only gate left
//           wc dead on every dust2 wall, log disp=0 bn=-1,0,0). A brush win = flush ledge / lip / wall top above the
//           last ground only (LandOk face_disp false), and the sweep runs only while that is reachable (BrushReach)
//   NEAR    the hull face is within k_engage_support of it (one tick of fwd=450 closes ~0.45u, 16u out no variant reaches)
//   GROUND  a held variant goes FL_ONGROUND sooner than the player's own keys would, <= k_land_support from THAT face, or
//           <= k_engage_support on a disp ground higher than the last ground (crest / quadrant catch; LandOk)
// No win = no write, the player keeps his own cmd. Hands off the keys = hands off the cmd.
//
// Kept from the csgo port, the parts that were real upgrades:
//   wall-clock budget (AsClock k_time_share) on top of SimsLeft()      crouch catches option (g_wcDuck = try the other stance)
//   prevent slow (g_wcPreventSlow: no win that halves xy > 200)        visualize (WC_GetDrawData -> render.cpp RenderWallClimbOverlay)
//   rise threshold from CategorizePosition (CSS 140)                  TB's non-catch creep no longer starves wc on a shared key:
//   the sweep starts from the keys TB was handed (s_wcPreTb*), TB's cmd stays unless wc wins
//   steer to seam (g_wallClimbSeamSteer, "delta strafe scale" g_wallClimbSteerScale): the csgo steer job, scoring every along target
// Dropped: parks / holds / exit pin / spot scan / jump fix -- the csgo pin model, TF2 grounds instead. Snapshot slots
// k_snap_base.. are back inside one call (the lookahead's resume); only the steer job's 3 slots (k_steer_slot..) live
// across ticks, from the first air tick to the steer tick.
#include <mutex>
#include "wallclimb_pure.h"

static wc::Hop s_wcHop;
static int     s_wcWallTick  = -100000; /* last tick a disp face was engaged (the unlatch window) */
static float   s_wcScanStart = 0.f;
static float   s_wcGroundZ   = -1e9f;   /* z of the last ground tick: viz "+h", LandOk's rise */
/* the committed depth-d win: replayed while the re-search misses, so tick 1 of a plan is never sent alone */
static struct { Vec3 wish; float fwd = 0.f, yo = 0.f; int buttons = 0; int left = 0; } s_wcPlan;
static int s_wcGroundCmd = 0;   /* command_number of the last ground START (steer job: first air tick = +1) */
/* steer to seam job (botox m_steer): START of the first air tick in slot k_steer_slot, every along target rolled over the
   first k_steer_delay ticks, the best one steered from then on */
static struct WcSteer {
    bool  valid = false, done = false, rolling = false, decided = false, steer = false, haveAt = false;
    int   startCmd = 0, cand = -1, count = 0, tick = 0, left = 0, steerLeft = 0, sims = 0;
    float order[wc::k_steer_n]{};
    float base = -1.f, best = -1.f, target = 0.f, cur = 0.f;   /* scores: -1 = not rolled */
    float accel = 0.f;            /* airaccel * ipt (x the live friction where it is used) */
    float intoYaw = 0.f, faceZ = 0.f;
    Vec3  t{}, at{};              /* along-wall unit; base roll's START at tick k_steer_delay */
    GameTrace_t face{};
    Vec3  view{}; float fwd = 0.f, side = 0.f; int buttons = 0;   /* your keys at the job's START */
    bool  disp = true;            /* the job's face kind: LandOk's brush rule */
} s_wcSteer;

/* per-second counters, debug log */
static int s_wcN[29]{}; /* ticks scan disp near sweep win hop strip wall rej d1 d2 d3 d4 plan zmiss dup cut | steer: job pick tick drift done | pin: try land | brush: face low win | pre */
static int s_wcDep[wc::k_depth]{};   /* wins per depth */
static Vec3 s_wcBrushN{};  /* last brush face the disp gate skipped (log bn=) */
static int s_wcSims = 0; static long long s_wcUs = 0ll, s_wcUsWorst = 0ll;
static unsigned long long s_wcLogNext = 0ull;

/* last wall tick, written in create move, copied out for the render thread */
static std::mutex s_wcVizLock;
static WcDrawData s_wcViz, s_wcVizOut;

void WC_GetDrawData(WcDrawData& out)
{
    std::lock_guard<std::mutex> l(s_wcVizLock);
    out = s_wcVizOut;
}

// user-facing controls -- declared extern in movement.h (botox m_wall_climb_seam_steer / _steer_scale / m_show_wall_slope)
bool  g_wallClimbSeamSteer  = false;
float g_wallClimbSteerScale = 1.0f;
bool  g_showWallSlope     = false; int g_showWallSlopeKey = 0; int g_showWallSlopeMode = 1;

static bool s_wcSlopeDown = false;
// ebanat 0x1054b3: on press, slope of the surface you look at. movement_add_window -> DebugLog
static void ShowWallSlope(void* pLocal)
{
    const bool down  = On(g_showWallSlope, g_showWallSlopeKey, g_showWallSlopeMode);
    const bool press = down && !s_wcSlopeDown;
    s_wcSlopeDown     = down;
    if (!press || !EntAlive(pLocal))
        return;
    Vec3 view{};
    if (!EngineGetViewAngles(view))
        return;
    const float p = DEG2RADF(view.x), y = DEG2RADF(view.y);
    const Vec3 eye = EntV3(pLocal, g_offOrigin) + (g_offViewOffset > 0 ? EntV3(pLocal, g_offViewOffset) : Vec3(0.f, 0.f, 64.f));
    const Vec3 dir(cosf(p) * cosf(y), cosf(p) * sinf(y), -sinf(p));
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceLine(eye, eye + dir * 500.f, MASK_PLAYERSOLID, &filter, &tr);
    if (tr.fraction >= 1.f || tr.startsolid)
        return;
    DebugLog("WC SLOPE: %.7f deg", RAD2DEGF(acosf((std::min)(1.f, fabsf(tr.plane.normal.z)))));
}

static void WallClimb(void* pLocal, UserCmd_t* pCmd)
{
    using namespace wc;
    ShowWallSlope(pLocal);   // botox: unconditional, independent of wall_climb's own on/off
    s_wallClimbHit = s_wallClimbActive = s_wcLanded = false;
    s_wcViz.valid = false;
    struct Publish { ~Publish() { std::lock_guard<std::mutex> l(s_wcVizLock); s_wcVizOut = s_wcViz; } } publish_viz;

    if (const unsigned long long now = GetTickCount64(); g_debugLogEnable && now >= s_wcLogNext) {
        s_wcLogNext = now + 1000ull;
        if (s_wcN[0])
            DebugLog("WC: ticks=%d scan=%d wall=%d disp=%d near=%d sweep=%d win=%d hop=%d strip=%d rej=%d dep=%d/%d/%d/%d/%d/%d plan=%d zmiss=%d pre=%d dup=%d cut=%d steer=%d/%d/%d/%d/%d pin=%d/%d brush=%d/%d low=%d sims=%d us=%lld worst=%lld bn=%.2f,%.2f,%.2f",
                     s_wcN[0], s_wcN[1], s_wcN[8], s_wcN[2], s_wcN[3], s_wcN[4], s_wcN[5], s_wcN[6], s_wcN[7], s_wcN[9],
                     s_wcDep[0], s_wcDep[1], s_wcDep[2], s_wcDep[3], s_wcDep[4], s_wcDep[5], s_wcN[14], s_wcN[15], s_wcN[28], s_wcN[16], s_wcN[17],
                     s_wcN[18], s_wcN[22], s_wcN[19], s_wcN[20], s_wcN[21], s_wcN[23], s_wcN[24], s_wcN[25], s_wcN[27], s_wcN[26],
                     s_wcSims, s_wcUs, s_wcUsWorst, s_wcBrushN.x, s_wcBrushN.y, s_wcBrushN.z);
        for (int& c : s_wcN) c = 0;
        for (int& c : s_wcDep) c = 0;
        s_wcSims = 0; s_wcUs = s_wcUsWorst = 0ll;
    }

    // the entity holds this cmd's END here; START is the pre-pred capture. Ground z tracked key or not: a hold key pressed
    // mid-air must compare against the ground just left, not one from before the last release
    const bool bGroundPre  = (s_prePredFlags & FL_ONGROUND) != 0;
    if (bGroundPre) s_wcGroundZ = s_prePredOrigin.z;

    const bool key = On(g_wallClimb, g_wallClimbKey, g_wallClimbMode);
    s_wcSyncWant   = key && EntAlive(pLocal);
    if (!pCmd || !key || !EntAlive(pLocal) || EntMoveType(pLocal) != MOVETYPE_WALK) {
        s_wcHop.Reset();
        s_wcPlan.left = 0;
        s_wcScanStart = 0.f;
        s_wcSteer.valid = false;
        return;
    }

    const int iTickNow = g_globalVars->tickcount;
    if (s_wcWallTick > iTickNow) s_wcWallTick = -100000;   // map change rewound the clock
    const bool bGroundPost = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    if (bGroundPre) {
        s_wcPlan.left   = 0;
        s_wcSteer.valid = false;
        s_wcGroundCmd   = pCmd->command_number;
    }

    // armed-hop clock, before every yield: a hop armed in the air must survive the ticks TB/AS own. Fires only on the
    // ground the win simulated (a lost plan lands on the floor)
    const bool hop_ground = s_wcHop.Clock(bGroundPre);
    const bool armed_hop  = hop_ground && s_wcHop.ZOk(s_prePredOrigin.z);
    if (hop_ground && !armed_hop) ++s_wcN[15];
    // the win's ground tick arrived with a pressable jump already in the cmd: LJ / edge jump / pixel hop run first and
    // hop it (11-09 log: 24 of 31 chains = "LJ: takeoff edge", hop=0) -- log pre=, the climb went on all the same
    if (armed_hop && (pCmd->buttons & IN_JUMP) && !s_prevCmdJump) ++s_wcN[28];

    // UNLATCH. FinishMove copies this cmd's buttons into m_nOldButtons and a grounded jump is refused while IN_JUMP is set
    // there -- only a jumpless cmd clears it. Held space would block the very seam jump the climb exists to make
    const bool engaged = s_wcHop.pending > 0 || (iTickNow - s_wcWallTick) <= k_engage_ticks;
    const bool other_wants_jump = s_jumpbugHit || On(g_jumpbug, g_jumpbugKey, g_jumpbugMode) || s_psAssistHopActive;
    if (Unlatch(bGroundPre, bGroundPost, s_rawJumpHeld, engaged, other_wants_jump)) {
        pCmd->buttons &= ~IN_JUMP;
        ++s_wcN[7];
    }

    // PLAN LOST: last tick's win had ticks to go, and this cmd neither sent a wc land nor grounds. at= the gate that
    // returned (11-09 log: 7 of 31 chains died mid-plan with nothing logged)
    const int   plan_in  = bGroundPre ? 0 : s_wcPlan.left;
    const char* plan_why = "miss";
    struct PlanLost {
        const int& in; const char* const& why; const bool& post;
        ~PlanLost() { if (in > 0 && !s_wcLanded && !post) DebugLog("WC PLAN LOST: left=%d at=%s", in, why); }
    } plan_lost{ plan_in, plan_why, bGroundPost };

    // yields. TB / HS / HB catches, AS pins (AS outranks WC, coldtires order), a running tb ride, EB's cmd, every PS owner:
    // the assist arc flies beside the very wall this sweeps and a win would overwrite its catch duck / steer. Pixel hop
    // ran first this cmd (ref order). A bare pin (s_psRidingNow) is NOT a yield in CSS: see the pin search below
    if (s_textureBugHit || s_headSurfHit || s_headBounceHit || s_airStuckHit || s_asHolding || s_ebDetected
        || (On(g_textureBug, g_textureBugKey, g_textureBugMode) && s_tbRideHold > 0)
        || s_psAssistHopActive || s_shouldPixelSurf || s_psAssistDuckHeld || s_pixelHopHit) {
        plan_why = s_textureBugHit ? "tb" : s_headSurfHit ? "hs" : s_headBounceHit ? "hb" : s_airStuckHit || s_asHolding ? "as"
                 : s_ebDetected ? "eb" : s_pixelHopHit ? "ph" : (s_psAssistHopActive || s_shouldPixelSurf || s_psAssistDuckHeld) ? "ps" : "tbride";
        return;
    }

    // ---- THE LAUNCH ---- wc never jumps on its own: s_rawJumpHeld is the real space bar before AutoJump rewrites the cmd
    if (bGroundPre) {
        switch (s_wcHop.Launch(armed_hop, s_rawJumpHeld, s_prevCmdJump)) {
        case hop_release: pCmd->buttons &= ~IN_JUMP; return;
        case hop_press:
            pCmd->buttons |= IN_JUMP;    // duck bit kept: CSS CheckJumpButton has no FL_DUCKING refusal (cs_gamemovement.cpp:634-680)
            s_wallClimbHit = true;
            ++s_wcN[6];
            return;
        default: return;
        }
    }
    s_wcHop.jumpedLast = false;
    ++s_wcN[0];

    // airborne at the cmd's start, grounded at its end = the player's own move already lands them
    if (bGroundPost) return;

    // provable skip: CategorizePosition nulls the ground entity when vz > 140 at the check (before FinishGravity = next
    // START vz - h, gamemovement.cpp:3812). Rising
    // ticks still run the steer job: it plans the whole jump from the first one
    const Vec3 velocity = EntV3(pLocal, g_offVelocity);
    const bool rising   = velocity.z + TargetPredictZVel() > k_rise_vz;
    const int  cmdNum   = pCmd->command_number;
    if (!g_wallClimbSeamSteer) s_wcSteer.valid = false;
    const bool steer_first = g_wallClimbSeamSteer && rising && cmdNum - s_wcGroundCmd == 1;
    const bool steer_slice = s_wcSteer.valid && !s_wcSteer.done && cmdNum < s_wcSteer.startCmd + k_steer_delay;
    plan_why = "rising";
    if (rising && !steer_first && !steer_slice) return;

    // the keys the sweep starts from: TB's creep / probe (no catch) is a band setup on a shared key -- it fired every air wall
    // tick and wc never got the cmd. Sweep from what TB was handed; TB's cmd goes out unless a variant wins
    const bool tb_creep = s_tbOwnsCmd && !s_textureBugHit;
    UserCmd_t base = *pCmd;
    if (tb_creep) {
        base.forwardmove = s_wcPreTbFwd;
        base.sidemove    = s_wcPreTbSide;
        base.buttons     = s_wcPreTbButtons;
    }
    // A CLIMB IS SOMETHING THE PLAYER DOES. No keys = no write, and no 120-230 trace scan either
    plan_why = "keys";
    if (fabsf(base.forwardmove) < k_min_input && fabsf(base.sidemove) < k_min_input) return;
    plan_why = "rising";

    const Vec3 start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);

    GameTrace_t primary{};
    float found_angle = 0.f;
    bool  face_disp   = false;
    // GATE 1: displacement first. The scan hands back the FIRST face in angle order, so a brush face there (the trim
    // under a disp cliff, the far side of a corner) used to end the tick with a disp face sitting in the next direction:
    // 11-04 log read disp=0 on every second. Step past brush faces, one full turn at most (wallclimb_pure.h NextScan).
    // No disp anywhere = the first brush face (CSS 11-09, header FACE). Scan cursor stays on disps only, as before
    const auto scan_disp = [&]() {
        ++s_wcN[1];
        bool any_wall = false;
        GameTrace_t brush{};
        face_disp = false;
        for (float a = s_wcScanStart; ScanLeft(s_wcScanStart, a); a = NextScan(found_angle)) {
            if (!MV_DetectWallLines(pLocal, start_pos, mins, maxs, a, primary, found_angle, k_scan_nz, k_scan_reach))
                break;
            if (primary.dispFlags & DISPSURF_SURFACE) { face_disp = true; break; }
            if (!any_wall) brush = primary;
            any_wall = true;
            s_wcBrushN = primary.plane.normal;
        }
        if (face_disp) {
            ++s_wcN[8];
            ++s_wcN[2];
            s_wcScanStart = WrapScan(found_angle);
            return true;
        }
        s_wcScanStart = 0.f;   // next scan fresh, do not pin on a brush
        if (!any_wall) return false;
        ++s_wcN[8];
        ++s_wcN[25];
        primary = brush;
        return true;
    };

    AsClock budget;
    bool clocked = false;   /* started on the first sim ask: the scan stays off the sweep's clock, as it always was */
    int sims = 0;
    bool cut = false;
    const int  nBase   = PredCommandsPredicted();
    const auto restore = [&]() { RestoreToFrame(nBase - 1); };
    const auto can_sim = [&]() {
        if (!clocked) { budget.Start(k_time_share, TickInterval()); clocked = true; }
        if (SimsLeft() > 0 && !(sims > 0 && budget.Expired())) return true;
        cut = true;
        return false;
    };
    const auto sim     = [&](UserCmd_t c) { restore(); Pred_Simulate(pLocal, &c); ++sims; };
    const auto grounded = [&]() { return (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0; };
    struct Done { const decltype(restore)& r; ~Done() { r(); } } done_restore{ restore };
    // per-second stats on every way out (the sweep's early returns used to skip them)
    const auto account = [&]() {
        if (cut) ++s_wcN[17];
        s_wcSims += sims;
        if (!clocked) return;
        const long long us = budget.UsedUs();
        s_wcUs += us;
        s_wcUsWorst = (std::max)(s_wcUsWorst, us);
    };
    struct Account { const decltype(account)& a; ~Account() { a(); } } account_stats{ account };

    // ---- STEER TO SEAM (botox m_wall_climb_seam_steer, lab P76) ----
    // which seam the hull comes back down on is set early in the jump: one air tick of along wish moves the along speed
    // anywhere in +-30, k_steer_delay ticks of it ~15u along the wall by the fall. The job replays the jump from the first
    // air START with your keys, from tick k_steer_delay a steer onto each along target, and on every falling tick beside
    // the face the sweep's straight-in catches (k_forward). EVERY target is scored, highest catch wins (botox stopped at
    // the first that beat your path); the steer goes out only for a strictly higher catch than your own path's.
    // ponytail: the roll probes 2 of the sweep's 33 variants and LandOk's z test only, a catch the full sweep would find
    // elsewhere is scored 0; widen the probe set if the log shows steer picks that never land
    WcSteer& J = s_wcSteer;
    const auto along     = [&](const Vec3& v) { return v.x * J.t.x + v.y * J.t.y; };
    const auto job_keys  = [&]() {
        UserCmd_t c   = *pCmd;   // this cmd's number / tick; the keys are the job START's
        c.viewangles  = J.view;
        c.forwardmove = J.fwd;
        c.sidemove    = J.side;
        c.buttons     = J.buttons;
        return c;
    };
    const auto aim_wish = [&](UserCmd_t c, const float yaw, const float fwd) {
        c.forwardmove = fwd;
        c.sidemove    = 0.f;
        Vec3 wish     = c.viewangles;
        wish.y        = yaw;
        CorrectMovement(&c, wish, c.viewangles);   // silent: the view is not touched
        return c;
    };
    // friction off the state the entity holds (the roll's, or this cmd's): CategorizePosition sets 0.25 rising beside a
    // steep face, 1 otherwise, and AirAccelerate scales by it
    const auto steer_wish = [&](const UserCmd_t& c, const Vec3& v, const float target) {
        const float need = target - along(v), sg = need < 0.f ? -1.f : 1.f;
        return aim_wish(c, RAD2DEGF(atan2f(J.t.y * sg, J.t.x * sg)), SteerWish(need, target, J.accel * AsSurfaceFriction(pLocal)));
    };
    const auto face_support = [&](const Vec3& o) {
        GameTrace_t f = J.face;
        f.endpos.z += o.z - J.faceZ;
        return MV_SupportDistance(pLocal, f, &o);
    };
    // the entity just grounded: a catch = on THAT face (gate 3 without the disp traces: flush, or a crest above the last ground)
    const auto roll_caught = [&](const Vec3& o) { return LandOk(face_support(o), true, o.z, s_wcGroundZ, false, J.disp); };
    const auto steer_job = [&]() {
        const auto finish = [&](const float score) {
            J.rolling = false;
            if (J.cand < 0) J.base = score;
            else if (score > J.best) { J.best = score; J.target = J.order[J.cand]; }   // ties keep the nearer target
            if (++J.cand >= J.count) J.done = true;
        };
        int used = 0;
        bool fresh = false;   /* first step of a roll: START (your path) or the fork (a target) */
        while (!J.done && used < k_steer_tick_sims && can_sim()) {
            if (!J.rolling) {
                // every target flies your keys up to tick k_steer_delay: it starts from your path's state there (the fork)
                if (J.cand >= 0 && !J.haveAt) { J.done = true; break; }   // your path ended first: nothing to steer
                J.rolling = fresh = true;
                J.tick    = J.cand >= 0 ? k_steer_delay : 0;
                J.left    = J.cand >= 0 ? k_steer_max : 0;
            }
            const int slot = fresh ? (J.cand >= 0 ? k_steer_slot + 2 : k_steer_slot) : k_steer_slot + 1;
            fresh = false;
            if (!Pred_SnapLoad(pLocal, slot)) { J.done = true; break; }   // edge skip's Pred_SnapReset dropped the pool
            const Vec3 o0 = EntV3(pLocal, g_offOrigin), v0 = EntV3(pLocal, g_offVelocity);
            if (J.cand < 0 && J.tick == k_steer_delay) {
                // the fork: the targets are deltas on the along speed you fly HERE (your strafe changed it since tick 0)
                J.at     = o0;
                J.haveAt = Pred_SnapSave(pLocal, k_steer_slot + 2);
                J.cur    = along(v0);
                J.count  = J.haveAt ? SteerOrder(J.cur, g_wallClimbSteerScale, J.order) : 0;
            }
            const float target = J.cand >= 0 ? J.order[J.cand] : 0.f;
            UserCmd_t c = job_keys();
            if (J.left > 0 && J.tick >= k_steer_delay && fabsf(along(v0) - target) > k_steer_done) {
                --J.left;
                c = steer_wish(c, v0, target);
            } else {
                if (J.tick >= k_steer_delay) J.left = 0;
                if (v0.z <= k_rise_vz && face_support(o0) <= k_engage_support) {
                    bool caught = false;
                    for (const float f : k_forward) {
                        UserCmd_t p = aim_wish(job_keys(), J.intoYaw, f);
                        Pred_Simulate(pLocal, &p); ++sims; ++used;
                        const Vec3 po = EntV3(pLocal, g_offOrigin);
                        const bool hit = grounded() && roll_caught(po);
                        if (!Pred_SnapLoad(pLocal, slot)) { J.done = true; break; }
                        if (hit) { finish(SteerScore(true, po.z, s_wcGroundZ)); caught = true; break; }
                    }
                    if (caught || J.done) continue;
                }
            }
            Pred_Simulate(pLocal, &c); ++sims; ++used;
            const Vec3 o = EntV3(pLocal, g_offOrigin);
            // over: grounded (a catch, or a landing anywhere else = 0), off the face (the sweep's judge calls it dead
            // too, and it stops a roll flying 60 ticks of probes for nothing), under the last ground, out of ticks
            if (grounded()) finish(SteerScore(roll_caught(o), o.z, s_wcGroundZ));
            else if (face_support(o) > k_keep_support || o.z < s_wcGroundZ - k_steer_fall
                     || ++J.tick >= k_steer_roll_ticks || !Pred_SnapSave(pLocal, k_steer_slot + 1))
                finish(0.f);
        }
        J.sims += used;
    };

    // the job starts on the first air tick, by a disp face, START saved (botox steer_init)
    if (steer_first && !tb_creep && scan_disp() && MV_SupportDistance(pLocal, primary, &start_pos) <= k_engage_support
        && !TB_SteeringAwayFromWall(&base, primary.plane.normal, base.forwardmove, base.sidemove)) {
        Pred_SnapReserve(k_steer_slot + 3);   // START, roll, fork
        restore();
        if (Pred_SnapSave(pLocal, k_steer_slot)) {
            J          = {};
            J.valid    = true;
            J.startCmd = cmdNum;
            J.face     = primary;
            J.disp     = face_disp;
            J.faceZ    = start_pos.z;
            J.t        = Vec3(primary.plane.normal.y, -primary.plane.normal.x, 0.f);
            const float tl = J.t.Length2D();
            J.t        = tl > 1e-4f ? J.t * (1.f / tl) : Vec3(1.f, 0.f, 0.f);
            J.intoYaw  = RAD2DEGF(atan2f(-primary.plane.normal.y, -primary.plane.normal.x));
            J.view     = base.viewangles;
            J.fwd      = base.forwardmove;
            J.side     = base.sidemove;
            J.buttons  = base.buttons & ~IN_JUMP;   // the unlatch strips a held jump beside the face: no air press in the roll
            J.accel    = CvarFloat("sv_airaccelerate", 10.f) * TickInterval();   // x the friction of the state steered
            ++s_wcN[18];
        }
    }
    if (J.valid && !J.done && cmdNum < J.startCmd + k_steer_delay) steer_job();

    // decide once, on the steer tick: only if the real START is where the job's own path put it (your keys held)
    if (J.valid && !J.decided && cmdNum >= J.startCmd + k_steer_delay) {
        J.decided = true;
        const float drift = J.haveAt ? (s_prePredOrigin - J.at).Length() : 1e9f;
        J.steer     = SteerPick(J.base, J.best) && cmdNum == J.startCmd + k_steer_delay && drift <= k_steer_drift;
        J.steerLeft = k_steer_max;
        s_wcN[19] += J.steer;
        s_wcN[21] += SteerPick(J.base, J.best) && !J.steer;
        s_wcN[22] += J.done;
        DebugLog("WC STEER: pick=%d base=%.0f best=%.0f delta=%+.1f along=%.2f cand=%d/%d done=%d sims=%d drift=%.3f",
                 J.steer, J.base, J.best, J.best > 0.f ? J.target - J.cur : 0.f, J.cur, J.cand, J.count, J.done, J.sims,
                 J.haveAt ? drift : -1.f);
    }

    if (rising) return;

    // CSS PIN. START hung on a pin (the +1/32 clip nudge parks a wall hull at vz -h +-1/32 for ticks): ref pixel hop's
    // grounding search, any face kind, climb-only (above the last ground by k_climb_rise). Its win is one tick, this cmd
    // grounds; the miss falls through to the disp sweep. Shared pool (pooled), the sweep's clock never started here
    if (const float h = -TargetPredictZVel(); fabsf(s_prePredVel.z + h) <= we::PinTol(h) && SimsLeft() > 0) {
        ++s_wcN[23];
        s_wcWallTick = iTickNow;   // beside a face: the unlatch keeps space off the landing cmd
        restore();
        UserCmd_t c = base;
        if (!other_wants_jump && s_prevCmdJump) c.buttons &= ~IN_JUMP;   // the hop must be pressable on the ground tick
        float pinLandZ = 0.f;
        if (WePixelHopAir(pLocal, &c, [&](float z) { pinLandZ = z; return z > s_wcGroundZ + k_climb_rise; }, sims, true)) {
            pCmd->forwardmove = c.forwardmove;
            pCmd->sidemove    = c.sidemove;
            pCmd->buttons     = c.buttons;
            pCmd->viewangles  = c.viewangles;
            s_tbOwnsCmd       = false;
            s_wcHop.Arm(pinLandZ);
            s_wcPlan.left     = 0;
            s_wallClimbHit = s_wallClimbActive = s_wcLanded = true;
            ++s_wcN[24];
            AT_Stamp(*pCmd, "wc");
            DebugLog("WC LAND: pin z=%.3f up=%.2f fwd=%.0f side=%.0f duck=%d vz=%.2f sims=%d", pinLandZ, pinLandZ - s_wcGroundZ,
                     c.forwardmove, c.sidemove, (c.buttons & IN_DUCK) != 0, s_prePredVel.z, sims);
            return;
        }
    }

    plan_why = "scan";
    if (!scan_disp()) return;
    // brush: a win must land above the last ground + 2. Falling past that = no win exists, no sims (dust2 = every wall)
    plan_why = "low";
    if (!face_disp && !BrushReach(start_pos.z, velocity.z, s_wcGroundZ, CvarFloat("sv_gravity", 800.f))) { ++s_wcN[26]; return; }
    // GATE 2: near it, live, before any sim
    const float live_support = MV_SupportDistance(pLocal, primary, &start_pos);
    plan_why = "far";
    if (live_support > k_engage_support) return;
    plan_why = "away";
    ++s_wcN[3];
    // disp only: pixel surf lives on brush walls, so a brush tick claims nothing until a win (s_wallClimbHit, and
    // PS yields to that). The unlatch window too: a held space bar near a brush wall stays the player's
    if (face_disp) {
        s_wcWallTick      = iTickNow;
        s_wallClimbActive = true;   // an air wall tick: pixel surf yields
    }

    // seamless leave: steering off the wall = the player keeps the cmd (and drops the steer, botox you_leave)
    if (TB_SteeringAwayFromWall(&base, primary.plane.normal, base.forwardmove, base.sidemove)) {
        s_wcPlan.left = 0;
        J.valid = false;
        return;
    }

    // the steer goes out (botox steer_air): wc's write, the sweep below still overrides it with a catch. Never over TB's creep
    bool steered = false;
    if (J.valid && J.steer && J.steerLeft > 0 && !tb_creep) {
        if (fabsf(along(s_prePredVel) - J.target) <= k_steer_done) J.steer = false;
        else {
            --J.steerLeft;
            const UserCmd_t sc = steer_wish(*pCmd, s_prePredVel, J.target);
            pCmd->forwardmove = sc.forwardmove;
            pCmd->sidemove    = sc.sidemove;
            s_wallClimbHit    = steered = true;
            ++s_wcN[20];
        }
    }

    const Vec3 approach_normal = primary.plane.normal * -1.f;
    Vec3 angle_wall = VectorAngles(approach_normal);
    angle_wall.y    = NormalizeAngle(angle_wall.y);
    const Vec3  original_view = pCmd->viewangles;
    const float velocity_yaw  = RAD2DEGF(atan2f(velocity.y, velocity.x));
    const float sign          = SweepSign(NormalizeAngle(velocity_yaw - angle_wall.y));
    // the player's own wish yaw (sidemove + = right: wish = fwd*forward + side*right, right = yaw - 90). The variants are
    // tried nearest to the live plan's wish first, else nearest to this (OrderYaws)
    const float own_yaw   = original_view.y + RAD2DEGF(atan2f(-base.sidemove, base.forwardmove));
    const bool  plan_live = s_wcPlan.left > 0;
    int yaw_order[k_yaws];
    OrderYaws(angle_wall.y, plan_live ? s_wcPlan.wish.y : own_yaw, sign, yaw_order);

    const float start_xy = velocity.Length2D();
    UserCmd_t best{};
    bool found = false;
    float land_support = 0.f;
    int found_depth = 0, gb = k_depth + 1;
    float land_z = 0.f, best_yo = 0.f, cur_yo = 0.f;   /* cur_yo: the yaw offset judge() is looking at */
    Vec3 best_wish{};
    bool from_plan = false;

    // a held cmd just simulated one more tick (its d-th): 0 = still airborne beside the face, 1 = dead (left the face, or
    // landed somewhere that is not a catch), 2 = a catch
    const auto judge = [&](const UserCmd_t& cmd, const Vec3& wish, const int d) -> int {
        // support's ray runs at the contact's z; a held variant has fallen since, so the contact moves with the hull
        const Vec3 org = EntV3(pLocal, g_offOrigin);
        GameTrace_t face = primary;
        face.endpos.z += org.z - start_pos.z;
        const float support = MV_SupportDistance(pLocal, face);
        if (!grounded()) return support <= k_keep_support ? 0 : 1;
        // GATE 3: grounded ON THAT FACE. On a disp map FL_ONGROUND is very often the floor you were landing on anyway
        bool ground_disp = false, center_floor = false;
        if (face_disp && support > k_land_support && support <= k_engage_support) {   // brush: flush or nothing
            TraceFilterWorldProps filter; filter.Setup(pLocal);
            GameTrace_t tr{};
            TraceHull(org, org - Vec3(0, 0, 2.f), EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs), MASK_PLAYERSOLID, &filter, &tr);
            ground_disp = tr.fraction < 1.f && (tr.dispFlags & DISPSURF_SURFACE);
            if (ground_disp) {
                GameTrace_t cl{};
                TraceLine(org + Vec3(0, 0, 1.f), org - Vec3(0, 0, 4.f), MASK_PLAYERSOLID, &filter, &cl);
                center_floor = cl.fraction < 1.f && cl.plane.normal.z >= 0.7f;
            }
        }
        if (!LandOk(support, ground_disp, org.z, s_wcGroundZ, center_floor, face_disp)
            || (Slowed(start_xy, EntV3(pLocal, g_offVelocity).Length2D()) && g_wcPreventSlow)) {
            ++s_wcN[9];
            return 1;
        }
        found = true; best = cmd; best_wish = wish; land_support = support; found_depth = d; land_z = org.z; best_yo = cur_yo;
        return 2;
    };

    // THE PLAN FIRST. Last tick's win is a held cmd with `left` ticks to go: sim it straight through (<= k_depth sims) and,
    // if it still catches, that is the whole search -- the re-search found it one shallower anyway, at ~100 sims
    if (s_wcPlan.left > 0 && can_sim()) {
        UserCmd_t c = base;
        c.viewangles  = original_view;
        c.forwardmove = s_wcPlan.fwd;
        c.sidemove    = 0.f;
        c.buttons     = (base.buttons & ~(IN_DUCK | IN_JUMP)) | s_wcPlan.buttons;   // the plan owns stance + jump only
        CorrectMovement(&c, s_wcPlan.wish, original_view);
        cur_yo = s_wcPlan.yo;
        restore();
        for (int d = 1; d <= k_depth && can_sim(); ++d) {
            UserCmd_t t = c;
            Pred_Simulate(pLocal, &t); ++sims;
            if (judge(c, s_wcPlan.wish, d) != 0) break;
        }
        from_plan = found;
        if (found) ++s_wcN[14];
        else s_wcPlan.left = 0;   // the plan no longer catches from here: search fresh
    }

    // THE BASELINE decides whether the rest may run: the player's own move (TB's, if it is creeping), held up to k_depth
    // ticks. Grounding next tick = nothing to buy. Grounding at tick gb = a variant must beat it (land sooner), or a hop
    // armed off it would fire on whatever they happened to land on
    plan_why = "cut";
    if (!found) {
        if (!can_sim()) return;
        plan_why = "own";   // your keys ground next tick
        sim(*pCmd);
        if (grounded()) return;
        plan_why = "miss";
        UserCmd_t c = *pCmd;
        for (int d = 2; d <= k_depth && can_sim(); ++d) {
            Pred_Simulate(pLocal, &c); ++sims;
            if (grounded()) { gb = d; break; }
        }
        ++s_wcN[4];
    }

    // the variants. stance 0 is the player's own, so a catch the plain sweep makes is the first one tried; the other stance
    // only with crouch catches on (no crouch pops in demos otherwise). A HELD jump (last cmd had it) is stripped: the
    // airborne unlatch does the same beside a face, and a win carrying it would latch m_nOldButtons on the very tick it
    // grounds -- a crest catch is often ONE ground tick, the hop must be pressable on it. A fresh press stays (scout
    // double jump, parachute)
    const bool  duck_now = (base.buttons & IN_DUCK) != 0;
    static UserCmd_t s_var[k_max_vars];
    static Vec3      s_wish[k_max_vars];
    static bool      s_live[k_max_vars];
    static float     s_yo[k_max_vars];
    int nVar = 0;
    for (int stance = 0; stance < 2 && !found; ++stance) {
        if (stance == 1 && !g_wcDuck) break;
        const float* fwd_list = stance == 0 ? k_forward : k_duck_forward;
        const int    fwd_n    = stance == 0 ? k_forwards : k_duck_forwards;
        for (const int y : yaw_order)
            for (int f = 0; f < fwd_n; ++f) {
                const float offset = k_yaw[y];
                UserCmd_t fake  = base;
                fake.viewangles = original_view;
                if (stance == 1) fake.buttons = duck_now ? (fake.buttons & ~IN_DUCK) : (fake.buttons | IN_DUCK);
                if (!other_wants_jump && s_prevCmdJump) fake.buttons &= ~IN_JUMP;
                fake.forwardmove = fwd_list[f];
                fake.sidemove    = 0.f;
                Vec3 wish = original_view;
                wish.y    = angle_wall.y + offset * sign;
                CorrectMovement(&fake, wish, original_view);   // silent: the view is not touched
                s_var[nVar]  = fake;
                s_wish[nVar] = wish;
                s_yo[nVar]   = offset * sign;
                s_live[nVar++] = true;
            }
    }

    Pred_SnapReserve(k_snap_base + k_max_vars);
    // iterative deepening: every variant one tick, then every survivor a second tick from its snapshot, ... A held
    // variant dies when it grounds (win or not: it landed somewhere) or drifts off the face
    int alive = nVar;
    static Vec3 s_lo[k_max_vars], s_lv[k_max_vars];
    static int  s_lf[k_max_vars];
    for (int d = 1; d <= k_depth && d < gb && alive > 0 && !found; ++d) {
        int nl = 0;   // states kept this layer, for the duplicate drop
        for (int i = 0; i < nVar && !found; ++i) {
            if (!s_live[i]) continue;
            if (!can_sim()) { alive = 0; break; }
            const int slot = k_snap_base + i;
            if (d == 1) restore();
            else if (!Pred_SnapLoad(pLocal, slot)) { s_live[i] = false; --alive; continue; }
            UserCmd_t c = s_var[i];
            Pred_Simulate(pLocal, &c); ++sims;
            cur_yo = s_yo[i];
            if (judge(s_var[i], s_wish[i], d) == 0 && d < k_depth) {
                const Vec3 o = EntV3(pLocal, g_offOrigin), v = EntV3(pLocal, g_offVelocity);
                const int  fl = EntI(pLocal, g_offFlags) & FL_DUCKING;
                bool dup = false;
                for (int k = 0; k < nl && !dup; ++k)
                    dup = s_lf[k] == fl && (o - s_lo[k]).Length() < k_dup_org && (v - s_lv[k]).Length() < k_dup_vel;
                if (dup) ++s_wcN[16];
                else {
                    s_lo[nl] = o; s_lv[nl] = v; s_lf[nl++] = fl;
                    if (Pred_SnapSave(pLocal, slot)) continue;
                }
            }
            s_live[i] = false; --alive;
        }
    }

    if (found) {
        s_wcPlan.wish    = best_wish;
        s_wcPlan.fwd     = sqrtf(best.forwardmove * best.forwardmove + best.sidemove * best.sidemove);   // pre-CorrectMovement wish length
        s_wcPlan.buttons = best.buttons & (IN_DUCK | IN_JUMP);
        s_wcPlan.yo      = best_yo;
        s_wcPlan.left    = found_depth - 1;
    }

    if (found) {
        ++s_wcDep[found_depth - 1];
        pCmd->forwardmove = best.forwardmove;
        pCmd->sidemove    = best.sidemove;
        pCmd->buttons     = best.buttons;
        s_tbOwnsCmd       = false;   // wc's win is the cmd now
        s_wcHop.Arm(land_z);         // the ground tick this bought, stamped when it really arrives on that z
        AT_Stamp(*pCmd, "wc");      // last writer: AS_FinalStamp re-applies it after the aimbot
        s_wallClimbHit = s_wcLanded = true;
        ++s_wcN[5];
        if (!face_disp) ++s_wcN[27];
        DebugLog("WC LAND: d=%d gb=%d fwd=%.0f side=%.0f duck=%d yo=%.0f dev=%.0f live=%.2f land=%.2f up=%.2f n=%.3f,%.3f,%.3f xy=%.0f vz=%.1f sims=%d%s%s%s",
                 found_depth, gb, best.forwardmove, best.sidemove, (best.buttons & IN_DUCK) != 0,
                 best_yo, fabsf(WrapDeg(best_wish.y - own_yaw)), live_support, land_support, land_z - s_wcGroundZ,
                 primary.plane.normal.x, primary.plane.normal.y, primary.plane.normal.z, start_xy, velocity.z, sims,
                 tb_creep ? " tbdrop" : "", from_plan ? " plan" : "", face_disp ? "" : " brush");
    }

    // visualize: the face, then where the outgoing cmd takes you (green end = a land). Extra sims, off the clock
    if (g_wcViz) {
        WcDrawData& v = s_wcViz;
        v         = {};
        v.valid   = true;
        v.ms      = GetTickCount64();
        v.ground  = false;
        v.branch  = found ? "land" : steered ? "steer" : "scan";
        v.n       = primary.plane.normal;
        v.sims    = sims;
        v.half    = maxs.x;
        v.height  = maxs.z - mins.z;
        v.groundZ = s_wcGroundZ;
        v.origin  = start_pos;
        const Vec3 mid = primary.endpos + Vec3(0.f, 0.f, (mins.z + maxs.z) * 0.5f);
        const float off = mid.x * v.n.x + mid.y * v.n.y + mid.z * v.n.z - primary.plane.dist;
        v.wall = fabsf(off) <= 32.f ? mid - v.n * off : mid - v.n * (maxs.x * (fabsf(v.n.x) + fabsf(v.n.y)));
        restore();
        v.path[v.pathN++] = start_pos;
        for (int k = 0; k < k_viz_ticks; ++k) {
            UserCmd_t c = *pCmd;
            Pred_Simulate(pLocal, &c);
            v.path[v.pathN++] = EntV3(pLocal, g_offOrigin);
            if (grounded()) {
                v.lands = found;
                v.land  = v.path[v.pathN - 1];
                break;
            }
        }
    }
}
