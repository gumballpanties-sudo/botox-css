// pixelsurf.inl -- pixel surf (decision rewritten 11-07 off the CSS source, see PixelSurfImpl) +
// the shared wall probes (PsWallAhead / PsWallTouch / PsWallDetectFallback), ps_cmd_start bail,
// the ride detect, pixelsurf lines and the detection sound.
// Textually included by movement.cpp (same TU: Pred_Simulate / RestoreToFrame / TraceHull /
// s_prePred* / s_airStuckHit / s_textureBugHit / s_tbOwnsCmd / s_wallClimbHit are its statics).
// Readers elsewhere keep their names: s_shouldPixelSurf (latched either way), s_psShouldDuck /
// s_psAirDuckHeld (the duck latch), s_pixelSurfTicks (the ride timer), s_psRidingNow, s_psEv.

#include "airstuck_pure.h"
#include "ps_lines.h"   // PSPORT round 3: g_psLinesEnable gates PsTrajectoryRecord below

static void PrCmd(const char* fmt, ...);   // defined later in this TU by practice.inl (movement.cpp)

// ---- PIXEL SURF DETECTION SOUND -- botox movement.cpp:4781 play_trick_sound() + :5018-5063
// surf_sound() 1:1 (own copy, pixel surf only -- texture bug's own copy is texturebug.inl's).
// index: 0 none / 1 arena switch / 2 button / 3 money / 4 beep / 5 custom (g_psSoundCustom).
// None of botox's 4 csgo files ship with CSS (vpk dir checked, same conclusion movement_tricks2.inl
// reached for EdgeBug): nearest stock cstrike/hl2 sound per slot, same order, same files EdgeBug
// already confirmed present. "play" has no volume argument (Source console cmd) -- g_psSoundVolume
// is read and stored but has no effect until routed through IEngineSound (see report NEEDS FROM OTHERS),
// same known gap movement_tricks2.inl's EdgeBugSoundPre already ships with.
static const char* const kPsSounds[] = { "buttons/lever7.wav", "buttons/button9.wav",
    "buttons/bell1.wav", "buttons/blip1.wav" };

static void PsPlaySound(int idx, const char* custom)
{
    if (idx <= 0) return;
    const char* path = nullptr;
    if (idx >= 1 && idx <= 4) path = kPsSounds[idx - 1];
    else if (idx == 5) path = (custom && custom[0]) ? custom : nullptr;
    if (!path) return;
    if (idx == 5 && CustomSound_Play(path, g_psSoundVolume)) return;   // C:\botox-css\sounds\<file>.wav
    PlaySoundVol(path, g_psSoundVolume);   // edgebug.inl, playvol
}

static void PsSurfSoundTick(bool riding, void* pLocal)
{
    constexpr int k_offForever = 1 << 30;   // "hasn't surfed in forever" sentinel (botox INT_MAX)
    struct State { bool wasSurfing = false; Vec3 lastPos{}; float accum = 0.f; int offTicks = k_offForever; unsigned long long nextTime = 0ull; };
    static State s;
    const int offTrigger = as::TicksScaled(8, TickInterval());
    if (riding && pLocal) {
        const Vec3 pos = EntV3(pLocal, g_offOrigin);
        constexpr unsigned long long k_minGap = 120ull;   // ms, botox's own gap
        const unsigned long long now = GetTickCount64();
        const bool newSurf = !s.wasSurfing && s.offTicks >= offTrigger;
        s.offTicks = 0;
        if (newSurf) {
            if (now >= s.nextTime) { PsPlaySound(g_psSound, g_psSoundCustom); s.nextTime = now + k_minGap; }
            s.accum = 0.f;
        } else {
            const Vec3 d(pos.x - s.lastPos.x, pos.y - s.lastPos.y, 0.f);
            const float travelled = d.Length2D();
            if (travelled >= 0.05f) s.accum += travelled;
            while (s.accum >= 10.f) {
                s.accum -= 10.f;
                if (now >= s.nextTime) { PsPlaySound(g_psSound, g_psSoundCustom); s.nextTime = now + k_minGap; }
            }
        }
        s.lastPos = pos;
    } else {
        if (s.offTicks < k_offForever) ++s.offTicks;
        if (s.offTicks >= offTrigger) s.accum = 0.f;
    }
    s.wasSurfing = riding;
}

