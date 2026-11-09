#pragma once
#include "../../globals/globals.h"
#include "movement.h"

// ==== ENGINE PREDICTION CORE (split out of movement.cpp) ==========================
// Everything in here is engine machinery: the CPrediction / IGameMovement call chain, the
// predicted-frame restore, the shared per-cmd sim pool and the prediction random seed. The
// FEATURES (TB / HS / WC / AS / PS / EB) stay in movement.cpp and its .inl files and ride these
// primitives -- they were file-static there, which is why the .inl files carry a note saying
// "TB rides ~30 file-static engine primitives that live in movement.cpp". They now live here.
//
// Nothing about the call sequence changed in the split. The one addition is the SNAPSHOT pair
// at the bottom, which is the same CPredictionCopy transfer Pred_Start/Pred_End already do,
// aimed at a slot of our own instead of the engine's frame storage.

// ==== vtable slots (confirmed x64) ====
inline constexpr int kPred_SetLocalViewAngles = 13; // CPrediction (base IPrediction slots first)
inline constexpr int kPred_SetupMove          = 18;
inline constexpr int kPred_FinishMove         = 19;
inline constexpr int kGM_ProcessMovement      = 1;  // IGameMovement::ProcessMovement
inline constexpr int kMH_SetHost              = 12; // IMoveHelper::SetHost
inline constexpr int kEnt_GetPredDescMap      = 15; // CBaseEntity::GetPredDescMap

// CPrediction member byte offsets: vfptr(8) + m_hLastGround(4)
inline constexpr int kPredOff_InPrediction       = 12;
inline constexpr int kPredOff_FirstTimePredicted = 13;
inline constexpr int kPredOff_EnginePaused       = 15;
// vfptr(8) + m_hLastGround(4) + 4 bools(12..15) + m_nPreviousStartFrame(16)
inline constexpr int kPredOff_CommandsPredicted  = 20;

// ==== CGlobalVarsBase (engine) ====
struct GlobalVars_t
{
    float realtime;
    int   framecount;
    float absoluteframetime;
    float curtime;
    float frametime;
    int   maxClients;
    int   tickcount;
    float interval_per_tick;
};

// ==== datamap (for CPredictionCopy) ====
struct datamap_t;
struct typedescription_t
{
    int         fieldType;
    const char* fieldName;
    int         fieldOffset[2];
    unsigned short fieldSize;
    short       flags;
    const char* externalName;
    void*       pSaveRestoreOps;
    void*       inputFunc;
    datamap_t*  td;
    int         fieldSizeInBytes;
    typedescription_t* override_field;
    int         override_count;
    float       fieldTolerance;
};
struct datamap_t
{
    typedescription_t* dataDesc;
    int         dataNumFields;
    const char* dataClassName;
    datamap_t*  baseMap;
    bool        chains_validated;
    bool        packed_offsets_computed;
    int         packed_size;
};

// ==== CPredictionCopy (field layout must match client.dll's -- verified) ====
inline constexpr int  PC_EVERYTHING    = 0;
inline constexpr bool PC_DATA_PACKED   = true;
inline constexpr bool PC_DATA_NORMAL   = false;
inline constexpr int  TD_OFFSET_NORMAL = 0;
inline constexpr int  TD_OFFSET_PACKED = 1;

using TransferData_t = int(__fastcall*)(void* thisptr, const char* op, int entindex, datamap_t* dmap);
extern TransferData_t g_fnTransferData;

struct CPredictionCopy
{
    int         m_nType;
    void*       m_pDest;
    const void* m_pSrc;
    int         m_nDestOffsetIndex;
    int         m_nSrcOffsetIndex;
    bool        m_bErrorCheck;
    bool        m_bReportErrors;
    bool        m_bDescribeFields;
    typedescription_t* m_pCurrentField;
    const char* m_pCurrentClassName;
    datamap_t*  m_pCurrentMap;
    bool        m_bShouldReport;
    bool        m_bShouldDescribe;
    int         m_nErrorCount;
    bool        m_bPerformCopy;
    void*       m_FieldCompareFunc;
    typedescription_t* m_pWatchField;
    const char* m_pOperation;

