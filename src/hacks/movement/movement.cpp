#include "../../globals/globals.h"
#include "../../globals/pinned_thread.h"   // routecalc.inl solver worker
#include "movement.h"
#include "../lua/lua.h"   // g_lua.on_create_move -- pre_prediction + final create_move dispatch
#include "../hud/avatar_cache.h"   // unit E5: Avatar_Pump piggybacks this game-thread tick
#include "prediction.h"   // engine prediction core (Pred_*, restore, sim pool, snapshots)
#include "mv_perf.h"      // per-call sim / restore / trace cost, "PERF:" log line
#include "recorder.h"      // unit B: movement recorder (record/play owns the cmd before features run)
#include "eb_stance.h"    // edge bug stance filter (pure, gate-tested)
#include "airlean.h"      // AirAccelerate geometry: best-gain / free-turn wish angle (gate-tested)
#include "routesim.h"     // z-only CTFGameMovement port: AutoJumpbug's policy + its look-ahead
#include "ps_surf.h"      // "is this pin a surf or a stuck hull" -- the ps indicator's rule (gate-tested)
#include "ps_map.h"       // whole-map scan candidates: a brush wall face's top edge (gate-tested)
#include "../menu/cfg/u_mvps.h" // MVPS unit vars: pixel surf detection sound (g_psSound/_Custom/_Volume)
#include "../menu/cfg/u_mvtb.h" // MVTB unit vars: texture bug sound (g_tbSound*)
#include "../menu/cfg/u_movx.h" // MOVX unit vars: air freeze, ps wall strafe toggle, edgebug lob
#include "mv_scanbound.h" // MV_DetectWallLines broadphase box (pure, gate-tested)
#include "css_moves_pure.h"   // 11-07 aa/ps/lj/mj/jb rewrite: hull shift, wish math, lj hold (gate-tested)
#include "movement_ex_pure.h" // Unit A port: pure decisions (hsw/delay hop/lj adaptive cancel), gate-tested
#include "movement_tricks2_pure.h" // Unit A3 port: pure decisions (bounce tangent/eb style), gate-tested
#include "movement_edge_pure.h" // Unit A2 port: pure decisions (edge skip duck gate, blockbot wish/sign), gate-tested
#include "fireman_pure.h" // ladder unit: fireman catch math (flat-face/degenerate, air-accel approach), gate-tested
#include "ladder_pure.h"  // ladder unit: fast_ladder/glide/freelook/anti_ladder/auto_one_hop angle math, gate-tested
#include "detections.h"   // botox chat detections, fed the cmd start state at the publish point
#include "jumpstats_core.h" // botox jump_stats state machine (movement_ex.inl JumpstatsTick), gate-tested
#include "../hud/points.h" // g_esFired: edge skip's lip landing, the keybind indicator particle count
#include "../web/websurface.h"       // Web_BlockFire
#include "../aim/css_bridge.h"       // CssBridge_SetFakePov (fake pov in demos -> AIMTAB's engine hook)
#include "../botoxnet/botoxnet.h"    // g_botox_net.on_create_move
#include <mutex>
#include <atomic>
#include <thread>
#include <cfloat>
#include <climits>
#include <cstdlib>   // std::rand (bunny hop "success rate" roll)
#include <memory>    // check floor level's shared_ptr<const vector<...>> mesh (botox's own data.mesh shape)

// ref Misc.cpp movement port (core set) + EnginePrediction port.
// All engine access = netvar offsets + vtable slots confirmed against the x64 client.

// ==== buttons / flags / etc ====
static constexpr int IN_ATTACK    = 1 << 0;
static constexpr int IN_JUMP      = 1 << 1;
static constexpr int IN_DUCK      = 1 << 2;
static constexpr int IN_FORWARD   = 1 << 3;
static constexpr int IN_BACK      = 1 << 4;
static constexpr int IN_MOVELEFT  = 1 << 9;
static constexpr int IN_MOVERIGHT = 1 << 10;
static constexpr int IN_ATTACK2   = 1 << 11;

static constexpr int FL_ONGROUND = 1 << 0;
static constexpr int FL_DUCKING  = 1 << 1;
// const.h:150-181, the FL_ANIMDUCKING branch (the one TF2 builds). Read by the fast-probe
// tick guard: each of these adds or replaces a force the closed-form air tick does not model
static constexpr int FL_ANIMDUCKING  = 1 << 2;
static constexpr int FL_WATERJUMP    = 1 << 3;
static constexpr int FL_FROZEN       = 1 << 6;
static constexpr int FL_INWATER      = 1 << 10;
static constexpr int FL_SWIM         = 1 << 12;
static constexpr int FL_CONVEYOR     = 1 << 13;
static constexpr int FL_BASEVELOCITY = 1 << 24;

static constexpr int MOVETYPE_WALK     = 2;
static constexpr int MOVETYPE_FLY      = 4;   // was 3 = MOVETYPE_STEP (const.h:196-197)
static constexpr int MOVETYPE_NOCLIP   = 8;
static constexpr int MOVETYPE_LADDER   = 9;   // const.h MoveType_t: NONE0 ISOMETRIC1 WALK2 STEP3
                                               // FLY4 FLYGRAVITY5 VPHYSICS6 PUSH7 NOCLIP8 LADDER9
static constexpr int MOVETYPE_OBSERVER = 10;

static constexpr unsigned MASK_ALL_MV   = 0xFFFFFFFF;
static constexpr unsigned short SURF_TRIGGER_MV = 0x40;   // trigger brush surface
static constexpr int DISPSURF_SURFACE   = 1 << 0;         // trace.dispFlags: displacement hit
static constexpr int FSOLID_NOT_SOLID_MV = 0x4;           // m_usSolidFlags
static constexpr int FSOLID_TRIGGER_MV   = 0x8;

// MASK_PLAYERSOLID = SOLID|MOVEABLE|PLAYERCLIP|WINDOW|MONSTER|GRATE
static constexpr unsigned MASK_PLAYERSOLID = 0x1 | 0x4000 | 0x10000 | 0x2 | 0x2000000 | 0x8;

// CS has no team-conditional solid brushes (no TF setup-gate force fields, no buildable-object
// collision group) and no TF-style m_nPlayerCond movement-blocking bits, so neither constant
// group from the TF port applies here -- see TraceFilterWorldProps::sShouldHit below.

// ==== vtable slots (confirmed x64) ====
static constexpr int kEngine_SetViewAngles   = 20; // IVEngineClient::SetViewAngles(QAngle&)
// kTrace_TraceRay, GameTrace_t, Ray_mv, cplane_mv, csurface_mv moved to movement.h --
// backstab.cpp traces too, and two copies of an engine struct is layout drift waiting

// TraceType 3 = TRACE_EVERYTHING_FILTER_PROPS. World brushes always trace; filter
// decides entities. Port of CTraceFilterWorldAndPropsOnly, class check simplified:
// hit static props (serial 1<<15); reject players (entity list compare); reject
// trigger / non-solid brush volumes (m_usSolidFlags) so eb/ps/aa/jumpbug/wallclimb
// never false-detect off resupply / capture / hurt zones; hit the rest (doors/props).
struct TraceFilterWorldProps
{
    void** vfptr;
    void*  pSkip;

