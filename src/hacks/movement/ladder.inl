// ==== CS:GO ladder features ported to CSS -- csgo-sdk hacks/movement/movement.cpp ====
//
// Engine facts (quoted again per-feature below), all from css-community-master mp/src/game/
// shared/{gamemovement.cpp, cstrike/cs_gamemovement.cpp}:
//   LadderMove          gamemovement.cpp:2854-3014
//   FullLadderMove      gamemovement.cpp:2536-2554
//   CCSGameMovement::OnLadder / LadderDistance / LadderMask / ClimbSpeed / CheckForLadders
//                       cs_gamemovement.cpp:450-569
// Once attached (MOVETYPE_LADDER, :2898), the climb branch (no IN_JUMP, :2941-3010) reads
// BUTTONS (IN_FORWARD/IN_BACK/IN_MOVELEFT/IN_MOVERIGHT, :2922-2932), not forward/side floats --
// same as cstrike15/CS:GO (botox memory ":3302 reads BUTTONS"). ClimbSpeed is a flat
// MAX_CLIMB_SPEED=200 (shareddefs.h:103), halved while ducking (DuckSpeedMultiplier,
// cs_gamemovement.cpp:476-486) -- CS:GO's sv_ladder_scale_speed (200*0.78=156) does not exist
// in this source at all (grepped, no hit), so CSS climbs faster than CS:GO's default.
//
// Pipeline note: Movement_RunImpl's bMoveBlocked gate (movement.cpp ~2034) stops the whole
// TF2/CS:GO ground+air feature set the instant movetype != WALK, which includes LADDER --
// correct for every OTHER feature here (none of them know what a ladder is), but it means the
// climbing features below need their OWN call site that runs while bMoveBlocked is true. See
// LadderOnLadderTick's call site in Movement_RunImpl (movement.cpp edit, "ladder: runs even
// while bMoveBlocked").

// user-facing controls -- declared extern in movement.h, listed under MENU/CONFIG in the report
bool g_fastLadder = false;            int g_fastLadderKey = 0;            int g_fastLadderMode = 0;
bool g_ladderGlide = false;           int g_ladderGlideKey = 0;           int g_ladderGlideMode = 0;
bool g_ladderFreelookClimb = false;   int g_ladderFreelookClimbKey = 0;   int g_ladderFreelookClimbMode = 0;
bool g_antiLadder = false;            int g_antiLadderKey = 0;            int g_antiLadderMode = 0;
bool g_autoOneHopLadder = false;      int g_autoOneHopLadderKey = 0;      int g_autoOneHopLadderMode = 0;

static bool s_antiLadderHit = false, s_autoOneHopHit = false;

// ---- fast_ladder / ladder_glide -- csgo-sdk movement.cpp fast_ladder/ladder_glide, verbatim
// intent: snap the view toward the ladder face while strafing full-speed, so the climb branch's
// forwardSpeed/rightSpeed decompose (gamemovement.cpp:2941-2973) always gets a full climb/
// lateral push. Pure button/view manipulation -- no CS:GO-specific field involved, ports
// unchanged (ladder_glide additionally forces the move-left/right button to match the lean,
// the CS:GO source's own quirk, kept identically).
static void FastLadder(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_fastLadder, g_fastLadderKey, g_fastLadderMode)) return;
    if (EntMoveType(pLocal) != MOVETYPE_LADDER) return;

    if (pCmd->sidemove == 0.f) pCmd->viewangles.y += 45.f;
    const bool fwd = (pCmd->buttons & IN_FORWARD) != 0, back = (pCmd->buttons & IN_BACK) != 0;
    pCmd->viewangles.y += ladderfx::LadderViewNudge(pCmd->sidemove < 0.f, pCmd->sidemove > 0.f, fwd, back);
    if (fwd)  pCmd->forwardmove = 450.f;
    if (back) pCmd->forwardmove = -450.f;
    if (pCmd->sidemove > 0.f) pCmd->sidemove = 450.f;
    if (pCmd->sidemove < 0.f) pCmd->sidemove = -450.f;
}

