// autoalign.inl -- auto align = botox csgo-sdk hacks/movement/auto_align.cpp 1:1 again (11-08b), plus the
// CSS "hold pixel surfs" layer (g_aaHoldSurf, 11-08). Textually included by movement.cpp after
// pixelsurf.inl (uses its PsWishWorld / PsPressInto).
//
// Why back to botox: the 11-07 rewrite (press = PsPressInto at 450, no sim) parks the hull 0.03125 off
// the plane like any hard hit, where no seam pin exists; botox instead SEARCHES the press that pins this
// tick. The 11-07 port of botox died on one thing only: its flush pin test was the exact
// |vz - pin| < 1e-3 and a CSS clipped vz sits at pin + 1/32 (ref clip nudge). Every pin test here goes
// through IsTargetVz (movement.cpp: pin, pin +- 1/32).
//
// botox order per air wall tick: wall scan (16 x 1u hull rays, prop face = hands off) -> tb key held = hands off -> [CSS: hold
// pixel surfs] -> steering away = hands off -> leaving over the lip = hands off -> YOUR OWN MOVE RIDES =
// keep it -> fine ulp dx ladder -> coarse 10..90 pin loop -> park / unstick / keep (flush) -> contact loop
// -> along-wall keys speed branch -> fix air stucks -> AaGuard (our cmd stalls, yours
// doesn't = yours). CSS 11-08 22:45: no lip / pixel surf gate (user: keep
// the tb stick). AA: rides= / tbpin= = how many 1-tick sim pins also hold a 2nd tick (diag only).
//
// CSS mapping (disclosed):
//   g_prediction.begin/end            -> Pred_Simulate; restore_entity_to_predicted_frame -> RestoreToFrame
//   m_bounds_max_speed / real_max     -> none: Pred_Simulate is the plain engine path (same as TB here)
//   m_last_face_dz (head-face reject) -> none: no touch-list face read in this port
//   n_tick::ticks(1) > 1 warm/fail    -> 128-tick only in botox; CSS 66 tick never runs them (warm <= 2 ticks does)
//   get_base_velocity()               -> 0 (trigger_push / conveyor only)
//   m_ladder_bug_key / fireman ladder -> MOVETYPE_LADDER gate
//   pixel jump (aa_pj_lip)            -> not ported: g_autoAlignPixelJump was deleted with its menu row 11-07
//   hull heights 54 / 72              -> CSS 45 / 62 (Fine's head pin bound)
//
// "hold pixel surfs" (g_aaHoldSurf): one START pin step (we::PinStep, the wall stuck window -- a single
// pinned tick is enough) latches the face. From then on every air tick runs the hold search into that
// face (AaHoldSearch: last winner, your cmd, straight-in sizes, small yaw offsets, stance flip; 6-tick
// look-ahead, most pinned ticks wins) the way texture bug re-proves its press every tick; nothing pins on
// a pinned START = resend last / straight press in, on an unpinned one = plain botox auto align. Let go
// (we::HoldLatch): ground, duck-key bail (PsCmdStart), 2 unpinned STARTs, keys steering off the face 2
// ticks running, face gone, texture bug key, edge bug replaying (its pin, not a surf).

namespace aa
{
    constexpr float k_eps   = 0.03125f;
    constexpr float k_touch = k_eps * 1.3f;
    constexpr int   k_rungs = 48;
    constexpr int   k_keys  = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
    constexpr float k_pinEps = 1e-3f;   // sim pins: same engine, same floats -- only the nudge to swallow

    int   s_parkMargin = 0, s_parkCmd = -1;
    float s_parkFrom = 0.f, s_parkTo = 0.f, s_parkWx = 0.f, s_parkWy = 0.f;
    bool  s_parkOff = false;   // server pinned you stuck right after a park: park off till ground

    // botox aa_outs + the hold layer's: hold = the written press pins all k_holdH ticks ahead, short =
    // fewer, hpress = none pins, pressed in anyway. mis = the sent hold said "pins next tick" and START
    // did not; blind = said nothing pins, START did
    enum Out { o_near, o_face, o_tb, o_away, o_angle, o_lip, o_pin, o_fine, o_park, o_unstick, o_keep, o_touch, o_miss,
               o_slant, o_poff, o_stuckfix, o_hold, o_hold1, o_hpress, o_mis, o_blind, o_rides, o_tbpin, o_prop, o_guard, o_count };
    int s_outs[o_count] = {};

    struct Budget
    {
        LARGE_INTEGER t0{};
        double ms = 0.0;
        void Start(float share)
        {
            QueryPerformanceCounter(&t0);
            ms = (double)TickInterval() * 1000.0 * share;
        }
        bool Expired() const
        {
            static LARGE_INTEGER f = [] { LARGE_INTEGER q{}; QueryPerformanceFrequency(&q); return q; }();
            LARGE_INTEGER n{}; QueryPerformanceCounter(&n);
            return SimsLeft() <= 0 || double(n.QuadPart - t0.QuadPart) * 1000.0 / double(f.QuadPart) >= ms;
        }
    };

    void WorldWish(const UserCmd_t& c, float& x, float& y)
    {
        const float a = DEG2RADF(c.viewangles.y);
        x = c.forwardmove * cosf(a) + c.sidemove * sinf(a);
        y = c.forwardmove * sinf(a) - c.sidemove * cosf(a);
    }

    void FlushDrop() { s_parkMargin = 0; s_parkCmd = -1; }

    void Restore() { RestoreToFrame(PredCommandsPredicted() - 1); }

    // botox n_tb::move_fix_to_yaw: fwd/side meant at world yaw, rotated onto the cmd's view yaw
    void MoveFixToYaw(float yaw, UserCmd_t& c)
    {
        const float y = yaw >= 0.f ? yaw : yaw + 360.f;
        const float v = c.viewangles.y >= 0.f ? c.viewangles.y : c.viewangles.y + 360.f;
        float d = y <= v ? 360.f - fabsf(y - v) : fabsf(v - y);
        d = 360.f - d;
        const float f = c.forwardmove, s = c.sidemove;
        c.forwardmove = cosf(DEG2RADF(d)) * f + cosf(DEG2RADF(d + 90.f)) * s;
        c.sidemove    = sinf(DEG2RADF(d)) * f + sinf(DEG2RADF(d + 90.f)) * s;
    }

    // botox n_tb::hull_plane_dist: a world brush plane's own dist, else the hit point form
    float HullPlaneDist(const GameTrace_t& tr, const Vec3& mins, const Vec3& maxs)
    {
        const Vec3& p = tr.plane.normal;
        if (!(tr.dispFlags & DISPSURF_SURFACE) && tr.hitbox == 0)
            return tr.plane.dist;
        return tr.endpos.x * p.x + tr.endpos.y * p.y + tr.endpos.z * p.z + fminf(p.x * mins.x, p.x * maxs.x)
             + fminf(p.y * mins.y, p.y * maxs.y) + fminf(p.z * mins.z, p.z * maxs.z) - 0.03125f;
    }

