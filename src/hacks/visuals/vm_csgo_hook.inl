// CS:GO viewmodel glue: hook + eye adjust around vm_csgo.h. Included by visuals.cpp.
// SAME FILE in SimpleVisual and fpsoverlay (src/hacks/visuals/vm_csgo_hook.inl) -- edit both.
// Before including, define VMCS_REMOVE_BOB as the project's "remove bob" bool.
// Needs: g_eject, g_offVelocity/g_offFlags/g_offMaxspeed, DebugLog, g_globalVars/GlobalVars_t
// (prediction.h, included by the caller -- visuals.cpp), and the g_vmCs* globals.
#include "vm_csgo.h"

// ---- gpGlobals ----
// CalcViewModelBobHelper (TF2's compiled helper this file originally hooked) does not exist in
// CS:S's client.dll -- confirmed, PatternScan for it always misses here, so o_VmCsBobHelper never
// resolved and gpGlobals (which that scan also located, as a byte offset from the hook site) never
// resolved either. VmCs_EyeAdjustSafe computes bob/lower/dip itself (no hook needed, see below) but
// still silently depended on that dead scan for curtime/frametime/framecount, which is why bob/
// lower/dip never ran in this port. Fix: use g_globalVars (prediction.h), already resolved by
// Movement_Install's own verified engine.dll sig (movement.cpp:165-169) before VmCsgo_Install runs
// (thread.cpp: Movement_Install at line 297, VmCsgo_Install at 308) -- same CGlobalVarsBase layout,
// first 5 fields (realtime/framecount/absoluteframetime/curtime/frametime) identical.
static GlobalVars_t* VmCs_Globals()
{
    __try { return g_globalVars; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// ---- gun lower on jump + landing dip + bob + shift: CS:GO moves the eye BEFORE the base
// CalcViewModelView ----
// CalcViewModelView runs once per viewmodel (2) per frame: state steps once per framecount.
//
// Round-3 fix: no hook on CalcViewModelBobHelper needed or possible in CS:S (that compiled TF2
// helper does not exist here). g_offVelocity/g_offFlags are fine (netvars.cpp's "11-03 CSS fix"
// already points them at CCSPlayer's own m_vecVelocity/m_fFlags). Gun-lower/landing-dip already
// lived here as a plain eye-space nudge (no hook needed beyond h_CalcViewModelView itself); bob
// and shift join them the same way, computed straight from vm_csgo.h's engine-free math every
// frame, fed by g_globalVars (see VmCs_Globals above -- round-4 fix, that's the part that was
// actually still broken).
static int       s_vmCsFrame = -1;
static float     s_vmCsPitch = 0.f, s_vmCsZ = 0.f;
static VmCsLower s_vmCsLower;
static VmCsDip   s_vmCsDip;
static VmBobState s_vmCsBobState;
static VmCsShift s_vmCsShift;

// ---- CS:GO accuracy shift input (weapon_csbase.cpp:3393). CSS weapon scripts are encrypted and
// GetCSWpnData is unresolved, so GetInaccuracyStand/Crouch = lowest live GetInaccuracy seen per weapon
// while grounded + unscoped (penalty decays to 0 at rest, so the min IS the stand/crouch value).
// ponytail: silencer on/off share one base (m4/usp read the lower), real fix = resolve GetCSWpnData.
static float s_vmCsAccBase[64][2];   // [weapon id][ducked], 0 = not seen yet
static float VmCs_ShiftSafe(uintptr_t player, bool onGround, float frametime)
{
    bool valid = false, sniper = false;
    float acc = 0.f, stand = 0.f;
    __try {
        const int localIdx = GetLocalPlayerIndex();
        const int w = localIdx > 0 ? CssBridge_GetActiveWeaponEnt(localIdx) : 0;
        const int id = w > 0 ? CssBridge_GetWeaponID(w) : 0;
        // CS:GO: no shift while reloading or on elites
        if (id > 0 && id < 64 && !CssBridge_ReloadingOrElites(w))
        {
            const bool ducked = (g_offDucked > 0 && *reinterpret_cast<const bool*>(player + g_offDucked)) ||
                                (g_offDucking > 0 && *reinterpret_cast<const bool*>(player + g_offDucking));
            const float live = CssBridge_GetInaccuracy(w);
            float& base = s_vmCsAccBase[id][ducked ? 1 : 0];
            if (onGround && live > 0.f && !CssBridge_IsScoped(localIdx) && (base <= 0.f || live < base)) base = live;
            stand = s_vmCsAccBase[id][0];
            const float crouch = s_vmCsAccBase[id][1];
            acc = ducked ? (crouch > 0.f ? crouch : live) : live;   // CS:GO: ducked reads the crouch base
            sniper = CssBridge_IsSniperWeapon(w);
            valid = stand > 0.f;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
    return VmCs_AccShift(s_vmCsShift, valid, acc, stand, sniper, g_wdVmShiftLeft, g_wdVmShiftRight, frametime);
}

static void VmCs_EyeAdjustSafe(void* owner, float eye[3], float ang[3])
{
    __try {
        const GlobalVars_t* gl = VmCs_Globals();
        if (!gl || !owner || g_offVelocity <= 0 || g_offFlags <= 0) return;
        int& s_frame = s_vmCsFrame;
        float& s_pitch = s_vmCsPitch;
        float& s_z = s_vmCsZ;
        VmCsLower& s_lower = s_vmCsLower;
        VmCsDip& s_dip = s_vmCsDip;
        if (gl->framecount != s_frame)
        {
            s_frame = gl->framecount;
            const uintptr_t base = reinterpret_cast<uintptr_t>(owner);
            const float* vel = reinterpret_cast<const float*>(base + g_offVelocity);
            const float vz = vel[2];
            const bool onGround = (*reinterpret_cast<const int*>(base + g_offFlags) & 1) != 0;

            float pitchSub = 0.f, zSub = 0.f;
            if (g_vmCsGunLower) VmCs_GunLower(s_lower, !onGround, g_vmCsGunLowerAngle, g_vmCsGunLowerSpeed, pitchSub, zSub);
            else                s_lower.offset = 0.f;

            float dip = 0.f;
            if (g_vmCsLandDip) dip = VmCs_LandDip(s_dip, onGround ? 0.f : -vz, gl->curtime, (int)g_vmCsLandDipAmt, g_vmCsLandDipTime);
            else               s_dip = VmCsDip{};

            s_pitch = pitchSub;
            s_z = zSub + dip;

            // bob: CS:GO feeds weapon GetMaxSpeed; CSS player m_flMaxspeed = active weapon's speed
            // (1 in freezetime -> fixed 150 path). Shift lives INSIDE bob like CS:GO
            // (flGunAccPos) and botox (cl_viewmodel_shift_* gated on bob_on).
            if (g_vmCsBob && !g_vmRemoveBob)
            {
                const float speed2d = sqrtf(vel[0] * vel[0] + vel[1] * vel[1]);
                float maxSpeed = g_offMaxspeed > 0 ? *reinterpret_cast<const float*>(base + g_offMaxspeed) : 0.f;
                if (!(maxSpeed > 1.f && maxSpeed < 1000.f)) maxSpeed = 0.f;
                const float accPos = VmCs_ShiftSafe(base, onGround, gl->absoluteframetime);
                VmCsBobCfg c;
                c.cycle = g_vmCsBobCycle; c.up = g_vmCsBobUp; c.amtVert = g_vmCsBobVert; c.amtLat = g_vmCsBobLat; c.lowerAmt = g_vmCsBobLower;
                VmCs_CalcBob(s_vmCsBobState, speed2d, onGround, maxSpeed, gl->curtime, gl->absoluteframetime, c,
                             nullptr, nullptr, accPos);
                static bool s_logged = false;
                if (!s_logged && speed2d > 50.f)
                {
                    s_logged = true;
                    DebugLog("vmcs: live bob speed=%.0f maxspeed=%.0f ground=%d vert=%.2f lat=%.2f acc=%.2f",
                             speed2d, maxSpeed, (int)onGround, s_vmCsBobState.vert, s_vmCsBobState.lat, accPos);
                }
            }
            else
            {
                s_vmCsBobState = VmBobState{};
                s_vmCsShift = VmCsShift{};
            }
        }
        ang[0] -= s_pitch;
        eye[2] -= s_z;

        // bob = AddViewModelBobHelper 1:1 (tf_weaponbase.cpp:4703, CS:GO same helper). vert carries
        // the run lower (~-6 at 250 u/s): raw on eye z it sank the gun off screen -- that was the break.
        const float vb = s_vmCsBobState.vert, lb = s_vmCsBobState.lat;
        if (vb != 0.f || lb != 0.f)
        {
            constexpr float d2r = 3.14159265f / 180.f;
            const float sp = sinf(ang[0] * d2r), cp = cosf(ang[0] * d2r);
            const float sy = sinf(ang[1] * d2r), cy = cosf(ang[1] * d2r);
            const float sr = sinf(ang[2] * d2r), cr = cosf(ang[2] * d2r);
            const float fwd[3] = { cp * cy, cp * sy, -sp };
            const float rgt[3] = { -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp };
            for (int i = 0; i < 3; ++i) eye[i] += fwd[i] * vb * 0.4f + rgt[i] * lb * 0.2f;
            eye[2] += vb * 0.1f;
            ang[2] += vb * 0.5f;
            ang[0] -= vb * 0.4f;
            ang[1] -= lb * 0.3f;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// No hook to install in CS:S (see banner above) -- h_CalcViewModelView calls VmCs_EyeAdjustSafe
// directly every frame. This is just the one-time log so inject-time debug.log shows whether
// gpGlobals (via g_globalVars) is actually up by the time the viewmodel hook needs it.
static void VmCs_Install(HMODULE client)
{
    (void)client;
    static bool logged = false;
    if (logged) return;
    logged = true;
    // vel/maxspeed resolve later (Movement_ResolveExtraNetvars) -- 0 here is expected
    DebugLog("vmcs: gpGlobals(g_globalVars)=%p (hook-free: bob/lower/dip run from h_CalcViewModelView)",
             (void*)g_globalVars);
}