// TRACE_EVERYTHING_FILTER_PROPS with no entity ever accepted = world brushes only (botox
// c_trace_filter_world / c_trace_filter_trace_type_everything_filter_props). Shared with airstuck.inl
struct TraceFilterWorldOnly
{
    void** vfptr;
    static bool __fastcall sShouldHit(TraceFilterWorldOnly*, void*, int) { return false; }
    static int  __fastcall sTraceType(TraceFilterWorldOnly*) { return 3; }
    void Setup()
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
    }
};

static bool s_psShouldSurf   = false;   // data.should_pixel_surf  ( duck latched )
static bool s_psShouldUnduck = false;   // data.should_unduck      ( stand latched )
static bool s_psBail         = false;   // s_ps_bail
static int  s_psTicks        = 0;       // `ticks`: latch + flip + 16 ( scaled )

/* botox ps_cmd_start: your own duck key flipped on a live ride = you bailed, PS lets go until the ground */
static void PsCmdStart(void* pLocal)
{
    static bool prev_key = false;
    const bool ground = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool key    = (s_rawButtons & IN_DUCK) != 0;
    const bool ducked = (s_prePredFlags & FL_DUCKING) != 0;
    const bool riding = !ground && IsTargetVz(s_prePredVel.z, 0.01f);
    if (ground || EntMoveType(pLocal) != MOVETYPE_WALK)
        s_psBail = false;
    else if (key != prev_key && riding && key != ducked && !s_psBail) {
        s_psBail = true;
        s_psShouldSurf = s_psShouldUnduck = false;
        DebugLog("PSU: bail key=%s on %s surf z=%.3f", key ? "duck" : "stand", ducked ? "duck" : "stand", s_prePredOrigin.z);
    }
    prev_key = key;
}

static bool PsWallAhead(void* pLocal, const int n, Vec3* normal = nullptr)
{
    const float dt   = TickInterval() * static_cast<float>(n);
    const Vec3 vel   = s_prePredVel;
    const Vec3 from  = s_prePredOrigin;
    const Vec3 to(from.x + vel.x * dt, from.y + vel.y * dt, from.z);
    const Vec3 grow(1.f, 1.f, 0.f);
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(from, to, EntV3(pLocal, g_offMins) - grow, EntV3(pLocal, g_offMaxs) + grow, MASK_PLAYERSOLID, &fil, &tr);
    const bool hit = tr.DidHit() && fabsf(tr.plane.normal.z) < 0.7f;
    if (hit && normal)
        *normal = tr.plane.normal;
    return hit;
}