    struct Flush
    {
        struct End { bool ok = false, cut = false, pin = false, stuck = false, ground = false, nextStuck = false; Vec3 org{}, vel{}; };

        void* local = nullptr;
        const Budget* budget = nullptr;
        UserCmd_t in{};
        Vec3 n{}, org{}, vel{};
        int sims = 0;
        float gap = 0.f, ulp = 1e-7f, ipt = 0.f, k_acc = 0.f, pin = 0.f, xy0 = 0.f, yawIn = 0.f, vOld = 0.f;
        bool duck = false, ducked0 = false, rising = false, userDies = false, userPins = false, userStuck = false;

        bool Init(void* pLocal, const UserCmd_t* cmd, const Vec3& wallN, float planeDist, const Vec3& maxs, const Budget& b)
        {
            local = pLocal; budget = &b;
            ipt = TickInterval();
            if (!local || ipt <= 0.f) return false;
            in = *cmd;
            Restore();
            org     = EntV3(local, g_offOrigin);
            vel     = EntV3(local, g_offVelocity);
            ducked0 = EntDucking(local);
            duck    = (in.buttons & IN_DUCK) != 0;
            pin     = TargetPredictZVel();
            rising  = vel.z + pin > 1e-3f;
            xy0     = vel.Length2D();
            n       = Vec3(wallN.x, wallN.y, 0.f);
            gap     = org.x * n.x + org.y * n.y - planeDist - (fabsf(n.x) * maxs.x + fabsf(n.y) * maxs.y);
            yawIn   = RAD2DEGF(atan2f(-n.y, -n.x));
            vOld    = -(vel.x * n.x + vel.y * n.y);
            k_acc   = CvarFloat("sv_airaccelerate", 10.f) * ipt * AsSurfaceFriction(local);
            // the circle ray is 1u: a gap off that = a plane that isn't this hull's face
            if (k_acc <= 0.f || gap < -0.1f || gap > 1.5f) return false;
            // slanted wall: sim vs server split past ulps -- the old pushes park 0.03125 off there like the engine
            if (fminf(fabsf(n.x), fabsf(n.y)) > 1e-3f) { s_outs[o_slant]++; return false; }
            const float coord = fmaxf(fabsf(org.x * n.x), fabsf(org.y * n.y));
            int ex = 0;
            frexpf(coord, &ex);
            if (coord > 0.f) ulp = ldexpf(1.f, ex - 24);

            // a server that won't take the planned park -> keep margin ulps. any stuck arrival on a wall
            if (!s_parkOff && xy0 < 1.f && IsTargetVz(vel.z, 0.01f)) { s_parkOff = true; s_outs[o_poff]++; }
            if (s_parkCmd >= 0 && cmd->command_number == s_parkCmd + 1)
            {
                if (s_parkFrom - gap < 0.5f * (s_parkFrom - s_parkTo) || (s_parkTo < 0.5f * k_eps && gap > 0.9f * k_eps))
                {
                    s_parkMargin = s_parkMargin ? s_parkMargin * 2 : 2;
                    if (g_debugLogEnable)
                        DebugLog("AA: park refused from=%.6f plan=%.6f got=%.6f margin=%d", s_parkFrom, s_parkTo, gap, s_parkMargin);
                }
                else
                    s_parkMargin = 0;
            }
            s_parkCmd = -1;

            const End e = Sim(in);
            userDies  = e.ok && Dies(e);
            userPins  = e.pin;
            // slanted wall, hull ulps off: your move zeroes xy + vz (air stuck, rise killed)
            userStuck = e.ok && e.stuck;
            if (userStuck && g_debugLogEnable)
                DebugLog("AA: stuck gap=%.6f n=%.2f %.2f xy=%.1f vz=%.2f", gap, n.x, n.y, xy0, vel.z);
            return !e.cut;
        }

        bool Dies(const End& e) const { return xy0 > 50.f && e.vel.Length2D() < 0.2f * xy0; }

        End Sim(UserCmd_t c, const UserCmd_t* next = nullptr)
        {
            End e;
            if (sims > 0 && budget->Expired()) { e.cut = true; return e; }
            Restore();
            Pred_Simulate(local, &c);
            ++sims;
            e.ok     = EntMoveType(local) == MOVETYPE_WALK;
            e.ground = (EntI(local, g_offFlags) & FL_ONGROUND) != 0;
            e.org    = EntV3(local, g_offOrigin);
            e.vel    = EntV3(local, g_offVelocity);
            const bool pinned = e.ok && !e.ground && IsTargetVz(e.vel.z, k_pinEps);
            e.pin   = pinned && e.vel.Length2D() >= 1.f && vel.z + pin < 0.f;
            e.stuck = pinned && e.vel.Length2D() < 1.f;
            // + your move one tick on: park ends clean but the next move off a slanted wall's ulp gap zeroes
            if (next && e.ok && !e.ground && !budget->Expired())
            {
                UserCmd_t c2 = *next;
                Pred_Simulate(local, &c2);
                ++sims;
                const Vec3 v2 = EntV3(local, g_offVelocity);
                e.nextStuck = EntMoveType(local) == MOVETYPE_WALK && !(EntI(local, g_offFlags) & FL_ONGROUND)
                           && IsTargetVz(v2.z, k_pinEps) && v2.Length2D() < 1.f;
            }
            return e;
        }

        // into-wall speed vOld -> vT in one AirAccelerate, straight at the wall (< 0 = straight out)
        UserCmd_t Press(float vT) const
        {
            const float crop = (duck || ducked0) ? 0.34f : 1.f;
            float w = vT >= vOld ? fmaxf((vT - vOld) / k_acc, vT) : -fmaxf((vOld - vT) / k_acc, -vT);
            w = std::clamp(w / crop, -450.f, 450.f);
            UserCmd_t c = in;
            c.forwardmove = fabsf(w);
            c.sidemove    = 0.f;
            MoveFixToYaw(w >= 0.f ? yawIn : yawIn + 180.f, c);
            c.buttons &= ~k_keys;
            return c;
        }

        static void Commit(UserCmd_t* cmd, const UserCmd_t& c)
        {
            cmd->forwardmove = c.forwardmove;
            cmd->sidemove    = c.sidemove;
            cmd->buttons     = c.buttons;
        }

        // touching + falling: dx ladder from the smallest crossing press up to the head pin bound, ulp
        // steps, send the pinning run's middle. rising pins = head bounces off your rise, tb's call
        bool Fine(UserCmd_t* cmd)
        {
            if (gap > k_touch || rising) return false;
            const float g  = fmaxf(gap, 1e-7f);
            const float dz = fabsf((vel.z + pin) * ipt) + 1e-4f;
            const float hp = (duck || ducked0) ? cssm::k_hullDuck : cssm::k_hullStand;
            const float hi = fmaxf((k_eps - g) * dz / (hp + k_eps), g * 1.5f);
            float run[k_rungs];
            int runN = 0, k = 0;
            for (float dx = g * 1.0005f + 1e-7f; dx <= hi && k < k_rungs; dx = fmaxf(dx * 1.4f, dx + ulp), ++k)
            {
                const End e = Sim(Press(dx / ipt));
                if (e.cut) break;
                if (e.pin && (userDies || !Dies(e))) run[runN++] = dx;
                else if (runN) break;
            }
            if (!runN) return false;
            Commit(cmd, Press(run[runN / 2] / ipt));
            return true;
        }

