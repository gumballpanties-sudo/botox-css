#pragma once
// ============================================================================
//  routesim.h -- route calculator core. CSS vertical movement, tick for tick, no engine.
//
//  Z-only port of CGameMovement (css-community-master game/shared/gamemovement.cpp) with
//  CCSGameMovement's cstrike overrides (game/shared/cstrike/cs_gamemovement.cpp). Horizontal
//  is the player's job; every height, duck rule and ground rule below is the engine's own
//  line, same float/double order:
//    PlayerMove          gamemovement.cpp:4549  ReduceTimers, vz>250 unground (:4591), Duck, FullWalkMove
//    ReduceTimers        cs_gamemovement.cpp:572 m_flStamina -= 1000*frametime, then :1091 m_flDucktime
//    Duck                cs_gamemovement.cpp:860  CSS OWN Duck(): no DuckOverrides, no strip rules, no
//                        "FL_DUCKING stamped for free" under a ceiling (that is TF2 gamemovement.cpp)
//    FinishDuck/UnDuck   cs_gamemovement.cpp:820 / :782  +-0.5*(62-45) = 8.5 in air, CategorizePosition
//    CanUnduck           cs_gamemovement.cpp:750  STANDING hull swept 8.5 down in air, 0 on the ground
//    Start/FinishGravity gamemovement.cpp:1248 / :1684  0.5 is a double literal in both
//    CheckJumpButton     cs_gamemovement.cpp:593  NO ducked refusal (:634-640 only air / pogo);
//                        ducking|FL_DUCKING = SET sqrt(2*800*57.0) (double), else ADD; then
//                        *= stamina ratio (:683), m_flStamina = 25/19*1000 ms (:692), FinishGravity
//    FullWalkMove        gamemovement.cpp:2025  gravity, jump, move, categorize, gravity
//    CategorizePosition  gamemovement.cpp:3774  2u trace; Categorize() below folds WalkMove's StayOnGround
//                        (:1859, up 2 / down stepsize 18, snap over 0.5*COORD_RESOLUTION) into the same
//                        call for an already-grounded hull -- two engine functions, one outcome
//    SetGroundEntity     gamemovement.cpp:3591  vz 0
//    ClipVelocity        live x64 client adds normal * DIST_EPSILON to every clipped velocity (not in
//                        css-community source, log-proven: pinned surfs read vz -5.97, not -6) --
//                        a floor/seam pin ends on -halfgrav + 1/32, a ceiling clip on -halfgrav - 1/32
//  Traces: a plane stops a hull DIST_EPSILON (1/32) off it. So a floor is kept as floorEff =
//  plane + 1/32 (the z a landing leaves the origin at, and what a crosshair marker already
//  stores) and a ceiling as ceilEff = plane - 1/32.
//
//  Validated by tests/routesim_test.cpp against the recorded hightower hops (launch ladder
//  +0.705/+0.72, longjump 7/7 from -254.53, 0/13 lower, jump 0/37) and in game by the solve's
//  own Pred_Simulate cross-check (routecalc.inl RcEngineCheck).
// ============================================================================
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace rs
{
constexpr float  kDistEps     = 0.03125f;  // DIST_EPSILON
constexpr float  kStepSize    = 18.f;      // sv_stepsize, movevars_shared.cpp:94
constexpr float  kNonJumpVz   = 140.f;     // NON_JUMP_VELOCITY, gamemovement.cpp:3810 (CategorizePosition's "moving up rapidly")
constexpr float  kOptMoveUnground = 250.f; // PlayerMove's sv_optimizedmovement shortcut, gamemovement.cpp:4591: a grounded
                                            // hull with vz > 250 skips the start-of-tick CategorizePosition and is ungrounded
constexpr float  kDuckMs      = 1000.f;    // GAMEMOVEMENT_DUCK_TIME
constexpr double kTimeToDuck  = 0.4;       // TIME_TO_DUCK, shareddefs.h:109 (non-TF branch), double literal
constexpr double kTimeToUnduck = 0.2;      // TIME_TO_UNDUCK, shareddefs.h:112
constexpr int    kAirDuckMax  = 2;         // TF_AIRDUCKED_COUNT -- CS has no air-duck-count limit, see Params::clampAirDucks
// pixel surf catch, pixelsurf_assist check(): d = seam - z on a falling tick, d in (-0.01, 0.03).
// Recorded: catches at d -0.003 / +0.012, misses at 0.10+ (routesim_test).
constexpr float  kSurfLo      = -0.01f;
constexpr float  kSurfHi      = 0.03f;
constexpr float  kInf         = 1e9f;
constexpr float  kJumpMul     = 301.9933774f;  // (float)sqrt(2*800*57.0) = 301.99337741..., CCSGameMovement::
                                                // CheckJumpButton cs_gamemovement.cpp:676/680. CS, NOT TF2's ~289.
                                                // The engine does the add in DOUBLE (JumpImpulse below); this
                                                // float is the divisor routecalc.inl's engine check calibrates on
inline double JumpImpulse() { return std::sqrt(2.0 * 800.0 * 57.0); }   // :676 `sqrt(2 * 800 * 57.0)`, a double
// stamina, cs_gamemovement.cpp:21-24. m_flStamina is MILLISECONDS left (set to 25/19*1000 by a jump,
// -1000*frametime a tick); the jump ratio is (100 - ms/1000*19)/100 in double (:687)
constexpr double kStaminaMax      = 100.0;
constexpr double kStaminaCostJump = 25.0;
constexpr double kStaminaRecover  = 19.0;
inline double StaminaRatio(float ms) { return (kStaminaMax - (((double)ms / 1000.0) * kStaminaRecover)) / kStaminaMax; }
inline float  StaminaAfterJump()    { return (float)((kStaminaCostJump / kStaminaRecover) * 1000.0); }   // 1315.789
// CSS JumpBug (movement.cpp JbEarliestFire k_jbHorizon): "duck from now, fire on tick k", k = 0..6
constexpr int    kJbHorizon   = 6;

struct Params
{
    float dt        = 0.015f;   // interval_per_tick
    float gravity   = 800.f;    // sv_gravity, movevars_shared.cpp:37 (DEFAULT_GRAVITY_STRING)
    float jumpMod   = 1.f;      // surface jumpFactor (CheckJumpButton flGroundFactor); 1 = stock. Stamina is
                                // NOT folded in here any more: State::stamina carries it tick by tick
    float scale     = 1.f;      // model scale (hull 62/45, the 8.5u air duck shift)
    float maxVel    = 3500.f;   // sv_maxvelocity, movevars_shared.cpp:93
    float cropFrac  = 1.f;      // PreventBunnyJumping on a bhop press, 1 = under 1.2x maxspeed
    bool  clampAirDucks = false; // TF-only (tf_clamp_airducks): re-duck cooldown + air-duck-count cap.
                                 // CS's own Duck()/CheckJumpButton (cs_gamemovement.cpp:593,860) has
                                 // neither, so this stays off for CS. Left wired for a future TF toggle.
    int   duckTimerTicks = 20;  // TF-only re-duck cooldown; dead while clampAirDucks is off (see above)
    // dih features the route leans on (routecalc.inl reads them live)
    bool  pixelSurf   = true;   // PixelSurf on: its duck pass, its hold, its latch
    bool  jbYieldSurf = true;   // g_jbYieldSurf: AutoJumpbug idles while a ride is latched
    int   psHorizon   = 8;      // g_pixelSurfPredictTicks: the hunt's look-ahead
    // botox m_route_calc_delay_ticks: grounded cmds a delay hop waits before its press. 0 = no
    // delay hop is flown (it would be the bhop's own arc under another name)
    int   delayTicks  = 0;
};

struct State
{
    float z = 0.f, vz = 0.f;
    bool  ground = false;
    bool  fDuck = false;        // FL_DUCKING
    bool  ducking = false;      // m_bDucking
    bool  ducked = false;       // m_bDucked (picks the hull)
    float duckTime = 0.f;       // m_flDucktime, ms
    bool  oldDuck = false;      // m_nOldButtons & IN_DUCK
    bool  oldJump = false;      // m_nOldButtons & IN_JUMP
    int   airDucked = 0;        // m_nAirDucked (TF-only counter, kept for the TF toggle; CS never strips on it)
    float stamina = 0.f;        // CCSPlayer::m_flStamina, ms left (0 = fresh). A jump sets 1315.8, -15 a tick
    int   tick = 0;
    int   releaseTick = INT_MIN / 2;  // last duck release (m_flDuckTimer = curtime + 0.3)
    bool  edgy = false;         // a duck timer decision sat on its float boundary
    // ---- not engine state: dih's own feature memory, as the next cmd will read it ----
    // who holds IN_DUCK: bits 1 the player's duck key (crouch walk off), 2 PixelSurf
    // (s_psAirDuckHeld, OR'd after prediction until the feet touch or a duck-free prediction
    // lands, movement.cpp:3209 / 3823), 4 MiniJump (s_mjHoldDuck, OR'd after prediction until
    // room / landing, movement.cpp:1238)
    int   hold = 0;
    int   psAge = 17;           // cmds since PixelSurf's pin; < 17 = s_shouldPixelSurf still latched
    bool  psRiding = false;     // s_psRidingNow as the last cmd left it
    bool  psPrevSurf = false;   // s_prevSurfVel
    bool  onSeam = false;       // just caught a pixelsurf: the ride's length is the player's
};

struct World
{
    bool  floor = false;  float floorEff = 0.f;
    bool  ceil = false;   float ceilEff = 0.f;
    bool  floorPlayer = false;  // the floor is a player: dih's traces (TraceFilterWorldProps) skip it
};

struct Cmd { bool jump = false, duck = false; };

struct TickInfo
{
    float moveVz = 0.f;         // vz the move ran with
    bool  jumped = false;       // CheckJumpButton launched off the ground
    bool  duckStripped = false; // IN_DUCK held, DuckOverrides removed it
    bool  duckedNow = false;    // FinishDuck ran
    bool  unducked = false;     // FinishUnDuck ran
    bool  landHit = false;      // the move stopped on the floor
    bool  ceilHit = false;
    // z the STANCE teleported this tick (+-8.5 * scale, air only). It is not flight, it is
    // the hull changing size around a fixed centre, and drawing it puts a cliff in the arc
    float stanceDz = 0.f;
};

// one tick of a replayed leg, as the DRAWN LINE needs it (Solver::TraceBest). z is in its own
// vector because that is the answer's own arc; this is the pair of facts a line needs to place it
struct ArcTick
{
    bool  air = false;       // the hull was off the ground: this tick carries horizontal distance
    float stanceDz = 0.f;    // the stance teleport inside this tick's z (see TickInfo::stanceDz)
};

// ---- engine pieces ----------------------------------------------------------------------------
inline void CheckVelocity(State& s, const Params& p)
{
    if (s.vz > p.maxVel) s.vz = p.maxVel;
    else if (s.vz < -p.maxVel) s.vz = -p.maxVel;
}
inline void StartGravity(State& s, const Params& p)
{
    // ent_gravity * GetCurrentGravity() * 0.5 * frametime -- float*float, then double
    const double d = (double)(1.f * p.gravity) * 0.5 * (double)p.dt;
    s.vz = (float)((double)s.vz - d);
    CheckVelocity(s, p);
}
inline void FinishGravity(State& s, const Params& p)
{
    // ent_gravity * GetCurrentGravity() * frametime * 0.5 -- three floats, then double
    const float gdt = (1.f * p.gravity) * p.dt;
    s.vz = (float)((double)s.vz - (double)gdt * 0.5);
    CheckVelocity(s, p);
}
inline float StandH(const Params& p) { return 62.f * p.scale; }  // VEC_HULL_MAX.z, cs_gamerules.cpp:76
inline float DuckH(const Params& p)  { return 45.f * p.scale; }  // VEC_DUCK_HULL_MAX.z, cs_gamerules.cpp:79
inline float HullH(const State& s, const Params& p) { return s.ducked ? DuckH(p) : StandH(p); }
inline float DuckShift(const Params& p) { return StandH(p) - DuckH(p); }          // hull size difference, 17
// CSS FinishDuck/FinishUnDuck/CanUnduck (cs_gamemovement.cpp:826/800/767): the origin moves HALF the
// hull difference in the air -- 0.5f * (hullSizeNormal - hullSizeCrouch) = 8.5. Not TF2's full 20
inline float AirDuckShift(const Params& p) { return 0.5f * DuckShift(p); }
// the velocity a clip leaves: 0 along the normal plus the live client's DIST_EPSILON nudge, then the
// tick's own FinishGravity. up = a floor / seam under the hull (+1/32), else a ceiling over it (-1/32)
inline float ClipVz(bool up) { return up ? kDistEps : -kDistEps; }

inline void SetGround(State& s, bool on)
{
    if (on) { s.vz = 0.f; s.airDucked = 0; }
    s.ground = on;
}

// hull bottom at/over the plane top = over the floor; lower = beside the ledge
inline bool OverFloor(const State& s, const World& w) { return w.floor && s.z >= w.floorEff - 0.001f; }

inline void Categorize(State& s, const World& w)
{
    if (s.vz > kNonJumpVz) { SetGround(s, false); return; }
    const bool moveToEnd = s.ground;
    const float end = s.z - (2.f + (moveToEnd ? kStepSize : 0.f));
    if (OverFloor(s, w) && end < w.floorEff)
    {
        // StayOnGround subsumed: fraction > 0 && < 1, delta > 0.5 * COORD_RESOLUTION
        if (moveToEnd && s.z > w.floorEff && std::fabs(s.z - w.floorEff) > 0.015625)
            s.z = w.floorEff;
        SetGround(s, true);
    }
    else
        SetGround(s, false);
}

// CCSGameMovement::CanUnduck (:750): the STANDING hull traced from the origin to origin + offset,
// offset = 0 on the ground (VEC_DUCK_HULL_MIN - VEC_HULL_MIN, both 0 in CS) and -8.5 in the air.
// startsolid or fraction != 1 refuses. Standing hull at the origin under a ceiling = startsolid
inline bool CanUnduck(const State& s, const World& w, const Params& p)
{
    if (w.ceil && s.z + StandH(p) > w.ceilEff + kDistEps) return false;   // standing hull startsolid
    if (s.ground) return true;
    const float to = s.z - AirDuckShift(p);
    return !(OverFloor(s, w) && to < w.floorEff);                          // the 8.5 sweep hits the floor
}

// CCSGameMovement::FinishDuck (:820): FL_DUCKING + m_bDucking off every call; the origin moves (air:
// +8.5, ground: 0) only on the FIRST (!m_bDucked), a re-run while ducked moves nothing. No early
// return on FL_DUCKING (TF2's base FinishDuck has one, CSS's own does not)
inline void FinishDuck(State& s, const World& w, const Params& p, TickInfo& ti)
{
    s.fDuck = true; s.ducking = false;
    if (!s.ducked)
    {
        if (!s.ground) { s.z = s.z + AirDuckShift(p); ti.stanceDz += AirDuckShift(p); }
        s.ducked = true;
        ti.duckedNow = true;
    }
    Categorize(s, w);
}

// CCSGameMovement::FinishUnDuck (:782): air -8.5, ground 0; every duck flag off, m_flDucktime 0
inline void FinishUnDuck(State& s, const World& w, const Params& p, TickInfo& ti)
{
    if (!s.ground) { s.z = s.z - AirDuckShift(p); ti.stanceDz -= AirDuckShift(p); }
    s.ducked = false; s.fDuck = false; s.ducking = false; s.duckTime = 0.f;
    ti.unducked = true;
    Categorize(s, w);
}

inline void OnDuck(State& s, bool pressed, const World& w, const Params& p, TickInfo& ti)
{
    const bool inAir = !s.ground, inDuck = s.fDuck;
    if (pressed && !inDuck) { s.duckTime = kDuckMs; s.ducking = true; }
    if (s.ducking)
    {
        const float ms = std::max(0.f, kDuckMs - s.duckTime);
        const float sec = ms * 0.001f;
        if ((double)sec > kTimeToDuck || inDuck || inAir) FinishDuck(s, w, p, ti);
    }
}

// CCSGameMovement::Duck (:971-1012), the IN_DUCK-up branch. CSS's own: a release while FL_DUCKING
// restarts the 1000 ms timer and sets m_bDucking; CanUnduck -> (m_bDucking || m_bDucked) ->
// FinishUnDuck when > TIME_TO_UNDUCK or airborne; CanUnduck false -> m_flDucktime = 1000 and NOTHING
// else (TF2's "stamp FL_DUCKING for free" lives in gamemovement.cpp's base Duck, which CSS overrides).
// So a duck pressed on the takeoff tick and let go (MiniJump) rides m_bDucking into the air and
// FinishUnDuck drops the origin 8.5 the first tick the standing hull has 8.5 of room under it
inline void OnUnDuck(State& s, bool released, const World& w, const Params& p, TickInfo& ti)
{
    const bool inAir = !s.ground;
    if (released)
    {
        s.releaseTick = s.tick;                       // TF-only bookkeeping (clampAirDucks), harmless
        if (inAir) s.airDucked++;
        if (s.fDuck) { s.duckTime = kDuckMs; s.ducking = true; }
    }
    if (CanUnduck(s, w, p))
    {
        if (s.ducking || s.ducked)
        {
            const float ms = std::max(0.f, kDuckMs - s.duckTime);
            const float sec = ms * 0.001f;
            if ((double)sec > kTimeToUnduck || inAir) FinishUnDuck(s, w, p, ti);
        }
    }
    else
        s.duckTime = kDuckMs;
}

inline void Duck(State& s, Cmd& c, const World& w, const Params& p, TickInfo& ti)
{
    if (p.clampAirDucks && c.duck)
    {
        bool strip = false, edge = false;
        if (s.ground)
        {
            const int since = s.tick - s.releaseTick;
            if (since < p.duckTimerTicks) strip = true;
            else if (since == p.duckTimerTicks) { strip = true; edge = true; }
        }
        if (s.ducked && s.ducking) { strip = true; edge = false; }
        if (!s.ground && s.airDucked >= kAirDuckMax) { strip = true; edge = false; }
        if (strip) { c.duck = false; ti.duckStripped = true; if (edge) s.edgy = true; }
    }
    const bool pressed  = c.duck && !s.oldDuck;
    const bool released = !c.duck && s.oldDuck;
    s.oldDuck = c.duck;
    if (c.duck || s.ducking || s.fDuck)
    {
        if (c.duck) OnDuck(s, pressed, w, p, ti);
        else        OnUnDuck(s, released, w, p, ti);
    }
}

// CCSGameMovement::CheckJumpButton (:593), the walking-player path. CSS refuses only in the air
// (:634, latches IN_JUMP) and on the pogo latch (:640). A DUCKED hull jumps: m_bDucking or
// FL_DUCKING picks the SET branch (:667, vz = impulse, StartGravity's -halfgrav thrown away), a
// standing one ADDs (:680). Then the stamina ratio (:683) and the stamina cost (:692), FinishGravity
inline bool CheckJump(State& s, const Params& p, TickInfo& ti)
{
    if (!s.ground) { s.oldJump = true; return false; }
    if (s.oldJump) return false;
    if (p.cropFrac < 1.f) s.vz *= p.cropFrac;
    SetGround(s, false);
    // flGroundFactor (float) * sqrt(2 * 800 * 57.0) (double) -> double, added to / stored into a float
    const double imp = (double)p.jumpMod * JumpImpulse();
    if (s.ducking || s.fDuck) s.vz = (float)imp;
    else                      s.vz = (float)((double)s.vz + imp);
    if (s.stamina > 0.f) s.vz = (float)((double)s.vz * StaminaRatio(s.stamina));
    s.stamina = StaminaAfterJump();
    FinishGravity(s, p);
    s.oldJump = true;
    ti.jumped = true;
    return true;
}

inline void AirMove(State& s, const World& w, const Params& p, TickInfo& ti)
{
    ti.moveVz = s.vz;
    const float zEnd = s.z + p.dt * s.vz;      // VectorMA
    if (s.vz < 0.f && OverFloor(s, w) && zEnd < w.floorEff)
    {
        s.z = w.floorEff; s.vz = 0.f; ti.landHit = true;      // ClipVelocity on the floor plane
        return;
    }
    const float H = HullH(s, p);
    if (s.vz > 0.f && w.ceil && s.z + H <= w.ceilEff + 0.001f && zEnd + H > w.ceilEff)
    {
        s.z = w.ceilEff - H; s.vz = ClipVz(false); ti.ceilHit = true;   // ClipVelocity on the ceiling plane
        return;
    }
    s.z = zEnd;
}

// one CCSGameMovement::ProcessMovement, z only
inline void Tick(State& s, Cmd c, const World& w, const Params& p, TickInfo* pti = nullptr)
{
    TickInfo ti;
    const float frameMs = 1000.0f * p.dt;
    if (s.stamina > 0.f) { s.stamina -= frameMs; if (s.stamina < 0.f) s.stamina = 0.f; }   // CCSGameMovement::ReduceTimers
    if (s.duckTime > 0.f) { s.duckTime -= frameMs; if (s.duckTime < 0.f) s.duckTime = 0.f; }
    if (s.vz > kOptMoveUnground) SetGround(s, false);    // sv_optimizedmovement, gamemovement.cpp:4591 (a literal 250, not NON_JUMP_VELOCITY)
    Duck(s, c, w, p, ti);
    StartGravity(s, p);
    if (c.jump)
        CheckJump(s, p, ti);
    else
        s.oldJump = false;
    CheckVelocity(s, p);
    if (s.ground) s.vz = 0.f;                            // WalkMove: z stays
    else          AirMove(s, w, p, ti);
    Categorize(s, w);
    FinishGravity(s, p);
    if (s.ground) s.vz = 0.f;
    CheckVelocity(s, p);
    // FinishMove / CPrediction::FinishMove (prediction.cpp:713): next tick's old buttons are THIS
    // tick's buttons, whatever CheckJumpButton did -- a refused press still latches IN_JUMP
    s.oldJump = c.jump;
    s.oldDuck = c.duck;           // post-DuckOverrides (Duck() stripped c.duck in place)
    s.tick++;
    if (pti) *pti = ti;
}

// ================================================================================================
//  MOVES -- only what a bind replays the same way every time. Each is dih's own feature, button
//  for button, so a printed name is a thing you can press:
//    jump             one press. Off a landing it is Bunnyhop's press on the first grounded tick --
//                     ducked or not (CSS CheckJumpButton takes a ducked jump: SET branch)
//    minijump         MiniJump (movement.cpp:1603): jump + duck on the takeoff cmd ONLY. m_bDucking
//                     rides into the air, FinishUnDuck drops the feet 8.5 the first tick the standing
//                     hull has room: a lower arc that never went up. ("hold crouch" = hold bit 4)
//    longjump         LongJump (movement.cpp:1554): jump + duck on the takeoff cmd and the next
//    jumpbug          JumpBug (movement.cpp:1122 JbEarliestFire), jump key held while falling: the
//                     earliest k in 0..6 where "duck held k cmds, then duck off + jump" ends the
//                     fire cmd airborne with vz > 0 (air FinishUnDuck -8.5 lands the 2u trace,
//                     CheckJumpButton fires off that fresh ground). Replaces the landing on a marker
//    walk off         start only: one grounded tick past the edge, then free fall
//    crouch walk off  start only: duck on the floor until FL_DUCKING, walk off holding it
//    delay hop        DelayHop (movement_ex.inl): off a STANDING landing, Params::delayTicks
//                     grounded cmds with jump stripped, then the press. The wait is StayOnGround's
//                     snap from the 2u hover to the floor, so it launches up to 2u under the bhop
//                     (botox route_engine: "delay hop = plain jump only, off a standing landing")
// ================================================================================================
enum Script { SC_JUMP = 0, SC_MINI, SC_LONG, SC_WALKOFF, SC_WALKOFFDUCK, SC_DELAY, SC_FALL, SC_COUNT };

// MB_DELAY is last on purpose: bits 0-5 keep the meaning every older build gave them
enum MoveBit { MB_JUMP = 0, MB_MINI, MB_LONG, MB_JUMPBUG, MB_WALKOFF, MB_CROUCHWALKOFF, MB_DELAY, MB_COUNT };
constexpr unsigned kMovesAll    = (1u << MB_COUNT) - 1u;
constexpr unsigned kMovesPress  = (1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_LONG) | (1u << MB_DELAY);   // off a floor
constexpr unsigned kMovesMarker = kMovesPress | (1u << MB_JUMPBUG);                       // per marker
constexpr unsigned kMovesStart  = (1u << MB_WALKOFF) | (1u << MB_CROUCHWALKOFF);          // start only

inline const char* MoveName(int b)
{
    static const char* k[MB_COUNT] = { "jump", "minijump", "longjump", "jumpbug", "walk off", "crouch walk off",
                                       "delay hop" };
    return (b >= 0 && b < MB_COUNT) ? k[b] : "";
}
// a delay hop's printed step names its wait, botox's "delay hop 2t": a wait you cannot see is a
// wait you cannot perform
inline std::string DelayName(int ticks) { return "delay hop " + std::to_string(ticks) + "t"; }
// the press itself: jump, minijump, longjump or a delay hop's late jump
inline bool PressScript(int sc) { return sc == SC_JUMP || sc == SC_MINI || sc == SC_LONG || sc == SC_DELAY; }

inline int ScriptBit(int sc)
{
    switch (sc)
    {
    case SC_JUMP:        return MB_JUMP;
    case SC_MINI:        return MB_MINI;
    case SC_LONG:        return MB_LONG;
    case SC_WALKOFF:     return MB_WALKOFF;
    case SC_WALKOFFDUCK: return MB_CROUCHWALKOFF;
    case SC_DELAY:       return MB_DELAY;
    default:             return -1;
    }
}

// kept for the callers' signatures (routecalc.inl RcEngineCheck): CSS MiniJump has no hold policy
// of its own any more, so nothing lives in here
struct ScriptMem { bool mjHold = false; };

inline Cmd ScriptCmd(int sc, int i, const State& s, ScriptMem&, const World&, const Params&)
{
    Cmd c;
    switch (sc)
    {
    case SC_JUMP:
    case SC_DELAY: c.jump = i == 0; break;                         // RunLeg already waited
    case SC_LONG: c.jump = i == 0; c.duck = i <= 1; break;          // LongJump: 2 duck ticks
    case SC_MINI: c.jump = c.duck = i == 0; break;                  // MiniJump: the takeoff cmd only
    case SC_WALKOFF:     break;
    case SC_WALKOFFDUCK: c.duck = true; break;
    case SC_FALL:        c.duck = (s.hold & 1) != 0; break;   // the player's own key; feature holds are post
        break;
    default: break;
    }
    return c;
}

// dih JumpBug (movement.cpp:1122, CSS rewrite 11-07) off the state the cmd starts on, written
// against this sim instead of Pred_Simulate. The live feature: jump key held, airborne + falling
// START -> JbEarliestFire flies "IN_DUCK held for k cmds (IN_JUMP stripped), then IN_DUCK off +
// IN_JUMP" for k = 0..kJbHorizon on the engine and takes the earliest k whose fire cmd ENDS
// airborne with vz > 0 (the air unduck's -8.5 puts the feet inside the 2u ground trace, Categorize
// grounds it, CheckJumpButton jumps off that same tick). k = 0 fires now, k > 0 ducks now. No k =
// the cmd is left alone (IN_JUMP still stripped in the air; the landing START hops instead).
// false = not armed, c untouched. true = it owns IN_DUCK and IN_JUMP this cmd; band = this is the
// fire cmd, margin = how deep inside the window the fire cmd's start height sits (the window is
// [8.5, 10.5) over floorEff, ducked: CanUnduck's sweep below it, the 2u trace above it).
// The prefix is flown once and every k fires off it: k+1 ticks per call, not k^2
inline bool JumpbugCmd(const State& s, const World& w, const Params& p, Cmd& c, bool& band, float& margin)
{
    band = false;
    if (s.ground || s.vz > 0.f || !OverFloor(s, w) || w.floorPlayer) return false;   // rising idles; traces skip players
    // the live reach trace: floor inside the horizon's fall chord + 12 (8.5 window + 2u trace)
    const float t = p.dt * (float)kJbHorizon;
    const float reach = -s.vz * t + 0.5f * p.gravity * t * t + 12.f;
    if (s.z - w.floorEff > reach) return false;
    State q = s;
    for (int k = 0; k <= kJbHorizon; k++)
    {
        State f = q; Cmd fire; fire.jump = true; fire.duck = false;
        const float dist = f.z - w.floorEff;
        Tick(f, fire, w, p);
        if (!f.ground && f.vz > 0.f)
        {
            if (k == 0) { c = fire; band = true; margin = std::min(dist - AirDuckShift(p), AirDuckShift(p) + 2.f - dist); }
            else        { c.duck = true; c.jump = false; }
            return true;
        }
        Cmd duck; duck.duck = true; duck.jump = false;
        Tick(q, duck, w, p);
        if (q.ground) return false;          // a wait tick that lands: every later k lands too
    }
    return false;
}

// ---- WILL THAT JUMPBUG ACTUALLY FIRE? --------------------------------------------------------
// Replay the policy against the sim before pressing anything: a fire cmd before the floor = arm
// it, none = hand the cmd back untouched. Pure, no traces and no engine sims -- w is the floor the
// caller already traced. The key is held the whole fall (that is what runs the feature), and the
// feature strips IN_JUMP off every airborne cmd but the fire (movement.cpp:1152), so no unarmed
// tick ever sends it: the fire is always a fresh press. jumpHeld is kept for the signature only.
inline bool JumpbugWillHit(State s, const World& w, const Params& p, bool /*jumpHeld*/,
                           int maxTicks = 64)
{
    for (int i = 0; i < maxTicks; i++)
    {
        if (s.ground) return false;           // landed, never fired
        Cmd c; bool band = false; float margin = 0.f;
        if (JumpbugCmd(s, w, p, c, band, margin) && band) return true;
        Tick(s, c, w, p);
    }
    return false;
}

// ================================================================================================
//  ROUTE -- markers in order, one element each:
//    ground     land on it, then jump / minijump / longjump -- or jumpbug off it, no landing
//    pixelsurf  caught on a falling tick in the window        "pixelsurf (ducked)"
//    edgebug    fall onto the ledge, slide off                 "edgebug (stand)"
//    headbang   head hits the ceiling on the way up            "headbang (stand)"
//    texturebug the hull's SIDE spans a wall seam and the clip freezes z  "texturebug (ducked)"
//  Search = layered DAG. A node is an exact engine state at a marker; two routes that arrive in
//  the same state share every continuation, so they merge and nothing is lost: counts add up
//  along edges (every combination is counted) and each node keeps its K cheapest paths.
// ================================================================================================
// new types go LAST: 0..3 are the numbers every saved marker file already holds
//    pixeljump  botox route_pt_pixeljump: a LIP on the wall face (an up face a hull pressed into
//               the wall lands on and a hull 1u out misses). A floor, but only for a hull that
//               has been over it kPjCreepTicks ticks (time to creep in over it), and hop only
//               off it: no delay, no walk off                          "pixeljump (stand)"
//    headbounce botox route_pt_headbounce: TWO faces in one marker, picked by Point::hbCeiling --
//               hbCeiling true is a flat ceiling and plays exactly like headbang (route_engine.cpp
//               point_step literally retypes it to route_pt_headbang before simulating; here the
//               marker keeps its own name, WorldFor/Arrive just share headbang's ceiling clip).
//               hbCeiling false is the "seam window" face: no ceiling exists to clip against, the
//               catch is the pre-tick HEAD rising into [z - kDistEps, z) with the duck state held
//               through the tick (route_engine.cpp:1977 hb_rise / hb_head). Finder-only (botox
//               pf_hb_is_ceiling classifies ceiling vs seam off its own ascent-catch scan; CSS's
//               routecalc.inl RcHeadbounceCeiling does the same off a real trace at placement --
//               never a menu row, botox has none either)         "headbounce (stand)"
enum PointType { PT_GROUND = 0, PT_SURF, PT_EDGEBUG, PT_HEADBANG, PT_TEXTUREBUG, PT_PIXELJUMP, PT_HEADBOUNCE, PT_COUNT };

inline const char* PointTypeName(int t)
{
    static const char* k[PT_COUNT] = { "ground", "pixelsurf", "edgebug", "headbang", "texturebug", "pixeljump",
                                        "headbounce" };
    return (t >= 0 && t < PT_COUNT) ? k[t] : "ground";
}
// the marker's z is a wall line the hull is held against, not a floor or a ceiling
inline bool SeamLike(int t) { return t == PT_SURF || t == PT_TEXTUREBUG; }
inline bool FloorLike(int t) { return t == PT_GROUND || t == PT_EDGEBUG || t == PT_PIXELJUMP; }
// a floor you land on and press off (its move list means something)
inline bool LandLike(int t) { return t == PT_GROUND || t == PT_PIXELJUMP; }
// exists only where the pixel finder found one: its z is a finder measurement, not a click
inline bool FinderOnly(int t) { return SeamLike(t) || t == PT_PIXELJUMP || t == PT_HEADBOUNCE; }
// botox route_engine.cpp k_pj_creep_ticks: ticks the feet must be over a lip before it is a floor
constexpr int kPjCreepTicks = 3;

// ---- AIMING AT A DRAWN POINT -------------------------------------------------------
// The pixel finder draws its results as rings, so "am I aiming at that ring" is an ANGLE
// question, not a distance one. perp / along IS the tangent of the angle between the aim ray and
// the point, which is exactly how far the ring sits from the crosshair ON SCREEN -- so ranking by
// it picks the ring you are looking at. Ranking by perpendicular distance (the old rule) does not:
// it hands a 3u-off point 100u away the same score as a 3u-off point 1000u away, though the far
// one is a tenth of the distance from the crosshair on screen and is plainly the one being aimed
// at.
// The window opens with distance so a ring stays grabbable across a map, with a floor so a whole
// column is still separable point blank and a cap so a far wall is not a net.
constexpr float kAimTan     = 0.0437f;   // tan(2.5 deg)
constexpr float kAimPerpMin = 2.f;       // point blank: 2u across
constexpr float kAimPerpMax = 64.f;      // far: the cone stops opening

// tan of the angle from the ray to p, or -1 when p is behind the eye or outside the window.
// vFwd must be unit length. V is anything with float x, y, z.
template <class V>
inline float AimTanTo(const V& vEye, const V& vFwd, const V& p)
{
    const float dx = p.x - vEye.x, dy = p.y - vEye.y, dz = p.z - vEye.z;
    const float along = dx * vFwd.x + dy * vFwd.y + dz * vFwd.z;
    if (along < 1.f) return -1.f;
    const float px = dx - vFwd.x * along, py = dy - vFwd.y * along, pz = dz - vFwd.z * along;
    const float perp = std::sqrt(px * px + py * py + pz * pz);
    const float win = std::min(std::max(along * kAimTan, kAimPerpMin), kAimPerpMax);
    return perp <= win ? perp / along : -1.f;
}

struct Point
{
    int   type = PT_GROUND;
    // floorEff (ground, edgebug), seam z (surf, texturebug), ceilEff (headbang, and headbounce
    // when hbCeiling), the seam window's top (headbounce when !hbCeiling)
    float z = 0.f;
    unsigned moves = kMovesMarker; // ground: presses after landing on it + whether it may be jumpbugged
    bool  allowStand = true, allowDuck = true;   // arrival stance (surf, edgebug, headbang, headbounce)
    bool  player = false;          // a player's head
    bool  hbCeiling = false;       // headbounce only: true = flat ceiling (plays as headbang), false = seam window
    // pixeljump only (botox route_point_t::pj_skin, route_calc.cpp pj_lip_kind): the lip is a prop
    // top flush with the wall, not a real ledge -- the catch settles kSkinUp higher than the pin.
    // kSkinLo/kSkinHi bound the botox band itself; PjFloorZ (below) is what WorldFor/Arrive use
    bool  pjSkin = false;
};
// UNVERIFIED css: botox k_skin_lo/hi (movement.h:216-217) are ~[1.969, 2.000] above the pin --
// here as one constant (the band's own width, 0.03ish, is inside Categorize's existing landing
// tolerance already, so one target height reproduces it close enough; a dedicated 2-edge band
// was not worth the second code path for a sub-kDistEps difference)
constexpr float kSkinUp = 2.f - kDistEps;
// the height WorldFor/Arrive actually treat as this point's floor: the pin itself, or -- a skin
// pixeljump only -- kSkinUp higher (the real surface the pin's wall-press settles onto)
inline float PjFloorZ(const Point& t) { return (t.type == PT_PIXELJUMP && t.pjSkin) ? t.z + kSkinUp : t.z; }

struct Start
{
    bool  air = false;          // continue the live air path (no floor under)
    State s;
    bool  hasFloor = true;      // standing start: the floor stood on
    float floorEff = 0.f;
    bool  player = false;      // what the start floor is
    unsigned moves = kMovesAll; // start marker's own filter (ANDed with Options::moves)
};

// a floor is a floor to the engine; only the printed name says whose head
inline const char* OnName(bool player)
{
    return player ? " off player" : "";
}
// "jump off player" (not "walk off off player")
// a combo's press sequence: its "ride N ticks" steps dropped (how long a ride lasts is the
// player's, like the horizontal, not a press)
inline std::string StripRide(const std::string& text)
{
    std::string out;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t next = text.find(" -> ", pos);
        if (next == std::string::npos) next = text.size();
        const std::string step = text.substr(pos, next - pos);
        if (step.compare(0, 5, "ride ") != 0) out += (out.empty() ? "" : " -> ") + step;
        pos = next + 4;
    }
    return out;
}

