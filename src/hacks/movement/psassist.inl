// ============================================================================
// psassist.inl -- pixel finder (dih) + PIXELSURF ASSIST
// (botox pixelsurf_assist.cpp).
//
// ROUND 2 (PSPORT 11-04): the old PsSimHop/PsHopInputs/JT_* assist engine (a from-scratch
// radius+snap+ride-hold+jumpbox state machine, NOT botox's design) is deleted. PixelSurfAssistRun
// below is a 1:1 port of botox's n_movement::impl_t::pixelsurf_assist onto the SAME
// n_assist::plan_t simulate-and-execute search (n_assist::arrival_surf, assist_predict.h).
//
// Contract preserved for wallclimb.inl / movement_tricks2.inl / autobounce.inl / movement.cpp /
// render.cpp (every external reader of psassist.inl's exposed state, grepped before this round
// started): s_psAssistHopActive means the same thing it always did ("an assist plan owns the
// cmd's buttons right now, don't fight it") and s_psAssistDuckHeld means the same thing
// ("the assist is forcing duck past the plan's last tick, e.g. a ride"). s_psRidingNow is NOT
// mine -- it is pixelsurf.inl's own ride detector, read here but never set here, unchanged.
//
// Included from movement.cpp (same pattern as texturebug.inl) so it keeps the
// file-static prediction helpers: Pred_Simulate / RestoreToFrame / SimsLeft /
// EntV3 / TraceLine / s_prePredFlags / s_psRidingNow.
//
// The finder half (PixelFinder, below) is dih's and unchanged.
//
// Points persist per map: C:\botox-css\points\<map>.txt. Format versioned by token count on
// load (see PsLoadPointsForMap): old 3/4-token lines ("x y z [typemask]", typemask = the old
// 7-bit mini/regular/miniduck/crouch/long/longduck/dash mask) migrate into botox's 13
// n_assist::point_flags_t bits; new lines carry the full flag set + delta_strafe + restrict-bind
// fields (see PsWritePointList). A point saved by either build keeps loading.
// ============================================================================

#include "pjseam.h"
#include "assist_predict.h"   // botox pixeljump_assist.cpp / bounce_assist.cpp / pixelsurf_assist.cpp sim-search infra
#include "tick_scale.h"
#include <iterator>   // std::size(n_assist::k_programs)

// saved assist target + per-point settings. flags = botox points_check_t's own 13 per-launch-type
// bools (n_assist::point_flags_t, assist_predict.h), default-true like a fresh botox point.
// deltaStrafe = points_check_t::delta_strafe (ps assist type "delta strafe": turn-to-strafe scale
// while airborne). restrict* = botox is_bind_restricted_in_radius (pixelsurf_assist.cpp:562),
// the full 7-group tag split (PSPORT round 3 11-04: n_assist::e_bind, assist_predict.h) -- "ch"
// and "hcj" are separate groups even though both launch_crouch_hop, which round 2's plain
// e_launch comparison could not tell apart.
struct PsAssistPt
{
    Vec3 pos = {};
    n_assist::point_flags_t flags{};
    // per-point engage radius. Botox's points_check_t::radius has no "0 = follow a global
    // slider" convention (every point just carries its own absolute radius) -- CSS keeps radius
    // as a real per-point value too now that the old g_psAssistRadius global is gone; a freshly
    // placed point gets k_psNewPointRadius (see PS_AssistTick's set-point handler).
    float radius = 0.f;
    float deltaStrafe = 0.f;
    bool restrictBind = false;
    n_assist::e_bind restrictGroup = n_assist::bind_j;
    float restrictRadius = 0.f;
    // botox points_check_t::use_custom_first_jump_radius / first_jump_radius (pixelsurf_assist_
    // ground_help's own first-jump radius override) -- ground help here just reads radius (CSS
    // ground_help steers toward the nearest point regardless of jump type, see PixelSurfAssistRun).
    // ---- THE WALL NORMAL, RESOLVED ONCE AND CACHED (runtime only, not in the file) ----
    // The confirm's flush test needs to know which way the wall faces, because the hull's
    // support distance IS |maxs.x*n.x| + |maxs.y*n.y| -- 24 axis-aligned, 33.9 at 45 degrees.
    // recovered n from the origin->point direction, which is circular (the offset is
    // along the normal only AT a catch, which is the thing being tested) and the log
    // shows what it cost: both fires read sup33.9 on an axis-aligned wall, so 36.0 of open air
    // passed as "flush". Re-tracing the wall at pick time answers it directly, works on the
    // points already saved (the file has no normal field), and re-asks every tick until it
    // gets a clean answer, so a bad approach angle heals itself
    Vec3 normal = {};
    bool hasNormal = false;
};

// Shared with the render thread (PS_GetDrawData copies under this lock).
static std::mutex g_psLock;
static std::vector<Vec3> s_psFinderPts;        // finder results, current map only
static std::vector<Vec3> s_psTbPts;            // texturebug seams on the scanned column (red)
// whole-map scan output (psmap.inl): confirmed surf segments, A then B, one pair per line
static std::vector<Vec3> s_psMapLines;
// ...and the texturebug seams the same walk found. Its OWN list, not s_psTbPts: a column scan
// overwrites that one on every release, and a whole-map scan takes minutes to produce this. Both
// are published as red rings and both place a TEXTUREBUG marker (routecalc.inl RcPickFinder)
static std::vector<Vec3> s_psMapTbPts;
static std::vector<PsAssistPt> s_psAssistPts;  // saved assist targets, current map only
static unsigned s_psGen = 0;                   // bumped on load/add/erase -- open ENTER window closes on change
static char s_psMap[128] = {};            // map the lists belong to ("" = none loaded)
static int  s_psAssistActive = -1;        // assist target index for the draw (-1 = none)
static Vec3 s_psLocalOrigin = {};
static unsigned long long s_psAliveMs = 0; // last movement tick that refreshed state

// finder scan state
static bool s_psFinderHeld = false;  // key state last tick (edge detect; keeps needPred up on release)
static bool s_psFinderFirst = false; // scan column anchored this hold
static Vec3 s_psFinderStart = {}, s_psFinderEnd = {}, s_psFinderNormal = {};

// ---- the scan is CHUNKED, cursor lives here (see PixelFinder) ----
// One cmd used to run the whole column: 601 heights x 9 lateral offsets x 2 duck states x up to
// 12 Pred_Simulate = ~130k engine movement ticks in one frame. The work is now split at the
// lateral-offset boundary and spends at most g_psFinderBudget ms per cmd
static bool  s_pfBusy = false;       // scan in flight (also keeps needPred up -- movement.cpp)
static int   s_pfNextI = 0;          // next height step index
static int   s_pfNextLat = 0;        // next lateral offset index inside that height
static bool  s_pfHeightHit = false;  // this height already produced a pin (old bFoundThisHeight)
static int   s_pfSteps = 0;
static float s_pfLow = 0.f, s_pfHigh = 0.f;
static Vec3  s_pfNormal = {}, s_pfTangent = {}, s_pfColumn = {};
static int   s_pfBaseButtons = 0;    // buttons snapshot at release -- probes must not follow live keys
static std::vector<Vec3> s_pfFound;  // pins so far, pre-dedupe
static double s_pfMs = 0.0;          // wall clock actually spent scanning
static int   s_pfChunks = 0, s_pfProbes = 0, s_pfPruned = 0;
// pins found, and how many of them a FALL could actually catch (diag only)
static int   s_pfFalls = 0, s_pfConf = 0;
static int   s_pfCells = 0;          // cells actually visited (cells= vs sims= is the cost)
static int   s_pfNoStand = 0;        // standing pass skipped -- 62u standing hull does not fit here
// pfstrict diag: sealed = pins the hull cannot get out of (clip = of those, a clip brush blocked),
// deep = cells whose wall came from the 48u re-trace (buried start / recessed face)
static int   s_pfSealed = 0, s_pfClipBlk = 0, s_pfDeep = 0;
static int   s_pfProp = 0;           // cells skipped -- the face there is a prop, not brush
static int   s_pfVoid = 0;           // cells skipped -- the spot is inside a brush or off the map
// fall-check memo: successive heights converge on ONE settled catch, ask the fall once per catch
struct PfFallMemo { Vec3 at; bool duck; bool ok; };
static std::vector<PfFallMemo> s_pfFallMemo;

// Unit A3 port: PixelFinder's two MISSING detection sub-modes (gap.md). Own lists -- mixing
// either into s_pfFound would read as a plain pixel-surf pin to the render side.
static std::vector<Vec3> s_pfJumpPts;        // g_psFinderJumpSurf: pins no FALL catches, kept instead of dropped
static std::vector<Vec3> s_pfHeadbouncePts;  // g_psFinderHeadbounce: ascent (ceiling) pins
// g_psFinderPixeljump: botox pixel jump LIPS (pixel_finder.cpp pf_pixel_jump). An up face a hull
// k_pfLipGap off the wall lands on and a hull k_pfLipFloor out does not reach: a ledge narrower
// than 1u, landed on by pressing into the wall (routesim PT_PIXELJUMP). z = the feet on its top
static std::vector<Vec3> s_pfLipPts;
static int s_pfLips = 0;                     // lips found this scan (diag)
// the lip pass has its own cursor, AFTER the pin column: a height ends on its first pin, so the
// centre cell a lip is probed from would be skipped at every height a side cell pinned at
static int s_pfLipI = 0;
// CSS player hull heights (cs_shareddefs/g_CSViewVectors: VEC_HULL_MAX.z 62, VEC_DUCK_HULL_MAX.z 45).
// Were TF2's 82/62: every "standing fits" test asked for 20u of headroom CSS never needs.
static constexpr float k_pfStandHullZ = 62.f;
static constexpr float k_pfDuckHullZ  = 45.f;
static constexpr float k_pfLipGap   = 0.001f;   // botox k_pf_gap_pj
static constexpr float k_pfLipFloor = 1.f;      // botox k_pf_pj_floor
static constexpr float k_pfLipRest  = 0.0325f;  // botox k_pf_gap_rest: where a hard push parks (log only)

// botox pf_pj_taken / pf_pj_add: of lips within 2u of each other only the LOWEST is kept, and a
// lip out of the scanned band is not this scan's
static bool PfLipTaken(const std::vector<Vec3>& pts, float pz, float lo, float hi)
{
    if (pz < lo - 0.5f || pz > hi + 0.5f) return true;
    for (const Vec3& q : pts)
        if (q.z <= pz + 0.05f && pz <= q.z + 2.05f) return true;
    return false;
}
static void PfLipAdd(std::vector<Vec3>& pts, const Vec3& dot)
{
    pts.erase(std::remove_if(pts.begin(), pts.end(),
              [&](const Vec3& q) { return dot.z < q.z && q.z <= dot.z + 2.05f; }), pts.end());
    pts.push_back(dot);
}

// ---- SURF LENGTH (g_psFinderLength, ref finder_length / "draw pixelsurf length") -----------
// ref's finder walks every pin it finds along the wall, 10u a step, re-proving
// it at the same z until it stops holding, and draws the run. A dot says "this height catches
// here"; the line says "and this far either side of here", which is where you can actually line
// up for it. Same here, after the column: 8u steps out each way, the end bisected to 1u, chunked
// on the scan budget like the column itself. Crosshair finder only (the map scan draws its own).
struct PfHit
{
    Vec3  wall;          // on the wall surface, z = the engine's settled pin
    Vec3  n;             // the face normal the cell pinned on (xy, unit)
    float gap = 0.f;     // hull origin offset off that face (support - 0.022 / + 0.001)
    bool  duck = false;
    float ext[2] = { 0.f, 0.f };   // run along the tangent (n.y, -n.x): ext[0] <= 0 <= ext[1]
    float pressOff = 0.f, pressSize = 10.f;   // the hold press that proved it (yaw off "into", wish size)
};
// THE HOLD PROOF ASKS WHAT THE HOLDERS CAN DO (11-08). A pin used to count only if the light 10 u/s
// straight press kept it a second tick; `unheld=` was every one it did not. Auto align's "hold pixel
// surfs" and pixel surf's own hold fly the wall stuck hold set (wallengine_pure.h k_wsMoveStuck, yaw off
// the face), so a pin any of those keeps is a surf you can stay on. Light press first (k = 0), then
// these, each replayed from the same placement (deterministic) for its one proof tick
static constexpr float k_pfHoldOff[] = { 0.f, 45.f, -45.f, 89.9f, -89.9f };
static int s_pfRescued = 0;                  // pins only a hold-set press kept (diag)
// TB SIM PASS (11-08): the CSS texture bug is the box-brush band clip (texturebug.inl header), not a
// texture seam -- it rides anywhere a hull inside the DIST_EPSILON band of a box-brush face gets the
// top plane as its clip. Asked of the engine per height: hull k_pfTbGap off the face, falling and
// sliding, no wish, one tick, END vz on -h (we::TbPinVz, the TB engine's own window). Marked at the
// TOP of every pinned run (where the ride starts), per lateral offset, deduped with the seam marks.
// Own cursor after the lip pass, chunked on the same clock
static constexpr float k_pfTbGap = 0.015625f;   // half the DIST_EPSILON band
static constexpr int   k_pfTbLat = 3;           // band left end, centre, right end
static int  s_pfTbI = 0;                     // next height step of the TB sim pass
static bool s_pfTbPrev[k_pfTbLat][2] = {};   // last height pinned, per offset x stance (run tops)
static int  s_pfTbSims = 0, s_pfTbPins = 0, s_pfTbMarks = 0;
static bool s_pfTbAxis = false;              // anchored face is axis-aligned (a box-brush face can be)
static bool PfSeamProbe(TraceFilterWorldProps& filter, const Vec3& vColumn, const Vec3& vN, float flZ, GameTrace_t& tr);
static bool PfSeamKnown(const std::vector<Vec3>& v, const Vec3& p, float flRadius);
static std::vector<PfHit> s_pfHits;          // lockstep with s_pfFound while !s_pfQuiet
static std::vector<PfHit> s_pfLen;           // deduped pins being walked
static int s_pfLenI = -1;                    // pin being walked (-1 = walk not started)
static int s_pfLenDir = 0, s_pfLenK = 1;     // 0 = toward -tangent, 1 = +; next step index
static int s_pfLenSims = 0;                  // sims the walk spent (diag)
static int s_pfLenLost = 0;                  // pins the walk could not re-prove where found (diag)
static int s_pfUnheld = 0;                   // pins dropped: held one tick, not the next (diag)
static std::vector<Vec3> s_psFinderLines;    // published runs, A then B (under g_psLock)
static constexpr float k_pfLenStep = 8.f;
static constexpr int   k_pfLenMax  = 160;    // steps per side: 1280u, longer than any real wall face

// ---- who is driving the column scanner ------------------------------------------------------
// Two features anchor columns now: the crosshair finder below, and the whole-map scan
// (psmap.inl), which walks the map's own brush edges and hands this scanner one cell at a time.
// The probes, prunes and strict gates are the same code for both -- only the anchor differs.
// The crosshair finder has no idea WHERE on the wall a surf is, so it sweeps a +/-16u band in
// 4u steps; the map scan knows the seam's exact xy, so it sets the band to one cell.
static float s_pfBandHalf = 16.f, s_pfBandStep = 4.f;
// quiet = the map scan owns this job's result and its log line: do not publish the pins as
// crosshair-finder points, do not print a PF: line per cell, and leave s_pfFound to the caller
static bool  s_pfQuiet = false;
static void PmAbort();   // psmap.inl -- defined below fakemove.inl, same translation unit