        // park: tightest press that ends short of the plane with no hit (a hit clips v_in, parks
        // 0.03125), never under margin ulps
        bool Park(UserCmd_t* cmd)
        {
            if (gap <= 0.f || s_parkMargin > 16 || s_parkOff) return false;
            static const float k_keep[] = { 0.000001f, 0.0001f, 0.002f, 0.05f, 0.3f };
            const float floorGap = static_cast<float>((std::max)(s_parkMargin, 1)) * ulp;
            float lastEnd = -1.f;
            for (const float keep : k_keep)
            {
                const float end = fmaxf(gap * keep, floorGap);
                if (end >= gap) break;
                if (end == lastEnd) continue;
                lastEnd = end;
                const UserCmd_t c = Press((gap - end) / ipt);
                const End e = Sim(c, &in);
                if (e.cut) break;
                if (!e.ok || e.ground || e.stuck || e.nextStuck) continue;
                const float g1 = (e.org.x - org.x) * n.x + (e.org.y - org.y) * n.y + gap;
                const bool touched = -(e.vel.x * n.x + e.vel.y * n.y) <= 1e-3f;
                if (g1 > 0.f && g1 < gap && !touched && g1 >= floorGap * 0.5f && (userDies || !Dies(e)))
                {
                    Commit(cmd, c);
                    s_parkCmd = cmd->command_number; s_parkFrom = gap; s_parkTo = g1;
                    WorldWish(c, s_parkWx, s_parkWy);
                    return true;
                }
            }
            return false;
        }

        // your move dead-stops while flush (next brush's lip sticks out under 0.03125): step off to the
        // smallest gap that keeps the slide
        bool Unstick(UserCmd_t* cmd)
        {
            if ((!userDies && !userStuck) || gap > k_touch) return false;
            for (const float to : { k_eps, 0.125f, 0.5f })
            {
                const UserCmd_t c = Press(-(to - gap) / ipt);
                const End e = Sim(c);
                if (e.cut) break;
                if (e.ok && !e.stuck && !Dies(e)) { Commit(cmd, c); return true; }
            }
            return false;
        }

    };

    float Difference(float a, float b) { return fmaxf(fabsf(a), fabsf(b)) - fminf(fabsf(a), fabsf(b)); }

    // ---- hold pixel surfs (11-08 v2..v6) ----
    // texture bug's rule: press INTO the face, keep doing what works. Each candidate flies k_holdH ticks
    // repeating its cmd; most pinned ticks wins, ties go to the EARLIER candidate (order = how much we
    // trust it), never to speed: along-face wishes gain speed but add nothing into the face and the clip
    // nudge walks the hull out of the band over dozens of ticks. A held PS ends at a FIXED z (each pinned
    // tick lifts the hull ~0.0005u); ride length = how deep the catch was.
    constexpr int k_holdH = 6;
    // sliding, not stuck. A wedged hull reads xy ~1/32 (the clip nudge); a slow ride is still a ride
    constexpr float k_holdMinXY = we::k_rideMinXY;
    // candidate kinds: last = what was SENT last tick (reality proved it when START pinned), own = your
    // cmd, press = PsPressInto (your wish turned into the face), wish = (yaw off "into" toward your
    // velocity along the face, size); flip = stance flipped
    struct HoldCand { int kind; float off; float move; int flip; };
    enum { hk_last, hk_own, hk_press, hk_wish };
    constexpr HoldCand k_holdCands[] = {
        { hk_last, 0.f, 0.f, 0 },
        { hk_own,  0.f, 0.f, 0 },
        { hk_press, 0.f, 0.f, 0 },
        { hk_wish, 0.f, 450.f, 0 }, { hk_wish, 0.f, 30.f, 0 }, { hk_wish, 0.f, 10.f, 0 },
        { hk_wish, 0.f, 3.f, 0 },   { hk_wish, 0.f, 1.f, 0 },  { hk_wish, 0.f, 0.25f, 0 },
        { hk_wish, 10.f, 450.f, 0 }, { hk_wish, 25.f, 450.f, 0 }, { hk_wish, 45.f, 450.f, 0 },
        { hk_wish, -10.f, 450.f, 0 }, { hk_wish, -25.f, 450.f, 0 },
        { hk_wish, 10.f, 10.f, 0 },  { hk_wish, 25.f, 10.f, 0 },
        { hk_wish, 0.f, 0.f, 0 },    // no wish
        { hk_own,  0.f, 0.f, 1 },
        { hk_wish, 0.f, 450.f, 1 }, { hk_wish, 0.f, 10.f, 1 },
    };

    we::HoldLatch s_hold;
    int   s_holdTicks = 0;           // air ticks this latch has lived (release log)
    int   s_holdSims  = 0;           // hold search sims this second (diag)
    bool  s_prevHave = false, s_prevGround = true;
    float s_prevVz = 0.f;
    // last SENT hold cmd. rel = yaw off "into" with the along-face side baked in
    bool  s_lastOk = false; int s_lastKind = hk_wish; float s_lastRel = 0.f, s_lastMove = 0.f; int s_lastFlip = 0;
    bool  s_expect = false, s_expectPin = false;   // last tick sent a hold, and whether it said "pins next"

    static void HoldOff() { s_hold.Off(); s_holdTicks = 0; s_lastOk = false; s_expect = false; }
    static void HoldRelease(const char* why)
    {
        if (s_hold.on || s_holdTicks)
            DebugLog("AA: hold release why=%s ticks=%d z=%.3f vz=%.2f xy=%.1f", why, s_holdTicks,
                     s_prePredOrigin.z, s_prePredVel.z, s_prePredVel.Length2D());
        HoldOff();
    }
}

// botox auto_align_sent: end of the cmd. A later feature (fireman / wc ...) rewrote our park = no server
// verdict, never grow the margin off it. Ground re-arms the park. Called from movement.cpp after the
// pipeline's last mover.
static void AutoAlignSent(void* pLocal, const UserCmd_t* cmd)
{
    if (pLocal && (EntI(pLocal, g_offFlags) & FL_ONGROUND)) aa::s_parkOff = false;
    if (!cmd || aa::s_parkCmd != cmd->command_number) return;
    float x, y;
    aa::WorldWish(*cmd, x, y);
    if (fabsf(x - aa::s_parkWx) + fabsf(y - aa::s_parkWy) > 1e-3f)
        aa::s_parkCmd = -1;
}

// the cmd a hold candidate writes over `base` against face n. rel = yaw off "into" (side already applied)
static UserCmd_t AaHoldCmd(const UserCmd_t& base, int kind, float rel, float move, int flip, const Vec3& n)
{
    UserCmd_t c = base;
    if (kind == aa::hk_press)
        c = PsPressInto(base, n);
    else if (kind == aa::hk_wish)
    {
        const float intoYaw = RAD2DEGF(atan2f(-n.y, -n.x));
        we::WishToCmd(we::NormYaw(intoYaw + rel), move, c.viewangles.y, c.forwardmove, c.sidemove);
        c.upmove = 0.f;
        c.buttons &= ~aa::k_keys;
    }
    if (flip) c.buttons ^= IN_DUCK;
    c.buttons &= ~IN_JUMP;
    return c;
}