static bool PsWallTouch(void* pLocal, Vec3& normal, const Vec3& from)
{
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float dx[4] = { 0.05f, -0.05f, 0.f, 0.f }, dy[4] = { 0.f, 0.f, 0.05f, -0.05f };
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    float best = 1.f;
    for (int k = 0; k < 4; k++) {
        GameTrace_t tr{};
        TraceHull(from, Vec3(from.x + dx[k], from.y + dy[k], from.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.DidHit() && !tr.startsolid && tr.plane.normal.z == 0.f && tr.fraction < best) {
            best   = tr.fraction;
            normal = tr.plane.normal;
        }
    }
    return best < 1.f;
}
static bool PsWallTouch(void* pLocal, Vec3& normal) { return PsWallTouch(pLocal, normal, EntV3(pLocal, g_offOrigin)); }

/* botox movement.cpp:456-475: auto align off = pixel surf finds its own wall ( 16 x 1u hull rays ) */
static void PsWallDetectFallback(void* pLocal)
{
    s_aaWallDetected = false;
    if (!g_pixelSurf || (s_prePredFlags & FL_ONGROUND) || (EntI(pLocal, g_offFlags) & FL_ONGROUND))
        return;
    const Vec3 start = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldOnly fil; fil.Setup();
    GameTrace_t tr{};
    TraceHull(start, start, mins - Vec3(1.f, 1.f, 0.f), maxs + Vec3(1.f, 1.f, 0.f), MASK_PLAYERSOLID, &fil, &tr);
    for (int k = 0; k < 16 && tr.DidHit() && !s_aaWallDetected; k++) {
        const float a = static_cast<float>(k) * (as::k_pi * 2.f / 16.f);
        GameTrace_t hit{};
        TraceHull(start, Vec3(start.x + cosf(a), start.y + sinf(a), start.z), mins, maxs, MASK_PLAYERSOLID, &fil, &hit);
        s_aaWallDetected = hit.fraction != 1.f && hit.plane.normal.z == 0.f;
    }
}

// ==== PIXEL SURF ===============================================================================
// Mechanism (css-community gamemovement.cpp): in the air TryPlayerMove (:2560) clips velocity off
// every plane the hull sweep hits. A hull flush on a face whose feet cross that brush's TOP plane on
// the same sweep gets the top plane as its clip: vz -> 0 (+1/32 CSS nudge), CategorizePosition's
// 2u down trace misses the brush, FinishGravity leaves vz = -halfgrav. Repeats while every tick's
// AirAccelerate presses back into the face. Levers: feet height on the crossing tick (air duck /
// unduck = 8.5u) and the press.
// 11-08b: decision = botox movement.cpp:3440 pixel_surf 1:1 again (PixelSurfImpl below). The 11-07
// port of it never latched for ONE reason: its pin test was botox's exact |vz - pin| < 0.01 and a
// CSS clipped vz sits at pin + 1/32 (ref clip nudge) -- fixed in as::SurfPinned. The ref
// wall stuck engine that replaced it logged catches=0 on ~every tick (~300 sims/miss, 11-08 log).

static Vec3 PsWishWorld(const UserCmd_t& c)
{
    float x = 0.f, y = 0.f;
    cssm::CmdToWorld(c.viewangles.y, c.forwardmove, c.sidemove, x, y);
    return Vec3(x, y, 0.f);
}

// the cmd with its wish swapped for "into face n, along-face part of your wish kept while the START
// velocity leaves the air cap room for it to still push in" (cssm::PressIntoDir)
static UserCmd_t PsPressInto(const UserCmd_t& c, const Vec3& n)
{
    const Vec3 w = PsWishWorld(c);
    float dx = 0.f, dy = 0.f;
    cssm::PressIntoDir(w.x, w.y, n.x, n.y, s_prePredVel.x, s_prePredVel.y, dx, dy);
    UserCmd_t out = c;
    MV_WriteWishWorld(&out, Vec3(dx, dy, 0.f), 450.f);
    out.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    return out;
}

static UserCmd_t PsWithDuck(UserCmd_t c, bool duck)
{
    c.buttons = duck ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
    return c;
}

// per-second air-tick outcomes: nowall = no face near, yield = AS/TB/WC/PH own the tick (or AA holds),
// budget = sim pool spent, own = your stance catches by itself (nothing to latch), miss = searched,
// nothing pins, latch / hold / lost = ride caught / kept / dropped
namespace psd
{
    enum Out { o_nowall, o_yield, o_budget, o_own, o_miss, o_latch, o_hold, o_lost, o_count };
    int s_outs[o_count] = {};
    static void Count(Out o)
    {
        ++s_outs[o];
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (!g_debugLogEnable || sec == s_sec) return;
        s_sec = sec;
        DebugLog("PS: nowall=%d yield=%d budget=%d own=%d miss=%d latch=%d hold=%d lost=%d", s_outs[o_nowall], s_outs[o_yield],
                 s_outs[o_budget], s_outs[o_own], s_outs[o_miss], s_outs[o_latch], s_outs[o_hold], s_outs[o_lost]);
        for (int& c : s_outs) c = 0;
    }
}

// botox movement.cpp:3440-3635 pixel_surf, 1:1. CSS shims:
//   g_prediction.begin/end               -> Pred_Simulate on a copy (it walks m_nTickBase itself = botox's base + z)
//   restore_entity_to_predicted_frame    -> RestoreToFrame;  backup_data -> s_prePred* (START)
//   old_view_point                       -> cmd->viewangles (movement runs before aim here)
//   air stuck / tb / wall climb owns     -> s_airStuckHit / s_textureBugHit|s_tbOwnsCmd / s_wallClimbHit|Active
//   n_tick::ticks / cmd_clock_left       -> as::TicksScaled / TbBudgetExpired (+ SimsLeft per sim)
//   pinned / stuck                       -> as::SurfPinned / as::WallHang (CSS clip nudge aware)
// Added: pixel hop yield (it runs right before on TB's cmd), auto align "hold pixel surfs" (s_aaHolding:
// AA already proved this cmd 6 ticks out = keep the latch, no search), ps assist seam press keeps keys,
// psd per-second counters.
static int s_psUnpinned = 0, s_psLastBy = 0;
static void PixelSurfImpl(void* pLocal, UserCmd_t* cmd)
{
    const auto unlatch = [] { s_psShouldSurf = s_psShouldUnduck = false; };
    if (!On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode)) { unlatch(); return; }
    if (!EntAlive(pLocal)) { s_psTicks = 0; return; }
    if (EntMoveType(pLocal) == MOVETYPE_NOCLIP || EntMoveType(pLocal) == MOVETYPE_LADDER) { unlatch(); return; }
    // ground drops the latch in the pipeline (movement.cpp, before AutoAlign)
    if ((s_prePredFlags & FL_ONGROUND) || s_psBail) return;

    const bool latched = s_psShouldSurf || s_psShouldUnduck;
    const bool tbOwns  = s_textureBugHit || s_tbOwnsCmd;
    // s_pixelHopOn: ref skips wall stuck while the PH key is held. A PS catch latches duck / presses the
    // face 8 ticks out and steers you off the ledge PH lands on (log 11-08: PS on = floor lands only)
    if (s_airStuckHit || tbOwns || s_wallClimbHit || s_wallClimbActive || s_pixelHopHit || s_pixelHopOn)
    {
        if (latched)
            DebugLog("PSU: yield by=%s", s_airStuckHit ? "as" : tbOwns ? "tb" : (s_pixelHopHit || s_pixelHopOn) ? "ph" : "wc");
        unlatch();
        return psd::Count(psd::o_yield);
    }
    if (s_aaHolding)
    {
        if (latched) s_psUnpinned = 0;
        return psd::Count(latched ? psd::o_hold : psd::o_yield);
    }

    const float ipt = TickInterval();
    if (!latched && !s_aaWallDetected && !PsWallAhead(pLocal, as::TicksScaled(8, ipt)))
        return psd::Count(psd::o_nowall);
    if (!latched && TbBudgetExpired())
        return psd::Count(psd::o_budget);

    const int   frame = PredCommandsPredicted() - 1;
    const float pin   = TargetPredictZVel();
    /* xy < 1 = wall hang ( stuck ), not a surf. 10-07 21:28 log: all 3 rides spd 0, one hung 53 ticks */
    const auto pinned = [pin](const Vec3& v) { return as::SurfPinned(v.z, v.Length2D(), pin); };
    const auto sim = [&](int buttons) {
        UserCmd_t t = *cmd;
        t.buttons = buttons;
        Pred_Simulate(pLocal, &t);
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND);
    };

    // first tick of 2 pinned in a row within n ticks flying `buttons` (the first `delay` ticks your own), -1 none
    const auto hunt = [&](const int buttons, const int n, const int delay = 0) {
        const int live = cmd->buttons;
        RestoreToFrame(frame);
        int found = -1, run = 0;
        for (int z = 0; z <= n && found < 0 && SimsLeft() > 0; z++)
        {
            if (!sim(z < delay ? live : buttons))
                break;
            run = pinned(EntV3(pLocal, g_offVelocity)) ? run + 1 : 0;
            if (run == 2)
                found = z - 1;
        }
        RestoreToFrame(frame);
        return found;
    };

    const auto keepsPin = [&]() {
        RestoreToFrame(frame);
        bool ok = true;
        for (int z = 0; z < 2 && ok && SimsLeft() > 0; z++)
            ok = sim(cmd->buttons) && pinned(EntV3(pLocal, g_offVelocity));
        RestoreToFrame(frame);
        return ok;
    };

    const bool keysFree = !AutoBounceOwnsCmd() && !s_psAssistPressIn;
    // 0 none / 1 your keys keep it / 2 your wish dir / 3 straight into the wall / 4 best along-wall strafe
    const auto hold = [&]() -> int {
        RestoreToFrame(frame);
        Vec3 n{};
        if (!keysFree || !PsWallTouch(pLocal, n))
            return 0;
        const float fwd = cmd->forwardmove, side = cmd->sidemove;
        const Vec3  wish = PsWishWorld(*cmd);
        const float len  = wish.Length2D();
        constexpr float k_leaveDot = 0.5f;
        if (len > 0.01f && (wish.x * n.x + wish.y * n.y) / len > k_leaveDot)
            return 0;
        // texture bug guard = keepsPin's 2 pinned ticks: CSS clip nudge leaves a tb pin unholdable the
        // next tick, a pixel surf rides on (11-08 PsAhead geometry gates read no on every surf, deleted)
        /* strafing into the face = 450 at the best along-wall strafe yaw ( wall eats the into part ), toward the side your
           keys point along the wall ( straight in = velocity side ), only if it keeps the pin */
        constexpr float k_gainDot = 0.25f;
        // first press that pins wins. 11-08 22:58: picking the deepest catch measured deeper=0.0000 on all
        // 5 catches (catch depth is not press-controlled), reverted
        // botox m_pixel_surf_wall_strafe (variables.h:1082, menu_tab_movement.cpp:609): default
        // true. The gain path itself (as::WallStrafeYaw) already ran unconditionally in the CSS
        // port -- only the user-facing on/off toggle botox ships was missing.
        if (g_pixelSurfWallStrafe && len > 0.01f && -(wish.x * n.x + wish.y * n.y) / len > k_gainDot)
        {
            float gy = 0.f;
            if (as::WallStrafeYaw(n.x, n.y, s_prePredVel.x, s_prePredVel.y, CvarFloat("sv_air_max_wishspeed", 30.f), gy))
            {
                as::MoveTowardYaw(cmd->viewangles.y, gy, 450.f, cmd->forwardmove, cmd->sidemove);
                if (keepsPin())
                {
                    DebugLog("PSWS: gain yaw=%.2f xy=%.1f", gy, s_prePredVel.Length2D());
                    return 4;
                }
                cmd->forwardmove = fwd;
                cmd->sidemove    = side;
            }
        }
        if (keepsPin())
            return 1;
        const float yaws[2]    = { RAD2DEGF(atan2f(wish.y, wish.x)), RAD2DEGF(atan2f(-n.y, -n.x)) };
        const float presses[2] = { 45.f, 450.f };
        for (int i = 0; i < 2; i++)
        {
            if (i == 0 && len <= 0.01f)
                continue;
            for (const float f : presses)
            {
                as::MoveTowardYaw(cmd->viewangles.y, yaws[i], f, cmd->forwardmove, cmd->sidemove);
                if (keepsPin())
                    return 2 + i;
            }
        }
        cmd->forwardmove = fwd;
        cmd->sidemove    = side;
        return 0;
    };
    const auto logHold = [&](const int by) {
        if (by != s_psLastBy)
            DebugLog("PSU: hold by=%s fwd=%.0f side=%.0f vz=%.2f xy=%.1f", by == 0 ? "none" : by == 1 ? "keys" : by == 2 ? "wish" : by == 3 ? "wall" : "gain",
                     cmd->forwardmove, cmd->sidemove, s_prePredVel.z, s_prePredVel.Length2D());
        s_psLastBy = by;
        if (by >= 2) AT_Stamp(*cmd, "ps");
    };

    if (!latched)
    {
        const bool holdsDuck = (cmd->buttons & IN_DUCK) != 0;
        const int  held      = hold();
        logHold(held);
        if (held)
            return psd::Count(psd::o_hold);
        const int n8 = as::TicksScaled(8, ipt);
        if (hunt(cmd->buttons, n8) >= 0)
            return psd::Count(psd::o_own);   // your stance catches by itself: nothing to latch yet
        const int flipButtons = holdsDuck ? cmd->buttons & ~IN_DUCK : cmd->buttons | IN_DUCK;
        const int liveButtons = cmd->buttons;
        int flip = hunt(flipButtons, n8);
        /* your crouch is dropped only on the last tick that still catches: one more crouched tick still pins = wait,
           so a guess 8 ticks out never uncrouches you ( path / keys change before the lip = nothing lost ) */
        if (holdsDuck && flip >= 0 && hunt(flipButtons, n8, 1) >= 0)
            return psd::Count(psd::o_own);
        if (flip < 0)
        {
            cmd->buttons = flipButtons;
            const int by = hold();
            if (by < 2)
            {
                cmd->buttons = liveButtons;
                return psd::Count(psd::o_miss);
            }
            logHold(by);
            flip = 0;
        }
        (holdsDuck ? s_psShouldUnduck : s_psShouldSurf) = true;
        s_psTicks    = cmd->tick_count + flip + as::TicksScaled(16, ipt);
        s_psUnpinned = 0;
        cmd->buttons = flipButtons;
        AT_Stamp(*cmd, "ps");
        DebugLog("PSU: latch %s in=%d z=%.3f vz=%.2f spd=%.1f", holdsDuck ? "stand" : "duck", flip, s_prePredOrigin.z,
                 s_prePredVel.z, s_prePredVel.Length2D());
        return psd::Count(psd::o_latch);
    }

    const int ownButtons = cmd->buttons;
    cmd->buttons = s_psShouldSurf ? cmd->buttons | IN_DUCK : cmd->buttons & ~IN_DUCK;
    const int held = hold();
    logHold(held);
    const Vec3& v0   = s_prePredVel;
    const bool stuck = !held && as::WallHang(v0.z, v0.Length2D(), pin);
    if (stuck)
        cmd->buttons = ownButtons;
    else
    {
        if (cmd->tick_count <= s_psTicks)
            return psd::Count(psd::o_hold);
        s_psUnpinned = pinned(v0) ? 0 : s_psUnpinned + 1;
        if (s_psUnpinned < 2)
            return psd::Count(psd::o_hold);
        if (hunt(cmd->buttons, as::TicksScaled(4, ipt)) >= 0)
        {
            s_psUnpinned = 0;
            return psd::Count(psd::o_hold);
        }
    }
    DebugLog("PSU: release %s vz=%.2f xy=%.1f stuck=%d", s_psShouldSurf ? "duck" : "stand", v0.z, v0.Length2D(), stuck ? 1 : 0);
    unlatch();
    psd::Count(psd::o_lost);
}

