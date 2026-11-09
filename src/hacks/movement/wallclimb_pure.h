#pragma once
// wall climb pure half: constants + the hop state machine. No engine types, gate-tested by tests/wallclimb_test.cpp.
//
// A climb is ground -> hop -> one-tick sweep that grounds the hull flush on a displacement seam -> hop again.
// The sweep lives in wallclimb.inl (engine sims). What can be tested cold is here: the armed hop (an air win marks
// the ground tick it bought, the launch fires on the REAL grounded tick, a refused press is released for one tick
// so m_nOldButtons clears) and the prevent-slow test.
#include <algorithm>
#include <cmath>

namespace wc
{
    constexpr float k_scan_nz        = 0.65f; /* disp walls are bumpy: facets read up to ~0.7 below standable */
    constexpr float k_scan_reach     = 16.f;  /* detect out to here (2u threw away 43% of the air ticks) */
    constexpr float k_engage_support = 8.f;   /* hull face -> disp face, live, before any sim: commit range */
    constexpr float k_land_support   = 2.f;   /* post-sim: grounded ON the face, not near it */
    constexpr float k_min_input      = 0.01f; /* hands off the keys = hands off the cmd */
    constexpr float k_rise_vz        = 140.f; /* CSS CategorizePosition never grounds vz > NON_JUMP_VELOCITY 140 (gamemovement.cpp:3810) */
    constexpr int   k_jump_pending   = 6;     /* ticks an air win stays armed waiting for its ground tick (>= k_depth) */
    constexpr float k_plan_z_tol     = 2.f;   /* real ground tick z vs the win's simmed land z (sims match to ~1 ulp) */
    constexpr int   k_engage_ticks   = 8;     /* "a face recently": the jump-bit unlatch needs a wall */
    /* of one tick, wall clock, on top of the shared sim pool. 11-04 log: ~3 ms a sweep tick, worst 5.8 = frame spikes.
       0.35 = 5.25 ms at 66 tick; the yaw order puts the preferred variants first, so a cut mid-layer drops that layer's least wanted (cut=) */
    constexpr float k_time_share     = 0.35f;
    constexpr float k_slow_xy        = 200.f; /* prevent slow: no win that halves xy above this */
    constexpr float k_slow_ratio     = 0.5f;
    constexpr int   k_viz_ticks      = 16;
    /* fwd values: 450 spends the whole air addspeed cap, 30 is the cap. The duck is the axis that really moves the hull
       (airborne Duck() finishes the same tick = 8.5u origin lift, cs_gamemovement.cpp:826), so the other stance only gets 450. 11-04 log, 107 wins:
       none from the old fwd 5 crawl, all 7 duck wins at 450 -- both cuts halve the search for nothing */
    constexpr float k_forward[]      = { 450.f, 30.f };
    constexpr float k_duck_forward[] = { 450.f };
    /* yaw off the into-wall direction, BOTH sides (first side = against the velocity, SweepSign). Steep disps ground on crest
       vertices / near-flat edges (dispcoll_common.cpp Z slab = normal 0,0,1) or a 24x24 quadrant hull
       (TracePlayerBBoxForGround): lateral travel along the face is what drags a quadrant over one. 87 = near tangent */
    constexpr float k_yaw[]          = { 0.f, 15.f, -15.f, 30.f, -30.f, 45.f, -45.f, 70.f, -70.f, 87.f, -87.f };
    constexpr int   k_yaws           = (int)(sizeof(k_yaw) / sizeof(k_yaw[0]));
    constexpr int   k_forwards       = (int)(sizeof(k_forward) / sizeof(k_forward[0]));
    constexpr int   k_duck_forwards  = (int)(sizeof(k_duck_forward) / sizeof(k_duck_forward[0]));
    constexpr int   k_max_vars       = (k_forwards + k_duck_forwards) * k_yaws;   /* 33 */
    /* lookahead: each variant is held up to this many ticks (iterative deepening, depth 1 for all first). Only the first
       tick goes out; the next tick re-finds the plan one shallower. Capped by the armed hop's wait */
    /* 11-04 log at 4: 49 of 79 climbs were first seen AT depth 4 = the horizon was the limit, not the geometry */
    constexpr int   k_depth          = 6;
    static_assert(k_depth <= k_jump_pending, "an armed hop must outlive the deepest lookahead");
    constexpr int   k_snap_base      = 224;   /* snapshot slots k_snap_base.. (EB owns 80..223, edge skip 0..64) */
    constexpr float k_keep_support   = 8.f;   /* a held variant further off the face is dead (= k_engage_support) */
    /* two held variants in the SAME state after a tick (wish fully spent: airaccel addspeed <= 0, or clipped flat by the
       face) fly the same from then on: the later one is dropped. Tight on purpose -- crest catches live 1-2 floats apart */
    constexpr float k_dup_org        = 1e-3f;
    constexpr float k_dup_vel        = 1e-2f;
    constexpr float k_climb_rise     = 2.f;   /* relaxed land: above the last ground by this (csgo: jump floor + 2) */