// fly `c` up to k_holdH ticks from START, repeating it; consecutive pin steps from the first tick
static int AaHoldSim(void* pLocal, int nBase, const UserCmd_t& c, float h)
{
    using namespace aa;
    RestoreToFrame(nBase - 1);
    int pins = 0;
    float prevVz = s_prePredVel.z;
    for (int i = 0; i < k_holdH; ++i)
    {
        if (SimsLeft() <= 0 || TbBudgetExpired()) break;
        UserCmd_t t = c;
        t.buttons &= ~(IN_ATTACK | IN_ATTACK2);
        t.hasbeenpredicted = false;
        Pred_Simulate(pLocal, &t);
        ++s_holdSims;
        if (EntMoveType(pLocal) != MOVETYPE_WALK || (EntI(pLocal, g_offFlags) & FL_ONGROUND)) break;
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        if (!we::PinStep(false, prevVz, false, v.z, h) || v.Length2D() < k_holdMinXY) break;   // stuck != ride
        ++pins;
        prevVz = v.z;
    }
    RestoreToFrame(nBase - 1);
    return pins;
}

// writes the best hold into cmd. Returns its pinned ticks (0 = nothing pins, cmd untouched)
static int AaHoldSearch(void* pLocal, UserCmd_t* cmd, const Vec3& n, float h, int& pickIdx)
{
    using namespace aa;
    const int nBase = PredCommandsPredicted();
    const Vec3& v0 = s_prePredVel;
    // "into" = -n; turned +90 deg it is the tangent (n.y, -n.x): sign = which way along it you move
    const float along = v0.x * n.y - v0.y * n.x;
    const float sign = along >= 0.f ? 1.f : -1.f;

    int best = 0; UserCmd_t bestCmd{}; pickIdx = -1;
    int bKind = hk_wish; float bRel = 0.f, bMove = 0.f; int bFlip = 0;
    for (int i = 0; i < (int)std::size(k_holdCands); ++i)
    {
        const HoldCand& k = k_holdCands[i];
        int kind = k.kind; float rel = sign * k.off, move = k.move; int flip = k.flip;
        if (kind == hk_last)
        {
            if (!s_lastOk) continue;
            kind = s_lastKind; rel = s_lastRel; move = s_lastMove; flip = s_lastFlip;
        }
        const UserCmd_t c = AaHoldCmd(*cmd, kind, rel, move, flip, n);
        const int pins = AaHoldSim(pLocal, nBase, c, h);
        if (pins > best)
        {
            best = pins; bestCmd = c; pickIdx = i;
            bKind = kind; bRel = rel; bMove = move; bFlip = flip;
        }
        if (best >= k_holdH) break;
        if (SimsLeft() <= 0 || TbBudgetExpired()) break;
    }
    if (best < 1) return 0;
    s_lastOk = true; s_lastKind = bKind; s_lastRel = bRel; s_lastMove = bMove; s_lastFlip = bFlip;
    cmd->forwardmove = bestCmd.forwardmove;
    cmd->sidemove    = bestCmd.sidemove;
    cmd->upmove      = bestCmd.upmove;
    cmd->buttons     = bestCmd.buttons;
    return best;
}

// pixel surf vs texture bug, engine-proven: c1 pins its tick; does SOME hold press keep pinning the next
// 2 ticks? Log 22:43: surf z -383.98 rode 28 ticks (hold pick 0) though c1 itself failed a 2nd tick, tbs at
// z -147 / -171 / -173 pins=0/6 under every candidate then fall (clip nudge, tb RULE). Ends with sim state
// = c1's tick (callers read it like botox). Budget out = no proof = no stick
static bool AaRideOn(void* pLocal, const UserCmd_t& c1, const UserCmd_t& user, const Vec3& n)
{
    using namespace aa;
    const int nBase = PredCommandsPredicted();
    const float along = s_prePredVel.x * n.y - s_prePredVel.y * n.x;
    const float sign = along >= 0.f ? 1.f : -1.f;
    const auto step = [&](const UserCmd_t& c) {
        UserCmd_t t = c;
        t.buttons &= ~(IN_ATTACK | IN_ATTACK2);
        t.hasbeenpredicted = false;
        Pred_Simulate(pLocal, &t);
        ++s_holdSims;
        if (EntMoveType(pLocal) != MOVETYPE_WALK || (EntI(pLocal, g_offFlags) & FL_ONGROUND)) return false;
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return IsTargetVz(v.z, k_pinEps) && v.Length2D() >= k_holdMinXY;
    };
    // kind -1 = c1 again; rest = k_holdCands' first picks (hold log: pick 0..2 carry every ride)
    static constexpr HoldCand k_ride[] = {
        { -1, 0.f, 0.f, 0 }, { hk_own, 0.f, 0.f, 0 }, { hk_press, 0.f, 0.f, 0 },
        { hk_wish, 0.f, 450.f, 0 }, { hk_wish, 0.f, 30.f, 0 }, { hk_wish, 0.f, 10.f, 0 },
        { hk_wish, 0.f, 0.f, 0 }, { hk_own, 0.f, 0.f, 1 },
    };
    bool ok = false;
    for (const HoldCand& k : k_ride)
    {
        if (ok || SimsLeft() < 3 || TbBudgetExpired()) break;
        const UserCmd_t c = k.kind < 0 ? c1 : AaHoldCmd(user, k.kind, sign * k.off, k.move, k.flip, n);
        RestoreToFrame(nBase - 1);
        ok = step(c1) && step(c) && step(c);
    }
    RestoreToFrame(nBase - 1);
    step(c1);
    return ok;
}