    static bool __fastcall sShouldHit(TraceFilterWorldProps* self, void* pEnt, int /*mask*/)
    {
        if (!pEnt || pEnt == self->pSkip) return false;
        unsigned int h = 0xFFFFFFFF;
        __try {
            unsigned int* pH = VCall<unsigned int*>((void*)pEnt, 2); // IHandleEntity::GetRefEHandle
            if (pH) h = *pH;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if ((h >> 16) == (1u << 15)) return true;           // static prop
        int idx = (int)(h & 0xFFFF);                        // ENT_ENTRY_MASK (16 bit)
        if (idx >= 1 && idx <= 64 && GetClientEntity(idx) == pEnt)
            return false;                                   // player slot -> skip
        if (g_offSolidFlags > 0)                            // pass-through trigger volumes
        {
            __try {
                unsigned short fl = *reinterpret_cast<unsigned short*>(reinterpret_cast<uintptr_t>(pEnt) + g_offSolidFlags);
                if (fl & (FSOLID_TRIGGER_MV | FSOLID_NOT_SOLID_MV))
                    return false;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        // the TF port's "team brushes are not solid to their own team" exception modelled
        // CTFGameMovement::PlayerSolidMask's enemy-team setup-gate bit + the buildable-object
        // collision group (baseobject_shared.cpp:468). CS has neither mechanic: no setup gates,
        // no buildable objects, so every brush here is just solid or not -- see report, CMAKE/N-A.
        return true;                                        // props / doors / world ents
    }
    static int __fastcall sTraceType(TraceFilterWorldProps* /*self*/) { return 3; }

    void Setup(void* skip)
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
        pSkip = skip;
    }
};

// ==== interfaces / sigs (Movement_Install) ====
static void* g_engineTrace  = nullptr; // EngineTraceClient003

// wave 2: engine predicted-frame restore + abs origin write (both direct fns, ref sigs)
using SetAbsOrigin_t = void(__fastcall*)(void* ent, const Vec3&);  // CBaseEntity::SetAbsOrigin
static SetAbsOrigin_t g_fnSetAbsOrigin = nullptr;
// CBaseEntity::SetAbsVelocity: SetupMove reads GetAbsVelocity (sdk prediction.cpp:623), so a sim
// started off a m_vecVelocity netvar write alone runs the OLD velocity. Optional, nothing gates on it
static SetAbsOrigin_t g_fnSetAbsVelocity = nullptr;

// mov/lea [rip+disp32]: disp at +3, insn len 7
static uint8_t* RelToAbs(uint8_t* addr)
{
    return addr + 7 + *reinterpret_cast<int32_t*>(addr + 3);
}

void Movement_Install()
{
    if (g_predReady) return;

    if (!g_engineTrace)  g_engineTrace  = GetInterface("engine.dll", "EngineTraceClient003");
    if (!g_prediction)   g_prediction   = GetInterface("client.dll", "VClientPrediction001");
    if (!g_gameMovement) g_gameMovement = GetInterface("client.dll", "GameMovement001");

    HMODULE hClient = GetModuleHandleA("client.dll");
    HMODULE hEngine = GetModuleHandleA("engine.dll");
    if (!hClient || !hEngine) return;

    __try {
        if (!g_moveHelper) {
            // ref IMoveHelper sig: mov rcx,[rip] -> &g_pMoveHelper, deref once
            uint8_t* a = PatternScan(hClient, "48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 0F B7 D7");
            if (a) g_moveHelper = *reinterpret_cast<void**>(RelToAbs(a));
        }
        if (!g_globalVars) {
            // ref CGlobalVarsBase sig: lea rax,[rip+gpGlobals]; ret
            uint8_t* a = PatternScan(hEngine, "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B CA");
            if (a) g_globalVars = reinterpret_cast<GlobalVars_t*>(RelToAbs(a));
        }
        if (!g_randomSeed) {
            // ref RandomSeed sig
            uint8_t* a = PatternScan(hClient, "0F B6 1D ? ? ? ? 89 9D");
            if (a) g_randomSeed = reinterpret_cast<int*>(RelToAbs(a));
        }
        if (!g_fnTransferData) {
            // ref CPredictionCopy::TransferData sig (direct function)
            uint8_t* a = PatternScan(hClient, "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 8B 3D");
            if (a) g_fnTransferData = reinterpret_cast<TransferData_t>(a);
        }
        if (!g_fnRestoreFrame) {
            // ref CPrediction_RestoreEntityToPredictedFrame sig (direct fn: this, frame)
            uint8_t* a = PatternScan(hClient, "40 55 48 83 EC ? 8B EA");
            if (a) g_fnRestoreFrame = reinterpret_cast<RestoreFrame_t>(a);
        }
        if (!g_fnSetAbsOrigin) {
            // ref CBaseEntity_SetAbsOrigin sig (direct fn: this, const Vec3&)
            uint8_t* a = PatternScan(hClient, "48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? F3 0F 10 83");
            if (a) g_fnSetAbsOrigin = reinterpret_cast<SetAbsOrigin_t>(a);
        }
        if (!g_fnSetAbsVelocity) {
            // ref CBaseEntity_SetAbsVelocity sig (aim/defs.h), unique in client.dll on disk 11-03
            uint8_t* a = PatternScan(hClient, "48 89 5C 24 ? 57 48 83 EC ? F3 0F 10 81 ? ? ? ? 48 8B DA 0F 2E 02");
            if (a) g_fnSetAbsVelocity = reinterpret_cast<SetAbsOrigin_t>(a);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    g_predReady = g_prediction && g_gameMovement && g_moveHelper && g_globalVars
               && g_randomSeed && g_fnTransferData;
    g_frameReady = g_predReady && g_fnRestoreFrame && g_fnSetAbsOrigin;

    DebugLog("movement: trace=%p pred=%p gm=%p mh=%p gv=%p seed=%p xfer=%p restore=%p setabs=%p setvel=%p ready=%d frame=%d",
             g_engineTrace, g_prediction, g_gameMovement, g_moveHelper,
             g_globalVars, g_randomSeed, (void*)g_fnTransferData,
             (void*)g_fnRestoreFrame, (void*)g_fnSetAbsOrigin, (void*)g_fnSetAbsVelocity, (int)g_predReady, (int)g_frameReady);
}


// ==== entity field reads (netvar offsets from netvars.cpp) ====
static bool EntGround(void* e)
{
    bool byHandle = g_offGroundEntity > 0 && EntI(e, g_offGroundEntity) != -1;
    bool byFlags  = g_offFlags > 0 && (EntI(e, g_offFlags) & FL_ONGROUND);
    return byHandle || byFlags;
}
static bool EntDucking(void* e) { return g_offFlags > 0 && (EntI(e, g_offFlags) & FL_DUCKING); }
static int  EntMoveType(void* e){ return g_offWaterLevel > 0 ? EntB(e, g_offWaterLevel - 4) : MOVETYPE_WALK; }
static bool EntSwimming(void* e){ return g_offWaterLevel > 0 && EntB(e, g_offWaterLevel) >= 2; }

// CSS stamina (DT_CSPlayer -> DT_CSLocalPlayerExclusive m_flStamina, css-community-master
// c_cs_player.cpp:660-676; aim/css.h already has the NETVAR but that header is aim-only --
// INCLUDE RULE forbids non-aim TUs from it, so this is the brief's local stopgap resolution:
// its own RecvTable lookup, read as the raw netvar (not CssBridge_GetStaminaFraction's 0-1
// "fresh..spent" normalization, a different scale CssStaminaRatio's formula below is not
// written against). css_bridge.h DOES have CssBridge_GetStaminaFraction/CssBridge_Stamina now
// (11-04) -- left unswapped here: wallclimb/autobounce/psassist all read CssStaminaRatio's
// current 0-1000-scale formula, and re-deriving it against the bridge's normalized fraction
// without a chance to verify the behavior change is a regression risk this report flags rather
// than silently taking.
static int s_offStamina = -1;
static float EntStamina(void* e)
{
    if (s_offStamina < 0)
    {
        RecvTable* t = FindNetworkTable("DT_CSPlayer");
        s_offStamina = t ? FindPropOffset(t, "m_flStamina") : 0;
    }
    return (s_offStamina > 0 && e) ? EntF(e, s_offStamina) : 0.f;
}
// cs_gamemovement.cpp:683-692/418 STAMINA_MAX=100 STAMINA_RECOVER_RATE=19: jump/duck speed
// ratio applied when stamina > 0, else 1 (full speed).
static float CssStaminaRatio(void* e)
{
    const float stam = EntStamina(e);
    if (stam <= 0.f) return 1.f;
    const float ratio = (100.f - (stam / 1000.f) * 19.f) / 100.f;
    return std::clamp(ratio, 0.f, 1.f);
}
// TF's m_nPlayerCond taunt/charge movement-blocking bits have no CS equivalent (no taunts, no
// charge meter) -- the TF port's EntCondBlocked() is gone, bMoveBlocked below drops that term.

// Forward decl: defined in autobounce.inl (included after psassist.inl in this TU) so
// pixelsurf assist's yield check can see AutoBounce's ownsCmd flag, matching botox's
// "every feature that already owns the cmd outranks a new plan" rule.
static bool AutoBounceOwnsCmd();

// Cached ConVar float read. `def` is returned for a var this build does not have, so a
// missing name is a documented default rather than a zero nobody notices.
static float CvarFloat(const char* name, float def)
{
    if (!g_cvar) return def;
    // 16, not 8. The movement half asks for ten distinct names (sv_gravity, sv_airaccelerate,
    // sv_accelerate, sv_maxvelocity, sv_stepsize, tf_clamp_airducks, cl_forwardspeed,
    // cl_sidespeed, m_yaw, sensitivity), so an 8-slot cache left two of them permanently on the
    // "cache full" path -- an ICvar dictionary lookup by string, every call, every command.
    // Headroom, not a guess: if this ever fills again the same silent cost comes back.
    static struct { const char* n; ConVar* v; } cache[16] = {};
    ConVar* cv = nullptr;
    bool inCache = false;
    for (auto& c : cache) {
        // pointer test first: callers pass literals, /O2 pools them, so strcmp almost never runs
        if (c.n && (c.n == name || strcmp(c.n, name) == 0)) { cv = c.v; inCache = true; break; }
        if (!c.n) { c.n = name; c.v = cv = g_cvar->FindVar(name); inCache = true; break; }
    }
    if (!inCache) cv = g_cvar->FindVar(name); // cache full: slow lookup, not silent default
    if (!cv) return def;
    float r = def;
    __try { r = *reinterpret_cast<float*>(ConVarParent(cv) + k_cvFloat); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = def; }
    return r;
}


// Not VCall: the engine takes `Vec3&`, and VCall's by-value parameter pack would hand it
// a copy instead of the caller's object.
static void EngineSetViewAngles(Vec3& ang)
{
    if (!g_engine) return;
    using SetVA_t = void(__fastcall*)(void*, Vec3&);
    __try {
        void** vt = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000)
            ((SetVA_t)vt[kEngine_SetViewAngles])(g_engine, ang);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// IVEngineClient::GetViewAngles sits immediately BEFORE SetViewAngles in the interface
// (cdll_int.h:263/265), so it is the confirmed slot 20 minus one.
static constexpr int kEngine_GetViewAngles = kEngine_SetViewAngles - 1;

static bool EngineGetViewAngles(Vec3& ang)
{
    if (!g_engine) return false;
    using GetVA_t = void(__fastcall*)(void*, Vec3&);
    __try {
        void** vt = *(void***)g_engine;
        ((GetVA_t)vt[kEngine_GetViewAngles])(g_engine, ang);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ==== bind runtime (delusional menu::checkkey) ====
// Every gate used to hit user32 on EVERY ask, and a cmd asks ~30 times: needPred alone
// tests 7 features, then each feature re-tests its own bind, and PS assist tests two more.
// That is ~30 GetForegroundWindow + ~30 GetAsyncKeyState per tick, ~4000/s at 66.7.
// Worse, it was not even self-consistent: a key released between two asks inside ONE cmd
// let feature A run on "pressed" while feature B saw "released" -- a half-applied tick.
// Resolve both once per cmd and hand every ask the same answer.
static bool s_bindAppActive = true;
struct BindMemo_t { int key; int mode; bool val; };
static BindMemo_t s_bindMemo[16] = {};
static int  s_bindMemoN = 0;

static void MovementBinds_NewCmd()
{
    s_bindAppActive = !g_hwnd || GetForegroundWindow() == g_hwnd; // is_active_app gate
    s_bindMemoN = 0;
}

static bool BindKeyDown(int key, int mode)
{
    return (mode == 1) ? (GetAsyncKeyState(key) & 0x8000) != 0   // on hotkey
                       : (GetKeyState(key) & 1) != 0;            // toggle (key toggle bit)
}

bool MovementKeyOK(int key, int mode)
{
    if (!s_bindAppActive) return false;
    if (mode != 1 && mode != 2) return true;   // 0 = always on; unknown modes stay on
    if (key == 0) return false;

    for (int i = 0; i < s_bindMemoN; ++i)
        if (s_bindMemo[i].key == key && s_bindMemo[i].mode == mode) return s_bindMemo[i].val;

    const bool v = BindKeyDown(key, mode);
    // memo full (more distinct binds held than slots): answer live, never guess
    if (s_bindMemoN < (int)(sizeof(s_bindMemo) / sizeof(s_bindMemo[0])))
        s_bindMemo[s_bindMemoN++] = { key, mode, v };
    return v;
}
static bool On(bool enabled, int key, int mode) { return enabled && MovementKeyOK(key, mode); }

// ==== trace helper ====
// COLDTIRES 80th -- the trace budget is the REAL tick cost near a wall, not the sims:
// one wall scan is 16 dirs x (1 hull + ~11-73 line traces), and a seam tick ran it twice.
// Counted here, printed as tr= in the TB line, so "faster" is a measurement not a claim
static int s_tracesThisCmd = 0;

// __try and the timer object cannot share a function (C2712)
static void MvTraceRaySeh(const Ray_mv& ray, unsigned mask, void* filter, GameTrace_t* tr)
{
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)g_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(g_engineTrace, ray, mask, filter, tr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { tr->fraction = 1.f; }
}
static void MvTraceRayTimed(const Ray_mv& ray, unsigned mask, void* filter, GameTrace_t* tr)
{
    MvPerfScope _perf(g_perfTrace);
    MvTraceRaySeh(ray, mask, filter, tr);
}

static void TraceHull(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                      unsigned mask, void* filter, GameTrace_t* tr)
{
    ++s_tracesThisCmd;
    if (!g_engineTrace) { tr->fraction = 1.f; return; }
    Ray_mv ray;
    ray.Init(start, end, mins, maxs);
    MvTraceRayTimed(ray, mask, filter, tr);
}

static bool s_predicted = false; // this cmd got a Start/Simulate (End pending)



// Diag throttle. TB and AS each printed a ~250 byte line EVERY tick, and DebugLog is a
// synchronous WriteFile under a critical section on the CreateMove thread -- ~130
// syscalls/s in the movement path, and TB alone was 2175 of one log's 2500 lines, which
// buried everything else. A tick where something HAPPENED still prints every time; a
// quiet tick prints 1 in 33, the rate the PS reject diag already used.
static bool DiagOk(int& lastTick, bool bInteresting)
{
    const int now = g_globalVars ? g_globalVars->tickcount : 0;
    if (bInteresting || now < lastTick || now - lastTick >= 33) { lastTick = now; return true; }
    return false;
}

// ---- THE WISH THAT EXACTLY SATURATES ONE TICK OF AIR ACCELERATION ----
// AirAccelerate adds `accelspeed = sv_airaccelerate * wishspeed * frametime * surfaceFriction`
// and caps it at `addspeed = GetAirSpeedCap() - dot(velocity, wishdir)` (gamemovement.cpp:1720-
// 1745, cap 30 in gamemovement.h:104 and CTFGameMovement::GetAirSpeedCap for the normal case).
// With the wish perpendicular to velocity the dot is 0, so the cap is the full 30 and the wish
// that reaches it is 30 / (airaccel * dt).
// IT WAS WRITTEN AS THE CONSTANT 200, and 200 is that arithmetic at 1/66.7 s -- 192 at CSGO's 64
// tick, 198 at TF2's 66, and anything at all on a server that runs neither. Both readers use it
// as a UNIT (AS drives in at 1x, the EB lip steer asks for a fraction of it), so a wrong unit is
// a wrong fraction on every tick, not merely a slow approach. Derived per call now: the cvars
// are cached lookups and this runs once per steer, not once per trace.
static float AirWishSaturate()
{
    const float flAccel = CvarFloat("sv_airaccelerate", 10.f);
    const float flPerUnit = flAccel * TickInterval();
    if (flPerUnit < 1e-4f) return 450.f;   // airaccel 0 -> no wish buys anything; ask for max
    return std::clamp(30.f / flPerUnit, 1.f, 450.f);
}

// AS wall-detect reach FLOOR. An earlier build set this to 8 and passed pVel = nullptr, because detect's
// lookahead (`max(0, dot(vel,dir) * TickInterval())`) was then the WHOLE reach: 2u standing,
// ~9.5u at 500 u/s. Making it constant fixed the slow case and broke the fast one -- the log read:
// `nowall=321 ran=651` = HALF of AS's ticks exit "no wall found", every one of them at
// xy 250-320 u/s. That is "graze a wall very fast and the bind does nothing": at 300 u/s the
// hull crosses 4.5u per tick, so a wall 9u out is out of reach this tick and behind us the next.
// 8u FLOOR + the per-direction lookahead ON TOP is both cases at once: standing still keeps the
// full 8 (an earlier build's fix intact), 500 u/s into the wall reaches ~15.8. The sweep sims a whole tick
// forward, so a wall one tick's travel away is a wall it can legitimately work
static constexpr float k_asWallReach = 8.f;


// ==== CMisc::CorrectMovement port ====
// SDK::FixMovement lived here too and had zero call sites (/W4 C4505) -- CorrectMovement
// is what every silent-move path in this file actually uses. Deleted, not commented out.
void CorrectMovement(UserCmd_t* pCmd, Vec3 vWishAngle, Vec3 vOldAngles)
{
    if (vOldAngles.x == vWishAngle.x && vOldAngles.y == vWishAngle.y && vOldAngles.z == vWishAngle.z)
        return;

    Vec3 vWishForward, vWishRight, vWishUp;
    Vec3 vCmdForward, vCmdRight, vCmdUp;
    Vec3 vMoveData(pCmd->forwardmove, pCmd->sidemove, pCmd->upmove);

    AngleVectors(vWishAngle, &vWishForward, &vWishRight, &vWishUp);
    AngleVectors(vOldAngles, &vCmdForward, &vCmdRight, &vCmdUp);

    float flWishForwardLen = vWishForward.Length2D();
    float flWishRightLen = vWishRight.Length2D();
    float flWishUpLen = fabsf(vWishUp.z);
    if (flWishForwardLen == 0.f || flWishRightLen == 0.f || flWishUpLen == 0.f) return;

    Vec3 vWishForwardNorm(vWishForward.x / flWishForwardLen, vWishForward.y / flWishForwardLen, 0.f);
    Vec3 vWishRightNorm(vWishRight.x / flWishRightLen, vWishRight.y / flWishRightLen, 0.f);
    Vec3 vWishUpNorm(0.f, 0.f, vWishUp.z / flWishUpLen);

    float flCmdForwardLen = vCmdForward.Length2D();
    float flCmdRightLen = vCmdRight.Length2D();
    float flCmdUpLen = fabsf(vCmdUp.z);
    if (flCmdForwardLen == 0.f || flCmdRightLen == 0.f || flCmdUpLen == 0.f) return;

    Vec3 vCmdForwardNorm(vCmdForward.x / flCmdForwardLen, vCmdForward.y / flCmdForwardLen, 0.f);
    Vec3 vCmdRightNorm(vCmdRight.x / flCmdRightLen, vCmdRight.y / flCmdRightLen, 0.f);
    Vec3 vCmdUpNorm(0.f, 0.f, vCmdUp.z / flCmdUpLen);

    Vec3 vCorrectMove;
    vCorrectMove.x = vCmdForwardNorm.x * (vWishForwardNorm.x * vMoveData.x + vWishRightNorm.x * vMoveData.y) +
        vCmdForwardNorm.y * (vWishForwardNorm.y * vMoveData.x + vWishRightNorm.y * vMoveData.y);
    vCorrectMove.y = vCmdRightNorm.x * (vWishForwardNorm.x * vMoveData.x + vWishRightNorm.x * vMoveData.y) +
        vCmdRightNorm.y * (vWishForwardNorm.y * vMoveData.x + vWishRightNorm.y * vMoveData.y);
    vCorrectMove.z = vCmdUpNorm.z * vWishUpNorm.z * vMoveData.z;

    vCorrectMove.x = std::clamp(vCorrectMove.x, -450.f, 450.f);
    vCorrectMove.y = std::clamp(vCorrectMove.y, -450.f, 450.f);
    vCorrectMove.z = std::clamp(vCorrectMove.z, -320.f, 320.f);

    pCmd->forwardmove = vCorrectMove.x;
    pCmd->sidemove = vCorrectMove.y;
    pCmd->upmove = vCorrectMove.z;
}

// ============================================================================
// wave 2 helpers (ref TB_* primitives + engine predicted-frame anchor)
// ============================================================================


static void ClampAnglesMv(Vec3& v)
{
    v.x = std::clamp(NormalizeAngle(v.x), -89.f, 89.f);
    v.y = NormalizeAngle(v.y);
    v.z = 0.f;
}

// line trace (no extents)
static void TraceLine(const Vec3& start, const Vec3& end, unsigned mask, void* filter, GameTrace_t* tr)
{
    ++s_tracesThisCmd;
    if (!g_engineTrace) { tr->fraction = 1.f; return; }
    Ray_mv ray;
    ray.Init(start, end);
    MvTraceRayTimed(ray, mask, filter, tr);
}

// ---- the two questions a trace cannot answer, asked of the BSP directly --------------------
// IEngineTrace slot 0 GetPointContents and slot 16 PointOutsideWorld (src/public/engine/
// IEngineTrace.h, in that order after the confirmed TraceRay 4 / GetBrushesInAABB 13 /
// GetBrushInfo 15 this file already calls). Both walk one leaf, so they cost a fraction of the
// hull trace they replace, and they answer what a swept trace CANNOT: a hull trace reports where
// a sweep stopped, never whether the point it started from is buried in a brush or sitting in the
// void outside every playable area. The whole-map scan asks both before it spends a sim.
static int MV_PointContents(const Vec3& p)
{
    if (!g_engineTrace) return 0;
    using Contents_t = int(__fastcall*)(void*, const Vec3&, void*);
    __try {
        void** vt = *(void***)g_engineTrace;
        return ((Contents_t)vt[0])(g_engineTrace, p, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// "Tests a point to see if it's outside any playable area" -- the engine's own words. True for
// the solid void a sealed map is wrapped in, which is where half the junk a whole-map brush walk
// turns up lives (the OUTSIDE faces of the brushes that do the sealing).
static bool MV_PointOutsideWorld(const Vec3& p)
{
    if (!g_engineTrace) return false;
    using Outside_t = bool(__fastcall*)(void*, const Vec3&);
    __try {
        void** vt = *(void***)g_engineTrace;
        return ((Outside_t)vt[16])(g_engineTrace, p);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// SLOT CHECK, ONCE PER SESSION. PointOutsideWorld sits one index past the last slot of this
// interface anything here has confirmed in the wild (TraceRay 4, GetBrushesInAABB 13,
// GetBrushInfo 15 all work), and TF2 ships EngineTraceClient004 against the SDK header's 003. A
// wrong slot would not crash -- the __try sees to that -- it would answer nonsense, and nonsense
// here means silently throwing away every spot the finder found, which is the worst kind of bug
// to ship into a search. So it is asked about a point we KNOW is inside the map (the local
// player, who is standing in it) before it is believed, and switched off for the session if it
// gets that wrong. GetPointContents is slot 0 and needs no such check: a vtable's first entry is
// the one thing a version bump cannot move.
static int s_mvOutsideOk = -1;            // -1 not asked yet, 0 do not trust, 1 trusted
static void MV_ValidateOutsideWorld(const Vec3& vKnownInside)
{
    if (s_mvOutsideOk >= 0 || !g_engineTrace) return;
    s_mvOutsideOk = MV_PointOutsideWorld(vKnownInside) ? 0 : 1;
    DebugLog("MV: PointOutsideWorld self-check at %.0f %.0f %.0f -> %s", vKnownInside.x,
             vKnownInside.y, vKnownInside.z,
             s_mvOutsideOk ? "ok, out-of-bounds gate armed"
                           : "it called the player's own hull OUTSIDE the map -- wrong slot on "
                             "this build, gate OFF (CONTENTS_SOLID still applies)");
}

// a spot no player can ever occupy: buried in a brush, or off the edge of the map
static bool MV_PointUnusable(const Vec3& p)
{
    if ((MV_PointContents(p) & 0x1) != 0) return true;      // CONTENTS_SOLID
    return s_mvOutsideOk == 1 && MV_PointOutsideWorld(p);
}

// ---- which BSP AREA a point sits in (-1 = could not ask) ---------------------------------
// IVEngineClient GetBSPTreeQuery 42 + GetLeavesArea 57 (cdll_int.h order, == Amalgam x64
// IVEngineClient.h, counted from the confirmed GetLevelName 51) and ISpatialQuery slot 1
// EnumerateLeavesAtPoint (bsptreedata.h:71: LeafCount 0, then the enumerators, no dtor). The
// whole-map scan asks it to tell the 3D skybox (its own area) from the map.
// Validated once like PointOutsideWorld: the tree's LeafCount (its slot 0) has to equal the
// engine's LevelLeafCount (41). Anything else = not the interface we think, gate stays off.
struct MvLeafCatch   // ISpatialLeafEnumerator: one virtual, first leaf wins, stop
{
    virtual bool EnumerateLeaf(int leaf, intptr_t ctx) { *reinterpret_cast<int*>(ctx) = leaf; return false; }
};
static MvLeafCatch s_mvLeafCatch;
static int s_mvAreaOk = -1;               // -1 not asked yet, 0 do not trust, 1 trusted
static int MV_PointArea(const Vec3& p)
{
    if (!g_engine) return -1;
    __try {
        void** ve = *(void***)g_engine;
        void* q = ((void*(__fastcall*)(void*))ve[42])(g_engine);
        if (!q) return -1;
        void** vq = *(void***)q;
        if (s_mvAreaOk < 0)
        {
            const int nTree = ((int(__fastcall*)(void*))vq[0])(q);
            const int nLvl  = ((int(__fastcall*)(void*))ve[41])(g_engine);
            s_mvAreaOk = (nTree > 0 && nTree == nLvl) ? 1 : 0;
            DebugLog("MV: BSP area query self-check: tree leaves %d, level leaves %d -> %s",
                     nTree, nLvl, s_mvAreaOk ? "ok" : "MISMATCH, area gate OFF");
        }
        if (s_mvAreaOk != 1) return -1;
        int leaf = -1;
        ((bool(__fastcall*)(void*, const Vec3&, void*, intptr_t))vq[1])(q, p, &s_mvLeafCatch, (intptr_t)&leaf);
        if (leaf < 0) return -1;
        return ((int(__fastcall*)(void*, int*, int))ve[57])(g_engine, &leaf, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// WorldProps now rejects trigger / non-solid volumes itself -> the two filters are
// identical. Alias kept so wall-detect / texbug call sites read as brush-only intent.
using TraceFilterWorldBrush = TraceFilterWorldProps;


// target catch vz: -(sv_gravity * interval * 0.5) ~= -6.0 @66.67 tick (ref
// GetTargetPredictZVelocity -- the "landed on a surface this tick" pin velocity).
// cached per cmd: IsTargetVz runs 100s of times inside TB/AS sweep sims -- cvar
// walk (strcmp cache + parent deref) each call is pure waste, gravity is fixed mid-cmd
static float s_targetPredictZVel = -6.f;
static void RefreshTargetPredictZVel()
{
    s_targetPredictZVel = -(CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f);
}
static float TargetPredictZVel() { return s_targetPredictZVel; }
// CSS x64 ClipVelocity also pushes its result off the plane by normal * DIST_EPSILON (user 11-07;
// log: every caught surf read vz -5.97 while the free-fall step was 12.00 = halfgrav 6.0). So a
// tick pinned by a CLIP ends 1/32 off the pin along the plane's z (floor / lip: pin + 1/32;
// ceiling: pin - 1/32), and one whose velocity was ZEROED (TryPlayerMove allsolid / stuck, no
// clip) ends at pin exactly. All three are "pinned"; every vz-pin test goes through here.
static bool IsTargetVz(float vz, float eps)
{
    return cssm::PinnedVz(vz, -s_targetPredictZVel, eps);
}


// projection of the player half-extents onto the wall normal
static float TB_WallSupportDistance(void* pLocal, const Vec3& vWallNormal)
{
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    const Vec3 vHalf(fmaxf(fabsf(vMins.x), fabsf(vMaxs.x)),
                     fmaxf(fabsf(vMins.y), fabsf(vMaxs.y)),
                     fmaxf(fabsf(vMins.z), fabsf(vMaxs.z)));
    return fabsf(vWallNormal.x) * vHalf.x + fabsf(vWallNormal.y) * vHalf.y + fabsf(vWallNormal.z) * vHalf.z;
}

// ---- shared wall-scan / support helpers (creator texturebug base, 2026-07-12) ----

// 108th: how far off the ride plane's own offset a hit may sit and still count as that plane.
// 2u covers a seam step and brush float; a parallel wall across a gap never does
static constexpr float k_tbLockPlaneSlack = 2.f;

// per-Z line-trace scan for nearest facing wall (16 dirs). hull prefilter per angle --
// hull can only hit MORE than the lines, pure perf. hull hit with facing near-vertical
// normal also rescues short walls sitting between the scan lines.
//
// pVel = 1-tick lookahead: each ray is extended by the distance the player covers
// along THAT direction in one tick. The sweeps sim a tick ahead, so a detect that only
// sees walls already touching hands them a wall one tick after the variant that would
// have caught it -- the "delay before it activates on a wall". Extension is per-angle
// and clamped at 0, so a wall you are drifting away from never reaches further than
// extra_reach and cold/slow ticks keep the base 2u window (no early steering/duck)
//
// pRequireNormal = plane lock: ignore hits that aren't the given plane. Within one scan
// angle `best` is just the min fraction over every z ray, with nothing tying it to the
// plane we were on last tick -- a neighbouring brush poking out a hair at ANY z along
// the hull wins it, and then angle_wall moves and `sign` can flip, aiming the whole
// sweep at the wrong plane. That is the random stop at seams between brushes. Riders
// pass their ride plane here; callers fall back to an unlocked scan so leaving the
// plane for real still works
// COLDTIRES 120th -- the unlocked answer a FAILED locked scan already had in hand. A
// locked scan traces every direction the unlocked retry would trace, so keeping its first
// any-plane hit turns the seam tick's SECOND full scan into zero traces
struct TbWallHit_t
{
    GameTrace_t trace{};
    float angle = 0.f;
    bool  valid = false;
};

// the locked pass needs n.N >= 0.99 AND n.dir <= -0.1, so N.dir must be <= ~0.0413 for any
// match at all -- past that bound no hit in that direction can ever be returned, so the whole
// direction (hull + column + lines) is skippable
static constexpr float k_tbLockDirBound = 0.04f;

static bool MV_DetectWallLines(void* pLocal, const Vec3& start_pos, const Vec3& mins, const Vec3& maxs,
                               float scan_start, GameTrace_t& out_trace, float& out_angle,
                               float max_normal_z = 0.35f, float extra_reach = 2.f,
                               const Vec3* pVel = nullptr, const Vec3* pRequireNormal = nullptr,
                               const float* pRequirePlaneD = nullptr,
                               TbWallHit_t* pOutFallback = nullptr)
{
    const float max_radius = MV_PI * 2.f;
    const float step = max_radius / 16.f;
    const float z_step = g_tbScanStep > 0.5f ? g_tbScanStep : 1.f;
    const float half_x = fmaxf(fabsf(mins.x), fabsf(maxs.x));
    const float half_y = fmaxf(fabsf(mins.y), fabsf(maxs.y));
    const float lookahead_dt = pVel ? TickInterval() : 0.f;
    // FALL LOOKAHEAD, and it is the other half of the SHORT-LEDGE fix. The scan only ever
    // looked ahead HORIZONTALLY, so it saw a face that is level with the hull right now -- but
    // the sweeps sim a tick FORWARD, and at 400 u/s the hull drops ~6u in that tick. A lip 6u
    // below the feet is a face the sim can absolutely clip on, and the scan answered "no wall",
    // so TB never swept the one tick that could have caught it. Falling only (the direction of
    // travel), one tick's worth, capped at 32u so a terminal-velocity fall cannot turn the scan
    // into a floor detector. Callers that pass no velocity (WC/AS) are untouched
    const float fall_look = (pVel && pVel->z < 0.f) ? fminf(-pVel->z * lookahead_dt, 32.f) : 0.f;
    const Vec3  scan_mins(mins.x, mins.y, mins.z - fall_look);
    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    TbWallHit_t fallback{};

    // ---- BROADPHASE: ONE TRACE INSTEAD OF SIXTEEN ------------------------------------------
    // Every one of the 16 directions below opens with a hull trace before anything can reject
    // it, so standing in open air cost 16 hull traces to answer "no wall" -- and TB's ladder
    // (texturebug.inl) runs the whole scan twice on a full miss, so 32 per tick, every tick the
    // bind is held. AutoAlign / WallClimb / AirStuck pay it per tick too.
    // The predictive sweep is the site that hurts most: it runs one scan per predicted path
    // step against k_tbPredTraceCap = 400, so in open air the cap was spent after ~25 steps.
    // At 1 trace per empty step instead of 16 the same budget reaches far further, which is a
    // RANGE win for the sweep on top of the tick cost.
    // The box is a superset of every trace the loop can run (mv_scanbound.h derives it, and
    // tests/mvscan_test.cpp brute-forces that claim against the loop's own arithmetic), so a
    // miss here cannot hide a wall the loop would have found.
    {
        const float reach_xy = mv::ScanReachXY(half_x, half_y, extra_reach,
                                               pVel ? pVel->Length2D() : 0.f, lookahead_dt);
        GameTrace_t wide{};
        TraceHull(start_pos, start_pos,
                  Vec3(-reach_xy, -reach_xy, scan_mins.z), Vec3(reach_xy, reach_xy, maxs.z),
                  MASK_PLAYERSOLID, &fil, &wide);
        if (!wide.DidHit())
        {
            if (pOutFallback) *pOutFallback = fallback;   // empty, as a full miss leaves it
            return false;
        }
    }

    // 0.99 ~= 8 deg: coplanar neighbours (dot 1.0) stay on the ride, a genuinely
    // different face doesn't. Displacements/brush staircases lose the lock and come
    // back through the caller's unlocked rescan
    // 108th -- normals alone are NOT a plane. A PARALLEL wall (other side of a doorway, the
    // far face of the same brush) passes a normal-only lock, and the ride then measures its
    // support against one 24u away on alternate ticks: minsup flips -0.03 / 7.97 / -0.03
    // while lock=1. Pin the ride to the plane's OFFSET too (the plane's own dist, not endpos:
    // the hull prefilter's endpos sits a support distance short of the surface)
    const auto plane_ok = [&](const cplane_mv& p) -> bool
    {
        if (!pRequireNormal) return true;
        if ((p.normal.x * pRequireNormal->x + p.normal.y * pRequireNormal->y
             + p.normal.z * pRequireNormal->z) < 0.99f) return false;
        if (pRequirePlaneD && fabsf(p.dist - *pRequirePlaneD) > k_tbLockPlaneSlack) return false;
        return true;
    };

    for (float a = scan_start; a < scan_start + max_radius; a += step)
    {
        const float angle = fmodf(a, max_radius);
        const float dir_x = cosf(angle);
        const float dir_y = sinf(angle);

        // locked pass: this direction cannot hold the ride plane, and the unlocked answer it
        // would otherwise feed is already in hand -> nothing here can ever be returned
        if (pRequireNormal && fallback.valid
            && (pRequireNormal->x * dir_x + pRequireNormal->y * dir_y) > k_tbLockDirBound)
            continue;

        const float ax = fmaxf(fabsf(dir_x), 1e-6f);
        const float ay = fmaxf(fabsf(dir_y), 1e-6f);
        const float lookahead = pVel
            ? fmaxf(0.f, (pVel->x * dir_x + pVel->y * dir_y) * lookahead_dt) : 0.f;
        // the hull's own support along this direction: centre -> the corner the sweep leads with
        const float support = fminf(half_x / ax, half_y / ay);
        // rays leave the CENTRE, so they need the support on top to clear the hull's own width.
        // reach past the hull surface = extra_reach + lookahead, which is the contract
        const float ray_length = support + extra_reach + lookahead;
        const Vec3  ray_end(start_pos.x + dir_x * ray_length, start_pos.y + dir_y * ray_length, start_pos.z);

        // ---- THE PREFILTER WAS SWEEPING A WHOLE HULL PAST THE END OF ITS OWN REACH ----
        // "wall reach" is meant to be how far past the hull a face still counts, and every LINE
        // here honours that (they start at the centre, so ray_length carries the support). The
        // prefilter is a HULL trace, and a hull trace already occupies the support before it moves
        // an inch -- sweeping it ray_length reaches support + extra_reach + lookahead PAST THE
        // FACE, i.e. 24u on an axis and 34u on a diagonal of pure over-reach that the slider does
        // not appear in. At the slider's floor (8) the extra 24-34 is three to four times the
        // whole setting, which is exactly "the reach slider does nothing and it grabs walls I am
        // nowhere near": with the centre column empty (a wall off to one side) the prefilter is
        // the ONLY thing that can answer, and it answers with that far face, and AirStuck drives
        // at every face the scan returns. Sweep it the reach and nothing else -- then the hull,
        // the column and the lines all mean the same thing by "reach".
        const float pre_length = fmaxf(extra_reach + lookahead, 0.f);
        const Vec3  pre_end(start_pos.x + dir_x * pre_length, start_pos.y + dir_y * pre_length,
                            start_pos.z);

        GameTrace_t pre{};
        TraceHull(start_pos, pre_end, scan_mins, maxs, MASK_PLAYERSOLID, &fil, &pre);
        if (!pre.DidHit())
            continue;

        // THE COLUMN SWEEP, and it is the whole SHORT-LEDGE fix. The z grid samples lines
        // g_tbScanStep apart (the user's own cfg runs 8), so any lip shorter than the step can
        // sit ENTIRELY BETWEEN two lines and the scan never sees it -- the only rescue was the
        // full-width hull prefilter below, whose nearest hit is any face out to 24u either side.
        // This is one box sweep with ZERO x/y extents over the hull's full z span: the player's
        // own centre column, swept along dir, at byte-identical geometry to the lines. It crosses
        // every face the lines cross AND every face between them. Three wins at once:
        //   miss  -> the whole column is empty, skip ~11-73 line traces
        //   hit   -> it IS the nearest facing face in the column, so it is the answer (short
        //            ledges included) and it bounds how near any line could come
        //   bound -> the line loop stops the instant the answers it still owes are at the bound
        GameTrace_t seg{};
        TraceHull(start_pos, ray_end, Vec3(0.f, 0.f, scan_mins.z), Vec3(0.f, 0.f, maxs.z),
                  MASK_PLAYERSOLID, &fil, &seg);
        const bool  column_empty = !seg.DidHit();
        const float column_bound = seg.fraction + 1e-4f;   // float slack

        GameTrace_t best_match{}, best_any{};
        best_match.fraction = best_any.fraction = 1.f;
        bool have_match = false, have_any = false;

        // `any` ignores the plane lock (= what the unlocked retry would pick here), `match`
        // obeys it. Two answers per direction, one set of traces
        const auto consider = [&](const GameTrace_t& tr, bool to_any, bool to_match)
        {
            if (tr.fraction >= 1.f) return;
            if (fabsf(tr.plane.normal.z) > max_normal_z) return;
            if (tr.plane.normal.x * dir_x + tr.plane.normal.y * dir_y > -0.1f) return;
            if (to_any && (!have_any || tr.fraction < best_any.fraction))
                { best_any = tr; have_any = true; }
            if (to_match && plane_ok(tr.plane)
                && (!have_match || tr.fraction < best_match.fraction))
                { best_match = tr; have_match = true; }
        };
        const auto column_settled = [&]() -> bool
        {
            if (!have_any || best_any.fraction > column_bound) return false;
            if (pRequireNormal && (!have_match || best_match.fraction > column_bound)) return false;
            return true;
        };

        if (!column_empty)
            consider(seg, true, true);

        for (float z = start_pos.z + scan_mins.z; !column_empty && z <= start_pos.z + maxs.z; z += z_step)
        {
            if (column_settled()) break;
            GameTrace_t tr{};
            TraceLine(Vec3(start_pos.x, start_pos.y, z), Vec3(ray_end.x, ray_end.y, z),
                      MASK_PLAYERSOLID, &fil, &tr);
            consider(tr, true, true);
        }

        // hull rescue, unchanged in intent: a face neither the centre column nor the lines reach
        // (a lip off to one side). Feeds only the answer that came up empty
        if (!have_any || !have_match)
            consider(pre, !have_any, !have_match);

        const bool want_match = pRequireNormal != nullptr;
        if (want_match ? have_match : have_any)
        {
            out_trace = want_match ? best_match : best_any;
            out_angle = a;
            if (pOutFallback) *pOutFallback = fallback;
            return true;
        }
        if (!fallback.valid && have_any)
            { fallback.trace = best_any; fallback.angle = a; fallback.valid = true; }
    }
    if (pOutFallback) *pOutFallback = fallback;
    return false;
}

// distance hull face -> wall along -normal at wall-hit height (fraction*32 minus
// half-extent projection). ~0 = flush against the band. call while entity holds
// PREDICTED state (right after Pred_Simulate)
// how many sims this tick had to fall back to plane math (diag: supf=)
static int s_tbSupportFallback = 0;

// pOrigin = measure a state the entity is NOT standing in (the analytic probe's end position,
// which was never simulated). Null = read the live predicted origin, exactly as before
static float MV_SupportDistance(void* pLocal, const GameTrace_t& wall, const Vec3* pOrigin = nullptr)
{
    const Vec3 origin = pOrigin ? *pOrigin : EntV3(pLocal, g_offOrigin);
    const Vec3 flat(wall.plane.normal.x, wall.plane.normal.y, 0.f);

    // THE 32 WAS AN AXIS-ALIGNED HARDCODE, and it is the "wall not aligned with the
    // player bounding box" bug. The hull's support along a wall normal is |nx|*24 + |ny|*24
    // (TB_WallSupportDistance): 24.0 when the normal lies on x or y, 33.94 at 45 deg. 32 is
    // exactly 24 + 8 = the AXIS-ALIGNED half plus the same 8u margin every other reach here
    // uses -- so on a diagonal wall the ray stopped 1.94u SHORT of the face it was sent to
    // measure. Worse, the ray was built as normal * -32 and then flattened by the TraceLine
    // (both endpoints sit at wall.endpos.z), so its real xy length was 32 * |normal.xy| --
    // shorter still on anything that is not perfectly vertical. Either way the face is out of
    // reach: fraction comes back 1.0 and every diagonal tick falls to the plane-math branch,
    // or some nearer brush answers instead and the caller gets a support distance measured
    // against the wrong surface.
    // half + 8 reproduces 32 EXACTLY on an axis-aligned vertical wall (24 + 8), so the whole
    // axis-aligned population -- every minsup=-0.031 in the log -- is arithmetically unchanged.
    // Only off-axis normals move, which is the population that is broken
    const float flat_len = flat.Length2D();
    if (flat_len < 1e-4f) return 0.f;              // face has no horizontal normal at all
    const Vec3  dir = flat * (1.f / flat_len);     // unit, in the plane the TraceLine works in
    // and an earlier fix corrected the RAY but left the SUPPORT measured off the same broken vector.
    // TB_WallSupportDistance is |n.x|*hx + |n.y|*hy + |n.z|*hz, which is the box's support only
    // for a UNIT n; it was handed `flat`, the normal with z zeroed and NOT renormalized, whose
    // length is sqrt(1-nz^2). So on a tilted face `half` came back short by that factor -- 6% at
    // nz 0.35, 24% at nz 0.65 (AirStuck's limit) = up to 5.8u -- and the returned support is
    // `hit_distance - half`, so a hull sitting FLUSH on a slanted wall reported itself 5.8u off
    // it. Nothing that gates on flush (WC's creep < 0.2, TB's minsup, AS's align) could ever fire
    // there, so the feature kept pressing in forever: the "stuck on slanted walls" half.
    // dir is the unit vector the ray already uses, and on a vertical wall (nz 0) dir == flat, so
    // every axis-aligned and every diagonal-yaw number in the logs is unchanged to the bit
    const float half = TB_WallSupportDistance(pLocal, dir);
    const float ray_len = half + 8.f;

    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceLine(Vec3(origin.x, origin.y, wall.endpos.z),
              Vec3(origin.x - dir.x * ray_len, origin.y - dir.y * ray_len, wall.endpos.z),
              MASK_ALL_MV, &fil, &tr);
    if (tr.fraction < 1.f)
        return tr.fraction * ray_len - half;

    // THE 8.000 SENTINEL, and C:\botox-css\debug.log is nothing but it: one center LINE at
    // wall.endpos.z, 32u long. Miss it and this returned 32 - half = 8.000 EXACTLY on a TF2 hull
    // (half 24) -- every tick of every ride in the log, pin ticks included. The line misses on
    // real geometry the detect DOES see: a hull-prefilter hit (endpos.z is the feet, the face is
    // a lip further up) and any face the perpendicular from the center does not reach at that
    // one z. 8.000 is > every support gate in TB, so the CREEP -- "THE hold, keeps the eps band
    // alive so the next seam tick re-pins" -- could never fire, every no-pin tick fell through to
    // the bare drive-in (pbr=6, drv climbing 1..11), the hull bled ~1.3u of z per pin cycle and
    // the ride died the moment one cycle missed its seam.
    // The plane is KNOWN, so measure against it: signed distance origin -> plane, minus the same
    // hull half-extent. Identical quantity, no ray to miss. No "sticks to nothing" risk -- the
    // detect already proved this brush is inside the hull sweep before the sweep ever runs
    ++s_tbSupportFallback;
    const float plane_dist = origin.x * wall.plane.normal.x + origin.y * wall.plane.normal.y
                           + origin.z * wall.plane.normal.z - wall.plane.dist;
    // this branch measures along the FULL 3D normal (plane_dist carries the z term), so
    // its support has to be the 3D one too; `half` above is the horizontal ray's, which drops the
    // |n.z|*hz column. Identical on a vertical face (n.z 0), so no logged number moves
    return plane_dist - TB_WallSupportDistance(pLocal, wall.plane.normal);
}


// movement input projected into world space points outward along the wall normal?
static bool TB_SteeringAwayFromWall(const UserCmd_t* pCmd, const Vec3& vWallNormal,
                                    float flForwardMove, float flSideMove, float flMinDot = 0.3f)
{
    if (flForwardMove == 0.f && flSideMove == 0.f)
        return false;

    Vec3 vForward, vRight;
    AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vForward, &vRight, nullptr);

    Vec3 vWish = vForward * flForwardMove + vRight * flSideMove;
    vWish.z = 0.f;
    const float flWishLen = vWish.Length2D();
    if (flWishLen < 1.f)
        return false;
    vWish *= (1.f / flWishLen);

    Vec3 vNormal = vWallNormal;
    vNormal.z = 0.f;
    const float flNormLen = vNormal.Length2D();
    if (flNormLen < 0.001f)
        return false;
    vNormal *= (1.f / flNormLen);

    return (vWish.x * vNormal.x + vWish.y * vNormal.y) > flMinDot;
}


// ==== feature state ====
static bool s_rawJumpHeld = false;   // real IN_JUMP before AutoJump strips it
// the WHOLE cmd as the player sent it, captured on the same line as s_rawJumpHeld and
// for the same reason: by the time RunPost is reached, AutoJump/PS assist/WallClimb have all
// written buttons, so pCmd there answers "what will the server run", never "what did he press".
// The route calculator's live recorder needs the second question and nothing else does.
static int  s_rawButtons = 0;
static float s_rawFwd = 0.f, s_rawSide = 0.f;   // botox m_user_*_move_raw: the keys before any feature (wall climb's "you leave")
// IN_JUMP of the cmd we actually SENT last tick = the engine's m_nOldButtons bit, mirrored.
// CheckJumpButton refuses a jump while that bit is set (tf_gamemovement.cpp:1235) and ONLY a
// jumpless cmd clears it (gamemovement.cpp:1441), so this is the one honest answer to "will
// the engine take a press this tick". Written at the very end of Movement_RunImpl, after every
// feature has authored the cmd -- reading pCmd mid-pipeline would answer for a half-built cmd
static bool s_prevCmdJump = false;
static bool s_prevCmdDuck = false;   // last cmd's final IN_DUCK = m_nOldButtons for the route calc
static bool s_edgeJumpGround = false;
// edge jump injected IN_JUMP on THIS cmd, and the engine will honour it (fresh press
// off a grounded start). Set post-pred by EdgeJump, read by MiniJump three lines later. Needed
// because that press lands AFTER Pred_Start, so the predicted velocity mini jump reads is the
// walk-off (vz ~ -6), never the launch (vz 271) -- see the takeoff gate in MiniJump
static bool s_edgeJumpFired = false;
static bool s_strafeWeaveLeft = false;
static int  s_prePredFlags = 0;      // m_fFlags captured before prediction (long/mini jump)
// mini jump decides its duck for THIS cmd, and the engine runs Duck() at the START of it --
// so the state that decides the press is the one BEFORE prediction, not the predicted end
static bool s_prePredDucking = false;   // m_Local.m_bDucking at cmd start
static Vec3 s_prePredOrigin = {};       // feet at cmd start
static Vec3 s_prePredVel = {};       // velocity before prediction (pixel surf ride detect, mini jump)
// the REAL tickcount for this cmd, sampled with the rest of the pre-pred snapshot.
// g_globalVars->tickcount is rewritten by Pred_Start's sims, so a feature running INSIDE the
// predicted window reads a tick one ahead of the one Movement_RunImpl's tail sees after
// Pred_End. an earlier build's MJ: lines were stamped with the inflated tick and MJT: with the restored
// one, so `REBOUND after 5 ticks` and `MJT: t=4` were the same cmd under two different names --
// an hour of log reading to notice. Both now stamp from here.
static int  s_prePredTick = 0;


// ==== features (ref CMisc ports) ====

// Bunnyhop -- hold jump, re-press only on the grounded non-duck landing tick.
static void AutoJump(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_bhop) return; // plain on/off, no bind (user pick)

    static bool bStaticJump = false, bStaticGrounded = false, bLastAttempted = false;
    const bool bLastJump = bStaticJump, bLastGrounded = bStaticGrounded;
    const bool bCurJump = bStaticJump = (pCmd->buttons & IN_JUMP) != 0;
    const bool bCurGrounded = bStaticGrounded = EntGround(pLocal);
    // botox: bunny_hop is skipped while the jump bug key is held -- jump_bug owns IN_JUMP then
    if (On(g_jumpbug, g_jumpbugKey, g_jumpbugMode)) { bLastAttempted = bCurJump; return; }

    if (bCurJump && bLastJump && (bCurGrounded ? !EntDucking(pLocal) : true))
    {
        const bool bLandingEdge = bCurGrounded && !bLastGrounded;
        if (!bLandingEdge)
            pCmd->buttons &= ~IN_JUMP;
        if (!(pCmd->buttons & IN_JUMP) && bCurGrounded && !bLastAttempted)
            pCmd->buttons |= IN_JUMP;
        // botox bunny_hop(): roll "success rate" (m_bunny_hop_chance, def 100) once per landing,
        // AFTER the fallback above decides -- a miss vetoes this hop's press (movement.cpp:620
        // `rand() % 100 >= chance -> strip jump`); next tick's landing edge rolls again.
        if (bLandingEdge && (pCmd->buttons & IN_JUMP)
            && (std::rand() % 100) >= std::clamp(g_bhopChance, 0, 100))
            pCmd->buttons &= ~IN_JUMP;
    }
    // fake wall jump removed with fake movement (11-03/11-04): no botox csgo equivalent
    bLastAttempted = (pCmd->buttons & IN_JUMP) != 0;
}

// ---- pixel surf ride state (declared here, written by PixelSurf / PS_AssistTick further down) ----
// These used to live down with the rest of the per-cmd state, below every pre-prediction feature.
// AutoJumpbug is pre-pred and needs them, so they moved up: a plain bool has no init order to
// break. Read from a pre-pred feature they hold LAST cmd's value (PixelSurf runs post-pred), which
// is the right question anyway -- a ride that was live a tick ago is a ride not to stomp
static bool s_shouldPixelSurf = false;   // PixelSurf ride latched (ducked OR standing)
static bool s_psShouldDuck = false;      // ...and this ride needs the crouch (false = standing surf)
static bool s_psAirDuckHeld = false;     // the surf duck is pressed and holds until the feet land
static bool s_psRidingNow = false;       // 2-tick sustained surf ride (ref m_bPixelSurfingNow; PS assist gates on it)
// ...and what that ride LOOKS like, for the indicator only (ps_surf.h). s_psRidingNow is the vz
// signature, which an air stuck / texture bug / wedged hull share -- PixelSurf measures the shape
// of the thing holding us into this, and the publish at the tail asks ps::IsSurf once
static ps::SurfEvidence s_psEv{};
// were PixelSurf function statics; file scope so the route calculator can read a live
// ride latch (s_pixelSurfTicks = pin + 16) and the ride-velocity memory for a mid-air start
static bool s_prevSurfVel = false;
static int  s_pixelSurfTicks = 0;
static bool s_psAssistHopActive = false; // assist hop fired/pending -- arc is sim-validated, AirStuck must yield
// botox `wall_detected`: AutoAlign's flat-wall find, or pixel surf's own 16-ray probe when
// AutoAlign is off (pixelsurf.inl PsWallDetectFallback). Pixel surf's hunt gate reads it
static bool s_aaWallDetected = false;
// auto align "hold pixel surfs" wrote this cmd's press (autoalign.inl). Pixel surf runs last and its own
// hold keeps the most speed -- the along-face drift that dropped the rides -- so it leaves the cmd alone
static bool s_aaHolding = false;
// the assist's own catch crouch, held until the feet land. Declared here (not in
// psassist.inl, which is #included far below) so WallClimb can yield to it -- see PS_AssistTick
static bool s_psAssistDuckHeld = false;
// the assist is reproducing PixelFinder's into-wall press at the seam. Declared up here
// for the same reason as the crouch -- AutoAlign runs after PS_AssistTick and writes the same
// two fields, so it has to be able to yield to it
static bool s_psAssistPressIn = false;

// ==== the live hull as a routesim state ======================================================
// One reader for every feature that wants to ask routesim.h a question about the CURRENT hull.
// It is the state the engine will start the NEXT simulated tick from, so it is read
// pre-prediction: m_nOldButtons is last cmd's committed pair (s_prevCmdDuck/Jump), not a
// half-built one. routecalc.inl's RcReadState is this function.
static rs::State MV_ReadSimState(void* pLocal, int tickNow)
{
    rs::State s;
    s.z        = EntV3(pLocal, g_offOrigin).z;
    s.vz       = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity).z : 0.f;
    const int fl = EntI(pLocal, g_offFlags);
    s.ground   = (fl & FL_ONGROUND) != 0;
    s.fDuck    = (fl & FL_DUCKING) != 0;
    s.ducking  = g_offDucking > 0 && EntB(pLocal, g_offDucking) != 0;
    s.ducked   = g_offDucked  > 0 && EntB(pLocal, g_offDucked)  != 0;
    s.duckTime = g_offDuckTime > 0 ? EntF(pLocal, g_offDuckTime) : 0.f;
    s.airDucked = g_offAirDucked > 0 ? EntI(pLocal, g_offAirDucked) : 0;
    s.stamina  = g_offStamina > 0 ? EntF(pLocal, g_offStamina) : 0.f;   // CCSPlayer::m_flStamina, ms (predicted)
    s.oldDuck  = s_prevCmdDuck;              // m_nOldButtons: what the last cmd actually sent
    s.tick     = 0;
    // m_flDuckTimer is a curtime: turn "strips until" into the release tick that would give it
    if (g_offDuckTimer > 0 && g_globalVars)
    {
        const float flNow  = TickInterval() * float(tickNow);
        const float flLeft = EntF(pLocal, g_offDuckTimer) - flNow;
        if (flLeft > 0.f)
            s.releaseTick = (int)ceilf(flLeft / TickInterval()) - (int)lroundf(0.3f / TickInterval());
    }
    return s;
}

// the engine knobs routesim.h integrates against, read live off the cvars + this hull
static rs::Params MV_SimParams(void* pLocal)
{
    rs::Params p;
    p.dt      = TickInterval();
    p.gravity = CvarFloat("sv_gravity", 800.f);
    p.maxVel  = CvarFloat("sv_maxvelocity", 3500.f);
    p.clampAirDucks  = CvarFloat("tf_clamp_airducks", 1.f) != 0.f;
    p.duckTimerTicks = (int)lroundf(0.3f / p.dt);
    if (g_offModelScale > 0)
    {
        const float sc = EntF(pLocal, g_offModelScale);
        if (sc > 0.1f && sc < 10.f) p.scale = sc;
    }
    return p;
}

// ==== JUMP BUG (rewrite 11-07, straight off css-community cs_gamemovement.cpp) ===============
// One CSS tick: PlayerMove -> Duck() (:860) -> FullWalkMove { StartGravity, CheckJumpButton,
// AirMove, CategorizePosition } (gamemovement.cpp:4611 / 2025). sv_optimizedmovement skips the
// start-of-tick CategorizePosition for a walking player (:4583), so "on ground" at tick start is
// whatever the LAST tick ended with.
// The bug: a DUCKED hull falling, duck released + jump pressed on the tick its feet sit ~8.5..10.5u
// over the floor. Duck() -> FinishUnDuck in the air drops the origin 8.5 (:800) and recategorizes
// (:814): the 2u ground trace now hits, SetGroundEntity zeroes vz. CheckJumpButton then runs on that
// fresh ground (:634) and jumps (no m_nOldButtons IN_JUMP, :640). The tick ENDS airborne: no landing
// tick, no CheckFalling damage, no friction. CanUnduck (:750) refuses if the 8.5 drop is blocked,
// which is the lower edge of the window; the 2u trace is the upper one.
// No band model: fly "duck from now, fire on tick k" on the engine from START for k = 0..H and take
// the earliest k that ends airborne with vz > 0. k = 0 fires now (only possible already ducked),
// k > 0 = duck this cmd. Nothing proves out = the move is not touched; the landing hops instead
// (11-07 "sometimes not jumping": the window is a fixed lattice of tick-start heights, the duck only
// shifts it by a constant 8.5, so a fall that drops > 2u a tick steps over it about 1 - 2/drop of the time).
static bool s_jumpbugHit = false;   // the fire tick went out this cmd (indicator holds it 0.2 s)

static constexpr int k_jbHorizon = 6;   // ticks; past this the duck would ride too long for nothing

// earliest fire tick for this move from START, -1 = none. Each k re-flies its duck prefix: a
// prefix tick that lands means every later k lands too.
static int JbEarliestFire(void* pLocal, const UserCmd_t& in, int nBase, float fwd, float side, int horizon)
{
    UserCmd_t duck = in, fire = in;
    duck.forwardmove = fire.forwardmove = fwd;
    duck.sidemove    = fire.sidemove    = side;
    duck.buttons = (in.buttons | IN_DUCK) & ~IN_JUMP;   // jump released on every wait tick: the fire needs a fresh press
    fire.buttons = (in.buttons & ~IN_DUCK) | IN_JUMP;
    for (int k = 0; k <= horizon; ++k)
    {
        if (SimsLeft() < k + 1 || TbBudgetExpired()) return -1;
        RestoreToFrame(nBase - 1);
        for (int i = 0; i < k; ++i)
        {
            Pred_Simulate(pLocal, &duck);
            if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
                return -1;
        }
        Pred_Simulate(pLocal, &fire);
        if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK
            && EntV3(pLocal, g_offVelocity).z > 0.f)
            return k;
    }
    return -1;
}

// post-prediction, right after EdgeJump. Leaves the entity at the predicted END of the cmd it sends.
static bool JumpBugKangarooYield();
static bool  s_jbAir = false;          // key held through an airborne START: the next ground START hops
static bool  s_jbSawWindow = false;    // some search this fall found a fire tick (log)
static float s_jbFallVz = 0.f;
static void JumpBug(void* pLocal, UserCmd_t* pCmd)
{
    static bool  s_aOn = false;           // height assist found a move: keep flying it, don't re-search
    static float s_aFwd = 0.f, s_aSide = 0.f;
    static int   s_lastK = -2;            // log on change only
    s_jumpbugHit = false;
    const auto idle = [&] { s_aOn = false; s_lastK = -2; };

    if (!On(g_jumpbug, g_jumpbugKey, g_jumpbugMode) || !s_predicted || g_offFlags <= 0 || g_offVelocity <= 0
        || g_offOrigin <= 0 || !g_engineTrace)
        { idle(); s_jbAir = false; return; }
    if (EntMoveType(pLocal) != MOVETYPE_WALK) { idle(); s_jbAir = false; return; }
    if (!s_rawJumpHeld) { idle(); s_jbAir = false; return; }   // jb only while YOU hold space
    // A fall whose tick-START feet step over the 2u window (8.5..10.5 ducked, see above) cannot jump bug
    // at all: per-tick drop > 2u = most falls past ~140 u/s. Bunny hop stands down while this key is held
    // (AutoJump), so that miss used to be a dead landing. Landing START = jump now: CheckJumpButton runs on
    // that ground (:634) and the landing cmd went out without IN_JUMP (stripped below), so it is a fresh press
    if (s_prePredFlags & FL_ONGROUND)
    {
        if (s_jbAir)
        {
            pCmd->buttons |= IN_JUMP;
            const int nBase = PredCommandsPredicted();
            RestoreToFrame(nBase - 1);
            Pred_Simulate(pLocal, pCmd);   // post features read this cmd's END
            DebugLog("JB: no window, hop on landing z=%.3f fell=%.1f window=%d", s_prePredOrigin.z, s_jbFallVz, s_jbSawWindow ? 1 : 0);
        }
        s_jbAir = false; s_jbSawWindow = false;
        idle();
        return;
    }
    s_jbAir = true;
    s_jbFallVz = s_prePredVel.z;
    pCmd->buttons &= ~IN_JUMP;   // the key owns jump in the air: the landing cmd must not hold it
    // only a falling, walking, airborne START can jump bug
    if (s_prePredVel.z > 0.f)
        { idle(); return; }
    if (g_jbYieldSurf && (s_psRidingNow || s_shouldPixelSurf || s_psAssistHopActive)) { idle(); return; }
    if (g_jumpbugKangaroo && JumpBugKangarooYield()) { idle(); return; }   // an edge bug plan owns the landing
    if ((s_rawButtons & IN_DUCK) && !g_jumpbugCrouch) { idle(); return; }   // your own crouch, not ours to release

    // floor in reach? one hull sweep along the START fall chord over the horizon, +12 for the
    // 8.5 window and the 2u ground trace. Nothing under you = no sims this cmd
    const int   horizon = g_jumpbugAssist ? std::clamp(g_jumpbugAssistTicks, 1, 8) + 1 : k_jbHorizon;
    const float dt = TickInterval(), t = dt * (float)horizon;
    const float g  = CvarFloat("sv_gravity", 800.f);
    const bool  ducked = (s_prePredFlags & FL_DUCKING) != 0;
    const Vec3  mins(-16.f, -16.f, 0.f), maxs(16.f, 16.f, ducked ? cssm::k_hullDuck : cssm::k_hullStand);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t reach{};
    const Vec3 from = s_prePredOrigin;
    TraceHull(from, from + Vec3(s_prePredVel.x * t, s_prePredVel.y * t, s_prePredVel.z * t - 0.5f * g * t * t - 12.f),
              mins, maxs, MASK_PLAYERSOLID, &filter, &reach);
    if (!reach.DidHit() || reach.startsolid) { idle(); return; }

    const int nBase = PredCommandsPredicted();
    float fwd = pCmd->forwardmove, side = pCmd->sidemove;
    int k = -1;
    if (s_aOn)
    {
        k = JbEarliestFire(pLocal, *pCmd, nBase, s_aFwd, s_aSide, horizon);
        if (k >= 0) { fwd = s_aFwd; side = s_aSide; }
        else s_aOn = false;
    }
    if (k < 0)
        k = JbEarliestFire(pLocal, *pCmd, nBase, pCmd->forwardmove, pCmd->sidemove, horizon);
    // height assist: your move misses the window -> the least bending move that hits it. Tiers by
    // bend: a 10 u/s lean in 8 directions, no wish, a full 450 wish in 8 directions
    if (k < 0 && g_jumpbugAssist)
    {
        static constexpr float kTiers[] = { 10.f, 0.f, 450.f };
        for (const float m : kTiers)
        {
            if (k >= 0) break;
            if (m == 0.f && pCmd->forwardmove == 0.f && pCmd->sidemove == 0.f) continue;
            for (int d = 0; d < (m > 0.f ? 8 : 1) && k < 0; ++d)
            {
                float f = 0.f, s = 0.f;
                cssm::WorldToCmd(pCmd->viewangles.y, cosf(DEG2RADF(d * 45.f)) * m, sinf(DEG2RADF(d * 45.f)) * m, f, s);
                k = JbEarliestFire(pLocal, *pCmd, nBase, f, s, horizon);
                if (k >= 0) { fwd = f; side = s; s_aOn = true; s_aFwd = f; s_aSide = s; }
            }
        }
    }

    if (k >= 0)
    {
        s_jbSawWindow = true;
        pCmd->forwardmove = fwd;
        pCmd->sidemove    = side;
        if (k == 0) { pCmd->buttons = (pCmd->buttons & ~IN_DUCK) | IN_JUMP; s_jumpbugHit = true; s_aOn = false; }
        else          pCmd->buttons = (pCmd->buttons | IN_DUCK) & ~IN_JUMP;
        if (k != s_lastK)
            DebugLog("JB: %s k=%d z=%.3f vz=%.1f ducked=%d assist=%d", k == 0 ? "FIRE" : "duck", k,
                     s_prePredOrigin.z, s_prePredVel.z, ducked ? 1 : 0, s_aOn ? 1 : 0);
    }
    else if (s_lastK >= 0)
        DebugLog("JB: lost plan z=%.3f vz=%.1f", s_prePredOrigin.z, s_prePredVel.z);
    s_lastK = k;

    // every post feature after this reads the entity as this cmd's END
    RestoreToFrame(nBase - 1);
    Pred_Simulate(pLocal, pCmd);
}

// ---- tick-time window for sims outside Pred_Start (post-Pred_End edge bug) ----
// Outside Pred_Start the engine's gpGlobals hold the real FRAME time, and ProcessMovement
// integrates against gpGlobals->frametime: a sim run without this moves by a frame, not a tick.
struct EbTimeGuard
{
    int t; float c, f;
    explicit EbTimeGuard(int iTickBase)
    {
        t = g_globalVars->tickcount; c = g_globalVars->curtime; f = g_globalVars->frametime;
        g_globalVars->tickcount = iTickBase;
        g_globalVars->curtime = TickInterval() * iTickBase;
        g_globalVars->frametime =
            reinterpret_cast<unsigned char*>(g_prediction)[kPredOff_EnginePaused] ? 0.f : TickInterval();
    }
    ~EbTimeGuard() { g_globalVars->tickcount = t; g_globalVars->curtime = c; g_globalVars->frametime = f; }
};

// Auto strafe -- steer-to-look perfect strafe, silent wish angle via CorrectMovement.
static void AutoStrafe(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_autoStrafe, g_autoStrafeKey, g_autoStrafeMode)) return;
    if (g_offVelocity <= 0 || g_offFlags <= 0) return;

    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP || mt == MOVETYPE_LADDER) return;   // ladder: no AirAccelerate, strafing does nothing
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return;

    const Vec3 vVelocity = EntV3(pLocal, g_offVelocity);
    const float flSpeed = vVelocity.Length2D();
    if (flSpeed <= 1.f) return;

    const bool bBack    = (pCmd->buttons & IN_BACK) != 0;
    const bool bForward = (pCmd->buttons & IN_FORWARD) != 0;
    const bool bRight   = (pCmd->buttons & IN_MOVERIGHT) != 0;
    const bool bLeft    = (pCmd->buttons & IN_MOVELEFT) != 0;

    // botox "type" (m_auto_strafe_type, variables.h:1112): 0 standard (silent, target = view
    // yaw, no key/mouse offset -- this function's own "steer-to-look" design), 1 WASD (target
    // offset from the held movement keys, persisted on release if "strafe in last pressed
    // direction"), 2 circle/mouse (persisted turn offset, "strafe in last mouse direction").
    static float s_asKeyOff = 0.f, s_asMouseOff = 0.f;
    float flAngleOffset = 0.f;
    if (g_autoStrafeType == 1)
    {
        float flKeyOffset = 0.f;
        if (bBack)      { flKeyOffset = -180.f; if (bLeft) flKeyOffset -= 45.f; else if (bRight) flKeyOffset += 45.f; }
        else if (bLeft) { flKeyOffset = 90.f;   if (bForward) flKeyOffset -= 45.f; }
        else if (bRight){ flKeyOffset = -90.f;  if (bForward) flKeyOffset += 45.f; }
        if (bBack || bForward || bLeft || bRight) s_asKeyOff = flKeyOffset;
        else if (!g_autoStrafeLastKeys) s_asKeyOff = 0.f;
        flAngleOffset = s_asKeyOff;
    }
    else if (g_autoStrafeType == 2)
    {
        // botox auto_strafe_mouse(): atan2(mx, -my) off the raw mouse delta (screen-space: +x
        // right, +y down, so "-my" points the angle up-screen = forward). CUserCmd already
        // carries mousedx/mousedy per cmd (movement.h UserCmd_t) -- no separate pre-view-update
        // steal hook needed, this just reads this cmd's own pair instead of botox's accumulated
        // between-cmds sum (CSS cmds run 1:1 with mouse samples here, nothing to accumulate).
        const float mx = (float)pCmd->mousedx, my = (float)pCmd->mousedy;
        if (mx != 0.f || my != 0.f)
            s_asMouseOff = RAD2DEGF(atan2f(mx, -my));
        else if (!g_autoStrafeLastMouse)
            s_asMouseOff = 0.f;
        flAngleOffset = s_asMouseOff;
    }

    // Unit A port: avoid wall collisions -- flip the strafe side if the heading it's about
    // to weave into is blocked within g_autoStrafeAvoidDist (botox movement.cpp avoid_walls).
    if (g_autoStrafeAvoidWalls && g_offOrigin > 0 && g_offMins > 0 && g_offMaxs > 0 && g_engineTrace)
    {
        const float flHeadYaw = NormalizeAngle(pCmd->viewangles.y + flAngleOffset);
        const float flHeadRad = DEG2RADF(flHeadYaw);
        const Vec3 vFrom = EntV3(pLocal, g_offOrigin);
        const Vec3 vTo = vFrom + Vec3(cosf(flHeadRad), sinf(flHeadRad), 0.f) * g_autoStrafeAvoidDist;
        TraceFilterWorldProps avoidFilter; avoidFilter.Setup(pLocal);
        GameTrace_t avoidTr{};
        TraceHull(vFrom, vTo, EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
                  MASK_PLAYERSOLID, &avoidFilter, &avoidTr);
        if (avoidTr.DidHit())
            flAngleOffset = NormalizeAngle(flAngleOffset + 180.f);
    }

    const Vec3 vOldAngles = pCmd->viewangles;
    const float flDesiredYaw = NormalizeAngle(vOldAngles.y + flAngleOffset);
    const float flVelYaw = RAD2DEGF(atan2f(vVelocity.y, vVelocity.x));
    const float flErr = NormalizeAngle(flVelYaw - flDesiredYaw);

    s_strafeWeaveLeft = flErr < 0.f;
    const bool bTurnLeft = s_strafeWeaveLeft;

    // ONE magnitude for both axes: the cos/sin below is a direction, and splitting it across
    // cl_forwardspeed and cl_sidespeed skews that direction the moment the two differ (the old
    // code did exactly that). The engine scales the pair down to m_flMaxspeed anyway.
    const float flMoveSpeed = (std::max)(CvarFloat("cl_sidespeed", 450.f), CvarFloat("cl_forwardspeed", 450.f));

    // the wish speed the ENGINE will use: AirMove clamps the move magnitude to m_flMaxSpeed
    // before AirAccelerate, and CheckParameters already pinned that to m_flMaxspeed. The old
    // code fed the raw 636 in and overstated accelspeed ~1.6x.
    const float flMaxSpeed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    const float flWishSpeed = (flMaxSpeed > 1.f && flMaxSpeed < flMoveSpeed) ? flMaxSpeed : flMoveSpeed;
    const airlean::Lean lean = airlean::Solve(flSpeed, flWishSpeed,
                                              CvarFloat("sv_airaccelerate", 10.f), TickInterval(), 30.f);

    // LEAN BACK WHILE THE HEADING IS STILL OFF. cosTurn is the furthest the wish can sit behind
    // perpendicular and still cost ZERO speed (airlean.h): at scout speed that is 8.6 deg/tick of
    // rotation against 4.3 at the pure gain angle, for free, because leaning back raises addspeed
    // so the full 60u of accelspeed lands instead of 30u. Fold back to cosGain as the error closes
    // -- under ~5 deg of heading error there is nothing left to steer, so the steady state is
    // the max-speed strafe it always was (full lean from 30 deg out).
    const float flLean = std::clamp((fabsf(flErr) - 5.f) / 25.f, 0.f, 1.f);
    const float flCosLean = lean.cosGain + (lean.cosTurn - lean.cosGain) * flLean;
    const float flSinLean = sqrtf(1.f - flCosLean * flCosLean > 0.f ? 1.f - flCosLean * flCosLean : 0.f);

    Vec3 vWishAngle = vOldAngles;
    vWishAngle.y = flVelYaw;
    pCmd->forwardmove = flMoveSpeed * flCosLean;
    pCmd->sidemove = (bTurnLeft ? -flMoveSpeed : flMoveSpeed) * flSinLean;

    CorrectMovement(pCmd, vWishAngle, vOldAngles);
    pCmd->viewangles = vOldAngles;
}

// Strafe optimizer -- perfects YOUR strafes: steers view yaw so the wish direction
// sits at the air-accel-optimal angle off velocity. Visible on purpose (real view moves).
//
// Old version had two faults, both fixed here:
//   1. BACKWARDS BROKE. It required forwardmove == 0, so S / S+A / W+A were dead, and it
//      inferred "backward" from a HARD 90 deg view-vs-velocity threshold that flipped the
//      target axis AND the sidemove sign at once -- one degree of view either side of that
//      line swapped the goal by 180 deg, which is the yank. The wish angle is now read from
//      the cmd's own move vector (same convention as AutoStrafe: wishYaw = viewYaw +
//      atan2(-sidemove, forwardmove)), so every key combo is one continuous case and there
//      is no threshold left to cross.
//   2. SNAPPY. gain% of the whole error was applied EVERY tick with no rate limit, so even a
//      small gain ate a big error in a few ticks, and roundf() to the mouse step threw away
//      every sub-step remainder instead of carrying it (drop, drop, drop, whole-step jump).
//      Now: per-tick step is capped by gain^2, and the leftover fraction is carried in
//      s_soResidual so low gain slews smoothly instead of stepping.
static void StrafeOptimizer(void* pLocal, UserCmd_t* pCmd)
{
    static float flPrevYaw = 0.f;      // post-correction yaw of last cmd -> this tick's raw mouse delta
    static float s_soResidual = 0.f;   // sub-mouse-step correction carried between ticks
    static float s_soCap = 0.f;        // per-tick step ceiling, ramps open on engage (see below)
    static bool  s_soActive = false;   // engaged last tick (residual is only valid while engaged)

    const float flYawNow = pCmd->viewangles.y;
    const float flMouseDelta = NormalizeAngle(flYawNow - flPrevYaw);   // + = user turning left
    flPrevYaw = flYawNow;

    auto Disengage = [&]() { s_soResidual = 0.f; s_soCap = 0.f; s_soActive = false; };

    if (!g_strafeOpt) { Disengage(); return; }                          // plain on/off, no bind (user pick)
    if (On(g_autoStrafe, g_autoStrafeKey, g_autoStrafeMode)) { Disengage(); return; }  // AutoStrafe wins
    if (g_offVelocity <= 0 || g_offFlags <= 0) { Disengage(); return; }

    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER) { Disengage(); return; }
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) { Disengage(); return; }

    const Vec3 vVelocity = EntV3(pLocal, g_offVelocity);
    const float flSpeed = vVelocity.Length2D();
    if (flSpeed <= g_strafeOptMinSpeed || flSpeed <= 1.f) { Disengage(); return; }

    // wish direction, view-relative. atan2(-side, fwd): A (side -450) -> +90 (left),
    // D -> -90, S -> 180, S+A -> +135. No move pressed = nothing to optimize.
    const float flFwd = pCmd->forwardmove, flSide = pCmd->sidemove;
    const float flWishMag = sqrtf(flFwd * flFwd + flSide * flSide);
    if (flWishMag < 1.f) { Disengage(); return; }
    const float flWishOffset = RAD2DEGF(atan2f(-flSide, flFwd));

    // m_yaw may be negative (inverted look, or flip world holding the sign): the step is its size,
    // and the mousedx written below carries the sign the engine will multiply back in.
    const float flYawStepSigned = CvarFloat("sensitivity", 1.f) * CvarFloat("m_yaw", 0.022f);
    const float flYawStep = fabsf(flYawStepSigned);
    if (flYawStep <= 0.f) { Disengage(); return; }

    // ideal angle between velocity and wish -- same physics AutoStrafe uses (airlean.h). In TF2
    // accelspeed (accel*wishspeed*tick) far exceeds the 30u add cap, so this lands on a flat
    // 90 deg (perpendicular wish = max gain); it is computed, not assumed, so odd
    // sv_airaccelerate / slowed-class cases lean in on their own. The wish magnitude is clamped
    // to m_flMaxspeed first, the way AirMove clamps it before AirAccelerate ever sees it.
    const float flMaxSpeed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    const float flWishSpeed = (flMaxSpeed > 1.f && flMaxSpeed < flWishMag) ? flMaxSpeed : flWishMag;
    const float flIdeal = RAD2DEGF(acosf(airlean::Solve(flSpeed, flWishSpeed,
        CvarFloat("sv_airaccelerate", 10.f), TickInterval(), 30.f).cosGain));

    const float flVelYaw = RAD2DEGF(atan2f(vVelocity.y, vVelocity.x));
    const float flWishYaw = NormalizeAngle(flYawNow + flWishOffset);
    const float flWishOffVel = NormalizeAngle(flWishYaw - flVelYaw);   // + = wish left of velocity

    // the user must already be turning, and the wish must lead on the side they are turning
    // -- that is the generalized form of the old "turning left AND holding A" pair, and it is
    // what keeps this from steering a straight-line accel (wish on velocity) sideways.
    if (fabsf(flMouseDelta) < 0.001f) { Disengage(); return; }
    const float flSideSign = flMouseDelta > 0.f ? 1.f : -1.f;
    if (flSideSign * flWishOffVel <= 0.f) { Disengage(); return; }

    // error = how far the wish is from the optimal lead. The old 75 deg bound was a CLIFF: at
    // 74 deg the full gain*err landed, at 76 nothing did. Falling backwards is where that bit --
    // S held while the view swings puts the wish 40-60 deg off the lead, every tick, and the view
    // got dragged most of the way there in one tick. Window is 60 deg now, and the take fades
    // with the SQUARE of how far out the wish is, so only a wish already near the lead (<=35 deg)
    // is corrected at full gain and the edge is a fade, not a step.
    const float flErr = NormalizeAngle(flSideSign * flIdeal - flWishOffVel);
    const float flErrAbs = fabsf(flErr);
    if (flErrAbs > 60.f) { Disengage(); return; }
    const float flTaper = std::clamp((60.f - flErrAbs) / 25.f, 0.f, 1.f);

    // and fade in over the 60 u/s above the speed floor. Near the floor the 2D velocity is a
    // small remainder of a mostly-vertical fall, so its yaw (and with it the lead this chases)
    // swings tens of degrees per tick -- that is the other half of the falling-backwards spin.
    const float flSpeedFade = std::clamp((flSpeed - (std::max)(g_strafeOptMinSpeed, 1.f)) / 60.f, 0.f, 1.f);

    const float flGain = std::clamp(g_strafeOptGain / 100.f, 0.f, 1.f);
    if (flGain <= 0.f) { Disengage(); return; }

    // gain^2 rate cap: 100% keeps the old instant snap (180 deg/tick is no cap at all),
    // 50% -> 45 deg/tick, 25% -> 11 deg/tick, 10% -> 1.8 deg/tick. Squared so the low half
    // of the slider is where the smoothing actually lives.
    const float flMaxStep = 180.f * flGain * flGain;

    if (!s_soActive) { s_soResidual = 0.f; s_soCap = 0.f; }
    s_soActive = true;

    // ATTACK RAMP. The old code put the WHOLE gain*err on the FIRST tick it engaged -- that one
    // tick is the snap, and every re-engage paid it again. The ceiling now starts at zero and
    // opens by gain^2 deg per tick, so a 40 deg error at 100% gain goes 6.5 / 13 / 19.5 / 1
    // instead of 40 / 0 and is still closed inside 4 ticks. Ramping the CEILING and not the
    // value is deliberate: the step itself stays proportional to the CURRENT error, so this
    // cannot overshoot and ring the way smoothing the applied angle does (measured: an EMA in
    // this loop settles in 5 ticks but swings 4 deg past the lead on the way).
    s_soCap = (std::min)(flMaxStep, s_soCap + 0.5f + 6.f * flGain * flGain);

    float flWant = flErr * flGain * flTaper * flTaper * flSpeedFade;
    flWant = std::clamp(flWant, -s_soCap, s_soCap);
    if (flErrAbs < 0.5f) flWant = 0.f;        // deadzone: stop hunting the last half degree
    if (flWant == 0.f) return;

    // quantize to whole mouse steps, CARRY the remainder (this is the smoothness fix)
    s_soResidual += flWant;
    const float flSteps = truncf(s_soResidual / flYawStep);
    if (flSteps == 0.f) return;

    const float flApplied = flSteps * flYawStep;
    s_soResidual -= flApplied;

    pCmd->viewangles.y = NormalizeAngle(flYawNow + flApplied);
    flPrevYaw = pCmd->viewangles.y;
    // engine: yaw -= m_yaw*sens*mousedx, so mousedx = -applied / (m_yaw*sens)
    pCmd->mousedx = (short)std::clamp(flYawStepSigned < 0.f ? flSteps : -flSteps, -32767.f, 32767.f);
    EngineSetViewAngles(pCmd->viewangles);
}

// Edge jump active: own enable OR piggybacked on mini/long jump ("edgejump on
// mj/lj" -- runs whenever that jump type is active, edge jump itself may be off).
static bool EdgeJumpOn()
{
    return On(g_edgeJump, g_edgeJumpKey, g_edgeJumpMode)
        || (g_miniJumpEdge && On(g_miniJump, g_miniJumpKey, g_miniJumpMode))
        || (g_longJumpEdge && On(g_longJump, g_longJumpKey, g_longJumpMode));
}

// Edge jump -- pre: remember real ground; post (predicted state): about to leave
// ground this cmd -> press jump. Needs engine prediction.
static void EdgeJump(void* pLocal, UserCmd_t* pCmd, bool bPost)
{
    if (!EdgeJumpOn()) return;
    if (g_offGroundEntity <= 0) return;
    // ground only: this runs on WALK cmds (bMoveBlocked). "on ladders" is LadderEdgeJump (ladder.inl)

    if (!bPost)
        s_edgeJumpGround = EntI(pLocal, g_offGroundEntity) != -1;
    else if (s_predicted && s_edgeJumpGround && EntI(pLocal, g_offGroundEntity) == -1)
    {
        pCmd->buttons |= IN_JUMP;
        // will the engine actually TAKE it? Grounded at cmd start is s_edgeJumpGround; the only
        // other refusal is the no-pogo latch (m_nOldButtons & IN_JUMP), mirrored by s_prevCmdJump.
        // Space already held -> this OR changes nothing and mini jump must not claim a takeoff
        s_edgeJumpFired = !s_prevCmdJump;
    }
}

// ==== LONG JUMP / MINI JUMP (rewrite 11-07, straight off css-community cs_gamemovement.cpp) ====
// Takeoff tick = grounded at START (FL_ONGROUND as the last tick left it, see JumpBug) and this cmd
// jumps: your own fresh press, or the walk-off tick with the edge option (jump from START, the last
// ground position there is). Duck() runs BEFORE CheckJumpButton, so IN_DUCK on that tick sets
// m_bDucking (:943) and CheckJumpButton takes its ducking branch (:667): vz = 301.99 SET instead of
// += onto StartGravity's -halfgrav, i.e. +halfgrav (~6 u/s) more jump.
//   long jump: duck held into the first air tick -> FinishDuck lifts the feet 8.5 (:955 / :843) =
//              crouch jump; adaptive keeps holding to land 8.5u higher.
//   mini jump: duck on the takeoff tick ONLY. m_bDucking stays pending, the unduck branch (:985)
//              runs FinishUnDuck the moment the 8.5 drop is clear: feet DOWN 8.5 without ever having
//              gone up = an 8.5u lower arc. "hold crouch" turns it into a crouch jump to the ground.
// Both are sim-checked: the engine has to fly the takeoff (airborne, vz > 0) or the cmd is left alone.

// true = this cmd is a takeoff (addJump: it has to press jump itself, the walk-off case)
static bool MvTakeoff(void* pLocal, const UserCmd_t* pCmd, bool edge, bool& addJump)
{
    addJump = false;
    if (!(s_prePredFlags & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
        return false;
    if (s_prevCmdJump)   // m_nOldButtons & IN_JUMP: CheckJumpButton refuses (:640), no takeoff to shape
        return false;
    if (pCmd->buttons & IN_JUMP)
        return true;
    if (edge && !(EntI(pLocal, g_offFlags) & FL_ONGROUND))   // END airborne without a jump = walking off
    {
        addJump = true;
        return true;
    }
    return false;
}

// fly `c` from START; true = it took off. Leaves the entity at c's END either way (the caller
// re-flies its own cmd when it keeps that one instead)
static bool MvTakeoffFlies(void* pLocal, UserCmd_t* c)
{
    RestoreToFrame(PredCommandsPredicted() - 1);
    Pred_Simulate(pLocal, c);
    return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntV3(pLocal, g_offVelocity).z > 0.f;
}

// file scope: a ladder edge jump (ladder.inl LadderEdgeJump) arms the hold from the ladder tick
static int s_ljHeld = 0;          // duck ticks sent since takeoff, 0 = idle
static int s_ljTakeoffTick = 0;

static void LongJump(void* pLocal, UserCmd_t* pCmd)
{
    int& s_held = s_ljHeld;
    int& s_takeoffTick = s_ljTakeoffTick;
    if (!On(g_longJump, g_longJumpKey, g_longJumpMode) || !s_predicted || g_offFlags <= 0)
    {
        s_held = 0;   // key up mid-air = stop holding (adaptive "let go" is this)
        return;
    }

    bool addJump = false;
    if (MvTakeoff(pLocal, pCmd, g_longJumpEdge, addJump))
    {
        UserCmd_t c = *pCmd;
        c.buttons |= IN_JUMP | IN_DUCK;
        if (MvTakeoffFlies(pLocal, &c))
        {
            pCmd->buttons = c.buttons;
            s_held = 1;
            s_takeoffTick = pCmd->tick_count;
            DebugLog("LJ: takeoff %s z=%.3f spd=%.1f", addJump || s_edgeJumpFired ? "edge" : "jump", s_prePredOrigin.z, s_prePredVel.Length2D());
        }
        else
        {
            DebugLog("LJ: takeoff refused z=%.3f", s_prePredOrigin.z);
            MvTakeoffFlies(pLocal, pCmd);   // back to this cmd's own END
            s_held = 0;
        }
        return;
    }
    if (!s_held)
        return;

    const bool landed = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool timeUp = pCmd->tick_count - s_takeoffTick >= (int)std::round(g_longJumpAdaptiveTime / TickInterval());
    // key-up never reaches here (returned above); landed is always a stop
    const bool cancel = LjAdaptiveShouldCancel(g_longJumpAdaptiveStopMask, false, landed, timeUp);
    if (!cssm::LjHoldDuck(s_held, landed, g_longJumpAdaptive, cancel))
    {
        s_held = 0;
        return;
    }
    pCmd->buttons |= IN_DUCK;
    ++s_held;
}

static bool s_miniHit    = false;   // takeoff duck went out this tick (indicator flash)
static bool s_mjHoldDuck = false;   // "hold crouch": duck held from takeoff until the ground (routecalc hold bit 4)

static void MiniJump(void* pLocal, UserCmd_t* pCmd)
{
    s_miniHit = false;
    if (!On(g_miniJump, g_miniJumpKey, g_miniJumpMode) || !s_predicted || g_offFlags <= 0)
    {
        s_mjHoldDuck = false;
        return;
    }
    if (s_prePredFlags & FL_ONGROUND)
        s_mjHoldDuck = false;   // landed (held through the landing tick: that is the 8.5 the hold buys)

    bool addJump = false;
    if (MvTakeoff(pLocal, pCmd, g_miniJumpEdge, addJump))
    {
        UserCmd_t c = *pCmd;
        c.buttons |= IN_JUMP | IN_DUCK;
        if (MvTakeoffFlies(pLocal, &c))
        {
            pCmd->buttons = c.buttons;
            s_miniHit = true;
            s_mjHoldDuck = g_miniJumpHoldDuck;
            DebugLog("MJ: takeoff %s hold=%d z=%.3f", addJump ? "edge" : "jump", s_mjHoldDuck ? 1 : 0, s_prePredOrigin.z);
        }
        else
        {
            DebugLog("MJ: takeoff refused z=%.3f", s_prePredOrigin.z);
            MvTakeoffFlies(pLocal, pCmd);
        }
        return;
    }
    if (s_mjHoldDuck)
        pCmd->buttons |= IN_DUCK;
}

// Fast stop. botox's csgo 0.1 wish was a no-op in CSS (0.1 wish = ~0 accel, friction alone ran).
// CSS brake = Friction() then Accelerate() with wishdir = -vel (gamemovement.cpp FullWalkMove):
// addspeed never clamps, accel adds accel*dt*wish*sf against vel. Wish sized so the tick ends at
// exactly 0 (FastStopBrakeWish): full maxspeed brake while fast, exact on the last tick, no
// backwards drift. Reads START (pre-pred) state: this runs after Pred_Start, entity holds END.
static float AsSurfaceFriction(void* pLocal);   // airstuck.inl (included later, same TU)
static void FastMovement(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_fastStop) return;
    if (!(s_prePredFlags & FL_ONGROUND) || g_offMaxspeed <= 0) return;
    if (pCmd->forwardmove != 0.f || pCmd->sidemove != 0.f) return;
    // bhop landing tick = grounded w/ no move keys; braking here eats hop speed
    if (s_rawJumpHeld || (pCmd->buttons & IN_JUMP)) return;
    // your own keys, not just this cmd, must be clear of a move
    if (s_rawFwd != 0.f || s_rawSide != 0.f
        || (s_rawButtons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) != 0)
        return;
    Vec3 vel = s_prePredVel; vel.z = 0.f;
    if (vel.Length2D() < 1.f) return;

    const float wish = FastStopBrakeWish(vel.Length2D(), TickInterval(), CvarFloat("sv_friction", 4.f),
        CvarFloat("sv_stopspeed", 75.f), CvarFloat("sv_accelerate", 5.5f), AsSurfaceFriction(pLocal),
        EntF(pLocal, g_offMaxspeed), (s_prePredFlags & FL_DUCKING) != 0);
    if (wish <= 0.f) return;   // friction alone stops it this tick
    float fwd = 0.f, side = 0.f;
    FastStopWish(VectorAngles(vel).y, pCmd->viewangles.y, wish, fwd, side);
    pCmd->forwardmove = fwd;
    pCmd->sidemove = side;
}

// ============================================================================
// wave 2 features (ref ports): edge bug, pixel surf, auto align,
// texture bug, head surf, wall climb, air stuck.
// Skipped extras (first pass): edge bug sound/chat/visualize/mouse lock,
// texture bug choke ticks (needs sendpacket ptr), pixel surf assist family.
// ============================================================================

static bool s_textureBugHit = false; // TB catch owns pCmd this tick (HeadSurf/WallClimb/AirStuck yield)
static bool s_tbOwnsCmd = false;     // TB steered pCmd this tick WITHOUT a hit (creep/drive/probe = band
                                     // setup) -- AirStuck must not stomp it with its coarse sweep move
// TB ride coyote ticks left (texturebug.inl s_ride_grace), as of the END of the last TB call.
// > 0 = TB is mid-ride, including the free-fall ticks between two pins. PixelSurf (runs before
// TB) and WallClimb yield on it, coldtires rule: a tb ride owns the wall
static int  s_tbRideHold = 0;
static bool s_headSurfHit = false;
static bool s_headBounceHit = false; // HB clipped a crease this tick (one-tick catch, no hold)
static bool s_wallClimbHit = false;    // botox g_wall_climb.caught(): wc's catch (land / hold / jump fix) is this cmd
static bool s_wallClimbActive = false; // botox g_wall_climb.active(): wc ran an air wall tick this cmd (pixel surf yields)
static bool s_wcLanded = false;        // botox g_wall_climb.m_hit: a land went out (indicator + detection)
static bool s_wcSyncWant = false;      // wc key held: AS_Sync keeps cl_pred_optimize armed for it too
static float s_wcPreTbFwd = 0.f, s_wcPreTbSide = 0.f;   // the keys TB was handed this cmd: wc sweeps from them over TB's creep
static int   s_wcPreTbButtons = 0;
static bool s_tbSyncWant = false;      // tb key held: botox texturebug.cpp:387 sync_arm( k_sync_tb ) -- START must be the server END
static bool s_phSyncWant = false;      // ph key held: a pin landing is exact-float, START must be the server END too
static bool s_airStuckHit = false;
static bool s_airStuckReal = false;     // actually stuck (detections), set in AirStuckTick
// s_shouldPixelSurf / s_psRidingNow / s_psAssistHopActive moved ABOVE AutoJumpbug --
// jumpbug is a PRE-prediction feature and has to read the ride state too
// indicator snapshot, written once per cmd at the end of Movement_RunImpl
static MvInd_t s_mvInd{};

// ---- Edge bug: the search, the replay and the overlay snapshot ----
#include "edgebug.inl"

// JumpBugKangarooYield definition (forward-declared above AutoJumpbug): s_ebDetected is
// edgebug.inl's found-plan flag ("m_found"), the same thing botox's kangaroo() reads as
// m_will_edgebug -- read directly now that edgebug.inl is visible in this TU.
static bool JumpBugKangarooYield() { return s_ebDetected; }

// ---- Pixel surf (maintain the surf by ducking; self-contained duck/stand prediction) ----
// write a WORLD-space 2D wish into the cmd's view-relative forward/side pair. Every
// feature in this file did this inline (AutoAlign 4x, the PixelSurf hold, TB); the guide and the
// speed brake need it twice more, and getting the sin sign wrong steers into the opposite wall
static void MV_WriteWishWorld(UserCmd_t* pCmd, const Vec3& vDir, float flMag)
{
    Vec3 vFlat(vDir.x, vDir.y, 0.f);
    const float flLen = vFlat.Length2D();
    if (flLen < 0.0001f || flMag <= 0.f)
        { pCmd->forwardmove = 0.f; pCmd->sidemove = 0.f; return; }
    Vec3 vAngle = VectorAngles(vFlat);
    ClampAnglesMv(vAngle);
    const float flRot = DEG2RADF(vAngle.y - pCmd->viewangles.y);
    pCmd->forwardmove = cosf(flRot) * flMag;
    pCmd->sidemove = -sinf(flRot) * flMag;
}

// entity hit whose model is a studio model (prop, building), not a brush model ("*N" =
// func_brush, door). IVModelInfoClient GetModel 1 / GetModelName 3 (visuals.cpp ModelByIndex).
// Moved here from the deleted fakemove.inl (11-03/11-04): MV_TraceIsProp below is real pixel
// surf prop-rejection, not fake movement, and still needs this test.
static bool FM_StudioEnt(void* ent)
{
    if (!g_modelInfo || g_offModelIndex <= 0) return false;
    __try {
        // m_nModelIndex is a short (skins.cpp masks the int read); negative = client-side model
        const void* model = VCall<const void*>(g_modelInfo, 1, (int)(short)EntI(ent, g_offModelIndex));
        const char* name = reinterpret_cast<uintptr_t>(model) < 0x10000 ? nullptr
                         : VCall<const char*>(g_modelInfo, 3, model);
        return reinterpret_cast<uintptr_t>(name) >= 0x10000 && name[0] && name[0] != '*';
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- A PROP IS NOT A SURF ------------------------------------------------------------
// Every part of pixel surf -- the hunt's wall gate, the guide's steer, the park's brake, the
// stick's 10u press -- took ANY upright non-displacement face the trace returned, and a prop
// answers that description: crates, barrels, fences, signs and dispensers all trace as flat
// steep faces. They are not brush planes though. A static prop's collision is a convex hull
// from its .phy, at whatever angle the mapper dropped it at, so the into-wall press meets it
// at a hull CORNER instead of flush -- the corner digs past the face, the hull wedges, and the
// press keeps feeding it: "auto pixel surf gets stuck on props". There is no sub-unit lip to
// ride on one either, so nothing is lost by refusing them outright.
// Two shapes to refuse (sdk gametrace.h:71 -- "If hEnt points at the world entity, then this is
// the static prop index"): a static prop reports the WORLD as m_pEnt with the prop index in
// hitbox, and a dynamic prop / building reports itself with a studio model. Brush entities
// (func_brush, doors, force fields) are NOT refused: those are real brush planes, and the trace
// filter already drops the ones the engine lets us through
static bool MV_TraceIsProp(const GameTrace_t& tr)
{
    void* pWorld = GetClientEntity(0);
    if (tr.hitbox > 0 && (!tr.m_pEnt || tr.m_pEnt == pWorld)) return true;   // static prop
    // world hit, or no world to compare against: a brush face, and NOT a question for
    // FM_StudioEnt -- worldspawn's model name is "maps/<name>.bsp", which passes its "no
    // leading *" test and would report the whole map as a prop
    if (!pWorld || !tr.m_pEnt || tr.m_pEnt == pWorld) return false;
    return FM_StudioEnt(tr.m_pEnt);                                          // prop / building
}

// ---- AND THE PIN ITSELF HAS TO REST ON A BRUSH ---------------------------------------
// MV_TraceIsProp gates the faces pixel surf goes LOOKING for, and the log says that gate
// fires constantly -- prop=9887 refusals. The same log has latch=116 next to it, because NO PIN
// IS EVER ASKED WHAT CLIPPED IT. The hunt's gate only proves a brush wall stands somewhere within
// 10u of the LIVE hull; the pin test downstream is velocity-only, and a crate standing against
// that wall (every TF2 map) clips vz to -halfgrav exactly the same way. So the wall passes the
// gate and the prop takes the ride: "auto ps still counts some props as pixelsurfs".
// At the pinned tick the hull is flush against whatever held it, so one fan AT THAT ORIGIN
// answers it -- nearest brush face vs nearest prop face, and a prop closer than any brush IS the
// thing we are resting on. Ties pass (both in contact = a real seam with a crate beside it).
// Displacements are neither: real geometry, they do hold rides, so they are skipped and this can
// only ever refuse less. Horizontal fan on purpose -- floors are the ONGROUND break's job
static bool MV_PinOnProp(const Vec3& origin, const Vec3& mins, const Vec3& maxs,
                         TraceFilterWorldProps& filter)
{
    constexpr float flRange = 6.f;   // contact only: a pinned hull rests ~0 off its own face
    constexpr int   kRays  = 16;   // the ring is a COUNT, not a float accumulator (see below)
    constexpr float flStep = (MV_PI * 2.f) / float(kRays);
    float flBrush = flRange * 2.f, flProp = flRange * 2.f;
    for (int i = 0; i < kRays; i++)
    {
        const float a = flStep * float(i);
        GameTrace_t tr{};
        TraceHull(origin, origin + Vec3(cosf(a) * flRange, sinf(a) * flRange, 0.f),
                  mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
        if (tr.fraction >= 1.f || (tr.surface.flags & SURF_TRIGGER_MV)) continue;
        if (tr.dispFlags & DISPSURF_SURFACE) continue;
        float& flBest = MV_TraceIsProp(tr) ? flProp : flBrush;
        flBest = std::min(flBest, tr.fraction * flRange);
    }
    return flProp < flBrush;
}

// ---- Pixel surf: botox pixel_surf( ) 1:1 (pixelsurf.inl) ----
static bool AutoBounceOwnsCmd();   // autobounce.inl (included later): botox m_auto_bounce_data.m_owns_cmd
static void PrCmd(const char* fmt, ...);   // practice.inl (included later): pixelsurf.inl's PsPlaySound uses it
// ref ports (wallengine.inl, included after wallclimb.inl): TB, wall stuck (AS / PS engine), pixel hop (+ WC landings)
#include "wallengine_pure.h"
static we::WsOut WeWallStuck(void* pLocal, UserCmd_t* cmd, we::WsState& st, bool slow, const char* tag);
static bool WeTextureBugTick(void* pLocal, UserCmd_t* pCmd);   // true = stall veto (hands off)
template <class Accept> static bool WePixelHopAir(void* pLocal, UserCmd_t* cmd, Accept accept, int& sims, bool pooled);
static bool s_pixelHopHit = false;   // WePixelHop wrote this cmd (land / hop): the air movers after it yield
static bool s_pixelHopOn  = false;   // PH key active this cmd: pixel surf stands down (ref 18000c0b0 skips wall stuck)
static void AT_Stamp(const UserCmd_t& c, const char* who);   // texturebug.inl
#include "pixelsurf.inl"

// The player hull is an AABB, so only a face square to +-X or +-Y meets it FLAT. On anything
// else -- a rotated brush, a displacement wall, a bevel -- contact is a single hull CORNER, and
// auto align presses straight in against it: the corner digs past the face and the hull wedges
// = stuck. 0.999 ~= 2.6 deg off square, which is float noise on a real axis-aligned brush and
// nothing else. Ramps are already out via the z term (a wall normal has no z at all)
static constexpr float k_mvAxisAlignDot = 0.999f;
static bool MV_IsHullFlatWall(const Vec3& n)
{
    if (fabsf(n.z) > 0.001f) return false;
    return fabsf(n.x) >= k_mvAxisAlignDot || fabsf(n.y) >= k_mvAxisAlignDot;
}

// ---- Auto align (nudge movement into a nearby wall to set up / hold a pixel surf) ----
// AutoAlign = botox auto_align.cpp 1:1 (11-08b, nudge-aware pin tests) + the "hold pixel surfs" layer.
static float AsSurfaceFriction(void* pLocal);   // airstuck.inl (included later, same TU)
#include "autoalign.inl"

// TB pin tests, ceiling detect, the sweep helpers and HeadSurf all moved INTO
// texturebug.inl (user: "keep everything tb related inside the texturebug.inl").
// TB_WallSupportDistance + TB_SteeringAwayFromWall deliberately stay above: AutoAlign
// and WallClimb call them, and both run before this include point.
#include "texturebug.inl"


// ---- Air stuck: botox air_stuck.cpp 1:1 (airstuck.inl) ----
#include "airstuck.inl"

// ---- Wall climb: botox wall_climb.cpp 1:1 (wallclimb.inl). After airstuck.inl: it takes the cmd from AS on a
// catch (AsClock / AsSetOrigin live there)
#include "wallclimb.inl"

// ---- ref (ref) movement ports: TB / wall stuck / pixel hop ----
#include "wallengine.inl"

// the whole pixel finder + pixel surf assist moved OUT of movement.cpp into
// psassist.inl (user: "seperate the ps assist function into a different file"). Same
// include trick texturebug.inl uses -- the code keeps this file's static prediction
// helpers (Pred_Simulate / RestoreToFrame / SimsLeft / s_prePredFlags / s_prePredVel).
#include "psassist.inl"

// the route calculator, and it is included AFTER psassist.inl on purpose -- it flies
// the assist's own choreography (PsHopInputs) and snaps its markers onto the finder's points,
// so it needs both of them declared. Same include trick, same file-static prediction helpers.
#include "routecalc.inl"

// fake movement DELETED (11-03/11-04 tabs round): no botox csgo equivalent. FM_Brushes --
// the only piece of it anything else still needs -- moved below, kept for psmap.inl (MVPS,
// whole-map pixel surf scan), which is real pixel surf, not fake movement.
#include "fm_seam.h"   // FmPlane (FM_Brushes' callback type)

// every solid world brush whose AABB meets [mn, mx], with its planes. fn(planes, n) true = stop.
// `cap` bounds the walk: 512 is plenty for a hull-sized window, and psmap.inl's whole-map scan
// passes the brush count of a map instead. IEngineTrace slots (sdk-2013 IEngineTrace.h order ==
// ref x64 header): GetBrushesInAABB 13, GetBrushInfo 15.
// CUtlVector<T> x64: CUtlMemory { T* mem; int alloc; int grow; } int size; T* dbg
struct FmUtlVec { void* mem; int alloc; int grow; int size; void* dbg; };
static constexpr int k_fmTraceBrushesInAABB = 13;
static constexpr int k_fmTraceBrushInfo     = 15;

template<class Fn>
static void FM_Brushes(const float mn[3], const float mx[3], Fn&& fn, int cap = 512)
{
    using Brushes_t = void(__fastcall*)(void*, const Vec3&, const Vec3&, FmUtlVec*, int);
    using Info_t    = bool(__fastcall*)(void*, int, FmUtlVec*, int*);
    // engine grows these with its own allocator; reused forever, never freed (one buffer each)
    static thread_local FmUtlVec s_brushes{}, s_planes{};
    if (!g_engineTrace) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engineTrace);
        s_brushes.size = 0;
        ((Brushes_t)vt[k_fmTraceBrushesInAABB])(g_engineTrace, Vec3(mn[0], mn[1], mn[2]),
            Vec3(mx[0], mx[1], mx[2]), &s_brushes, (int)MASK_PLAYERSOLID);
        for (int i = 0; i < s_brushes.size && i < cap; ++i)
        {
            const int brush = static_cast<int*>(s_brushes.mem)[i];
            int contents = 0;
            s_planes.size = 0;
            if (!((Info_t)vt[k_fmTraceBrushInfo])(g_engineTrace, brush, &s_planes, &contents)) continue;
            if (!(contents & MASK_PLAYERSOLID) || s_planes.size <= 0 || !s_planes.mem) continue;
            if (fn(static_cast<const FmPlane*>(s_planes.mem), s_planes.size)) return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// whole-map pixel surf scan (psmap.inl, MVPS-owned): its brushes come from FM_Brushes above.
#include "psmap.inl"

// FakeMove_Restore()/FakeMove_DemoTick() deleted with fake movement (11-03/11-04), but their
// call sites are not mine (src/hooks/thread.cpp:535 eject path, src/hooks/fsn.cpp:41 demo
// playback stage) -- no-op stubs so those keep linking. NEEDS FROM OTHERS: drop the calls
// whenever that unit next touches those files; harmless either way until then.
void FakeMove_Restore() {}
void FakeMove_DemoTick() {}

// practice tab: console commands only (bots, buildings, noclip, save/teleport). AFTER routecalc.inl
// on purpose -- it aims with that file's RcRay and RcAimPlayerHead, which is what puts a placed
// bot on a player's HEAD instead of inside him.
#include "practice.inl"

// Unit A port: small tricks (nulls/hsw/delay hop/auto crouch/smooth
// demo view/jumpstats). After everything else -- it only reads helpers already defined above.
#include "movement_ex.inl"

// auto bounce = botox auto_bounce.cpp. After airstuck.inl (AsClock, AsSetOrigin), before
// movement_tricks2.inl (bounce assist yields to s_ab.ownsCmd)
#include "autobounce.inl"

// Unit A3 port: bounce/pf modes/eb presets/ps fix. After movement_ex.inl (needs
// edgebug.inl's s_eb* statics and this file's own s_*Hit flags, all already defined above).
#include "movement_tricks2.inl"
// Unit A2 port: edge skip + blockbot. After movement_ex.inl -- reuses its AutoCrouch-shaped
// trace helpers conceptually but shares nothing directly, just the file's own statics.
#include "movement_edge.inl"

// CSS HAS LADDERS, TF2 did not -- CS:GO csgo-sdk hacks/movement/{movement.cpp,fireman.cpp}
// port. fireman.inl needs k_ladderMaskMv/k_contentsLadderMv before ladder.inl.
#include "fireman.inl"
#include "ladder.inl"

// ==== distance calculator: botox hacks/movement/dist_calc.cpp 1:1 (two-click aim tool) ====
// dist_calc.h/dist_calc_draw.h stay engine-free / ImGui-only; this is the trace + key + state glue
// dist_calc.cpp's n_movement::impl_t::dist_calc did, ported with this repo's own trace/filter/On()
// helpers. AFTER everything above so EngineLevelName (psassist.inl) is already declared.
#include "dist_calc_draw.h"
// check floor level's world mesh reuses websurface.cpp's WsTranspose4 (botox get_world_to_screen_matrix
// transpose, verified good at websurface.cpp:1018-1024) instead of re-deriving the same math twice.
#include "../web/websurf_math.h"

bool g_distCalc = false; int g_distCalcKey = 0; int g_distCalcMode = 0;

struct DcState_t
{
    std::string map;
    int count = 0;              // 0 none, 1 = A placed, 2 = A+B placed (verdict shown)
    Vec3 aAim{}, a{}, b{};
    dc::DcResult r{};
};
static DcState_t s_dc;
static std::mutex s_dcLock;      // render thread reads a snapshot, game thread writes s_dc

// dc::Vec3Like-shaped trace fn over this repo's TraceLine/TraceFilterWorldProps (MASK_PLAYERSOLID,
// botox's mask_playersolid). skip = the local player, same as botox's c_trace_filter(local)
struct DcTraceAdapter
{
    void* pLocal;
    struct R { float frac; dc::Vec3Like end; float normalZ; bool startSolid; };
    R operator()(const dc::Vec3Like& a, const dc::Vec3Like& b) const
    {
        TraceFilterWorldProps f; f.Setup(pLocal);
        GameTrace_t tr{};
        TraceLine(Vec3(a.x, a.y, a.z), Vec3(b.x, b.y, b.z), MASK_PLAYERSOLID, &f, &tr);
        return { tr.fraction, { tr.endpos.x, tr.endpos.y, tr.endpos.z }, tr.plane.normal.z, tr.startsolid };
    }
};

// botox edgebug.cpp draw_plan "project onto geometry": one down ray per pad vertex, 18u up
// (step height) to 64u down, paint thread like botox. Drops v.z onto the floor, false = no hit.
bool MV_DropToFloor(Vec3& v)
{
    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!pLocal) return false;
    TraceFilterWorldProps f; f.Setup(pLocal);
    GameTrace_t tr{};
    TraceLine(Vec3(v.x, v.y, v.z + 18.f), Vec3(v.x, v.y, v.z - 64.f), MASK_PLAYERSOLID, &f, &tr);
    if (tr.fraction >= 1.f || tr.startsolid) return false;
    v.z = tr.endpos.z + 0.5f;
    return true;
}

static void DistCalc(void* pLocal, UserCmd_t* pCmd)
{
    constexpr float kSnap = 50.f;   // botox k_dist_calc_snap
    const char* level = EngineLevelName();
    if (!level) return;
    {
        std::lock_guard<std::mutex> l(s_dcLock);
        if (!s_dc.map.empty() && s_dc.map != level) s_dc = DcState_t{};
    }
    static bool s_prev = false;
    const bool down  = On(g_distCalc, g_distCalcKey, g_distCalcMode);
    const bool press = down && !s_prev;
    s_prev = down;
    if (!press || !pCmd || !EntAlive(pLocal)) return;

    std::lock_guard<std::mutex> l(s_dcLock);
    if (s_dc.count == 2) { s_dc = DcState_t{}; DebugLog("DC: cleared"); return; }

    const Vec3 eye = EntV3(pLocal, g_offOrigin) + (g_offViewOffset > 0 ? EntV3(pLocal, g_offViewOffset) : Vec3(0.f, 0.f, 64.f));
    Vec3 fwd; AngleVectors(pCmd->viewangles, &fwd);
    DcTraceAdapter trace{ pLocal };
    const auto hit = trace({ eye.x, eye.y, eye.z }, { eye.x + fwd.x * 8192.f, eye.y + fwd.y * 8192.f, eye.z + fwd.z * 8192.f });
    if (hit.frac >= 1.f || hit.normalZ < 0.7f) { DebugLog("DC: aim at a floor"); return; }
    const Vec3 end(hit.end.x, hit.end.y, hit.end.z);

    s_dc.map = level;
    if (s_dc.count == 0)
    {
        s_dc.aAim = end;
        const dc::Vec3Like e = dc::NearestEdge(trace, dc::Vec3Like{ end.x, end.y, end.z }, kSnap);
        s_dc.a     = { e.x, e.y, e.z };
        s_dc.count = 1;
        DebugLog("DC: point A set, aim at the landing and press again");
        return;
    }

    Vec3 dir(end.x - s_dc.aAim.x, end.y - s_dc.aAim.y, 0.f);
    const float len = dir.Length2D();
    if (len < 1.f) { DebugLog("DC: B is on top of A"); return; }
    dir.x /= len; dir.y /= len;

    const auto snap = [&](const Vec3& aim, const Vec3& facing, Vec3& normal) {
        const dc::Vec3Like ap{ aim.x, aim.y, aim.z };
        dc::Vec3Like n{};
        dc::Vec3Like e = dc::NearestEdge(trace, ap, kSnap, dc::Vec3Like{ facing.x, facing.y, facing.z }, &n);
        if (e.x != aim.x || e.y != aim.y || e.z != aim.z) { normal = { n.x, n.y, n.z }; return Vec3(e.x, e.y, e.z); }
        e = dc::NearestEdge(trace, ap, kSnap, dc::Vec3Like{}, &n);
        normal = { n.x, n.y, n.z };
        return Vec3(e.x, e.y, e.z);
    };
    Vec3 na = dir, nb(-dir.x, -dir.y, 0.f);
    s_dc.a = snap(s_dc.aAim, dir, na);
    s_dc.b = snap(end, nb, nb);

    // opposite-normal square clamp (dist_calc.cpp:179-187): A and B landed on opposing edges of
    // the SAME narrow block -- square B onto A's approach line so the block reads edge-to-edge,
    // not corner-to-corner
    if (na.x * nb.x + na.y * nb.y < -0.95f)
    {
        const Vec3 tangent(-nb.y, nb.x, 0.f);
        const float along = (s_dc.b.x - s_dc.a.x) * tangent.x + (s_dc.b.y - s_dc.a.y) * tangent.y;
        Vec3 square(s_dc.b.x - tangent.x * along, s_dc.b.y - tangent.y * along, s_dc.b.z);
        const auto probe = trace({ square.x - nb.x * 0.5f, square.y - nb.y * 0.5f, square.z + 4.f },
                                  { square.x - nb.x * 0.5f, square.y - nb.y * 0.5f, square.z - 6.f });
        if (!probe.startSolid && probe.frac < 1.f && probe.normalZ >= 0.7f && fabsf(probe.end.z - square.z) <= 2.f)
            s_dc.b = square;
    }

    s_dc.count = 2;
    const float maxSpeed = g_offMaxspeed > 0 && EntF(pLocal, g_offMaxspeed) > 1.f ? EntF(pLocal, g_offMaxspeed) : 250.f;
    s_dc.r = dc::Verdict(s_dc.a, s_dc.b, maxSpeed, TickInterval(), CvarFloat("sv_gravity", 800.f), 1.f,
                          CvarFloat("sv_airaccelerate", 10.f), 30.f);   // airCap: GetAirSpeedCap() hardcoded 30 (dist_calc.h header)
    DebugLog("%s", dc::StatusLine(s_dc.r).c_str());
}

// render-thread entry point (NEEDS FROM OTHERS: render.cpp call site, see report). Snapshots
// s_dc under lock, then draws -- same thread-safety contract as AB_GetDrawData/EB_GetDrawData.
void DistCalc_Render()
{
    if (!g_distCalc) return;
    DcState_t snap;
    { std::lock_guard<std::mutex> l(s_dcLock); snap = s_dc; }
    if (snap.count == 0) return;
    if (snap.count == 1) { DcRenderPointA(snap.a); return; }
    DcRenderVerdict(snap.a, snap.b, snap.r);
}

// ==== check floor level: botox hacks/movement/dist_calc.cpp floor_level 1:1 (lines ~310-568) ====
// Scan grid + readout + toggle key live here (game thread); the colored mesh is raw d3d, drawn
// from render.cpp h_Present (same world-space-quad slot Web_Present already uses, render.cpp:2030)
// instead of botox's DoPostScreenSpaceEffects hook -- h_Present IS the render thread here (every
// other raw-d3d overlay in this port already draws from it), so none of screenfx.cpp's
// Render_PresentTid/ScreenFx_QueueFunctor multicore dance (needed because THAT hook can land on
// the main thread) applies. Same mesh, same depth test, same z-fight lift, different (simpler,
// already-proven) place to issue it.
bool g_floorLevel = false; int g_floorLevelKey = 0; int g_floorLevelMode = 0; float g_floorLevelSize = 128.f;

constexpr int kFloorLevelN = 49;   // botox k_floor_level_n

struct FloorLevelVertex_t { float x, y, z; unsigned long color; };   // D3DFVF_XYZ | D3DFVF_DIFFUSE

struct FloorLevelState_t
{
    bool on = false;
    std::shared_ptr<const std::vector<FloorLevelVertex_t>> mesh;
    Vec3 center{};
    float lo = 0.f, hi = 0.f, mean = 0.f;
    std::string map;
};
static FloorLevelState_t s_fl;
static std::mutex s_flLock;        // render thread reads a snapshot, game thread writes s_fl
static std::atomic<bool> s_flClick{ false }, s_flShown{ false };

void FloorLevelToggle() { s_flClick = true; }
bool FloorLevelShown() { return s_flShown; }

static std::string FloorLevelReadout(float lo, float hi, float mean)
{
    char buf[96]{};
    snprintf(buf, sizeof(buf), "lowest: %.3f, highest: %.3f, mean: %.3f", lo, hi, mean);
    return buf;
}

static void FloorLevel(void* pLocal, UserCmd_t* pCmd)
{
    const char* level = EngineLevelName();
    if (!level) return;
    {
        std::lock_guard<std::mutex> l(s_flLock);
        if (!s_fl.map.empty() && s_fl.map != level) s_fl = FloorLevelState_t{};
        s_flShown = s_fl.on;
    }

    static bool s_prev = false;
    const bool down  = On(g_floorLevel, g_floorLevelKey, g_floorLevelMode);
    const bool press = (down && !s_prev) | s_flClick.exchange(false);
    s_prev = down;
    if (!press || !pCmd || !EntAlive(pLocal)) return;

    {
        std::lock_guard<std::mutex> l(s_flLock);
        if (s_fl.on) { s_fl = FloorLevelState_t{}; s_flShown = false; return; }
    }

    Vec3 cam{};
    EngineGetViewAngles(cam);
    const Vec3 eye = EntV3(pLocal, g_offOrigin) + (g_offViewOffset > 0 ? EntV3(pLocal, g_offViewOffset) : Vec3(0.f, 0.f, 64.f));
    Vec3 fwd; AngleVectors(cam, &fwd);
    TraceFilterWorldProps f; f.Setup(pLocal);
    GameTrace_t aim{};
    TraceLine(eye, eye + fwd * 8192.f, MASK_PLAYERSOLID, &f, &aim);
    if (aim.fraction >= 1.f || aim.plane.normal.z < 0.7f) { DebugLog("FL: aim at a floor"); return; }

    constexpr int n = kFloorLevelN;
    const float radius = std::clamp(g_floorLevelSize, 16.f, 512.f);
    const float step = 2.f * radius / (n - 1);
    const float x0 = aim.endpos.x - radius, y0 = aim.endpos.y - radius;
    std::vector<float> zs(n * n, NAN);
    float lo = FLT_MAX, hi = -FLT_MAX;
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const float x = x0 + j * step, y = y0 + i * step;
            GameTrace_t t{};
            // point rays slip through brush seams and hit the void below, a tiny hull does not
            // (botox dist_calc.cpp:384-385)
            TraceHull(Vec3(x, y, aim.endpos.z + 64.f), Vec3(x, y, aim.endpos.z - 64.f),
                      Vec3(-0.25f, -0.25f, 0.f), Vec3(0.25f, 0.25f, 0.f), MASK_PLAYERSOLID, &f, &t);
            if (t.startsolid || t.fraction >= 1.f || t.plane.normal.z < 0.7f) continue;
            const float z = t.endpos.z;
            zs[i * n + j] = z;
            lo = (std::min)(lo, z);
            hi = (std::max)(hi, z);
            sum += z;
            count++;
        }
    }
    if (!count) { DebugLog("FL: no floor under the aim"); return; }
    const float mean = static_cast<float>(sum / count);

    // low red -> mid blue -> high green (botox dist_calc.cpp:404-413)
    const float span = hi - lo;
    const auto color = [&](float z) -> unsigned long {
        constexpr float kStops[3][3] = { { 255.f, 40.f, 40.f }, { 40.f, 90.f, 255.f }, { 40.f, 230.f, 60.f } };
        const float t = span < 0.01f ? 0.5f : (z - lo) / span;
        const int s = t < 0.5f ? 0 : 1;
        const float a = t < 0.5f ? t * 2.f : t * 2.f - 1.f;
        const auto mix = [&](int c) { return static_cast<unsigned long>(kStops[s][c] + (kStops[s + 1][c] - kStops[s][c]) * a); };
        return (170ul << 24) | (mix(0) << 16) | (mix(1) << 8) | mix(2);
    };

    auto mesh = std::make_shared<std::vector<FloorLevelVertex_t>>();
    mesh->reserve((n - 1) * (n - 1) * 6);
    for (int i = 0; i + 1 < n; i++) {
        for (int j = 0; j + 1 < n; j++) {
            const int idx[4] = { i * n + j, i * n + j + 1, (i + 1) * n + j + 1, (i + 1) * n + j };
            if (std::isnan(zs[idx[0]]) || std::isnan(zs[idx[1]]) || std::isnan(zs[idx[2]]) || std::isnan(zs[idx[3]])) continue;
            const float qlo = (std::min)({ zs[idx[0]], zs[idx[1]], zs[idx[2]], zs[idx[3]] });
            const float qhi = (std::max)({ zs[idx[0]], zs[idx[1]], zs[idx[2]], zs[idx[3]] });
            // steeper than walkable = wall or step edge, leave a gap instead of a vertical sheet
            if (qhi - qlo > 2.f * step) continue;
            // 0.25u lift: flush with the floor it z-fights
            for (const int k : { idx[0], idx[1], idx[2], idx[0], idx[2], idx[3] })
                mesh->push_back({ x0 + (k % n) * step, y0 + (k / n) * step, zs[k] + 0.25f, color(zs[k]) });
        }
    }

    {
        std::lock_guard<std::mutex> l(s_flLock);
        s_fl.mesh = std::move(mesh);
        s_fl.on   = true;
        s_fl.map  = level;
        s_fl.center = Vec3(aim.endpos.x, aim.endpos.y, aim.endpos.z);
        s_fl.lo = lo; s_fl.hi = hi; s_fl.mean = mean;
    }
    s_flShown = true;
    DebugLog("%s", FloorLevelReadout(lo, hi, mean).c_str());
}

// readout text, same contract/thread split as DistCalc_Render above
void FloorLevel_Render()
{
    if (!g_floorLevel) return;
    FloorLevelState_t snap;
    { std::lock_guard<std::mutex> l(s_flLock); snap = s_fl; }
    if (!snap.on) return;

    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();
    ImVec2 mid{};
    if (!font || !W2S_Project(snap.center, mid)) return;
    const std::string text = FloorLevelReadout(snap.lo, snap.hi, snap.mean);
    const ImVec2 tw = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text.c_str());
    const ImVec2 pos(mid.x - tw.x * 0.5f, mid.y + 8.f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddText(font, font->FontSize, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(font, font->FontSize, pos, IM_COL32(255, 255, 255, 255), text.c_str());
}

// fixed function (no shader needed, same as botox floor_level_draw), state save/restore shaped
// like websurface.cpp's WsSave/WsRestore + mc_world.cpp's McDraw. ZENABLE/ZFUNC LESSEQUAL = the
// mesh is hidden behind real geometry exactly like botox's version, not an always-on-top overlay.
static void FloorLevelDraw(const std::vector<FloorLevelVertex_t>& mesh, IDirect3DDevice9* dev)
{
    if (!dev || mesh.empty()) return;
    const float (*m)[4] = Render_W2SMatrix();
    if (!m) return;
    float vp[16]; WsTranspose4(&m[0][0], vp);
    D3DMATRIX proj{}; memcpy(&proj, vp, sizeof(proj));
    const D3DMATRIX ident = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };

    static constexpr D3DRENDERSTATETYPE kStates[] = { D3DRS_ZENABLE,         D3DRS_ZWRITEENABLE,     D3DRS_ZFUNC,           D3DRS_ALPHABLENDENABLE,
                                                       D3DRS_SRCBLEND,        D3DRS_DESTBLEND,        D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
                                                       D3DRS_LIGHTING,        D3DRS_FOGENABLE,        D3DRS_STENCILENABLE,   D3DRS_COLORWRITEENABLE,
                                                       D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE,
                                                       D3DRS_SEPARATEALPHABLENDENABLE };
    static constexpr D3DTEXTURESTAGESTATETYPE kStages[] = { D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1 };
    DWORD states[std::size(kStates)]{}, stages[std::size(kStages)]{}, stage1[2]{}, fvf = 0;
    D3DMATRIX world{}, view{}, oldProj{};
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DBaseTexture9* texture = nullptr;

    for (std::size_t i = 0; i < std::size(kStates); i++) dev->GetRenderState(kStates[i], &states[i]);
    for (std::size_t i = 0; i < std::size(kStages); i++) dev->GetTextureStageState(0, kStages[i], &stages[i]);
    dev->GetTextureStageState(1, D3DTSS_COLOROP, &stage1[0]);
    dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &stage1[1]);
    dev->GetTransform(D3DTS_WORLD, &world);
    dev->GetTransform(D3DTS_VIEW, &view);
    dev->GetTransform(D3DTS_PROJECTION, &oldProj);
    dev->GetVertexShader(&vs);
    dev->GetPixelShader(&ps);
    dev->GetVertexDeclaration(&decl);
    dev->GetFVF(&fvf);
    dev->GetTexture(0, &texture);

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    dev->SetTexture(0, nullptr);
    dev->SetTransform(D3DTS_WORLD, &ident);
    dev->SetTransform(D3DTS_VIEW, &ident);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(mesh.size() / 3), mesh.data(), sizeof(mesh[0]));

    for (std::size_t i = 0; i < std::size(kStates); i++) dev->SetRenderState(kStates[i], states[i]);
    for (std::size_t i = 0; i < std::size(kStages); i++) dev->SetTextureStageState(0, kStages[i], stages[i]);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, stage1[0]);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, stage1[1]);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &oldProj);
    dev->SetTexture(0, texture);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    if (decl) dev->SetVertexDeclaration(decl); else dev->SetFVF(fvf);
    for (IUnknown* held : { (IUnknown*)vs, (IUnknown*)ps, (IUnknown*)decl, (IUnknown*)texture })
        if (held) held->Release();
}

// render.cpp h_Present entry point -- mesh is immutable once built (shared_ptr), this call only
// takes a ref under lock then draws, same split as FloorLevel_Render above.
void FloorLevel_World(IDirect3DDevice9* dev)
{
    if (!g_floorLevel) return;
    std::shared_ptr<const std::vector<FloorLevelVertex_t>> mesh;
    {
        std::lock_guard<std::mutex> l(s_flLock);
        if (!s_fl.on) return;
        mesh = s_fl.mesh;
    }
    if (!mesh || mesh->empty()) return;
    FloorLevelDraw(*mesh, dev);
}

// ==== per-cmd pipeline (ref RunPre -> prediction -> RunPost -> restore) ====
static void Movement_RunImpl(UserCmd_t* pCmd)
{
    // one line, once per session. What USED to be here was ~10 KB of build changelog stamped
    // into the log (and into the .rdata of every shipped DLL) on the first cmd. History belongs
    // in the handoff docs, not in the binary.
    static bool s_verStamped = false;
    if (!s_verStamped) { s_verStamped = true;
        DebugLog("movement: dih159 (movement.cpp + psassist/texturebug/routecalc/fakemove .inl, "
                 "physics in routesim.h)"); }

    // one is_active_app + one read per distinct bind, for the WHOLE cmd (see MovementKeyOK).
    // Must lead: every On() below, and every feature they call, reads this snapshot.
    MovementBinds_NewCmd();

    ResolveNetvars();
    Movement_ResolveExtraNetvars();
    // FakeMove_Tick() call removed with fake movement (11-03/11-04): no botox csgo equivalent
    Practice_Tick(pCmd);   // same reason: a practice bind is pressed while noclipping or dead

    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return;
    void* pLocal = GetClientEntity(idx);
    const bool bAlive = pLocal && EntAlive(pLocal);
    // botox create_move: capture the player's own input, then lay the replay frame on the cmd so
    // every feature below + prediction sees it. Dead = reset. Its end half runs in Movement_Run.
    Recorder_CmdStart(bAlive ? pLocal : nullptr, pCmd);
    if (!bAlive) return;

    // a movement blocker (noclip, water) stops the MOVEMENT features. It does not stop the
    // MEASURING ones: the route calculator, the pixel calculator and the ps finder answer off a
    // hull teleported onto a spot, and flying around the map placing markers is how they are
    // actually used. Their sims run as a walking player (g_predWalkSims below), or every leg of
    // every solve is a FullNoClipMove that never falls and never lands.
    const int  iLocalMove = EntMoveType(pLocal);
    const bool bMoveBlocked = iLocalMove != MOVETYPE_WALK || EntSwimming(pLocal);
    // the last term is the overlay alone: "show points" draws off a heartbeat this function
    // publishes, so a blocked cmd has to reach it even when nothing is measuring
    const bool bCalcOn = g_routeCalc || g_pixelCalc || g_psFinder || s_pfBusy || s_rcBusy
                      || s_pmBusy || s_pmReq || g_psMapLines
                      || (g_psAssist && g_psAssistRender)
                      || g_distCalc    // two-click distance tool: measures off an aim trace, same as RC/PF
                      || g_floorLevel; // check floor level: same aim-trace shape, botox calls it right after dist_calc
    // ladder: runs even while bMoveBlocked -- none of the TF2/CS:GO ground+air features below
    // know what MOVETYPE_LADDER is (bMoveBlocked exists precisely to keep them off it), but
    // fast_ladder/ladder_glide/ladder_freelook_climb/anti_ladder/auto_one_hop need to act on
    // every tick the player is actually attached to one. Own call site, nothing else touched.
    // raw keys first: fireman's ladder ride gates on YOUR keys, not the ladder features' writes
    s_rawJumpHeld = (pCmd->buttons & IN_JUMP) != 0;   // real press, before AutoJump strips it
    s_rawButtons  = pCmd->buttons;                    // ...and the rest of it
    s_rawFwd      = pCmd->forwardmove;
    s_rawSide     = pCmd->sidemove;
    if (iLocalMove == MOVETYPE_LADDER && !FiremanLadderTick(pLocal, pCmd))
    {
        LadderOnLadderTick(pLocal, pCmd);
        LadderEdgeJump(pLocal, pCmd);   // last: sims the cmd the ladder features above wrote
    }
    LadderEdgeJumpWindow(pCmd);         // every cmd: its window runs after the ladder is gone
    if (bMoveBlocked && !bCalcOn) return;

    DistCalc(pLocal, pCmd);   // measuring tool, not a movement feature -- runs even bMoveBlocked
    FloorLevel(pLocal, pCmd); // botox calls this right after dist_calc, same two call sites (390/508)

    RefreshTargetPredictZVel();  // once per cmd -- IsTargetVz in sweep loops reads cache
    Pred_ResetCmd();            // shared sim pool + restore counters reset per cmd
    s_tracesThisCmd = 0;        // perf counters, printed as tr= / res= / resk= / fast=
    s_tbFastProbes = 0;         // analytic probe budget -- its own pool, never spends sims
    // a fresh cmd starts on the LIVE end-of-cmd state, not the restore base, so the first
    // restore of the tick always runs
    g_predDirty = true;
    TbBudgetStart();            // the clock the whole TB family searches against
    // no AS sim fence any more: botox air_stuck searches on its own wall clock (g_asBudget), so
    // the shared pool is everyone's
    g_simReserve = 0;

    // ---- pre-prediction ---- (raw key capture sits above the ladder call site)
    s_edgeJumpFired = false;   // per-cmd: only the post-pred EdgeJump call sets it
    if (!bMoveBlocked)   // movement features only. The raw button reads above are shared
    {
        // Unit A2: blockbot PRE, before every other assist gets a say over forwardmove/sidemove
        BlockBot(pLocal, pCmd);
        EsPre(pLocal, pCmd);   // edge skip's own pre-prediction snapshot, same timing as EdgeBugPre
        EdgeBugPre(pLocal, pCmd);
        EbSoundPre(pLocal);   // real-state reset check, not tied to the search (edgebug.inl; replaces
                               // movement_tricks2.inl's old EdgeBugSoundPre -- botox sound combo now lives here)
        AutoJump(pLocal, pCmd);
        EdgeJump(pLocal, pCmd, false);
        // jump bug runs post-prediction (JumpBug): crouch + height assist are its own search now
        // fireman runs post-prediction (engine sims) and overwrites whatever these wrote on its ticks
        AutoStrafe(pLocal, pCmd);
        StrafeOptimizer(pLocal, pCmd);
        // Unit A port -- pure usercmd math/traces, same tier as the calls above
        Nulls(pLocal, pCmd);
        Hsw(pLocal, pCmd);
        DelayHop(pLocal, pCmd);
        // AutoGoomba / DoubleJumpStrafe removed here (TF2-only, see movement_ex.inl)
        AutoCrouch(pLocal, pCmd);
        SmoothDemoView(pLocal, pCmd);
        FakePov_Tick(g_globalVars ? g_globalVars->absoluteframetime : 0.015625f);   // once per cmd, botox fake_pov_frame()
        {
            float fpYaw = 0.f, fpPitch = 0.f, fpArrowAng = 0.f, fpArrowLen = 0.f; bool fpActive = false;
            FakePov_GetState(fpYaw, fpPitch, fpArrowAng, fpArrowLen, fpActive);
            CssBridge_SetFakePov(fpYaw, fpPitch, fpActive);   // AIMTAB's CPrediction::GetLocalViewAngles hook reads this
        }
    }

    // botox: pre features may have rewritten it -- predict + detect on the exact recorded inputs
    Recorder_CmdPrePred(pCmd);

    // ---- engine prediction (post features read predicted state; skip clean if sigs miss) ----
    g_lua.on_create_move(pCmd, nullptr, true);   // botox create_move_proxy.cpp:439 -- pre-prediction, before Pred_Start
    s_prePredFlags = g_offFlags > 0 ? EntI(pLocal, g_offFlags) : 0;
    s_prePredVel   = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity) : Vec3();
    s_prePredOrigin  = g_offOrigin > 0 ? EntV3(pLocal, g_offOrigin) : Vec3();
    s_prePredDucking = g_offDucking > 0 && EntB(pLocal, g_offDucking) != 0;
    s_prePredTick    = g_globalVars->tickcount;   // before Pred_Start moves it
    s_jsEnd.ok       = false;                     // jumpstats END: set only by this cmd's JsCaptureEnd
    PsCmdStart(pLocal);   // botox ps_cmd_start: a duck key flipped on a live ride bails pixel surf
    const bool probesOK = g_frameReady && g_offVelocity > 0 && g_offOrigin > 0
                       && g_offMins > 0 && g_offMaxs > 0 && g_offFlags > 0;
    // the movement half is asked for only on a cmd the movement features may act on --
    // a blocked cmd runs none of them, so starting prediction for them would buy one wasted sim
    const bool needPredMove = !bMoveBlocked
                       && (EdgeJumpOn()
                        || On(g_fireMan, g_fireManKey, g_fireManMode)   // grab/steer are engine sims
                        || On(g_jumpbug, g_jumpbugKey, g_jumpbugMode)
                        || On(g_longJump, g_longJumpKey, g_longJumpMode)
                        || On(g_miniJump, g_miniJumpKey, g_miniJumpMode)
                        || g_jumpstats   // reads the predicted END (JsCaptureEnd)
                        || (probesOK && (On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode)
                                      || g_autoAlign
                                      || On(g_textureBug, g_textureBugKey, g_textureBugMode)
                                      || On(g_wallClimb, g_wallClimbKey, g_wallClimbMode)
                                      || On(g_airStuck, g_airStuckKey, g_airStuckMode)
                                      // auto bounce runs in the probe block: no prediction = never called
                                      || On(g_autoBounce, g_autoBounceKey, g_autoBounceMode)
                                      || On(g_psAssist, g_psAssistKey, g_psAssistMode)
                                      // pixel hop sims in the probe block too: missing here = dead alone
                                      || On(g_pixelHop, g_pixelHopKey, g_pixelHopMode)
                                      // pixelsurf lines: ride detect + catch sims live in the probe
                                      // block. Missing here = lines only record while PS bind is on
                                      || g_psLinesEnable)));
    const bool needPred = needPredMove
                       || (probesOK && (
                                     // finder: held OR just released (release tick runs the scan),
                                     // plus every later cmd a chunked scan is still running on
                                        On(g_psFinder, g_psFinderKey, g_psFinderMode)
                                     || s_psFinderHeld || s_pfBusy
                                     // the whole-map scan is the same scanner on a queue of
                                     // seams, and the REQUEST is in here for the same reason
                                     // the route calc's key is: the cmd that starts it has to
                                     // already be predicted or the button does nothing
                                     || s_pmBusy || s_pmReq
                                     // a solve spans commands, so prediction has to
                                     // stay up for every one of them -- resume into a dead
                                     // frame and every leg after it is a lie. The KEY is in
                                     // here too: the press tick is the one that seeds the
                                     // root snapshot, and it happens before s_rcBusy is set.
                                     // the pixel calculator's key answers on its OWN
                                     // feature flag -- it used to need the route calculator on
                                     // too, so a pixel-only user got "prediction is not available"
                                     || s_rcBusy
                                     || (g_routeCalc && g_rcSolveKey != 0
                                         && MovementKeyOK(g_rcSolveKey, 1))
                                     || (g_pixelCalc && g_pcSolveKey != 0
                                         && MovementKeyOK(g_pcSolveKey, 1))));
    s_predicted = needPred && Pred_Start(pLocal, pCmd);
    // AFTER Pred_Start on purpose: the cmd's OWN prediction tick stays honest (a
    // noclipping hull predicts as noclip, so the finder's anchor ray leaves the eye you are really
    // looking from), and only the probe sims that follow -- finder, route calc, pixel calc, which
    // teleport the hull somewhere else first anyway -- are flown as a walking player.
    // Cleared in Movement_Run, so an early return or the SEH there cannot leave it set
    g_predWalkSims = iLocalMove != MOVETYPE_WALK;

    // ---- post-prediction ----
    if (!bMoveBlocked)
    {
        EdgeJump(pLocal, pCmd, true);
        // fireman: engine sims from START, leaves the entity at the sent cmd's END. Its cmd is the
        // landing (grab / launch hop): the jump-timing features below would strip or re-time it
        if (!FireMan(pLocal, pCmd))
        {
            JumpBug(pLocal, pCmd);   // engine search from START, leaves the entity at the sent cmd's END
            FastMovement(pLocal, pCmd);
            LongJump(pLocal, pCmd);
            MiniJump(pLocal, pCmd);
        }
    }

    // PS assist sits before PixelSurf (ref RunPost order); hop bookkeeping,
    // set/delete points and choreography run even un-predicted -- only the pickers sim
    // a blocked cmd runs no assist, but the overlay heartbeat is not the assist -- without
    // it the finder's points stop drawing 500ms into the noclip you are flying to look at them
    if (probesOK)
    {
        if (bMoveBlocked) PS_PublishAlive(pLocal);
        else              PS_AssistTick(pLocal, pCmd, s_predicted);
    }

    // probe features: need working prediction + frame-restore sigs (ref RunPost order:
    // PixelSurf ducks first, AutoAlign steers with the crouch active, then TB -> HS -> WC -> AS)
    if (s_predicted && probesOK)
    {
        if (!bMoveBlocked)   // these MOVE you. The finder below only measures
        {
            PsRideDetect();   // this cmd's ride bit, for every reader below
            // botox on_create_move_pre:415 -- last cmd's pixel_surf latch is the crouch this cmd
            // flies with, applied before auto_align / air_stuck get the cmd.
            // botox movement.cpp:525 -- ground drops the latch. PixelSurfImpl returns on ground
            // without clearing it, so a low ps landed inside the 16-tick window ducked forever
            if ((s_prePredFlags & FL_ONGROUND) && (s_psShouldSurf || s_psShouldUnduck))
            {
                DebugLog("PSU: ground drop %s", s_psShouldSurf ? "duck" : "stand");
                s_psShouldSurf = s_psShouldUnduck = false;
            }
            if (s_psShouldSurf)        pCmd->buttons |= IN_DUCK;
            else if (s_psShouldUnduck) pCmd->buttons &= ~IN_DUCK;
            PsSurfFixTick(pLocal, pCmd);   // Unit A3: hold duck a couple ticks into a fresh ride
            AutoAlign(pLocal, pCmd);
            s_wcPreTbFwd = pCmd->forwardmove; s_wcPreTbSide = pCmd->sidemove; s_wcPreTbButtons = pCmd->buttons;
            TextureBug(pLocal, pCmd);
            HeadSurf(pLocal, pCmd);
            // after HS on purpose (coldtires texturebug.cpp:212): a ceiling RIDE always beats a
            // one-tick clip, and HeadBounce returns outright when TB or HS owns the tick
            HeadBounce(pLocal, pCmd);
            // ref order (ref texture bug, pixel hop, then wall stuck (AS / PS here)
            WePixelHop(pLocal, pCmd);
            // AS moved ABOVE WallClimb. coldtires states the order outright -- "priority high ->
            // low: texture_bug, air_stuck, wall_climb, edge_bug" -- and ours had AS dead last,
            // yielding to WC and arriving at a drained sim pool. HS stays above it: HS is TB's
            // ceiling twin (shares the TB key, and TB->HS is a deliberate transition), not a rival
            AirStuckTick(pLocal, pCmd);
            WallClimb(pLocal, pCmd);
            AutoBounce(pLocal, pCmd);   // botox auto_bounce: yields to every s_*Hit flag above
            // botox on_create_move_post:1267 -- pixel_surf is the last mover: it yields to this
            // cmd's AS / TB / WC flags (not last tick's) and its hold re-proves the final move
            PixelSurf(pLocal, pCmd);
            JsCaptureEnd(pLocal, pCmd);   // botox: jump_stats after the last mover, before the finder probes
        }
        // pixelsurf lines walk (finder hold proof): after jumpstats' END capture, like the finder
        PsRideLineTick(pLocal, pCmd);
        PixelFinder(pLocal, pCmd);
        PixelMapScan(pLocal, pCmd);   // whole-map scan: same scanner, a queue of brush seams
    }
    else
    {
        if (s_predicted && !bMoveBlocked) JsCaptureEnd(pLocal, pCmd);   // no probe movers: END = Pred_Start's sim
        s_textureBugHit = false;
        s_tbOwnsCmd = false;
        s_headSurfHit = false;
        s_headBounceHit = false;
        s_wallClimbHit = s_wallClimbActive = s_wcLanded = s_wcSyncWant = s_tbSyncWant = s_phSyncWant = false;
        s_airStuckHit = s_airStuckReal = false;
        s_pixelHopHit = s_pixelHopOn = false;
        s_asHolding = false;
        s_shouldPixelSurf = false;
        s_psShouldSurf = s_psShouldUnduck = false;
        s_ab.ownsCmd = false;   // botox movement.cpp:1260: stale true on a skipped tick
        // PixelFinder is the only owner of its key edge, and it did not run this cmd. Left alone,
        // s_psFinderHeld stays stuck true for good: needPred never drops, and the NEXT real press
        // reads as "already held" so no column is ever anchored again
        s_psFinderHeld = false;
        s_psFinderFirst = false;
        // same reason for the chunked scan : its cursor points at a prediction base that
        // no longer exists, so drop it instead of resuming into a dead frame
        if (s_pfBusy)
        {
            DebugLog("PF: scan dropped at step %d/%d -- prediction went away", s_pfNextI, s_pfSteps);
            PfCancelScan();
        }
        PmAbort();   // ...and the map scan's queue, for the same reason. Lines already found stay
    }

    // the route calculator. Markers and keys run on EVERY cmd (placing one needs a
    // trace, not a sim); the solver only when prediction is live, and it has to run before
    // Pred_End because its legs are engine sims inside this cmd's prediction scope.
    // the pixel calculator is its own feature and runs the same per-cmd entry point
    if (g_routeCalc || g_pixelCalc)
        RouteCalc_Tick(pLocal, pCmd, s_predicted && probesOK);

    if (s_predicted)
        Pred_End(pLocal, pCmd);
    s_predicted = false;

    // edge bug runs AFTER Pred_End (ref: after EnginePrediction.End), probing the
    // ENGINE's predicted frames with its own re-established tick-time window
    // botox: recorder owns cmd = eb post over a desynced replay overwrites recorded duck timing;
    // and a plan latched by pre would hold the mouse lock, so drop it
    if (Recorder_OwnsCmd())
    {
        EbDropPlan("recorder");
        EbDonorReset();
        EsReset(true);
    }
    else if (probesOK && !bMoveBlocked)
    {
        EdgeBugPost(pLocal, pCmd);
        EdgeSkipPost(pLocal, pCmd);   // same timing: after Pred_End, re-establishes its own window
    }

    // fireman's grab tick was predicted with its own push + jump; a post feature near the same
    // wall (auto align, edge bug...) rewriting the move would send the server a cmd that never latches
    FiremanReassert(pCmd);
    AutoAlignSent(pLocal, pCmd);   // botox auto_align_sent: a later writer stomped our park = no server verdict

    // ---- publish the indicator snapshot (movement thread -> render thread) ----
    // Last thing in the pipeline so every feature's hit flag is final. Bind state is read
    // HERE and not on the render thread: On() goes through the per-cmd bind memo, which is
    // movement-thread-only state.
    {
        MvInd_t s{};
        s.ms       = GetTickCount64();
        s.onGround = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        s.speed    = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity).Length2D() : 0.f;
        s.tick     = g_globalVars ? g_globalVars->tickcount : 0;
        s.curtime  = g_globalVars ? g_globalVars->curtime : 0.f;
        s.wjSeq    = 0;   // fake movement wall jump deleted (11-03/11-04): no botox csgo equivalent
        // MVTOOLS stamina indicator: same spent-fraction formula as CssBridge_GetStaminaFraction
        // (css_bridge.cpp), via the local raw netvar read above -- indicators.cpp is a render-
        // thread TU, can't touch the netvar/CssBridge game-thread state directly
        s.stamina  = std::clamp(EntStamina(pLocal) * 19.f / 100000.f, 0.f, 1.f);
        s.wjSpeed  = 0.f;   // fake movement wall jump deleted (11-03/11-04): no botox csgo equivalent
        s.tbOn    = On(g_textureBug, g_textureBugKey, g_textureBugMode);
        s.tbHit    = s_textureBugHit;
        s.tbOwn    = s_tbOwnsCmd;
        TB_GetInd(s.tbPhase, s.tbRide, s.tbSeams, s.tbD, s.tbVin, s.tbAim);
        s.asOn     = On(g_airStuck, g_airStuckKey, g_airStuckMode);
        s.asHit    = s_airStuckHit;
        s.wcOn     = On(g_wallClimb, g_wallClimbKey, g_wallClimbMode);
        s.wcHit    = s_wcLanded;   // botox m_wall_climb_now = m_hit
        s.phOn     = On(g_pixelHop, g_pixelHopKey, g_pixelHopMode);
        s.phHit    = s_pixelHopHit;
        s.psOn     = On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode);
        // (user: "light up even if it didnt auto crouch when on a surf"). psHit used to
        // be the AUTO-CROUCH flag, so a standing surf -- a real ride, just one the hull already
        // fits into -- left the tag dark.
        // ...then it was `s_shouldPixelSurf || s_psRidingNow`, which lights on the HUNT'S
        // PREDICTION (a fall beside a wall latches it and nothing has to happen) and on every
        // other way the engine pins vz at -halfgrav -- an air stuck, a texture bug's crease, a
        // hull wedged on a crate all read identically. ps::IsSurf is the honest question: a
        // sustained pin, against a brush face we are flush with, whose face ENDS above our feet
        // (that is a lip being ridden, not a wall we are jammed in). Evidence measured in
        // PixelSurf; the two flags below are only final HERE, which is why the call is here
        s_psEv.otherOwns = s_airStuckHit || s_textureBugHit;
        s.psHit    = ps::IsSurf(s_psEv);
        // one measurement, one cmd: PixelSurf only runs with prediction up and the move
        // unblocked, so consuming the evidence here is what keeps a cmd it SKIPPED from lighting
        // the tag off the last tick it did run on
        s_psEv = ps::SurfEvidence{};
        // ps assist: delusional's HITGODA is exactly this flag -- true while a sim-validated
        // hop is pending or in flight, cleared the tick the arc ends
        s.astOn    = On(g_psAssist, g_psAssistKey, g_psAssistMode);
        s.astHit   = s_psAssistHopActive;
        s.ebOn     = On(g_edgeBug, g_edgeBugKey, g_edgeBugMode);
        s.ebHit    = s_ebDetected;           // publish runs after EdgeBugPost, so this is final
        // ...and so is the plan the overlay draws. One publish point, so every early return and
        // every drop inside EdgeBugPost is reflected without hooking each of them
        // edge skip (Unit MVEB, this round): its own find also drives the shared viz path/pad --
        // not gated on s.ebOn (edge skip may run with edge bug's own toggle off)
        EbVizPublish((s.ebOn && s_ebDetected) || (On(g_edgeSkip, g_edgeSkipKey, g_edgeSkipMode) && s_esActive),
                     s_ebDetected ? s_ebCurrentTick : s_esHit);
        s.strafeOn = On(g_autoStrafe, g_autoStrafeKey, g_autoStrafeMode);
        s.ejOn     = On(g_edgeJump, g_edgeJumpKey, g_edgeJumpMode);
        s.ljOn     = On(g_longJump, g_longJumpKey, g_longJumpMode);
        s.mjOn     = On(g_miniJump, g_miniJumpKey, g_miniJumpMode);
        s.mjHit    = s_miniHit;   // the takeoff duck actually went out (not refused/stripped)
        s.jbOn     = On(g_jumpbug, g_jumpbugKey, g_jumpbugMode);
        // botox m_can_jb: lit for 0.2 s after the jb, so a one-tick hit survives the frame sampling
        static unsigned long long s_jbHitMs = 0;
        if (s_jumpbugHit) s_jbHitMs = s.ms;
        s.jbHit    = s_jbHitMs && s.ms - s_jbHitMs < 200;
        s.ebPred   = s_ebDetected && s_ebCurrentTick < s_ebLockTicks;
        s.dhOn     = On(g_delayHop, g_delayHopKey, g_delayHopMode);
        s.acOn     = On(g_autoCrouch, g_autoCrouchKey, g_autoCrouchMode);
        s.abOn     = On(g_autoBounce, g_autoBounceKey, g_autoBounceMode);
        s.hswOn    = On(g_hsw, g_hswKey, g_hswMode);
        s.esOn     = On(g_edgeSkip, g_edgeSkipKey, g_edgeSkipMode);
        s.esHit    = s_esActive;
        s.frOn     = On(g_fireMan, g_fireManKey, g_fireManMode);
        s.frHit    = s_firemanHit;
        s.alOn     = On(g_antiLadder, g_antiLadderKey, g_antiLadderMode);
        s.alHit    = s_antiLadderHit;
        s.buttons  = pCmd->buttons;
        s.mouseDx  = pCmd->mousedx;
        s.mouseDy  = pCmd->mousedy;
        s_mvInd    = s;

