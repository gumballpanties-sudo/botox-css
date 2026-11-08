// airstuck.inl -- textually included by movement.cpp right after texturebug.inl (same TU). Air stuck
// rewritten 11-07 off css-community gamemovement.cpp (AIR STUCK block below); it uses texturebug.inl's
// shared engine-search helpers (AT_*). The top half is infra other files of this TU use: AsSetOrigin
// (autobounce.inl), AsClock (autobounce.inl), AsSurfaceFriction (movement_edge.inl, TB, WC), AS_Sync
// (cl_pred_optimize arm for every exact-float pin feature: as + tb + wc).
#include "airstuck_pure.h"
#include <cfloat>
#include <cstring>

int Aim_LastCommandAck();   // aim/aim.cpp

static void AsSetOrigin(void* pLocal, const Vec3& o)
{
    if (g_fnSetAbsOrigin) g_fnSetAbsOrigin(pLocal, o);
    EntSetV3(pLocal, g_offOrigin, o);
}
// CInput::m_pCommands[seq % 90], the sent cmd for a command number (log only)
static const UserCmd_t* AsUserCmd(int seq)
{
    if (!g_input || seq < 0) return nullptr;
    const UserCmd_t* pCmds = *reinterpret_cast<UserCmd_t**>(reinterpret_cast<uintptr_t>(g_input) + 264);
    if (!pCmds) return nullptr;
    const UserCmd_t* c = pCmds + (seq % 90);
    return c->command_number == seq ? c : nullptr;
}

// botox n_tick::c_sim_budget: a wall-clock slice of one engine tick, never past the cmd clock
struct AsClock
{
    long long freq = 1ll, start = 0ll, span = 0ll;
    static long long Now() { LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart; }
    void Start(float fraction, float interval)
    {
        LARGE_INTEGER f{}; QueryPerformanceFrequency(&f);
        freq  = f.QuadPart > 0 ? f.QuadPart : 1ll;
        start = Now();
        span  = (long long)((double)freq * (double)interval * (double)std::clamp(fraction, 0.02f, 0.9f));
        const long long left = TbBudgetLeftUs() * freq / 1000000ll;
        if (span > left) span = left;
    }
    long long UsedUs() const { return (Now() - start) * 1000000ll / freq; }
    bool Expired() const { return Now() - start >= span; }
};

// m_surfaceFriction is a prediction datamap field (c_baseplayer.cpp:409), not a netvar
static int AsDatamapOffset(datamap_t* map, const char* name)
{
    constexpr int FIELD_EMBEDDED = 10;
    for (; map; map = map->baseMap)
    {
        for (int i = 0; i < map->dataNumFields; ++i)
        {
            const typedescription_t& td = map->dataDesc[i];
            if (td.fieldName && strcmp(td.fieldName, name) == 0) return td.fieldOffset[0];
            if (td.fieldType == FIELD_EMBEDDED && td.td)
            {
                const int o = AsDatamapOffset(td.td, name);
                if (o >= 0) return td.fieldOffset[0] + o;
            }
        }
    }
    return -1;
}
static float AsSurfaceFriction(void* pLocal)
{
    static int s_off = -2;   // -2 unresolved, -1 missing
    if (s_off == -2)
    {
        datamap_t* map = EntPredDescMap(pLocal);
        s_off = map ? AsDatamapOffset(map, "m_surfaceFriction") : -1;
        DebugLog("AS: m_surfaceFriction off=%d", s_off);
    }
    if (s_off <= 0) return 1.f;
    const float f = EntF(pLocal, s_off);
    return (f >= 0.1f && f <= 1.f) ? f : 1.f;   /* datamap miss reads garbage: never trust it */
}
// sim START velocity: SetupMove reads GetAbsVelocity and the CSS SetAbsVelocity sig is null (log setvel=0),
// so write m_vecAbsVelocity too (pred datamap field, c_baseentity.cpp:497; botox dirties EFL_DIRTY_ABSVELOCITY)
static void AsSetVelocity(void* pLocal, const Vec3& v)
{
    static int s_off = -2;
    if (s_off == -2)
    {
        datamap_t* map = EntPredDescMap(pLocal);
        s_off = map ? AsDatamapOffset(map, "m_vecAbsVelocity") : -1;
        DebugLog("AS: m_vecAbsVelocity off=%d", s_off);
    }
    EntSetV3(pLocal, g_offVelocity, v);
    if (s_off > 0) EntSetV3(pLocal, s_off, v);
}

/* online the engine keeps its own START while the server's END differs < 0.0625u / 0.5u/s ( cl_pred_optimize 2, c_baseentity.cpp:656/681,
   prediction.cpp:1681 ); pins are exact floats, so armed = full re-predict off the server snapshot every packet ( offline already is ).
   log: server END of the acked cmd vs the START we built on */
