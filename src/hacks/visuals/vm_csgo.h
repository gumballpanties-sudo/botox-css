#pragma once
// CS:GO viewmodel bob / gun lower / landing dip, ported 1:1 from csgosrc2019, engine-free.
// SAME FILE in SimpleVisual and fpsoverlay (src/hacks/visuals/vm_csgo.h) -- edit both.
// Gate test: tests/vmcsgo_test.cpp.
//
// Sources (csgosrc2019/src/game/shared):
//   cstrike15/weapon_csbase.cpp  CalcViewModelBobHelper   -> VmCs_CalcBob
//   predicted_viewmodel.cpp      ApplyViewModelPitchAndDip -> VmCs_GunLower
//   baseplayer_shared.cpp        CBasePlayer::CalcViewBob  -> VmCs_LandDip
// TF2 applies the bob with AddViewModelBobHelper, byte-for-byte the CS:GO helper, so only
// the CALC half is replaced (hook on TF's CalcViewModelBobHelper, see VmCs_* glue).
//
// TF adaptations:
//   - weapon GetMaxSpeed() -> player m_flMaxspeed (TF speed lives on the class, not the gun)
//   - GetRunBobScale() -> 1 (TF weapons have no such stat)
//   - accuracy shift (cl_viewmodel_shift_*) -> 0: TF has no dynamic inaccuracy to read
//   - IsZoomed() skip of the lowering -> not needed, TF hides the viewmodel when scoped
//   - landing dip moves the viewmodel only (CS:GO moved the whole eye)
#include <cmath>

// == TF2 BobState_t (client.dll CalcViewModelBobHelper disasm: +0/+4/+8/+C/+10)
struct VmBobState
{
    float bobTime;
    float lastBobTime;
    float lastSpeed;
    float vert;
    float lat;
};

struct VmCsBobCfg
{
    float cycle    = 0.98f;  // cl_bobcycle      (0.1..2)
    float up       = 0.5f;   // cl_bobup
    float amtVert  = 0.25f;  // cl_bobamt_vert   (0.1..2)
    float amtLat   = 0.4f;   // cl_bobamt_lat    (0.1..2)
    float lowerAmt = 21.f;   // cl_bob_lower_amt (5..30)
};

inline float VmCs_Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// mathlib Approach(target, value, speed)
inline float VmCs_Approach(float target, float value, float speed)
{
    const float delta = target - value;
    if (delta > speed)       value += speed;
    else if (delta < -speed) value -= speed;
    else                     value = target;
    return value;
}

// speed2d = |velocity.xy|, maxSpeed <= 0 = no weapon (CS:GO fixed 150 path)
// rawVert/rawLat = BobState m_flRawVerticalBob/m_flRawLateralBob (pre-clamp), read by cs_hud_scope.cpp
// gunAccPos = VmCs_AccShift result (CS:GO m_flGunAccuracyPosition), 0 = no shift
inline void VmCs_CalcBob(VmBobState& s, float speed, bool onGround, float maxSpeed,
                         float curtime, float frametime, const VmCsBobCfg& c,
                         float* rawVert = nullptr, float* rawLat = nullptr, float gunAccPos = 0.f)
{
    if (!frametime) return;   // "just use old value"

    // cvar-free guards CS:GO got from its cvar ranges: never divide by 0
    const float bobUp = VmCs_Clamp(c.up, 0.01f, 0.99f);

    float flmaxSpeedDelta = (curtime - s.lastBobTime) * 640.f;
    if (flmaxSpeedDelta < 0.f) flmaxSpeedDelta = 0.f;

    // don't allow too big speed changes
    speed = VmCs_Clamp(speed, s.lastSpeed - flmaxSpeedDelta, s.lastSpeed + flmaxSpeedDelta);
    speed = VmCs_Clamp(speed, -320.f, 320.f);
    s.lastSpeed = speed;

    // when the player is moving forward, the gun lowers a bit
    // CS:GO's clamp(flSpeedFactor, 0, 0.5) throws its result away -> unclamped, kept 1:1
    const float flSpeedFactor = speed * 0.006f;
    const float flLowerAmt = c.lowerAmt * 0.2f;
    float flRunAddAmt = flLowerAmt * flSpeedFactor;

    const float bob_offset = speed / 320.f;   // RemapVal(speed, 0, 320, 0, 1)
    s.bobTime += (curtime - s.lastBobTime) * bob_offset;
    s.lastBobTime = curtime;

    float flBobCycle;
    const float flGunAccPos = gunAccPos;
    if (maxSpeed > 0.f)
    {
        flRunAddAmt *= 1.f;   // GetRunBobScale
        flBobCycle = (((1000.f - maxSpeed) / 3.5f) * 0.001f) * c.cycle;
    }
    else
        flBobCycle = (((1000.f - 150.f) / 3.5f) * 0.001f) * c.cycle;
    if (flBobCycle < 0.01f) flBobCycle = 0.01f;   // 1000+ u/s or cycle <= 0

    constexpr float kPi = 3.14159265358979323846f;

    // vertical bob
    float cycle = s.bobTime - (int)(s.bobTime / flBobCycle) * flBobCycle;
    cycle /= flBobCycle;
    if (cycle < bobUp) cycle = kPi * cycle / bobUp;
    else               cycle = kPi + kPi * (cycle - bobUp) / (1.f - bobUp);

    // if we're in the air, slow our bob down a bit
    const float flBobMultiplier = onGround ? 0.00625f : 0.00125f;

    s.vert = speed * (flBobMultiplier * c.amtVert);
    s.vert = (float)(s.vert * 0.3 + s.vert * 0.7 * std::sin((double)cycle));
    if (rawVert) *rawVert = s.vert;
    s.vert = VmCs_Clamp(s.vert - (flRunAddAmt - (flGunAccPos * 0.2f)), -7.f, 4.f);

    // lateral bob -- CS:GO's operator precedence kept: (int)(t / cycle * 2) * cycle * 2
    cycle = s.bobTime - (int)(s.bobTime / flBobCycle * 2) * flBobCycle * 2;
    cycle /= flBobCycle * 2;
    if (cycle < bobUp) cycle = kPi * cycle / bobUp;
    else               cycle = kPi + kPi * (cycle - bobUp) / (1.f - bobUp);

    s.lat = speed * (flBobMultiplier * c.amtLat);
    s.lat = (float)(s.lat * 0.3 + s.lat * 0.7 * std::sin((double)cycle));
    if (rawLat) *rawLat = s.lat;
    s.lat = VmCs_Clamp(s.lat + flGunAccPos * 0.25f, -8.f, 8.f);
}