inline std::string WithOn(const std::string& move, const char* on)
{
    if (!*on) return move;
    const bool endsOff = move.size() >= 4 && move.compare(move.size() - 4, 4, " off") == 0;
    return move + (endsOff ? on + 4 : on);
}

// ---- marker file line (routecalc.inl) --------------------------------------------------------
// "x y z type moves enabled snapped stand duck player hbCeiling". rc18 = written by +;
// older lines use an earlier build's 6 types / 13 move bits (8 bits, same first six) and migrate.
// hbCeiling is appended LAST and read only when present (n >= 11): an old line has no opinion on
// it, and PT_HEADBOUNCE itself is appended after PT_PIXELJUMP, so neither change renumbers a type
// an old saved marker already holds.
struct MarkerLine
{
    float x = 0.f, y = 0.f, z = 0.f;
    int   type = PT_GROUND;
    unsigned moves = kMovesMarker;
    bool  enabled = true, snapped = false, stand = true, duck = true, player = false;
    bool  hbCeiling = false;
};
inline bool ParseMarkerLine(const char* line, bool rc18, MarkerLine& m)
{
    int type = 0, en = 1, snap = 0, st = 1, dk = 1, pl = 0, hbc = 0;
    unsigned mask = kMovesMarker;
    const int n = std::sscanf(line, "%f %f %f %d %u %d %d %d %d %d %d", &m.x, &m.y, &m.z, &type, &mask,
                              &en, &snap, &st, &dk, &pl, &hbc);
    if (n < 3) return false;
    m.enabled  = n >= 6 ? en != 0 : true;
    m.snapped  = n >= 7 ? snap != 0 : false;
    m.stand    = n >= 8 ? st != 0 : true;
    m.duck     = n >= 9 ? dk != 0 : true;
    m.player   = n >= 10 ? pl != 0 : false;
    m.hbCeiling = n >= 11 ? hbc != 0 : false;
    if (!m.stand && !m.duck) m.stand = m.duck = true;
    if (rc18)
    {
        m.type  = (type >= 0 && type < PT_COUNT) ? type : PT_GROUND;
        m.moves = n >= 5 ? (mask & kMovesMarker) : kMovesMarker;
    }
    else
    {
        // old bits: 0 jump, 1 crouch jump (-> jump), 2 minijump, 3 minijump hold, 4 longjump, 5 longjump hold
        m.moves = kMovesMarker;
        if (n >= 5)
            m.moves = ((mask & 0x03) ? (1u << MB_JUMP) : 0u) | ((mask & 0x0C) ? (1u << MB_MINI) : 0u)
                    | ((mask & 0x30) ? (1u << MB_LONG) : 0u) | (1u << MB_JUMPBUG);
        if (type == 4)      { m.type = PT_GROUND; m.moves = 1u << MB_JUMPBUG; }          // jumpbug marker
        else if (type == 5)                                                              // pixelbounce
        {
            m.type = PT_GROUND;
            m.moves = (m.stand ? (1u << MB_JUMP) : 0u) | (m.duck ? (1u << MB_JUMPBUG) : 0u);
        }
        else m.type = (type >= 0 && type < PT_COUNT) ? type : PT_GROUND;
    }
    if (!m.moves) m.moves = kMovesMarker;
    return true;
}

