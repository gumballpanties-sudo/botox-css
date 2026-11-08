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

// pixel surf vs texture bug: both pin vz, but a pixel surf's clip is a brush TOP plane the hull sweep
// crosses while pressed flush on face n (see Mechanism below), and CategorizePosition's 2u down trace from
// 1/32 off the face misses it. A texture bug is a press on a plain face: no such plane on the fall.
// PsAhead answers "about to fall on a pixel surf" from geometry, not from a re-flown fall: the catch is
// ulp-exact (PSA needs ~500 engine sims to land one), so an 8-tick trace fly never hits the top plane and
// the 20:50 version said no on every surf (log 22:10: ahead=0, AA miss=39, PS notop=54, PSA surf -383.97).
// Ray straight down the column 0.1u past the face, over the feet range the next `ticks` of fall (+ the 8.5u
// an air duck / unduck moves the feet) cover:
//   lip   = hits a top plane (nz > 0.7). From inside the wall brush too: a ray that starts solid and leaves
//           it reports the next brush's plane (brush seam = the wall's horizontal edge = the surf)
//   solid = allsolid: one plain brush the whole range = texture bug face, no surf
//   floor = the same top under the hull side (0.1u off the face) within 1u = a ledge you land on
// 3 samples along the face (now, half, full along-face travel): a lip that starts further along still reads
namespace psl { int lip = 0, floor = 0, solid = 0, none = 0, noface = 0; }
static bool PsAhead(void* pLocal, const Vec3& start, const Vec3& vel0, const Vec3& n, const Vec3& mins,
                    const Vec3& maxs, int ticks)
{
    const auto diag = [](bool r) {
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (g_debugLogEnable && sec != s_sec) {
            s_sec = sec;
            if (psl::lip || psl::floor || psl::solid || psl::none || psl::noface)
                DebugLog("PSL: lip=%d floor=%d solid=%d none=%d noface=%d", psl::lip, psl::floor, psl::solid, psl::none, psl::noface);
            psl::lip = psl::floor = psl::solid = psl::none = psl::noface = 0;
        }
        return r;
    };
    const float dt = TickInterval();
    if (dt <= 0.f) return true;
    TraceFilterWorldProps fil; fil.Setup(pLocal);

    // hull flush on the face: AA hands a face up to 1u off, PS one 0.05u off
    GameTrace_t face{};
    TraceHull(start, start - Vec3(n.x * 2.f, n.y * 2.f, 0.f), mins, maxs, MASK_PLAYERSOLID, &fil, &face);
    if (!face.startsolid && face.fraction >= 1.f) { ++psl::noface; return diag(false); }
    const Vec3 contact = face.startsolid ? start : face.endpos;

    // feet z range over the fall (lowest feet), widened by the duck shift: standing can duck up onto a
    // lip above the feet, ducked can unduck down onto one below the fall
    const float halfG = -TargetPredictZVel();
    float z = contact.z, vz = vel0.z, lowest = z;
    for (int t = 0; t < ticks; t++) { z += (vz - halfG) * dt; vz -= 2.f * halfG; lowest = fminf(lowest, z); }
    constexpr float k_duckShift = 8.5f;   // CSS air duck feet shift, (62 - 45) / 2
    const bool  standing = maxs.z - mins.z > 50.f;
    const float zTop = contact.z + mins.z + 0.5f + (standing ? k_duckShift : 0.f);
    const float zBot = lowest + mins.z - 0.5f - (standing ? 0.f : k_duckShift);

    const float d = fabsf(n.x) * maxs.x + fabsf(n.y) * maxs.y;   // AABB support toward the face
    const float vn = vel0.x * n.x + vel0.y * n.y;
    const Vec3  along(vel0.x - n.x * vn, vel0.y - n.y * vn, 0.f);
    const float travel = dt * static_cast<float>(ticks);
    bool solid = false;
    for (int s = 0; s < 3; s++)
    {
        const float f = travel * 0.5f * static_cast<float>(s);
        const float cx = contact.x + along.x * f, cy = contact.y + along.y * f;
        GameTrace_t in{};
        TraceLine(Vec3(cx - n.x * (d + 0.1f), cy - n.y * (d + 0.1f), zTop),
                  Vec3(cx - n.x * (d + 0.1f), cy - n.y * (d + 0.1f), zBot), MASK_PLAYERSOLID, &fil, &in);
        if (in.allsolid) { solid = true; continue; }
        if (in.fraction >= 1.f || in.plane.normal.z <= 0.7f) continue;
        GameTrace_t out{};
        TraceLine(Vec3(cx - n.x * (d - 0.1f), cy - n.y * (d - 0.1f), zTop),
                  Vec3(cx - n.x * (d - 0.1f), cy - n.y * (d - 0.1f), zBot), MASK_PLAYERSOLID, &fil, &out);
        if (!out.allsolid && out.fraction < 1.f && out.plane.normal.z > 0.7f && fabsf(out.endpos.z - in.endpos.z) < 1.f)
        {
            ++psl::floor;
            return diag(false);
        }
        ++psl::lip;
        return diag(true);
    }
    ++(solid ? psl::solid : psl::none);
    return diag(false);
}

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
    // notop = touching a face with no brush top plane in reach: presses skipped (texture bug, not a surf)
    enum Out { o_nowall, o_yield, o_budget, o_own, o_miss, o_latch, o_hold, o_lost, o_notop, o_count };
    int s_outs[o_count] = {};
    static void Count(Out o)
    {
        ++s_outs[o];
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (!g_debugLogEnable || sec == s_sec) return;
        s_sec = sec;
        DebugLog("PS: nowall=%d yield=%d budget=%d own=%d miss=%d latch=%d hold=%d lost=%d notop=%d", s_outs[o_nowall], s_outs[o_yield],
                 s_outs[o_budget], s_outs[o_own], s_outs[o_miss], s_outs[o_latch], s_outs[o_hold], s_outs[o_lost], s_outs[o_notop]);
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
        // not about to fall on a pixel surf = any pin a press finds is a texture bug: your keys only
        if (!PsAhead(pLocal, EntV3(pLocal, g_offOrigin), s_prePredVel, n, EntV3(pLocal, g_offMins),
                     EntV3(pLocal, g_offMaxs), as::TicksScaled(8, ipt)))
        {
            psd::Count(psd::o_notop);
            return keepsPin() ? 1 : 0;
        }
        /* strafing into the face = 450 at the best along-wall strafe yaw ( wall eats the into part ), toward the side your
           keys point along the wall ( straight in = velocity side ), only if it keeps the pin */
        constexpr float k_gainDot = 0.25f;
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
// PIXELSURF LINES -- live trajectory recording, botox movement.cpp:1020-1210, 1:1 (PSPORT
// round 3 11-04). On the rising edge of a ride (s_psRidingNow, matches botox's "now_in_surf &&
// !m_was_in_surf"): find the wall face under the feet (32-direction horizontal scan), walk it
// both ways in 16u steps tracing INTO the wall each step (stop on a face whose normal diverges
// from the first one, or a step too big to be the same wall), and at each step confirm the catch
// still holds by actually simulating a jump+duck+10u-press-into-the-wall from a point
// k_test_al off the traced face and checking vz lands near -halfgrav. Segments are deduped
// against what is already recorded (ps_line_uncovered_runs) so riding the same seam twice
// does not pile up overlapping lines.
//
// botox's is_solution_at also pokes `*(int*)(entity+0x25C) = 2`: CS:GO m_MoveType = MOVETYPE_WALK.
// Here g_predWalkSims does the same for every probe sim (set after Pred_Start), so no poke.
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

struct PsTrajSegment { std::vector<Vec3> points; char map[128]; };
static std::mutex s_psTrajLock;
static std::vector<PsTrajSegment> s_psTrajSegments;
static bool s_psTrajWasRiding = false;

static void SetEntOrigin(void* e, const Vec3& v)
{
    *reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(e) + g_offOrigin) = v;
}