static void PfCancelScan()
{
    s_pfBusy = false;
    s_pfNextI = 0; s_pfNextLat = 0; s_pfHeightHit = false;
    s_pfFound.clear();
    s_pfFallMemo.clear();
    s_pfJumpPts.clear(); s_pfHeadbouncePts.clear(); s_pfLipPts.clear();
    s_pfHits.clear(); s_pfLen.clear(); s_pfLenI = -1;
    s_pfTbI = 0;
}

// publish the extra finder lists (copy under the render lock, same as PS_GetDrawData)
void PF_GetExtraPts(std::vector<Vec3>& jump, std::vector<Vec3>& headbounce, std::vector<Vec3>& lip)
{
    std::lock_guard<std::mutex> l(g_psLock);
    jump = s_pfJumpPts;
    headbounce = s_pfHeadbouncePts;
    lip = s_pfLipPts;
}

// IVEngineClient::GetLevelName = slot 51 (counted in the interface header against the
// confirmed anchors Time=14 / SetViewAngles=20)
static const char* EngineLevelName()
{
    if (!g_engine) return "";
    const char* n = nullptr;
    __try {
        void** vt = *(void***)g_engine;
        n = ((const char*(__fastcall*)(void*))vt[51])(g_engine);
    } __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "";
}

static void PsPointsFile(const char* map, char* out, size_t n, const char* ext = ".txt")
{
    char safe[128];
    snprintf(safe, sizeof(safe), "%s", (map && map[0]) ? map : "unknown");
    for (char* c = safe; *c; ++c)
        if (*c == '/' || *c == '\\' || *c == ':') *c = '_'; // workshop/x.ugc123 etc.
    snprintf(out, n, "%s\\points\\%s%s", CONFIG_DIR, safe, ext);
}

// mask = 13 bits in n_assist::k_flags order (jump,c_jump,minijump,c_minijump,longjump,c_longjump,
// crouch_hop,high_crouch_jump,c_crouch_hop,c_high_crouch_jump,c_mini_crouch_hop,jumpbug,c_jumpbug).
// A short/missing mask defaults every bit on, same "allow everything" a fresh botox point has.
static n_assist::point_flags_t PsMaskToFlags(int mask)
{
    n_assist::point_flags_t f;
    bool* const bits[] = { &f.jump, &f.c_jump, &f.minijump, &f.c_minijump, &f.longjump, &f.c_longjump,
                            &f.crouch_hop, &f.high_crouch_jump, &f.c_crouch_hop, &f.c_high_crouch_jump,
                            &f.c_mini_crouch_hop, &f.jumpbug, &f.c_jumpbug };
    for (int i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])); i++)
        *bits[i] = (mask & (1 << i)) != 0;
    return f;
}
static int PsFlagsToMask(const n_assist::point_flags_t& f)
{
    const bool bits[] = { f.jump, f.c_jump, f.minijump, f.c_minijump, f.longjump, f.c_longjump,
                           f.crouch_hop, f.high_crouch_jump, f.c_crouch_hop, f.c_high_crouch_jump,
                           f.c_mini_crouch_hop, f.jumpbug, f.c_jumpbug };
    int mask = 0;
    for (int i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])); i++)
        if (bits[i]) mask |= 1 << i;
    return mask;
}

// migrates a PRE-round-2 7-bit typeMask (bits: mini,regular,miniduck,crouch,long,longduck,dash,
// in that PsTypeBit order) onto botox's 13 n_assist::point_flags_t bits. Permissive by design --
// an old point never loses reach, it can only end up trying a couple of extra programs it didn't
// have an exact old-format bit for (miniduck has no separate botox flag, folded into minijump;
// the old "dash" 7th slot -- CS has no air dash -- maps to jumpbug, the nearest "exotic 7th
// launch" rather than being silently dropped).
static n_assist::point_flags_t PsMigrateMask7(const int m7)
{
    n_assist::point_flags_t f{};
    f.jump = f.c_jump = f.minijump = f.c_minijump = f.longjump = f.c_longjump = false;
    f.crouch_hop = f.c_crouch_hop = f.high_crouch_jump = f.c_high_crouch_jump = false;
    f.c_mini_crouch_hop = f.jumpbug = f.c_jumpbug = false;
    if (m7 & (1 << 0)) f.minijump = f.c_minijump = true;              // MINI
    if (m7 & (1 << 1)) f.jump = f.c_jump = true;                      // REGULAR
    if (m7 & (1 << 2)) f.minijump = f.c_minijump = true;               // MINIDUCK -> minijump
    if (m7 & (1 << 3)) { f.crouch_hop = f.c_crouch_hop = f.high_crouch_jump = f.c_high_crouch_jump = true; } // CROUCH
    if (m7 & (1 << 4)) f.longjump = true;                              // LONG
    if (m7 & (1 << 5)) f.c_longjump = true;                            // LONGDUCK
    if (m7 & (1 << 6)) f.jumpbug = f.c_jumpbug = true;                 // AIRDASH/DASHAIM -> jumpbug
    return f;
}

static std::vector<PsAssistPt> PsReadPoints(const char* map, const char* ext)
{
    std::vector<PsAssistPt> pts;
    char path[MAX_PATH];
    PsPointsFile(map, path, sizeof(path), ext);
    if (FILE* f = fopen(path, "r"))
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            PsAssistPt p;
            int mask = 127, restrictBind = 0, restrictGroup = 0;
            float r = 0.f, deltaStrafe = 0.f, restrictRadius = 0.f;
            // new format (PSPORT round 2/3): x y z mask13 radius deltaStrafe restrictBind
            // restrictGroup restrictRadius -- mask13 is n_assist::k_flags order (see
            // PsMaskToFlags), restrictGroup is n_assist::e_bind (round 3: was e_launch)
            int n = sscanf(line, "%f %f %f %d %f %f %d %d %f", &p.pos.x, &p.pos.y, &p.pos.z,
                           &mask, &r, &deltaStrafe, &restrictBind, &restrictGroup, &restrictRadius);
            if (n < 3)
                continue;
            if (n >= 8)
            {
                p.flags = PsMaskToFlags(mask);
                p.restrictBind   = restrictBind != 0;
                p.restrictGroup  = static_cast<n_assist::e_bind>(std::clamp(restrictGroup, 0, (int)n_assist::bind_jb));
                p.restrictRadius = restrictRadius;
            }
            else
            {
                // pre-round-2 line: "x y z [mask7] [radius]", mask7 in PsTypeBit order, 127 = all
                p.flags = (n >= 4) ? PsMigrateMask7(mask & 127) : n_assist::point_flags_t{};
            }
            p.radius = (n >= 5 && r > 0.f) ? r : 0.f;
            p.deltaStrafe = deltaStrafe;
            pts.push_back(p);
        }
        fclose(f);
    }
    return pts;
}

static void PsLoadPointsForMap(const char* map)
{
    std::vector<PsAssistPt> pts = PsReadPoints(map, ".txt");
    PmAbort();             // ...and the map scan's cursor points into the OLD map's brush list
    std::lock_guard<std::mutex> l(g_psLock);
    s_psAssistPts.swap(pts);
    s_psFinderPts.clear(); // finder results are per-session, drop on map change
    s_psFinderLines.clear();
    s_psTbPts.clear();
    s_psMapLines.clear();
    s_psMapTbPts.clear();
    PfCancelScan();        // a chunked scan is anchored to the OLD map's geometry
    s_psAssistActive = -1;
    s_psGen++;
    snprintf(s_psMap, sizeof(s_psMap), "%s", map ? map : "");
}

static void PsWritePointList(const std::vector<PsAssistPt>& pts, const char* ext)
{
    char dir[MAX_PATH], path[MAX_PATH];
    _mkdir(CONFIG_DIR);
    snprintf(dir, sizeof(dir), "%s\\points", CONFIG_DIR);
    _mkdir(dir);
    PsPointsFile(s_psMap, path, sizeof(path), ext);
    FILE* f = fopen(path, "w");
    if (!f) return;
    // new format (PSPORT round 2/3): x y z mask13 radius deltaStrafe restrictBind restrictGroup
    // restrictRadius -- 9 tokens, so PsReadPoints's `n >= 8` check always takes this branch again
    for (const PsAssistPt& p : pts)
        fprintf(f, "%f %f %f %d %f %f %d %d %f\n", p.pos.x, p.pos.y, p.pos.z, PsFlagsToMask(p.flags),
                p.radius, p.deltaStrafe, p.restrictBind ? 1 : 0, static_cast<int>(p.restrictGroup), p.restrictRadius);
    fclose(f);
}

static void PsWritePoints() { PsWritePointList(s_psAssistPts, ".txt"); }

// ---- Pixel finder (hold = stretch a scan column on the aimed wall, release =
// brute-force it for pixel-surf catch heights; ref PixelFinder) ----
//
// PERFORMANCE. Nothing about a probe changed; what changed is that the column no
// longer runs inside ONE cmd. The old release did the lot in a single frame:
//     601 heights x 9 lateral offsets x 2 duck states x up to 12 Pred_Simulate
// = ~130k full engine movement ticks, which is the multi-second hitch on release. Now:
//   * CHUNKED: the cursor (s_pfNextI / s_pfNextLat) survives the cmd, each cmd spends at most
//     g_psFinderBudget ms and the scan carries on next tick. Same probes, same order, same
//     points -- only the wall clock is spread out, and the game keeps running through it.
//   * REGION PRUNE: one stationary hull test, box grown to everything a probe can reach
//     (+/-6u out along the wall normal, 14u down -- the 10-tick fallback probe falls 10.2u at
//     sv_gravity 800 / 66 tick), replaces the 24 movement sims wherever the answer is "no
//     geometry here at all". Conservative by construction: nothing solid in that box means no
//     collision in any probe, so vz can never pin. Column ends, margin overhang and the air
//     above a short wall stop costing sims.
//   * results publish EVERY chunk, so spots appear on screen while the column fills in.
// Probes and prunes are counted and logged (probes= / pruned=) -- that is the evidence for
// whether a slow scan is geometry or budget.

// dedupe collapses runs of adjacent steps that converged on one catch; keep the FIRST pin
// of each run verbatim (the engine's exact settled height). Unchanged from the one-shot scan
// (indices, so s_pfHits -- pushed in lockstep -- dedupes the same way)
static std::vector<int> PfDedupeIdx(const std::vector<Vec3>& vIn)
{
    std::vector<int> vOut;
    if (vIn.empty()) return vOut;
    vOut.push_back(0);
    float flPrevZ = vIn.front().z;
    for (size_t i = 1; i < vIn.size(); i++)
    {
        if (fabsf(vIn[i].z - flPrevZ) > 1.5f)
            vOut.push_back((int)i);
        flPrevZ = vIn[i].z;
    }
    return vOut;
}

static std::vector<Vec3> PfDedupe(const std::vector<Vec3>& vIn)
{
    std::vector<Vec3> vOut;
    for (int i : PfDedupeIdx(vIn)) vOut.push_back(vIn[i]);
    return vOut;
}

