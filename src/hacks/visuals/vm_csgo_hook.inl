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
static float     s_vmCsPitch = 0.f, s_vmCsZ = 0.f, s_vmCsRight = 0.f;
static VmCsLower s_vmCsLower;
static VmCsDip   s_vmCsDip;
static VmBobState s_vmCsBobState;

static void VmCs_EyeAdjustSafe(void* owner, float eye[3], float ang[3])
{
    __try {
        const GlobalVars_t* gl = VmCs_Globals();
        if (!gl || !owner || g_offVelocity <= 0 || g_offFlags <= 0) return;
        int& s_frame = s_vmCsFrame;
        float& s_pitch = s_vmCsPitch;
        float& s_z = s_vmCsZ;
        float& s_right = s_vmCsRight;
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

            // bob: speed2d off the same velocity read above, maxSpeed left 0 (CS:GO's fixed
            // 150 u/s path, vm_csgo.h banner -- CS:S's own m_flMaxspeed netvar isn't bridged
            // into visuals.cpp, same TF adaptation note this header already carries).
            if (g_vmCsBob && !g_vmRemoveBob)
            {
                const float speed2d = sqrtf(vel[0] * vel[0] + vel[1] * vel[1]);
                VmCsBobCfg c;
                c.cycle = g_vmCsBobCycle; c.up = g_vmCsBobUp; c.amtVert = g_vmCsBobVert; c.amtLat = g_vmCsBobLat; c.lowerAmt = g_vmCsBobLower;
                VmCs_CalcBob(s_vmCsBobState, speed2d, onGround, 0.f, gl->curtime, gl->absoluteframetime, c);
            }
            else
                s_vmCsBobState = VmBobState{};

            // shift left/right: botox's real weapon_csbase.cpp:3399 signal -- CWeaponCSBase::
            // GetInaccuracy (vtable slot 382), via CssBridge_GetInaccuracy (css_bridge.cpp,
            // round-4: UpdateAccuracyPenalty(384) + GetInaccuracy(382), same disasm-verified
            // slots nospread already calls, css.h banner). Sign picks the slider by current
            // strafe direction (right-vector component of velocity), since botox's two sliders
            // are a left shift and a right shift, not a signed pair.
            s_right = 0.f;
            if ((g_wdVmShiftLeft > 0.f || g_wdVmShiftRight > 0.f))
            {
                const int localIdx = GetLocalPlayerIndex();
                const int weaponEnt = localIdx > 0 ? CssBridge_GetActiveWeaponEnt(localIdx) : 0;
                const float inaccuracy = weaponEnt > 0 ? CssBridge_GetInaccuracy(weaponEnt) : 0.f;
                const float sy = sinf(ang[1] * 3.14159265f / 180.f), cy = cosf(ang[1] * 3.14159265f / 180.f);
                const float rightDotVel = vel[0] * sy - vel[1] * cy;   // right = (sin yaw, -cos yaw, 0)
                const float scale = VmCs_Clamp(inaccuracy * 10.f, 0.f, 1.f);
                s_right = scale * (rightDotVel >= 0.f ? g_wdVmShiftRight : -g_wdVmShiftLeft);
            }
        }
        ang[0] -= s_pitch;
        eye[2] -= s_z - s_vmCsBobState.vert;
        const float lateral = s_right + s_vmCsBobState.lat;
        if (lateral != 0.f)
        {
            const float sy = sinf(ang[1] * 3.14159265f / 180.f), cy = cosf(ang[1] * 3.14159265f / 180.f);
            eye[0] += sy * lateral;
            eye[1] += -cy * lateral;
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
    DebugLog("vmcs: gpGlobals(g_globalVars)=%p offVelocity=%d offFlags=%d offMaxspeed=%d (hook-free: bob/lower/dip run from h_CalcViewModelView)",
             (void*)g_globalVars, g_offVelocity, g_offFlags, g_offMaxspeed);
}