// true = the hold owns this cmd (written), false = plain auto align decides
static bool AaHoldTick(void* pLocal, UserCmd_t* cmd, const Vec3& n, float h, bool pinNow, bool steerAway)
{
    using namespace aa;
    if (!g_aaHoldSurf)
    {
        if (s_hold.on || s_holdTicks) HoldRelease("option off");
        return false;
    }
    // an edgebug ends on -h too: while edge bug replays its plan (s_ebDetected) that pin is its, not a surf
    if (s_ebDetected) { HoldRelease("edgebug"); return false; }

    const bool was = s_hold.on;
    const we::HoldLatch::Why why = s_hold.Step(false, s_psBail, pinNow, steerAway);
    if (why != we::HoldLatch::w_none)
        HoldRelease(why == we::HoldLatch::w_bail ? "bail" : why == we::HoldLatch::w_lost ? "lost" : "away");
    else if (s_hold.on && !was)
        DebugLog("AA: hold latch z=%.3f vz=%.2f xy=%.1f duck=%d", s_prePredOrigin.z, s_prePredVel.z,
                 s_prePredVel.Length2D(), (s_prePredFlags & FL_DUCKING) ? 1 : 0);
    // sim vs reality: did last tick's sent hold do what its look-ahead said?
    if (s_expect && s_hold.on)
    {
        if (s_expectPin && !pinNow)
        {
            s_outs[o_mis]++;
            DebugLog("AA: hold mispredict z=%.3f vz=%.2f xy=%.1f t=%d", s_prePredOrigin.z, s_prePredVel.z,
                     s_prePredVel.Length2D(), s_holdTicks);
        }
        else if (!s_expectPin && pinNow)
            s_outs[o_blind]++;
    }
    s_expect = false;
    if (!s_hold.on) return false;

    ++s_holdTicks;
    int pick = -1;
    const int r = AaHoldSearch(pLocal, cmd, n, h, pick);
    // short = the best hold runs out inside the look-ahead. s = hull position along the face (tangent
    // n.y, -n.x): where a lip ends
    if (r < k_holdH && g_debugLogEnable)
        DebugLog("AA: hold short pins=%d/%d pick=%d pin=%d z=%.3f vz=%.2f xy=%.1f s=%.2f t=%d",
                 r, k_holdH, pick, pinNow ? 1 : 0, s_prePredOrigin.z, s_prePredVel.z, s_prePredVel.Length2D(),
                 s_prePredOrigin.x * n.y - s_prePredOrigin.y * n.x, s_holdTicks);
    s_aaHolding = true;
    s_expect = true; s_expectPin = r >= 1;
    if (r >= k_holdH) { s_outs[o_hold]++;  return true; }
    if (r >= 1)       { s_outs[o_hold1]++; return true; }
    // pinned and nothing the look-ahead flies keeps it = the pin dies next tick (texture bug, or a surf's
    // end): no blind re-press (11-08 22:45: was hpress, = tbpin 1:1 in log). Plain auto align decides
    s_aaHolding = false; s_expect = false;
    return false;
}

static bool s_aaWedge = false;   // this tick's wall can wedge the AABB (slanted / prop): only then AaGuard runs

