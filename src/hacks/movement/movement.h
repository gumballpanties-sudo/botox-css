#pragma once
#include "../../globals/common.h"
#include <string>   // RcDrawData carries the route lines the panel prints

// ref movement core port: bhop, jumpbug, autostrafe, strafe optimizer,
// edge jump (engine prediction), fast stop/accelerate, duck speed, movement lock,
// break jump. Binds = delusional style: mode 0 always on, 1 on hotkey, 2 toggle.

// ==== Vec3 (the vector subset the movement code needs) ====
struct Vec3
{
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3() {}
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    Vec3 operator+(const Vec3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    Vec3 operator-(const Vec3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    Vec3 operator*(float f)       const { return { x * f, y * f, z * f }; }
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(float f) { x *= f; y *= f; z *= f; return *this; }
    float& operator[](int i)       { return (&x)[i]; }
    float  operator[](int i) const { return (&x)[i]; }

    float Length2D()  const { return sqrtf(x * x + y * y); }
    float LengthSqr() const { return x * x + y * y + z * z; }
    float Length()    const { return sqrtf(LengthSqr()); }
    bool  IsZero()    const { return x == 0.f && y == 0.f && z == 0.f; }
    void  Set(float X = 0.f, float Y = 0.f, float Z = 0.f) { x = X; y = Y; z = Z; }
    float Dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    float DistTo(const Vec3& v) const { return (*this - v).Length(); }
    void  Normalize() { float l = Length(); if (l > 0.f) { x /= l; y /= l; z /= l; } }
};

// 16-aligned Vec3 + w -- Ray_t needs it (engine SIMD reads)
struct __declspec(align(16)) VectorAligned : Vec3
{
    VectorAligned() {}
    VectorAligned& operator=(const Vec3& o) { Set(o.x, o.y, o.z); return *this; }
    float w = 0.f;
};

// ==== math ====
#define MV_PI 3.14159265358979323846f
#define DEG2RADF(d) ((d) * (MV_PI / 180.f))
#define RAD2DEGF(r) ((r) * (180.f / MV_PI))

// surface extension (PAPER table 1, "eps"): every brush side is virtually pushed out by
// this much, and it is the numerator offset in the trace fraction f = (s - eps)/(s1 - s2).
// Every flush-brush bug (pixelwalk/pixelsurf/texturebug) lives inside this band
#define MV_SURF_EPS 0.03125f

inline float NormalizeAngle(float a) { return isfinite(a) ? remainderf(a, 360.f) : 0.f; }

inline void VectorAngles(const Vec3& f, Vec3& out)
{
    float yaw, pitch;
    if (f.y == 0.f && f.x == 0.f) { yaw = 0.f; pitch = f.z > 0.f ? 270.f : 90.f; }
    else {
        yaw = RAD2DEGF(atan2f(f.y, f.x));   if (yaw < 0.f) yaw += 360.f;
        pitch = RAD2DEGF(atan2f(-f.z, f.Length2D())); if (pitch < 0.f) pitch += 360.f;
    }
    out = { pitch, yaw, 0.f };
}
inline Vec3 VectorAngles(const Vec3& f) { Vec3 r; VectorAngles(f, r); return r; }

inline void AngleVectors(const Vec3& ang, Vec3* fwd, Vec3* right = nullptr, Vec3* up = nullptr)
{
    float sp = sinf(DEG2RADF(ang.x)), cp = cosf(DEG2RADF(ang.x));
    float sy = sinf(DEG2RADF(ang.y)), cy = cosf(DEG2RADF(ang.y));
    float sr = sinf(DEG2RADF(ang.z)), cr = cosf(DEG2RADF(ang.z));
    if (fwd)   { fwd->x = cp * cy; fwd->y = cp * sy; fwd->z = -sp; }
    if (right) { right->x = -1.f * sr * sp * cy + -1.f * cr * -sy; right->y = -1.f * sr * sp * sy + -1.f * cr * cy; right->z = -1.f * sr * cp; }
    if (up)    { up->x = cr * sp * cy + -sr * -sy; up->y = cr * sp * sy + -sr * cy; up->z = cr * cp; }
}

// ==== trace structs (engine CGameTrace / Ray_t) ====
// Here rather than in movement.cpp because backstab.cpp traces too, and two copies of
// a 16-field engine struct is a layout drift waiting to happen.
struct cplane_mv { Vec3 normal; float dist; unsigned char type, signbits, pad[2]; };
struct csurface_mv { const char* name; short surfaceProps; unsigned short flags; };

struct GameTrace_t
{
    Vec3 startpos;
    Vec3 endpos;
    cplane_mv plane;
    float fraction = 1.f;
    int contents = 0;
    unsigned short dispFlags = 0;
    bool allsolid = false;
    bool startsolid = false;
    // CGameTrace extras
    float fractionleftsolid = 0.f;
    csurface_mv surface{};
    int hitgroup = 0;
    short physicsbone = 0;
    void* m_pEnt = nullptr;
    int hitbox = 0;

    bool DidHit() const { return fraction < 1.f || allsolid || startsolid; }
};

struct Ray_mv
{
    VectorAligned m_Start, m_Delta, m_StartOffset, m_Extents;
    bool m_IsRay = true, m_IsSwept = true;

    void Init(const Vec3& start, const Vec3& end)
    {
        m_Delta = end - start;
        m_IsSwept = m_Delta.LengthSqr() != 0.f;
        m_Extents.Set(); m_IsRay = true; m_StartOffset.Set();
        m_Start = start;
    }
    void Init(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs)
    {
        m_Delta = end - start;
        m_IsSwept = m_Delta.LengthSqr() != 0.f;
        m_Extents = (maxs - mins) * 0.5f;
        m_IsRay = m_Extents.LengthSqr() == 0.f;
        Vec3 off = (mins + maxs) * 0.5f;
        m_Start = start + off;
        m_StartOffset = off * -1.f;
    }
};

// IEngineTrace::TraceRay -- confirmed x64 slot, shared by movement + backstab
inline constexpr int kTrace_TraceRay = 4;

// ==== CUserCmd (TF2 x64, sizeof 72 -- same layout as bhop.cpp CUserCmd_t) ====
struct UserCmd_t
{
    void*         vfptr;            // 0
    int           command_number;   // 8
    int           tick_count;       // 12
    Vec3          viewangles;       // 16
    float         forwardmove;      // 28
    float         sidemove;         // 32
    float         upmove;           // 36
    int           buttons;          // 40
    unsigned char impulse;          // 44
    int           weaponselect;     // 48
    int           weaponsubtype;    // 52
    int           random_seed;      // 56
    short         mousedx;          // 60
    short         mousedy;          // 62
    bool          hasbeenpredicted; // 64
};

// ==== bind runtime (delusional checkkey) ====
// mode: 0 = always on, 1 = on hotkey (hold), 2 = toggle
bool MovementKeyOK(int key, int mode);

void Movement_Install();                 // resolve interfaces/sigs -- PollThread, after client.dll up
void Movement_Run(UserCmd_t* pCmd);      // full per-cmd pipeline -- call from h_CreateMove after o_CreateMove
// Same pipeline under dih's eject drain guard. Called by the aim half's CreateMove hook,
// which owns that slot when that half loads (see src/hacks/aim/aim.h).
void Movement_RunGuarded(UserCmd_t* pCmd);
// botox air_stuck_final_stamp + air_stuck_shot_proof: call AFTER every other writer of the cmd
// (the aim hook calls it after its aimbot / prediction end). No-op unless air stuck sent a pin.
void Movement_AirStuckLastWriter(UserCmd_t* pCmd);

// Re-express forwardmove/sidemove/upmove: they are read as a wish in vWishAngle's frame
// and written back in vOldAngles' frame. Silent movement passes the fake angle as the
// wish and keeps the real view; silent AIM passes the real view as the wish and the
// fake angle as the frame the command will be sent in (backstab.cpp).
void CorrectMovement(UserCmd_t* pCmd, Vec3 vWishAngle, Vec3 vOldAngles);

// ==== CHLClient::CreateMove hook (VClient vtable slot 21) + CInput input plumbing ====
// (was bhop.cpp -- folded in; pCmd read from CInput::m_pCommands[seq % MULTIPLAYER_BACKUP])
using CreateMove_t = void(__fastcall*)(void* rcx, int seq, float frametime, bool active);
extern CreateMove_t o_CreateMove;
void __fastcall h_CreateMove(void* rcx, int seq, float frametime, bool active);

extern void* g_input;   // CInput* (resolved via signature)
bool ResolveInput();    // sig-scan client.dll -> g_input, true if found

// ==== C_TFPlayer::AvoidPlayers -- "no push" DELETED (11-03/11-04): no botox csgo equivalent
// (CCSPlayer has no such call). h_AvoidPlayers is now a pure passthrough (movement.cpp) --
// NEEDS FROM OTHERS: whoever installs this hook can drop it; harmless either way until then.
using AvoidPlayers_t = void(__fastcall*)(void* rcx, UserCmd_t* pCmd);
extern AvoidPlayers_t o_AvoidPlayers;
void __fastcall h_AvoidPlayers(void* rcx, UserCmd_t* pCmd);

// ==== movement indicator snapshot (movement thread -> render thread) ====
// Published once per cmd at the end of the pipeline. Plain scalars, no lock: the render
// thread only draws it, so the worst a torn read can do is show one stale digit.
struct MvInd_t
{
    unsigned long long ms = 0;   // GetTickCount64 at publish; stale = not in a live cmd
    bool  tbOn = false, tbHit = false, tbOwn = false;
    int   tbPhase = 0, tbRide = 0, tbSeams = 0;
    float tbD = 0.f, tbVin = 0.f, tbAim = 0.f;
    bool  asOn = false, asHit = false;
    bool  wcOn = false, wcHit = false;
    bool  phOn = false, phHit = false;   // pixel hop (wallengine.inl): hit = a grounding cmd or the hop went out
    bool  psOn = false, psHit = false;
    // ps assist ("ast"): on = the assist bind is live, hit = it owns the arc right now
    bool  astOn = false, astHit = false;
    bool  ebOn = false, ebHit = false;
    bool  strafeOn = false, ejOn = false, ljOn = false, mjOn = false;
    bool  mjHit = false;                 // mini jump: hit = the takeoff duck went out this tick
    bool  jbOn = false, jbHit = false;   // jumpbug: hit = the unduck+jump fired in the last 0.2 s (botox m_can_jb hold)
    bool  ebPred = false;                // eb plan latched, its landing tick still ahead (botox m_edge_bug_tick > tick)
    // botox keybind indicator rows with no landing state (plain labels), and the two that have one
    bool  dhOn = false, acOn = false, abOn = false, hswOn = false;
    bool  esOn = false, esHit = false;       // edge skip: hit = a plan is flying (botox g_edge_skip.active())
    // ladder unit (CS:GO csgo-sdk port): fr = fireman ("fr" key, same label as botox CS:GO),
    // hit = grab+jump pressed this tick. alOn/alHit = anti_ladder stuck-escape.
    bool  frOn = false, frHit = false;
    bool  alOn = false, alHit = false;
    bool  onGround = false;
    float speed = 0.f;
    int   tick = 0;        // globals tickcount / curtime after Pred_End (real, not a sim's)
    float curtime = 0.f;
    int   wjSeq = 0;       // fake wall jump count: a change = takeoff at wjSpeed
    float wjSpeed = 0.f;
    float stamina = 0.f;   // CCSPlayer::m_flStamina spent fraction, 0 fresh .. 1 empty (MVTOOLS stamina indicator)
    // raw input, for the keystroke indicator (unit B). Same publish point, same safety:
    // a torn read is one stale frame of a display-only overlay.
    int   buttons = 0;
    short mouseDx = 0, mouseDy = 0;
};
void MV_GetIndicators(MvInd_t& out);

// ==== ladder unit (CS:GO csgo-sdk hacks/movement/{movement.cpp,fireman.cpp} port) ====
// Definitions live in fireman.inl / ladder.inl (included from movement.cpp); declared here so
// the menu/config unit can wire a checkbox + keybind per botox csgo-sdk menu_tab_movement.cpp
// (see report MENU/CONFIG). Defaults are all off/unbound until wired.
extern bool g_fireMan;             extern int g_fireManKey;             extern int g_fireManMode;
// fireman settings popup (botox menu_tab_movement.cpp:813-839). CSS rebuild 11-07: approach = steer
// toward the ladder, reach = search radius, crouch jump = duck on launch hops. See fireman.inl header.
extern bool  g_firemanApproach;
extern float g_firemanReach;
extern bool  g_firemanCrouchJump;
// botox popup "drop in" / "lock movement" / "sides only" (+ key), 11-09 port
extern bool  g_firemanDropIn;
extern bool  g_firemanDropInLock;
extern bool  g_firemanSideOnly;    extern int g_firemanSideOnlyKey;    extern int g_firemanSideOnlyMode;
extern bool g_fastLadder;          extern int g_fastLadderKey;          extern int g_fastLadderMode;
extern bool g_ladderGlide;         extern int g_ladderGlideKey;         extern int g_ladderGlideMode;
extern bool g_ladderFreelookClimb; extern int g_ladderFreelookClimbKey; extern int g_ladderFreelookClimbMode;
extern bool g_antiLadder;          extern int g_antiLadderKey;          extern int g_antiLadderMode;

// ==== "show wall slope" (botox menu_tab_movement.cpp:808) -- defined in wallclimb.inl. Wall climb's
// prevent_slow/duck/viz live in globals.h. "steer to seam" + scale back with the 11-08 botox-tf2 sweep restore
extern bool g_showWallSlope;          extern int g_showWallSlopeKey;      extern int g_showWallSlopeMode;
extern bool g_wallClimbSeamSteer;     // botox m_wall_climb_seam_steer: steer the jump's along speed onto the seam that catches highest
extern float g_wallClimbSteerScale;   // botox m_wall_climb_steer_scale ("delta strafe scale"): along targets x this, 0.25..2

// ==== auto bounce lumi mode knobs (botox variables.h m_auto_bounce_lumi_*, autobounce.inl) ====
// Ranges per botox menu_tab_movement.cpp: rays 4-64 int, radius 5-100, depth 50-4096 log, min
// angle 5-60, max angle 20-90, smooth 0-1 (see report MENU/CONFIG).
extern int   g_abLumiRays;
extern float g_abLumiRadius;
extern float g_abLumiDepth;
extern float g_abLumiMinAngle;
extern float g_abLumiMaxAngle;
extern float g_abLumiSmooth;

// ==== distance calculator (botox hacks/movement/dist_calc.cpp 1:1, movement.cpp) ====
extern bool g_distCalc; extern int g_distCalcKey; extern int g_distCalcMode;
// draws the two markers + verdict text. NEEDS FROM OTHERS: render.cpp must call this once per
// frame (see report) -- same contract as every other *_GetDrawData render-thread entry point here.
void DistCalc_Render();

// ==== check floor level (botox hacks/movement/dist_calc.cpp floor_level 1:1, movement.cpp) ====
extern bool g_floorLevel; extern int g_floorLevelKey; extern int g_floorLevelMode; extern float g_floorLevelSize;
void FloorLevelToggle();    // botox floor_level_toggle -- menu "clear floor level"/"check floor level" button
bool FloorLevelShown();     // botox floor_level_shown -- same button's label
void FloorLevel_Render();   // readout text, same contract as DistCalc_Render above
// raw-d3d colored grid mesh (botox floor_level_draw/floor_level_world). NEEDS FROM OTHERS:
// render.cpp h_Present must call this once per frame (see report) -- same world-space-quad slot
// websurface.cpp's Web_Present already draws through (render.cpp:2030).
void FloorLevel_World(IDirect3DDevice9* dev);

// ==== edge bug plan snapshot (movement thread -> render thread) ====
// The winning variant's own simulated flight path -- not a re-sim and not a guess. `path` is
// what is LEFT of it (the replay eats a point per tick, so the leftover line is the countdown)
// and `at` is the tick the bug lands on. Empty / active = false whenever nothing is latched.
struct EbDrawData
{
    std::vector<Vec3> path;
    Vec3 at;
    bool active = false;
};
void EB_GetDrawData(EbDrawData& out);
bool MV_DropToFloor(Vec3& v);   // edge bug pad "project onto geometry" (render thread)

// ==== auto bounce plan snapshot (movement thread -> render thread) ====
// botox auto_bounce_render: the armed plan's verified flight (engine tick + fly) and the face it lands on.
struct AbDrawData
{
    std::vector<Vec3> path;
    Vec3 land, normal;
    bool active = false;
};
void AB_GetDrawData(AbDrawData& out);

// ==== wall climb visualize (movement thread -> render thread), botox wall_climb viz_t + spot job ====
// last wall tick: the face wc read, where the sent cmd takes you ( lands = over your jump floor ), on ground the spot scan
struct WcDrawData
{
    bool valid = false, ground = false, lands = false;
    unsigned long long ms = 0;   // GetTickCount64 of the publish: stale = wc stopped running
    const char* branch = "-";    // string literal
    Vec3 wall{}, n{}, land{}, origin{};
    Vec3 path[17]{};
    int pathN = 0, sims = 0;
    float half = 24.f, height = 82.f, groundZ = 0.f;
};
void WC_GetDrawData(WcDrawData& out);

// ==== PS finder/assist draw snapshot (movement thread -> render thread) ====
// Render copies under the movement-side mutex; stale when aliveMs is old
// (dead / not in game / movement pipeline idle).
struct PsDrawData
{
    std::vector<Vec3> finderPts;   // finder-found surf spots (current map)
    std::vector<Vec3> tbPts;       // finder-found texturebug seams (drawn with a red ring)
    std::vector<Vec3> assistPts;   // saved assist targets (current map)
    std::vector<float> assistRadius;   // per assistPts: engage radius (override or g_psAssistRadius)
    std::vector<Vec3>  assistNormal;   // per assistPts: cached wall normal, zero = not resolved yet
    // whole-map scan (psmap.inl): confirmed surf segments, A then B, one pair per line
    std::vector<Vec3> mapLines;
    // finder surf lengths (g_psFinderLength): how far each found pin holds along its wall, A then B
    std::vector<Vec3> finderLines;
    // ...and the texturebug seams that scan found. Its own list because it follows the map
    // scan's switch, not the finder's: a scan is minutes of work and its marks outlive the aim
    std::vector<Vec3> mapTbPts;
    int  assistActive = -1;        // assistPts index the assist works toward (-1 = none)
    bool lineActive = false;       // finder scan column being stretched
    Vec3 lineA, lineB;
    Vec3 localOrigin;              // for the distance fade
    char boxText[96] = {};         // botox "<launch> to surf at <z> with <z> (stand)" popup text
    unsigned long long boxMs = 0;  // GetTickCount64 at fire (0 = none)
    unsigned long long aliveMs = 0;
};
void PS_GetDrawData(PsDrawData& out);
void PS_GetBox(PsDrawData& out);   // boxText/boxMs only -- no vector copies
// pixelsurf lines: current map's ridden surfs, walked with the finder's hold proof (pixelsurf.inl
// PsTrajectoryRecord -> psassist.inl PsRideLineTick), each a polyline of <= 8u pieces
void PS_GetTrajectory(std::vector<std::vector<Vec3>>& out);
void PS_ClearTrajectory();   // every map's lines (menu "clear lines")
// finder extra kinds: jump surfs (pins only a jump reaches), headbounce pins, pixeljump lips
void PF_GetExtraPts(std::vector<Vec3>& jump, std::vector<Vec3>& headbounce, std::vector<Vec3>& lip);

// ==== whole-map pixel surf scan (psmap.inl) ====
// The menu button toggles it; the scan itself lives on the movement thread, a slice per
// command, and publishes its lines into PsDrawData::mapLines as it goes.
struct PsMapScanState
{
    bool  busy = false;
    int   done = 0, total = 0;   // seams verified / seams found on this map
    int   lines = 0;             // surfs marked so far
    int   tb = 0;                // ...and texturebug seams (red rings), found by trace on the way
    float progress = 0.f;
};
void PS_MapScanToggle();              // start, or cancel a scan already running
void PS_MapScanGet(PsMapScanState& out);

// ==== route calculator snapshot (movement thread -> render thread) ====
// Same contract as PsDrawData: the movement thread owns the markers and the solve, the render
// thread only draws a copy. `gen` bumps on every edit so a menu row cannot act on a stale index.
struct RcDrawPt
{
    Vec3 pos;
    int  type = 0;         // rs::PointType (routesim.h)
    bool enabled = true;
    bool filtered = false; // its move list or stance flags are narrowed -- drawn with a *
    bool player = false;   // on a player's head: label " (player)"
    bool stand = true, duck = true;   // arrival stances: label " (ducked)" / " (stand)" when only one
    bool hbCeiling = false; // headbounce only: ceiling face (drawn red, like botox) vs seam window
    bool pjSkin = false;    // pixeljump only: a flush prop-top lip (botox pj_skin), label " (skin)"
};

struct RcDrawData
{
    std::vector<RcDrawPt>    pts;
    bool        hasPixel = false;      // pixel calculator marker
    Vec3        pixel;
    int         pixelType = 1;         // rs::PointType of it: PT_SURF or PT_PIXELJUMP (label)
    bool        pixelSolve = false;    // the answer below came from the pixel calculator, not the route
    // the first printed combination flown again tick by tick : the arc the overlay draws
    // between the markers instead of a straight line. Empty until a solve lands
    std::vector<Vec3> path;
    // the answer, composed on the movement thread: combos best first, "(and +N more)", or why not
    std::vector<std::string> lines;
    std::string message;               // placement / binding / cancel note (empty = none)
    bool        solving = false;
    float       progress = 0.f;
    unsigned    gen = 0;
};
void RC_GetDrawData(RcDrawData& out);
void RC_RefreshLive();   // MAIN thread: player-head markers follow their entity

// one marker, for the menu list
struct RcPointInfo
{
    Vec3     pos;
    int      type = 0;        // rs::PointType
    unsigned moves = 0;       // rs::MoveBit mask: what may be pressed off it
    bool     enabled = true;
    bool     stand = true, duck = true;   // arrival stances allowed
    bool     player = false;  // on a player's head
    bool     snapped = false; // pixel finder z
    bool     edgeZ = false;   // edgebug: z came off the nearest edge, not off where you clicked
    bool     hbCeiling = false; // headbounce only: true = ceiling face, false = seam window
    bool     pjSkin = false;    // pixeljump only: true = flush prop-top lip (botox pj_skin)
};

// marker access for the menu list (all lock internally)
unsigned    RC_PointsGen();
int         RC_PointCount();
bool        RC_PointGet(int i, RcPointInfo& out);
void        RC_PointSet(int i, const RcPointInfo& in);   // applies + persists (type/moves/enabled/stance)
void        RC_PointDelete(int i);
void        RC_PointMove(int i, int dir);   // -1 / +1: swap with the marker before / after it
void        RC_PointsClear();                // menu "clear all points"
void        RC_DropAnswer();                 // a setting changed: the shown route answer is stale
unsigned    RC_MarkerMoves();                // rs::MoveBit bits a ground marker's row may pick
unsigned    RC_StartMoves();                 // ...and the starting jump's row
const char* RC_MoveName(int bit);    // rs::MoveBit -> "longjump" etc (menu + panel share it)
int         RC_MoveCount();
const char* RC_TypeName(int type);
int         RC_TypeCount();

// assist point access for the render-thread ENTER window (all lock internally).
// mask bits, readout order: 0 mini, 1 regular, 2 mini+duck, 3 crouch, 4 long,
// 5 long+duck, 6 double jump. Gen bumps on load/add/erase -- open window closes.
unsigned PS_PointsGen();
bool PS_PointGet(int i, Vec3& pos, int& mask, float& radius); // false = index stale
void PS_PointSetMask(int i, int mask);          // applies + persists instantly
void PS_PointSetRadius(int i, float radius);    // 0 = follow the global radius slider
void PS_PointDelete(int i);                     // erase + persist

// fake pov in demos (movement_ex.inl FakePov_Tick/FakePov_GetState): smoothed yaw/pitch view
// offset + arrow indicator state. AIMTAB's CPrediction::GetLocalViewAngles hook (css_bridge,
// G::FakePOVOffset/G::FakePOVActive) reads this to feed botox's fake_pov.cpp effect -- the hook
// itself and the overlay draw are AIMTAB's, not movement's (INCLUDE RULE: this header has no
// aim/ types, plain floats only).
void FakePov_GetState(float& yaw, float& pitch, float& arrowAng, float& arrowLen, bool& active);