static bool PsTrajCovered(const Vec3& p, const char* map, const float zTol)
{
    std::lock_guard<std::mutex> l(s_psTrajLock);
    for (const PsTrajSegment& seg : s_psTrajSegments)
    {
        if (strncmp(seg.map, map, sizeof(seg.map) - 1) != 0)
            continue;
        const std::vector<Vec3>& pts = seg.points;
        for (size_t i = 0; i < pts.size(); i++)
        {
            const Vec3& a = pts[i];
            const Vec3& b = pts[i + 1 < pts.size() ? i + 1 : i];
            if (fabsf(a.z - p.z) >= zTol)
                continue;
            // point-to-segment distance, 2D (botox point_to_line_distance)
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len2 = dx * dx + dy * dy;
            float t = len2 > 0.0001f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.f;
            t = std::clamp(t, 0.f, 1.f);
            const float cx = a.x + t * dx, cy = a.y + t * dy;
            const float ddx = p.x - cx, ddy = p.y - cy;
            if (sqrtf(ddx * ddx + ddy * ddy) < 4.f)
                return true;
        }
    }
    return false;
}

static std::vector<std::vector<Vec3>> PsTrajUncoveredRuns(const std::vector<Vec3>& pts, const char* map, const float zTol)
{
    std::vector<std::vector<Vec3>> runs;
    if (pts.empty())
        return runs;
    std::vector<char> covered(pts.size());
    for (size_t i = 0; i < pts.size(); i++)
        covered[i] = PsTrajCovered(pts[i], map, zTol) ? 1 : 0;

    std::vector<Vec3> run;
    for (size_t i = 1; i < pts.size(); i++)
    {
        if (covered[i - 1] && covered[i])
        {
            if (run.size() >= 2) runs.push_back(std::move(run));
            run.clear();
            continue;
        }
        if (run.empty()) run.push_back(pts[i - 1]);
        run.push_back(pts[i]);
    }
    if (run.size() >= 2) runs.push_back(std::move(run));
    return runs;
}