// one budgeted slice of the anchored column
static void PfScanChunk(void* pLocal, UserCmd_t* pCmd)
{
    // QPF is fixed for the life of the process -- asking once per chunk is a syscall-shaped
    // call inside the thing this function exists to budget.
    static LARGE_INTEGER s_liFreq = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER liStart{}, liNow{};
    QueryPerformanceCounter(&liStart);
    // THE CHUNK MUST FIT IN A TICK. The slider was a raw ms number with a 32 max and
    // the cfg had it at 32: a tick is 15.15 ms at 66, so every chunk overran the whole command by
    // 2x. The game cannot make cmds while we hold the movement thread, so the scan bought its
    // speed straight out of the frame rate -- 15 chunks x 32 ms is the half second of stutter in
    // the log. Half a tick leaves the rest of the cmd its own time; the slider stays the ceiling,
    // never the floor, and `eff=` on the scan line is what actually ran
    const double flTickMs = (double)TickInterval() * 1000.0;
    const double flBudgetMs = std::min((double)std::clamp(g_psFinderBudget, 0.25f, 32.f),
                                       flTickMs > 0.0 ? flTickMs * 0.5 : 7.5);
    auto ElapsedMs = [&]() -> double
    {
        QueryPerformanceCounter(&liNow);
        return double(liNow.QuadPart - liStart.QuadPart) * 1000.0 / double(s_liFreq.QuadPart);
    };

    TraceFilterWorldProps filter; filter.Setup(pLocal);

    // tick-rate-correct surf velocity: engine pins vz to -halfgravity-per-tick while riding
    const float flHalfGrav = CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f;
    constexpr float flSurfTol = 1.5f; // same pin tolerance PixelSurf uses

    const int iBackupButtons = pCmd->buttons;
    const float flBackupForward = pCmd->forwardmove;
    const float flBackupSide = pCmd->sidemove;
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    // the player is standing in the map, so the out-of-bounds test has to say so (movement.cpp)
    MV_ValidateOutsideWorld(EntV3(pLocal, g_offOrigin) + Vec3(0.f, 0.f, 40.f));

    // pin the base prediction frame; every probe restores to it (bare restore drifts)
    const int nBasePredicted = PredCommandsPredicted();

    // Put the RESTORED hull airborne at vAt, in stance bDuck, so that after the probe's first
    // Duck() it really sits at vAt. Call right after RestoreToFrame.
    //  * CSS air duck/unduck moves the origin 0.5*(62-45) = 8.5u (cs_gamemovement.cpp FinishDuck /
    //    FinishUnDuck) when the stance differs from the base frame's. TF2's 10u kept the sub-unit
    //    catch phase (flExact) intact; 8.5 flips it by half a unit, so every ducked probe off a
    //    standing base landed 0.5u off the catch -- the "finder misses most surfs" report.
    //  * the ground HANDLE, not just FL_ONGROUND: sv_optimizedmovement 1 skips the recategorize
    //    before Duck(), so a base frame on the ground ran the probe's duck as an on-ground timed
    //    duck (no shift, then a late air FinishDuck +8.5 mid-probe)
    auto PfPlace = [&](Vec3 vAt, bool bDuck)
    {
        constexpr float k_cssAirDuckShift = 8.5f;
        const bool bBaseDuck = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
        if (bDuck && !bBaseDuck) vAt.z -= k_cssAirDuckShift;
        else if (!bDuck && bBaseDuck) vAt.z += k_cssAirDuckShift;
        g_fnSetAbsOrigin(pLocal, vAt);
        EntSetV3(pLocal, g_offOrigin, vAt);
        EntSetI(pLocal, g_offFlags, EntI(pLocal, g_offFlags) & ~FL_ONGROUND);
        if (g_offGroundEntity > 0) EntSetI(pLocal, g_offGroundEntity, -1);
    };

    const Vec3 vWallNormal = s_pfNormal;
    const Vec3 vTangent = s_pfTangent;      // wall tangent for the lateral band sweep
    const float flBandHalf = s_pfBandHalf;                       // crosshair finder: 16
    const float flBandStep = s_pfBandStep > 0.f ? s_pfBandStep : 4.f;
    const int   iLatCount = int(flBandHalf * 2.f / flBandStep) + 1;   // 9: -16..+16 by 4

    // the hold press the last successful TryPin proved with (PfHit.pressOff / pressSize)
    float flWonOff = 0.f, flWonSize = 10.f;

    // one probe per height: PLACE the hull at DNA's exact sub-unit catch offsets, seed the
    // already-pinned descent speed, ask the engine with a predicted tick (pinned vz = catch)
    auto TryPin =[&](const Vec3& vTestOrigin, const Vec3& vN, bool bDuck, float& flPinZ) -> bool
    {
        const Vec3 vInto = Vec3(-vN.x, -vN.y, 0.f);
        Vec3 vIntoAngle = VectorAngles(vInto);
        ClampAnglesMv(vIntoAngle);
        const float flRot = DEG2RADF(vIntoAngle.y - pCmd->viewangles.y);
        const float flCos = cosf(flRot), flSin = sinf(flRot);

        // flFloorZ: stop the drop once the hull has fallen past what THIS cell is
        // responsible for. Free fall can never pin (|vz + halfgrav| only grows), and every
        // height below is scanned by its own cell, so the ticks after that are pure duplication.
        // It is a FLOOR, not a tick count: if geometry slows the hull it stays above the floor
        // for longer and keeps all 10 ticks, which is exactly the case worth paying for
        auto Probe = [&](float flFeetZ, float flSeedVz, int iTicks, float flFloorZ = -1e9f) -> bool
        {
            // fly the light press from the placement; returns the tick it pinned on (-1 = none).
            // Deterministic, so a replay to the same tick lands in the same pinned state
            auto Fly = [&](int iMax) -> int
            {
                RestoreToFrame(nBasePredicted - 1);

                PfPlace(Vec3(vTestOrigin.x, vTestOrigin.y, flFeetZ), bDuck);   // airborne on the lip
                // light into-wall press only -- big speed pins flat on the main wall and falls past the lip
                EntSetV3(pLocal, g_offVelocity, vInto * 10.f + Vec3(0.f, 0.f, flSeedVz));

                // s_pfBaseButtons, not the live cmd: the scan spans cmds now, and a key the user
                // taps mid-scan must not change the probe half the column already ran with
                pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
                if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                pCmd->forwardmove = flCos * 10.f;
                pCmd->sidemove = -flSin * 10.f;

                for (int t = 0; t < iMax; t++)
                {
                    Pred_Simulate(pLocal, pCmd);
                    s_pfProbes++;
                    pCmd->buttons &= ~IN_JUMP;

                    if (fabsf(EntV3(pLocal, g_offVelocity).z + flHalfGrav) < flSurfTol)
                    {
                        flPinZ = EntV3(pLocal, g_offOrigin).z; // the engine's own settled catch height
                        return t;
                    }
                    if (EntV3(pLocal, g_offOrigin).z < flFloorZ)
                        break;                                 // fell out of this cell's own step
                }
                return -1;
            };

            const int tPin = Fly(iTicks);
            if (tPin < 0)
                return false;
            // HOLD PROOF (ref runs ProcessMovement twice and wants vz ==
            // -g*dt/2 after both, ground entity null). One pinned tick is also what a hull
            // grazing a lip on its way past reads like; a surf is a pin that is STILL a pin
            // on the next tick, at the same height, in the air. Light press, then the hold set
            const float flPinAt = flPinZ;
            const int nCand = 1 + (int)(std::size(k_pfHoldOff) * std::size(we::k_wsMoveStuck));
            for (int k = 0; k < nCand; k++)
            {
                float flOff = 0.f, flSize = 10.f;
                if (k > 0)
                {
                    flOff  = k_pfHoldOff[(k - 1) / std::size(we::k_wsMoveStuck)];
                    flSize = we::k_wsMoveStuck[(k - 1) % std::size(we::k_wsMoveStuck)];
                    if (Fly(tPin + 1) != tPin)
                        break;                             // replay drifted: nothing to prove from
                    we::WishToCmd(vIntoAngle.y + flOff, flSize, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
                }
                Pred_Simulate(pLocal, pCmd);
                s_pfProbes++;
                if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND)
                    && fabsf(EntV3(pLocal, g_offVelocity).z + flHalfGrav) < flSurfTol
                    && fabsf(EntV3(pLocal, g_offOrigin).z - flPinAt) < 0.1f)
                {
                    flPinZ = flPinAt;
                    flWonOff = flOff; flWonSize = flSize;
                    if (k > 0) s_pfRescued++;
                    return true;
                }
            }
            s_pfUnheld++;
            return false;
        };

        // exact placement at the DNA catch offset (1 tick decisive), then a gentle
        // fallback drop sweeping the rest of this 1u step
        //
        // cut this to a flat 4 ticks and put the 10 back (two probe changes shipped
        // in one build; the flat-face prune was the guilty one). keeps all 10 ticks and
        // adds a FLOOR instead: 26000 sims / 1376 cells = 18.9, i.e. the fallback is the whole
        // cost, and at -8 u/s under sv_gravity 800/66t it falls 10.2u -- ten 1u steps the loop
        // scans again on their own. Free fall cannot pin, so once the hull is below this cell's
        // own step the remaining ticks are duplication. Geometry that slows the hull keeps it
        // above the floor and keeps every tick, which is the only case those ticks ever earned
        //
        // the floor was flExact - 1.2, a WHOLE UNIT below the step this cell owns, so
        // every cell re-flew the cell below's unit as well. The drop from +0.9 at -8 u/s reads
        // +0.69, +0.30, -0.27, -1.02, -1.95 (sv_gravity 800, 66.67 tick) and the last two are the
        // next cell down's job. Floored at its own step (-0.1; the pin check runs BEFORE the floor
        // check, so the -0.27 tick still counts) that unit is still swept by the cell below --
        // from a FRESH -8, i.e. slower than this hull arrives there and easier to pin, not harder.
        // 5 sims -> 3 on the fallback, which is where the scan spends nearly all of its time
        const float flT = truncf(vTestOrigin.z);
        const float flExact = vTestOrigin.z >= 0.f ? flT + 0.0287018f : flT - 0.972092f;
        if (Probe(flExact, -flHalfGrav, 2))
            return true;
        return Probe(flExact + 0.9f, -8.f, 10, flExact - 0.1f);
    };

    // ---- HEADBOUNCE (g_psFinderHeadbounce): the same pin test, mirrored for an ASCENT catch
    // against a ceiling. TryPin above seeds a descent speed and asks vz to settle at -halfgrav;
    // this seeds a rise and asks vz to settle at +halfgrav -- same discriminator HeadBounce
    // (movement.cpp) already trusts, just the other sign. One probe, not TryPin's two-step
    // fallback: an ascent pin either catches at the seeded offset or it is not this cell's catch
    auto TryHeadbounce = [&](const Vec3& vTestOrigin, bool bDuck, float& flPinZ) -> bool
    {
        RestoreToFrame(nBasePredicted - 1);
        PfPlace(vTestOrigin, bDuck);
        EntSetV3(pLocal, g_offVelocity, Vec3(-vWallNormal.x, -vWallNormal.y, 0.f) * 10.f + Vec3(0.f, 0.f, flHalfGrav));
        pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
        if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        for (int t = 0; t < 4; t++)
        {
            Pred_Simulate(pLocal, pCmd);
            s_pfProbes++;
            if (fabsf(EntV3(pLocal, g_offVelocity).z - flHalfGrav) < flSurfTol)
            {
                flPinZ = EntV3(pLocal, g_offOrigin).z;
                return true;
            }
        }
        return false;
    };

    // ---- CAN A FALL CATCH THIS, or does it only HOLD a pin already there? ----------
    // TryPin above proves a point by TELEPORTING the hull to the catch offset with the pin's
    // own velocity already on it, then asking whether the next tick keeps it. That is not the
    // question the assist has to answer. The assist ARRIVES -- falling at 50 to 95 u/s, with
    // the ladder's sub-unit phase decided back at the launch -- and a point that only holds a
    // pin can never be jumped onto, whatever the picker does. Three builds of picker work went
    // into a question that may never have had a yes in it, and nothing has ever measured which.
    // So every pin the scan keeps is re-asked as a FALL: swept across one whole tick of travel
    // (the phase an arc cannot choose) at two real arrival speeds.
    // pfstrict: with "strict" on (default) a pin that no fall catches is DROPPED -- it is a
    // teleport-only artifact, not a surf. This is a movement sim, not a trace screen, so the
    // lesson (traces cannot see seams) does not apply. strict off = count only, as before.
    // `conf=N/M` on the scan line; conf far below M with strict off = how much noise it removes
    auto TryFall = [&](const Vec3& vTestOrigin, const Vec3& vN, float flPinZ, bool bDuck) -> bool
    {
        const Vec3 vInto = Vec3(-vN.x, -vN.y, 0.f);
        Vec3 vIntoAngle = VectorAngles(vInto);
        ClampAnglesMv(vIntoAngle);
        const float flRot = DEG2RADF(vIntoAngle.y - pCmd->viewangles.y);

        // pfstrict -- this used to bail on SimsLeft(), which is the TB/PS 0.40-tick clock + the
        // 384 cap. The finder's own probes (7.5ms chunks) had already spent both, so TryFall
        // mostly returned false for budget reasons and conf= never measured anything. The finder
        // is bounded by its own chunk clock; one TryFall is at most 2 x 30 x 4 sims.
        // Speeds: apex-ish hop arrivals (-30) up to a real drop (-260). Span = one tick of travel
        // at THAT speed -- a fast fall crosses more height per tick, so its phase window is wider
        static const float kFallVz[5] = { -30.f, -60.f, -100.f, -170.f, -260.f };
        constexpr int iFallSteps = 6;
        // the press the hull arrives with: the light one, then auto align's own (straight in, full
        // wish = the 30 u/s air cap, PsPressInto). One pinned tick is all "hold pixel surfs" needs
        static const float kFallPress[2] = { 10.f, 450.f };

        for (int p = 0; p < 2; p++)
        for (int s = 0; s < 5; s++)
            for (int i = 0; i < iFallSteps; i++)
            {
                const float flFallSpan = std::max(1.f, -kFallVz[s] * TickInterval());
                RestoreToFrame(nBasePredicted - 1);
                PfPlace(Vec3(vTestOrigin.x, vTestOrigin.y, flPinZ + flFallSpan * (float(i) / float(iFallSteps))), bDuck);
                EntSetV3(pLocal, g_offVelocity, vInto * 10.f + Vec3(0.f, 0.f, kFallVz[s]));

                pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
                if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                pCmd->forwardmove = cosf(flRot) * kFallPress[p];
                pCmd->sidemove = -sinf(flRot) * kFallPress[p];

                for (int t = 0; t < 4; t++)
                {
                    const float flPreVz = EntV3(pLocal, g_offVelocity).z;
                    Pred_Simulate(pLocal, pCmd);
                    s_pfProbes++;
                    pCmd->buttons &= ~IN_JUMP;
                    if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
                        break;                  // that is a landing, not a surf
                    const float flPostVz = EntV3(pLocal, g_offVelocity).z;
                    // the same pair PixelSurf and PsSimHop use: at -halfgrav AND off the
                    // free-fall ladder, so a graze through the window is refused
                    if (fabsf(flPostVz + flHalfGrav) < flSurfTol
                        && fabsf(flPostVz - (flPreVz - flHalfGrav * 2.f)) > flSurfTol)
                        return true;
                    // same floor TryPin's fallback has, and this pass never had one.
                    // -260 crosses 3.9u a tick: one tick puts the hull under the pin and ticks
                    // 2..4 were free fall 8, 12, 16u below it. A hull SLOWED by the seam stays
                    // above the floor and keeps all 4, which is the only case they earned
                    if (EntV3(pLocal, g_offOrigin).z < flPinZ - 2.f)
                        break;
                }
            }
        return false;
    };

    // pfstrict -- CAN THE HULL GET HERE AT ALL? Every probe TELEPORTS the hull onto the seam, so
    // a seam sealed off from the open map (behind a clip brush, inside a pocket) pins as well as a
    // real one. A real arrival comes from above or from out in front, so the catch hull must be
    // able to leave the spot one of those two ways: 48u straight up, or 32u straight out.
    // 0.25u off the wall first -- the probe origin is embedded 0.022u into brush walls.
    // ponytail: local test only -- a sealed pocket bigger than 48u up / 32u out still passes;
    // a real flood fill from the player's position is the upgrade if that shows up
    auto Reachable = [&](const Vec3& vAt, const Vec3& vN, bool bDuck) -> bool
    {
        constexpr unsigned kContentsPlayerClip = 0x10000;   // bspflags.h CONTENTS_PLAYERCLIP
        const Vec3 vHullMaxs(vMaxs.x, vMaxs.y, bDuck ? k_pfDuckHullZ : k_pfStandHullZ);
        const Vec3 vStart = vAt + vN * 0.25f;
        GameTrace_t up{}, out{};
        TraceHull(vStart, vStart + Vec3(0.f, 0.f, 48.f), vMins, vHullMaxs, MASK_PLAYERSOLID, &filter, &up);
        if (!up.startsolid && up.fraction >= 1.f)
            return true;
        TraceHull(vStart, vStart + vN * 32.f, vMins, vHullMaxs, MASK_PLAYERSOLID, &filter, &out);
        if (!out.startsolid && out.fraction >= 1.f)
            return true;
        s_pfSealed++;
        if ((unsigned)(up.contents | out.contents) & kContentsPlayerClip)
            s_pfClipBlk++;
        return false;
    };

    // fall check, memoized per settled catch (see s_pfFallMemo)
    auto FallCatches = [&](const Vec3& vTestOrigin, const Vec3& vN, float flPinZ, bool bDuck) -> bool
    {
        const Vec3 vAt(vTestOrigin.x, vTestOrigin.y, flPinZ);
        for (const PfFallMemo& m : s_pfFallMemo)
            if (m.duck == bDuck && fabsf(m.at.z - vAt.z) < 0.01f
                && fabsf(m.at.x - vAt.x) < 0.5f && fabsf(m.at.y - vAt.y) < 0.5f)
                return m.ok;
        s_pfFalls++;
        const bool bOk = TryFall(vTestOrigin, vN, flPinZ, bDuck);
        if (bOk) s_pfConf++;
        s_pfFallMemo.push_back({ vAt, bDuck, bOk });
        return bOk;
    };

    // one (height, lateral offset) cell -- the indivisible unit of the budget
    // bLipOnly: the lip pass (s_pfLipI) -- re-acquire the face here, probe for a lip, nothing else
    auto ScanCell = [&](float flZ, float flLat, bool bLipOnly = false)
    {
        // re-trace the wall at THIS height/offset (short +/-12u ray; the lip protrudes
        // at its own height only, and a long ray skims run-up terrain on low surfs)
        const Vec3 vProbeMid = Vec3(s_pfColumn.x, s_pfColumn.y, flZ) + vTangent * flLat;
        GameTrace_t wallTrace{};
        TraceLine(vProbeMid + vWallNormal * 12.f, vProbeMid - vWallNormal * 12.f,
                  MASK_PLAYERSOLID, &filter, &wallTrace);

        // pfstrict -- two cases the 12u ray got wrong, both common on CLIP brushes:
        //  * START BURIED: something thicker than 12u stands in front of the anchor plane at this
        //    height (smoothing clip over a detailed wall). The ray skipped it, hit the wall BEHIND,
        //    the hull spawned inside the clip and the cell died on startsolid -- the face the hull
        //    actually touches was never scanned. Re-ask from 48u out.
        //  * MISS: the face at this height is recessed past 12u. Re-ask 48u deeper.
        // The retry must face the same way as the anchored wall (dot > 0.7), which is what keeps
        // it off run-up terrain and off unrelated walls/props in front
        if (wallTrace.startsolid || wallTrace.fraction >= 1.f)
        {
            const bool bBuried = wallTrace.startsolid;
            GameTrace_t retry{};
            TraceLine(vProbeMid + vWallNormal * (bBuried ? 48.f : 12.f),
                      vProbeMid - vWallNormal * (bBuried ? 12.f : 48.f),
                      MASK_PLAYERSOLID, &filter, &retry);
            Vec3 vRn(retry.plane.normal.x, retry.plane.normal.y, 0.f);
            if (!retry.startsolid && retry.fraction < 1.f && fabsf(retry.plane.normal.z) <= 0.7f
                && vRn.Length2D() > 0.1f)
            {
                vRn.Normalize();
                if (vRn.x * vWallNormal.x + vRn.y * vWallNormal.y > 0.7f)
                {
                    wallTrace = retry;
                    s_pfDeep++;
                }
            }
        }

        // A PROP IN THE COLUMN IS NOT A WALL. The re-trace above hits whatever stands
        // in front of the anchored face first, and on a crate/barrel/sign that is the prop: the
        // cell then spawns the hull on the prop's own plane and every pin the 24 sims find is the
        // prop's. That is the report -- the finder saving points on props, and the assist then
        // firing at them. Cheap and exact: the trace already knows
        // now behind the "ignore props" checkbox (g_psFinderNoProps, on by default);
        // off puts the pre-behaviour back and props count as walls again
        if (g_psFinderNoProps && wallTrace.fraction < 1.f && MV_TraceIsProp(wallTrace))
            { s_pfProp++; return; }

        Vec3 vN, vWallXY;
        bool bDispWall = false;
        bool bReacquired = false;
        if (wallTrace.fraction < 1.f && fabsf(wallTrace.plane.normal.z) <= 0.7f
            && Vec3(wallTrace.plane.normal.x, wallTrace.plane.normal.y, 0.f).Length2D() > 0.1f)
        {
            bReacquired = true;
            vN = Vec3(wallTrace.plane.normal.x, wallTrace.plane.normal.y, 0.f);
            vN.Normalize();
            vWallXY = wallTrace.endpos;
            bDispWall = (wallTrace.dispFlags & DISPSURF_SURFACE) != 0;
        }
        else
        {
            // no clean re-acquire here -- fall back to the anchored wall plane; the
            // embed check + probes reject anything not actually there
            vN = vWallNormal;
            vWallXY = vProbeMid;
        }

        // feet origin at the AABB support distance for THIS column's normal; embed
        // 0.022u INTO brush walls (DNA), sit 0.001u OFF displacements (triangle soup)
        const float flSupport = fabsf(vMaxs.x) * fabsf(vN.x) + fabsf(vMaxs.y) * fabsf(vN.y);
        const float flGap = flSupport + (bDispWall ? 0.001f : -0.022f);
        const Vec3 vTestOrigin = Vec3(vWallXY.x, vWallXY.y, flZ) + vN * flGap;

        // ---- IS THIS SPOT ANYWHERE A PLAYER COULD EVER BE? -----------------------------------
        // Every test below this line is a SWEPT trace or a sim, and a sweep reports where it
        // stopped, never whether the place it started from is inside the map. A whole-map brush
        // walk hands this scanner the top edge of every wall face in the BSP, including the ones
        // buried inside other brushes and the OUTSIDE faces of the shell that seals the map, and
        // those are the "surfs inside walls / out of bounds" on the overlay. One leaf lookup each,
        // ahead of ~24 sims, so it is a speed win on the cells it kills as well
        if (MV_PointUnusable(vTestOrigin + Vec3(0.f, 0.f, 8.f))) { s_pfVoid++; return; }

        // ---- PIXELJUMP LIP (g_psFinderPixeljump, botox pf_pixel_jump "lip" probe) ---------------
        // Centre column only (its own pass), on a face the ray really hit: a ducked hull k_pfLipGap
        // off the face, swept from just over this height 2.5u down, lands on an up face (nz >= 0.7)
        // = something to stand on right at the wall. If a hull k_pfLipFloor further out lands on
        // the same plane too it is a floor, not a lip. Two traces, no sims
        if (bLipOnly)
        {
            if (!bReacquired) return;
            const Vec3 vLipMaxs(vMaxs.x, vMaxs.y, k_pfDuckHullZ);
            auto At = [&](float off) { return Vec3(vWallXY.x, vWallXY.y, 0.f) + vN * (flSupport + off); };
            const Vec3 a = At(k_pfLipGap);
            GameTrace_t lip{};
            TraceHull(Vec3(a.x, a.y, flZ + 0.1f), Vec3(a.x, a.y, flZ - 2.5f), vMins, vLipMaxs,
                      MASK_PLAYERSOLID, &filter, &lip);
            if (!lip.startsolid && lip.fraction < 1.f && lip.plane.normal.z >= 0.7f
                && !(g_psFinderNoProps && MV_TraceIsProp(lip)))
            {
                const float pz = lip.endpos.z;
                const Vec3 ln = lip.plane.normal;
                // the same plane `out` from the face: sloped lips drop with distance, so the probe
                // height follows the plane (botox reaches())
                auto Reaches = [&](float out)
                {
                    const Vec3 p = At(out);
                    const float z = pz - (out - k_pfLipGap) * (ln.x * vN.x + ln.y * vN.y) / ln.z;
                    GameTrace_t t{};
                    TraceHull(Vec3(p.x, p.y, z + 0.1f), Vec3(p.x, p.y, z - 0.1f), vMins, vLipMaxs,
                              MASK_PLAYERSOLID, &filter, &t);
                    return !t.startsolid && t.fraction < 1.f && t.plane.normal.z >= 0.7f;
                };
                bool bTaken;
                { std::lock_guard<std::mutex> l(g_psLock); bTaken = PfLipTaken(s_pfLipPts, pz, s_pfLow, s_pfHigh); }
                if (!bTaken && !Reaches(k_pfLipFloor))
                {
                    const bool bNatural = Reaches(k_pfLipRest);
                    {
                        std::lock_guard<std::mutex> l(g_psLock);
                        PfLipAdd(s_pfLipPts, Vec3(vWallXY.x, vWallXY.y, pz));
                    }
                    s_pfLips++;
                    DebugLog("PF: pixeljump lip z=%.4f (probe %.1f) %s", pz, flZ,
                             bNatural ? "natural (a hard press lands it)" : "tight (creep in to land it)");
                }
            }
            return;
        }

        // ---- THE FLAT-FACE PRUNE IS DELETED. FALSIFIED BY ITS OWN DIAG --------------
        // It swept three player hulls over the slabs a probe can reach and skipped the cell when
        // all three stopped at the same distance. The log: `cells=1620 sims=216 flat=1602` ->
        // **0 spot(s)**, on five separate columns. 98.9% of every column pruned and the finder
        // stopped finding anything at all.
        // WHY IT CANNOT WORK, and this is the reason an earlier build's note gave in advance: a pixel surf
        // pin is an artifact of the COLLISION SOLVER, not of visible geometry. The classic TF2
        // seam is the junction between two flush, coplanar brush faces -- there is nothing there
        // for a trace to measure, the hull sweeps across it at a constant fraction, and the catch
        // only exists because CGameMovement clips against the two brushes separately. **No trace
        // can screen for it. Only the movement sim can answer, which is why the finder sims.**
        // Do not re-derive this with a finer eps, more slabs, or a different trace shape.

        // REGION PRUNE -- ONLY on the fallback path, i.e. the +/-12u ray found no wall at
        // this height and the cell is running on the anchored plane on spec. Is there anything
        // here at all for a probe to hit? The box is the hull grown by every distance a probe can
        // travel: 1.5u of into-wall creep (10 u/s for 10 ticks) padded to 6, and 10.2u of fall on
        // the second probe padded to 14. Empty box = no collision on any of the 24 sims below =
        // vz never pins, so the answer is already known. Costs one hull trace instead.
        // Gated on !bReacquired on purpose: where the ray DID hit a wall the sims are probing
        // real geometry, and a stationary hull test is not the thing to arbitrate that with --
        // displacements are triangle soup with no inside for startsolid to report
        if (!bReacquired)
        {
            const Vec3 vRegionMins(vMins.x - 6.f, vMins.y - 6.f, vMins.z - 14.f);
            const Vec3 vRegionMaxs(vMaxs.x + 6.f, vMaxs.y + 6.f, k_pfStandHullZ);
            GameTrace_t regionTrace{};
            TraceHull(vTestOrigin, vTestOrigin, vRegionMins, vRegionMaxs,
                      MASK_PLAYERSOLID, &filter, &regionTrace);
            if (!regionTrace.startsolid && !regionTrace.allsolid)
            {
                s_pfPruned++;
                return;
            }
        }

        // ---- does the hull the probe spawns FIT where it spawns it? ---------------------------
        // under a lip the 62u standing hull (CSS VEC_HULL_MAX) is buried
        // in the ceiling and the standing pass spent 12 sims proving it. NOT the falsified
        // flat-face prune: it screens nothing about the seam, only the spawn.
        // ORDER. This was two hull traces ahead of the region prune, so an empty cell
        // paid three traces to reach a return. The prune goes first, and the 62u hull CONTAINS
        // the 45u one (same mins, taller maxs), so a standing fit IS a ducked fit and the duck
        // trace only runs where standing failed. Empty cell: 1 trace, was 3
        const Vec3 vStandMaxs = Vec3(vMaxs.x, vMaxs.y, k_pfStandHullZ);
        GameTrace_t standTrace{};
        TraceHull(vTestOrigin, vTestOrigin, vMins, vStandMaxs, MASK_PLAYERSOLID, &filter, &standTrace);
        const bool bStandFits = !standTrace.startsolid && !standTrace.allsolid;
        if (!bStandFits)
        {
            s_pfNoStand++;
            // reject embedded spawns -- DUCK hull (45 tall): low surfs fail a standing fit
            const Vec3 vDuckMaxs = Vec3(vMaxs.x, vMaxs.y, k_pfDuckHullZ);
            GameTrace_t solidTrace{};
            TraceHull(vTestOrigin, vTestOrigin, vMins, vDuckMaxs, MASK_PLAYERSOLID, &filter, &solidTrace);
            if (solidTrace.startsolid || solidTrace.allsolid)
                return;
        }

        // standing, then ducked (most pixel surfs are ducked)
        for (int iDuck = 0; iDuck < 2 && !s_pfHeightHit; iDuck++)
        {
            if (iDuck == 0 && !bStandFits) continue;   // 62u standing hull does not fit here

            float flPinZ = flZ;
            if (TryPin(vTestOrigin, vN, iDuck != 0, flPinZ))
            {
                const bool bDuck = iDuck != 0;
                // AND ASK THE PIN WHAT HELD IT. The gate above proves the FACE the cell
                // aimed at is brush; it cannot prove the thing that clipped vz was. A crate parked
                // against a real wall (every TF2 map) pins the sim exactly like a seam, which is
                // an earlier build's own finding for the live surf -- the finder never asked. MV_PinOnProp
                // is that question, run at the settled origin: nearest brush face vs nearest prop
                // face, prop closer = the prop is what we are resting on. 16 hull traces, and only
                // on a pin, so it costs nothing on the cells that find nothing
                if (g_psFinderNoProps
                    && MV_PinOnProp(Vec3(vTestOrigin.x, vTestOrigin.y, flPinZ), vMins,
                                    Vec3(vMaxs.x, vMaxs.y, bDuck ? k_pfDuckHullZ : k_pfStandHullZ), filter))
                {
                    s_pfProp++;
                    continue;
                }
                // pfstrict: a failed pin no longer ends this height -- the next lateral offset /
                // duck state gets its turn, which is where the extra real surfs come from
                const bool bFall = FallCatches(vTestOrigin, vN, flPinZ, bDuck);
                const bool bReach = Reachable(Vec3(vTestOrigin.x, vTestOrigin.y, flPinZ), vN, bDuck);
                // g_psFinderJumpSurf: a pin no FALL catches but the pocket is open is a real
                // catch too -- just one a JUMP into the seam has to make, not a drop onto it.
                // Own list, own marker (orange): mixing it into s_pfFound would read as a plain
                // surf. The route / pixel calculator place a pixelsurf on it and fly the jumps.
                // Successive heights settle on ONE catch: kept once (same z within 0.02, 2u xy),
                // which s_pfFound gets from PfDedupe and this list never had
                // (not for the map scan: these lists are drawn as crosshair-finder dots)
                if (!bFall && g_psFinderJumpSurf && bReach && !s_pfQuiet)
                {
                    const Vec3 vPin(vWallXY.x, vWallXY.y, flPinZ);
                    std::lock_guard<std::mutex> l(g_psLock);
                    const bool bDup = std::any_of(s_pfJumpPts.begin(), s_pfJumpPts.end(), [&](const Vec3& q)
                        { return fabsf(q.z - vPin.z) < 0.02f && (q - vPin).Length2D() < 2.f; });
                    if (!bDup)
                    {
                        s_pfJumpPts.push_back(vPin);
                        DebugLog("PF: jump surf pin z=%.4f duck=%d", flPinZ, (int)bDuck);
                    }
                    s_pfHeightHit = true;
                    continue;
                }
                // THE MAP SCAN IS ALWAYS STRICT. The checkbox is about a column you aimed at, where
                // you can see what the extra rings are and judge them yourself. A whole-map scan
                // draws thousands of lines on geometry you never looked at, so a pin no fall can
                // catch, or one sealed off from the open map, is noise with nothing to weigh it
                // against -- and that is the "finds a lot of surfs inside walls" half of the report
                if ((g_psFinderStrict || s_pfQuiet) && (!bFall || !bReach))
                    continue;
                // store ON the wall surface at the engine's settled height
                s_pfFound.push_back(Vec3(vWallXY.x, vWallXY.y, flPinZ));
                if (!s_pfQuiet)
                {
                    PfHit h;
                    h.wall = s_pfFound.back(); h.n = vN; h.gap = flGap; h.duck = bDuck;
                    h.pressOff = flWonOff; h.pressSize = flWonSize;
                    s_pfHits.push_back(h);
                }
                s_pfHeightHit = true;
            }
            else if (g_psFinderHeadbounce && !s_pfQuiet)
            {
                // g_psFinderHeadbounce: TryPin found no descent catch here -- try the ceiling
                // (ascent) catch instead. Same column, same height step, opposite sign
                float flHbZ = flZ;
                if (TryHeadbounce(vTestOrigin, iDuck != 0, flHbZ))
                {
                    std::lock_guard<std::mutex> l(g_psLock);
                    s_pfHeadbouncePts.push_back(Vec3(vWallXY.x, vWallXY.y, flHbZ));
                    s_pfHeightHit = true;
                    DebugLog("PF: headbounce pin z=%.1f duck=%d", flHbZ, (int)(iDuck != 0));
                }
            }
        }
    };

    while (s_pfNextI <= s_pfSteps)
    {
        // height done (a pin was found, or all 9 offsets are spent) -- step up
        if (s_pfHeightHit || s_pfNextLat >= iLatCount)
        {
            s_pfNextI++;
            s_pfNextLat = 0;
            s_pfHeightHit = false;
            continue;
        }

        const float flZ = s_pfLow + float(s_pfNextI);
        const float flLat = -flBandHalf + flBandStep * float(s_pfNextLat);
        s_pfNextLat++;
        s_pfCells++;
        ScanCell(flZ, flLat);

        // ONE CLOCK, OURS. This also stopped on TbBudgetExpired(), the 0.40-of-a-tick
        // share the whole TB/PS family searches against, and the finder runs LAST in the cmd:
        // with PS assist or TB active they spend that share before the finder is even called, so
        // every chunk did exactly ONE cell and a 600-step column took thousands of ticks. A scan
        // is a deliberate keypress with a visible result, not a background search -- it gets its
        // own budget on top, and the budget slider is what bounds it
        if (ElapsedMs() >= flBudgetMs)
            break;
    }

    // ---- SURF LENGTH: the column is done, walk each pin along its wall (see PfHit) -------------
    // Holds = ref's re-proof at one spot: hull at the pin z on the same face, the pin's own
    // vz and press, two ticks, still pinned at the same height, still in the air. The fit trace
    // first: a hull teleported into solid gets its velocity zeroed every tick, and gravity on top
    // of zero reads exactly like a pin -- that is how a walk would run on through the next wall.
    // ponytail: no MV_PinOnProp per step (16 traces); a crate against the wall extends a run. Add
    // it here if a line is seen crossing one
    auto Holds = [&](const PfHit& h, float s) -> bool
    {
        const Vec3 vT(h.n.y, -h.n.x, 0.f);
        const Vec3 vAt = h.wall + vT * s + h.n * h.gap;
        GameTrace_t fit{};
        TraceHull(vAt, vAt, vMins, Vec3(vMaxs.x, vMaxs.y, h.duck ? k_pfDuckHullZ : k_pfStandHullZ),
                  MASK_PLAYERSOLID, &filter, &fit);
        if (fit.startsolid || fit.allsolid) return false;
        const Vec3 vInto(-h.n.x, -h.n.y, 0.f);
        Vec3 vIntoAngle = VectorAngles(vInto);
        ClampAnglesMv(vIntoAngle);
        RestoreToFrame(nBasePredicted - 1);
        PfPlace(vAt, h.duck);
        EntSetV3(pLocal, g_offVelocity, vInto * 10.f + Vec3(0.f, 0.f, -flHalfGrav));
        pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
        if (h.duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        // the press that proved the pin (light 10 straight in, or the hold-set one that rescued it)
        we::WishToCmd(vIntoAngle.y + h.pressOff, h.pressSize, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
        for (int t = 0; t < 2; t++)
        {
            Pred_Simulate(pLocal, pCmd);
            s_pfLenSims++;
            if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return false;
            if (fabsf(EntV3(pLocal, g_offVelocity).z + flHalfGrav) >= flSurfTol) return false;
            if (fabsf(EntV3(pLocal, g_offOrigin).z - h.wall.z) > 0.1f) return false;
        }
        return true;
    };

    const bool bWalk = s_pfNextI > s_pfSteps && !s_pfQuiet && g_psFinderLength
                    && s_pfHits.size() == s_pfFound.size();
    if (bWalk && s_pfLenI < 0)
    {
        s_pfLen.clear();
        for (int i : PfDedupeIdx(s_pfFound)) s_pfLen.push_back(s_pfHits[i]);
        s_pfLenI = 0; s_pfLenDir = 0; s_pfLenK = 1;
        s_pfLenLost = 0;
    }
    while (bWalk && s_pfLenI < (int)s_pfLen.size() && ElapsedMs() < flBudgetMs)
    {
        PfHit& h = s_pfLen[s_pfLenI];
        // the pin has to re-prove where it was found, or every step after it measures nothing
        // (`lost=` on the scan line: if that is every pin, Holds is wrong, not the map)
        if (s_pfLenDir == 0 && s_pfLenK == 1 && !Holds(h, 0.f))
        {
            s_pfLenLost++;
            s_pfLenI++;
            continue;
        }
        const float sgn = s_pfLenDir ? 1.f : -1.f;
        if (s_pfLenK <= k_pfLenMax && Holds(h, sgn * k_pfLenStep * float(s_pfLenK)))
        {
            h.ext[s_pfLenDir] = sgn * k_pfLenStep * float(s_pfLenK);
            s_pfLenK++;
            continue;
        }
        // the run stops inside this step: bisect it to 1u
        if (s_pfLenK <= k_pfLenMax)
        {
            float lo = k_pfLenStep * float(s_pfLenK - 1), hi = k_pfLenStep * float(s_pfLenK);
            for (int b = 0; b < 3; b++)
            {
                const float mid = (lo + hi) * 0.5f;
                if (Holds(h, sgn * mid)) lo = mid; else hi = mid;
            }
            h.ext[s_pfLenDir] = sgn * lo;
        }
        if (s_pfLenDir == 0) { s_pfLenDir = 1; s_pfLenK = 1; }
        else                 { s_pfLenI++; s_pfLenDir = 0; s_pfLenK = 1; }
    }
    const bool bLenDone = !bWalk || s_pfLenI >= (int)s_pfLen.size();

    // ---- LIP PASS: the column again, centre cell only, two traces a height (see s_pfLipI) -------
    const bool bLips = s_pfNextI > s_pfSteps && !s_pfQuiet && g_psFinderPixeljump;
    while (bLips && s_pfLipI <= s_pfSteps && ElapsedMs() < flBudgetMs)
        ScanCell(s_pfLow + float(s_pfLipI++), 0.f, true);
    const bool bLipDone = !bLips || s_pfLipI > s_pfSteps;

    // ---- TB SIM PASS (see k_pfTbGap): band ends + centre, both stances, a height per step ---------
    // A box-brush face only: the seam pass's own diagonal rule (TB_UlpStepIntoWall axis) and its
    // prop / void / fit screens. Pin = no ground, END vz in the TB window, still sliding (xy >= 1:
    // a dead stop on the face is an air stuck, not a ride -- WeTbSim counts pins the same way)
    const bool bTbSim = s_pfNextI > s_pfSteps && !s_pfQuiet && g_psFinderTb && s_pfTbAxis;
    while (bTbSim && s_pfTbI <= s_pfSteps && ElapsedMs() < flBudgetMs)
    {
        const float flZ = s_pfLow + float(s_pfTbI++);
        const float flHGrav = flHalfGrav;
        for (int j = 0; j < k_pfTbLat; j++)
        {
            const float flLat = (float(j) - 1.f) * flBandHalf;
            const Vec3 vMid = Vec3(s_pfColumn.x, s_pfColumn.y, flZ) + vTangent * flLat;
            GameTrace_t face{};
            if (!PfSeamProbe(filter, vMid, vWallNormal, flZ, face)) { s_pfTbPrev[j][0] = s_pfTbPrev[j][1] = false; continue; }
            Vec3 vN(face.plane.normal.x, face.plane.normal.y, 0.f);
            vN.Normalize();
            const float flSup = fabsf(vMaxs.x) * fabsf(vN.x) + fabsf(vMaxs.y) * fabsf(vN.y);
            const Vec3 vAt = Vec3(face.endpos.x, face.endpos.y, flZ) + vN * (flSup + k_pfTbGap);
            if (MV_PointUnusable(vAt + Vec3(0.f, 0.f, 8.f))) { s_pfTbPrev[j][0] = s_pfTbPrev[j][1] = false; continue; }
            const Vec3 vAlong(vN.y, -vN.x, 0.f);
            for (int d = 0; d < 2; d++)
            {
                const bool bDuck = d != 0;
                GameTrace_t fit{};
                TraceHull(vAt, vAt, vMins, Vec3(vMaxs.x, vMaxs.y, bDuck ? k_pfDuckHullZ : k_pfStandHullZ),
                          MASK_PLAYERSOLID, &filter, &fit);
                bool bPin = false;
                Vec3 vEnd = vAt;
                if (!fit.startsolid && !fit.allsolid)
                {
                    RestoreToFrame(nBasePredicted - 1);
                    PfPlace(vAt, bDuck);
                    EntSetV3(pLocal, g_offVelocity, vAlong * 100.f + Vec3(0.f, 0.f, -100.f));
                    pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
                    if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                    pCmd->forwardmove = pCmd->sidemove = 0.f;
                    Pred_Simulate(pLocal, pCmd);
                    s_pfTbSims++;
                    const Vec3 v = EntV3(pLocal, g_offVelocity);
                    vEnd = EntV3(pLocal, g_offOrigin);
                    bPin = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && we::TbPinVz(v.z, flHGrav) && v.Length2D() >= 1.f;
                }
                if (bPin) s_pfTbPins++;
                // the top of a pinned run = where the ride starts on this stretch
                if (bPin && !s_pfTbPrev[j][d])
                {
                    const Vec3 vMark(face.endpos.x, face.endpos.y, vEnd.z);
                    std::lock_guard<std::mutex> l(g_psLock);
                    if (!PfSeamKnown(s_psTbPts, vMark, flBandHalf * 2.f + 1.f) && s_psTbPts.size() < 128)
                    {
                        s_psTbPts.push_back(vMark);
                        s_pfTbMarks++;
                    }
                }
                s_pfTbPrev[j][d] = bPin;
            }
        }
    }
    const bool bTbDone = !bTbSim || s_pfTbI > s_pfSteps;

    // restore everything the chunk touched
    pCmd->buttons = iBackupButtons;
    pCmd->forwardmove = flBackupForward;
    pCmd->sidemove = flBackupSide;
    RestoreToFrame(nBasePredicted - 1);

    s_pfChunks++;
    s_pfMs += ElapsedMs();

    // publish what we have every chunk -- points appear while the column is still scanning.
    // The map scan's jobs are one cell each and its own markers are LINES, so it keeps its pins
    // (s_pfFound) and publishes nothing here -- otherwise every cell would wipe the crosshair
    // finder's points and repaint the screen with single dots.
    int nFound = 0;
    if (!s_pfQuiet)
    {
        std::vector<Vec3> vPub = PfDedupe(s_pfFound);
        nFound = (int)vPub.size();
        // runs publish as they grow, like the dots. Under 1u is the dot alone
        std::vector<Vec3> vLines;
        if (bWalk)
            for (const PfHit& h : s_pfLen)
            {
                if (h.ext[1] - h.ext[0] < 1.f) continue;
                const Vec3 vT(h.n.y, -h.n.x, 0.f);
                vLines.push_back(h.wall + vT * h.ext[0]);
                vLines.push_back(h.wall + vT * h.ext[1]);
            }
        std::lock_guard<std::mutex> l(g_psLock);
        s_psFinderPts = std::move(vPub);
        s_psFinderLines = std::move(vLines);
    }

    if (s_pfNextI > s_pfSteps && bLenDone && bLipDone && bTbDone)
    {
        s_pfBusy = false;
        if (s_pfQuiet)
            return;   // the map scan reads s_pfFound, logs its own totals, and clears
        // conf=N/M -- of M pins found, N are catchable by a FALL (TryFall). READ IT:
        // conf=0/M means every saved point on this wall only HOLDS a pin and the assist can
        // never jump onto one, however good the picker is
        // unheld= pins that held one tick and not the next (dropped). len= walked runs, lost= pins
        // the walk could not re-prove at their own spot, lensims= what the walk cost
        float flLongest = 0.f;
        for (const PfHit& h : s_pfLen) flLongest = std::max(flLongest, h.ext[1] - h.ext[0]);
        DebugLog("PF: scan z=%.1f..%.1f steps=%d margin=%.0f -> %d spot(s) | %d chunk(s) %.1f ms "
                 "cells=%d sims=%d pruned=%d nostand=%d conf=%d/%d sealed=%d clip=%d deep=%d "
                 "prop=%d void=%d unheld=%d strict=%d budget=%.1f eff=%.1f | len=%d lost=%d "
                 "longest=%.0fu lensims=%d | lips=%d kept=%d jumpsurfs=%d | rescued=%d band=%.0f/%.0f "
                 "| tbsim=%d tbpins=%d tbmarks=%d axis=%d",
                 s_pfLow, s_pfHigh, s_pfSteps, g_psFinderMargin, nFound,
                 s_pfChunks, s_pfMs, s_pfCells, s_pfProbes, s_pfPruned, s_pfNoStand,
                 s_pfConf, s_pfFalls, s_pfSealed, s_pfClipBlk, s_pfDeep, s_pfProp, s_pfVoid,
                 s_pfUnheld, (int)g_psFinderStrict, (double)g_psFinderBudget, flBudgetMs,
                 bWalk ? (int)s_pfLen.size() : -1, s_pfLenLost, flLongest, s_pfLenSims,
                 bLips ? s_pfLips : -1, (int)s_pfLipPts.size(), (int)s_pfJumpPts.size(),
                 s_pfRescued, flBandHalf, flBandStep,
                 bTbSim ? s_pfTbSims : -1, s_pfTbPins, s_pfTbMarks, (int)s_pfTbAxis);
        s_pfFound.clear();
        s_pfHits.clear();
        s_pfLenI = -1;
    }
}

// ============================================================================
// TEXTURE BUG SEAMS ON THE SAME COLUMN 
// ----------------------------------------------------------------------------
// A pixel surf is a solver artifact with nothing for a trace to see, which is why
// the scan above sims. A TEXTURE BUG is the opposite: it IS a visible boundary --
// two coplanar brush faces the collision solver clips separately -- and TB already
// owns the test that decides it, TB_FaceSame (same plane within 0.10, same surface
// name). So this half is a TRACE pass: walk the anchored column a unit at a time,
// keep every height where the face changes while the plane stays flush, bisect the
// unit down to ~0.03u and mark it. One line trace per unit of column (<= 601) plus
// 5 per seam, run once on release -- cheap next to one cell of the sim scan.
// ponytail: centre column only, and open-air is a thin box in front of the seam.
// Sweep the lateral band / re-ask the ride as a sim if a real wall proves it needs it.
static int s_pfSeamTr = 0;   // traces the seam pass spent (diag)

// the filter is built by the caller -- this ran Setup() on all ~600 traces of a column
static bool PfSeamProbe(TraceFilterWorldProps& filter, const Vec3& vColumn, const Vec3& vN, float flZ,
                        GameTrace_t& tr)
{
    const Vec3 vMid(vColumn.x, vColumn.y, flZ);
    TraceLine(vMid + vN * 12.f, vMid - vN * 12.f, MASK_PLAYERSOLID, &filter, &tr);
    s_pfSeamTr++;
    // and never a prop. A texturebug is two COPLANAR BRUSH FACES the solver clips
    // apart; a crate's .phy hull has no pair to be, but it does trace as an upright face with
    // its own material name, so PfIsSeam read every crate-on-wall as a boundary and rang it
    return tr.fraction < 1.f && !tr.startsolid && fabsf(tr.plane.normal.z) <= 0.7f
        && !(g_psFinderNoProps && MV_TraceIsProp(tr));   // same checkbox as the sim half
}

// flush enough to be one surface to the eye, not the same face to the solver.
// A corner (normals apart) or a ledge (dist apart) is a wall, not a seam
static bool PfIsSeam(const GameTrace_t& a, const GameTrace_t& b)
{
    if (a.plane.normal.x * b.plane.normal.x + a.plane.normal.y * b.plane.normal.y
      + a.plane.normal.z * b.plane.normal.z < 0.9995f) return false;
    if (fabsf(a.plane.dist - b.plane.dist) > 0.5f) return false;
    return !TB_FaceSame(a, b);
}

// THE BAND, NOT THE ONE COLUMN. The pass used to walk the crosshair's centre line and nothing
// else, so a texture boundary that stops 20u to the side of where you happened to aim did not
// exist -- and a boundary is a thing with ENDS, which is exactly when that happens. It sweeps the
// same lateral band the sim half does now, and the count is bounded by the trace budget rather
// than by the band: a 600u column gets 3 offsets, a 60u one gets all 9, and neither costs more
// than ~1800 line traces on the release command that runs the whole pass inline.
static constexpr int k_pfSeamTraceCap = 6000;   // 11-08: was 1800 (3 offsets on a tall column); traces ~1us, 9+ offsets always

// two marks are the same seam when they are at the same height on the same stretch of wall. The
// band is swept per offset, so without this a boundary that really does run the width of the
// column comes back as nine rings in a row. Radius = whole band width: a fixed 8u let every
// other 4u offset through, so one seam rang 5 times
static bool PfSeamKnown(const std::vector<Vec3>& v, const Vec3& p, float flRadius)
{
    for (const Vec3& q : v)
        if (std::fabs(q.z - p.z) < 0.5f && (q - p).Length2D() < flRadius) return true;
    return false;
}

static void PfScanSeams(void* pLocal)
{
    std::vector<Vec3> vOut;
    s_pfSeamTr = 0;
    const int iSteps = s_pfSteps;
    const Vec3 vN = s_pfNormal;

    // A DIAGONAL WALL CANNOT TEXTUREBUG, so it gets no markers. The bug is the ulp step
    // into the wall (TB_UlpStepIntoWall): the trace's delta has to cross the wall's own float
    // boundary, which only exists on x or on y. Off-axis there is nothing to step onto and every
    // ring drawn on one was a ride nobody could take
    {
        int iAxis = -1;
        TB_UlpStepIntoWall(vN, s_pfColumn, iAxis);
        if (iAxis < 0)
        {
            { std::lock_guard<std::mutex> l(g_psLock); s_psTbPts.clear(); }
            DebugLog("PF: seam pass skipped -- wall is diagonal (n=%.4f %.4f), no texturebug on it",
                     vN.x, vN.y);
            return;
        }
    }

    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps filter; filter.Setup(pLocal);

    const float flBandHalf = s_pfBandHalf;
    const float flBandStep = s_pfBandStep > 0.f ? s_pfBandStep : 4.f;
    const int   iLatFull   = int(flBandHalf * 2.f / flBandStep) + 1;   // the sim half's own 9
    const int   iLat = std::clamp(k_pfSeamTraceCap / std::max(1, iSteps + 1), 1, iLatFull);
    // one offset = the centre, as before; more than one spreads over the WHOLE band so the ends
    // are always asked (that is where a boundary stops)
    const float flLatStep = iLat > 1 ? (flBandHalf * 2.f) / float(iLat - 1) : 0.f;

    for (int j = 0; j < iLat && (int)vOut.size() < 128; j++)
    {
        const float flLat = iLat > 1 ? -flBandHalf + flLatStep * float(j) : 0.f;
        const Vec3 vColumn = s_pfColumn + s_pfTangent * flLat;

        GameTrace_t prev{};
        bool bPrev = PfSeamProbe(filter, vColumn, vN, s_pfLow, prev);
        float flPrevZ = s_pfLow;

        for (int i = 1; i <= iSteps && (int)vOut.size() < 128; i++)
        {
            const float flZ = s_pfLow + float(i);
            GameTrace_t cur{};
            const bool bCur = PfSeamProbe(filter, vColumn, vN, flZ, cur);

            if (bPrev && bCur && PfIsSeam(prev, cur))
            {
                // the ride is ON the boundary -- a marker a whole unit off is a marker for
                // a different height. lo stays on the OLD face, hi is the first z on the new one
                float flLo = flPrevZ, flHi = flZ;
                for (int k = 0; k < 5; k++)
                {
                    const float flMid = (flLo + flHi) * 0.5f;
                    GameTrace_t mid{};
                    if (PfSeamProbe(filter, vColumn, vN, flMid, mid) && TB_FaceSame(prev, mid))
                        flLo = flMid;
                    else
                        flHi = flMid;
                }

                // open air in front of the seam, or nothing can ride it. Thin box: the hull
                // that rides a TB is falling past, so its feet are not at the seam
                Vec3 vFace(cur.plane.normal.x, cur.plane.normal.y, 0.f);
                if (vFace.Length2D() > 0.1f) vFace.Normalize(); else vFace = vN;
                const float flSup = fabsf(vMaxs.x) * fabsf(vFace.x) + fabsf(vMaxs.y) * fabsf(vFace.y);
                const Vec3 vAt = Vec3(cur.endpos.x, cur.endpos.y, flHi) + vFace * (flSup + 0.5f);
                GameTrace_t fit{};
                TraceHull(vAt, vAt, Vec3(vMins.x, vMins.y, -4.f), Vec3(vMaxs.x, vMaxs.y, 4.f),
                          MASK_PLAYERSOLID, &filter, &fit);
                const Vec3 vMark(cur.endpos.x, cur.endpos.y, flHi);
                // ...and the ride has to be somewhere a player can be. The thin box above is a
                // sweep that started where it ended, so it says nothing about a spot sealed
                // inside a brush or sitting off the edge of the map
                if (!fit.startsolid && !fit.allsolid && !PfSeamKnown(vOut, vMark, flBandHalf * 2.f + 1.f)
                    && !MV_PointUnusable(vAt))
                    vOut.push_back(vMark);
            }

            prev = cur; bPrev = bCur; flPrevZ = flZ;
        }
    }

    const int nFound = (int)vOut.size();
    {
        std::lock_guard<std::mutex> l(g_psLock);
        s_psTbPts = std::move(vOut);
    }
    DebugLog("PF: seam pass z=%.1f..%.1f -> %d texturebug(s) | %d offset(s) of %d, %d trace(s)",
             s_pfLow, s_pfHigh, nFound, iLat, iLatFull, s_pfSeamTr);
}

static void PixelFinder(void* pLocal, UserCmd_t* pCmd)
{
    const bool bHeld = On(g_psFinder, g_psFinderKey, g_psFinderMode);
    const bool bRising = bHeld && !s_psFinderHeld;
    const bool bFalling = !bHeld && s_psFinderHeld;
    s_psFinderHeld = bHeld;

    // a new column throws away the one still scanning -- the cursor points into the old anchor.
    // unticking the feature mid-scan stops it too: the checkbox is an off switch, not a pause
    if (bRising || !g_psFinder)
        PmAbort();   // a column press (or the checkbox) outranks a map scan: one scanner, one job
    if (s_pfBusy && (bRising || !g_psFinder))
    {
        DebugLog("PF: scan cancelled at step %d/%d -- %s", s_pfNextI, s_pfSteps,
                 bRising ? "new column anchored" : "feature turned off");
        PfCancelScan();
    }

    if (!bHeld && !bFalling)
    {
        // key long gone, chunked scan still running: keep spending its slice each cmd
        if (s_pfBusy && g_offViewOffset > 0)
            PfScanChunk(pLocal, pCmd);
        return;
    }
    if (g_offViewOffset <= 0)
        return;

    // where the crosshair hits the world
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t trace{};
    TraceLine(vEye, vEye + vForward * 6000.f, MASK_PLAYERSOLID, &filter, &trace);
    const Vec3 vHit = trace.endpos;

    if (bRising)
    {
        // first press: clear old results, anchor the scan column (ignore triggers/empty aim)
        std::lock_guard<std::mutex> l(g_psLock);
        s_psFinderPts.clear();
        s_psFinderLines.clear();
        s_psTbPts.clear();
        s_psFinderFirst = false;
        // a prop anchor is refused outright. Aiming at a crate used to anchor the
        // column on its .phy hull and scan the whole thing; every cell would now be skipped, so
        // saying so once beats 601 silent misses
        if (trace.fraction >= 1.f || (trace.surface.flags & SURF_TRIGGER_MV)
            || (g_psFinderNoProps && MV_TraceIsProp(trace)))   // the same checkbox
        {
            DebugLog("PF: press ignored (frac=%.2f trigger=%d prop=%d) -- aim at a BRUSH wall to anchor",
                     trace.fraction, (int)((trace.surface.flags & SURF_TRIGGER_MV) != 0),
                     (int)MV_TraceIsProp(trace));
            return;
        }
        s_psFinderStart = vHit;
        s_psFinderEnd = vHit;
        s_psFinderNormal = trace.plane.normal;
        s_psFinderFirst = true;
        DebugLog("PF: anchor z=%.1f n=(%.2f %.2f %.2f)",
                 vHit.z, trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z);
        return;
    }

    if (bHeld)
    {
        // while held: stretch the scan column to the current aim height
        if (s_psFinderFirst)
        {
            std::lock_guard<std::mutex> l(g_psLock);
            s_psFinderEnd.z = vHit.z;
        }
        return;
    }

    // release: brute-force the wall column for pixel-surf catch positions
    if (!s_psFinderFirst)
        return;
    s_psFinderFirst = false;

    Vec3 vWallNormal = s_psFinderNormal;
    vWallNormal.z = 0.f;
    if (vWallNormal.Length() < 0.1f)
    {
        DebugLog("PF: scan refused -- anchored face is flat (nz=%.2f), not a wall", s_psFinderNormal.z);
        return;
    }
    vWallNormal.Normalize();

    // anchor the chunked scan; the cursor + column live in the s_pf* statics and the work
    // itself runs in PfScanChunk, a budgeted slice per cmd 
    Vec3 vTangent = Vec3(vWallNormal.y, -vWallNormal.x, 0.f);   // wall tangent for the band sweep
    if (vTangent.Length() > 0.1f)
        vTangent.Normalize();

    const float flMargin = g_psFinderMargin;
    s_pfLow  = std::min(s_psFinderStart.z, s_psFinderEnd.z) - flMargin;
    s_pfHigh = std::max(s_psFinderStart.z, s_psFinderEnd.z) + flMargin;
    s_pfSteps = std::clamp(int(s_pfHigh - s_pfLow), 0, 600);

    s_pfNormal  = vWallNormal;
    s_pfTangent = vTangent;
    s_pfColumn  = s_psFinderStart;
    // the crosshair column's own band (map scan: 1 cell). 2u steps (was 4): a catch on a face that
    // ends or steps inside the band no longer falls between two offsets
    s_pfBandHalf = 16.f; s_pfBandStep = 2.f;
    s_pfQuiet = false;
    s_pfBaseButtons = pCmd->buttons;   // frozen for the whole scan, not re-read per cmd

    s_pfFound.clear();
    s_pfHits.clear(); s_pfLen.clear(); s_pfLenI = -1;
    s_pfUnheld = 0; s_pfLenSims = 0; s_pfLenLost = 0;
    { std::lock_guard<std::mutex> l(g_psLock); s_pfJumpPts.clear(); s_pfHeadbouncePts.clear(); s_pfLipPts.clear(); }   // render copies them
    s_pfNextI = 0; s_pfNextLat = 0; s_pfHeightHit = false;
    s_pfLipI = 0; s_pfLips = 0;
    s_pfMs = 0.0; s_pfChunks = 0; s_pfProbes = 0; s_pfPruned = 0;
    s_pfCells = 0; s_pfFalls = 0; s_pfConf = 0; s_pfNoStand = 0;
    s_pfSealed = 0; s_pfClipBlk = 0; s_pfDeep = 0; s_pfProp = 0; s_pfVoid = 0;
    s_pfFallMemo.clear();   // memo is per column -- a new anchor is new geometry
    s_pfRescued = 0;
    s_pfTbI = 0; s_pfTbSims = 0; s_pfTbPins = 0; s_pfTbMarks = 0;
    for (auto& r : s_pfTbPrev) r[0] = r[1] = false;
    { int iAxis = -1; TB_UlpStepIntoWall(vWallNormal, s_pfColumn, iAxis); s_pfTbAxis = iAxis >= 0; }
    s_pfBusy = true;

    DebugLog("PF: scan start z=%.1f..%.1f steps=%d margin=%.0f budget=%.1f ms/tick",
             s_pfLow, s_pfHigh, s_pfSteps, flMargin, (double)g_psFinderBudget);

    // the texturebug half is OPTIONAL now (g_psFinderTb). It is a different ride on a
    // different thing (two flush faces the solver clips apart, not a sub-unit lip), so a column
    // scanned for pixel surfs alone should not come back ringed in red. Off = no pass, no markers
    if (g_psFinderTb)
        PfScanSeams(pLocal);     // texturebugs are traces, not sims -- the whole pass fits here
    else
    {
        std::lock_guard<std::mutex> l(g_psLock);
        s_psTbPts.clear();
    }
    PfScanChunk(pLocal, pCmd);   // first slice runs on the release cmd itself
}

// ============================================================================
// PIXELSURF ASSIST (real planner) -- botox hacks/movement/pixelsurf_assist.cpp, 1:1 on
// n_assist::arrival_surf (assist_predict.h). Candidates (up to 4, nearest-first, ps_type==1
// "delta strafe" also filters to cands[0]'s own delta_strafe value like botox): fly every
// n_assist program the point allows, the first arc whose window hit also probes a real lip
// (surf_lip) is the plan; a window-no-lip anywhere falls back to after a short wait (botox's
// own fallback, not a CSS invention). jump/duck replayed only, move/view stay the player's
// except the seam press while riding (FlyRide below) -- matches HITGODA's old meaning exactly.
// ============================================================================
static n_assist::plan_t s_psaPlan;
static bool  s_psaPending = false;
static n_assist::program_t s_psaPendingProgram{};
static float s_psaPendingZ  = 0.f;
static int   s_psaPendingTries = 0;
static Vec3  s_psaLastView{};
static int   s_psaRideArrival = 0, s_psaRideUntil = -1, s_psaRidePins = 0;
static float s_psaRideTarget = 0.f;
static int   s_psaNolipSkips = 0;
static long long s_psaCalls = 0ll, s_psaSims = 0ll, s_psaPlans = 0ll, s_psaDiverge = 0ll,
                  s_psaRejects = 0ll, s_psaNolip = 0ll, s_psaUs = 0ll;
static bool  s_psaDuckHeld = false;   // the catch duck outlives the hop -- see FlyRide

// a freshly placed point's starting engage radius (botox's own point-placement UI, points_ui.cpp,
// is not ported here -- see report; this is the CSS "set point" key's own default)
static constexpr float k_psaNewPointRadius = 300.f;

// botox is_bind_restricted_in_radius (pixelsurf_assist.cpp:562), 1:1 full group split
static bool PsaBindRestricted(const PsAssistPt& p, const Vec3& playerPos, n_assist::e_bind bind)
{
    return p.restrictBind && p.restrictGroup == bind && playerPos.DistTo(p.pos) <= p.restrictRadius;
}

// ---- UNEVEN GROUND, PART ONE: WALK ME UNDER THE SEAM ------------------------------------------
// PsSimHop flies a Z-ONLY arc (no wish at all but the seam press), so the assist can only fire
// from roughly under the point -- `worth=0` is the sweep saying "you are not standing there".
// On flat ground you line that up by eye. On stairs, a slope or a stack of crates the spot that
// works is a few units off the one you walked to, and no amount of clock finds a hop from the
// wrong place. So while you are on the ground with a target in range, YOUR OWN move is rotated
// toward it, capped so it stays a walk and not a snap. botox pixelsurf_assist_ground_help
// (pixelsurf_assist.cpp:488-544): same yaw-diff-clamp-steer shape, botox's own var/range/default
// (m_pixel_surf_assist_ground_angle_limit, 0-45, def 2.5 -- PSPORT round 2, was g_psGroundHelpAngle
// 5-90, a CSS-range extra).
// Nothing is ever pressed: with no movement key held forwardmove/sidemove are 0 and this is a
// no-op, which is the whole reason it is safe to leave on.
static void PsGroundHelp(UserCmd_t* pCmd, const Vec3& vOrigin, const Vec3& vTarget)
{
    if (pCmd->forwardmove == 0.f && pCmd->sidemove == 0.f)
        return;
    const Vec3 vFlat(vTarget.x - vOrigin.x, vTarget.y - vOrigin.y, 0.f);
    if (vFlat.Length2D() < 1.f)
        return;                                      // standing on it already
    const float flWant = RAD2DEGF(atan2f(vFlat.y, vFlat.x));
    const float flCap  = std::clamp(g_psGroundAngleLimit, 0.f, 45.f);
    const float flDiff = std::clamp(NormalizeAngle(flWant - pCmd->viewangles.y), -flCap, flCap);
    if (fabsf(flDiff) < 0.01f)
        return;
    Vec3 vSteer = pCmd->viewangles;
    vSteer.y = NormalizeAngle(vSteer.y + flDiff);
    CorrectMovement(pCmd, vSteer, pCmd->viewangles);   // silent: the view never moves
}

// jump box popup ("minijump to <z>"), render thread reads
static char s_asBoxText[96] = {};
static unsigned long long s_asBoxMs = 0;

static void PsStampText(const char* szText)
{
    std::lock_guard<std::mutex> l(g_psLock);
    snprintf(s_asBoxText, sizeof(s_asBoxText), "%s", szText);
    s_asBoxMs = GetTickCount64();
}

// botox pixelsurf_assist render(): "<launch> to surf at <z> with <origin z> (crouch|stand)",
// 1:1 -- n_assist::k_launch_names, at most one per 500 ms
static void PsStampJumpBox(const n_assist::program_t& p, float flTargetZ)
{
    static unsigned long long s_renderAfter = 0;
    const unsigned long long now = GetTickCount64();
    if (now < s_renderAfter)
        return;
    s_renderAfter = now + 500;
    char sz[96];
    snprintf(sz, sizeof(sz), "%s to surf at %f with %f%s", n_assist::k_launch_names[p.launch], flTargetZ,
              s_prePredOrigin.z, p.crouch ? " (crouch)" : " (stand)");
    PsStampText(sz);
}

// draw heartbeat + map tracking, and NOTHING else -- no sim, no press, no bookkeeping.
// the points overlay drops off the screen after 500ms of silence (render.cpp:189), so a cmd
// that runs no assist at all -- noclip, water, a taunt -- still has to stamp this or the finder's
// own results vanish while you fly around looking at them. Movement_RunImpl calls it on those cmds
static void PS_PublishAlive(void* pLocal)
{
    const char* szMap = EngineLevelName();
    if (strncmp(szMap, s_psMap, sizeof(s_psMap) - 1) != 0)
        PsLoadPointsForMap(szMap);

    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    std::lock_guard<std::mutex> l(g_psLock);
    s_psLocalOrigin = vOrigin;
    s_psAliveMs = GetTickCount64();
}

// botox pixelsurf_assist.cpp's ps_assist_ground_reachable, 1:1: when falling with no visible
// catch yet, flies a cheap wide-hull trace forward along gravity to check SOME surface comes
// into reach within horizon_ticks, before paying for the real per-program sim search
static bool PsAssistGroundReachable(void* pLocal, const int horizon_ticks)
{
    if (!pLocal || horizon_ticks < 1)
        return true;
    const float ipt = TickInterval();
    if (ipt <= 0.f)
        return true;

    const float grav = CvarFloat("sv_gravity", 800.f);
    const int seg_ticks = (std::max)(1, (horizon_ticks + 15) / 16);
    const float seg_dt  = (float)seg_ticks * ipt;
    const float sag     = 0.5f * grav * seg_dt * seg_dt;
    const float drift   = 4.f + 30.f * ((float)horizon_ticks * ipt);

    Vec3 pos = EntV3(pLocal, g_offOrigin);
    Vec3 vel = EntV3(pLocal, g_offVelocity);
    const Vec3 hull_mins = EntV3(pLocal, g_offMins), hull_maxs = EntV3(pLocal, g_offMaxs);
    const Vec3 wide_mins(hull_mins.x - drift, hull_mins.y - drift, hull_mins.z - sag);
    const Vec3 wide_maxs(hull_maxs.x + drift, hull_maxs.y + drift, hull_maxs.z + sag);

    TraceFilterWorldProps fil; fil.Setup(pLocal);
    for (int t = 0; t < horizon_ticks; t += seg_ticks)
    {
        const int steps = (std::min)(seg_ticks, horizon_ticks - t);
        const float dt  = (float)steps * ipt;
        const Vec3 disp(vel.x * dt, vel.y * dt, vel.z * dt - 0.5f * grav * dt * dt);
        vel.z -= grav * dt;

        GameTrace_t tr{};
        TraceHull(pos, pos + disp, wide_mins, wide_maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.fraction < 1.f)
            return true;
        pos = pos + disp;
    }
    return false;
}

// botox pixelsurf_assist_ground_help (pixelsurf_assist.cpp:488-544), 1:1 dispatch: a separate
// top-level call (botox calls it as its own on_create_move entry, not from inside
// pixelsurf_assist() itself) with its own gate chain, nearest saved point within its own radius
static void PixelSurfAssistGroundHelp(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_psGroundHelp || !g_psAssist)
        return;
    if (!pLocal || !pCmd || !EntAlive(pLocal))
        return;
    if (!On(g_psAssist, g_psAssistKey, g_psAssistMode))
        return;
    if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND))
        return;
    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    int idx = -1; float best = FLT_MAX;
    {
        std::lock_guard<std::mutex> l(g_psLock);
        if (s_psAssistPts.empty())
            return;
        for (int i = 0; i < (int)s_psAssistPts.size(); i++)
        {
            const float d = vOrigin.DistTo(s_psAssistPts[i].pos);
            if (d <= s_psAssistPts[i].radius && d < best) { best = d; idx = i; }
        }
        if (idx < 0)
            return;
        PsGroundHelp(pCmd, vOrigin, s_psAssistPts[idx].pos);
    }
}

