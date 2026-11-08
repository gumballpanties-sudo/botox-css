#include "prediction.h"
#include "mv_perf.h"
#include <cstring>
#include <cstdlib>
#include <vector>

// ==== ENGINE PREDICTION CORE ======================================================
// ref CEnginePrediction port (AdjustPlayers omitted: other players stay at interpolated
// origins -- only matters standing on a player). Lifted verbatim out of movement.cpp; the call
// sequence, the slot numbers and the seed are unchanged. See prediction.h for the split note.

// ==== interfaces (Movement_Install fills these) ====
void* g_prediction   = nullptr;
void* g_gameMovement = nullptr;
void* g_moveHelper   = nullptr;
GlobalVars_t* g_globalVars = nullptr;
int*  g_randomSeed   = nullptr;
bool  g_predReady    = false;
bool  g_frameReady   = false;
TransferData_t g_fnTransferData = nullptr;
RestoreFrame_t g_fnRestoreFrame = nullptr;

bool g_predDirty     = true;   // the entity no longer holds the restore frame
bool g_predWalkSims  = false;  // sim as MOVETYPE_WALK (see prediction.h)
int  g_simsThisCmd   = 0;
int  g_simReserve    = 0;

static int s_restoreFrame = -1000;
static int s_restoresRun = 0, s_restoresSkipped = 0;

static unsigned char* s_predBackup = nullptr;
static size_t s_predBackupSize = 0;
static int   s_oldTickCount = 0;
static float s_oldCurtime = 0.f, s_oldFrametime = 0.f;
static bool  s_predOpen = false;   // Pred_Start ran, Pred_End has not (see Pred_AbortIfOpen)
static CMoveData s_moveData = {};
static int s_seedCmdNum = -1, s_seed = -1;

// ---- COLDTIRES 120th: A CLOCK, NOT A COUNT ---------------------------------------
// k_cmdSimCap is a COUNT, and a sim is not a fixed price (~23us p10 / 50 median / 121 p90), so
// 192 never protected a frame -- while it bound constantly, and what a bound cap truncates is
// the TAIL of the sweep: the solve and fan passes, i.e. exactly the two that find the pin.
// The clock is the real guard. Cheap ticks now search to completion, busy ticks stop on time;
// the count stays as the ceiling for a bad QPC. 0.40 of one tick = 6.0ms @66.7
static long long s_qpcFreq = 0, s_budgetStart = 0, s_budgetLimit = 0;
static long long s_budgetPrevStart = 0;   // last run's start -- the gap IS the frame time
static int  s_budgetPoll = 0;
static bool s_budgetBlown = false;

void TbBudgetStart()
{
    if (!s_qpcFreq) { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); s_qpcFreq = f.QuadPart; }
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);

    // Wall clock since the previous run of this same pipeline: one rendered frame, whatever
    // the frame rate is. Never trust it blind -- a first call, a map load or an alt-tab all
    // produce a gap that is not a frame, so CmdBudgetSeconds clamps it to one tick's share
    // and only ever narrows the old budget, never widens it. See prediction.h.
    float flSinceLast = 0.f;
    if (s_budgetPrevStart > 0 && s_qpcFreq > 0 && now.QuadPart > s_budgetPrevStart)
        flSinceLast = (float)((double)(now.QuadPart - s_budgetPrevStart) / (double)s_qpcFreq);
    s_budgetPrevStart = now.QuadPart;

    const float flBudget = CmdBudgetSeconds(TickInterval(), flSinceLast, k_cmdTimeShare);
    s_budgetStart = now.QuadPart;
    s_budgetLimit = (long long)((double)s_qpcFreq * (double)flBudget);
    s_budgetPoll = 0;
    s_budgetBlown = false;
}
bool TbBudgetExpired()
{
    if (s_budgetBlown) return true;
    if (s_budgetLimit <= 0) return false;
    if ((++s_budgetPoll & 3) != 0) return false;   // QPC every 4th ask -- the poll is in sweep loops
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    if (now.QuadPart - s_budgetStart < s_budgetLimit) return false;
    s_budgetBlown = true;
    return true;
}
long long TbBudgetUsedUs()
{
    if (!s_qpcFreq) return 0;
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    return ((now.QuadPart - s_budgetStart) * 1000000ll) / s_qpcFreq;
}
long long TbBudgetLeftUs()
{
    // ~18 minutes: "no clock", and still safe for a caller that multiplies by the QPC frequency
    if (!s_qpcFreq || s_budgetLimit <= 0) return 1ll << 30;
    if (s_budgetBlown) return 0;
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const long long left = s_budgetLimit - (now.QuadPart - s_budgetStart);
    return left > 0 ? (left * 1000000ll) / s_qpcFreq : 0;
}