static void PsTrajectoryRecord(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_psLinesEnable || !pLocal || !pCmd)
    {
        s_psTrajWasRiding = false;
        return;
    }
    const bool nowRiding = s_psRidingNow;
    if (!(nowRiding && !s_psTrajWasRiding))
    {
        s_psTrajWasRiding = nowRiding;
        return;
    }
    s_psTrajWasRiding = nowRiding;

    const Vec3 midPos = EntV3(pLocal, g_offOrigin);
    Vec3 velocity = EntV3(pLocal, g_offVelocity); velocity.z = 0.f;

    bool surfaceFound = false;
    Vec3 wallPos = midPos, wallNormal{}, tangent{};
    constexpr int   k_angleSteps = 32;
    constexpr float k_probeDist  = 128.f;
    constexpr float k_normalZEps = 0.35f;
    constexpr float k_facingThresh = -0.1f;
    constexpr float k_twoPi = 6.28318530717958647692f;
    float bestFraction = 1.f;
    TraceFilterWorldProps fltScan; fltScan.Setup(pLocal);

    for (int i = 0; i < k_angleSteps; ++i)
    {
        const float ang = (float)i / (float)k_angleSteps * k_twoPi;
        const Vec3 dir(cosf(ang), sinf(ang), 0.f);
        GameTrace_t t{};
        TraceLine(midPos, midPos + dir * k_probeDist, MASK_PLAYERSOLID, &fltScan, &t);
        if (t.fraction >= 1.f) continue;
        if (fabsf(t.plane.normal.z) > k_normalZEps) continue;
        if (t.plane.normal.x * dir.x + t.plane.normal.y * dir.y > k_facingThresh) continue;
        if (t.fraction >= bestFraction) continue;
        bestFraction = t.fraction;
        wallPos = t.endpos; wallNormal = t.plane.normal; surfaceFound = true;
    }

    if (surfaceFound)
    {
        const float vDotN = velocity.x * wallNormal.x + velocity.y * wallNormal.y;
        tangent = Vec3(velocity.x - wallNormal.x * vDotN, velocity.y - wallNormal.y * vDotN, 0.f);
        if (tangent.Length() < 0.01f)
            tangent = Vec3(-wallNormal.y, wallNormal.x, 0.f);
        if (tangent.Length() > 0.01f)
        {
            tangent.Normalize();
            if (velocity.Length2D() > 1.f && (tangent.x * velocity.x + tangent.y * velocity.y) < 0.f)
                tangent = tangent * -1.f;
        }
        else
            surfaceFound = false;
    }

    constexpr float k_testAl = 15.97803f;
    constexpr float k_step = 16.f;
    constexpr int   k_maxSteps = 128;
    constexpr float k_probeDepth = 48.f;
    const int predFrame = PredCommandsPredicted() - 1;
    const float surfZ = midPos.z;

    const Vec3 savedOrigin = EntV3(pLocal, g_offOrigin);
    const int savedButtons = pCmd->buttons;
    const float savedFwd = pCmd->forwardmove, savedSide = pCmd->sidemove;

    const auto isSolutionAt = [&](const Vec3& candidate) -> bool
    {
        RestoreToFrame(predFrame);
        pCmd->buttons |= IN_JUMP | IN_DUCK;
        const Vec3 ang(-wallNormal.x, -wallNormal.y, 0.f);
        const float rot = DEG2RADF(VectorAngles(ang).y - pCmd->viewangles.y);
        pCmd->forwardmove = cosf(rot) * 10.f;
        pCmd->sidemove = -sinf(rot) * 10.f;
        SetEntOrigin(pLocal, candidate);
        Pred_Simulate(pLocal, pCmd);
        // botox: |vz - get_half_gravity_per_tick()| < 3, that fn returns -halfgrav. Wide on purpose:
        // the candidate is a guessed hull spot, not a replay, so an exact pin test (was 0.05) failed
        // every step and each side stopped after 1 point = 32u stub lines
        return IsTargetVz(EntV3(pLocal, g_offVelocity).z, 3.f);
    };

    const auto scanAlongWall = [&](const Vec3& dir) -> std::vector<Vec3>
    {
        std::vector<Vec3> out;
        Vec3 probeRef = wallPos, lastPt = wallPos;
        for (int i = 0; i < k_maxSteps; ++i)
        {
            probeRef = probeRef + dir * k_step;
            const Vec3 probeStart = probeRef + wallNormal * k_probeDepth;
            const Vec3 probeEnd   = probeRef - wallNormal * k_probeDepth;
            TraceFilterWorldProps f2; f2.Setup(pLocal);
            GameTrace_t t{};
            TraceLine(probeStart, probeEnd, MASK_PLAYERSOLID, &f2, &t);
            if (t.fraction >= 1.f || t.startsolid) break;
            if ((t.plane.normal.x * wallNormal.x + t.plane.normal.y * wallNormal.y + t.plane.normal.z * wallNormal.z) < 0.95f) break;

            const Vec3 delta(t.endpos.x - lastPt.x, t.endpos.y - lastPt.y, 0.f);
            if (delta.Length2D() > k_step * 1.5f) break;
            const float normalJump = fabsf((t.endpos.x - lastPt.x) * wallNormal.x + (t.endpos.y - lastPt.y) * wallNormal.y + (t.endpos.z - lastPt.z) * wallNormal.z);
            if (normalJump > 1.f) break;

            auto signOrZero = [](float v) -> float { return v < -0.001f ? -1.f : v > 0.001f ? 1.f : 0.f; };
            const Vec3 candidate(t.endpos.x + signOrZero(wallNormal.x) * k_testAl,
                                  t.endpos.y + signOrZero(wallNormal.y) * k_testAl, surfZ);
            const bool predictOk = isSolutionAt(candidate);
            if (!predictOk && i > 0) break;

            out.push_back(t.endpos);
            lastPt = t.endpos;
            probeRef = t.endpos + wallNormal * 0.5f;
            if (!predictOk) break;
        }
        return out;
    };

    const char* entryMap = PsTrajLevelName();
    if (PsTrajCovered(wallPos, entryMap, 2.f))
        DebugLog("PSL: rec skip, already drawn here");
    else
    {
        PsTrajSegment seg;
        snprintf(seg.map, sizeof(seg.map), "%s", entryMap ? entryMap : "");

        if (surfaceFound)
        {
            const std::vector<Vec3> fwd = scanAlongWall(tangent);
            const std::vector<Vec3> bwd = scanAlongWall(tangent * -1.f);

            RestoreToFrame(predFrame);
            SetEntOrigin(pLocal, savedOrigin);
            pCmd->buttons = savedButtons;
            pCmd->forwardmove = savedFwd;
            pCmd->sidemove = savedSide;

            seg.points.reserve(bwd.size() + 1 + fwd.size());
            for (auto it = bwd.rbegin(); it != bwd.rend(); ++it) seg.points.push_back(*it);
            seg.points.push_back(wallPos);
            for (const Vec3& p : fwd) seg.points.push_back(p);
        }
        else
            seg.points.push_back(wallPos);

        DebugLog("PSL: rec wall=%d pts=%d z=%.2f n=(%.2f %.2f)", surfaceFound ? 1 : 0, (int)seg.points.size(),
                 midPos.z, wallNormal.x, wallNormal.y);
        std::lock_guard<std::mutex> l(s_psTrajLock);
        if (seg.points.size() < 2)
            s_psTrajSegments.push_back(std::move(seg));
        else
            for (auto& run : PsTrajUncoveredRuns(seg.points, entryMap, 2.f))
            {
                PsTrajSegment r; r.points = std::move(run);
                snprintf(r.map, sizeof(r.map), "%s", seg.map);
                s_psTrajSegments.push_back(std::move(r));
            }
    }
}