// a hull standing still on a floor: settled, nothing held, every timer long gone
inline State RestState(float floorEff)
{
    State s;
    s.z = floorEff; s.ground = true;
    return s;
}

struct Options
{
    unsigned moves    = kMovesAll;
    int    topK       = 32;
    size_t maxNodes   = 60000;     // per marker: distinct states
    size_t maxEdges   = 400000;    // per marker: incoming edges (unbounded even with maxNodes hit --
                                    // many arrivals can merge into one already-seen state)
    int    maxLegTicks = 400;
};

// presses / switches: binds hit, and how often two presses in a row are different binds. The list
// is sorted on those two first, so they are what "easiest at the top" means
struct Combo { std::string text; int cost = 0; float margin = kInf; int presses = 0, switches = 0; };

struct Result
{
    std::vector<Combo> combos;
    double total = 0.0;
    std::vector<size_t> layerNodes;  // states that arrived at marker i
    int    deadPoint = -1;           // first marker nothing arrived at
    std::vector<float> closest;      // per marker (surf): nearest falling tick to the window
    // ...and WHICH WAY it missed, as the z shift that would have put that same tick inside the
    // window (+ = the seam wanted higher, - = lower). `closest` is a distance and a distance
    // cannot say which side of the 0.04u window the hull went past, which is exactly what a
    // "put it here instead" answer needs. Same tick, same arc: a surf marker is not a floor
    // (WorldFor gives it none), so moving its z does not move the flight that missed it
    std::vector<float> closestDz;
    std::vector<float> highest;      // per marker: highest feet (head, headbang) got over its z
    bool   capped = false;
    bool   timedOut = false;         // worker hit its wall-clock ceiling before Finish() ran
    long long ticks = 0;
};