    // GATE 3. Grounded flush on the scanned face (<= 2u) is a catch. Further out (a crest / quadrant catch reads several u
    // off one facet's plane on a bumpy disp) it still is when the ground under the hull is a disp, higher than the last
    // ground (a landing on the floor you jumped from is never higher) and NOT walkable under the hull's center (a crest /
    // quadrant catch hangs the center over the steep slope; an uphill disp floor has a standable facet right below it)
    // face_disp false = CSS brush face (botox csgo wall_climb catches brush lips too, only its hug is disp-only):
    // a brush face is one plane, so the only catch is a ledge / lip / wall top flush beside it, and only ABOVE the last
    // ground -- flush on the floor beside a brush wall is a plain landing, never a climb
    inline bool LandOk(const float support, const bool ground_disp, const float land_z, const float ground_z, const bool center_floor,
                       const bool face_disp = true)
    {
        if (!face_disp) return support <= k_land_support && land_z > ground_z + k_climb_rise;
        if (support <= k_land_support) return true;
        return support <= k_engage_support && ground_disp && !center_floor && land_z > ground_z + k_climb_rise;
    }

    // brush gate, before any sim: a brush win must land above ground_z + k_climb_rise. Origin z can still gain the
    // rise left in vz (v^2 / 2g, vz <= 140 here) plus one air duck (CSS FinishDuck lift 8.5, cs_gamemovement.cpp:826;
    // 9 = margin). Under that = no brush win exists, skip ~100 sims. Every falling tick beside a dust2 wall hits this
    constexpr float k_air_duck_lift = 9.f;
    inline bool BrushReach(const float start_z, const float vz, const float ground_z, const float gravity = 800.f)
    {
        const float up = vz > 0.f ? vz * vz / (2.f * gravity) : 0.f;
        return start_z + up + k_air_duck_lift > ground_z + k_climb_rise;
    }

    // disp-face scan past brush faces. MV_DetectWallLines walks 16 directions from its start and returns the first
    // face plus that direction's angle UNWRAPPED (start .. start+2pi). The next call starts one direction later; the walk
    // stops once a full turn from the first start is covered, so it costs at most the 16 directions one scan has
    constexpr float k_two_pi    = 6.28318530718f;
    constexpr float k_scan_step = k_two_pi / 16.f;
    inline float NextScan(const float found_angle) { return found_angle + k_scan_step; }
    inline bool  ScanLeft(const float start, const float a) { return a < start + k_two_pi - 1e-3f; }
    inline float WrapScan(const float a) { return std::fmod(a, k_two_pi); }

    inline bool Slowed(const float old_xy, const float new_xy)
    {
        return old_xy > k_slow_xy && new_xy / old_xy < k_slow_ratio;
    }