    CPredictionCopy(int type, void* dest, bool destPacked, const void* src, bool srcPacked)
    {
        m_nType = type; m_pDest = dest; m_pSrc = src;
        m_nDestOffsetIndex = destPacked ? TD_OFFSET_PACKED : TD_OFFSET_NORMAL;
        m_nSrcOffsetIndex  = srcPacked  ? TD_OFFSET_PACKED : TD_OFFSET_NORMAL;
        m_bErrorCheck = false; m_bReportErrors = false; m_bPerformCopy = true;
        m_bDescribeFields = false;
        m_pCurrentField = nullptr; m_pCurrentMap = nullptr; m_pCurrentClassName = nullptr;
        m_bShouldReport = false; m_bShouldDescribe = false; m_nErrorCount = 0;
        m_FieldCompareFunc = nullptr; m_pWatchField = nullptr; m_pOperation = nullptr;
    }
    int TransferData(const char* op, int entindex, datamap_t* dmap)
    {
        return g_fnTransferData ? g_fnTransferData(this, op, entindex, dmap) : -1;
    }
};

// ==== CMoveData (TF2 x64 layout) ====
struct CMoveData
{
    bool m_bFirstRunOfFunctions : 1;
    bool m_bGameCodeMovedPlayer : 1;
    unsigned long m_nPlayerHandle;
    int  m_nImpulseCommand;
    Vec3 m_vecViewAngles;
    Vec3 m_vecAbsViewAngles;
    int  m_nButtons;
    int  m_nOldButtons;
    float m_flForwardMove;
    float m_flOldForwardMove;
    float m_flSideMove;
    float m_flUpMove;
    float m_flMaxSpeed;
    float m_flClientMaxSpeed;
    Vec3 m_vecVelocity;
    Vec3 m_vecAngles;
    Vec3 m_vecOldAngles;
    float m_outStepHeight;
    Vec3 m_outWishVel;
    Vec3 m_outJumpVel;
    Vec3 m_vecConstraintCenter;
    float m_flConstraintRadius;
    float m_flConstraintWidth;
    float m_flConstraintSpeedFactor;
    Vec3 m_vecAbsOrigin;
};

// wave 2: engine predicted-frame restore (direct fn, ref sig)
using RestoreFrame_t = void(__fastcall*)(void* pred, int frame); // CPrediction::RestoreEntityToPredictedFrame
extern RestoreFrame_t g_fnRestoreFrame;

// ==== interfaces (resolved in Movement_Install) ====
extern void* g_prediction;   // VClientPrediction001
extern void* g_gameMovement; // GameMovement001
extern void* g_moveHelper;   // sig-resolved global
extern GlobalVars_t* g_globalVars;
extern int*  g_randomSeed;
extern bool  g_predReady;
extern bool  g_frameReady;   // restore + setabs sigs resolved -> probe features usable

inline float TickInterval()
{
    return g_globalVars ? g_globalVars->interval_per_tick : (1.f / 66.6667f);
}

// ==== entity field reads (netvar offsets from netvars.cpp) ====
inline float EntF (void* e, int off) { return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(e) + off); }
inline int   EntI (void* e, int off) { return *reinterpret_cast<int*>  (reinterpret_cast<uintptr_t>(e) + off); }
inline unsigned char EntB(void* e, int off) { return *reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(e) + off); }
inline Vec3  EntV3(void* e, int off) { return *reinterpret_cast<Vec3*> (reinterpret_cast<uintptr_t>(e) + off); }
inline bool  EntAlive(void* e) { return g_offLifeState > 0 && EntB(e, g_offLifeState) == 0; }

// ---- COLDTIRES 120th: REDUNDANT RESTORES ----
// RestoreEntityToPredictedFrame is the engine's whole prediction-state copy, and every
// restore-then-variant pair paid one even when the previous variant was answered by an ANALYTIC
// probe -- which simulates nothing, so it leaves the entity exactly where the last restore put
// it. Only a sim, a fresh cmd, or a hand-written state can dirty the frame, and that is the
// whole proof. Every restore in the TB family targets the same frame, so one flag covers all.
extern bool g_predDirty;

// both dirty the predicted frame the same way a sim does (PixelFinder places the hull by hand),
// so the restore-skip can never hand a written-over state back as "already restored"
inline void EntSetV3(void* e, int off, const Vec3& v) { g_predDirty = true; *reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(e) + off) = v; }
inline void EntSetI (void* e, int off, int v)         { g_predDirty = true; *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(e) + off) = v; }
// the same two writes for the fields a hand-written stance is made of: the duck timers are
// floats (m_flDucktime is a countdown, m_flDuckTimer a curtime) and m_bDucked/m_bDucking bytes
inline void EntSetF (void* e, int off, float v)       { g_predDirty = true; *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(e) + off) = v; }
inline void EntSetB (void* e, int off, bool v)        { g_predDirty = true; *reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(e) + off) = v ? 1 : 0; }