static void LadderGlide(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_ladderGlide, g_ladderGlideKey, g_ladderGlideMode)) return;
    if (EntMoveType(pLocal) != MOVETYPE_LADDER) return;

    if (pCmd->sidemove == 0.f) pCmd->viewangles.y += 45.f;
    if (pCmd->buttons & IN_FORWARD)
    {
        if (pCmd->sidemove > 0.f) pCmd->viewangles.y -= 1.f;
        else if (pCmd->sidemove < 0.f) pCmd->viewangles.y += 90.f;
        pCmd->buttons &= ~IN_MOVELEFT; pCmd->buttons |= IN_MOVERIGHT;
    }
    if (pCmd->buttons & IN_BACK)
    {
        if (pCmd->sidemove < 0.f) pCmd->viewangles.y -= 1.f;
        else if (pCmd->sidemove > 0.f) pCmd->viewangles.y += 90.f;
        pCmd->buttons &= ~IN_MOVERIGHT; pCmd->buttons |= IN_MOVELEFT;
    }
}

// ---- ladder_freelook_climb -- csgo-sdk movement.cpp ladder_freelook_climb, reduced to the
// part that still has an engine hook in CSS: force full-speed climb buttons while attached, so
// looking anywhere (freelook) never costs climb speed. CS:GO's version also air-strafes toward
// a ladder before attaching and fakes the exit jump's view frame -- that half duplicates
// fireman's hunt/grab (same engine calls, same physics) and is not re-built here; this feature
// is the CLIMB-while-attached half only, same as its name says.
static void LadderFreelookClimb(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_ladderFreelookClimb, g_ladderFreelookClimbKey, g_ladderFreelookClimbMode)) return;
    if (EntMoveType(pLocal) != MOVETYPE_LADDER) return;

    pCmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    pCmd->buttons |= IN_FORWARD;   // full climb regardless of where the player is looking
    pCmd->forwardmove = 450.f;
    pCmd->sidemove = 0.f;
}

// ---- anti_ladder -- csgo-sdk movement.cpp anti_ladder. CS:GO brute-forces 8 directions
// through a full g_prediction.begin/end resim and keeps whichever one left movetype != ladder.
// CSS equivalent asks the SAME question geometrically instead of resimming: once attached,
// LadderDistance() is 10u (cs_gamemovement.cpp:54-58), and the top of every tick's PlayerMove
// re-runs LadderMove -- a direction whose 10u hull trace does NOT hit a ladder is a direction
// LadderMove will refuse next tick (":if (!LadderMove() && GetMoveType()==LADDER) SetMoveType(
// WALK)", gamemovement.cpp:4623-4631), which is exactly what the resim was answering. Avoids
// depending on the flagged-ambiguous ProcessMovement slot (see report, NEEDS FROM OTHERS).
static bool AntiLadderExitDir(void* pLocal, const Vec3& origin, const Vec3& mins, const Vec3& maxs,
                              float yawRad)
{
    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    const Vec3 dir(cosf(yawRad), sinf(yawRad), 0.f);
    const Vec3 end = origin + dir * 10.0f;   // LadderDistance() attached = 10u
    GameTrace_t tr{};
    TraceHull(origin, end, mins, maxs, k_ladderMaskMv, &fil, &tr);
    return tr.fraction >= 1.f || !(tr.contents & k_contentsLadderMv);
}