    // yaw sweep sign: skew the into-wall wish against the side the velocity comes from
    inline float SweepSign(const float relative_yaw) { return relative_yaw >= 0.f ? -1.f : 1.f; }

    inline float WrapDeg(float a)
    {
        a = std::fmod(a + 180.f, 360.f);
        if (a < 0.f) a += 360.f;
        return a - 180.f;
    }

    // variant yaw order: nearest to ref_yaw first (the live plan's wish, else the player's own), ties keep the k_yaw
    // order skewed by sign. Iterative deepening commits the FIRST win of the shallowest layer, so with the fixed order
    // two climbs in a row bent the move 0..87 deg apart (11-04 log: fwd/side 182/-411, 436/-110, 26/-15): now the win
    // is the catch that bends the player's move least, and the winning layer stops sooner
    inline void OrderYaws(const float wall_yaw, const float ref_yaw, const float sign, int out[k_yaws])
    {
        float dev[k_yaws];
        for (int i = 0; i < k_yaws; ++i) {
            out[i] = i;
            dev[i] = std::fabs(WrapDeg(wall_yaw + k_yaw[i] * sign - ref_yaw));
        }
        std::stable_sort(out, out + k_yaws, [&](const int a, const int b) { return dev[a] < dev[b]; });
    }

    // ---- steer to seam (botox m_wall_climb_seam_steer + m_wall_climb_steer_scale "delta strafe scale", lab P76) ----
    // where along the wall the hull comes back onto the face decides which seam catches it, and one air tick of along wish
    // moves the along speed by up to ~37 (AirAccelerate). Targets are DELTAS on the along speed you fly ("delta strafe
    // scale"): 11-05 log, 118 jobs, along speed -320..384 -- botox's absolute +-30 targets were a 300 u/s brake or out of
    // reach. Every reachable one is scored (botox took the first that beat your path)
    constexpr float k_steer_deltas[]    = { -6.f, 6.f, -12.f, 12.f, -20.f, 20.f, -30.f, 30.f }; /* along wall u/s */
    constexpr int   k_steer_n           = (int)(sizeof(k_steer_deltas) / sizeof(k_steer_deltas[0]));
    constexpr float k_air_cap           = 30.f;  /* GetAirSpeedCap: no air wish raises a speed past this along its own dir */
    constexpr int   k_steer_delay       = 5;     /* job slices (first air tick on), the steer goes out from this tick */
    constexpr int   k_steer_tick_sims   = 150;   /* job sims per slice, on top of the wall clock */
    constexpr int   k_steer_max         = 6;     /* steer ticks to reach the target */
    constexpr int   k_steer_roll_ticks  = 64;    /* CSS jump sqrt(2*800*57) = 302: 2 x 302 / 800 = 50 ticks back to the jump z, + margin */
    constexpr float k_steer_fall        = 64.f;  /* a roll this far under the last ground is over */
    constexpr float k_steer_done        = 0.5f;  /* along speed this close to the target = steered */
    constexpr float k_steer_wish        = 250.f;
    /* the same log: 414 of 489 catches came FALLING, most under the +10 botox scored -- every catch on the face counts now */
    constexpr float k_steer_gain        = 8.f;   /* your path already catches: steer only for one this much higher */
    constexpr float k_steer_drift       = 1.f;   /* real START at the steer tick vs the job's: further = your keys changed */
    constexpr int   k_steer_slot        = k_snap_base + k_max_vars;   /* START, +1 = the roll (past the sweep's slots) */
    constexpr float k_steer_scale_min   = 0.25f, k_steer_scale_max = 2.f;

    // the wish that moves along speed `need` in ONE AirAccelerate tick along sign(need) * t:
    // accelspeed = accel*w*ipt*fr = |need|, and addspeed = min(w, 30) - sg*cur >= |need| needs w >= sg*target
    inline float SteerWish(const float need, const float target, const float accel_ipt_fr)
    {
        const float sg = need < 0.f ? -1.f : 1.f;
        const float a  = accel_ipt_fr > 1e-4f ? accel_ipt_fr : 1e-4f;
        return (std::min)((std::max)(std::fabs(need) / a, sg * target), k_steer_wish);
    }