        // botox run_detections: cmd START state (pre-pred), hit flags final here
        DetTick d;
        d.onGround    = (s_prePredFlags & FL_ONGROUND) != 0;
        d.walk        = iLocalMove == MOVETYPE_WALK;
        d.jumpPressed = (pCmd->buttons & IN_JUMP) != 0;
        d.vz          = s_prePredVel.z;
        d.speed       = s_prePredVel.Length2D();
        d.gravity     = CvarFloat("sv_gravity", 800.f);
        d.interval    = TickInterval();
        d.tick        = s_prePredTick;
        d.tb = s_textureBugHit; d.as = s_airStuckReal; d.ps = s.psHit; d.wc = s_wcLanded;
        d.tbOwns = s_tbOwnsCmd;
        Detections_Tick(d);
        // botox get_trick_states: eb queued / tb / as / wc / ps now void the jump
        JumpstatsTick(pCmd, s_ebDetected || s_textureBugHit || s_airStuckHit || s_airStuckReal
                            || s_wcLanded || s.psHit);
    }
}

void MV_GetIndicators(MvInd_t& out) { out = s_mvInd; }

void Movement_Run(UserCmd_t* pCmd)
{
    if (g_eject || !pCmd) return;
    __try {
        Movement_RunImpl(pCmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("movement: SEH in Movement_Run"); }
    }
    // once a second: what one sim / restore / trace actually costs (mv_perf.h)
    if (static unsigned long long s_perfNext = 0; GetTickCount64() >= s_perfNext)
    {
        s_perfNext = GetTickCount64() + 1000ull;
        if (g_perfSim.n || g_perfTrace.n)
        {
            static LARGE_INTEGER s_qpf{};
            if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
            auto us = [](long long t) { return (double)t * 1e6 / (double)s_qpf.QuadPart; };
            auto avg = [&](const MvPerfSlot& s) { return s.n ? us(s.ticks) / s.n : 0.0; };
            DebugLog("PERF: sim n=%d avg=%.1fus max=%.0fus | restore n=%d avg=%.1fus max=%.0fus | trace n=%d avg=%.1fus max=%.0fus",
                     g_perfSim.n, avg(g_perfSim), us(g_perfSim.maxTicks),
                     g_perfRestore.n, avg(g_perfRestore), us(g_perfRestore.maxTicks),
                     g_perfTrace.n, avg(g_perfTrace), us(g_perfTrace.maxTicks));
        }
        g_perfSim = g_perfRestore = g_perfTrace = MvPerfSlot{};
    }
    // botox records the FINAL cmd: out here so every early return in RunImpl (blocked move,
    // water, taunt) still steps/records. No-op unless RunImpl's Recorder_CmdStart began this cmd.
    Recorder_CmdEnd(pCmd);
    // Same reason as g_predWalkSims below, one level worse: a fault between Pred_Start and
    // Pred_End leaves gpGlobals->tickcount / curtime / frametime on the PREDICTION tick, and
    // the engine's CL_Move -> CL_SendMove -> CPrediction::Update run off those three the
    // instant we return. No-op on every normal cmd (Pred_End already cleared the flag).
    Pred_AbortIfOpen();
    // out here, not at the end of RunImpl: every early return and the SEH above have to
    // clear it too, or one blocked cmd leaves every later sim of the session forced to walk
    g_predWalkSims = false;
    // the IN_JUMP bit the server is about to see = m_nOldButtons for the NEXT cmd. Every
    // feature that presses jump on a ground tick (mini jump's chain, wall climb's launch, the
    // PS assist launch) has to know whether the engine will take that press or refuse it as a
    // pogo stick, and only a JUMPLESS cmd clears the latch. Recorded out here rather than at
    // the end of RunImpl so a dead / noclip / early-return tick still updates it
    s_prevCmdJump = (pCmd->buttons & IN_JUMP) != 0;
    s_prevCmdDuck = (pCmd->buttons & IN_DUCK) != 0;
}

