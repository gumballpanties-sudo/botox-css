// Unit A port: small movement tricks from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp (same convention as edgebug.inl/texturebug.inl) --
// shares every helper/static in that TU (On, EntV3/EntI/EntF, TraceHull, IN_*, FL_*...).
// Ported: nulls, hsw, delay hop, auto crouch, smooth demo
// view (reuses engIsPlayingDemo=76, same confirmed vtable slot as fakemove.inl), jumpstats.
// edge skip + blockbot: movement_edge.inl. bounce/pf modes/eb presets/ps fix:
// movement_tricks2.inl. No ladder tricks: TF2 has no ladders.

// (movement_ex_pure.h included near the top of movement.cpp -- LongJump's adaptive-cancel
// append needs it before this file is included)

// ==== nulls: opposing strafe keys cancel instead of fighting (botox movement.cpp:541) ====
static void Nulls(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_nulls) return;
    const int buttons = pCmd->buttons;
    if ((buttons & (IN_MOVELEFT | IN_MOVERIGHT)) == (IN_MOVELEFT | IN_MOVERIGHT))
    {
        static int s_sideWin = 0; // last unopposed side press wins the tie, like the source
        if (!(buttons & IN_MOVELEFT))  s_sideWin = 0;
        else if (!(buttons & IN_MOVERIGHT)) s_sideWin = 0;
        pCmd->buttons &= ~(s_sideWin >= 0 ? IN_MOVELEFT : IN_MOVERIGHT);
        pCmd->sidemove = (s_sideWin >= 0 ? -1.f : 1.f) * CvarFloat("cl_sidespeed", 450.f);
    }
    if ((buttons & (IN_FORWARD | IN_BACK)) == (IN_FORWARD | IN_BACK))
    {
        pCmd->buttons &= ~IN_BACK;
        pCmd->forwardmove = CvarFloat("cl_forwardspeed", 450.f);
    }
}

// ==== half sideways bind: 1:1 port of botox movement.cpp:557 (mv_hsw) ====
static void Hsw(void* pLocal, UserCmd_t* pCmd)
{
    static int s_lastButtons = 0;
    const int last = s_lastButtons;
    s_lastButtons = pCmd->buttons;
    if (!On(g_hsw, g_hswKey, g_hswMode) || EntMoveType(pLocal) != MOVETYPE_WALK)
        return;

    const bool onGround = EntGround(pLocal);
    const bool autohop  = g_bhop || CvarFloat("sv_autobunnyhopping", 0.f) != 0.f;
    const bool jumping  = onGround && (pCmd->buttons & IN_JUMP) && (autohop || !(last & IN_JUMP));
    const bool grounded = onGround && !jumping;

    const int   s = HswSign(g_hswSide, grounded);
    const float f = (float)s * 450.f;

    if ((!grounded && g_hswAllowWS) || (grounded && g_hswPreGround))
    {
        if (pCmd->buttons & IN_FORWARD) {
            pCmd->forwardmove = 450.f; pCmd->sidemove = -f;
            pCmd->buttons |= s == 1 ? IN_MOVELEFT : IN_MOVERIGHT;
        } else if (pCmd->buttons & IN_BACK) {
            pCmd->forwardmove = -450.f; pCmd->sidemove = f;
            pCmd->buttons |= s == 1 ? IN_MOVERIGHT : IN_MOVELEFT;
        }
    }
    if (!grounded || (g_hswPreGround && g_hswAllowAD))
    {
        if (pCmd->buttons & IN_MOVELEFT) {
            pCmd->forwardmove = f; pCmd->sidemove = -450.f;
            pCmd->buttons |= IN_FORWARD;
        } else if (pCmd->buttons & IN_MOVERIGHT) {
            pCmd->forwardmove = -f; pCmd->sidemove = -450.f;
            pCmd->buttons |= IN_BACK;
        }
    }
}