// "full trajectory" -- botox movement.cpp:774-871 live re-scan, 1:1 math. botox runs it in render
// once per game tick; here it runs per cmd (same rate) so the traces stay on the game thread, and
// the render thread only reads the published runs. Stamp = drop stale runs when this stops running
// (blocked cmd, no prediction) instead of freezing the last scan on screen.
static std::vector<std::vector<Vec3>> s_psTrajLive;
static unsigned long long s_psTrajLiveMs = 0;

static void PsTrajectoryLive(void* pLocal)
{
    std::vector<std::vector<Vec3>> runs;
    // botox is_pixelsurf_active = m_predicted_succesful (PS latch) || m_pin_detected (ride)
    const bool on = g_psLinesEnable && g_psLinesFull && (s_shouldPixelSurf || s_psRidingNow)
                 && pLocal && EntAlive(pLocal);
    Vec3 horizDir = on ? EntV3(pLocal, g_offVelocity) : Vec3{};
    horizDir.z = 0.f;
    if (on && horizDir.Length2D() > 0.1f)
    {
        horizDir.Normalize();
        Vec3 feetPos = EntV3(pLocal, g_offOrigin);
        feetPos.z += 4.f;   // don't start in the floor

        constexpr int   k_scanDirs   = 16;
        constexpr float k_scanRadius = 32.f;
        constexpr float k_twoPi      = 6.28318530717958647692f;
        GameTrace_t initial{};
        float closestDist = FLT_MAX;
        bool foundWall = false;
        for (int i = 0; i < k_scanDirs; ++i)
        {
            const float ang = (float)i * (k_twoPi / k_scanDirs);
            const Vec3 dir(cosf(ang), sinf(ang), 0.f);
            TraceFilterWorldProps flt; flt.Setup(pLocal);
            GameTrace_t t{};
            TraceLine(feetPos, feetPos + dir * k_scanRadius, MASK_PLAYERSOLID, &flt, &t);
            if (t.fraction < 1.f && !t.startsolid)
            {
                const float d = t.fraction * k_scanRadius;
                if (d < closestDist) { closestDist = d; initial = t; foundWall = true; }
            }
        }

        if (foundWall)
        {
            const Vec3 wallNormal = initial.plane.normal;
            const float hDotN = horizDir.x * wallNormal.x + horizDir.y * wallNormal.y + horizDir.z * wallNormal.z;
            Vec3 tangent = horizDir - wallNormal * hDotN;
            if (tangent.Length() < 0.01f)
            {
                tangent = Vec3(-wallNormal.y, wallNormal.x, 0.f);
                if (tangent.x * horizDir.x + tangent.y * horizDir.y < 0.f)
                    tangent = tangent * -1.f;
            }
            if (tangent.Length() > 0.01f)
            {
                tangent.Normalize();
                std::vector<Vec3> scanPoints;
                scanPoints.reserve(48);
                scanPoints.push_back(initial.endpos);

                constexpr float k_step = 16.f;
                constexpr int   k_maxSteps = 48;
                constexpr float k_probeDepth = 48.f;
                constexpr float k_normalCohere = 0.8f;
                Vec3 probeRef = initial.endpos;
                for (int i = 0; i < k_maxSteps; ++i)
                {
                    probeRef = probeRef + tangent * k_step;
                    TraceFilterWorldProps f2; f2.Setup(pLocal);
                    GameTrace_t t{};
                    TraceLine(probeRef + wallNormal * k_probeDepth, probeRef - wallNormal * k_probeDepth, MASK_PLAYERSOLID, &f2, &t);
                    if (t.fraction >= 1.f || t.startsolid) break;
                    if (t.plane.normal.x * wallNormal.x + t.plane.normal.y * wallNormal.y + t.plane.normal.z * wallNormal.z < k_normalCohere) break;
                    scanPoints.push_back(t.endpos);
                    probeRef = t.endpos + wallNormal * 0.5f;
                }
                runs = PsTrajUncoveredRuns(scanPoints, PsTrajLevelName(), 6.f);   // locks inside: call before ours
            }
        }
    }
    std::lock_guard<std::mutex> l(s_psTrajLock);
    s_psTrajLive = std::move(runs);
    s_psTrajLiveMs = GetTickCount64();
}

// render-thread snapshot: current map's recorded segments only, each >= 2 points; `live` = the
// full-trajectory runs (empty when off or stale)
void PS_GetTrajectory(std::vector<std::vector<Vec3>>& out, std::vector<std::vector<Vec3>>& live)
{
    std::lock_guard<std::mutex> l(s_psTrajLock);
    out.clear();
    live.clear();
    const char* map = PsTrajLevelName();
    for (const PsTrajSegment& seg : s_psTrajSegments)
        if (strncmp(seg.map, map ? map : "", sizeof(seg.map) - 1) == 0 && seg.points.size() >= 2)
            out.push_back(seg.points);
    if (g_psLinesFull && GetTickCount64() - s_psTrajLiveMs < 250ull)
        live = s_psTrajLive;
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
    PsTrajectoryLive(pLocal);

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