// ============================================================================
// CHLClient::CreateMove hook + CInput plumbing (folded in from bhop.cpp).
// UserCmd_t (movement.h) IS the 72-byte TF2 x64 CUserCmd layout, so it doubles
// as the m_pCommands[] element type for stride/indexing.
// ============================================================================
static constexpr int MULTIPLAYER_BACKUP = 90;
static constexpr int kInputCmdsOff      = 264; // vtable(8) + pad0[256] -> CInput::m_pCommands

void*        g_input      = nullptr;
CreateMove_t o_CreateMove = nullptr;

// CInput::GetUserCmd (IInput vtable slot 8). Engine sends/predicts the cmd via GetUserCmd = a
// CRC-verified COPY snapshotted INSIDE CInput::CreateMove -- button edits after o_CreateMove never
// reach it. Return the live m_pCommands slot instead (ref CInput_GetUserCmd hook).
//
// THE COMMAND-NUMBER CHECK IS NOT OPTIONAL. The original is four lines (in_main.cpp:1399):
//     usercmd = &m_pCommands[ sequence_number % MULTIPLAYER_BACKUP ];
//     if ( usercmd->command_number != sequence_number ) return NULL;  // overwritten by a newer cmd
// The ring is 90 slots, so asking for anything older wraps onto a LIVE command, and a hook that
// hands that back is telling the engine a command exists when it does not. Two callers pay:
//   * CPrediction::PerformPrediction (prediction.cpp:1623) walks incoming_acknowledged+1 ..
//     outgoing_command and breaks on `if ( !cmd )`. Kill the NULL and the loop runs to its only
//     other exit, i >= MULTIPLAYER_BACKUP -- up to 90 full RunSimulation passes PER FRAME, on
//     commands that are not the ones it asked for. That gap only opens when acks fall behind,
//     i.e. during loss: the first hitch hands the client a 90x prediction bill, the frame time
//     that buys makes the next ack later still, and the loss sustains itself. Exactly the
//     "it starts and then it stays" shape.
//   * CInput::WriteUsercmdDeltaToBuffer (:1353) delta-encodes against whatever came back, so a
//     stale baseline is sent as if it were the real one.
// Falling through to o_GetUserCmd rather than returning nullptr outright: the original repeats
// the same test on the same array and answers NULL itself, so this stays a pure passthrough on
// a path that is rare by construction.
using GetUserCmd_t = UserCmd_t*(__fastcall*)(void* rcx, int seq);
static GetUserCmd_t o_GetUserCmd = nullptr;

