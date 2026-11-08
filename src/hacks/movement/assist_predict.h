#pragma once
// botox csgo-sdk hacks/movement/assist_predict.h, ported 1:1 for pixeljump assist
// (arrival_land), head-bounce assist (arrival_bang) and pixelsurf assist (arrival_surf,
// added PSPORT round 2 11-04 -- the cross-unit block on psassist.inl's shared state is lifted,
// see psassist.inl's PixelSurfAssistRun). arrival_surf adds target_t.grid/plane, in_window()
// and the lip probe (surf_lip/replay_to below): botox's own window/lip test from
// pixelsurf_assist.cpp, needed because a window hit with no face under it (no lip) must fall
// through to a lower point instead of taking a phantom catch.
//
// ADAPTATION (surf_lip/replay_to): botox's lip() closure tries 12 press magnitudes from the
// SAME mid-arc instant via g_prediction.snapshot_save(0)/snapshot_load(0). CSS's prediction.h
// (Pred_Simulate/RestoreToFrame) has no mid-arc snapshot primitive, only a restore-to-the-
// whole-arc's-start -- adding one would mean touching prediction.cpp's noclip-frame internals
// blind, which the project's own rules forbid (crash-prone, verify-before-write). replay_to()
// substitutes a full replay of ticks [0, arrival] from c.frame for each magnitude trial instead
// of a O(1) snapshot restore: same result (did some into-wall press pin the hull here), bounded
// by the existing sim budget (c.budget/c.max_sims), at O(arrival_tick) cost per trial instead of
// O(1). An exhausted budget mid-probe returns -1, which is botox's own "not probed" value and is
// treated as a hit, same as a probed catch -- never a false rejection.
//
// botox -> CSS/this-repo shims, nothing else differs:
//   g_ctx.m_local / m_cmd              -> pLocal / pCmd, passed to run() (CSS convention: every
//                                         psassist.inl function takes them explicitly, no global ctx)
//   local->get_origin/velocity/flags   -> EntV3(pLocal,g_offOrigin/g_offVelocity), EntI(...,g_offFlags)
//   local->get_collideable()->obb_maxs -> EntV3(pLocal, g_offMaxs) (the hull read is live engine
//                                         state either way, so CSS's real 62/45 hull comes through
//                                         for free -- nothing here hardcodes a hull number)
//   local->get_tick_base() = tick_base+i -> nothing: Pred_Simulate (prediction.cpp) already reads/
//                                         advances the entity's own tick_base and moves curtime with
//                                         it every call, so the duck-timer gate botox's manual bump
//                                         existed for already works
//   g_prediction.restore_entity_to_predicted_frame -> RestoreToFrame
//   g_prediction.begin(local,cmd)+end(local)        -> Pred_Simulate(pLocal, pCmd)
//   g_prediction.m_commands_predicted                -> PredCommandsPredicted()
//   c_angle (+, normalize, clamp)       -> Vec3 (x=pitch,y=yaw), NormalizeAngle(y)/std::clamp(x)
//   start_movement_fix/end_movement_fix bracket  -> CorrectMovement(pCmd, v, c.view) in one call
//                                         (same contract: "read as a wish in v's frame, write back
//                                         in c.view's frame" -- movement.h's own docstring)
//   cmd->m_forward_move/m_side_move/m_view_point/m_buttons/m_tick_count -> UserCmd_t's own fields
//   n_tick::c_sim_budget                -> tick_scale.h (ported alongside this file)
#include "movement.h"
#include "tick_scale.h"
#include "css_moves_pure.h"   // PinnedVz: CSS clip nudge
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace n_assist
{
    enum e_launch : int {
        launch_jump = 0,
        launch_minijump,
        launch_longjump,
        launch_crouch_hop,
        launch_mini_crouch_hop,
        launch_jumpbug,
        launch_count
    };
    inline const char* const k_launch_names[launch_count] = { "jump", "mj", "lj", "crouch jump", "crouch jb", "jb" };

    struct program_t {
        e_launch launch = launch_jump;
        bool crouch     = false;
    };

    inline int buttons(const program_t& p, const int i, const bool ground, const int air, const int jb_tick, const int lj_hold)
    {
        if (air < 0) {
            if (p.launch == launch_jumpbug)
                return i == jb_tick ? IN_DUCK : IN_JUMP;
            if (!ground)
                return p.launch == launch_crouch_hop || p.launch == launch_mini_crouch_hop ? IN_DUCK : 0;
            return p.launch == launch_jump || p.launch == launch_mini_crouch_hop ? IN_JUMP : IN_JUMP | IN_DUCK;
        }
        const bool own        = (p.launch == launch_longjump && air < lj_hold) || p.launch == launch_mini_crouch_hop;
        const int crouch_from = p.launch == launch_minijump ? 2 : 1;
        return own || (p.crouch && air >= crouch_from) ? IN_DUCK : 0;
    }

    inline constexpr program_t k_programs[] = {
        { launch_jump, false },       { launch_jump, true },       { launch_minijump, false },   { launch_minijump, true },
        { launch_longjump, false },   { launch_longjump, true },   { launch_crouch_hop, false }, { launch_crouch_hop, true },
        { launch_mini_crouch_hop, true }, { launch_jumpbug, false }, { launch_jumpbug, true },
    };

    // per-point allowed-program flags (botox points_check_t's per-launch-type subset). Used by
    // head-bounce assist's own point list; pixeljump assist allows every program on its single
    // nearest point, matching the "pixeljump = its own key over the same points, its types" rule
    // the existing CSS pixeljump toggle already used before this port (no per-point mask there).
    struct point_flags_t {
        bool jump              = true;
        bool c_jump            = true;
        bool minijump          = true;
        bool c_minijump        = true;
        bool longjump          = true;
        bool c_longjump        = true;
        bool crouch_hop        = true;
        bool high_crouch_jump  = true;
        bool c_crouch_hop      = true;
        bool c_high_crouch_jump = true;
        bool c_mini_crouch_hop = true;
        bool jumpbug           = true;
        bool c_jumpbug         = true;
    };

    // botox is_bind_restricted_in_radius's restriction GROUPS (pixelsurf_assist.cpp:562-588),
    // added PSPORT round 3 11-04: a point restricts ONE whole group, not a single launch+crouch
    // pair -- "ch" (crouch_hop + its crouch variant) and "hcj" (high_crouch_jump + its crouch
    // variant) are deliberately SEPARATE groups even though both launch_crouch_hop, which a
    // plain e_launch comparison (round 2's PsaBindRestricted) could not tell apart. 7 groups,
    // botox's exact tag strings as an enum instead of std::string compares.
    enum e_bind : int { bind_j = 0, bind_mj, bind_lj, bind_ch, bind_hcj, bind_mch, bind_jb };

    struct flag_t {
        bool point_flags_t::*flag;
        program_t program;
        e_bind bind;
    };
    inline constexpr flag_t k_flags[] = {
        { &point_flags_t::jump, { launch_jump, false }, bind_j },
        { &point_flags_t::c_jump, { launch_jump, true }, bind_j },
        { &point_flags_t::minijump, { launch_minijump, false }, bind_mj },
        { &point_flags_t::c_minijump, { launch_minijump, true }, bind_mj },
        { &point_flags_t::longjump, { launch_longjump, false }, bind_lj },
        { &point_flags_t::c_longjump, { launch_longjump, true }, bind_lj },
        { &point_flags_t::crouch_hop, { launch_crouch_hop, false }, bind_ch },
        { &point_flags_t::high_crouch_jump, { launch_crouch_hop, false }, bind_hcj },
        { &point_flags_t::c_crouch_hop, { launch_crouch_hop, true }, bind_ch },
        { &point_flags_t::c_high_crouch_jump, { launch_crouch_hop, true }, bind_hcj },
        { &point_flags_t::c_mini_crouch_hop, { launch_mini_crouch_hop, true }, bind_mch },
        { &point_flags_t::jumpbug, { launch_jumpbug, false }, bind_jb },
        { &point_flags_t::c_jumpbug, { launch_jumpbug, true }, bind_jb },
    };

    inline bool same(const program_t& a, const program_t& b) { return a.launch == b.launch && a.crouch == b.crouch; }

    inline int settle_air(const program_t& p, const int lj_hold) { return p.launch == launch_longjump ? lj_hold : 2; }

    // land = lumi "pixel climb": a sim tick that LANDS (air -> ground) within radius of the point.
    // bang = head-bounce: the hull's HEAD reaches the point's z, vz clipped (ceiling contact).
    // surf = pixelsurf assist: vz clips to -half_g (a wall catch) inside the target's z window.
    enum e_arrival : int { arrival_bang = 0, arrival_land, arrival_surf };

    struct target_t {
        float z = 0.f, x = 0.f, y = 0.f, radius = 0.f;
        float plane = 0.f;   // arrival_surf only: floor(z)
        bool grid   = false; // arrival_surf only: z sits on the 1/32 grid (in_window uses a wider band)
        int point = -1;
    };

    inline target_t make_land_target(const Vec3& pos, const float radius, const int point)
    {
        target_t t;
        t.z = pos.z; t.x = pos.x; t.y = pos.y; t.radius = radius; t.point = point;
        return t;
    }
    inline target_t make_target(const float z, const int point)
    {
        target_t t;
        t.z     = z; t.point = point;
        t.plane = std::floor(z);
        const float frac = z - t.plane;
        t.grid  = frac > 0.f && frac <= 0.03125f + 0.0005f;
        return t;
    }

    // botox pixelsurf_assist.cpp check()/in_window(), 1:1: the arrival tolerance for a non-grid
    // z (check) and the wider post-grid-line band for a z that sits exactly on the 1/32 grid
    inline bool check(const float a, const float b) { const float d = b - a; return d > -0.01f && d < 0.03f; }
    inline bool in_window(const target_t& t, const float z) { return t.grid ? z > t.plane && z <= t.plane + 0.03125f : check(z, t.z); }

    struct sample_t {
        double z_in = 0.0, vz_in = 0.0, z = 0.0, vz = 0.0;
        float hull_in = 0.f, hull = 0.f;
        bool ground = false, clipped = false, ground_in = false;
        float x = 0.f, y = 0.f;
    };

    inline float land_dist(const target_t& t, const sample_t& s)
    {
        const float dx = s.x - t.x, dy = s.y - t.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // landing tick on the point's top: z within 0.5 (origin = top), xy inside the radius
    inline bool lands(const target_t& t, const sample_t& s)
    {
        return s.ground && !s.ground_in && std::fabs(static_cast<float>(s.z) - t.z) <= 0.5f && land_dist(t, s) <= t.radius;
    }

    // half_g = h (gravity*frametime*0.5), only meaningful for arrival_surf -- a wall catch is
    // vz clipped to -half_g-ish AND the hull inside the target's z window, botox's arrives():
    // `s.vz - half_g >= 0.0 -> false; return in_window(t, z)`.
    inline bool arrives(const e_arrival kind, const target_t& t, const sample_t& s, const double half_g = 0.0)
    {
        if (kind == arrival_land)
            return lands(t, s);
        if (s.ground)
            return false;
        if (kind == arrival_surf)
            return s.vz - half_g < 0.0 && in_window(t, static_cast<float>(s.z));
        if (s.vz_in <= 0.0 || s.hull_in != s.hull)
            return false;
        if (s.z_in + s.hull_in <= t.z + 0.001 && s.z + s.hull > t.z)
            return true;
        return s.clipped && std::fabs(s.z + s.hull - t.z) < 0.05;
    }

    inline float near_gap(const e_arrival kind, const target_t& t, const sample_t& s, const double half_g = 0.0)
    {
        if (kind == arrival_land)
            return s.ground && !s.ground_in && std::fabs(static_cast<float>(s.z) - t.z) <= 0.5f ? land_dist(t, s) - t.radius : 1e9f;
        if (s.ground)
            return 1e9f;
        if (kind == arrival_surf)
            return s.vz - half_g < 0.0 ? static_cast<float>(std::fabs(s.z - t.z)) : 1e9f;
        return s.vz_in > 0.0 ? static_cast<float>(std::fabs(s.z + s.hull - t.z)) : 1e9f;
    }

    // botox: `kind != arrival_bang` takes the strict (fell well past it, stop) branch -- true
    // for arrival_land already and now also for arrival_surf (a bang/ceiling catch has no floor
    // to fall past, so it keeps the loose "still falling" branch alone)
    inline bool past(const e_arrival kind, const double lowest, const sample_t& s)
    {
        return kind != arrival_bang ? s.vz < 0.0 && s.z < lowest - 1.0 : s.vz < 0.0;
    }

    struct ctx_t {
        e_arrival kind = arrival_bang;
        int frame      = 0;
        int buttons    = 0;
        float fwd = 0.f, side = 0.f;
        float fwd0 = 0.f, side0 = 0.f;
        Vec3 view{};
        Vec3 delta{};
        int pre_ticks = 64;  // the launch must happen inside this
        int air_ticks = 256;
        int lj_hold   = 3;
        int jb_tick   = -1;
        double lowest      = 0.0;
        double prune_below = 96.0;
        float gravity      = 800.f;
        n_tick::c_sim_budget* budget = nullptr;
        int sims     = 0;
        int max_sims = 512;
        bool out_of_budget = false;
    };

    struct tick_t {
        int buttons = 0;
        float z     = 0.f;
        bool ground = false;
    };

    struct run_t {
        bool hit = false, exact = false, complete = true;
        int target  = -1;
        int arrival = -1, launch = -1, landing = -1;
        float arrive_z  = 0.f;
        float gap       = 1e9f;
        float end_z     = 0.f;
        bool end_ground = false;
        // arrival_surf only (botox pixelsurf_assist.cpp run_t): lip = 1 probed+caught,
        // 0 probed+no catch (that target is dead, fell through to a lower one), -1 not probed
        // (bang/land always -1, surf when the budget ran out mid-probe -- treated as a hit).
        // nolip* records the FIRST window hit nothing pinned, so the caller can fall back to it
        // (botox: "ride the nolip" after a few ticks of waiting for a lippier one) instead of
        // flying nothing at all.
        int lip          = -1;
        int nolip        = -1;
        int nolip_target = -1;
        float nolip_z    = 0.f;
        std::vector<tick_t> ticks;
    };

    inline bool big_turn(const float a, const float b)
    {
        float d = std::fabs(a - b);
        if (d > 180.f) d = 360.f - d;
        return d >= 90.f;
    }

    // replays ticks [0, until) exactly as run()'s main loop would (same aim/buttons/Pred_Simulate
    // sequence) to reach the state run() had right after tick `until - 1`. See the header banner:
    // this stands in for botox's g_prediction.snapshot_save/load, which CSS's prediction.h has no
    // equivalent for. `view` is the turn-tracking angle run() threads through aim() calls.
    inline void replay_to(void* pLocal, UserCmd_t* pCmd, ctx_t& c, const program_t& p, const int until, Vec3& view)
    {
        RestoreToFrame(c.frame);
        view = c.view;
        int air = -1;
        for (int i = 0; i < until; ++i) {
            const bool ground_in = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
            pCmd->forwardmove = i == 0 ? c.fwd0 : c.fwd;
            pCmd->sidemove    = i == 0 ? c.side0 : c.side;
            pCmd->viewangles  = c.view;
            if (!ground_in && (c.delta.x != 0.f || c.delta.y != 0.f)) {
                if (!big_turn(view.y, c.view.y)) {
                    view.y = NormalizeAngle(view.y + c.delta.y);
                    view.x = std::clamp(view.x + c.delta.x, -89.f, 89.f);
                }
                CorrectMovement(pCmd, view, c.view);
            }
            const double vz_in = EntV3(pLocal, g_offVelocity).z;
            const int press = buttons(p, i, ground_in, air, c.jb_tick, c.lj_hold);
            pCmd->buttons = (c.buttons & ~(IN_JUMP | IN_DUCK)) | press;
            ++c.sims;
            Pred_Simulate(pLocal, pCmd);
            if (air < 0) {
                const bool ground_now = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
                const double vz = EntV3(pLocal, g_offVelocity).z;
                if (!ground_now && vz > 0.0 && vz > vz_in + 100.0) air = 0;
            } else
                ++air;
        }
    }

    // botox pixelsurf_assist.cpp's lip() closure (:280-322), 1:1 on the result it reports (did
    // SOME into-wall press pin the hull at this seam -- 1 yes, 0 probed/no, -1 not probed), via
    // the replay_to() substitute for its snapshot_save/load. `press_i` is the arrival tick's own
    // press (its duck bit is kept, same as botox: `cmd->buttons = (c.buttons & ~in_duck) | (press & in_duck)`).
    inline int surf_lip(void* pLocal, UserCmd_t* pCmd, ctx_t& c, const program_t& p, const int arrival_i, const int press_i)
    {
        const Vec3 o    = EntV3(pLocal, g_offOrigin);
        const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
        TraceFilterWorldProps fil; fil.Setup(pLocal);
        float best = 1.f;
        Vec3 n{};
        for (int d = 0; d < 8; ++d) {
            const float a = static_cast<float>(d) * 0.78539816f;
            GameTrace_t tr{};
            TraceHull(o, Vec3(o.x + 0.1f * cosf(a), o.y + 0.1f * sinf(a), o.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
            if (!tr.startsolid && tr.fraction < best && fabsf(tr.plane.normal.z) < 0.7f) { best = tr.fraction; n = tr.plane.normal; }
        }
        if (best >= 1.f)
            return -1;

        const float frametime = n_tick::engine_interval();
        const double h_end    = static_cast<double>(c.gravity * frametime) * 0.5;
        const float pin       = static_cast<float>(-h_end);
        const float rot       = DEG2RADF(VectorAngles(Vec3(-n.x, -n.y, 0.f)).y - c.view.y);
        constexpr float k_presses[] = { 0.f, 10.f, 20.f, 30.f, 40.f, 45.f, 50.f, 60.f, 70.f, 80.f, 90.f, 450.f };

        for (const float m : k_presses) {
            // bounded: a replay_to(arrival_i+1) plus one more sim, repeated per magnitude --
            // an exhausted budget bails to "not probed" (-1) rather than running away
            if ((c.budget && c.budget->expired()) || c.sims + arrival_i + 2 > c.max_sims)
                return -1;

            Vec3 view;
            replay_to(pLocal, pCmd, c, p, arrival_i + 1, view);   // reach the state run() had after tick arrival_i

            if (m > 0.f) {
                pCmd->forwardmove = cosf(rot) * m;
                pCmd->sidemove    = -sinf(rot) * m;
                pCmd->viewangles  = c.view;
            } else {
                pCmd->forwardmove = c.fwd;
                pCmd->sidemove    = c.side;
                pCmd->viewangles  = c.view;
                if (c.delta.x != 0.f || c.delta.y != 0.f) {
                    if (!big_turn(view.y, c.view.y)) {
                        view.y = NormalizeAngle(view.y + c.delta.y);
                        view.x = std::clamp(view.x + c.delta.x, -89.f, 89.f);
                    }
                    CorrectMovement(pCmd, view, c.view);
                }
            }
            pCmd->buttons = (c.buttons & ~IN_DUCK) | (press_i & IN_DUCK);
            ++c.sims;
            Pred_Simulate(pLocal, pCmd);
            if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND) && cssm::PinnedVz(EntV3(pLocal, g_offVelocity).z, -pin, 0.01f))
                return 1;
        }
        return 0;
    }

    // the one sim loop both assists search with. full=true flies the whole engine arc tick by
    // tick (pixeljump assist always does -- a land arc has no xy in a float step); full=false
    // (head-bounce's quick pass) settles onto a free-flight ladder and steps it analytically
    // ahead, verified later with a full=true run on the one target that hit.
    inline run_t run(void* pLocal, UserCmd_t* pCmd, ctx_t& c, const program_t& p, const std::vector<target_t>& targets, const bool full)
    {
        run_t r;
        if (!pLocal || !pCmd)
            return r;

        RestoreToFrame(c.frame);
        const float frametime = n_tick::engine_interval();
        const double dt       = frametime;
        const double h        = static_cast<double>(1.f * c.gravity) * 0.5 * static_cast<double>(frametime);
        const double h_end    = static_cast<double>(1.f * c.gravity * frametime) * 0.5;
        const double pin      = -h_end;
        Vec3 view   = c.view;
        int air     = -1;
        bool free_prev = false;
        unsigned dead  = 0u;   // arrival_surf: targets a probed-and-rejected (lip==0) window already dropped
        r.ticks.reserve(128);

        const auto aim = [&](const int i, const bool ground_in, Vec3& v) {
            pCmd->forwardmove = i == 0 ? c.fwd0 : c.fwd;
            pCmd->sidemove    = i == 0 ? c.side0 : c.side;
            pCmd->viewangles  = c.view;
            if (!ground_in && (c.delta.x != 0.f || c.delta.y != 0.f)) {
                if (!big_turn(v.y, c.view.y)) {
                    v.y = NormalizeAngle(v.y + c.delta.y);
                    v.x = std::clamp(v.x + c.delta.x, -89.f, 89.f);
                }
                CorrectMovement(pCmd, v, c.view);   // wish authored for c.view, re-expressed for v
            }
        };

        for (int i = 0; i < c.pre_ticks + c.air_ticks; ++i) {
            if (air < 0 && i >= c.pre_ticks)
                break;
            if ((c.budget && c.budget->expired()) || c.sims >= c.max_sims) {
                r.complete = false;
                c.out_of_budget = true;
                break;
            }

            const bool ground_in = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
            aim(i, ground_in, view);

            const int press = buttons(p, i, ground_in, air, c.jb_tick, c.lj_hold);
            pCmd->buttons = (c.buttons & ~(IN_JUMP | IN_DUCK)) | press;

            tick_t rec;
            rec.buttons = press;
            rec.z       = EntV3(pLocal, g_offOrigin).z;
            rec.ground  = ground_in;
            r.ticks.push_back(rec);

            sample_t s;
            s.z_in    = EntV3(pLocal, g_offOrigin).z;
            s.vz_in   = EntV3(pLocal, g_offVelocity).z;
            s.hull_in = EntV3(pLocal, g_offMaxs).z;
            const bool ducked_in = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;

            Pred_Simulate(pLocal, pCmd);
            ++c.sims;

            s.z      = EntV3(pLocal, g_offOrigin).z;
            s.x      = EntV3(pLocal, g_offOrigin).x;
            s.y      = EntV3(pLocal, g_offOrigin).y;
            s.vz     = EntV3(pLocal, g_offVelocity).z;
            s.hull   = EntV3(pLocal, g_offMaxs).z;
            s.ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
            s.ground_in = ground_in;

            if (air < 0) {
                if (!s.ground && s.vz > 0.0 && s.vz > s.vz_in + 100.0) {
                    air = 0;
                    r.launch = i;
                } else {
                    if (s.ground && !ground_in && r.landing < 0) r.landing = i;
                    if (s.ground && ground_in) break;
                    if (c.kind != arrival_bang && !s.ground && s.vz < 0.0 && s.z < c.lowest - c.prune_below) break;
                    continue;
                }
            } else
                ++air;

            if (targets.empty())
                break;

            s.clipped = s.vz_in > 0.0 && cssm::PinnedVz((float)s.vz, (float)-pin, 0.01f);
            for (int t = 0; t < static_cast<int>(targets.size()); ++t) {
                if (dead >> t & 1u)
                    continue;
                r.gap = (std::min)(r.gap, near_gap(c.kind, targets[t], s, h));
                if (arrives(c.kind, targets[t], s, h)) {
                    if (c.kind == arrival_surf) {
                        r.lip = surf_lip(pLocal, pCmd, c, p, i, press);
                        replay_to(pLocal, pCmd, c, p, i + 1, view);   // undo the probe, restore "after tick i"
                        if (r.lip == 0) {
                            dead |= 1u << t;
                            if (r.nolip < 0) {
                                r.nolip        = i;
                                r.nolip_target = t;
                                r.nolip_z      = static_cast<float>(s.z);
                            }
                            continue;
                        }
                    }
                    r.hit     = true;
                    r.exact   = true;
                    r.target  = t;
                    r.arrival = i;
                    r.arrive_z = static_cast<float>(s.z);
                    return r;
                }
            }
            if (s.ground || past(c.kind, c.lowest, s))
                break;
            // land: the float step below has no xy, the engine flies the whole arc
            if (full || c.kind == arrival_land)
                continue;

            const double dv      = s.vz_in - s.vz;
            const bool ducked    = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
            const bool wants     = (buttons(p, i + 1, false, air + 1, c.jb_tick, c.lj_hold) & IN_DUCK) != 0;
            const bool free_step = s.hull == s.hull_in && ducked == ducked_in && std::fabs(dv - (h + h_end)) < 1e-3 &&
                                   std::fabs(s.z - (s.z_in + (s.vz_in - h) * dt)) < 1e-3;
            const bool settled   = free_step && free_prev && air >= settle_air(p, c.lj_hold) && ducked == wants;
            free_prev = free_step;
            if (!settled)
                continue;

            float zf = static_cast<float>(s.z), vf = static_cast<float>(s.vz);
            const float off = s.hull * 0.5f;
            for (int j = i + 1; j < c.pre_ticks + c.air_ticks; ++j) {
                sample_t f;
                f.z_in    = zf;
                f.vz_in   = vf;
                f.hull_in = f.hull = s.hull;
                vf                = static_cast<float>(static_cast<double>(vf) - h);
                const float end   = zf + frametime * vf;
                const float delta = end - zf;
                const float start = (zf + off) - off;
                zf                = start + delta;
                vf                = static_cast<float>(static_cast<double>(vf) - h_end);
                f.z  = zf;
                f.vz = vf;
                for (int t = 0; t < static_cast<int>(targets.size()); ++t) {
                    if (dead >> t & 1u)
                        continue;
                    r.gap = (std::min)(r.gap, near_gap(c.kind, targets[t], f, h));
                    if (arrives(c.kind, targets[t], f, h)) {
                        r.hit      = true;
                        r.lip      = -1;   // botox: the analytic fast-forward branch never probes the lip
                        r.target   = t;
                        r.arrival  = j;
                        r.arrive_z = zf;
                        return r;
                    }
                }
                if (past(c.kind, c.lowest, f))
                    break;
            }
            break;
        }
        r.end_z      = EntV3(pLocal, g_offOrigin).z;
        r.end_ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        return r;
    }

    struct plan_t {
        bool active = false;
        program_t program{};
        int start_tick = 0;
        int launch = -1, arrival = -1;
        int point  = -1;
        float target_z = 0.f, arrive_z = 0.f;
        int unpinned = 0;   // arrival_surf's ride phase: consecutive un-pinned ticks past arrival
        std::vector<tick_t> ticks;

        void clear()
        {
            active   = false;
            unpinned = 0;
            ticks.clear();
        }
        void take(const program_t& p, run_t& r, const int tick_count, const int cand, const float target)
        {
            active     = true;
            unpinned   = 0;
            program    = p;
            start_tick = tick_count;
            launch     = r.launch;
            arrival    = r.arrival;
            point      = cand;
            target_z   = target;
            arrive_z   = r.arrive_z;
            ticks      = std::move(r.ticks);
        }
    };

    inline float start_dz(void* pLocal, const plan_t& plan, const int k)
    {
        return EntV3(pLocal, g_offOrigin).z - plan.ticks[k].z;
    }

    inline bool on_track(void* pLocal, const plan_t& plan, const int k)
    {
        if (!pLocal || k < 0 || k >= static_cast<int>(plan.ticks.size()))
            return false;
        const bool ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        return ground == plan.ticks[k].ground && std::fabs(start_dz(pLocal, plan, k)) < 0.25f;
    }
}