class Solver
{
public:
    void Begin(const std::vector<Point>& pts, const Start& st, const Params& p, const Options& o)
    {
        m_pts = pts; m_start = st; m_p = p; m_o = o;
        m_res = Result();
        m_res.closest.assign(pts.size(), kInf);
        m_res.closestDz.assign(pts.size(), 0.f);
        m_res.highest.assign(pts.size(), -kInf);
        m_labels.clear(); m_labelIx.clear();
        m_layers.assign(1, {}); m_edges.assign(1, {}); m_keys.assign(1, {});
        Node n;
        n.s = st.s;
        n.best.push_back({ 0, kInf, -1, -1, 0, 0, -1 });
        n.count = 1.0;
        m_layers[0].push_back(n);
        m_layer = 0; m_next = 0;
        m_done = false;
        if (m_pts.empty()) Finish();
    }

    // expand up to nNodes states; true when finished
    bool Step(int nNodes)
    {
        while (!m_done && nNodes-- > 0)
        {
            if ((int)m_layers.size() == m_layer + 1)
            {
                m_layers.emplace_back(); m_edges.emplace_back(); m_keys.emplace_back();
            }
            if (m_next < m_layers[m_layer].size())
                Expand(m_layer, (int)m_next++);
            if (m_next >= m_layers[m_layer].size())
            {
                m_res.layerNodes.push_back(m_layers[m_layer + 1].size());
                m_keys[m_layer + 1].clear();
                m_layer++; m_next = 0;
                if (m_layer >= (int)m_pts.size() || m_layers[m_layer].empty()) Finish();
            }
        }
        return m_done;
    }