// body in _impl: uses __try, so the MiscHookGuard cannot live here (C2712)
static UserCmd_t* __fastcall h_GetUserCmd_impl(void* rcx, int seq)
{
    if (g_eject || !g_input) return o_GetUserCmd(rcx, seq);
    __try {
        static bool s_hit = false;
        if (!s_hit) { s_hit = true; DebugLog("bhop: GetUserCmd live path hit"); }
        UserCmd_t* pCmds = *reinterpret_cast<UserCmd_t**>(
            reinterpret_cast<uintptr_t>(g_input) + kInputCmdsOff);
        if (pCmds && seq >= 0) {
            UserCmd_t* pCmd = reinterpret_cast<UserCmd_t*>(
                reinterpret_cast<uintptr_t>(pCmds) + (size_t)(seq % MULTIPLAYER_BACKUP) * sizeof(UserCmd_t));
            if (pCmd->command_number == seq)
                return pCmd;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return o_GetUserCmd(rcx, seq);
}

static UserCmd_t* __fastcall h_GetUserCmd(void* rcx, int seq)
{
    MiscHookGuard _g;
    return h_GetUserCmd_impl(rcx, seq);
}

// CInput::ValidateUserCmd -- TF2 x64 re-checks each cmd vs the CRC-verified copy before send and
// reverts tampered ones. ref kills it (empty hook) -- without this every post-CreateMove
// button edit is undone = no-op even with the live GetUserCmd.
using ValidateUserCmd_t = void(__fastcall*)(void* rcx, void* usercmd, int seq);
static ValidateUserCmd_t o_ValidateUserCmd = nullptr;

static void __fastcall h_ValidateUserCmd(void* rcx, void* usercmd, int seq)
{
    MiscHookGuard _g;
    if (g_eject) o_ValidateUserCmd(rcx, usercmd, seq);
    // else: skip -- validation defeated on purpose
}

static void InstallValidateUserCmdHook(HMODULE hClient)
{
    if (o_ValidateUserCmd) return;
    uint8_t* addr = PatternScan(hClient,
        "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 41 8B D8"); // ref CInput_ValidateUserCmd sig
    if (!addr) { DebugLog("bhop: ValidateUserCmd sig NOT FOUND"); return; }
    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_ValidateUserCmd, (LPVOID*)&o_ValidateUserCmd) == MH_OK) {
        MH_EnableHook((LPVOID)addr);
        DebugLog("bhop: ValidateUserCmd hook installed (no-op)");
    }
}