// the ride detect every other reader keys on (PS assist, PsSurfFixTick, route calc, the indicator).
// Runs at the top of the probe block, before any mover, so every feature in this cmd reads this
// cmd's ride and not last tick's
static Vec3 s_psPinStart{};
static bool s_psPinAnchored = false;
static void PsRideDetect()
{
    const float flHalfGrav = -TargetPredictZVel();
    constexpr float flSurfTol = 1.5f;
    const bool bSurfVel = fabsf(s_prePredVel.z + flHalfGrav) < flSurfTol
        && (!(s_prePredFlags & FL_ONGROUND) || s_shouldPixelSurf);
    const bool wasRiding = s_psRidingNow;
    s_psRidingNow = bSurfVel && s_prevSurfVel;
    s_prevSurfVel = bSurfVel;
    if (static bool s_linesWas = false; s_linesWas != g_psLinesEnable)
    {
        s_linesWas = g_psLinesEnable;
        DebugLog("PSR: pixelsurf lines %s", g_psLinesEnable ? "on" : "off");
    }
    if (s_psRidingNow && !wasRiding)   // the edge pixelsurf lines record on
        DebugLog("PSR: ride start z=%.3f vz=%.3f lines=%d", s_prePredOrigin.z, s_prePredVel.z, g_psLinesEnable ? 1 : 0);
    // where this pin began -- the indicator's surf / wedge split (ps_surf.h)
    if (!s_psRidingNow) s_psPinAnchored = false;
    else if (!s_psPinAnchored) { s_psPinStart = s_prePredOrigin; s_psPinAnchored = true; }
    s_psEv = ps::SurfEvidence{};
}