static void PS_AssistTick(void* pLocal, UserCmd_t* pCmd, bool bPredicted)
{
    // botox movement.cpp:406-407: entity back to START of this cmd before every assist. Pred_Start
    // left it at END, so on_track compared END-of-k z to plan.ticks[k] (START-of-k): every airborne
    // plan "diverged" at k=1, and the post-arrival pin check read a no-duck END = ride dropped
    if (bPredicted) RestoreToFrame(PredCommandsPredicted() - 1);

    // draw heartbeat + map tracking run whenever the pipeline is alive
    PS_PublishAlive(pLocal);
    PixelSurfAssistGroundHelp(pLocal, pCmd);   // botox: a separate dispatch, own gates, see above

    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    s_psAssistPressIn = false;   // per-cmd, re-earned below (ride press -- botox has none for
                                 // pixelsurf past arrival, only other features set it)
    // real ground contact is the PRE-prediction flag (post-prediction already reflects the
    // cmd we are building) -- same convention the other assists use
    const bool bOnGround = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool bAssistOn = On(g_psAssist, g_psAssistKey, g_psAssistMode);

    // ---- where the crosshair hits the world (set-point / delete-point) --------------------
    auto AimHit = [&](Vec3& vOut) -> bool
    {
        if (g_offViewOffset <= 0) return false;
        Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
        const Vec3 vEye = vOrigin + EntV3(pLocal, g_offViewOffset);
        TraceFilterWorldProps filter; filter.Setup(pLocal);
        GameTrace_t trace{};
        TraceLine(vEye, vEye + vForward * 4000.f, MASK_PLAYERSOLID, &filter, &trace);
        vOut = trace.endpos;
        return trace.fraction < 1.f;
    };

    static bool s_psaSetHeld = false, s_psaDelHeld = false;
    const bool bSetHeld = g_psSetPointKey != 0 && MovementKeyOK(g_psSetPointKey, 1);
    if (bSetHeld && !s_psaSetHeld)
    {
        Vec3 vHit;
        if (AimHit(vHit))
        {
            std::lock_guard<std::mutex> l(g_psLock);
            const Vec3* pSnap = nullptr;
            float flBest = g_psAssistSnap;
            for (const Vec3& p : s_psFinderPts)
            {
                const float flDist = vHit.DistTo(p);
                if (flDist < flBest) { flBest = flDist; pSnap = &p; }
            }
            if (pSnap)
            {
                PsAssistPt np; np.pos = *pSnap; np.radius = k_psaNewPointRadius;
                s_psAssistPts.push_back(np);
                s_psGen++;
                PsWritePoints();
            }
        }
    }
    s_psaSetHeld = bSetHeld;

    const bool bDelHeld = g_psDeletePointKey != 0 && MovementKeyOK(g_psDeletePointKey, 1);
    if (bDelHeld && !s_psaDelHeld)
    {
        Vec3 vHit;
        if (AimHit(vHit))
        {
            std::lock_guard<std::mutex> l(g_psLock);
            int iErase = -1;
            float flBest = g_psAssistSnap;
            for (int i = 0; i < (int)s_psAssistPts.size(); i++)
            {
                const float flDist = vHit.DistTo(s_psAssistPts[i].pos);
                if (flDist < flBest) { flBest = flDist; iErase = i; }
            }
            if (iErase >= 0)
            {
                s_psAssistPts.erase(s_psAssistPts.begin() + iErase);
                s_psGen++;
                PsWritePoints();
            }
            else
            {
                flBest = g_psAssistSnap;
                for (int i = 0; i < (int)s_psFinderPts.size(); i++)
                {
                    const float flDist = vHit.DistTo(s_psFinderPts[i]);
                    if (flDist < flBest) { flBest = flDist; iErase = i; }
                }
                if (iErase >= 0)
                {
                    // ...and its run: the one line that passes through the dot at its height
                    const Vec3 p = s_psFinderPts[iErase];
                    for (size_t k = 0; k + 1 < s_psFinderLines.size(); k += 2)
                    {
                        const Vec3& a = s_psFinderLines[k];
                        const Vec3& b = s_psFinderLines[k + 1];
                        const float len = (b - a).Length2D();
                        if (fabsf(a.z - p.z) > 0.01f || len < 0.01f) continue;
                        if ((p - a).Length2D() + (b - p).Length2D() - len < 0.5f)
                        {
                            s_psFinderLines.erase(s_psFinderLines.begin() + k, s_psFinderLines.begin() + k + 2);
                            break;
                        }
                    }
                    s_psFinderPts.erase(s_psFinderPts.begin() + iErase);
                }
            }
        }
    }
    s_psaDelHeld = bDelHeld;

    // ============================================================================
    // botox n_movement::impl_t::pixelsurf_assist, 1:1 from here down.
    // ============================================================================
    const auto PsaStop = [&](const char* why)
    {
        if (s_psaPlan.active)
            DebugLog("[psa] drop %s k=%d/%d", why, pCmd->tick_count - s_psaPlan.start_tick, s_psaPlan.arrival);
        s_psaPlan.clear();
        s_psaPending = false;
        s_psaNolipSkips = 0;
    };

    if (!g_psAssist || !pLocal || !pCmd || !EntAlive(pLocal))
    {
        PsaStop("off");
        s_psAssistHopActive = false;
        return;
    }

    static bool s_psaKeyPrev = false;
    const bool key = bAssistOn;
    if (!key && (!s_psaPlan.active || pCmd->tick_count - s_psaPlan.start_tick <= s_psaPlan.launch))
    {
        s_psaKeyPrev = false;
        PsaStop("key");
        s_psAssistHopActive = false;
        return;
    }
    if (!key && s_psaKeyPrev)
        DebugLog("[psa] key up k=%d/%d: flown out", pCmd->tick_count - s_psaPlan.start_tick, s_psaPlan.arrival);
    s_psaKeyPrev = key;

    const int ps_type       = g_psAssistType;
    const Vec3 live_view    = pCmd->viewangles;
    const Vec3 turn         = live_view - s_psaLastView;
    s_psaLastView           = live_view;

    // botox arrival-divergence check: off the window's own tolerance, or (grounded tick) off
    // the window re-centred on where the plan's ground contact actually put the feet
    if (s_psaPlan.active)
    {
        const int k = pCmd->tick_count - s_psaPlan.start_tick;
        bool off = k <= s_psaPlan.arrival && !n_assist::on_track(pLocal, s_psaPlan, k);
        if (!off && k <= s_psaPlan.arrival && k < (int)s_psaPlan.ticks.size() && s_psaPlan.ticks[k].ground)
            off = !n_assist::in_window(n_assist::make_target(s_psaPlan.target_z, -1),
                                        s_psaPlan.arrive_z + n_assist::start_dz(pLocal, s_psaPlan, k));
        if (off)
        {
            ++s_psaDiverge;
            DebugLog("[psa] diverge %s%s k=%d/%d gnd=%d", n_assist::k_launch_names[s_psaPlan.program.launch],
                      s_psaPlan.program.crouch ? " crouch" : "", k, s_psaPlan.arrival, bOnGround ? 1 : 0);
            s_psaPlan.clear();
        }
    }

    // ps_bailed(): another feature already owns this cmd -- never pixelsurf's OWN detection
    // flags (s_shouldPixelSurf/s_psRidingNow), checking those against itself would be circular
    const auto PsaBailed = [&]()
    {
        return s_textureBugHit || s_headSurfHit || s_headBounceHit || s_airStuckHit
            || s_wallClimbHit || AutoBounceOwnsCmd();
    };
    if (!s_psaPlan.active && PsaBailed())
    {
        s_psAssistHopActive = false;
        return;
    }

    if (!s_psaPlan.active && key)
    {
        constexpr int ps_max_points = 4;
        const Vec3 origin = vOrigin;
        PsAssistPt cands[ps_max_points];
        float cand_dist[ps_max_points]{};
        int cand_count = 0;
        {
            std::lock_guard<std::mutex> l(g_psLock);
            for (int i = 0; i < (int)s_psAssistPts.size(); i++)
            {
                const PsAssistPt& p = s_psAssistPts[i];
                const float d = origin.DistTo(p.pos);
                if (d > p.radius)
                    continue;
                int slot = cand_count < ps_max_points ? cand_count++ : ps_max_points;
                for (; slot > 0 && cand_dist[slot - 1] > d; --slot)
                    if (slot < ps_max_points) { cands[slot] = cands[slot - 1]; cand_dist[slot] = cand_dist[slot - 1]; }
                if (slot < ps_max_points) { cands[slot] = p; cand_dist[slot] = d; }
            }
        }
        if (ps_type == 1)   // delta strafe: only candidates sharing cands[0]'s own delta_strafe
        {
            int kept = 1;
            for (int c = 1; c < cand_count; ++c)
                if (cands[c].deltaStrafe == cands[0].deltaStrafe)
                    cands[kept++] = cands[c];
            cand_count = cand_count > 0 ? kept : 0;
        }

        if (cand_count == 0)
        {
            static uint64_t s_lastUnreach = 0;
            if (const uint64_t now = GetTickCount64(); now - s_lastUnreach > 3000)
            {
                if (g_psAssistRender) PsStampText("[-] point unreachable");
                s_lastUnreach = now;
            }
            s_psAssistHopActive = false;
            return;
        }

        // "stamina hops" off: never press off a stand that still carries landing stamina
        if (!g_psAssistBrokeHop && bOnGround && EntStamina(pLocal) != 0.f)
        {
            s_psAssistHopActive = false;
            return;
        }

        const int raw_ticks = g_psAssistTicks;
        if (raw_ticks <= 0)
        {
            s_psAssistHopActive = false;
            return;
        }
        const int assist_ticks = std::clamp(n_tick::ticks(raw_ticks), 1, 256);

        if (!bOnGround && EntV3(pLocal, g_offVelocity).z <= 0.f
            && !PsAssistGroundReachable(pLocal, assist_ticks))
        {
            s_psAssistHopActive = false;
            return;
        }

        n_assist::ctx_t c;
        c.kind      = n_assist::arrival_surf;
        c.frame     = PredCommandsPredicted() - 1;
        c.buttons   = pCmd->buttons;
        c.fwd       = c.fwd0  = pCmd->forwardmove;
        c.side      = c.side0 = pCmd->sidemove;
        c.view      = live_view;
        c.delta     = ps_type == 1 ? Vec3(turn.x * cands[0].deltaStrafe, turn.y * cands[0].deltaStrafe, 0.f) : Vec3{};
        c.pre_ticks = assist_ticks;
        c.air_ticks = n_tick::ticks(256);
        c.lj_hold   = n_tick::ticks(3);
        c.gravity   = CvarFloat("sv_gravity", 800.f);
        c.lowest    = cands[0].pos.z;
        for (int i = 1; i < cand_count; ++i)
            c.lowest = (std::min)(c.lowest, (double)cands[i].pos.z);
        if (const Vec3 v0 = EntV3(pLocal, g_offVelocity); g_psSurfFix && v0.Length2D() >= 285.91f)
        {
            const float razn = (v0.Length2D() + 2.f - 285.91f) / 12.f * n_tick::rate();
            Vec3 back = v0 * -1.f; back.z = 0.f;
            const float rot = DEG2RADF(VectorAngles(back).y - live_view.y);
            c.fwd0  = cosf(rot) * razn;
            c.side0 = -sinf(rot) * razn;
        }
        n_tick::c_sim_budget budget;
        budget.start(0.45f, 0.f, n_tick::search_psa);
        c.budget   = &budget;
        c.max_sims = 512;

        static unsigned long long s_renderAfter = 0;
        const auto render = [&](const n_assist::program_t& p, float surf_z)
        {
            if (!g_psAssistRender) return;
            if (const unsigned long long now = GetTickCount64(); now >= s_renderAfter)
            {
                s_renderAfter = now + 500;
                PsStampJumpBox(p, surf_z);
            }
        };
        const auto take = [&](const n_assist::program_t& p, n_assist::run_t& r, int point, float surf_z)
        {
            s_psaPlan.take(p, r, pCmd->tick_count, point, surf_z);
            s_psaRidePins = 0;
            s_psaNolipSkips = 0;
            ++s_psaPlans;
            DebugLog("[psa] plan %s%s surf %.4f arrive %.4f launch k=%d arrival k=%d sims=%d", n_assist::k_launch_names[p.launch],
                      p.crouch ? " crouch" : "", surf_z, s_psaPlan.arrive_z, s_psaPlan.launch, s_psaPlan.arrival, c.sims);
            render(p, surf_z);
        };

        n_assist::run_t fallback;
        n_assist::program_t fallback_prog{};
        float fallback_z = 0.f;
        int fallback_point = -1;
        const auto note_nolip = [&](const n_assist::program_t& p, n_assist::run_t& r, const std::vector<n_assist::target_t>& tg)
        {
            if (r.nolip < 0 || fallback.nolip >= 0) return;
            fallback_prog = p;
            fallback_z = tg[r.nolip_target].z;
            fallback_point = tg[r.nolip_target].point;
            fallback = std::move(r);
        };

        bool landing_in_reach = bOnGround;
        if (!bOnGround && (!s_psaPending || s_psaPendingProgram.launch == n_assist::launch_jumpbug))
        {
            const n_assist::run_t probe = n_assist::run(pLocal, pCmd, c, n_assist::program_t{ n_assist::launch_jump, false }, {}, false);
            c.jb_tick = probe.landing;
            landing_in_reach = probe.landing >= 0 || !probe.complete;
        }

        if (s_psaPending)
        {
            s_psaPending = false;
            const std::vector<n_assist::target_t> one{ n_assist::make_target(s_psaPendingZ, -1) };
            n_assist::run_t v = n_assist::run(pLocal, pCmd, c, s_psaPendingProgram, one, true);
            if (v.hit)
                take(s_psaPendingProgram, v, -1, s_psaPendingZ);
            else if (!v.complete && ++s_psaPendingTries < 4)
                s_psaPending = true;
        }

        static int s_resume = 0;
        constexpr int n_progs = static_cast<int>(std::size(n_assist::k_programs));
        int last = -1;
        std::vector<n_assist::target_t> targets;
        targets.reserve(ps_max_points);
        for (int visit = 0; visit < n_progs && landing_in_reach && !s_psaPlan.active && !s_psaPending && !c.out_of_budget; ++visit)
        {
            last = (s_resume + visit) % n_progs;
            const n_assist::program_t& prog = n_assist::k_programs[last];
            if (prog.launch == n_assist::launch_jumpbug && bOnGround)
                continue;

            targets.clear();
            for (int ci = 0; ci < cand_count; ++ci)
                for (const n_assist::flag_t& f : n_assist::k_flags)
                    if (n_assist::same(f.program, prog) && cands[ci].flags.*f.flag
                        && !PsaBindRestricted(cands[ci], origin, f.bind))
                    {
                        targets.push_back(n_assist::make_target(cands[ci].pos.z, ci));
                        break;
                    }
            if (targets.empty())
                continue;

            n_assist::run_t r = n_assist::run(pLocal, pCmd, c, prog, targets, false);
            if (!r.hit)
            {
                note_nolip(prog, r, targets);
                continue;
            }

            const int t = r.target;
            if (!r.exact)
            {
                n_assist::run_t v = n_assist::run(pLocal, pCmd, c, prog, targets, true);
                if (!v.hit)
                {
                    if (!v.complete)
                    {
                        s_psaPending = true;
                        s_psaPendingProgram = prog;
                        s_psaPendingZ = targets[t].z;
                        s_psaPendingTries = 0;
                    }
                    else
                        note_nolip(prog, v, targets);
                    continue;
                }
                r = std::move(v);
            }
            take(prog, r, targets[r.target].point, targets[r.target].z);
        }
        if (!c.out_of_budget) s_resume = 0;
        else if (last >= 0) s_resume = last;

        if (!s_psaPlan.active && !s_psaPending && !c.out_of_budget && fallback.nolip >= 0)
        {
            ++s_psaNolip;
            const int k_wait = n_tick::ticks(8);
            const bool now_ = fallback.launch == 0;
            if (++s_psaNolipSkips > k_wait || now_)
            {
                fallback.hit = true;
                fallback.lip = 0;
                fallback.target = fallback.nolip_target;
                fallback.arrival = fallback.nolip;
                fallback.arrive_z = fallback.nolip_z;
                fallback.ticks.resize((size_t)fallback.nolip + 1);
                take(fallback_prog, fallback, fallback_point, fallback_z);
            }
        }
        else if (!s_psaPlan.active && !s_psaPending && !c.out_of_budget)
            s_psaNolipSkips = 0;

        pCmd->buttons     = c.buttons;
        pCmd->forwardmove = c.fwd;
        pCmd->sidemove    = c.side;
        pCmd->viewangles  = live_view;
        RestoreToFrame(c.frame);

        ++s_psaCalls;
        s_psaSims += c.sims;
        s_psaUs   += budget.used_us();
    }

    if (s_psaPlan.active)
    {
        const int k = pCmd->tick_count - s_psaPlan.start_tick;
        s_psaRideArrival = s_psaPlan.start_tick + s_psaPlan.arrival;
        s_psaRideTarget  = s_psaPlan.target_z;
        if (k <= s_psaPlan.arrival)
        {
            const n_assist::tick_t& t = s_psaPlan.ticks[k];
            pCmd->buttons = (pCmd->buttons & ~(IN_JUMP | IN_DUCK)) | t.buttons;
        }
        else
        {
            const bool pinned = IsTargetVz(EntV3(pLocal, g_offVelocity).z, 0.01f);   // incl. the CSS clip nudge
            const bool ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
            s_psaPlan.unpinned = pinned ? 0 : s_psaPlan.unpinned + 1;
            s_psaRidePins += pinned ? 1 : 0;
            if (ground || PsaBailed() || (k > s_psaPlan.arrival + 2 && s_psaPlan.unpinned > 2))
            {
                DebugLog("[psa] end %s%s rode=%d pinned=%d %s", n_assist::k_launch_names[s_psaPlan.program.launch],
                          s_psaPlan.program.crouch ? " crouch" : "", k - s_psaPlan.arrival, s_psaRidePins,
                          ground ? "ground" : "bail");
                s_psaRideUntil = pCmd->tick_count + n_tick::ticks(6);
                s_psaPlan.clear();
            }
            else
                pCmd->buttons = (pCmd->buttons & ~IN_DUCK) | (s_psaPlan.ticks.back().buttons & IN_DUCK);
        }
    }

    // the catch duck outlives the hop -- contract preserved for wallclimb.inl's own read of it.
    // Unlike botox's plan.ticks tail (which already keeps holding duck above), external readers
    // expect this flag true for exactly the same window
    s_psaDuckHeld = s_psaPlan.active && (pCmd->tick_count - s_psaPlan.start_tick) > s_psaPlan.arrival
                 && (s_psaPlan.ticks.back().buttons & IN_DUCK) != 0;
    s_psAssistDuckHeld = s_psaDuckHeld;
    s_psAssistHopActive = s_psaPlan.active || s_psaPending;

    static unsigned long long s_psaNextReport = 0ull;
    if (const unsigned long long now = GetTickCount64(); now >= s_psaNextReport)
    {
        s_psaNextReport = now + 1000ull;
        if (s_psaCalls || s_psaPlans || s_psaDiverge)
            DebugLog("PSA: calls=%lld sims=%lld plans=%lld diverge=%lld nolip=%lld us=%lld", s_psaCalls, s_psaSims,
                      s_psaPlans, s_psaDiverge, s_psaNolip, s_psaCalls ? s_psaUs / s_psaCalls : 0ll);
        s_psaCalls = s_psaSims = s_psaPlans = s_psaDiverge = s_psaNolip = s_psaUs = 0ll;
    }
}