static void AntiLadder(void* pLocal, UserCmd_t* pCmd)
{
    s_antiLadderHit = false;
    if (!On(g_antiLadder, g_antiLadderKey, g_antiLadderMode)) { return; }
    if (g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0) return;

    static int s_ticksOnLadder = 0;
    if (EntMoveType(pLocal) != MOVETYPE_LADDER) { s_ticksOnLadder = 0; return; }
    if (++s_ticksOnLadder <= 10) return;   // csgo-sdk anti_ladder's own stuck threshold

    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float viewYawRad = pCmd->viewangles.y * (3.14159265f / 180.f);

    constexpr float kOffsetsDeg[8] = { 0.f, 180.f, 90.f, -90.f, 45.f, -45.f, 135.f, -135.f };
    for (float offDeg : kOffsetsDeg)
    {
        const float yaw = viewYawRad + offDeg * (3.14159265f / 180.f);
        if (AntiLadderExitDir(pLocal, origin, mins, maxs, yaw))
        {
            // push straight at `yaw` (world space), decomposed into the cmd's own view frame.
            // BUG (fixed): LadderMove's attached branch reads BUTTONS only, never these floats
            // (gamemovement.cpp:2921-2932, see ladder.inl file header) -- writing forwardmove/
            // sidemove alone was a no-op on the actual climb. Derive the button bits too.
            const float yawDeg = yaw * (180.f / 3.14159265f);
            const auto fs = ladderfx::WishToYaw(yawDeg, pCmd->viewangles.y, 450.f);
            pCmd->forwardmove = fs.forward;
            pCmd->sidemove    = fs.side;
            const auto btns = ladderfx::WishToButtons(fs.forward, fs.side);
            pCmd->buttons &= ~k_fmMoveBtns;
            if (btns.fwd)   pCmd->buttons |= IN_FORWARD;
            if (btns.back)  pCmd->buttons |= IN_BACK;
            if (btns.left)  pCmd->buttons |= IN_MOVELEFT;
            if (btns.right) pCmd->buttons |= IN_MOVERIGHT;
            s_antiLadderHit = true;
            return;
        }
    }
}

// ---- auto_one_hop_onladder -- csgo-sdk movement.cpp auto_one_hop's ladder-climbing phase.
// CS:GO uses g_prediction.begin/end to ask "does this cmd leave the ladder next tick"; the CSS
// substitute probes geometrically. BUG (fixed): the re-check LadderMove actually runs once
// attached does NOT use the view yaw or the un-attached 2u grab trace at all -- when
// GetMoveType()==MOVETYPE_LADDER, wishdir is forced to -m_vecLadderNormal (the STORED face
// normal, gamemovement.cpp:2869-2872) and LadderDistance() returns 10u, not 2u, while attached
// (cs_gamemovement.cpp:54-58). A trace along the camera's view yaw for 2u checks a direction
// and a distance the engine never checks. Fixed: find the real face normal the same way
// fireman.inl's FmLadderFace does (it already runs this exact 10u-attached search), then probe
// along -normal for 10u, from the current spot and from one climb-step higher -- press jump
// before the higher probe misses, same as CS:GO's "will_leave -> phase_jumped" transition
// (:2323-2345 in csgo-sdk).
static bool AutoOneHopProbe(void* pLocal, const Vec3& start, const Vec3& mins, const Vec3& maxs, const Vec3& n)
{
    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    const Vec3 end = start - n * 10.f;   // LadderDistance() attached = 10u, wishdir = -storedNormal
    GameTrace_t tr{};
    TraceHull(start, end, mins, maxs, k_ladderMaskMv, &fil, &tr);
    return tr.fraction < 1.f && (tr.contents & k_contentsLadderMv);
}

static void AutoOneHopLadder(void* pLocal, UserCmd_t* pCmd)
{
    s_autoOneHopHit = false;
    if (!On(g_autoOneHopLadder, g_autoOneHopLadderKey, g_autoOneHopLadderMode)) return;
    if (g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0) return;
    if (EntMoveType(pLocal) != MOVETYPE_LADDER) return;

    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);

    Vec3 n{};
    const bool hasFace = FmLadderFace(pLocal, origin, mins, maxs, n);
    const float climbStep = 200.f * TickInterval();   // ClimbSpeed() flat MAX_CLIMB_SPEED, shareddefs.h:103
    const bool stillOn = hasFace && AutoOneHopProbe(pLocal, origin, mins, maxs, n);
    const bool leavesNext = stillOn
        && !AutoOneHopProbe(pLocal, Vec3(origin.x, origin.y, origin.z + climbStep), mins, maxs, n);

    if (leavesNext)
    {
        pCmd->buttons |= IN_JUMP;   // LadderMove's own IN_JUMP branch flings us off the top
        pCmd->forwardmove = 0.f; pCmd->sidemove = 0.f;
        s_autoOneHopHit = true;
        return;
    }

    pCmd->buttons |= IN_FORWARD; pCmd->buttons &= ~IN_BACK;
    pCmd->forwardmove = 450.f; pCmd->sidemove = 0.f; pCmd->upmove = 0.f;
}