static void AS_SyncArm(const bool on)
{
    static int saved = -1;
    static bool armed = false;
    if (!g_cvar) return;
    ConVar* cv = g_cvar->FindVar("cl_pred_optimize");
    if (!cv) return;
    if (on && !armed)
    {
        saved = (int)cv->GetFloat();
        cv->SetValueInt("cl_pred_optimize", (std::min)(saved, 1));
        DebugLog("PRED: sync arm owner=as opt=%d->%d", saved, (int)cv->GetFloat());
        armed = true;
    }
    else if (!on && armed)
    {
        if (saved >= 0) cv->SetValueInt("cl_pred_optimize", saved);
        saved = -1;
        armed = false;
    }
}

static void AS_Sync(void* pLocal, UserCmd_t* cmd, const bool armed)
{
    AS_SyncArm(armed || s_wcSyncWant || s_tbSyncWant || s_phSyncWant);   // botox sync_arm owners: as + tb + wc (+ ph) ( all sub-ulp: pins are exact floats )

    struct rec_t {
        int cmd = -1, tick_base = 0;
        Vec3 origin{}, velocity{};
    };
    static rec_t ring[128];
    static int last_ack = -1, ok = 0;
    if (!armed) {
        last_ack = -1;
        return;
    }
    const Vec3 origin   = EntV3(pLocal, g_offOrigin);
    const Vec3 velocity = EntV3(pLocal, g_offVelocity);
    const int tick_base = EntI(pLocal, g_offTickBase);
    const int prev      = cmd->command_number - 1;
    ring[prev & 127]    = { prev, tick_base, origin, velocity };

    const int ack = Aim_LastCommandAck();
    if (ack < 0 || ack == last_ack || PredCommandsPredicted() <= 0 || ring[ack & 127].cmd != ack)
        return;
    last_ack = ack;

    RestoreToFrame(-1);
    const Vec3 sv_origin   = EntV3(pLocal, g_offOrigin);
    const Vec3 sv_velocity = EntV3(pLocal, g_offVelocity);
    const int sv_tick_base = EntI(pLocal, g_offTickBase);
    RestoreToFrame(PredCommandsPredicted() - 1);
    AsSetOrigin(pLocal, origin);
    EntSetV3(pLocal, g_offVelocity, velocity);
    EntSetI(pLocal, g_offTickBase, tick_base);

    const Vec3 d  = sv_origin - ring[ack & 127].origin;
    const Vec3 dv = sv_velocity - ring[ack & 127].velocity;
    if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f && dv.x == 0.0f && dv.y == 0.0f && dv.z == 0.0f) {
        ++ok;
        return;
    }
    /* same cmd as the one before + dtb 0 + a pinned START = server float noise; dtb != 0 = server ran an extra / null cmd ( player.cpp:3603 ) */
    const UserCmd_t* const a = AsUserCmd(ack);
    const UserCmd_t* const b = AsUserCmd(ack - 1);
    const bool same_cmd = a && b && a->viewangles.x == b->viewangles.x && a->viewangles.y == b->viewangles.y
                       && a->forwardmove == b->forwardmove && a->sidemove == b->sidemove && a->buttons == b->buttons;
    DebugLog("AS: sync miss ack=%d ok=%d d=%.3g,%.3g,%.3g dv=%.3g,%.3g,%.3g vz=%.3f cmd=%.4f/%.4f v=%.4f/%.4f b=%x same=%d dtb=%d",
             ack, ok, d.x, d.y, d.z, dv.x, dv.y, dv.z, sv_velocity.z,
             a ? a->forwardmove : 0.f, a ? a->sidemove : 0.f, a ? a->viewangles.x : 0.f, a ? a->viewangles.y : 0.f,
             a ? a->buttons : 0, (int)same_cmd, sv_tick_base - ring[ack & 127].tick_base);
    ok = 0;
}

// ============================================================================
// AIR STUCK -- 11-08b: botox air_stuck.cpp "lumi" core (csgo-sdk hacks/movement/air_stuck.cpp:628-868).
// 11-08a ran the ref wall stuck search here: 0 catches in game (its wishes are 450/10 at 0/45/80 deg off
// the face and it only writes on a found pin). Lumi's stuck lives elsewhere: a wish almost PARALLEL to the face
// (78..90 deg off straight-in, turned with your drift) at 1..120 u/s, pinned = END vz on -h (0.05, nudge-aware
// IsTargetVz), ranked by how many ticks the cmd keeps pinning (repeat = fixed point = held for good), then by the
// smallest gap. The best cmd is SENT every tick even unpinned: that approach steers the hull into the catch.
//   hold    START pinned by last tick's 3+ pin -> replay it, 1 sim proves it
//   resend  START pinned, nothing re-pins 3+ -> resend last tick's cmd (botox :379, server pins our sim misses)
// Wall: 0.05u hull contact first (the face we touch), else 16-heading x 1u line column, reach hull edge + 0.05..2.
// 11-08d: slanted (non-axis) wall in contact runs botox's clarity pass first (cardinal creep, see the block).
// ============================================================================
static bool s_asHolding = false;   // a 3+ tick pin (or its hold / resend) went out this cmd
static struct
{
    bool  hold = false; float yaw = 0.f, move = 0.f;                   // s_hold
    int   lastCmd = -1; float lastYaw = 0.f, lastMove = 0.f;          // s_last
    float startAng = 0.f;
    bool  latched = false; int len = 0;
    int   sims = 0, holds = 0, pins = 0, steers = 0, resends = 0, nowall = 0;
} s_as;