// Jump-box popup only -- no vector copies. The overlay wants this every frame even
// when point rendering is off, and copying both point lists for it was pure waste.
void PS_GetBox(PsDrawData& out)
{
    std::lock_guard<std::mutex> l(g_psLock);
    memcpy(out.boxText, s_asBoxText, sizeof(out.boxText));
    out.boxMs = s_asBoxMs;
}

// render-thread snapshot (movement.h PsDrawData)
void PS_GetDrawData(PsDrawData& out)
{
    std::lock_guard<std::mutex> l(g_psLock);
    out.finderPts = s_psFinderPts;
    out.tbPts = s_psTbPts;
    out.mapLines = s_psMapLines;
    out.finderLines = s_psFinderLines;
    out.mapTbPts = s_psMapTbPts;
    out.assistPts.resize(s_psAssistPts.size());
    out.assistRadius.resize(s_psAssistPts.size());
    out.assistNormal.resize(s_psAssistPts.size());
    for (size_t i = 0; i < s_psAssistPts.size(); i++)
    {
        const PsAssistPt& pt = s_psAssistPts[i];
        out.assistPts[i] = pt.pos;
        out.assistRadius[i] = pt.radius;   // always a real per-point value now (PSPORT round 2)
        out.assistNormal[i] = pt.hasNormal ? pt.normal : Vec3();
    }
    out.assistActive = s_psAssistActive;
    out.lineActive = s_psFinderFirst && s_psFinderHeld;
    out.lineA = s_psFinderStart;
    out.lineB = s_psFinderEnd;
    out.localOrigin = s_psLocalOrigin;
    memcpy(out.boxText, s_asBoxText, sizeof(out.boxText));
    out.boxMs = s_asBoxMs;
    out.aliveMs = s_psAliveMs;
}