// ---- dispatcher for the Movement_RunImpl call site: every feature here needs movetype==LADDER
// live, which bMoveBlocked's early return would otherwise hide (see file header).
static void LadderOnLadderTick(void* pLocal, UserCmd_t* pCmd)
{
    FastLadder(pLocal, pCmd);
    LadderGlide(pLocal, pCmd);
    LadderFreelookClimb(pLocal, pCmd);
    AntiLadder(pLocal, pCmd);
    AutoOneHopLadder(pLocal, pCmd);
}

// ---- edge jump "on ladders" + long jump's edge jump "on ladders" -- csgo-sdk movement.cpp
// edge_jump (:1496-1519) and long_jump (:1652-1682). botox runs post-prediction and its
// g_prediction.begin does NOT restore: the extra sim flies tick N+1 from this cmd's END. pre ladder
// + post not ladder = THIS cmd is the last one still attached -> IN_JUMP now, so LadderMove's jump
// branch (gamemovement.cpp:2934-2939) flings 270 * normal off the top. On the exit tick itself the
// trace already misses (:2895), WALK takes over and CheckJumpButton refuses an airborne press.
// CSS: no prediction runs on a ladder cmd (bMoveBlocked), so this opens its own from START:
// sim, sim again, and the fling cmd is sim-proven before it is sent.
// After: edge jump zeroes the move + ducks ticks 2..14 (botox ej_window); long jump holds duck via
// LongJump's own hold (s_ljHeld), the same as a ground takeoff.
static int  s_ladderEjTick = INT_MIN / 2;   // pCmd->tick_count of the last ladder edge jump
static bool s_ladderEjMain = false;         // that one was the main edge jump's (owns the window)

static void LadderEdgeJump(void* pLocal, UserCmd_t* pCmd)
{
    // mini jump's "edge jump" rides the main toggle, as on the ground (EdgeJump)
    const bool ej = g_edgeJumpLadder && (On(g_edgeJump, g_edgeJumpKey, g_edgeJumpMode)
                 || (g_miniJumpEdge && On(g_miniJump, g_miniJumpKey, g_miniJumpMode)));
    const bool lj = g_longJumpEdge && g_longJumpEdgeLadder && On(g_longJump, g_longJumpKey, g_longJumpMode);
    if (!ej && !lj) return;
    if (pCmd->buttons & IN_JUMP) return;   // already jumping off (you, or auto one hop)
    if (g_offVelocity <= 0) return;

    UserCmd_t plain = *pCmd;
    if (!Pred_Start(pLocal, &plain)) return;
    bool last = false;
    if (EntMoveType(pLocal) == MOVETYPE_LADDER)
    {
        Pred_Simulate(pLocal, &plain);
        last = EntMoveType(pLocal) != MOVETYPE_LADDER;
    }
    Pred_End(pLocal, pCmd);
    if (!last) return;

    // fling cmd: no move (botox zeroes it; the jump branch ignores climb keys, AirMove then adds
    // nothing so IsFlung's 270 check is exact). LJ tries the duck on this tick first (botox ducks
    // it: now_ladder is false after the jump) -- an air duck lifts the hull 8.5 BEFORE LadderMove
    // traces, which can miss the top rung, so the sim picks.
    UserCmd_t jump = *pCmd;
    jump.forwardmove = jump.sidemove = jump.upmove = 0.f;
    jump.buttons = (jump.buttons & ~k_fmMoveBtns) | IN_JUMP;
    const float g = CvarFloat("sv_gravity", 800.f), dt = TickInterval();
    const auto flings = [&](UserCmd_t c) {
        if (!Pred_Start(pLocal, &c)) return false;
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        const bool ok = EntMoveType(pLocal) == MOVETYPE_WALK && fireman::IsFlung(v.x, v.y, v.z, g, dt);
        Pred_End(pLocal, pCmd);
        return ok;
    };
    UserCmd_t duck = jump;
    duck.buttons |= IN_DUCK;
    const bool withDuck = lj && flings(duck);
    if (!withDuck && !flings(jump))
    {
        DebugLog("LEJ: last ladder tick, fling refused by the sim (%s)", lj ? "lj" : "ej");
        return;
    }

    const UserCmd_t& sent = withDuck ? duck : jump;
    pCmd->forwardmove = sent.forwardmove;
    pCmd->sidemove    = sent.sidemove;
    pCmd->upmove      = sent.upmove;
    pCmd->buttons     = sent.buttons;
    s_ladderEjTick = pCmd->tick_count;
    s_ladderEjMain = ej;
    if (lj) { s_ljHeld = 1; s_ljTakeoffTick = pCmd->tick_count; }   // LongJump holds from the next cmd
    DebugLog("LEJ: ladder edge jump (%s%s) z=%.1f", ej ? "ej" : "lj", withDuck ? " +duck" : "",
             EntV3(pLocal, g_offOrigin).z);
}