// ==== delay hop: space held → wait N ticks after landing, then jump (botox movement.cpp:623) ====
static void DelayHop(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_delayHop, g_delayHopKey, g_delayHopMode) || !g_globalVars) return;
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP) return;

    static int s_lastAirTick = 0;
    if (EntGround(pLocal))
    {
        // only on real space: no hold = no auto hop (air tick still tracked below)
        if (!s_rawJumpHeld) return;
        if (DelayHopShouldJump(pCmd->tick_count, s_lastAirTick, g_delayHopTicks))
        {
            pCmd->buttons |= IN_JUMP;
            s_lastAirTick = pCmd->tick_count;   // botox: reset on press
        }
        else
            pCmd->buttons &= ~IN_JUMP;
    }
    else
    {
        s_lastAirTick = pCmd->tick_count;
    }
}
// ==== auto goomba (TF rocket/sticky-jump blast boost) and double-jump-strafe (scout AirDash)
// removed: TF2-only, no CS equivalent (CS has no rocket/sticky jump, no scout, no air dash).
// bounce_assist.cpp (botox CS:GO's generic teammate-head-boost) is the real CS analog and is
// NOT yet ported -- it needs assist_predict.h's prediction-replay simulation, see report.
// ==== auto crouch: duck ahead of a landing too low to stand in (simplified from botox
// movement.cpp:2144 -- that version compares a standing-land trace vs a ducking-land trace;
// here we trust CategorizePosition's own down-trace the way MJ_ClearBelow above does: one
// hull trace at the HULL'S STANDING height. A floor that trace cannot reach but a ducked
// hull (62u tall, routesim.h) would is exactly the "stand would clip, duck wouldn't" case) ====
static void AutoCrouch(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_autoCrouch, g_autoCrouchKey, g_autoCrouchMode) || On(g_jumpbug, g_jumpbugKey, g_jumpbugMode))
        return;
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP || EntGround(pLocal))
        return;
    if (g_offVelocity <= 0 || g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0 || !g_engineTrace)
        return;

    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    if (vel.z >= 0.f) return;   // only while falling

    const rs::Params p = MV_SimParams(pLocal);
    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vStandMaxs = EntV3(pLocal, g_offMaxs);   // current (standing) hull top
    TraceFilterWorldProps filter; filter.Setup(pLocal);

    const float flLookAhead = fabsf(vel.z) * p.dt * (float)g_autoCrouchTicks;
    GameTrace_t tr{};
    TraceHull(vOrigin, vOrigin - Vec3(0, 0, flLookAhead + 2.f), vMins, vStandMaxs,
              MASK_PLAYERSOLID, &filter, &tr);
    if (!tr.DidHit()) return;

    // headroom above the landing spot, standing hull height
    GameTrace_t headTr{};
    const Vec3 vLand = tr.endpos;
    TraceHull(vLand, vLand + Vec3(0, 0, vStandMaxs.z - vMins.z), vMins, vStandMaxs,
              MASK_PLAYERSOLID, &filter, &headTr);
    if (headTr.DidHit() && headTr.fraction < 0.98f)
        pCmd->buttons |= IN_DUCK;
}

// ==== fake pov in demos (botox misc/fake_pov.cpp) ====
// 1:1 port of the angle/arrow math only -- fake_pov.cpp's actual effect (CPrediction::
// GetLocalViewAngles override, demo-angle writeback) is engine-hook plumbing that already
// exists under src/hacks/aim/ (G::FakePOVOffset/G::FakePOVActive, hooks.cpp
// CPrediction_GetLocalViewAngles -- a different unit's SDK layer, which this file cannot
// include per the INCLUDE RULE). FakePov_Tick() computes the same smoothed yaw/pitch/arrow
// state botox keeps and exposes it through FakePov_GetState(); NEEDS FROM OTHERS: a
// css_bridge.h setter (e.g. CssBridge_SetFakePov(yaw, pitch, active)) so the aim-unit hook can
// read it instead of fake_pov's own G:: globals staying permanently unfed on this port.
static float s_fpYaw = 0.f, s_fpPitch = 0.f, s_fpSpin = 0.f;
static float s_fpArrowAng = 0.f, s_fpArrowLen = 0.f;
static bool  s_fpActive = false;

static float FpNormalizeDeg(float a)
{
    a = fmodf(a + 180.f, 360.f);
    if (a < 0.f) a += 360.f;
    return a - 180.f;
}
static float FpApproachAngle(float cur, float target, float k) { return cur + FpNormalizeDeg(target - cur) * k; }

void FakePov_Tick(float dtRaw)
{
    enum { mode_right, mode_left, mode_up, mode_bottom, mode_backwards, mode_spinning };
    const float dt = (std::min)(dtRaw, 0.1f);
    const bool wanted = On(g_fakePov, g_fakePovKey, g_fakePovKeyMode);
    float targetPitch = 0.f, targetYaw = 0.f, targetArrow = 0.f, targetLen = 0.f;
    if (wanted)
    {
        targetLen = 1.f;
        switch (g_fakePovMode)
        {
        case mode_right:     targetYaw = -90.f; targetArrow = 90.f; break;
        case mode_left:      targetYaw = 90.f;  targetArrow = 270.f; break;
        case mode_up:        targetPitch = -89.f; break;
        case mode_bottom:    targetPitch = 89.f; targetArrow = 180.f; break;
        case mode_backwards: targetYaw = 180.f; targetArrow = 180.f; break;
        case mode_spinning:
            s_fpSpin = FpNormalizeDeg(s_fpSpin + g_fakePovSpinSpeed * dt);
            targetYaw = s_fpSpin; targetArrow = -s_fpSpin;
            break;
        }
    }
    if (!wanted || g_fakePovMode != mode_spinning) s_fpSpin = s_fpYaw;

    const float k = 1.f - expf(-(std::max)(0.1f, g_fakePovSmooth) * dt);
    s_fpYaw      = FpApproachAngle(s_fpYaw, targetYaw, k);
    s_fpPitch    = s_fpPitch + (targetPitch - s_fpPitch) * k;
    s_fpArrowAng = FpApproachAngle(s_fpArrowAng, targetArrow, k);
    s_fpArrowLen = s_fpArrowLen + (targetLen - s_fpArrowLen) * k;
    s_fpActive   = wanted || fabsf(s_fpYaw) > 0.05f || fabsf(s_fpPitch) > 0.05f;
}