// ==== shared per-cmd sim pool ====
// TB -> HS -> WC -> AS draw in call order. Lag spikes came from stacked worst cases (TB sweep +
// HS sweep + AS sweep same tick ~470 sims), not any single feature -- one pool caps the tick,
// first features keep priority. k_cmdSimCap is the runaway ceiling; TbBudget* is the real guard.
inline constexpr int   k_cmdSimCap    = 384;
inline constexpr float k_cmdTimeShare = 0.40f;
extern int g_simsThisCmd;
extern int g_simReserve;

// the wall-clock pacing for that share -- CmdBudgetSeconds, and why it is not a tick share
#include "cmdbudget.h"

void      TbBudgetStart();
bool      TbBudgetExpired();
long long TbBudgetUsedUs();
long long TbBudgetLeftUs();   // what is left of this cmd's clock (botox cmd_clock_left); huge when no clock
int       SimsLeft();

// ---- SIM AS A WALKING PLAYER ----
// The route calculator, the pixel calculator and the ps finder measure the MAP, not the player:
// every answer comes off a hull teleported onto a spot and simulated, so they are the one family
// that still means something while the real player flies. In noclip they came back empty anyway,
// because m_MoveType is read by ProcessMovement: every leg ran FullNoClipMove, so the hull never
// fell, never landed, never pinned. One byte fixes it -- m_MoveType is swapped to MOVETYPE_WALK
// around the ProcessMovement call INSIDE Pred_Simulate and put back after it, so the swap cannot
// be undone by a RestoreToFrame (which reloads the engine's real noclip frame) and the live
// entity outside a sim keeps the movetype the server gave it. Set per cmd by Movement_RunImpl.
extern bool g_predWalkSims;

// ==== the prediction chain ====
datamap_t* EntPredDescMap(void* e);
int  PredCommandsPredicted();
// Pred_Start without a Pred_End leaves gpGlobals' clock on the prediction tick and the engine
// reads it the moment we return. Call on every way out of the pipeline, fault included.
void Pred_AbortIfOpen();
void RestoreToFrame(int frame);
void RestoreEntityToPredicted();
void Pred_Simulate(void* pLocal, UserCmd_t* pCmd);
bool Pred_Start(void* pLocal, UserCmd_t* pCmd);
void Pred_End(void* pLocal, UserCmd_t* pCmd);
void Pred_ResetCmd();          // per-cmd counters (sim pool + restore stats)

// ==== PREFIX SNAPSHOTS ====
// A search that tests N variants of ONE cmd stream re-simulates the shared prefix N times. EB's
// duck sweep is the worst case in the file: every press tick k re-flies ticks 1..k of the base
// run it came from, and the log reads run=46 with the press ticks bunched around contact -- so
// three quarters of every sweep run is a re-computation of a path the search already has.
//
// A snapshot is the SAME transfer Pred_Start does (PC_EVERYTHING, packed, whole predict datamap),
// into a slot of ours instead of the engine's frame ring. Restoring one is therefore exactly as
// faithful as the Pred_Start/Pred_End round trip this file has always relied on -- no physics is
// re-derived, no tick is skipped, the resumed run sims the SAME ticks it would have simmed.
//
// Pred_SnapLoad dirties the restore flag: hand-written state must never be mistaken for the
// engine frame the last RestoreToFrame left behind.
inline constexpr int kPredSnapSlots = 8;
// THE POOL IS A FLOOR, NOT A CEILING. A prefix search that only ever resumes ONE
// path needs the 8 above; a BEAM search needs one live state per node it is keeping, and the
// route calculator keeps two banks of them (the level it is expanding and the level it is
// building). Reserve grows the pool; slots past the first 8 behave identically, and nothing
// that already uses 0..7 has to know. Never shrinks -- a slot another feature is holding is
// not this call's to free.
void      Pred_SnapReserve(int slots);            // grow the pool to at least this many slots
int       Pred_SnapCount();                       // slots that exist right now
void      Pred_SnapReset();                       // drop every slot (call per search)
bool      Pred_SnapSave(void* pLocal, int slot);
bool      Pred_SnapLoad(void* pLocal, int slot);
bool      Pred_SnapValid(int slot);
int       Pred_SnapSaves();                       // diag
int       Pred_SnapLoads();
long long Pred_SnapUs();                          // diag: what the copies actually cost