static void AutoAlignBotox(void* pLocal, UserCmd_t* cmd)
{
    using namespace aa;
    s_aaWallDetected = false;
    s_aaHolding = false;

    if (!g_autoAlign || !EntAlive(pLocal)) { HoldRelease("off"); s_prevHave = false; return; }   // plain on/off, no bind

    // the START pin step, tracked every cmd (ground too): one pinned START is the hold latch trigger.
    // A SURF pin keeps its speed; a zeroed one (xy 0) is an air stuck -- a corner wedged in a slanted face
    const bool  ground = (s_prePredFlags & FL_ONGROUND) != 0;
    const float h      = -TargetPredictZVel();
    const bool  pinNow = s_prevHave && we::PinStep(s_prevGround, s_prevVz, ground, s_prePredVel.z, h)
                      && s_prePredVel.Length2D() >= k_holdMinXY;
    s_prevHave = true; s_prevGround = ground; s_prevVz = s_prePredVel.z;

    // the PS assist's seam press reproduces PixelFinder's probe -- AA must not overwrite it
    if (s_psAssistPressIn) { HoldRelease("psassist"); return; }
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP) { HoldRelease("movetype"); return; }
    if (ground) { HoldRelease("ground"); return; }
    if (g_airStuck && MovementKeyOK(g_airStuckKey, g_airStuckMode) && s_airStuckHit) { HoldRelease("airstuck"); return; }

    // per-second diag, botox aa_outs + hold
    const auto Diag = [&]() {
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (!g_debugLogEnable || sec == s_sec) return;
        s_sec = sec;
        DebugLog("AA: near=%d face=%d tb=%d away=%d angle=%d lip=%d pin=%d fine=%d park=%d unstick=%d keep=%d touch=%d miss=%d slant=%d poff=%d stuckfix=%d"
                 " prop=%d guard=%d | hold=%d short=%d hpress=%d mis=%d blind=%d hsims=%d rides=%d tbpin=%d",
                 s_outs[o_near], s_outs[o_face], s_outs[o_tb], s_outs[o_away], s_outs[o_angle], s_outs[o_lip],
                 s_outs[o_pin], s_outs[o_fine], s_outs[o_park], s_outs[o_unstick], s_outs[o_keep], s_outs[o_touch], s_outs[o_miss],
                 s_outs[o_slant], s_outs[o_poff], s_outs[o_stuckfix], s_outs[o_prop], s_outs[o_guard],
                 s_outs[o_hold], s_outs[o_hold1], s_outs[o_hpress], s_outs[o_mis], s_outs[o_blind], s_holdSims, s_outs[o_rides], s_outs[o_tbpin]);
        for (int& o : s_outs) o = 0;
        s_holdSims = 0;
    };

    Restore();   // START hull (duck state) and origin: features before this one may have simmed
    const float maxRadians = 3.14159265358979323846f * 2.f;
    const float step = maxRadians / 16.f;
    const Vec3 startPos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    // what ProcessMovement hits: world-only skipped props + brush ents, the sims don't
    TraceFilterWorldProps fil; fil.Setup(pLocal);

    static float startCircle = 0.f;
    static float warmFwd = 0.f, warmSide = 0.f;
    static int warmTick = -1;

    int aaSims = 0;
    constexpr int AA_MAX_PREDICTIONS = 24;
    Budget budget; budget.Start(0.25f);
    const auto spent = [&]() { return aaSims >= AA_MAX_PREDICTIONS || budget.Expired(); };
    const int tickNow = g_globalVars ? g_globalVars->tickcount : 0;
    GameTrace_t trace{};
    Vec3 saveStartPos{};
    float savedCos = 0.f, savedSin = 0.f;

    {
        GameTrace_t nearTrace{};
        TraceHull(startPos, startPos, mins - Vec3(1.f, 1.f, 0.f), maxs + Vec3(1.f, 1.f, 0.f), MASK_PLAYERSOLID, &fil, &nearTrace);
        if (!nearTrace.DidHit())
        {
            startCircle = 0.f; warmTick = -1; FlushDrop(); HoldRelease("noface"); s_outs[o_near]++; Diag();
            return;
        }
    }

    // all 16 from last tick's wall angle, wrapping: a corner's lower angle used to cost a tick + the warm move.
    // first brush wall wins, no early out: a prop face anywhere in the ring = hands off below
    bool propNear = false;
    const float circle0 = startCircle;   // the ring walks from here even after the wall moves startCircle
    for (int s = 0; s < 16; s++)
    {
        const float a = fmodf(circle0 + step * static_cast<float>(s), maxRadians);
        const float ca = cosf(a), sa = sinf(a);
        GameTrace_t tr{};
        TraceHull(startPos, Vec3(startPos.x + ca, startPos.y + sa, startPos.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.fraction == 1.f) continue;
        if (MV_TraceIsProp(tr))
        {
            propNear |= tr.startsolid || fabsf(tr.plane.normal.z) < 0.7f;
            continue;
        }
        if (!s_aaWallDetected && !tr.startsolid && tr.plane.normal.z == 0.f)   // startsolid = zero normal, no wall dir
        {
            s_aaWallDetected = true;
            trace = tr;
            startCircle = a;
            saveStartPos = startPos;
            savedCos = ca * 64.f; savedSin = sa * 64.f;
        }
    }

    // prop = .phy convex at any yaw (vphysics trace): a press meets it on a corner and can wedge. never the wall,
    // no hand off (csgo 11-14 log: hand off cost pixel jumps), only wedge risk = AaGuard runs.
    // ponytail: filter has no ShouldCollide, a walk-through debris prop also counts (guard sims only)
    if (propNear) s_outs[o_prop]++;

    if (!s_aaWallDetected)
    {
        startCircle = 0.f; warmTick = -1; FlushDrop(); HoldRelease("noface"); s_outs[o_face]++; Diag();
        return;
    }
    // slanted face = AABB corner contact, prop = corner too: an xy 0 pin there is a wedge, not a head-on surf
    s_aaWedge = propNear || fminf(fabsf(trace.plane.normal.x), fabsf(trace.plane.normal.y)) > 1e-3f;

    // tb key held: tb's ladder / park run after us with flips + lookahead. wall_detected stays set for pixel_surf
    if (On(g_textureBug, g_textureBugKey, g_textureBugMode))
    {
        warmTick = -1; FlushDrop(); HoldRelease("tb"); s_outs[o_tb]++; Diag();
        return;
    }

    const Vec3 wallN(trace.plane.normal.x, trace.plane.normal.y, 0.f);   // normal.z == 0: unit xy
    const float userFwd = cmd->forwardmove, userSide = cmd->sidemove;   // given back when no pixel surf pins
    const bool steerAway = TB_SteeringAwayFromWall(cmd, trace.plane.normal, s_rawFwd, s_rawSide, 0.3f);
    const int userButtons = cmd->buttons;
    const UserCmd_t userCmd = *cmd;
    // 11-08: PsAhead lip gates (trace fly, column ray) read no on every surf (logs 22:10 ahead=0, 22:30
    // PSL lip=0), deleted. Surf vs tb = AaRideOn on every pin a press finds (rides below)
    if (AaHoldTick(pLocal, cmd, wallN, h, pinNow, steerAway))
    {
        warmTick = -1; FlushDrop(); Diag();
        return;
    }

    if (steerAway)
    {
        startCircle = 0.f; s_outs[o_away]++; Diag();
        return;
    }

    bool angleCheck = false;
    const Vec3 angles(trace.plane.normal.x * -0.005f, trace.plane.normal.y * -0.005f, 0.f);
    {
        GameTrace_t t228{};
        TraceHull(startPos, startPos + angles, mins, maxs, MASK_PLAYERSOLID, &fil, &t228);
        if (t228.fraction == 1.f)
        {
            Vec3 toWall = VectorAngles(angles); ClampAnglesMv(toWall);
            Vec3 velo = EntV3(pLocal, g_offVelocity); velo.z = 0.f;
            if (velo.Length2D() > 0.f)
            {
                Vec3 veloAng = VectorAngles(velo); ClampAnglesMv(veloAng);
                const float d = remainderf(veloAng.y - toWall.y, 360.f);
                if (fabsf(d) > 92.5f) angleCheck = true;
            }
        }
    }
    if (angleCheck) { s_outs[o_angle]++; Diag(); return; }

    // ebanat 0x117481: a 15 / 30 push that carries you over the top in 3 ticks (> 5u past the plane,
    // rising > 2u) = your keys, no align. Sims only if free flight gets there
    {
        const float ipt = TickInterval();
        const float cap = CvarFloat("sv_air_max_wishspeed", 30.f);
        const Vec3& n = trace.plane.normal;
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        const float vIn = -(v.x * n.x + v.y * n.y);
        if (ipt > 0.f && 3.f * ipt * v.z > 2.f && 3.f * ipt * fmaxf(vIn, cap) > 5.f)
        {
            const float rot = atan2f(-n.y, -n.x) - DEG2RADF(cmd->viewangles.y);
            bool over = false;
            for (float push = 15.f; push < 45.f && !over && !budget.Expired(); push += 15.f)
            {
                Restore();
                const Vec3 from = EntV3(pLocal, g_offOrigin);
                UserCmd_t c = *cmd;
                c.forwardmove = cosf(rot) * push;
                c.sidemove = -sinf(rot) * push;
                for (int t = 0; t < 3; t++) { UserCmd_t ct = c; Pred_Simulate(pLocal, &ct); }
                aaSims += 3;
                const Vec3 to = EntV3(pLocal, g_offOrigin);
                over = (to.x - from.x) * n.x + (to.y - from.y) * n.y < -5.f && to.z - from.z > 2.f && to.z - trace.endpos.z > 1.f;
            }
            Restore();
            if (over) { warmTick = -1; FlushDrop(); s_outs[o_lip]++; Diag(); return; }
        }
    }

    const auto simCmd = [&]() { UserCmd_t t = *cmd; Pred_Simulate(pLocal, &t); aaSims++; };
    const auto alignedNow = [&]() {
        if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) == MOVETYPE_LADDER) return false;
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return IsTargetVz(v.z, k_pinEps) && v.Length2D() >= 1.f;   // xy < 1 = stuck on the wall, not a surf
    };
    // after simCmd: pinned AND the pin rides on (AaRideOn) = stick; pinned but dies = texture bug, skip it
    // (tbpin). Sim state ends at *cmd's tick, like botox's 1-tick check
    const auto rides = [&]() {
        if (!alignedNow()) return false;
        const bool ok = AaRideOn(pLocal, *cmd, userCmd, wallN);
        s_outs[ok ? o_rides : o_tbpin]++;
        return ok;
    };

    Flush flush;
    const bool flushOn = flush.Init(pLocal, cmd, trace.plane.normal, HullPlaneDist(trace, mins, maxs), maxs, budget);
    // your own move already rides (tb act R): keep it and its speed
    if (flushOn && flush.userPins) { warmTick = -1; s_outs[o_pin]++; Restore(); Diag(); return; }
    if (flushOn && flush.Fine(cmd))
    {
        Restore(); simCmd();
        if (rides()) { warmTick = -1; s_outs[o_fine]++; Restore(); Diag(); return; }
        cmd->forwardmove = userFwd; cmd->sidemove = userSide; cmd->buttons = userButtons;
    }
    if (flush.sims) Restore();

    Vec3 wallAngle = VectorAngles(angles); ClampAnglesMv(wallAngle);

    const Vec3 lbStart(saveStartPos.x, saveStartPos.y, trace.endpos.z);
    const float rotation = DEG2RADF(wallAngle.y - cmd->viewangles.y);
    const float cosRot = cosf(rotation), sinRot = sinf(rotation);
    bool detect = false;

    GameTrace_t trace2{};
    TraceLine(lbStart, lbStart + Vec3(savedCos, savedSin, 0.f), MASK_PLAYERSOLID, &fil, &trace2);

    if (warmTick >= 0 && tickNow - warmTick <= 2 && (warmFwd != 0.f || warmSide != 0.f))
    {
        Restore();
        cmd->forwardmove = warmFwd;
        cmd->sidemove    = warmSide;
        simCmd();
        if (rides()) { warmTick = tickNow; detect = true; }
    }

    for (float multiplier = 10.f; !detect && multiplier < 100.f && !budget.Expired(); multiplier += 10.f)
    {
        Restore();
        const float fwd = cosRot * multiplier, side = -sinRot * multiplier;
        cmd->forwardmove = fwd;
        cmd->sidemove    = side;
        simCmd();
        if (rides())
        {
            warmFwd = fwd; warmSide = side; warmTick = tickNow;
            detect = true;
            break;
        }
    }

    // no pin THIS tick: the stick (botox, restored 11-08 22:45 by user: "bring back the tb stick"). 1 ulp park (or step off a dead stop) keeps you flush
    // for the catch tick instead of the hard-hit contact push; already flush and neither = YOUR move, the
    // contact push would only slam the hull back to 0.03125
    if (flushOn && !detect)
    {
        const bool parked = flush.Park(cmd);
        if (parked || flush.Unstick(cmd))
        {
            s_outs[parked ? o_park : o_unstick]++;
            Restore(); Diag();
            return;
        }
        Restore();
        // your move goes stuck and no step off frees it: never keep it, old path + fix air stucks flip
        if (flush.gap <= k_touch && !flush.userStuck)
        {
            Flush::Commit(cmd, flush.in);
            s_outs[o_keep]++;
            Diag();
            return;
        }
    }

    const float engineIpt = TickInterval();
    bool contactReachable = true;
    if (!detect && engineIpt > 0.f)
    {
        const float dirX = savedCos / 64.f, dirY = savedSin / 64.f;
        const float toward = -(dirX * trace.plane.normal.x + dirY * trace.plane.normal.y);
        const float gapN = trace.fraction * (toward > 0.f ? toward : 0.f);
        const float close = -(s_prePredVel.x * trace.plane.normal.x + s_prePredVel.y * trace.plane.normal.y) * engineIpt;
        if (gapN > 2.f * (close > 0.f ? close : 0.f) + 0.5f) contactReachable = false;
    }

    // contact loop runs on slanted faces too: the engine park (0.03125 off) sits outside every tighter lip.
    // 11-14 csgo log: skipping it there held the hull 0.03115 off n -0.71 -0.71, lips missed. stucks = AaGuard's
    if (!detect && contactReachable)
    {
        for (int i = 5; i >= 1 && !spent(); i--)
        {
            Restore();
            const float fwd = cosRot * i * 9.f, side = -sinRot * i * 9.f;
            for (int seph = 0; seph < 2; seph++)
            {
                UserCmd_t fake = *cmd;
                fake.forwardmove = fwd;
                fake.sidemove = side;
                Pred_Simulate(pLocal, &fake);
                aaSims++;
                if (EntMoveType(pLocal) == MOVETYPE_LADDER) continue;
                const Vec3 sp2 = EntV3(pLocal, g_offOrigin);
                GameTrace_t t3{};
                TraceHull(sp2, sp2 + angles, mins, maxs, MASK_PLAYERSOLID, &fil, &t3);
                if (t3.fraction < 1.f)
                {
                    cmd->forwardmove = fwd;
                    cmd->sidemove = side;
                    detect = true;
                    break;
                }
            }
            if (detect) break;
        }
    }

    {
        const bool pinned = detect && IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_pinEps);
        s_outs[pinned ? o_pin : detect ? o_touch : o_miss]++;
    }

    // along-wall keys: the strafe speed that keeps the pin, then the 15..25 u/s angle that gains the most
    const int b = cmd->buttons;
    if (IsTargetVz(s_prePredVel.z, 0.01f) || (b & IN_FORWARD) || (b & IN_BACK) || (b & IN_MOVELEFT) || (b & IN_MOVERIGHT))
    {
        Vec3 wishdir{};
        bool done = false;
        const float vy = cmd->viewangles.y;
        auto dirAt = [](float yawDeg) { return Vec3(cosf(DEG2RADF(yawDeg)) * 128.f, sinf(DEG2RADF(yawDeg)) * 128.f, 0.f); };
        if ((b & IN_FORWARD) && !(b & IN_BACK) && !(b & IN_MOVELEFT) && !(b & IN_MOVERIGHT)) { wishdir = dirAt(vy); done = true; }
        if ((b & IN_BACK) && !(b & IN_FORWARD) && !(b & IN_MOVELEFT) && !(b & IN_MOVERIGHT)) { wishdir = dirAt(vy + 180.f); done = true; }
        if ((b & IN_MOVELEFT) && !(b & IN_BACK) && !(b & IN_FORWARD) && !(b & IN_MOVERIGHT)) { wishdir = dirAt(vy + 90.f); done = true; }
        if ((b & IN_MOVERIGHT) && !(b & IN_BACK) && !(b & IN_MOVELEFT) && !(b & IN_FORWARD)) { wishdir = dirAt(vy - 90.f); done = true; }

        if (done)
        {
            GameTrace_t trace4{};
            const Vec3 st(saveStartPos.x, saveStartPos.y, trace.endpos.z);
            TraceLine(st, st + wishdir, MASK_PLAYERSOLID, &fil, &trace4);

            const auto forceKeys = [&]() {
                if (cmd->forwardmove < 0.f && (cmd->buttons & IN_FORWARD))   cmd->forwardmove = 450.f;
                if (cmd->forwardmove > 0.f && (cmd->buttons & IN_BACK))      cmd->forwardmove = -450.f;
                if (cmd->sidemove < 0.f && (cmd->buttons & IN_MOVERIGHT))    cmd->sidemove = 450.f;
                if (cmd->sidemove > 0.f && (cmd->buttons & IN_MOVELEFT))     cmd->sidemove = -450.f;
            };

            if (trace4.fraction < 1.f)
            {
                const int buttons2 = cmd->buttons;
                float fwd2 = cmd->forwardmove, side2 = cmd->sidemove;
                const int backupVelo = static_cast<int>(EntV3(pLocal, g_offVelocity).Length2D());

                if (IsTargetVz(s_prePredVel.z, 0.01f) || IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_pinEps))
                {
                    if (s_prePredVel.Length2D() != 0.f && EntV3(pLocal, g_offVelocity).Length2D() != 0.f)
                    {
                        static float warmSpeed = 0.f;
                        static int warmSpeedTick = -1;
                        bool speedWarm = false;
                        const auto setKeys = [&](float i) {
                            if (buttons2 & IN_FORWARD)   cmd->forwardmove = i;
                            if (buttons2 & IN_BACK)      cmd->forwardmove = -i;
                            if (buttons2 & IN_MOVELEFT)  cmd->sidemove = -i;
                            if (buttons2 & IN_MOVERIGHT) cmd->sidemove = i;
                        };
                        if (warmSpeed != 0.f && tickNow - warmSpeedTick <= 2)
                        {
                            Restore();
                            setKeys(warmSpeed);
                            simCmd();
                            if (IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_pinEps))
                            {
                                fwd2 = cmd->forwardmove; side2 = cmd->sidemove;
                                warmSpeedTick = tickNow;
                                speedWarm = true;
                            }
                        }
                        for (int i = speedWarm ? 0 : 450; i > 0 && !spent(); i -= 45)
                        {
                            Restore();
                            setKeys(static_cast<float>(i));
                            simCmd();
                            if (IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_pinEps))
                            {
                                fwd2 = cmd->forwardmove; side2 = cmd->sidemove;
                                warmSpeed = static_cast<float>(i);
                                warmSpeedTick = tickNow;
                                break;
                            }
                        }

                        const int predVelo = static_cast<int>(EntV3(pLocal, g_offVelocity).Length2D());
                        cmd->forwardmove = fwd2;
                        cmd->sidemove    = side2;

                        float dirFwd[3]{}, dirSide[3]{}, maxSpeed[3]{};
                        int found = 0;
                        if (Difference(static_cast<float>(backupVelo), static_cast<float>(predVelo)) < 5.f)
                        {
                            for (float angle = 15.f; angle < 30.f && !spent(); angle += 5.f)
                            {
                                Restore();
                                const float mVel = hypotf(s_prePredVel.x, s_prePredVel.y);
                                const float ideal = RAD2DEGF(atanf(angle / mVel));
                                Vec3 dvelo = EntV3(pLocal, g_offVelocity); dvelo.z = 0.f;
                                Vec3 veloAngle = VectorAngles(dvelo); ClampAnglesMv(veloAngle);
                                Vec3 intoAng = VectorAngles(Vec3(-trace.plane.normal.x, -trace.plane.normal.y, 0.f)); ClampAnglesMv(intoAng);
                                const float delta = remainderf(veloAngle.y - intoAng.y, 360.f);
                                wallAngle.y += delta >= 0.f ? ideal : -ideal;

                                const float rot2 = DEG2RADF(wallAngle.y - cmd->viewangles.y);
                                const float fwdA = cosf(rot2) * 450.f, sideA = -sinf(rot2) * 450.f;
                                cmd->forwardmove = fwdA;
                                cmd->sidemove    = sideA;

                                const Vec3 bVelo = EntV3(pLocal, g_offVelocity);
                                simCmd();
                                const Vec3 pVelo = EntV3(pLocal, g_offVelocity);
                                if (IsTargetVz(bVelo.z, 0.01f) && IsTargetVz(pVelo.z, k_pinEps)
                                    && pVelo.Length2D() > bVelo.Length2D() && found < 3)
                                {
                                    dirFwd[found] = fwdA; dirSide[found] = sideA;
                                    maxSpeed[found] = pVelo.Length2D() - bVelo.Length2D();
                                    found++;
                                }
                            }

                            cmd->forwardmove = fwd2;
                            cmd->sidemove    = side2;
                            float mxsp = 0.f;
                            int best = -1;
                            for (int k = 0; k < found; k++)
                                if (maxSpeed[k] > mxsp) { best = k; mxsp = maxSpeed[k]; }
                            if (best != -1) { cmd->forwardmove = dirFwd[best]; cmd->sidemove = dirSide[best]; }
                        }
                    }
                }

                if (Difference(trace4.endpos.x, trace2.endpos.x) > 1.f && Difference(trace4.endpos.y, trace2.endpos.y) > 1.f)
                    forceKeys();
            }
            else
                forceKeys();
        }
    }

    Restore();

    // botox auto_align.cpp:956-984 m_fix_air_stucks: our move hangs you dead on the face (xy < 1) and the
    // exact opposite press keeps more speed = send that. Botox only asks when the option is on; CSS also
    // always on a slanted face, where an AABB meets the face with one corner and digs it in (MV_IsHullFlatWall)
    if ((g_asFixStucks || !MV_IsHullFlatWall(wallN)) && !spent())
    {
        const float inFwd = cmd->forwardmove, inSide = cmd->sidemove;
        if (fabsf(inFwd) >= 0.01f || fabsf(inSide) >= 0.01f)
        {
            UserCmd_t test = *cmd;
            Pred_Simulate(pLocal, &test);
            const float inSpeed = EntV3(pLocal, g_offVelocity).Length2D();
            Restore();
            if (inSpeed < 1.f)
            {
                test = *cmd;
                test.forwardmove = -inFwd;
                test.sidemove    = -inSide;
                Pred_Simulate(pLocal, &test);
                const float outSpeed = EntV3(pLocal, g_offVelocity).Length2D();
                Restore();
                if (outSpeed > inSpeed + 0.01f)
                {
                    cmd->forwardmove = -inFwd;
                    cmd->sidemove    = -inSide;
                    s_outs[o_stuckfix]++;
                }
            }
        }
    }
    Diag();
}