// yaw/pitch = view angle offset while active; arrowAng/arrowLen = indicator state (0-1 len).
// Consumed by: whoever hooks CPrediction::GetLocalViewAngles (apply offset while recording) and
// whoever draws the overlay arrow (fakePovArrow* vars, cfg/u_mvcore.h) -- both NEEDS FROM OTHERS.
void FakePov_GetState(float& yaw, float& pitch, float& arrowAng, float& arrowLen, bool& active)
{
    yaw = s_fpYaw; pitch = s_fpPitch; arrowAng = s_fpArrowAng; arrowLen = s_fpArrowLen; active = s_fpActive;
}

// ==== smooth demo view: lock the OUTGOING cmd's viewangles to the last sent angle while
// playing a demo back, so a silent-aim/strafe-opt snap the user made live is not re-shown
// in playback. engIsPlayingDemo = IVEngineClient vtable slot 76, already confirmed and used
// by fakemove.inl / flipworld.cpp -- no new hook. Ported from botox create_move_proxy.cpp:238 ====
// isolated: MSVC C2712 forbids __try in a function that also needs C++ object unwinding
// (see globals.h's MiscHookGuard note) -- SmoothDemoView itself has none of that, but
// keeping the SEH call in its own tiny function is the project's own idiom for this.
static bool SDV_IsPlayingDemo()
{
    bool demo = false;
    __try { demo = VCall<bool>(g_engine, 76); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return demo;
}

static void SmoothDemoView(void* pLocal, UserCmd_t* pCmd)
{
    static Vec3 s_lastSentAngles{};
    static bool s_have = false;
    if (!g_silentView || !g_engine) { s_have = false; return; }

    if (!SDV_IsPlayingDemo()) { s_have = false; return; }
    if (!pLocal || !EntAlive(pLocal)) { s_have = false; return; }

    if (s_have) pCmd->viewangles = s_lastSentAngles;
    s_lastSentAngles = pCmd->viewangles;
    s_have = true;
}

// ==== jumpstats: botox jump_stats 1:1 (jumpstats_core.h), chat line per landed jump.
// botox reads START = pre-pred backup, END = local state after the last mover. Pred_End copies
// START back onto the entity, so END is captured at botox's spot (JsCaptureEnd, after PixelSurf)
// and stepped at cmd end, once this cmd's trick flags are final ====
struct JsEnd { bool ok; bool ground, walk; Vec3 org, vel; int buttons, mouseDx; };
static JsEnd s_jsEnd{};

static void JsCaptureEnd(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_jumpstats) return;
    // movers leave the entity wherever their last probe was -- PixelSurf / AutoAlign / TB / AS
    // restore START and stop. Reading that as END = start ground == end ground every cmd = no
    // takeoff ever = no line. So fly the final cmd from START here, read, put START back
    RestoreToFrame(PredCommandsPredicted() - 1);
    Pred_Simulate(pLocal, pCmd);
    s_jsEnd.ok      = true;
    s_jsEnd.ground  = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    s_jsEnd.walk    = EntMoveType(pLocal) == MOVETYPE_WALK;
    s_jsEnd.org     = EntV3(pLocal, g_offOrigin);
    s_jsEnd.vel     = EntV3(pLocal, g_offVelocity);
    s_jsEnd.buttons = pCmd->buttons;
    s_jsEnd.mouseDx = pCmd->mousedx;
    RestoreToFrame(PredCommandsPredicted() - 1);
}

static void JumpstatsTick(UserCmd_t* pCmd, bool trick)
{
    static JsState s;
    if (!g_jumpstats) { s = {}; return; }
    if (!s_jsEnd.ok) return;   // no END this cmd (unpredicted / blocked): the tick gap drops the jump
    s_jsEnd.ok = false;

    JsIn in;
    in.tick        = pCmd->tick_count;
    in.walk        = s_jsEnd.walk;
    in.startGround = (s_prePredFlags & FL_ONGROUND) != 0;
    in.endGround   = s_jsEnd.ground;
    in.startX = s_prePredOrigin.x; in.startY = s_prePredOrigin.y; in.startZ = s_prePredOrigin.z;
    in.startVz     = s_prePredVel.z;
    in.startSpeed  = s_prePredVel.Length2D();
    in.endX = s_jsEnd.org.x; in.endY = s_jsEnd.org.y; in.endZ = s_jsEnd.org.z;
    in.endVz       = s_jsEnd.vel.z;
    in.endSpeed    = s_jsEnd.vel.Length2D();
    in.duck        = (s_jsEnd.buttons & IN_DUCK) != 0;
    in.mouseDx     = s_jsEnd.mouseDx;
    in.trick       = trick;
    in.showFails   = g_jumpStatsShowFails;
    JsResult r;
    if (JsStep(s, in, r)) Detections_Jumpstats(r);
}