// CS:GO accuracy shift, weapon_csbase.cpp:3393-3440 1:1. acc = GetInaccuracy (or crouch base when
// ducked), standBase = GetInaccuracyStand. valid=false (reload / elites / no weapon) -> eases to 0.
// Returns m_flGunAccuracyPosition: < 0 = left (more accurate than standing), > 0 = right.
struct VmCsShift { float pos = 0.f; };
inline float VmCs_AccShift(VmCsShift& st, bool valid, float acc, float standBase, bool sniper,
                           float leftAmt, float rightAmt, float frametime)
{
    float diff = 0.f;
    if (valid)
    {
        float mult = 1.f;
        if (acc < standBase) mult = (sniper ? 0.15f : 18.f) * leftAmt;
        else
        {
            if (acc > 0.082f) acc = 0.082f;
            mult *= rightAmt;
        }
        diff = (acc - standBase) * mult;
        if (diff < -0.1f) diff = -0.1f;
    }
    const float target = diff * 80.f;
    st.pos = VmCs_Approach(target, st.pos, std::fabs((target - st.pos) * frametime) * 4.f);
    return st.pos;
}

// TF runs the calc TWICE per frame for the off-hand viewmodel (index 1): once through the
// weapon's AddViewmodelBob and once through CTFViewModel::AddViewModelBob (tf_viewmodel.cpp),
// both on the SAME BobState -- tf_weaponbase.cpp GetBobState reads it off the VIEWMODEL, not the
// weapon. Each call is followed by AddViewModelBobHelper, so the spy watch / dead ringer bobbed
// 2x the main viewmodel (same shape as the viewmodel-offset double shift in h_CalcViewModelView).
// One slot is enough: the two calls are back to back inside one CalcViewModelView.
// Repeat call -> caller zeroes vert/lat so the extra apply adds nothing.
struct VmCsDedupe { const void* state = nullptr; int frame = -1; };
inline bool VmCs_IsRepeatCall(VmCsDedupe& d, const void* state, int frame)
{
    if (d.state == state && d.frame == frame) return true;
    d.state = state;
    d.frame = frame;
    return false;
}

// ApplyViewModelPitchAndDip: gun drops (z) and pitches while airborne. Approach step is per CALL, not per
// second (CS:GO's own frame-rate dependence). Returns the pitch/z to subtract.
struct VmCsLower { float offset = 0.f; };
inline void VmCs_GunLower(VmCsLower& st, bool airborne, float angle, float speed,
                          float& pitchSub, float& zSub)
{
    st.offset = VmCs_Approach(airborne ? angle : 0.f, st.offset, speed);
    pitchSub = st.offset * 0.2f;   // vecNewAngles -= vecLoweredAngles * 0.2
    zSub     = st.offset * 0.4f;   // vecNewOrigin.z -= vecLoweredAngles.x * 0.4
}

// CalcViewBob landing dip. fallVel = m_flFallVelocity (-vz airborne, 0 grounded).
// Returns how far down (units) to push this frame.
struct VmCsDip { bool inLanding = false; float landingTime = 0.f; float oldFallVel = 0.f; };
// dipTime = seconds to the bottom (CS:GO hardcodes 0.25); whole dip lasts 1.5x that
inline float VmCs_LandDip(VmCsDip& st, float fallVel, float curtime, int dipAmt, float dipTime = 0.25f)
{
    if (dipTime < 0.01f) dipTime = 0.01f;
    constexpr float kFatalFall = 1024.f;   // PLAYER_FATAL_FALL_SPEED
    if (fallVel <= 0.1f && st.oldFallVel > 10.f && st.oldFallVel <= kFatalFall && !st.inLanding)
    {
        st.inLanding = true;
        st.landingTime = curtime;
    }

    float down = 0.f;
    if (st.inLanding)
    {
        float landseconds = curtime - st.landingTime;
        if (landseconds < 0.f) landseconds = 0.f;
        const float x = landseconds / dipTime;
        const float landFraction = x * x * (3.f - 2.f * x);   // SimpleSpline, clamp discarded 1:1

        const float flDipAmount = (1.f / st.oldFallVel) * 0.1f;
        const int dipHighOffset = 64;
        const int dipLowOffset = dipHighOffset - dipAmt;
        float z = ((dipLowOffset - flDipAmount) * landFraction) + (dipHighOffset * (1.f - landFraction));
        if (z > dipHighOffset)
        {
            z = (float)dipHighOffset;
            st.inLanding = false;
        }
        down = dipHighOffset - z;
    }

    // stop when our eyes get back to default
    if (st.inLanding && -down - 0.001f >= 0.f) st.inLanding = false;
    if (!st.inLanding) st.oldFallVel = fallVel;
    return down;
}
