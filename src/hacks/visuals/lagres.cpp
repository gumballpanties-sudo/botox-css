#include "lagres.h"
#include "../movement/movement.h"   // Vec3, NormalizeAngle

// ==== LAG RESOLVER -- why this is not the thread's resolver ==============================
// The hackvshack "accurate desync resolver" guesses an ENEMY'S REAL YAW so an aimbot can
// hit a spinning CSGO player. TF2 has no anti-aim and dih has no aimbot, and a yaw guess
// cannot make a stuttering player smooth anyway: the stutter is MISSING DATA. When a
// player's updates arrive late, his interpolator runs out of history, holds his last
// position for a few frames, then the next packet lands and he JUMPS. Freeze, jump,
// freeze, jump -- that is the twitch.
//
// So this resolves the LAG, not a desync:
//   * a stall (his rendered origin stops moving while he still had velocity) is carried
//     forward at his last measured velocity instead of frozen,
//   * when the real update lands, the drawn position EASES onto it over flLrSmooth
//     instead of snapping,
//   * anything bigger than g_lrMaxErr (teleport, respawn, spawn-in, a player who has
//     been still for a second) snaps -- a smear across half the map is worse than a jump.
//
// It is drawing only. The smoothed transform is written into the entity, the original
// SetupBones runs, and the real transform is written back before the hook returns. Nothing
// outside that window -- prediction, traces, the menu, the network -- ever sees it.

// ==== The five offsets, every one of them self-proving ===================================
// c_baseentity.h:1605-1624 is one run of members:
//     m_vecAbsOrigin | m_angAbsRotation | m_vecOldOrigin | m_vecOldAngRotation |
//     m_vecOrigin | m_iv_vecOrigin | m_angRotation | m_iv_angRotation |
//     m_rgflCoordinateFrame | m_vecNetworkOrigin
// The netvar table's "m_vecOrigin" is m_vecNetworkOrigin -- RECVINFO_NAME names one field
// and takes another's offset -- so every offset below is DERIVED from it and then CHECKED
// against something already trusted. A derivation that fails its check is not used, and
// says so in the log with a 0x0 sentinel; it never falls back to a plausible wrong number.
//   m_vecOrigin           netvar - 236   <- m_iv_vecOrigin.m_pValue points BACK at it
//   m_angRotation         X + 92 / +96   <- m_iv_angRotation.m_pValue points BACK at it
//   m_rgflCoordinateFrame netvar - 48    <- its translation column IS the origin
//   m_vecAbsOrigin        X - 48         <- equals m_vecOrigin on a parentless entity
//   m_angAbsRotation      X - 36         <- equals m_angRotation on a parentless entity

static constexpr int   k_originBack = 236;
static constexpr int   k_scan       = 512;   // bytes either side if a derivation misses
static constexpr int   k_maxTries   = 64;    // resolution attempts before giving up

// A stall longer than this is not lag, it is a dormant/idle player -- freeze, do not fly.
static constexpr float k_staleTime  = 1.0f;
// Two samples further apart than this are not one motion (alt-tab, load, map change).
static constexpr float k_maxDt      = 0.5f;
static constexpr float k_teleport   = 250.f; // units in one update = not movement
static constexpr float k_maxSpeed   = 1200.f;// carry no faster than a player can move
static constexpr float k_yawSnap    = 90.f;  // degrees of yaw error we refuse to smear

static int  s_offOrigin = 0, s_offAng = 0, s_offMat = 0, s_offAbsOrg = 0, s_offAbsAng = 0;
static bool s_resolved  = false, s_gaveUp = false;
static int  s_tries     = 0;

// ---- install/resolution line, stashed because the log is off by default -----------------
static char s_line[256] = {};
static bool s_lineNew   = false;

static void LrStash(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(s_line, sizeof(s_line), fmt, ap);
    va_end(ap);
    s_lineNew = true;
    DebugLog("%s", s_line);   // no-op unless the log is already on
}

void LagRes_LogInstallOnce()
{
    if (s_lineNew) { DebugLog("%s", s_line); s_lineNew = false; }
    else if (!s_resolved)
        DebugLog("lagres: offsets NOT RESOLVED yet (gaveup=%d tries=%d) -- feature is inert",
                 (int)s_gaveUp, s_tries);
}