    float Progress() const
    {
        if (m_done || m_pts.empty()) return 1.f;
        const size_t n = m_layers[m_layer].size();
        return ((float)m_layer + (n ? (float)m_next / (float)n : 0.f)) / (float)m_pts.size();
    }
    const Result& Out() const { return m_res; }
    bool Done() const { return m_done; }
    // the worker's wall-clock ceiling, not a search outcome: stop wherever Step() left off and
    // report it as a timeout, not a dead marker (Finish() never ran, so deadPoint stays -1)
    void ForceTimeout() { m_res.timedOut = true; m_done = true; }

private:
    // presses / switches / lastBind: how HARD the path is to play, carried along it. A press is a
    // bind you hit (jump, minijump, longjump, walk off, crouch walk off, jumpbug); a switch is two
    // presses in a row on DIFFERENT binds. The panel leads with the easiest, so the search keeps the
    // easiest K per node too: "jump -> longjump -> longjump" over "longjump -> jump -> longjump",
    // "minijump x3" over "longjump -> jumpbug -> jump". cost/margin only break ties now
    struct Best { int cost; float margin; int edge; int rank; int presses; int switches; int lastBind; };
    static bool Easier(int pA, int sA, int cA, float mA, int pB, int sB, int cB, float mB)
    {
        if (pA != pB) return pA < pB;
        if (sA != sB) return sA < sB;
        if (cA != cB) return cA < cB;
        return mA > mB;
    }
    // one more bind pressed on a path: its count, its switch count and the bind now held
    static void Press(int bind, int& presses, int& switches, int& last)
    {
        if (bind < 0) return;
        if (last >= 0 && last != bind) switches++;
        last = bind; presses++;
    }
    struct Node { State s; std::vector<Best> best; double count = 0.0; };
    // sc/jb/ride are the PLAN that flew this edge, kept so the winning route can be flown again
    // tick by tick for the drawn arc (TraceBest). Three bytes, not a second search.
    // duckApex too: a forced-duck landing replayed without its duck flies the standing arc, misses
    // the arrival it was built from, and the drawn line for that answer was silently dropped
    struct Edge { int from; int dep; int arr; int cost; float margin; int8_t sc; int8_t ride; bool jb; bool duckApex; };
    struct Key
    {
        uint32_t z, vz, bits, duckTime, stamina; int since, aduck;
        bool operator==(const Key& o) const
        {
            return z == o.z && vz == o.vz && bits == o.bits && duckTime == o.duckTime && stamina == o.stamina
                && since == o.since && aduck == o.aduck;
        }
    };
    struct KeyHash
    {
        size_t operator()(const Key& k) const
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
            mix(k.z); mix(k.vz); mix(k.bits); mix(k.duckTime); mix(k.stamina);
            mix((uint32_t)k.since); mix((uint32_t)k.aduck);
            return (size_t)h;
        }
    };

    struct Leg
    {
        int layer, from;
        const Point* T;
        bool  depFloor; float depFloorEff; bool depPlayer; const char* depOn;   // depOn: "" / " off player"
        bool  srcGround;   // this leg starts with a press off a floor
        bool  last;
    };
    struct Plan { int sc; bool jb; bool duckApex = false; };   // jb: AutoJumpbug held toward T instead
    // of landing on it. duckApex: the standing arc missed the last ground marker -- force the
    // air-duck teleport (FinishDuck's +8.5u) once the apex is past and see if the shorter hull
    // reaches it instead (Expand's plans(), only tried when the plain arc added no edge)
    static constexpr int kMaxRide = 17; // a ride this long has outlived every PixelSurf latch
    struct Fs
    {
        State s; ScriptMem mem; int i = 0;
        bool leftDep = false;   // the floor we left is behind us for good
        bool falling = false;   // last move went down
        bool aboveT = false;    // feet have been over the target's floor top
        bool psDuck = false;    // PixelSurf latched through its duck pass (s_psShouldDuck)...
        int  psPinI = 0;        // ...for the pin on this cmd index
        int  tbSeen = 0;        // texturebug: straddling ticks this leg has already offered
        int  creep = 0;         // pixeljump: ticks the feet started over the lip (- DIST_EPSILON)
        bool lipIn = false;     // ...enough of them: the hull has crept in over it, it is a floor
    };
    // edges out of a just-caught pixelsurf, gathered over every ride length before they are added
    struct Pending { State s; std::string dep, arr; int cost; float margin; int ride; int sc; bool jb; bool duckApex; };
    std::vector<Pending>* m_collect = nullptr;
    int m_ride = 0;
    int  m_curSc = SC_FALL;      // the plan the leg being flown right now is running
    bool m_curJb = false;
    bool m_curDuckApex = false;
    // ---- replay recorder (TraceBest): the arc of ONE known-good edge, not a search ----
    std::vector<float>* m_rec = nullptr;   // z after every tick of the leg being replayed
    std::vector<ArcTick>* m_recArc = nullptr;  // what the drawn line needs on top of z
    const State*        m_recTo = nullptr; // the arrival this replay is looking for
    bool                m_recHit = false;
    size_t              m_recLen = 0;
    int                 m_bestNode = -1, m_bestRank = -1;   // the combo printed first

    std::vector<Point> m_pts;
    Start   m_start;
    Params  m_p;
    Options m_o;
    Result  m_res;
    std::vector<std::string> m_labels;
    std::unordered_map<std::string, int> m_labelIx;
    std::vector<std::vector<Node>> m_layers;
    std::vector<std::vector<Edge>> m_edges;
    std::vector<std::unordered_map<Key, int, KeyHash>> m_keys;
    int    m_layer = 0;
    size_t m_next = 0;
    bool   m_done = true;

    int Label(const std::string& s)
    {
        if (s.empty()) return -1;
        auto it = m_labelIx.find(s);
        if (it != m_labelIx.end()) return it->second;
        m_labels.push_back(s);
        m_labelIx.emplace(s, (int)m_labels.size() - 1);
        return (int)m_labels.size() - 1;
    }

    // exact: two states merge only when every float is bit-identical, so a merged continuation is
    // the one the second arrival would have flown too
    static uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
    Key MakeKey(const State& s) const
    {
        Key k{};
        k.z = Bits(s.z);
        k.vz = Bits(s.vz);
        k.bits = (s.ground ? 1u : 0u) | (s.fDuck ? 2u : 0u) | (s.ducking ? 4u : 0u) | (s.ducked ? 8u : 0u)
               | (s.oldDuck ? 16u : 0u) | (s.oldJump ? 32u : 0u);
        k.duckTime = Bits(s.duckTime);
        k.stamina = Bits(s.stamina);    // the next jump's height: two arrivals a few ms apart are not one state
        const int since = s.tick - s.releaseTick;
        k.since = since > m_p.duckTimerTicks ? m_p.duckTimerTicks + 1 : since;
        k.aduck = s.airDucked | (s.hold << 8) | (((s.psAge + 64) & 0xFF) << 12) | (s.psRiding ? 1 << 20 : 0)
                | (s.psPrevSurf ? 1 << 21 : 0) | (s.onSeam ? 1 << 22 : 0);
        return k;
    }

    // the leg being replayed reached an arrival: is it THE one this edge was built from? The key is
    // what the search itself merges on, so an exact match is the same arrival and nothing else
    void RecArrive(const State& arrived)
    {
        if (m_recHit || !m_recTo) return;
        if (MakeKey(arrived) == MakeKey(*m_recTo)) { m_recHit = true; m_recLen = m_rec->size(); }
    }

    void AddEdge(const Leg& L, const State& arrived, const std::string& dep, const std::string& arr,
                 int cost, float margin)
    {
        if (arrived.edgy) return;
        if (m_rec) { RecArrive(arrived); return; }     // a replay never touches the DAG
        if (m_collect) { m_collect->push_back({ arrived, dep, arr, cost, margin, m_ride, m_curSc, m_curJb, m_curDuckApex }); return; }
        const int into = L.layer + 1;
        const int di = Label(dep), ai = Label(arr);
        std::vector<Node>& layer = m_layers[into];
        const Key k = MakeKey(arrived);
        int v;
        auto it = m_keys[into].find(k);
        if (it == m_keys[into].end())
        {
            if (layer.size() >= m_o.maxNodes) { m_res.capped = true; return; }
            v = (int)layer.size();
            layer.emplace_back();
            layer.back().s = arrived;
            m_keys[into].emplace(k, v);
        }
        else
            v = it->second;

        // every plan off a node prints a different step, so no edge here is ever a duplicate --
        // but a node that already existed skips the maxNodes check above, so edges are their own
        // cap: many arrivals merging into one popular state is exactly the unbounded growth path
        std::vector<Edge>& edges = m_edges[into];
        if (edges.size() >= m_o.maxEdges) { m_res.capped = true; return; }
        edges.push_back({ L.from, di, ai, cost, margin, (int8_t)m_curSc, (int8_t)m_ride, m_curJb, m_curDuckApex });
        const int ei = (int)edges.size() - 1;
        const Node& u = m_layers[L.layer][L.from];
        Node& n = layer[v];
        n.count += u.count;
        // the binds this edge asks for: the press it departs on (a seam ride departs on none) and
        // the jumpbug, which replaces the landing and is a bind of its own
        const int depBind = L.srcGround ? ScriptBit(m_curSc) : -1;
        const int arrBind = arr == "jumpbug" ? MB_JUMPBUG : -1;
        for (int r = 0; r < (int)u.best.size(); r++)
        {
            int pr = u.best[r].presses, sw = u.best[r].switches, lb = u.best[r].lastBind;
            Press(depBind, pr, sw, lb);
            Press(arrBind, pr, sw, lb);
            const Best c{ u.best[r].cost + cost, std::min(u.best[r].margin, margin), ei, r, pr, sw, lb };
            auto pos = std::find_if(n.best.begin(), n.best.end(), [&](const Best& b)
                { return Easier(c.presses, c.switches, c.cost, c.margin, b.presses, b.switches, b.cost, b.margin); });
            if ((int)n.best.size() >= m_o.topK && pos == n.best.end()) break;
            n.best.insert(pos, c);
            if ((int)n.best.size() > m_o.topK) n.best.pop_back();
        }
    }

    // ---- names and costs ---------------------------------------------------------------------------
    std::string DepName(const Leg& L, const Plan& pl) const
    {
        if (!L.srcGround) return std::string();
        return WithOn(pl.sc == SC_DELAY ? DelayName(m_p.delayTicks) : std::string(MoveName(ScriptBit(pl.sc))), L.depOn);
    }
    static int PlanCost(const Plan& pl, bool srcGround)
    {
        static const int k[SC_COUNT] = { 1, 2, 2, 0, 1, 1, 0 };
        return srcGround ? k[pl.sc] : 0;
    }
    static std::string Stance(bool ducked) { return ducked ? " (ducked)" : " (stand)"; }

    // the leg out of node `from` at `layer`: what it departs, what it is aiming at. Built here and
    // in TraceBest, so it is one function -- a replay off a different departure floor would fly a
    // different arc than the edge it is replaying
    Leg MakeLeg(int layer, int from, unsigned* pAllowed = nullptr) const
    {
        const State& s0 = m_layers[layer][from].s;
        Leg L;
        L.layer = layer; L.from = from;
        L.T = &m_pts[layer];
        L.last = layer + 1 == (int)m_pts.size();
        L.srcGround = s0.ground;
        if (layer == 0)
        {
            L.depFloor = !m_start.air && m_start.hasFloor;
            L.depFloorEff = m_start.floorEff; L.depPlayer = m_start.player; L.depOn = OnName(m_start.player);
            if (pAllowed) *pAllowed = m_o.moves & m_start.moves & (kMovesPress | kMovesStart);
        }
        else
        {
            // off a landing or a jumpbug you are over that floor; off a surf, edgebug or headbang
            // there is nothing under you
            const Point& src = m_pts[layer - 1];
            L.depFloor = LandLike(src.type);
            L.depFloorEff = src.z; L.depPlayer = src.player; L.depOn = OnName(src.player);
            // off a lip you hop on the landing tick: no delay hop (botox hop_only = bhop timing)
            if (pAllowed) *pAllowed = m_o.moves & src.moves & kMovesPress
                                    & (src.type == PT_PIXELJUMP ? ~(1u << MB_DELAY) : ~0u);
        }
        return L;
    }

    // ---- one state: every plan to the next marker -----------------------------------------------
    void Expand(int layer, int from)
    {
        const State s0 = m_layers[layer][from].s;
        unsigned allowed = 0;
        const Leg L = MakeLeg(layer, from, &allowed);

        // a ground marker is touched by landing on it (then a press: its press bits) or by a
        // jumpbug off it; a last ground marker lands if any press bit is left on it
        bool land = true, jb = false;
        if (LandLike(L.T->type))
        {
            const unsigned tm = m_o.moves & L.T->moves;
            land = (tm & kMovesPress) != 0;
            jb   = (tm & (1u << MB_JUMPBUG)) != 0;
        }
        // last ground marker only, and only once the plain arc has had its try: never a silent
        // duck when the standing one already lands (CS:GO's own rule, proj-rc-jb-crouch-1007).
        // A lip anywhere in the route (botox duck_air_here: a pixeljump the plain arc missed tries
        // the air duck mid-route too)
        const bool tryDuckApex = (L.last ? LandLike(L.T->type) : L.T->type == PT_PIXELJUMP) && !m_collect;
        auto plans = [&](const State& s)
        {
            for (int v = 0; v < 2; v++)
            {
                const bool useJb = v == 1;
                if (useJb ? !jb : !land) continue;
                if (!s.ground) { RunLeg(L, s, { SC_FALL, useJb }); continue; }
                for (int sc = SC_JUMP; sc < SC_FALL; sc++)
                {
                    if (!(allowed & (1u << ScriptBit(sc)))) continue;
                    const int into = layer + 1;
                    const size_t before = into < (int)m_edges.size() ? m_edges[into].size() : 0;
                    RunLeg(L, s, { sc, useJb });
                    // presses only -- a walk off that never fell was rejected on purpose (9b), and
                    // forcing a duck on it is not what this fallback is for
                    if (!useJb && tryDuckApex && PressScript(sc))
                    {
                        const size_t after = into < (int)m_edges.size() ? m_edges[into].size() : 0;
                        if (after == before) RunLeg(L, s, { sc, false, true });
                    }
                }
            }
        };
        if (!s0.onSeam) { m_ride = 0; plans(s0); return; }

        // off a pixelsurf: the ride lasts as long as the player keeps it (z never moves), and while
        // PixelSurf's latch lives it gates AutoJumpbug and its own next hunt. Fly every length up
        // to one that outlives the latch; an outcome only some lengths reach prints the shortest
        std::vector<Pending> got;
        m_collect = &got;
        for (int r = 0; r <= kMaxRide; r++)
        {
            State s = s0;
            s.onSeam = false;
            Ride(s, r);
            m_ride = r;
            plans(s);
        }
        m_collect = nullptr;
        // one outcome = same step, same state but the clock. The ride lengths that reach it are
        // printed as runs ("ride 3-9 ticks", "ride 12+ ticks"); every length reaching it = no label
        std::vector<char> dup(got.size(), 0);
        for (size_t a = 0; a < got.size(); a++)
        {
            if (dup[a]) continue;
            bool rides[kMaxRide + 1] = {};
            std::vector<size_t> members{ a };
            rides[got[a].ride] = true;
            for (size_t b = a + 1; b < got.size(); b++)
                if (!dup[b] && got[b].dep == got[a].dep && got[b].arr == got[a].arr && SameButClock(got[b].s, got[a].s))
                {
                    dup[b] = 1;
                    rides[got[b].ride] = true;
                    members.push_back(b);
                }
            for (int lo = 0; lo <= kMaxRide; lo++)
            {
                if (!rides[lo]) continue;
                int hi = lo;
                while (hi + 1 <= kMaxRide && rides[hi + 1]) hi++;
                const Pending* e = nullptr;
                for (size_t m : members) if (got[m].ride == lo) { e = &got[m]; break; }
                std::string dep = e->dep;
                if (!(lo == 0 && hi == kMaxRide))
                {
                    const std::string run = hi == kMaxRide ? std::to_string(lo) + "+"
                                          : lo == hi ? std::to_string(lo) : std::to_string(lo) + "-" + std::to_string(hi);
                    dep = (dep.empty() ? "" : dep + ", ") + "ride " + run + " ticks";
                }
                // the edge carries the SHORTEST ride of the run and the plan that flew it: that is
                // the one TraceBest re-flies, and the one the printed run starts at
                m_ride = lo; m_curSc = e->sc; m_curJb = e->jb; m_curDuckApex = e->duckApex;
                AddEdge(L, e->s, dep, e->arr, e->cost, e->margin);
                lo = hi;
            }
        }
    }

    // the player's ride on the seam: z and vz held by the clip, dih's features tick along
    void Ride(State& s, int ticks) const
    {
        for (int k = 0; k < ticks; k++)
        {
            s.tick++;
            if (s.stamina > 0.f)  s.stamina  = std::max(0.f, s.stamina  - 1000.f * m_p.dt);   // ReduceTimers
            if (s.duckTime > 0.f) s.duckTime = std::max(0.f, s.duckTime - 1000.f * m_p.dt);
            s.psRiding = s.psPrevSurf;          // pre vz sits on -halfgrav: s_prevSurfVel && now
            s.psPrevSurf = true;
            if (s.psAge < 17) s.psAge = std::min(s.psAge + 1, 16);   // riding vz never clears it
            s.oldJump = false;
            s.oldDuck = s.hold != 0;
        }
    }
    static bool SameButClock(const State& a, const State& b)
    {
        State x = a, y = b;
        x.tick = y.tick = 0; x.releaseTick = y.releaseTick = 0; x.duckTime = y.duckTime = 0.f;
        return std::memcmp(&x.z, &y.z, sizeof(float)) == 0 && std::memcmp(&x.vz, &y.vz, sizeof(float)) == 0
            && std::memcmp(&x.stamina, &y.stamina, sizeof(float)) == 0
            && x.ground == y.ground && x.fDuck == y.fDuck && x.ducking == y.ducking && x.ducked == y.ducked
            && x.oldDuck == y.oldDuck && x.oldJump == y.oldJump && x.airDucked == y.airDucked
            && x.hold == y.hold && x.psAge == y.psAge && x.psRiding == y.psRiding && x.psPrevSurf == y.psPrevSurf
            && x.edgy == y.edgy && x.onSeam == y.onSeam;
    }

    World WorldFor(const Leg& L, const Fs& f) const
    {
        World w;
        // a lip catches only a hull that has crept in over it (Fly counts f.creep)
        const bool tFloor = FloorLike(L.T->type) && (L.T->type != PT_PIXELJUMP || f.lipIn);
        bool any = false, player = false; float best = 0.f;
        if (!f.leftDep && L.depFloor) { any = true; best = L.depFloorEff; player = L.depPlayer; }
        const float pjz = PjFloorZ(*L.T);   // a skin pixeljump's real floor sits kSkinUp over its pin
        if (tFloor && (f.falling || f.aboveT) && (!any || pjz > best))
        {
            any = true; best = pjz; player = L.T->player;
        }
        if (any) { w.floor = true; w.floorEff = best; w.floorPlayer = player; }
        // headbounce's ceiling face is headbang's: same clip, same ceilEff (botox route_engine.cpp
        // point_step retypes it before simulating; Point::hbCeiling is this file's version of that)
        if (L.T->type == PT_HEADBANG || (L.T->type == PT_HEADBOUNCE && L.T->hbCeiling)) { w.ceil = true; w.ceilEff = L.T->z; }
        return w;
    }

    // one tick of the replayed leg, for the drawn arc. Feet z, one entry per cmd, in order
    void Rec(const State& s, const TickInfo* ti = nullptr)
    {
        if (!m_rec || m_recHit) return;
        m_rec->push_back(s.z);
        if (m_recArc) m_recArc->push_back({ !s.ground, ti ? ti->stanceDz : 0.f });
    }

    void RunLeg(const Leg& L, State s, const Plan& pl)
    {
        m_curSc = pl.sc; m_curJb = pl.jb; m_curDuckApex = pl.duckApex;
        World g; g.floor = L.depFloor; g.floorEff = L.depFloorEff;
        if (pl.sc == SC_DELAY)
        {
            // off a LANDING only, standing only (botox: "delay hop = style_jump off a STANDING
            // landing"). Off the start you are already settled, so the wait is the jump's own arc;
            // ducked, DelayHop's stripped cmds are the Bunnyhop stand-up below, a different bind
            if (L.layer == 0 || m_p.delayTicks <= 0 || !s.ground || s.fDuck || s.ducking) return;
            // DelayHop strips IN_JUMP off every grounded cmd until tick > lastAir + N: N cmds
            // standing still, then the press. Ground cmds: PixelSurf and MiniJump let go, the ride
            // detect reads vz 0 (Fly's own grounded-tick bookkeeping, done by hand here)
            for (int k = 0; k < m_p.delayTicks; k++)
            {
                Tick(s, Cmd(), g, m_p); m_res.ticks++; Rec(s);
                if (!s.ground || s.edgy) return;
                s.hold &= ~6; s.psAge = 17; s.psRiding = false; s.psPrevSurf = false;
            }
        }
        const bool press = pl.sc == SC_JUMP || pl.sc == SC_MINI || pl.sc == SC_LONG;
        if (press && s.ground && (s.fDuck || s.ducking))
        {
            // a press off a DUCKED floor, CSS: Bunnyhop (movement.cpp:962) skips its strip while the
            // grounded hull is FL_DUCKING, so the held IN_JUMP reaches the landing cmd as a fresh
            // press (the air cmds were stripped) and CheckJumpButton takes it ducked -- the SET
            // branch, no stand-up wait (TF2 refused ducked jumps; CSS :634-680 never does). The
            // player lets go of duck for the press: the air FinishUnDuck then drops the feet 8.5
            s.hold = 0;
        }
        if (pl.sc == SC_WALKOFFDUCK) s.hold |= 1;
        if (pl.sc == SC_WALKOFFDUCK && !(s.fDuck && !s.ducking))
        {
            // crouch on the floor first: TIME_TO_DUCK on the ground, no origin move
            bool ducked = false;
            for (int k = 0; k < 40 && !ducked; k++)
            {
                Cmd c; c.duck = true;
                TickInfo ti;
                Tick(s, c, g, m_p, &ti); m_res.ticks++; Rec(s, &ti);
                if (!s.ground || s.edgy || ti.duckStripped) return;
                ducked = s.fDuck && !s.ducking;
            }
            if (!ducked) return;
        }
        if (pl.sc == SC_WALKOFF && (s.fDuck || s.ducking)) return;

        Params pp = m_p;
        if (L.layer == 0 && !m_start.air) pp.cropFrac = 1.f;   // a standing press is not a bhop
        Fs f;
        f.s = s;
        Fly(L, pl, f, pp);
    }

    // the leg is not the plan any more: the engine refused the press, or ate a duck the plan itself
    // presses (LongJump / MiniJump cmd A, a crouch walk off). A feature duck the engine strips
    // mid-air just does not happen -- the arc goes on
    static bool TickOk(const Leg& L, const Plan& pl, int i, bool planDuck, const TickInfo& ti, const State& s)
    {
        if (s.edgy) return false;
        if (i == 0 && L.srcGround && PressScript(pl.sc) && !ti.jumped) return false;
        if (planDuck && ti.duckStripped) return false;
        return true;
    }

    static float HalfGrav(const Params& p) { State t; FinishGravity(t, p); return t.vz; }   // -6 at 800 / 66
    // what a pinned tick ends on: the clip's 1/32 nudge (ClipVz) through that tick's FinishGravity:
    // -5.96875 held on a floor / seam, -6.03125 under a ceiling (ref-css-clip-nudge, log-proven)
    static float PinVz(const Params& p, bool up = true) { State t; t.vz = ClipVz(up); FinishGravity(t, p); return t.vz; }
    // PixelSurf's "vz sits on -halfgrav" test, flSurfTol 1.5 (movement.cpp:3161)
    static bool SurfVel(float vz, const Params& p) { return std::fabs(vz - HalfGrav(p)) < 1.5f; }

    // PixelSurf's hunt (movement.cpp:3665): the cmd as it stands flown psHorizon ticks ahead, duck
    // cleared (standing pass), then duck held (duck pass) -- a held ride duck skips the standing
    // pass. The first tick that clips into the seam's window is the pin; a standing pin wins
    void Hunt(Fs& f, const Cmd& c, const World& w, const Params& p, const Point& T)
    {
        const bool held = (f.s.hold & 2) != 0;
        for (int pass = 0; pass < 2; pass++)
        {
            if (held && pass == 1) break;
            const bool duckPass = held || pass == 1;
            State q = f.s; Cmd qc = c; qc.duck = duckPass;
            for (int z = 0; z < p.psHorizon; z++)
            {
                TickInfo qt;
                Tick(q, qc, w, p, &qt); m_res.ticks++;
                if (q.ground) break;                              // touching down is a fall
                const float d = T.z - q.z;
                if (qt.moveVz < 0.f && d > kSurfLo && d < kSurfHi)
                {
                    f.s.psAge = -z - 1;                           // the pin cmd will be age 0
                    f.psDuck = duckPass;
                    f.psPinI = f.i + z;
                    return;
                }
            }
        }
    }

    // One leg, cmd by cmd, as dih builds each cmd (movement.cpp ~5020): the player's keys, then
    // AutoJumpbug before prediction, the prediction, then LongJump / MiniJump / PixelSurf, which
    // only OR IN_DUCK in after it. Every button below is one of those features' own
    void Fly(const Leg& L, const Plan& pl, Fs f, const Params& p)
    {
        const Point& T = *L.T;
        const bool tFloor = FloorLike(T.type);
        const bool walk = pl.sc == SC_WALKOFF || pl.sc == SC_WALKOFFDUCK;
        const std::string dep = DepName(L, pl);
        const int cost = PlanCost(pl, L.srcGround);
        const int li = L.layer;

        for (; f.i < m_o.maxLegTicks; f.i++)
        {
            const int i = f.i;
            State& s = f.s;
            if (tFloor && s.z >= T.z - 0.001f) f.aboveT = true;
            // ---- PIXELJUMP: the lip is a floor only once the hull is over it -----------------
            // botox route_engine.cpp point_step: a tick that STARTS with the feet at or over the
            // lip (- DIST_EPSILON) is one tick of pressing in over it; kPjCreepTicks of them and it
            // grounds you. The tick it turns solid must clear it on its own (the move flown with no
            // lip under it ends on or over the top), else that tick passes and the next one tries
            if (T.type == PT_PIXELJUMP)
            {
                const bool pushed = s.z >= T.z - kDistEps;
                if (pushed) f.creep++;
                f.lipIn = f.creep >= kPjCreepTicks;
                if (f.lipIn && pushed && f.creep == kPjCreepTicks)
                {
                    Fs g = f; g.lipIn = false;
                    const World wr = WorldFor(L, g);
                    ScriptMem m = f.mem; State r = s;
                    const Cmd rc = ScriptCmd(pl.sc, i, r, m, wr, p);
                    Tick(r, rc, wr, p); m_res.ticks++;
                    f.lipIn = r.z >= T.z;
                }
            }
            World w = WorldFor(L, f);
            const bool walkTick = walk && i == 0 && L.srcGround;
            if (walkTick) w.floor = false;

            // ---- the player's keys, then AutoJumpbug (idle while PixelSurf's ride is latched) ----
            Cmd c = ScriptCmd(pl.sc, i, s, f.mem, w, p);
            if (pl.sc == SC_MINI && i > 0) c.duck = false;   // MiniJump past cmd A = hold bit 4 below
            const bool scriptPost = (pl.sc == SC_LONG || pl.sc == SC_MINI) && c.duck;
            if (scriptPost) c.duck = false;                      // LongJump / MiniJump: after prediction
            const bool latched = s.psAge < 17;
            bool band = false; float jbMargin = kInf;
            if (pl.jb && !(p.pixelSurf && p.jbYieldSurf && (s.psRiding || latched)))
                JumpbugCmd(s, w, p, c, band, jbMargin);
            if (pl.duckApex && !s.ground && s.vz <= 0.f) c.duck = true;

            // ---- the prediction, then the features that read it ----
            State pr = s;
            Tick(pr, c, w, p); m_res.ticks++;
            if (scriptPost) c.duck = true;
            // MiniJump "hold crouch" (s_mjHoldDuck): IN_DUCK on every cmd until a grounded START
            // (movement.cpp:1612) -- the touch below clears bit 4
            if (s.hold & 4) c.duck = true;
            bool psClear = false;
            const bool sv = SurfVel(s.vz, p) && (!s.ground || latched);
            if (p.pixelSurf)
            {
                if (s.ground || pr.ground)
                {
                    // feet on something (either view): the ride and its hold end, no duck sent
                    s.hold &= ~2; f.psDuck = false; psClear = latched;
                }
                else
                {
                    if (latched)
                    {
                        if (s.psAge + 1 >= 17 && !SurfVel(pr.vz, p)) { psClear = true; f.psDuck = false; }
                    }
                    else if (T.type == PT_SURF)
                        Hunt(f, c, w, p, T);
                    const bool latchNow = s.psAge < 17 && !psClear;
                    if ((latchNow && f.psDuck && i >= f.psPinI - 1) || (s.hold & 2))
                    {
                        c.duck = true;
                        s.hold |= 2;
                    }
                }
            }

            const State pre = s;
            TickInfo ti;
            Tick(s, c, w, p, &ti); m_res.ticks++; Rec(s, &ti);

            // ---- feature memory the next cmd reads ----
            s.psRiding = sv && pre.psPrevSurf;
            s.psPrevSurf = sv;
            if (psClear) s.psAge = 17;
            else if (s.psAge < 17) s.psAge = std::min(s.psAge + 1, 16);
            if (s.ground) s.hold &= ~6;                          // PixelSurf / MiniJump let go on a touch

            if (band && ti.jumped)
            {
                // off T = the jumpbug; off anything else it jumped somewhere the route never said
                if (ti.unducked && !s.edgy && w.floor && std::fabs(w.floorEff - T.z) < 0.001f)
                {
                    State j = s; j.hold = 0;                     // the player lets go of duck to jump
                    AddEdge(L, j, dep, "jumpbug", cost + 2, jbMargin);
                }
                return;
            }
            if (!TickOk(L, pl, i, scriptPost || pl.sc == SC_WALKOFFDUCK, ti, s)) return;
            if (!s.ground)
            {
                if (ti.moveVz < 0.f) { f.falling = true; f.leftDep = true; }
                else if (ti.moveVz > 0.f) f.falling = false;
            }
            if (walkTick) f.leftDep = true;
            {
                // a headbang, a texturebug and a headbounce (either face) are reached by the HEAD,
                // everything else by the feet
                const bool byHead = T.type == PT_HEADBANG || T.type == PT_TEXTUREBUG || T.type == PT_HEADBOUNCE;
                const float over = byHead ? s.z + HullH(s, p) - T.z : s.z - T.z;
                // the duckApex retry is a fallback flight, not the arc the miss message describes --
                // it must not change what "best was Xu short" is measured against
                if (!pl.duckApex && over > m_res.highest[li]) m_res.highest[li] = over;
            }

            if (Arrive(L, pl, pre, f, ti, w, dep, cost, p)) return;

            // ---- give up: nothing below can raise the hull again ----
            if (s.ground) return;                                  // landed on something else
            // botox lip_missed (passed down by the lip with the hull still outside it) is the floor
            // give-up below: falling under a FloorLike z ends the leg, crept in or not
            if (f.falling)
            {
                // a ceiling headbounce plays as headbang (see WorldFor); falling past it the same.
                // the seam-window headbounce gives up on its own, inside Arrive (hb_rise != falling)
                if (T.type == PT_HEADBANG || (T.type == PT_HEADBOUNCE && T.hbCeiling)) return;
                // a standing hull still has PixelSurf's +8.5 duck pass
                if (T.type == PT_SURF && s.z < T.z - 1.f - (s.fDuck ? 0.f : AirDuckShift(p))) return;
                // a texturebug is caught anywhere on the hull's side, so the band only closes
                // once the STANDING head (the tallest it could become) is under the seam
                if (T.type == PT_TEXTUREBUG && s.z + StandH(p) < T.z - 1.f) return;
                // a duckApex plan still owes its +8.5: the forced duck goes out on the first falling
                // cmd, which is the one after this tick, so the feet are not under the floor until
                // that lift cannot reach it either (an arc that apexes 0.3u short of the marker is
                // exactly the one this fallback exists for)
                const float lift = (pl.duckApex && !s.fDuck && !s.ducked) ? AirDuckShift(p) : 0.f;
                if (tFloor && s.z + lift < T.z - 0.001f) return;
            }
            if (s.z < T.z - 8192.f) return;
        }
    }

    // true = this branch is finished
    bool Arrive(const Leg& L, const Plan& pl, const State& pre, Fs& f, const TickInfo& ti, const World& w,
                const std::string& dep, int cost, const Params& p)
    {
        const Point& T = *L.T;
        const State& s = f.s;
        const bool onT = s.ground && w.floor && std::fabs(w.floorEff - PjFloorZ(T)) < 0.001f;
        const bool stanceOk = s.ducked ? T.allowDuck : T.allowStand;
        switch (T.type)
        {
        case PT_SURF:
            if (!s.ground && ti.moveVz < 0.f)
            {
                const float d = T.z - s.z;
                if (d > kSurfLo && d < kSurfHi)
                {
                    // the first clip latches the ride (a MiniJump "hold crouch" duck holds until a
                    // grounded START, movement.cpp:1612 -- a ride is not grounded, so it keeps it)
                    if (stanceOk)
                    {
                        State a = s;
                        a.vz = PinVz(p);                             // a clipped tick ends on -halfgrav + 1/32
                        a.onSeam = true;
                        AddEdge(L, a, dep, "pixelsurf" + Stance(s.ducked), cost + (f.psDuck ? 1 : 0),
                                std::min(d - kSurfLo, kSurfHi - d));
                    }
                    return true;
                }
                const float off = d <= kSurfLo ? kSurfLo - d : d - kSurfHi;
                if (off < m_res.closest[L.layer])
                {
                    m_res.closest[L.layer] = off;
                    // the seam is UNDER the window -> it wanted to be higher, and the other way
                    // round. The shift lands the tick on the window's edge, so a caller offering
                    // it as a spot to aim at adds its own margin to get inside
                    m_res.closestDz[L.layer] = d <= kSurfLo ? off : -off;
                }
            }
            return false;
        case PT_HEADBANG:
            if (!ti.ceilHit) return false;
            if (stanceOk) AddEdge(L, s, dep, "headbang" + Stance(s.ducked), cost, kInf);
            return true;
        // botox route_pt_headbounce, route_engine.cpp:1977 (+1387 for the ceiling face). Point::type
        // does NOT change between the two faces -- Point::hbCeiling picks the branch every tick
        case PT_HEADBOUNCE:
            if (T.hbCeiling)
            {
                // the flat-ceiling face: WorldFor already gave it the same ceiling a headbang gets,
                // so this is headbang's own arrival, just under the "headbounce" name
                if (!ti.ceilHit) return false;
                if (stanceOk) AddEdge(L, s, dep, "headbounce" + Stance(s.ducked), cost, kInf);
                return true;
            }
            // the seam-window face: no ceiling to clip against, the catch is the pre-tick HEAD
            // rising into the window. hb_head (the window test) is pre (botox z_in/hull_in): the
            // window is tested against where the head WAS, not where this tick's move carried it --
            // a seam catch clips the swept hull's side, it does not teleport the head. hb_rise's OWN
            // ground test is POST-tick though (botox s.on_ground, read after sim_tick already ran):
            // on the launch tick itself pre is still grounded (the jump hasn't left yet when the
            // tick starts) and botox's rule is "still airborne once this tick is done", not "was
            // already airborne entering it" -- using pre here would give up on every single jump's
            // very first tick, before the hull ever leaves the ground
            {
                const float head = pre.z + HullH(pre, p);
                // not rising any more, or already risen clean past the window: no seam here will
                // ever catch this arc now (botox: "if (!hb_rise || hb_head >= target_z) break;")
                if (s.ground || ti.moveVz <= 0.f || head >= T.z) return true;
                // duck state changed mid-tick, or not yet into the window's 1/32 (botox: "continue;")
                if (s.ducked != pre.ducked || head < T.z - kDistEps) return false;
                // in the window, duck held -- the arrival stance is pre-tick's (botox ducked_in), and
                // a duck-apex retry plan needs to actually be ducked to use this face at all
                const bool stanceOkPre = pre.ducked ? T.allowDuck : T.allowStand;
                if (!stanceOkPre || (pl.duckApex && !pre.ducked)) return true;   // in the window, refused: give up
                State a = s;
                a.z = pre.z; a.vz = PinVz(p, false); a.ground = false;   // pinned at the tick's start height
                AddEdge(L, a, dep, "headbounce" + Stance(pre.ducked), cost, kInf);
                return true;
            }
        case PT_GROUND:
        case PT_PIXELJUMP:
        {
            if (!onT) return false;
            if (pl.jb) return true;                                 // landed: the jumpbug never fired
            // a walk off onto a floor at its own height never fell: that is walking, not a move
            if ((pl.sc == SC_WALKOFF || pl.sc == SC_WALKOFFDUCK) && L.srcGround
                && std::fabs(s.z - L.depFloorEff) < 0.01f)
                return true;
            // a lip is named wherever it is in the route: the press after it is a hop off a wall,
            // not off a floor you can walk around on (botox "(pixeljump) jump")
            const bool lip = T.type == PT_PIXELJUMP;
            AddEdge(L, s, dep, lip ? "pixeljump" + Stance(s.ducked) : L.last ? "land" + Stance(s.ducked) : std::string(),
                    cost, kInf);
            return true;
        }
        case PT_EDGEBUG:
        {
            if (!onT) return false;
            if (ti.moveVz >= 0.f || !stanceOk) return true;
            State a = s;
            a.ground = false;
            a.vz = HalfGrav(p);                              // walks off: FinishGravity from 0
            a.airDucked = pre.airDucked;                      // slide-off: count never reset
            if (!ti.landHit) a.tick += 1;                    // hover: one grounded tick first
            AddEdge(L, a, dep, "edgebug" + Stance(s.ducked), cost, kInf);
            return true;
        }
        case PT_TEXTUREBUG:
        {
            // THE WALL SEAM (the antoha bug). Two coplanar faces the solver clips separately: a
            // swept hull whose SIDE spans the line touches both at once, TryPlayerMove's two-plane
            // branch projects velocity onto the horizontal crease and zeroes vz, FinishGravity puts
            // back one half step. The move never moved the origin, so z is exactly where the tick
            // started -- texturebug.inl's pin test is that same "vz == TargetPredictZVel()" and its
            // log rows read pz == oz. Vertically this is a pixelsurf catch with a hull-tall window
            // instead of a 0.04u one; the horizontal push that lands it is the player's, like the
            // rest of the horizontal half, and so is WHERE in the band it is caught -- so every
            // straddling tick is an arrival and the arc flies on past it instead of stopping.
            // The window and the give-up below are dih's OWN TB predictor, not a guess:
            // texturebug.inl:638 picks its seam tick with `seam_z >= z + mins.z && seam_z <= z +
            // maxs.z` and gives up on `z + maxs.z < seam_z` ("fallen clean under the ledge").
            // The 1/32 margins are this file's: a seam exactly level with the feet or the head is
            // one face touching, not two, so it cannot be the clip that pins anything.
            if (s.ground) return false;
            // a stance change moves the origin (FinishDuck/UnDuck +-8.5 in air): z would not be pre.z
            if (ti.duckedNow || ti.unducked) return false;
            const float h = HullH(pre, p);
            if (pre.z + kDistEps >= T.z || pre.z + h - kDistEps <= T.z) return false;
            const int nth = f.tbSeen++;   // counted even when the stance bars it: the tick still straddled
            if (!stanceOk) return false;
            State a = s;
            a.z  = pre.z;                 // clipped: the move never moved it
            a.vz = PinVz(p);
            a.ground = false;
            a.onSeam = true;              // rides exactly like a pixelsurf from here
            // Which tick of the band you push in on is a press the player makes, so it is named
            // and each catch height is its own combination -- they are NOT the same route, the
            // hull is held at a different height and everything after it differs.
            // +nth on the cost so the list reads: the earliest catch of every press first, then
            // the later ones. Without it a marker deep in the band fills the whole panel with one
            // press at fifteen heights and buries the other presses under it.
            AddEdge(L, a, dep, "texturebug" + Stance(s.ducked)
                    + (nth ? " +" + std::to_string(nth) + " ticks" : std::string()), cost + nth, kInf);
            return false;
        }
        default: return true;
        }
    }

    void Finish()
    {
        m_done = true;
        const int N = (int)m_pts.size();
        for (int i = 1; i < (int)m_layers.size(); i++)
            if (m_layers[i].empty()) { m_res.deadPoint = i - 1; break; }
        if (m_res.deadPoint < 0 && (int)m_layers.size() <= N) m_res.deadPoint = (int)m_layers.size() - 1;
        if (m_res.deadPoint >= 0 || (int)m_layers.size() <= N) return;

        struct Pick { int cost; float margin; int node; int rank; int presses; int switches; };
        std::vector<Pick> all;
        double total = 0.0;
        for (int v = 0; v < (int)m_layers[N].size(); v++)
        {
            const Node& n = m_layers[N][v];
            total += n.count;
            for (int r = 0; r < (int)n.best.size(); r++)
                all.push_back({ n.best[r].cost, n.best[r].margin, v, r, n.best[r].presses, n.best[r].switches });
        }
        // easiest first: fewest binds pressed, then fewest switches between binds (Best)
        std::stable_sort(all.begin(), all.end(), [](const Pick& a, const Pick& b)
            { return Easier(a.presses, a.switches, a.cost, a.margin, b.presses, b.switches, b.cost, b.margin); });
        m_res.total = total;
        // one line per press sequence: paths that differ only in ride lengths merge (RideText).
        // With every path materialised the count is exact too
        const bool whole = (double)all.size() >= total;
        struct Group { Combo best; std::vector<std::string> variants; };
        std::vector<Group> groups;
        std::unordered_map<std::string, size_t> at;
        for (const Pick& pk : all)
        {
            if (!whole && (int)groups.size() >= m_o.topK) break;
            std::vector<std::string> parts;
            int layer = N, node = pk.node, rank = pk.rank;
            while (layer > 0)
            {
                const Best& b = m_layers[layer][node].best[rank];
                const Edge& e = m_edges[layer][b.edge];
                if (e.arr >= 0) parts.push_back(m_labels[e.arr]);
                if (e.dep >= 0) parts.push_back(m_labels[e.dep]);
                node = e.from; rank = b.rank; layer--;
            }
            std::string text;
            for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            {
                if (!text.empty()) text += " -> ";
                text += *it;
            }
            const std::string key = StripRide(text);
            auto it = at.find(key);
            if (it == at.end())
            {
                at.emplace(key, groups.size());
                groups.push_back({ { text, pk.cost, pk.margin, pk.presses, pk.switches }, { text } });
            }
            else
                groups[it->second].variants.push_back(text);
        }
        for (size_t g = 0; g < groups.size() && (int)m_res.combos.size() < m_o.topK; g++)
        {
            Combo c = groups[g].best;
            c.text = RideText(groups[g].variants, c.text);
            m_res.combos.push_back(c);
        }
        if (whole) m_res.total = (double)groups.size();
        // the first printed combo is the first pick: keep where it ends so its arc can be re-flown
        if (!all.empty() && !m_res.combos.empty()) { m_bestNode = all[0].node; m_bestRank = all[0].rank; }
    }