static void InstallGetUserCmdHook()
{
    if (o_GetUserCmd || !g_input) return;
    __try {
        void** vt = *(void***)g_input;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000 && vt[8]) {
            if (MH_CreateHook(vt[8], (LPVOID)h_GetUserCmd, (LPVOID*)&o_GetUserCmd) == MH_OK) {
                MH_EnableHook(vt[8]);
                DebugLog("bhop: GetUserCmd hook installed (slot 8)");
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("bhop: SEH during GetUserCmd install");
    }
}

bool ResolveInput()
{
    if (g_input) { InstallGetUserCmdHook(); return true; }
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return false;

    InstallValidateUserCmdHook(hClient);

    uint8_t* addr = PatternScan(hClient,
        "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 85 C0 0F 84 ? ? ? ? F3 0F 10 05");
    if (!addr) { DebugLog("bhop: CInput signature NOT FOUND"); return false; }

    __try {
        // mov rcx, [rip+disp32] : instruction len 7, disp at +3 -> &g_pInput, deref once -> CInput*
        int32_t   disp    = *reinterpret_cast<int32_t*>(addr + 3);
        uintptr_t pGlobal = reinterpret_cast<uintptr_t>(addr) + 7 + disp;
        g_input = *reinterpret_cast<void**>(pGlobal);
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_input = nullptr; }

    if (g_input) DebugLog("bhop: CInput resolved %p", g_input);
    InstallGetUserCmdHook();
    return g_input != nullptr;
}

// Resolve the live cmd slot, hand it to Movement_Run (all feature logic there).
// body in _impl: uses __try, so the MiscHookGuard cannot live here (C2712)
static void __fastcall h_CreateMove_impl(void* rcx, int seq, float frametime, bool active)
{
    o_CreateMove(rcx, seq, frametime, active); // let engine fill the command first

    // g_predReady is Movement_Install's own done flag, and this hook goes in one line BEFORE
    // Movement_Install runs (thread.cpp PollThread), so the engine can reach us with the
    // trace/prediction interfaces still unresolved. Movement_RunGuarded -- the aim half's entry
    // to the same pipeline -- has always checked it; this path did not, and leaned on the SEH
    // inside Movement_Run to swallow the fault instead.
    if (g_eject || !g_input || !g_predReady || seq < 0) return;

    Avatar_Pump();   // unit E5: game-thread Steam calls, rate-limited internally

    UserCmd_t* pCmd = nullptr;
    __try {
        UserCmd_t* pCmds = *reinterpret_cast<UserCmd_t**>(
            reinterpret_cast<uintptr_t>(g_input) + kInputCmdsOff);
        if (pCmds)
            pCmd = reinterpret_cast<UserCmd_t*>(
                reinterpret_cast<uintptr_t>(pCmds) + (size_t)(seq % MULTIPLAYER_BACKUP) * sizeof(UserCmd_t));
    } __except(EXCEPTION_EXECUTE_HANDLER) { pCmd = nullptr; }

    if (pCmd)
    {
        Movement_Run(pCmd);
        Movement_AirStuckLastWriter(pCmd);   // nothing runs after us on this path
    }
}

// Entry point for the aim half's CreateMove hook (CHLClient_CreateMove, src/hacks/aim/hooks.cpp),
// which owns the CHLClient::CreateMove slot when that half loads. That hook has the cmd
// already; all this adds is what dih's own hook wrapper added -- the eject drain counter and the
// g_eject early-out. Without the guard, MH_Uninitialize could free a trampoline while the
// movement pipeline is still running inside it.
// the LAST writer of the cmd: the aimbot (silent aim's CorrectMovement) runs after the movement
// pipeline and re-expresses view / forwardmove / sidemove, which breaks an exact-float pin.
// AS_FinalStamp (airstuck.inl) puts the cmd TB / AS proved back ("use movement fix" decides how)
static void AS_LastWriterImpl(UserCmd_t* pCmd)
{
    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!pLocal || !EntAlive(pLocal)) return;
    AS_FinalStamp(pCmd);   // the cmd TB / AS proved this tick, put back over whatever ran after them
}
// __try and the guard object cannot share a function (C2712)
static void AS_LastWriterSeh(UserCmd_t* pCmd)
{
    __try {
        AS_LastWriterImpl(pCmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("movement: SEH in Movement_AirStuckLastWriter"); }
    }
    Pred_AbortIfOpen();
}
void Movement_AirStuckLastWriter(UserCmd_t* pCmd)
{
    if (g_eject || !pCmd || !g_predReady) return;
    MiscHookGuard _g;
    AS_LastWriterSeh(pCmd);
    g_lua.on_create_move(pCmd, nullptr, false);   // botox create_move_proxy.cpp:572 -- final, after every other writer
    // botox create_move_proxy.cpp:503 / :553, after the aimbot
    if (Web_BlockFire()) pCmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
    g_botox_net.on_create_move();
}