// ---- assist point access for the render-thread ENTER window (movement.h) ----
unsigned PS_PointsGen()
{
    std::lock_guard<std::mutex> l(g_psLock);
    return s_psGen;
}

// mask is now the 13-bit n_assist flag layout (PsFlagsToMask), not the old 7-bit typeMask --
// render.cpp's per-point ENTER window checkbox labels were updated to match (PSPORT round 2)
bool PS_PointGet(int i, Vec3& pos, int& mask, float& radius)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return false;
    pos = s_psAssistPts[i].pos;
    mask = PsFlagsToMask(s_psAssistPts[i].flags);
    radius = s_psAssistPts[i].radius;
    return true;
}

// botox points_check_t::radius has no "0 = global" convention -- every point carries its own
// absolute radius now (PSPORT round 2, the old global g_psAssistRadius is gone). Clamped to a
// sane range, never 0 (0 would never engage at all).
void PS_PointSetRadius(int i, float radius)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return;
    s_psAssistPts[i].radius = std::clamp(radius, 8.f, 1024.f);
    PsWritePoints();
}

void PS_PointSetMask(int i, int mask)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return;
    s_psAssistPts[i].flags = PsMaskToFlags(mask);
    PsWritePoints(); // persist per toggle -- close/crash never loses the edit
}

void PS_PointDelete(int i)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return;
    s_psAssistPts.erase(s_psAssistPts.begin() + i);
    s_psGen++;
    PsWritePoints();
}