public:
    // ---- THE DRAWN ARC ----------------------------------------------------------------------
    // The route line used to be marker to marker, straight, which is the one shape the movement
    // never has. This flies the FIRST printed combo again, edge by edge, and hands back the feet
    // height after every cmd of every leg: the real arc, off the real port, of the answer on the
    // panel. Nothing is searched -- each edge already knows the plan that flew it (Edge::sc/jb/ride)
    // and the exact state it arrived in, so the replay is the same ticks a second time.
    // out[i] = leg i (start -> marker 1, marker 1 -> marker 2, ...). The horizontal is still the
    // player's, so routecalc.inl spreads each leg's ticks along the straight line between markers.
    // arc (optional): per leg, per tick, what a DRAWN line needs that z alone cannot say.
    //  * air -- the hull was off the ground. The line spreads the marker-to-marker distance over
    //    those ticks only: a leg can open with a dozen grounded ticks (the bunnyhop stand-up, a
    //    crouch before a walk off, a pinned seam ride) that move nobody anywhere.
    //  * stanceDz -- the +-8.5 the stance teleported inside that tick's z. An air duck does not fly
    //    the player up 8.5u, it swaps a 62u hull for a 45u one around a fixed centre, and drawing it
    //    raw puts a vertical cliff in the middle of every minijump arc.
    bool TraceBest(std::vector<std::vector<float>>& out, std::vector<std::vector<ArcTick>>* arc = nullptr)
    {
        out.clear();
        if (arc) arc->clear();
        if (!m_done || m_bestNode < 0 || m_res.deadPoint >= 0) return false;
        const int N = (int)m_pts.size();
        if ((int)m_layers.size() <= N) return false;

        struct Hop { int layer, from, to, sc, ride; bool jb, duckApex; };
        std::vector<Hop> hops;
        int layer = N, node = m_bestNode, rank = m_bestRank;
        while (layer > 0)
        {
            if (node < 0 || node >= (int)m_layers[layer].size()) return false;
            const std::vector<Best>& bs = m_layers[layer][node].best;
            if (rank < 0 || rank >= (int)bs.size()) return false;
            const Best& b = bs[rank];
            if (b.edge < 0 || b.edge >= (int)m_edges[layer].size()) return false;
            const Edge& e = m_edges[layer][b.edge];
            hops.push_back({ layer - 1, e.from, node, e.sc, e.ride, e.jb, e.duckApex });
            node = e.from; rank = b.rank; layer--;
        }
        std::reverse(hops.begin(), hops.end());

        // a replay must not write the search's own tallies (tick count, closest, highest)
        const Result saved = m_res;
        bool ok = true;
        for (const Hop& h : hops)
        {
            std::vector<float> z;
            std::vector<ArcTick> a;
            State s = m_layers[h.layer][h.from].s;
            if (s.onSeam)
            {
                s.onSeam = false;
                Ride(s, h.ride);
                // the ride holds z still -- and the player, pinned on the seam, goes nowhere
                for (int k = 0; k < h.ride; k++) { z.push_back(s.z); a.push_back({ false, 0.f }); }
            }
            const Leg L = MakeLeg(h.layer, h.from);
            m_rec = &z; m_recArc = arc ? &a : nullptr;
            m_recTo = &m_layers[h.layer + 1][h.to].s; m_recHit = false; m_recLen = 0;
            RunLeg(L, s, { h.sc, h.jb, h.duckApex });
            const bool hit = m_recHit;
            const size_t len = m_recLen;
            m_rec = nullptr; m_recArc = nullptr; m_recTo = nullptr;
            // the replay must reach the SAME arrival the edge was built from -- the key is what the
            // search merges on, so a miss means this is not the flight that made the edge and the
            // line would be a drawing of something that was never on the panel. Draw nothing then.
            if (!hit) { ok = false; break; }
            if (len <= z.size()) z.resize(len);          // stop at the arrival, not at the give-up
            if (len <= a.size()) a.resize(len);
            out.push_back(std::move(z));
            if (arc) arc->push_back(std::move(a));
        }
        m_res = saved;
        if (!ok) { out.clear(); if (arc) arc->clear(); return false; }
        return true;
    }