// ---- guarded reads ----------------------------------------------------------------------
static bool LrReadVec(uintptr_t base, int off, Vec3& v)
{
    float x = 0.f, y = 0.f, z = 0.f;
    __try {
        const float* f = reinterpret_cast<const float*>(base + off);
        x = f[0]; y = f[1]; z = f[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!(fabsf(x) < 16384.f) || !(fabsf(y) < 16384.f) || !(fabsf(z) < 16384.f)) return false;
    v = Vec3(x, y, z);
    return true;
}

// X names a variable that an adjacent CInterpolatedVar was handed to interpolate, iff that
// interpolator's m_pValue (second field, right past the vtable) points back at X. 12 is
// sizeof(Vector)/sizeof(QAngle); 16 is the same with four bytes of alignment padding.
static bool LrIsInterpolated(uintptr_t base, int X)
{
    if (X < 16) return false;
    bool bHit = false;
    __try {
        bHit = *reinterpret_cast<uintptr_t*>(base + X + 12 + 8) == base + X
            || *reinterpret_cast<uintptr_t*>(base + X + 16 + 8) == base + X;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return bHit;
}

static bool LrVecNear(const Vec3& a, const Vec3& b, float eps)
{
    return fabsf(a.x - b.x) < eps && fabsf(a.y - b.y) < eps && fabsf(a.z - b.z) < eps;
}

// The matrix is m_rgflCoordinateFrame iff its translation column is the entity's origin and
// its three rotation rows are unit length. A stale (dirty) frame fails the first test, which
// is why resolution is retried across entities and frames instead of given up on at once.
static bool LrIsFrame(uintptr_t base, int off, const Vec3& vOrigin)
{
    float m[12] = {};
    __try { memcpy(m, reinterpret_cast<const void*>(base + off), sizeof(m)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    for (int i = 0; i < 12; ++i) if (!isfinite(m[i])) return false;
    if (!LrVecNear(Vec3(m[3], m[7], m[11]), vOrigin, 0.5f)) return false;
    for (int r = 0; r < 3; ++r)
    {
        const float len = sqrtf(m[r * 4 + 0] * m[r * 4 + 0] + m[r * 4 + 1] * m[r * 4 + 1]
                              + m[r * 4 + 2] * m[r * 4 + 2]);
        if (fabsf(len - 1.f) > 0.05f) return false;
    }
    return true;
}

// pEnt must be a live, parentless player -- the abs checks are only true with no move parent.
// bOwnBudget: a LagRes_RenderOrigin caller -- it never spends the resolver's tries (it has its
// own 16), so marker presses cannot switch the resolver off, nor the resolver's give-up them
static int s_roTries = 0;
static void LrResolve(uintptr_t base, bool bOwnBudget = false)
{
    if (s_resolved || g_offOrigin <= 0) return;
    if (bOwnBudget) { if (++s_roTries > 16) return; }
    else if (s_gaveUp) return;
    else if (++s_tries > k_maxTries)
    {
        s_gaveUp = true;
        LrStash("lagres: offsets NOT RESOLVED after %d tries -- origin=0x0, feature inert", s_tries);
        return;
    }

    // 1. m_vecOrigin, the RENDERED (interpolated) origin.
    const int iDerived = g_offOrigin - k_originBack;
    int X = 0;
    if (LrIsInterpolated(base, iDerived)) X = iDerived;
    else for (int d = 4; d <= k_scan && !X; d += 4)
    {
        if      (LrIsInterpolated(base, iDerived - d)) X = iDerived - d;
        else if (LrIsInterpolated(base, iDerived + d)) X = iDerived + d;
    }
    if (!X) return;
    Vec3 vOrigin{};
    if (!LrReadVec(base, X, vOrigin)) return;

    // 2. m_angRotation -- same identity, one interpolator further along.
    int A = 0;
    if      (LrIsInterpolated(base, X + 92)) A = X + 92;
    else if (LrIsInterpolated(base, X + 96)) A = X + 96;
    else for (int c = X + 80; c <= X + 120 && !A; c += 4)
        if (LrIsInterpolated(base, c)) A = c;

    // 3. m_rgflCoordinateFrame -- the transform SetupBones actually builds bones from.
    const int M = g_offOrigin - 48;
    if (!LrIsFrame(base, M, vOrigin)) return;   // stale/dirty frame: try again next entity

    // 4/5. the abs pair, verified against the two fields already proved.
    int AO = 0, AA = 0;
    Vec3 v{};
    if (LrReadVec(base, X - 48, v) && LrVecNear(v, vOrigin, 0.5f)) AO = X - 48;
    if (A && LrReadVec(base, X - 36, v))
    {
        Vec3 vAng{};
        if (LrReadVec(base, A, vAng) && LrVecNear(v, vAng, 0.5f)) AA = X - 36;
    }

    s_offOrigin = X; s_offAng = A; s_offMat = M; s_offAbsOrg = AO; s_offAbsAng = AA;
    s_resolved  = true;
    LrStash("lagres: resolved origin=0x%X ang=0x%X mat=0x%X absorg=0x%X absang=0x%X "
            "(netvar m_vecOrigin=0x%X, tries=%d)",
            X, A, M, AO, AA, g_offOrigin, s_tries);
}

// ==== the filter =========================================================================
struct LrTrack
{
    bool  bValid     = false;
    float flSample   = 0.f;   // currentTime of the last sample
    float flMoved    = 0.f;   // currentTime his origin last CHANGED
    Vec3  vTruth{};           // last rendered origin we were given
    Vec3  vVel{};             // units/s across his last real move
    Vec3  vSmooth{};
    float flYaw      = 0.f;   // last rendered yaw, wrapped
    float flYawMoved = 0.f;
    float flYawRate  = 0.f;   // deg/s
    float flYawSmooth = 0.f;
    bool  bStalling  = false;
};
static LrTrack s_track[65];

// stats, one line a second
static int   s_nApply = 0, s_nStall = 0, s_nSnap = 0;
static float s_flStatT = 0.f, s_flWorst = 0.f;

static void LrUpdate(LrTrack& tr, const Vec3& vOrigin, float flYaw, float t)
{
    const float dt = t - tr.flSample;
    if (!tr.bValid || dt <= 0.f || dt > k_maxDt)
    {
        tr = LrTrack{};
        tr.bValid = true; tr.flSample = t; tr.flMoved = t; tr.flYawMoved = t;
        tr.vTruth = tr.vSmooth = vOrigin;
        tr.flYaw  = tr.flYawSmooth = flYaw;
        return;
    }
    tr.flSample = t;

    // ---- position ----
    const Vec3  vStep = vOrigin - tr.vTruth;
    const float flStep = vStep.Length();
    if (flStep > 0.001f)
    {
        const float gap = t - tr.flMoved;
        if (flStep > k_teleport)              // respawn / teleporter / map change: hard cut
        {
            tr.vVel.Set(); tr.vSmooth = vOrigin; ++s_nSnap;
        }
        else if (gap > 0.f && gap <= k_staleTime)
        {
            Vec3 v = vStep * (1.f / gap);
            const float spd = v.Length();
            if (spd > k_maxSpeed) v *= (k_maxSpeed / spd);
            tr.vVel = v;
        }
        tr.vTruth  = vOrigin;
        tr.flMoved = t;
    }

    const float flStall = t - tr.flMoved;
    tr.bStalling = flStall > 0.f && flStall <= g_lrExtrapMs * 0.001f;
    Vec3 vTarget = tr.vTruth;
    if (tr.bStalling && flStall < k_staleTime && !tr.vVel.IsZero())
    {
        vTarget += tr.vVel * flStall;   // carry him instead of freezing him
        ++s_nStall;
    }

    const Vec3  vErr = vTarget - tr.vSmooth;
    const float flErr = vErr.Length();
    if (flErr > s_flWorst) s_flWorst = flErr;
    if (flErr > g_lrMaxErr) { tr.vSmooth = vTarget; ++s_nSnap; }
    else
    {
        const float tau = std::max(g_lrSmoothMs, 5.f) * 0.001f;
        tr.vSmooth += vErr * (1.f - expf(-dt / tau));
    }

    // ---- yaw: same shape, angles unwrapped ----
    const float dYaw = NormalizeAngle(flYaw - tr.flYaw);
    if (fabsf(dYaw) > 0.01f)
    {
        const float gap = t - tr.flYawMoved;
        if (gap > 0.f && gap <= k_staleTime) tr.flYawRate = std::clamp(dYaw / gap, -1080.f, 1080.f);
        tr.flYaw = flYaw;
        tr.flYawMoved = t;
    }
    float flYawTarget = tr.flYaw;
    const float flYawStall = t - tr.flYawMoved;
    if (flYawStall > 0.f && flYawStall <= g_lrExtrapMs * 0.001f && flYawStall < k_staleTime)
        flYawTarget += tr.flYawRate * flYawStall;

    const float flYawErr = NormalizeAngle(flYawTarget - tr.flYawSmooth);
    if (fabsf(flYawErr) > k_yawSnap) tr.flYawSmooth = flYawTarget;
    else
    {
        const float tau = std::max(g_lrSmoothMs, 5.f) * 0.001f;
        tr.flYawSmooth = NormalizeAngle(tr.flYawSmooth + flYawErr * (1.f - expf(-dt / tau)));
    }
}

// ==== apply / restore ====================================================================
// SetupBones re-enters (a bonemerged cosmetic sets its owner's bones up from inside its own
// call), so the saved transform is a stack, not one slot.
struct LrSave
{
    uintptr_t base = 0;
    Vec3      vOrigin{}, vAbsOrigin{};
    float     ang[3] = {}, absAng[3] = {};
    float     mat[12] = {};
    bool      bAbsOrg = false, bAng = false, bAbsAng = false;
};
// THREAD_LOCAL, not file-static. TF2 sets bones up from a threaded job, so two threads can be
// inside h_SetupBones at once -- and this is a LIFO: interleaved Begin/End calls pop each
// other's frames and End then writes 12 floats plus an origin and angles into the WRONG
// entity. Per-thread the stack can only ever be popped by the thread that pushed it, which is
// what LrRunOriginal's __try/__finally pairing already assumes.
static thread_local LrSave s_save[4];
static thread_local int    s_depth = 0;

static void LrAngleMatrix(const float ang[3], const Vec3& pos, float m[12])
{
    const float sp = sinf(DEG2RADF(ang[0])), cp = cosf(DEG2RADF(ang[0]));
    const float sy = sinf(DEG2RADF(ang[1])), cy = cosf(DEG2RADF(ang[1]));
    const float sr = sinf(DEG2RADF(ang[2])), cr = cosf(DEG2RADF(ang[2]));
    m[0] = cp * cy;  m[1] = sr * sp * cy - cr * sy;  m[2]  = cr * sp * cy + sr * sy;  m[3]  = pos.x;
    m[4] = cp * sy;  m[5] = sr * sp * sy + cr * cy;  m[6]  = cr * sp * sy - sr * cy;  m[7]  = pos.y;
    m[8] = -sp;      m[9] = sr * cp;                 m[10] = cr * cp;                 m[11] = pos.z;
}

bool LagRes_Begin(void* pEnt, int idx, float t)
{
    if (!g_lagResolver || !g_netvarsReady || g_offOrigin <= 0) return false;
    if (idx < 1 || idx > 64 || s_depth >= 4) return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(pEnt);

    // a parented entity's abs transform comes from its parent, not from these fields
    if (g_offMoveParent > 0)
    {
        unsigned h = 0xFFFFFFFFu;
        __try { h = *reinterpret_cast<unsigned*>(base + g_offMoveParent); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (h != 0xFFFFFFFFu) return false;
    }

    if (!s_resolved) { LrResolve(base); if (!s_resolved) return false; }

    Vec3 vOrigin{}, vAng{};
    if (!LrReadVec(base, s_offOrigin, vOrigin)) return false;
    if (s_offAng && !LrReadVec(base, s_offAng, vAng)) return false;

    LrTrack& tr = s_track[idx];
    if (tr.flSample != t) LrUpdate(tr, vOrigin, s_offAng ? vAng.y : 0.f, t);

    const bool bYaw = g_lrYaw && s_offAng != 0;
    Vec3 vNew = tr.vSmooth;
    float angNew[3] = { vAng.x, bYaw ? tr.flYawSmooth : vAng.y, vAng.z };
    if (LrVecNear(vNew, vOrigin, 0.02f) && (!bYaw || fabsf(NormalizeAngle(angNew[1] - vAng.y)) < 0.02f))
        return false;   // nothing to correct -- do not touch the entity at all

    LrSave& sv = s_save[s_depth];
    sv = LrSave{};
    sv.base = base;
    sv.vOrigin = vOrigin;

    __try
    {
        memcpy(sv.mat, reinterpret_cast<const void*>(base + s_offMat), sizeof(sv.mat));
        if (s_offAng)    { memcpy(sv.ang, &vAng, sizeof(sv.ang)); sv.bAng = true; }
        if (s_offAbsOrg) { LrReadVec(base, s_offAbsOrg, sv.vAbsOrigin); sv.bAbsOrg = true; }
        if (s_offAbsAng) { memcpy(sv.absAng, reinterpret_cast<const void*>(base + s_offAbsAng),
                                  sizeof(sv.absAng)); sv.bAbsAng = true; }

        // CalcAbsolutePosition rebuilds the frame from m_vecOrigin/m_angRotation when the
        // entity is dirty and leaves it alone when it is not, so BOTH have to say the same
        // thing or the correction lands on some frames and not others.
        float matNew[12];
        if (bYaw) LrAngleMatrix(angNew, vNew, matNew);
        else { memcpy(matNew, sv.mat, sizeof(matNew)); matNew[3] = vNew.x; matNew[7] = vNew.y; matNew[11] = vNew.z; }

        memcpy(reinterpret_cast<void*>(base + s_offMat), matNew, sizeof(matNew));
        memcpy(reinterpret_cast<void*>(base + s_offOrigin), &vNew, sizeof(Vec3));
        if (sv.bAng)    memcpy(reinterpret_cast<void*>(base + s_offAng), angNew, sizeof(angNew));
        if (sv.bAbsOrg) memcpy(reinterpret_cast<void*>(base + s_offAbsOrg), &vNew, sizeof(Vec3));
        if (sv.bAbsAng) memcpy(reinterpret_cast<void*>(base + s_offAbsAng), angNew, sizeof(angNew));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    ++s_depth;
    ++s_nApply;

    if (g_debugLogEnable && t - s_flStatT >= 1.f)
    {
        s_flStatT = t;
        DebugLog("lagres: apply=%d stall=%d snap=%d worsterr=%.1f smooth=%.0fms extrap=%.0fms "
                 "maxerr=%.0f yaw=%d off=0x%X",
                 s_nApply, s_nStall, s_nSnap, s_flWorst, g_lrSmoothMs, g_lrExtrapMs,
                 g_lrMaxErr, (int)bYaw, s_offOrigin);
        s_nApply = s_nStall = s_nSnap = 0; s_flWorst = 0.f;
    }
    return true;
}

// The interpolated origin (where the player is DRAWN), for code that has to match what the
// user aims at. bResolve: may spend one of this path's own 16 resolution attempts (key presses
// only -- a per-frame caller would burn them in a quarter second).
bool LagRes_RenderOrigin(void* pEnt, Vec3& out, bool bResolve)
{
    if (!pEnt || !g_netvarsReady || g_offOrigin <= 0) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(pEnt);
    if (!s_resolved)
    {
        if (!bResolve) return false;
        // resolution proves offsets against abs fields -- only true on a parentless entity
        if (g_offMoveParent > 0)
        {
            unsigned h = 0xFFFFFFFFu;
            __try { h = *reinterpret_cast<unsigned*>(base + g_offMoveParent); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            if (h != 0xFFFFFFFFu) return false;
        }
        LrResolve(base, true);
        if (!s_resolved) return false;
    }
    return LrReadVec(base, s_offOrigin, out);
}

void LagRes_End()
{
    if (s_depth <= 0) return;
    const LrSave& sv = s_save[--s_depth];
    if (!sv.base) return;
    __try
    {
        memcpy(reinterpret_cast<void*>(sv.base + s_offMat), sv.mat, sizeof(sv.mat));
        memcpy(reinterpret_cast<void*>(sv.base + s_offOrigin), &sv.vOrigin, sizeof(Vec3));
        if (sv.bAng)    memcpy(reinterpret_cast<void*>(sv.base + s_offAng), sv.ang, sizeof(sv.ang));
        if (sv.bAbsOrg) memcpy(reinterpret_cast<void*>(sv.base + s_offAbsOrg), &sv.vAbsOrigin, sizeof(Vec3));
        if (sv.bAbsAng) memcpy(reinterpret_cast<void*>(sv.base + s_offAbsAng), sv.absAng, sizeof(sv.absAng));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