    // reachable in the air: a wish along +t can raise the along speed only up to k_air_cap (addspeed = cap - cur), along -t
    // only down to -k_air_cap; slowing toward / through 0 is always open
    inline bool SteerReachable(const float cur, const float target)
    {
        return target <= (std::max)(cur, k_air_cap) && target >= (std::min)(cur, -k_air_cap);
    }

    // cur + delta x scale, unreachable ones dropped, smallest change first (k_steer_deltas order). Returns the count
    inline int SteerOrder(const float cur, float scale, float out[k_steer_n])
    {
        scale = (std::min)((std::max)(scale, k_steer_scale_min), k_steer_scale_max);
        int n = 0;
        for (const float d : k_steer_deltas)
            if (SteerReachable(cur, cur + d * scale)) out[n++] = cur + d * scale;
        return n;
    }

    // a rolled path's worth: a catch on the face scores by its z off the last ground (higher = more climb), 0 = no catch
    inline float SteerScore(const bool caught, const float z, const float ground_z)
    {
        return caught ? (std::max)(1.f, 1000.f + (z - ground_z)) : 0.f;
    }

    // your path rolled; it misses and a target catches, or it catches and a target catches k_steer_gain higher. Never
    // trade a catch you have for one a float higher (11-05 log: 1047 -> 1048 went out)
    inline bool SteerPick(const float base, const float best)
    {
        if (base < 0.f || best <= 0.f) return false;
        return base == 0.f || best >= base + k_steer_gain;
    }

    enum e_hop { hop_none, hop_release, hop_press };

    struct Hop
    {
        int   pending    = 0;     /* ticks left on an armed hop (air win -> ground press) */
        bool  jumpedLast = false; /* pressed last tick and STILL grounded = engine refused */
        float landZ      = 0.f;   /* origin z the win's sim grounded at */

        void Arm(const float land_z = 0.f) { pending = k_jump_pending; landZ = land_z; }
        void Reset() { pending = 0; jumpedLast = false; }
        // a depth-d win only sends tick 1, so the plan can be lost on the way (keys changed, budget, other facet) and the
        // ground that arrives is the floor. The hop fires only on the ground the win simulated
        bool ZOk(const float z) const { return std::fabs(z - landZ) <= k_plan_z_tol; }

        // every tick, before any yield: returns true when the armed hop's ground tick arrived
        bool Clock(const bool ground_pre)
        {
            if (pending <= 0)
                return false;
            --pending;
            if (!ground_pre)
                return false;
            pending = 0;
            return true;
        }

        // grounded tick: what to do with IN_JUMP. raw_jump = the real space bar (wc never jumps on its own),
        // prev_jump = last cmd went out with IN_JUMP (m_nOldButtons has it = a press now is refused as a pogo)
        e_hop Launch(const bool armed, const bool raw_jump, const bool prev_jump)
        {
            if (!armed || !raw_jump) { jumpedLast = false; return hop_none; }
            if (jumpedLast || prev_jump) { jumpedLast = false; return hop_release; }
            jumpedLast = true;
            return hop_press;
        }
    };

    // airborne: strip a held IN_JUMP so the landing tick's press is not refused. Only beside a face wc has engaged, never
    // on the tick the ground is left / regained (ej / lj / mj own those), never while another feature wants the bit
    inline bool Unlatch(const bool ground_pre, const bool ground_post, const bool raw_jump, const bool engaged, const bool other_wants_jump)
    {
        const bool left = ground_pre && !ground_post, landing = !ground_pre && ground_post;
        return !ground_pre && !left && !landing && raw_jump && engaged && !other_wants_jump;
    }
}