// "park at end of surf" (TF2-port extra, no botox var/row) removed 11-03: botox pixel_surf has
// no end-of-ride braking/parking stage. PsPark() and s_psParkHold deleted with it.

// ============================================================================
// PIXELSURF LINES -- 11-09 rebuilt on the pixel finder's SURF LENGTH pass (psassist.inl PfHoldsAt).
// The botox recorder this replaced traced the wall and sim-checked guessed hull spots 16u apart,
// so its lines followed geometry, not the catch. Now: on the rising edge of a ride the real pin
// (START origin, START stance, the face the hull is flush on) becomes a finder PfHit, and
// PsRideLineTick (psassist.inl, own clock, after the movers) walks it 8u a step each way while the
// finder's two-tick hold proof still pins, the last step bisected to 1u. What it proves is the
// line; it grows on screen while walking and stays per map. A ride on a stretch already drawn
// (same face, same z) is not walked again.
// same IVEngineClient::GetLevelName slot 51 psassist.inl's EngineLevelName() calls -- that
// function is defined later in this TU (psassist.inl is #included after pixelsurf.inl in
// movement.cpp), so reusing its name here would be a duplicate definition, not a reuse
static const char* PsTrajLevelName()
{
    if (!g_engine) return "";
    const char* n = nullptr;
    __try {
        void** vt = *(void***)g_engine;
        n = ((const char*(__fastcall*)(void*))vt[51])(g_engine);
    } __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "";
}