// botox ej_window, every cmd (the window runs off the ladder): ticks 2..14 after the fling
static void LadderEdgeJumpWindow(UserCmd_t* pCmd)
{
    if (!s_ladderEjMain) return;
    if (!(g_edgeJumpLadder && (On(g_edgeJump, g_edgeJumpKey, g_edgeJumpMode)
                            || (g_miniJumpEdge && On(g_miniJump, g_miniJumpKey, g_miniJumpMode)))))
        return;
    const int window = std::max(2, (int)std::round(15.f / (64.f * TickInterval())));   // ticks(15) at 64
    const int d = pCmd->tick_count - s_ladderEjTick;
    if (d <= 1 || d >= window) return;
    pCmd->forwardmove = pCmd->sidemove = 0.f;
    pCmd->buttons = (pCmd->buttons & ~k_fmMoveBtns) | IN_DUCK;
}

// ---- ladder_bug -- N/A, with the CSS source proving it. csgo-sdk's ladder_bug writes
// `cmd->m_up_move = 450.f; cmd->m_buttons |= in_jump;` on the grab tick, relying on CS:GO's
// IGNORE_JUMP_TIME cooldown (cstrike15 gamemovement.cpp :3148-3292) to turn that into a held
// RIDE rather than an instant exit. CSS's LadderMove (gamemovement.cpp:2854-3014) and
// FullLadderMove (:2536-2554) never read mv->m_flUpMove at all (grepped both functions,
// verbatim above), and IN_JUMP while attached flings IMMEDIATELY (:2934-2939) -- there is no
// cooldown to ride and no up_move path to exploit. The exploit's engine hook does not exist in
// this source. anti_ladder, which csgo-sdk gates behind the SAME m_ladder_bug / m_ladder_bug_key
// variable, is kept above under its own g_antiLadder control (it is generic brute-force logic,
// no CS:GO-specific field) -- that is the useful half of the CS:GO "ladder_bug" key anyway.
//
// poop_ladder_jump -- N/A, with the CSS source proving it. The current csgo-sdk implementation
// (11-01 rewrite, no longer ladder-related despite the name) has two paths:
//   local:  scales the REPLICATED cvar `sv_jump_impulse`, which CheckJumpButton (Kisak-Strike's
//           cs_gamemovement.cpp) reads. This CSS source's CCSGameMovement::CheckJumpButton
//           (cs_gamemovement.cpp:593-699+) hardcodes the jump velocity as
//           `flGroundFactor * sqrt(2 * 800 * 57.0)` -- grepped the entire source tree for
//           "sv_jump_impulse", zero hits. No such cvar exists to scale.
//   online: issues `+jetpack`/`-jetpack` client commands. Grepped the entire source tree for
//           "jetpack", zero hits -- no such bind/command exists in this engine branch.
// Both mechanisms this feature depends on are absent from CSS. Faking a substitute (e.g.
// directly writing velocity.z client-side) would be a hybrid, not a port -- per AGENT_BRIEF,
// reported as N/A rather than invented.