private:

    // the variants of one press sequence (they differ only in "ride N ticks" steps) as one line:
    // every ride length each ride may take, when the variants together cover every combination of
    // them; otherwise the best variant, flagged
    static std::string RideText(const std::vector<std::string>& variants, const std::string& best)
    {
        using Set = std::vector<char>;
        auto split = [](const std::string& t)
        {
            std::vector<std::string> v;
            size_t pos = 0;
            while (pos <= t.size())
            {
                size_t n = t.find(" -> ", pos);
                if (n == std::string::npos) n = t.size();
                v.push_back(t.substr(pos, n - pos));
                pos = n + 4;
            }
            return v;
        };
        auto parse = [](const std::string& step)                 // "ride 3-9 ticks", "ride 2, 5+ ticks"
        {
            Set s(kMaxRide + 1, 0);
            size_t pos = 5;
            while (pos < step.size() && std::isdigit((unsigned char)step[pos]))
            {
                int lo = 0; while (pos < step.size() && std::isdigit((unsigned char)step[pos])) lo = lo * 10 + (step[pos++] - '0');
                int hi = lo;
                if (pos < step.size() && step[pos] == '+') { hi = kMaxRide; pos++; }
                else if (pos < step.size() && step[pos] == '-') { pos++; hi = 0; while (pos < step.size() && std::isdigit((unsigned char)step[pos])) hi = hi * 10 + (step[pos++] - '0'); }
                for (int r = lo; r <= hi && r <= kMaxRide; r++) s[r] = 1;
                if (step.compare(pos, 2, ", ") == 0) pos += 2;
            }
            return s;
        };
        auto label = [](const Set& s)
        {
            std::string out;
            for (int lo = 0; lo <= kMaxRide; lo++)
            {
                if (!s[lo]) continue;
                int hi = lo; while (hi + 1 <= kMaxRide && s[hi + 1]) hi++;
                if (!out.empty()) out += ", ";
                out += hi == kMaxRide ? std::to_string(lo) + "+" : lo == hi ? std::to_string(lo)
                     : std::to_string(lo) + "-" + std::to_string(hi);
                lo = hi;
            }
            return "ride " + out + " ticks";
        };
        if (variants.size() == 1) return best;
        // per variant: the ride set after each pixelsurf (no step = any length)
        const std::vector<std::string> plain = split(StripRide(best));
        std::vector<size_t> surfAt;
        // EVERY seam catch is followed by a ride, not just a pixelsurf one: a texturebug holds the
        // hull the same way and Expand enumerates its ride lengths the same way. Missing it here
        // left P at 0 while a "ride N ticks" step still arrived, and sets[pi] wrote off the end of
        // an empty vector
        for (size_t k = 0; k < plain.size(); k++)
            if (plain[k].compare(0, 9, "pixelsurf") == 0 || plain[k].compare(0, 10, "texturebug") == 0)
                surfAt.push_back(k);
        const size_t P = surfAt.size();
        std::vector<std::vector<Set>> vs;
        for (const std::string& v : variants)
        {
            std::vector<Set> sets(P, Set(kMaxRide + 1, 1));
            size_t idx = 0, pi = 0;
            for (const std::string& step : split(v))
            {
                if (step.compare(0, 5, "ride ") == 0)
                {
                    for (size_t q = 0; q < P; q++) if (surfAt[q] + 1 == idx) pi = q;
                    if (pi < P) sets[pi] = parse(step);   // a ride with no seam in front of it cannot index
                }
                else idx++;
            }
            vs.push_back(sets);
        }
        std::vector<Set> uni(P, Set(kMaxRide + 1, 0));
        for (const auto& sets : vs)
            for (size_t q = 0; q < P; q++)
                for (int r = 0; r <= kMaxRide; r++) uni[q][r] |= sets[q][r];
        // does the union's product hold only tuples some variant allows?
        std::vector<std::vector<int>> vals(P);
        double prod = 1.0;
        for (size_t q = 0; q < P; q++)
        {
            for (int r = 0; r <= kMaxRide; r++) if (uni[q][r]) vals[q].push_back(r);
            prod *= (double)vals[q].size();
        }
        bool covered = prod <= 200000.0;
        std::vector<size_t> cur(P, 0);
        for (bool more = covered && P > 0; more && covered; )
        {
            bool any = false;
            for (const auto& sets : vs)
            {
                bool okv = true;
                for (size_t q = 0; q < P && okv; q++) okv = sets[q][vals[q][cur[q]]] != 0;
                if (okv) { any = true; break; }
            }
            covered = any;
            size_t q = 0;
            while (q < P && ++cur[q] == vals[q].size()) { cur[q] = 0; q++; }
            more = q < P;
        }
        if (!covered) return best + " (or other ride lengths)";
        std::string out;
        for (size_t k = 0; k < plain.size(); k++)
        {
            out += (out.empty() ? "" : " -> ") + plain[k];
            for (size_t q = 0; q < P; q++)
                if (surfAt[q] == k && k + 1 < plain.size())
                {
                    bool full = true;
                    for (int r = 0; r <= kMaxRide; r++) full = full && uni[q][r];
                    if (!full) out += " -> " + label(uni[q]);
                }
        }
        return out;
    }
};
} // namespace rs