void Movement_RunGuarded(UserCmd_t* pCmd)
{
    MiscHookGuard _g;
    // g_predReady is Movement_Install's own done flag. The aim CreateMove hook is installed
    // BEFORE Movement_Install runs (it has to be -- it claims the slot dih would otherwise take),
    // so for the first few commands the movement port's interfaces/sigs are not resolved yet.
    if (g_eject || !pCmd || !g_predReady) return;
    Movement_Run(pCmd);
}

void __fastcall h_CreateMove(void* rcx, int seq, float frametime, bool active)
{
    MiscHookGuard _g;   // the whole movement pipeline runs under this -- drain it on eject
    h_CreateMove_impl(rcx, seq, frametime, active);
}

// ============================================================================
// "no push" removed (11-03 tabs round): no botox csgo equivalent (TF2-port invention off
// C_TFPlayer::AvoidPlayers, which CSS's CCSPlayer doesn't have anyway). h_AvoidPlayers is now a
// pure passthrough -- NEEDS FROM OTHERS: if nothing else needs this hook, whoever installs it
// (not movement.cpp) can drop the install call; left wired here is functionally inert either way.
// ============================================================================
AvoidPlayers_t o_AvoidPlayers = nullptr;

void __fastcall h_AvoidPlayers(void* rcx, UserCmd_t* pCmd)
{
    MiscHookGuard _g;
    o_AvoidPlayers(rcx, pCmd);
}