int SimsLeft()
{
    if (TbBudgetExpired()) return 0;
    return k_cmdSimCap - g_simReserve - g_simsThisCmd;
}

void Pred_ResetCmd()
{
    g_simsThisCmd = 0;
    s_restoresRun = 0;
    s_restoresSkipped = 0;
}

// ==== MD5_PseudoRandom (Valve MD5) -- prediction random seed ====
struct MD5Ctx { unsigned int buf[4]; unsigned int bits[2]; unsigned char in[64]; };
#define MD5_F1(x, y, z) (z ^ (x & (y ^ z)))
#define MD5_F2(x, y, z) MD5_F1(z, x, y)
#define MD5_F3(x, y, z) (x ^ y ^ z)
#define MD5_F4(x, y, z) (y ^ (x | ~z))
#define MD5STEP(f, w, x, y, z, data, s) (w += f(x, y, z) + data, w = w << s | w >> (32 - s), w += x)

static void MD5Transform(unsigned int buf[4], unsigned int const in[16])
{
    unsigned int a = buf[0], b = buf[1], c = buf[2], d = buf[3];
    MD5STEP(MD5_F1, a, b, c, d, in[0] + 0xd76aa478, 7);   MD5STEP(MD5_F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
    MD5STEP(MD5_F1, c, d, a, b, in[2] + 0x242070db, 17);  MD5STEP(MD5_F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
    MD5STEP(MD5_F1, a, b, c, d, in[4] + 0xf57c0faf, 7);   MD5STEP(MD5_F1, d, a, b, c, in[5] + 0x4787c62a, 12);
    MD5STEP(MD5_F1, c, d, a, b, in[6] + 0xa8304613, 17);  MD5STEP(MD5_F1, b, c, d, a, in[7] + 0xfd469501, 22);
    MD5STEP(MD5_F1, a, b, c, d, in[8] + 0x698098d8, 7);   MD5STEP(MD5_F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
    MD5STEP(MD5_F1, c, d, a, b, in[10] + 0xffff5bb1, 17); MD5STEP(MD5_F1, b, c, d, a, in[11] + 0x895cd7be, 22);
    MD5STEP(MD5_F1, a, b, c, d, in[12] + 0x6b901122, 7);  MD5STEP(MD5_F1, d, a, b, c, in[13] + 0xfd987193, 12);
    MD5STEP(MD5_F1, c, d, a, b, in[14] + 0xa679438e, 17); MD5STEP(MD5_F1, b, c, d, a, in[15] + 0x49b40821, 22);
    MD5STEP(MD5_F2, a, b, c, d, in[1] + 0xf61e2562, 5);   MD5STEP(MD5_F2, d, a, b, c, in[6] + 0xc040b340, 9);
    MD5STEP(MD5_F2, c, d, a, b, in[11] + 0x265e5a51, 14); MD5STEP(MD5_F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
    MD5STEP(MD5_F2, a, b, c, d, in[5] + 0xd62f105d, 5);   MD5STEP(MD5_F2, d, a, b, c, in[10] + 0x02441453, 9);
    MD5STEP(MD5_F2, c, d, a, b, in[15] + 0xd8a1e681, 14); MD5STEP(MD5_F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
    MD5STEP(MD5_F2, a, b, c, d, in[9] + 0x21e1cde6, 5);   MD5STEP(MD5_F2, d, a, b, c, in[14] + 0xc33707d6, 9);
    MD5STEP(MD5_F2, c, d, a, b, in[3] + 0xf4d50d87, 14);  MD5STEP(MD5_F2, b, c, d, a, in[8] + 0x455a14ed, 20);
    MD5STEP(MD5_F2, a, b, c, d, in[13] + 0xa9e3e905, 5);  MD5STEP(MD5_F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
    MD5STEP(MD5_F2, c, d, a, b, in[7] + 0x676f02d9, 14);  MD5STEP(MD5_F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);
    MD5STEP(MD5_F3, a, b, c, d, in[5] + 0xfffa3942, 4);   MD5STEP(MD5_F3, d, a, b, c, in[8] + 0x8771f681, 11);
    MD5STEP(MD5_F3, c, d, a, b, in[11] + 0x6d9d6122, 16); MD5STEP(MD5_F3, b, c, d, a, in[14] + 0xfde5380c, 23);
    MD5STEP(MD5_F3, a, b, c, d, in[1] + 0xa4beea44, 4);   MD5STEP(MD5_F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
    MD5STEP(MD5_F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);  MD5STEP(MD5_F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
    MD5STEP(MD5_F3, a, b, c, d, in[13] + 0x289b7ec6, 4);  MD5STEP(MD5_F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
    MD5STEP(MD5_F3, c, d, a, b, in[3] + 0xd4ef3085, 16);  MD5STEP(MD5_F3, b, c, d, a, in[6] + 0x04881d05, 23);
    MD5STEP(MD5_F3, a, b, c, d, in[9] + 0xd9d4d039, 4);   MD5STEP(MD5_F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
    MD5STEP(MD5_F3, c, d, a, b, in[15] + 0x1fa27cf8, 16); MD5STEP(MD5_F3, b, c, d, a, in[2] + 0xc4ac5665, 23);
    MD5STEP(MD5_F4, a, b, c, d, in[0] + 0xf4292244, 6);   MD5STEP(MD5_F4, d, a, b, c, in[7] + 0x432aff97, 10);
    MD5STEP(MD5_F4, c, d, a, b, in[14] + 0xab9423a7, 15); MD5STEP(MD5_F4, b, c, d, a, in[5] + 0xfc93a039, 21);
    MD5STEP(MD5_F4, a, b, c, d, in[12] + 0x655b59c3, 6);  MD5STEP(MD5_F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
    MD5STEP(MD5_F4, c, d, a, b, in[10] + 0xffeff47d, 15); MD5STEP(MD5_F4, b, c, d, a, in[1] + 0x85845dd1, 21);
    MD5STEP(MD5_F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);   MD5STEP(MD5_F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
    MD5STEP(MD5_F4, c, d, a, b, in[6] + 0xa3014314, 15);  MD5STEP(MD5_F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
    MD5STEP(MD5_F4, a, b, c, d, in[4] + 0xf7537e82, 6);   MD5STEP(MD5_F4, d, a, b, c, in[11] + 0xbd3af235, 10);
    MD5STEP(MD5_F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);  MD5STEP(MD5_F4, b, c, d, a, in[9] + 0xeb86d391, 21);
    buf[0] += a; buf[1] += b; buf[2] += c; buf[3] += d;
}

static unsigned int MD5_PseudoRandom(unsigned int nSeed)
{
    MD5Ctx ctx{};
    ctx.buf[0] = 0x67452301; ctx.buf[1] = 0xefcdab89;
    ctx.buf[2] = 0x98badcfe; ctx.buf[3] = 0x10325476;
    // 4-byte input -- inline the single-block Update+Final
    memcpy(ctx.in, &nSeed, 4);
    ctx.bits[0] = 4 << 3;
    ctx.in[4] = 0x80;
    memset(ctx.in + 5, 0, 64 - 5);
    reinterpret_cast<unsigned int*>(ctx.in)[14] = ctx.bits[0];
    reinterpret_cast<unsigned int*>(ctx.in)[15] = 0;
    MD5Transform(ctx.buf, reinterpret_cast<unsigned int*>(ctx.in));
    unsigned char digest[16];
    memcpy(digest, ctx.buf, 16);
    return *reinterpret_cast<unsigned int*>(digest + 6);
}

datamap_t* EntPredDescMap(void* e)
{
    datamap_t* r = nullptr;
    __try { r = VCall<datamap_t*>(e, kEnt_GetPredDescMap); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
    return r;
}

// engine predicted-frame anchor (CPrediction's own frame storage, NOT our Pred_* backup)
int PredCommandsPredicted()
{
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(g_prediction) + kPredOff_CommandsPredicted);
}

void RestoreToFrame(int frame)
{
    // provably redundant: nothing has moved the entity since the last restore of this
    // same frame. The frame check is the insurance -- a different target must always copy
    if (!g_predDirty && frame == s_restoreFrame) { ++s_restoresSkipped; return; }
    if (g_fnRestoreFrame && g_prediction)
    {
        MvPerfScope _perf(g_perfRestore);
        ++s_restoresRun;
        g_predDirty = false;
        s_restoreFrame = frame;
        g_fnRestoreFrame(g_prediction, frame);
    }
}

// ref CMisc::RestoreEntityToPredicted (bare -- drifts one slot per Simulate; probe
// loops that need a stable base pin nBase = PredCommandsPredicted() themselves)
void RestoreEntityToPredicted()
{
    RestoreToFrame(PredCommandsPredicted() - 1);
}

// ---- pl.v_angle, proved not counted --------------------------------------------------
// SetLocalViewAngles writes pl.v_angle, which is NOT in the predict datamap (c_baseplayer.cpp:319
// has its DEFINE_FIELD commented out), so no restore, snapshot or Pred_End ever put it back: the
// last probe of a cmd left the local player's EyeAngles() on its own sweep (TB walks pitch 88 ->
// 89.9) until the engine's next RunCommand. The aim tree reads it (nospread_projectile.cpp:38).
// coldtires prediction.cpp:78 found the same leak in CSGO. The offset is taken from the engine:
// C_BaseEntity declares EyeAngles (143) and LocalEyeAngles right after it; with no move parent
// and no killcam BOTH return &pl.v_angle, so the two addresses agreeing IS the proof of the slot.
// Anything else (move parent -> static world copy, killcam -> m_angEyeAngles) disagrees, and the
// resolve just tries again next cmd. Unresolved = no save/restore = exactly the old behaviour.
inline constexpr int kEnt_EyeAngles      = 143;
inline constexpr int kEnt_LocalEyeAngles = 144;
static int s_offVAngle = -1;           // -1 = not proved yet, 0 = gave up, >0 = &pl.v_angle - ent
static int s_vAngleTries = 0;

static bool VAngleProbe(void* e, uintptr_t* a, uintptr_t* b)
{
    using RefFn = const Vec3*(__fastcall*)(void*);
    __try {
        void** vt = *(void***)e;
        *a = (uintptr_t)((RefFn)vt[kEnt_EyeAngles])(e);
        *b = (uintptr_t)((RefFn)vt[kEnt_LocalEyeAngles])(e);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void ResolveVAngle(void* pLocal)
{
    if (s_offVAngle >= 0 || !pLocal) return;
    uintptr_t a = 0, b = 0;
    const uintptr_t ent = (uintptr_t)pLocal;
    if (VAngleProbe(pLocal, &a, &b) && a == b && a > ent && a - ent < 0x10000)
    {
        s_offVAngle = (int)(a - ent);
        DebugLog("PRED: pl.v_angle off=0x%X proved (EyeAngles == LocalEyeAngles) after %d tries",
                 s_offVAngle, s_vAngleTries + 1);
        return;
    }
    if (++s_vAngleTries >= 512) {
        s_offVAngle = 0;
        DebugLog("PRED: pl.v_angle off=UNRESOLVED (eye=%p local=%p ent=%p) -- probe angles not restored",
                 (void*)a, (void*)b, pLocal);
    }
}

void Pred_Simulate(void* pLocal, UserCmd_t* pCmd)
{
    MvPerfScope _perf(g_perfSim);
    g_simsThisCmd++;
    g_predDirty = true;   // the entity no longer holds the restore frame
    const int oldTickBase = EntI(pLocal, g_offTickBase);
    // THE CLOCK IS THE ENTITY'S, like CPrediction::RunCommand (prediction.cpp:894/977):
    // curtime = m_nTickBase * TICK_INTERVAL for this tick, m_nTickBase++ after it. The old code
    // wrote m_nTickBase back and never touched curtime, so a chain of N sims ran at ONE instant
    // and TF2's duck timer (DuckOverrides strips a grounded IN_DUCK while curtime < m_flDuckTimer,
    // armed curtime+0.3 on every release -- tf_gamemovement.cpp:3194/3295) never expired inside a
    // sim. m_nTickBase is DEFINE_PRED_FIELD (c_baseplayer.cpp:403), so every RestoreToFrame,
    // Pred_SnapLoad and Pred_End puts it back with the rest of the state: a restored probe starts
    // on the right tick again, a chained one keeps walking. curtime itself is handed back so the
    // code between sims (MiniJump's timer read) still sees the cmd's own instant.
    const float oldCurtime = g_globalVars->curtime;
    g_globalVars->curtime = TickInterval() * (float)oldTickBase;
    const bool bVAngle = s_offVAngle > 0;
    const Vec3 vOldVAngle = bVAngle ? EntV3(pLocal, s_offVAngle) : Vec3();
    unsigned char* predBase = reinterpret_cast<unsigned char*>(g_prediction);
    const bool oldFirst = predBase[kPredOff_FirstTimePredicted] != 0;
    const bool oldInPred = predBase[kPredOff_InPrediction] != 0;
    // m_MoveType = m_nWaterLevel - 4, one byte. Swapped for the sim only, so a restore
    // between sims cannot undo it and nothing outside a sim ever sees a movetype we wrote
    unsigned char* pMoveType = (g_predWalkSims && g_offWaterLevel > 4)
        ? reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(pLocal) + g_offWaterLevel - 4)
        : nullptr;
    const unsigned char oldMoveType = pMoveType ? *pMoveType : 0;
    if (pMoveType) *pMoveType = 2;   // MOVETYPE_WALK

    void** mhVt = *(void***)g_moveHelper;
    using SetHost_t = void(__fastcall*)(void*, void*);
    ((SetHost_t)mhVt[kMH_SetHost])(g_moveHelper, pLocal);

    // m_pCurrentCommand = m_hConstraintEntity - 8 (netvar-relative)
    *reinterpret_cast<UserCmd_t**>(reinterpret_cast<uintptr_t>(pLocal) + g_offConstraintEnt - 8) = pCmd;

    if (pCmd->command_number != s_seedCmdNum) {
        s_seedCmdNum = pCmd->command_number;
        s_seed = (int)(MD5_PseudoRandom((unsigned)pCmd->command_number) & 0x7FFFFFFF);
    }
    *g_randomSeed = s_seed;

    predBase[kPredOff_FirstTimePredicted] = 0;
    predBase[kPredOff_InPrediction] = 1;

    void** predVt = *(void***)g_prediction;
    using SetLVA_t      = void(__fastcall*)(void*, Vec3&);
    using SetupMove_t   = void(__fastcall*)(void*, void*, UserCmd_t*, void*, CMoveData*);
    using FinishMove_t  = void(__fastcall*)(void*, void*, UserCmd_t*, CMoveData*);
    using ProcessMove_t = void(__fastcall*)(void*, void*, CMoveData*);
    ((SetLVA_t)predVt[kPred_SetLocalViewAngles])(g_prediction, pCmd->viewangles);

    ((SetupMove_t)predVt[kPred_SetupMove])(g_prediction, pLocal, pCmd, g_moveHelper, &s_moveData);
    void** gmVt = *(void***)g_gameMovement;
    ((ProcessMove_t)gmVt[kGM_ProcessMovement])(g_gameMovement, pLocal, &s_moveData);
    ((FinishMove_t)predVt[kPred_FinishMove])(g_prediction, pLocal, pCmd, &s_moveData);

    ((SetHost_t)mhVt[kMH_SetHost])(g_moveHelper, nullptr);
    *reinterpret_cast<UserCmd_t**>(reinterpret_cast<uintptr_t>(pLocal) + g_offConstraintEnt - 8) = nullptr;
    *g_randomSeed = -1;

    *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pLocal) + g_offTickBase) =
        oldTickBase + (g_globalVars->frametime > 0.f ? 1 : 0);
    if (pMoveType) *pMoveType = oldMoveType;   // the real movetype is the server's
    g_globalVars->curtime = oldCurtime;
    if (bVAngle)   // raw write, not EntSetV3: v_angle is not predicted state, nothing to dirty
        *reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(pLocal) + s_offVAngle) = vOldVAngle;
    predBase[kPredOff_FirstTimePredicted] = oldFirst ? 1 : 0;
    predBase[kPredOff_InPrediction] = oldInPred ? 1 : 0;
}

bool Pred_Start(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_predReady || !EntAlive(pLocal)) return false;
    if (g_offTickBase <= 0 || g_offConstraintEnt <= 0) return false;

    datamap_t* pMap = EntPredDescMap(pLocal);
    if (!pMap) return false;
    ResolveVAngle(pLocal);   // before any sim of this cmd has touched pl.v_angle

    s_oldTickCount = g_globalVars->tickcount;
    s_oldCurtime   = g_globalVars->curtime;
    s_oldFrametime = g_globalVars->frametime;
    s_predOpen     = true;   // the engine's clock is ours until Pred_End (or Pred_AbortIfOpen)

    unsigned char* predBase = reinterpret_cast<unsigned char*>(g_prediction);
    g_globalVars->tickcount = EntI(pLocal, g_offTickBase);
    g_globalVars->curtime   = TickInterval() * g_globalVars->tickcount;
    g_globalVars->frametime = predBase[kPredOff_EnginePaused] ? 0.f : TickInterval();

    size_t sz = (size_t)(pMap->packed_size > 4 ? pMap->packed_size : 4);
    // realloc straight into s_predBackup leaked the old block on failure (and nulled the
    // only pointer to it) -- go through a temp so a failed grow keeps the buffer we have.
    if (!s_predBackup) {
        s_predBackup = (unsigned char*)malloc(sz);
        if (!s_predBackup) return false;
        s_predBackupSize = sz;
    }
    else if (s_predBackupSize != sz) {
        unsigned char* p = (unsigned char*)realloc(s_predBackup, sz);
        if (!p) return false;
        s_predBackup = p;
        s_predBackupSize = sz;
    }

    CPredictionCopy copy(PC_EVERYTHING, s_predBackup, PC_DATA_PACKED, pLocal, PC_DATA_NORMAL);
    copy.TransferData("EnginePredictionStart", GetLocalPlayerIndex(), pMap);

    Pred_Simulate(pLocal, pCmd);
    return true;
}

void Pred_End(void* pLocal, UserCmd_t* /*pCmd*/)
{
    datamap_t* pMap = EntPredDescMap(pLocal);
    if (pMap && s_predBackup) {
        g_globalVars->tickcount = s_oldTickCount;
        g_globalVars->curtime   = s_oldCurtime;
        g_globalVars->frametime = s_oldFrametime;

        CPredictionCopy copy(PC_EVERYTHING, pLocal, PC_DATA_NORMAL, s_predBackup, PC_DATA_PACKED);
        copy.TransferData("EnginePredictionEnd", GetLocalPlayerIndex(), pMap);
    }
    s_predOpen = false;
}

// Pred_Start moves gpGlobals->tickcount / curtime / frametime onto the prediction tick, and
// ONLY Pred_End puts them back. Every path through Movement_RunImpl reaches Pred_End -- except
// a fault, which the SEH in Movement_Run swallows on the way out. The engine's own CL_Move,
// CL_SendMove and CPrediction::Update all run off those three fields immediately afterwards,
// so one swallowed fault used to hand the rest of the frame a clock that says the wrong tick.
// Cheap to make impossible: the CLOCK is what the engine reads, and it is three scalars we
// already have saved. The entity copy-back is deliberately NOT retried here -- pLocal is the
// most likely thing to have faulted, and the server's next update re-syncs it anyway.
void Pred_AbortIfOpen()
{
    if (!s_predOpen) return;
    s_predOpen = false;
    if (!g_globalVars) return;
    g_globalVars->tickcount = s_oldTickCount;
    g_globalVars->curtime   = s_oldCurtime;
    g_globalVars->frametime = s_oldFrametime;
    static bool s_logged = false;
    if (!s_logged) { s_logged = true; DebugLog("PRED: prediction left open -- engine clock restored"); }
}

// ==== PREFIX SNAPSHOTS ======================================================================
// See the header for why this is exact rather than an approximation: it is the Pred_Start
// transfer with a slot of ours as the destination.
// the three fixed arrays became one growable pool (see the header). kPredSnapSlots is
// the size it starts at, so every existing caller keeps slots 0..7 with no change in behaviour.
static std::vector<unsigned char*> s_snapBuf(kPredSnapSlots, nullptr);
static std::vector<size_t>         s_snapSize(kPredSnapSlots, 0);
static std::vector<char>           s_snapValid(kPredSnapSlots, 0);
static int    s_snapSaves = 0, s_snapLoads = 0;
static long long s_snapUs = 0;

int Pred_SnapCount() { return (int)s_snapBuf.size(); }

void Pred_SnapReserve(int slots)
{
    if (slots <= (int)s_snapBuf.size()) return;
    s_snapBuf.resize(slots, nullptr);
    s_snapSize.resize(slots, 0);
    s_snapValid.resize(slots, 0);
}

static long long SnapNowQpc()
{
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
    return c.QuadPart;
}
static void SnapAddUs(long long qpcStart)
{
    if (!s_qpcFreq) { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); s_qpcFreq = f.QuadPart; }
    if (s_qpcFreq > 0)
        s_snapUs += ((SnapNowQpc() - qpcStart) * 1000000ll) / s_qpcFreq;
}

void Pred_SnapReset()
{
    for (size_t i = 0; i < s_snapValid.size(); i++)
        s_snapValid[i] = 0;
    s_snapSaves = 0;
    s_snapLoads = 0;
    s_snapUs = 0;
}

bool Pred_SnapValid(int slot)
{
    return slot >= 0 && slot < (int)s_snapBuf.size() && s_snapValid[slot] && s_snapBuf[slot];
}

bool Pred_SnapSave(void* pLocal, int slot)
{
    if (slot < 0 || slot >= (int)s_snapBuf.size() || !pLocal || !g_predReady) return false;
    datamap_t* pMap = EntPredDescMap(pLocal);
    if (!pMap) return false;

    const size_t sz = (size_t)(pMap->packed_size > 4 ? pMap->packed_size : 4);
    if (!s_snapBuf[slot]) {
        s_snapBuf[slot] = (unsigned char*)malloc(sz);
        if (!s_snapBuf[slot]) return false;
        s_snapSize[slot] = sz;
    }
    else if (s_snapSize[slot] != sz) {
        unsigned char* p = (unsigned char*)realloc(s_snapBuf[slot], sz);
        if (!p) return false;             // failed grow keeps the buffer we have
        s_snapBuf[slot] = p;
        s_snapSize[slot] = sz;
    }

    const long long t0 = SnapNowQpc();
    CPredictionCopy copy(PC_EVERYTHING, s_snapBuf[slot], PC_DATA_PACKED, pLocal, PC_DATA_NORMAL);
    copy.TransferData("PredSnapSave", GetLocalPlayerIndex(), pMap);
    SnapAddUs(t0);

    s_snapValid[slot] = 1;
    ++s_snapSaves;
    return true;
}

bool Pred_SnapLoad(void* pLocal, int slot)
{
    if (!Pred_SnapValid(slot) || !pLocal) return false;
    datamap_t* pMap = EntPredDescMap(pLocal);
    if (!pMap) return false;
    // a slot written for a different packed layout is not this entity's state
    if (s_snapSize[slot] != (size_t)(pMap->packed_size > 4 ? pMap->packed_size : 4)) return false;

    const long long t0 = SnapNowQpc();
    CPredictionCopy copy(PC_EVERYTHING, pLocal, PC_DATA_NORMAL, s_snapBuf[slot], PC_DATA_PACKED);
    copy.TransferData("PredSnapLoad", GetLocalPlayerIndex(), pMap);
    SnapAddUs(t0);

    // hand-written state: the next RestoreToFrame must copy for real, never skip
    g_predDirty = true;
    s_restoreFrame = -1000;
    ++s_snapLoads;
    return true;
}

int       Pred_SnapSaves() { return s_snapSaves; }
int       Pred_SnapLoads() { return s_snapLoads; }
long long Pred_SnapUs()    { return s_snapUs; }