struct PsTrajSegment { Vec3 a, b; char map[128]; };
static std::mutex s_psTrajLock;
static std::vector<PsTrajSegment> s_psTrajSegments;
static std::vector<Vec3> s_psTrajWalking;   // the run being walked right now, 0 or 2 pts
static bool s_psTrajWasRiding = false;

// p within 4u (xy) of a stored line at the same height (zTol)
static bool PsTrajCovered(const Vec3& p, const char* map, const float zTol)
{
    std::lock_guard<std::mutex> l(s_psTrajLock);
    for (const PsTrajSegment& seg : s_psTrajSegments)
    {
        if (strncmp(seg.map, map, sizeof(seg.map) - 1) != 0 || fabsf(seg.a.z - p.z) >= zTol)
            continue;
        const float dx = seg.b.x - seg.a.x, dy = seg.b.y - seg.a.y;
        const float len2 = dx * dx + dy * dy;
        const float t = len2 > 0.0001f ? std::clamp(((p.x - seg.a.x) * dx + (p.y - seg.a.y) * dy) / len2, 0.f, 1.f) : 0.f;
        const float ddx = p.x - (seg.a.x + t * dx), ddy = p.y - (seg.a.y + t * dy);
        if (ddx * ddx + ddy * ddy < 16.f)
            return true;
    }
    return false;
}