// air stuck = TryPlayerMove zeroed the move (allFraction 0, 3+ planes, crease reversal, end-pos stuck box) =
// xy 0 (css: the 1/32 clip nudge), and FinishGravity's half step makes vz read as a pin: a corner wedged in a
// prop / slanted face. our move stalls this tick or held one more where the cmd we got doesn't = the cmd we got back
static bool AaGuard(void* pLocal, UserCmd_t* cmd, const UserCmd_t& got)
{
    if (cmd->forwardmove == got.forwardmove && cmd->sidemove == got.sidemove && cmd->buttons == got.buttons) return false;
    if (SimsLeft() < 4) return false;
    const int frame = PredCommandsPredicted() - 1;
    const auto stalls = [&](const UserCmd_t& c) {
        RestoreToFrame(frame);
        for (int t = 0; t < 2; ++t)
        {
            UserCmd_t k = c;
            Pred_Simulate(pLocal, &k);   // walks m_nTickBase itself
            if (EntMoveType(pLocal) != MOVETYPE_WALK || (EntI(pLocal, g_offFlags) & FL_ONGROUND)) return false;
            // vz too: a slow creep / park / corner crease is xy < 1 with a live vz, a wedge sits on the pin
            const Vec3 v = EntV3(pLocal, g_offVelocity);
            if (v.Length2D() < 1.f && IsTargetVz(v.z, aa::k_pinEps)) return true;
        }
        return false;
    };
    const bool back = stalls(*cmd) && !stalls(got);
    RestoreToFrame(frame);
    if (!back) return false;
    cmd->forwardmove = got.forwardmove;
    cmd->sidemove    = got.sidemove;
    cmd->upmove      = got.upmove;   // the hold writes it too: send exactly the cmd simmed free
    cmd->buttons     = got.buttons;
    // the hold's press is gone: pixel surf / wall engine must not keep "AA's cmd", no mispredict read off it
    s_aaHolding = false;
    aa::s_expect = false;
    return true;
}

static void AutoAlign(void* pLocal, UserCmd_t* cmd)
{
    const UserCmd_t got = *cmd;
    s_aaWedge = false;
    AutoAlignBotox(pLocal, cmd);
    // square face, no prop = no wedge (a head-on surf is xy 0 too): native, no guard sims. reverted = never sent, no park verdict
    if (s_aaWedge && AaGuard(pLocal, cmd, got)) { aa::s_outs[aa::o_guard]++; aa::FlushDrop(); }
}