static void AirStuck(void* pLocal, UserCmd_t* cmd)
{
    s_asHolding = false;
    const bool on = g_airStuck && MovementKeyOK(g_airStuckKey, g_airStuckMode) && EntAlive(pLocal);
    AS_Sync(pLocal, cmd, on);
    {
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != s_sec)
        {
            s_sec = sec;
            if (g_debugLogEnable && (s_as.holds || s_as.pins || s_as.steers || s_as.resends || s_as.nowall))
                DebugLog("AS: holds=%d pins=%d steers=%d resends=%d nowall=%d sims=%d", s_as.holds, s_as.pins, s_as.steers,
                         s_as.resends, s_as.nowall, s_as.sims);
            s_as.holds = s_as.pins = s_as.steers = s_as.resends = s_as.nowall = s_as.sims = 0;
        }
    }
    const auto end = [&](const char* why) {
        if (s_as.latched && g_asScreenMessage) DebugLog("AS: unstuck why=%s held=%d z=%.3f", why, s_as.len, s_prePredOrigin.z);
        s_as.latched = false; s_as.len = 0; s_as.hold = false;
    };
    // pixel hop's land is this cmd (ref: ph runs before wall stuck, a land ends the tick)
    if (!on || (s_prePredFlags & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK || s_pixelHopHit)
    {
        end(!on ? "key" : s_pixelHopHit ? "ph" : "ground");
        return;
    }

    const int   nBase   = PredCommandsPredicted();
    const float dt      = TickInterval();
    const Vec3  origin  = s_prePredOrigin;
    const Vec3  vel     = s_prePredVel;
    const float viewYaw = cmd->viewangles.y;
    const UserCmd_t live = *cmd;
    RestoreToFrame(nBase - 1);
    const Vec3  mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float extX = (std::max)(fabsf(mins.x), fabsf(maxs.x)), extY = (std::max)(fabsf(mins.y), fabsf(maxs.y));
    const bool  startPinned = IsTargetVz(vel.z, 0.05f);

    const auto pinnedNow = [&]() {
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK
            && IsTargetVz(EntV3(pLocal, g_offVelocity).z, 0.05f);
    };
    // whole velocity zeroed (TryPlayerMove allsolid / "will become stuck" re-trace / allFraction 0, sdk
    // gamemovement.cpp:2627/2647/2794) then FinishGravity = pin exact; a clip leaves 1/32 xy -> 1/16 band
    const auto stuckNow = [&]() {
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK
            && fabsf(v.x) <= 0.0625f && fabsf(v.y) <= 0.0625f && IsTargetVz(v.z, 0.05f);
    };
    const auto cmdFor = [&](float yaw, float move) {
        UserCmd_t c = live;
        we::WishToCmd(yaw, move, viewYaw, c.forwardmove, c.sidemove);
        c.upmove = 0.f;
        c.buttons &= ~(IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
        return c;
    };
    // ticks the cmd keeps pinning from START (k_chain = fixed point), -1 = no sim budget
    constexpr int k_chain = 16;
    const auto chain = [&](const UserCmd_t& c, Vec3* endOut) -> int {
        RestoreToFrame(nBase - 1);
        if (!AT_CanSim()) return -1;
        UserCmd_t c0 = c;
        Pred_Simulate(pLocal, &c0);
        ++s_as.sims;
        int ticks = 0;
        if (pinnedNow())
        {
            ticks = 1;
            for (int k = 1; k < k_chain && AT_CanSim(); ++k)
            {
                const Vec3 from = EntV3(pLocal, g_offOrigin), fromV = EntV3(pLocal, g_offVelocity);
                UserCmd_t cc = c;
                Pred_Simulate(pLocal, &cc);
                ++s_as.sims;
                if (!pinnedNow()) break;
                ++ticks;
                const Vec3 o = EntV3(pLocal, g_offOrigin), v = EntV3(pLocal, g_offVelocity);
                if (o.x == from.x && o.y == from.y && o.z == from.z && v.x == fromV.x && v.y == fromV.y && v.z == fromV.z)
                    { ticks = k_chain; break; }
            }
        }
        if (endOut) *endOut = EntV3(pLocal, g_offOrigin);
        return ticks;
    };
    const auto send = [&](float yaw, float move, bool pin, const char* what) {
        *cmd = cmdFor(yaw, move);
        AT_Stamp(*cmd, "as");
        s_as.lastCmd = cmd->command_number; s_as.lastYaw = yaw; s_as.lastMove = move;
        s_as.hold = pin; s_as.yaw = yaw; s_as.move = move;
        s_asHolding = pin;
        if (pin && !s_as.latched && g_asScreenMessage)
            DebugLog("AS: stuck by=%s yaw=%.2f fwd=%.3f z=%.3f vz=%.2f xy=%.2f", what, yaw, move, origin.z, vel.z, vel.Length2D());
        if (pin) { s_as.latched = true; ++s_as.len; }
        else if (s_as.latched) end("lost");
    };
    // botox :380 resend: START pinned, the cmd that pinned it beats any unproven move
    const auto resend = [&]() {
        if (!startPinned || s_as.lastCmd != cmd->command_number - 1) return false;
        ++s_as.resends;
        send(s_as.lastYaw, s_as.lastMove, true, "resend");
        return true;
    };

    // ---- hold: last tick's 3+ pin, START still pinned -> replay, 1 sim proves it (botox :312) ----
    if (s_as.hold && startPinned)
    {
        RestoreToFrame(nBase - 1);
        if (AT_CanSim())
        {
            UserCmd_t c = cmdFor(s_as.yaw, s_as.move);
            Pred_Simulate(pLocal, &c);
            ++s_as.sims;
            const bool held = pinnedNow();
            RestoreToFrame(nBase - 1);
            if (held) { ++s_as.holds; send(s_as.yaw, s_as.move, true, "hold"); return; }
            DebugLog("AS: hold break t=%d yaw=%.1f fwd=%.4f", s_as.len, s_as.yaw, s_as.move);
        }
    }

    // ---- wall: 0.05u hull contact, else 16-heading line column (botox :404-424, :628-683) ----
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    bool found = false, disp = false;
    Vec3 n{};
    for (int i = 0; i < 4 && !found; ++i)
    {
        const float a = (float)(i * 90) * 0.017453292f;
        GameTrace_t tr{};
        TraceHull(origin, Vec3(origin.x + cosf(a) * 0.05f, origin.y + sinf(a) * 0.05f, origin.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.DidHit() && fabsf(tr.plane.normal.z) <= 0.35f) { found = true; n = tr.plane.normal; disp = tr.dispFlags != 0; }
    }
    if (found) s_as.startAng = atan2f(-n.y, -n.x);

    // ---- wedge (CSS, not botox): press INTO the face. Lumi (78..90 deg off straight-in) and clarity (cardinal
    // creep <= 14 fwd) never try it; AA's straight 450 press is what wedged the hull dead on a slanted face
    // (11-08 AA log vz=-6.00 xy=0.0, proj-aa-hold-finder v3). Each cmd repeats k_wdAhead ticks: stuck now and
    // 2+ ticks = pin (armed, hold replays it); else the soonest stuck tick ahead = approach, sent unarmed instead
    // of park / lumi's blind steer. Rank: stuck now > sooner > longer.
    // 11-08e log: wedge catches (vz=-6.00 xy=0.00) died next tick (`resend` -> `lost held=1`): from a stuck START
    // nothing re-stuck. So on a stuck START: full circle (36 yaws x 0/1/5/30/450), 1 tick ahead, hits chained to
    // k_chain, and ANY re-stick goes out armed (a 1-tick re-stick each tick = held; AA saw that loop)
    struct { bool valid = false; int eta = 0, len = 0; float yaw = 0.f, move = 0.f; } wd;
    if (found)
    {
        const int   wdAhead = startPinned ? 1 : 6, wdLen = startPinned ? k_chain : 4;
        const float inYaw   = RAD2DEGF(atan2f(-n.y, -n.x));
        int wdSims = 0, wdCands = 0;
        AsClock wc;
        wc.Start(std::clamp(g_asBudget, 5.f, 90.f) * 0.01f * (startPinned ? 0.6f : 0.35f), dt);
        static constexpr float k_relMove[]   = { 0.f, 15.f, -15.f, 35.f, -35.f, 60.f, -60.f };
        static constexpr float k_movesMove[] = { 450.f, 30.f, 5.f };
        static constexpr float k_movesPin[]  = { 0.f, 1.f, 5.f, 30.f, 450.f };
        const auto tryWedge = [&](float rel, float move) {
            ++wdCands;
            const float yaw = we::NormYaw(inYaw + rel);
            const UserCmd_t c = cmdFor(yaw, move);
            RestoreToFrame(nBase - 1);
            int eta = 0, len = 0;
            for (int t = 1; t <= wdAhead + wdLen && AT_CanSim(); ++t)
            {
                const Vec3 from = EntV3(pLocal, g_offOrigin);
                UserCmd_t cc = c;
                Pred_Simulate(pLocal, &cc);
                ++wdSims;
                if (EntI(pLocal, g_offFlags) & FL_ONGROUND) break;
                if (stuckNow())
                {
                    if (!eta) eta = t;
                    ++len;
                    const Vec3 o = EntV3(pLocal, g_offOrigin);
                    if (len > 1 && o.x == from.x && o.y == from.y && o.z == from.z) len = wdLen;   // fixed point
                    if (len >= wdLen) break;
                }
                else if (eta) break;           // run ended
                else if (t >= wdAhead) break;  // nothing ahead
            }
            if (!eta) return;
            const bool better = !wd.valid || (eta == 1) > (wd.eta == 1) || ((eta == 1) == (wd.eta == 1)
                              && (eta < wd.eta || (eta == wd.eta && len > wd.len)));
            if (better) wd = { true, eta, len, yaw, move };
        };
        const auto stop = [&]() { return (wdCands > 0 && wc.Expired()) || !AT_CanSim() || (wd.valid && wd.eta == 1 && wd.len >= wdLen); };
        if (startPinned)
        {
            for (const float move : k_movesPin)
                for (float rel = 0.f; rel < 360.f && !stop(); rel += 10.f)
                {
                    tryWedge(rel, move);
                    if (move == 0.f) break;   // zero move = view-free, one yaw
                }
        }
        else
            for (const float move : k_movesMove)
                for (const float rel : k_relMove)
                    if (!stop()) tryWedge(rel, move);
        RestoreToFrame(nBase - 1);
        s_as.sims += wdSims;
        static int s_wdLog = -1000;
        if (g_debugLogEnable && (cmd->command_number - s_wdLog > 16 || (wd.valid && !s_as.latched)))
        {
            s_wdLog = cmd->command_number;
            DebugLog("AS: wedge %s n=%.3f,%.3f eta=%d len=%d yaw=%.1f rel=%.0f fwd=%.0f cand=%d sims=%d vz=%.2f xy=%.2f us=%lld",
                     wd.valid ? "hit" : "miss", n.x, n.y, wd.eta, wd.len, wd.yaw, we::NormYaw(wd.yaw - inYaw), wd.move, wdCands,
                     wdSims, vel.z, vel.Length2D(), wc.UsedUs());
        }
        if (wd.valid && wd.eta == 1 && (wd.len >= 2 || startPinned)) { ++s_as.pins; send(wd.yaw, wd.move, true, "wedge"); return; }
    }

    // ---- clarity (botox :395-625): slanted wall in hull contact. 11-08 log wall n=0.831,0.557: lumi's tangent
    // fan (tuned on axis brushes) read pin=0 every tick; botox runs clarity first there and this port had dropped it.
    // Creep the two cardinal yaws around the face in 1/32 u/s wishvel steps (as::GridStep), a pin must last 6+ ticks;
    // ties -> robust to 1 ulp of END origin (2 ticks) then of START (crob, 1 tick). No pin + gap > 0.01 = park:
    // the straight-in press that closes on the face most without bouncing, sent unarmed.
    // Near-axis (< 10 deg off) moving START: the pin waits for lumi, used only when lumi finds none (botox :395)
    // 11-08e log: from a moving START clarity read np=0 on every line and ate ~200 sims (lumi cand=0); its one pin
    // came off a stuck START (t=16, held 28) -> runs on a stuck START only (botox's own data: moving-START pins 0/9)
    struct { bool valid = false; float yaw = 0.f, move = 0.f; } clDefer;
    if (found && startPinned && fabsf(n.z) < 1e-4f && !as::Clarity(n.x, n.y).axis)
    {
        const as::ClarityYaws cy = as::Clarity(n.x, n.y);
        const float clWallYaw = RAD2DEGF(atan2f(-n.y, -n.x));
        const float pinVz     = TargetPredictZVel();
        const auto setState = [&](const Vec3& o, const Vec3& v) { AsSetOrigin(pLocal, o); AsSetVelocity(pLocal, v); };
        AsClock cl;   // botox: whole slider on a pinned START, 0.4 near-axis, else 0.6 (lumi keeps the rest)
        cl.Start(std::clamp(g_asBudget, 5.f, 90.f) * 0.01f * (startPinned ? 1.f : cy.nearAxis ? 0.4f : 0.6f), dt);
        const float duckMul = (live.buttons & IN_DUCK) ? 0.33333333f : 1.f;   // CSS duck move frac, gamemovement.cpp:4296
        const float step = as::GridStep(CvarFloat("sv_airaccelerate", 10.f), dt, AsSurfaceFriction(pLocal), duckMul);

        constexpr int k_rob = 6, k_crob = 12;
        constexpr int k_rank = k_crob * (k_rob + 1) + k_rob + 1;   // one more pinned tick outranks any robustness
        constexpr int k_best = (k_chain + 1) * k_rank - 1;
        int clSims = 0, clScore = -1, clTicks = 0, clPins = 0, clRob = -1, clCrob = -1;
        float clYaw = 0.f, clFwd = 0.f;
        // END +-1 ulp per origin axis, velocity on the pin, 2 ticks each
        const auto robust = [&](const UserCmd_t& c) {
            const Vec3 base = EntV3(pLocal, g_offOrigin);
            int r = 0;
            for (int k = 0; k < k_rob && AT_CanSim(); ++k)
            {
                Vec3 o = base;
                float& a = k < 2 ? o.x : k < 4 ? o.y : o.z;
                a = nextafterf(a, (k & 1) ? FLT_MAX : -FLT_MAX);
                setState(o, Vec3(0.f, 0.f, pinVz));
                bool held = true;
                for (int t = 0; t < 2 && held && AT_CanSim(); ++t)
                {
                    UserCmd_t cc = c;
                    Pred_Simulate(pLocal, &cc);
                    ++clSims;
                    held = stuckNow();
                }
                r += held;
            }
            return r;
        };
        // START +-1 ulp per origin / velocity axis, 1 tick: server rounding != ours. Early out once it can't beat `need`
        const auto catchRob = [&](const UserCmd_t& c, int need) {
            int r = 0;
            for (int k = 0; k < k_crob && r + k_crob - k > need && !(k > 0 && cl.Expired()) && AT_CanSim(); ++k)
            {
                Vec3 o = origin, v = vel;
                Vec3& w = k < 6 ? o : v;
                const int ax = (k % 6) >> 1;
                float& a = ax == 0 ? w.x : ax == 1 ? w.y : w.z;
                a = nextafterf(a, (k & 1) ? FLT_MAX : -FLT_MAX);
                RestoreToFrame(nBase - 1);
                setState(o, v);
                UserCmd_t cc = c;
                Pred_Simulate(pLocal, &cc);
                ++clSims;
                r += stuckNow();
            }
            return r;
        };
        // i = 0 once: a zero move pins view-free, wins ties
        for (int y = cy.first; y < 2 && clScore < k_best; ++y)
            for (int i = y == cy.first ? 0 : 1; i <= 100 && clScore < k_best && !(clSims > 0 && cl.Expired()) && AT_CanSim(); ++i)
            {
                const float fwd = (float)i * step;
                const UserCmd_t c = cmdFor(cy.yaws[y], fwd);
                RestoreToFrame(nBase - 1);
                int ticks = 0;
                for (int t = 0; t < k_chain && AT_CanSim(); ++t)
                {
                    const Vec3 from = EntV3(pLocal, g_offOrigin);
                    UserCmd_t cc = c;
                    Pred_Simulate(pLocal, &cc);
                    ++clSims;
                    if (!stuckNow()) break;
                    ++ticks;
                    const Vec3 o = EntV3(pLocal, g_offOrigin);
                    if (t > 0 && o.x == from.x && o.y == from.y && o.z == from.z) { ticks = k_chain; break; }
                }
                if (ticks < 6) continue;
                ++clPins;
                const int base = ticks * k_rank;
                if (base + k_rank - 1 <= clScore) continue;
                const int rob = robust(c);
                if (base + k_crob * (k_rob + 1) + rob <= clScore) continue;
                const int rem   = clScore - base;
                const int crob  = catchRob(c, rem < rob ? -1 : (rem - rob) / (k_rob + 1));
                const int score = base + crob * (k_rob + 1) + rob;
                if (score > clScore) { clScore = score; clTicks = ticks; clRob = rob; clCrob = crob; clYaw = cy.yaws[y]; clFwd = fwd; }
            }

        bool  clParked = false;
        float parkFwd = 0.f, parkGap = -1.f;
        if (clScore < 0 && !disp)
        {
            const auto hullHits = [&](float d) {
                GameTrace_t tr{};
                TraceHull(origin, Vec3(origin.x - n.x * d, origin.y - n.y * d, origin.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
                return tr.DidHit() || tr.startsolid;
            };
            if (hullHits(0.05f))
            {
                float lo = 0.f, hi = 0.05f;
                for (int i = 0; i < 24; ++i) { const float mid = (lo + hi) * 0.5f; (hullHits(mid) ? hi : lo) = mid; }
                parkGap = lo;
            }
            if (parkGap > 0.01f)
            {
                const float startD = origin.x * n.x + origin.y * n.y;
                float lo = 1.17549435e-38f, hi = step * 100.f, bestD = FLT_MAX;
                bool bestPinned = false;
                for (int i = 0; i < 16 && AT_CanSim(); ++i)
                {
                    const float mid = (lo + hi) * 0.5f;
                    RestoreToFrame(nBase - 1);
                    UserCmd_t cc = cmdFor(clWallYaw, mid);
                    Pred_Simulate(pLocal, &cc);
                    ++clSims;
                    const Vec3  o    = EntV3(pLocal, g_offOrigin);
                    const float d    = o.x * n.x + o.y * n.y;
                    const bool  back = startD <= d;
                    if (!back && d < bestD && !(EntI(pLocal, g_offFlags) & FL_ONGROUND))
                        { bestD = d; bestPinned = stuckNow(); parkFwd = mid; clParked = true; }
                    (back ? hi : lo) = mid;
                }
                if (bestPinned) clParked = false;
            }
        }
        RestoreToFrame(nBase - 1);
        s_as.sims += clSims;

        static int s_clLog = -1000;
        if (g_debugLogEnable && (cmd->command_number - s_clLog > 16 || (clScore >= 0 && !s_as.latched)))
        {
            s_clLog = cmd->command_number;
            DebugLog("AS: clarity %s n=%.3f,%.3f off=%.1f yaw=%.0f fwd=%.4f sims=%d vz=%.3f vxy=%.2f gap=%.5f t=%d rob=%d crob=%d np=%d disp=%d us=%lld",
                     clScore >= 0 ? "pin" : clParked ? "park" : "miss", n.x, n.y, cy.off, clParked ? clWallYaw : clYaw,
                     clParked ? parkFwd : clFwd, clSims, vel.z, vel.Length2D(), parkGap, clTicks, clRob, clCrob, clPins, (int)disp, cl.UsedUs());
        }
        if (clScore >= 0)
        {
            if (cy.nearAxis && !startPinned) clDefer = { true, clYaw, clFwd };
            else { ++s_as.pins; send(clYaw, clFwd, true, "clarity"); return; }
        }
        // park is unpinned: a pinned START is never pushed off its pin
        if (clParked && !startPinned && !wd.valid) { ++s_as.steers; send(clWallYaw, parkFwd, false, "park"); return; }
    }
    const float extra = std::clamp(g_asExtraReach, 0.f, 32.f);
    for (float a = s_as.startAng; !found && a < s_as.startAng + 6.2831853f; a += 0.3926991f)
    {
        const float c = cosf(a), s = sinf(a);
        const float reach = (std::min)(extX / (std::max)(fabsf(c), 1e-6f), extY / (std::max)(fabsf(s), 1e-6f)) + std::clamp(extra, 0.05f, 2.f);
        float bestFrac = 1.f;
        for (float z = origin.z + mins.z; z <= origin.z + maxs.z; z += 1.f)
        {
            GameTrace_t tr{};
            TraceLine(Vec3(origin.x, origin.y, z), Vec3(origin.x + c * reach, origin.y + s * reach, z), MASK_PLAYERSOLID, &fil, &tr);
            const Vec3& tn = tr.plane.normal;
            if (tr.fraction < 1.f && fabsf(tn.z) <= 0.35f && tn.x * c + tn.y * s <= -0.1f && tr.fraction < bestFrac)
                { found = true; bestFrac = tr.fraction; n = tn; }
        }
        if (found) s_as.startAng = a;
    }
    if (!found)
    {
        ++s_as.nowall;
        if (!resend()) { s_as.startAng = 0.f; end("nowall"); }
        RestoreToFrame(nBase - 1);
        return;
    }

    // ---- lumi fan (botox :735-838) ----
    const float wallYaw = RAD2DEGF(atan2f(-n.y, -n.x));
    float drift = fmodf(RAD2DEGF(atan2f(vel.y, vel.x)) - wallYaw + 180.f, 360.f);
    if (drift < 0.f) drift += 360.f;
    const float side = drift < 180.f ? 1.f : -1.f;
    const float nl = sqrtf(n.x * n.x + n.y * n.y);
    const Vec3  n2 = nl > 1e-4f ? Vec3(n.x / nl, n.y / nl, 0.f) : Vec3(1.f, 0.f, 0.f);
    const float support = fabsf(n2.x) * extX + fabsf(n2.y) * extY;
    TraceFilterWorldOnly wfil; wfil.Setup();
    AsClock clock;   // after clarity: AsClock caps at what is left of the cmd's budget
    clock.Start(std::clamp(g_asBudget, 5.f, 90.f) * 0.01f, dt);
    int cands = 0, bestTicks = 0;
    float bestGap = FLT_MAX, bestYaw = wallYaw, bestMove = 0.f;
    const auto outOfTime = [&]() { return cands >= 512 || (cands > 0 && clock.Expired()) || !AT_CanSim(); };
    const auto tryMove = [&](float rel, float move, float gapLen) {
        ++cands;
        const float yaw = we::NormYaw(wallYaw + rel * side);
        Vec3 e{};
        const int ticks = chain(cmdFor(yaw, move), &e);
        if (ticks < 0) return;
        GameTrace_t tr{};
        TraceLine(e, Vec3(e.x - n2.x * gapLen, e.y - n2.y * gapLen, e.z), 0xFFFFFFFFu, &wfil, &tr);
        const float gap = tr.fraction * gapLen - support;
        if (ticks > bestTicks || (ticks == bestTicks && gap < bestGap))
            { bestTicks = ticks; bestGap = gap; bestYaw = yaw; bestMove = move; }
    };
    for (const float move : { 1.f, 5.f, 30.f })
        for (float rel = 85.999f; rel < 90.f && !outOfTime(); rel += 1.f) tryMove(rel, move, 32.f);
    if (bestTicks < k_chain)
        for (const float move : { 1.f, 5.f, 30.f, 60.f, 90.f, 120.f })
            for (float rel = 78.f; rel < 90.f && !outOfTime(); rel += 0.5f) tryMove(rel, move, 64.f);
    RestoreToFrame(nBase - 1);

    static int s_logCmd = -1000;
    if (g_debugLogEnable && cmd->command_number - s_logCmd > 16)
    {
        s_logCmd = cmd->command_number;
        DebugLog("AS: lumi n=%.3f,%.3f,%.3f side=%.0f pin=%d gap=%.3f yaw=%.1f fwd=%.0f cand=%d vz=%.2f xy=%.1f", n.x, n.y, n.z, side,
                 bestTicks, bestGap, bestYaw, bestMove, cands, vel.z, vel.Length2D());
    }
    // lumi found no pin at all: the deferred near-axis clarity pin beats an approach move (botox :848)
    if (bestTicks < 1 && clDefer.valid) { ++s_as.pins; send(clDefer.yaw, clDefer.move, true, "clarity defer"); return; }
    if (bestTicks >= 3) { ++s_as.pins; send(bestYaw, bestMove, true, "lumi"); return; }
    if (resend()) return;
    // approach: the best unpinned / short-pin cmd still goes out, unarmed (botox :862)
    ++s_as.steers;
    if (wd.valid) { send(wd.yaw, wd.move, false, "wedge approach"); return; }   // proven stuck ahead > blind steer
    send(bestYaw, bestMove, false, "steer");
}

// did AS write the cmd this tick (every later mover yields on s_airStuckHit)
static void AirStuckTick(void* pLocal, UserCmd_t* pCmd)
{
    const UserCmd_t pre = *pCmd;
    AirStuck(pLocal, pCmd);
    s_airStuckHit = s_asHolding || pCmd->forwardmove != pre.forwardmove || pCmd->sidemove != pre.sidemove
                 || pCmd->buttons != pre.buttons || pCmd->viewangles.x != pre.viewangles.x || pCmd->viewangles.y != pre.viewangles.y;
    // real stuck for chat/points: pin sent AND START (server result of last cmd) pinned, 2 ticks running.
    // s_airStuckHit is also true on steer/approach = any jump near a wall
    static int s_asStuckTicks = 0;
    s_asStuckTicks = (s_asHolding && IsTargetVz(s_prePredVel.z, 0.05f)) ? s_asStuckTicks + 1 : 0;
    s_airStuckReal = s_asStuckTicks >= 2;
}

// last writer (Movement_AirStuckLastWriter, after the aimbot): the cmd TB / AS proved this tick goes
// out as proven. "use movement fix" on = your final view stays (silent aim keeps its shot) and the
// proven world wish is turned into it, which can cost a pin its float bits; off = the proven view and
// move both go out verbatim
static void AS_FinalStamp(UserCmd_t* cmd)
{
    if (!cmd || s_atStamp.cmd != cmd->command_number) return;
    s_atStamp.cmd = -1;
    constexpr int mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_DUCK | IN_JUMP;
    const bool viewSame = cmd->viewangles.x == s_atStamp.view.x && cmd->viewangles.y == s_atStamp.view.y;
    const bool same = viewSame && cmd->forwardmove == s_atStamp.fwd && cmd->sidemove == s_atStamp.side
                   && (cmd->buttons & mask) == (s_atStamp.buttons & mask);
    if (same) return;
    if (g_asMovementFix && !viewSame)
        as::StampRotate(cmd->viewangles.y, s_atStamp.view.y, s_atStamp.fwd, s_atStamp.side, cmd->forwardmove, cmd->sidemove);
    else
    {
        cmd->viewangles  = s_atStamp.view;
        cmd->forwardmove = s_atStamp.fwd;
        cmd->sidemove    = s_atStamp.side;
    }
    cmd->upmove  = 0.f;
    cmd->buttons = (cmd->buttons & ~mask) | (s_atStamp.buttons & mask);
    static int s_last = -1000;
    if (g_asScreenMessage && cmd->command_number - s_last > 66)
    {
        s_last = cmd->command_number;
        DebugLog("AS: stamp %s fixed a later writer (view %s)", s_atStamp.who, viewSame ? "same" : g_asMovementFix ? "kept, move rotated" : "forced");
    }
}