static void PsRideLineStart(void* pLocal, UserCmd_t* pCmd);   // psassist.inl

static void PsTrajectoryRecord(void* pLocal, UserCmd_t* pCmd)
{
    const bool rising = s_psRidingNow && !s_psTrajWasRiding;
    s_psTrajWasRiding = s_psRidingNow;
    if (rising && g_psLinesEnable && pLocal && pCmd)
        PsRideLineStart(pLocal, pCmd);
}

// render-thread snapshot: current map's lines + the one being walked, each cut into <= 8u pieces
// so a long line does not vanish when one end goes behind the camera (finder DrawSegs, same reason)
void PS_GetTrajectory(std::vector<std::vector<Vec3>>& out)
{
    const auto cut = [](const Vec3& a, const Vec3& b)
    {
        std::vector<Vec3> pts;
        const Vec3 d = b - a;
        const int n = std::clamp((int)ceilf(d.Length() / 8.f), 1, 512);
        for (int i = 0; i <= n; i++) pts.push_back(a + d * (float(i) / float(n)));
        return pts;
    };
    std::lock_guard<std::mutex> l(s_psTrajLock);
    out.clear();
    const char* map = PsTrajLevelName();
    for (const PsTrajSegment& seg : s_psTrajSegments)
        if (strncmp(seg.map, map ? map : "", sizeof(seg.map) - 1) == 0)
            out.push_back(cut(seg.a, seg.b));
    if (s_psTrajWalking.size() == 2)
        out.push_back(cut(s_psTrajWalking[0], s_psTrajWalking[1]));
}

// menu thread. A walk in flight still lands its line when it finishes
void PS_ClearTrajectory()
{
    std::lock_guard<std::mutex> l(s_psTrajLock);
    s_psTrajSegments.clear();
}

// botox's pixel_surf, then the flags published under their old names
static void PixelSurf(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_autoAlign) PsWallDetectFallback(pLocal);

    PixelSurfImpl(pLocal, pCmd);

    s_shouldPixelSurf = s_psShouldSurf || s_psShouldUnduck;
    s_psShouldDuck    = s_psShouldSurf;
    s_psAirDuckHeld   = s_psShouldSurf;
    s_pixelSurfTicks  = s_psTicks;

    PsTrajectoryRecord(pLocal, pCmd);

    if (s_psRidingNow)
    {
        RestoreToFrame(PredCommandsPredicted() - 1);
        Vec3 n{};
        const bool touch = PsWallTouch(pLocal, n);
        s_psEv.riding = true;
        s_psEv.wall   = touch;
        s_psEv.gap    = touch ? 0.f : 999.f;
        s_psEv.travel = Vec3(s_prePredOrigin.x - s_psPinStart.x, s_prePredOrigin.y - s_psPinStart.y, 0.f).Length2D();
    }

    PsSurfSoundTick(s_psRidingNow, pLocal);
}
