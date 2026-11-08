#include "world_fx2.h"
#include "../movement/movement.h"
#include "../movement/prediction.h"   // g_globalVars -- sun angle drift timing
#include "world_fx3.h"
#include "world_fx3_math.h"
#include "../aim/css_bridge.h"   // CssBridge_SunAngleSet/Restore -- env_shadow_control netvar drive
#include <algorithm>   // std::clamp -- remove flash opacity scale
#include <cmath>       // std::fmod -- sun angle yaw drift

// ==== remove visual recoil: CBasePlayer::CalcView ===================================
// Pattern verified against ref Hooks/CBasePlayer_CalcView.cpp on this
// same TF2 x64 client.dll (see world_fx2.h). m_vecPunchAngle's offset is resolved the
// same way every other netvar in this DLL is (netvars.cpp's FindNetworkTable/
// FindPropOffset, walking the live RecvTable) -- not a hardcoded offset, so a game
// update that moves the field cannot silently corrupt memory, only fail to find it.
CalcView_t o_CalcView = nullptr;

// __try/__except cannot share a frame with MiscHookGuard's destructor (MSVC C2712), so the
// SEH body lives in its own noinline function -- same split visuals.cpp's own
// h_DrawModelExecute/h_DrawModelExecute_impl already uses for the same reason.
__declspec(noinline) static void h_CalcView_impl(void* rcx, void* eyeOrigin, void* eyeAngles,
                                                  float& zNear, float& zFar, float& fov)
{
    if (g_eject || !g_removeVisualRecoilEnable || !rcx) {
        o_CalcView(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
        return;
    }

    static int s_offPunch = 0;
    if (s_offPunch == 0) {
        RecvTable* t = FindNetworkTable("DT_CSPlayer");   // was DT_TFPlayer (TF2 leftover): punch never found in CSS
        s_offPunch = t ? FindPropOffset(t, "m_vecPunchAngle") : -1;
        if (s_offPunch <= 0) s_offPunch = -1;
    }
    if (s_offPunch <= 0) {
        o_CalcView(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
        return;
    }

    float* punch = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(rcx) + s_offPunch);
    float saved[3] = {};
    bool captured = false;
    __try {
        saved[0] = punch[0]; saved[1] = punch[1]; saved[2] = punch[2];
        punch[0] = punch[1] = punch[2] = 0.f;
        captured = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { captured = false; }

    o_CalcView(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);

    if (captured) {
        __try { punch[0] = saved[0]; punch[1] = saved[1]; punch[2] = saved[2]; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void __fastcall h_CalcView(void* rcx, void* eyeOrigin, void* eyeAngles, float& zNear, float& zFar, float& fov)
{
    MiscHookGuard _g;
    h_CalcView_impl(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
}

void WorldFx2_InstallHooks()
{
    if (o_CalcView) return;
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;
    InstallPatternHook(hClient, "removals: CBasePlayer::CalcView",
                        "40 57 48 83 EC ? 44 8B 91",
                        (void*)h_CalcView, (void**)&o_CalcView);
    WorldFx3_Install();   // FireBullet + ViewRenderBeams (tracers, impacts, trail)
}

// ==== dlight: moved to world_fx3.cpp (per frame, enemies + local, botox edicts.cpp dlights) ====

// ==== sun angle: botox menu_tab_visuals.cpp:762-777 / edicts.cpp:858 sun_angle() ====================
// botox pokes CS:GO's cl_csm_rot_x/y/z + cl_csm_max_shadow_dist convars directly -- CSS has no CSM,
// so this drives the CSS-real sink instead (env_shadow_control netvars, see css_bridge.h's
// CssBridge_SunAngleSet banner for the exact chain). Same yaw-drift-over-time shape as botox: speed
// is deg/s, drift wraps 360, reset to 0 the instant speed goes back to 0 (botox edicts.cpp:913-914).
static void SunAngle_Tick()
{
    static bool  s_wasOn = false;
    static float s_lastTime = 0.f;
    static float s_drift = 0.f;
    static bool  s_mapLogged = false;

    const bool enabled = !g_eject && g_sunAngleEnable;
    if (!enabled) {
        if (s_wasOn) { CssBridge_SunAngleRestore(); s_mapLogged = false; }
        s_wasOn = false;
        s_drift = 0.f;
        return;
    }

    const float now = g_globalVars ? g_globalVars->realtime : 0.f;
    const float delta = now - s_lastTime;
    s_lastTime = now;

    if (g_sunAngleSpeed != 0.f && delta > 0.f && delta < 1.f)
        s_drift = std::fmod(s_drift + g_sunAngleSpeed * delta, 360.f);
    else if (g_sunAngleSpeed == 0.f)
        s_drift = 0.f;

    const Vec3 ang(g_sunAnglePitch, std::fmod(g_sunAngleYaw + s_drift, 360.f), g_sunAngleRoll);
    Vec3 dir;
    AngleVectors(ang, &dir);
    const float dirArr[3] = { dir.x, dir.y, dir.z };

    const bool ok = CssBridge_SunAngleSet(dirArr, g_sunAngleShadowDistance);
    if (!ok && !s_mapLogged) { DebugLog("SUN: no env_shadow_control entity on this map, sun angle inactive"); s_mapLogged = true; }
    s_wasOn = true;
}

void WorldFx2_Tick()
{
    WorldFx3_Tick();   // cvar pokes + ragdoll proxy, same 100ms cadence
    SunAngle_Tick();
}

// ==== smoke/blood/flash: live apply (not just on first FindMaterial lookup) ================
// Round-3 fix: cssourcex64/dllmain.cpp:45-55 calls I::MaterialSystem->FindMaterial() directly
// any time, not only from inside an engine FindMaterial hook -- so toggling the menu rows at
// runtime (no map change) must do the same. Fixed set of named materials, own interface handle
// (independent of world_fx3.cpp's s_matSys, which is `static` to that TU): FindMaterial 71,
// ColorModulate 28, SetMaterialVarFlag 29 (same slots WorldFx2_OnFindMaterial above already uses,
// cross-checked against world_fx3.cpp's banner, not re-guessed).
static void* s_fx2MatSys = nullptr;
static void* Fx2_FindMaterial(const char* name, const char* group)
{
    if (!s_fx2MatSys) s_fx2MatSys = GetInterface("materialsystem.dll", "VMaterialSystem082");
    if (!s_fx2MatSys || !name) return nullptr;
    __try { return VCall<void*>(s_fx2MatSys, 71, name, group, false, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static void Fx2_Modulate(void* m, float r, float g, float b)
{
    if (!m) return;
    __try { VCall<void>(m, 28, r, g, b); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void Fx2_SetNoDraw(void* m, bool on)
{
    if (!m) return;
    __try { VCall<void>(m, 29, 1 << 2, on); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Applied every frame while any of the three toggles are live (7 fixed FindMaterial lookups --
// the engine caches the IMaterial* internally, this is not a Mod_Walk-style full material scan)
// and once more when a toggle just turned off, to restore the neutral/visible state. g_eject
// restores too (MiscHookGuard-style convention every other FindMaterial consumer here follows).
static bool s_smokeWasOn = false, s_bloodWasOn = false, s_flashWasOn = false;
static void Fx2_ApplyNamedMaterials()
{
    const bool smokeOn = !g_eject && (g_customSmokeEnable || g_removeSmokeEnable);
    if (smokeOn || s_smokeWasOn)
    {
        void* m = Fx2_FindMaterial("particle/particle_smokegrenade1", nullptr);
        if (!g_eject && g_customSmokeEnable) Fx2_Modulate(m, g_customSmokeColor[0], g_customSmokeColor[1], g_customSmokeColor[2]);
        else                                 Fx2_Modulate(m, 1.f, 1.f, 1.f);
        Fx2_SetNoDraw(m, !g_eject && g_removeSmokeEnable);
    }
    s_smokeWasOn = smokeOn;

    const bool bloodOn = !g_eject && g_customBloodEnable;
    if (bloodOn || s_bloodWasOn)
    {
        static const char* const kBlood[] = { "effects/blood_gore", "effects/blood_drop", "effects/blood_puff", "effects/blood_core" };
        for (const char* name : kBlood)
        {
            void* m = Fx2_FindMaterial(name, nullptr);
            if (bloodOn) Fx2_Modulate(m, g_customBloodColor[0], g_customBloodColor[1], g_customBloodColor[2]);
            else         Fx2_Modulate(m, 1.f, 1.f, 1.f);
        }
    }
    s_bloodWasOn = bloodOn;

    const bool flashOn = !g_eject && g_removeFlashEnable;
    if (flashOn || s_flashWasOn)
    {
        const float scale = flashOn ? 1.f - std::clamp(g_removeFlashOpacity, 0.f, 100.f) * 0.01f : 1.f;
        for (const char* name : { "effects/flashbang", "effects/flashbang_white" })
            Fx2_Modulate(Fx2_FindMaterial(name, "ClientEffect textures"), scale, scale, scale);
    }
    s_flashWasOn = flashOn;
}

// ==== weapon sheen: moved to lsd_chams.cpp (round-5 fix) ====================================
// This file's old Fx2_SheenTick (an $envmaptint IMaterialVar sweep on the weapon's own material,
// no DIP intercept) could never show a travelling sheen band: $envmaptint only pulses the WHOLE
// model's ambient reflection uniformly, it has no per-pixel band to paint. botox's real
// hacks/visuals/weapon_sheen.cpp IS a DIP-shader intercept (forced-material-override carrier +
// envmap cubemap + animated mask texture + model-space UV sweep) -- same shape as this file's
// neighbour lsd_chams.cpp's lsd/two-tone chams layers. See lsd_chams.h's WeaponSheen_Begin/End
// and the report (NEEDS FROM OTHERS: the viewmodel draw call site + the carrier material).
void WorldFx2_Frame()
{
    Fx2_ApplyNamedMaterials();
}

// ==== world/prop texture (colour) replace + weather particle colour =================
// Appended onto the already-hooked h_FindMaterial (visuals.cpp, VMaterialSystem082 slot
// 71) -- no new hook. pTextureGroup is the engine's own classification
// (public/texture_group_names.h: TEXTURE_GROUP_WORLD = "World textures",
// TEXTURE_GROUP_MODEL = "Model textures"), not a name-pattern guess. ColorModulate is
// IMaterial vtable slot 28 (public/materialsystem/imaterial.h virtual order, identical in
// ref SDK/Definitions/Main/IMaterial.h -- cross-checked, not guessed).
void WorldFx2_OnFindMaterial(void* pMaterial, const char* pTextureGroup, const char* pName)
{
    (void)pTextureGroup;   // world/prop/skybox modulation is world_fx3.cpp's live material walk now
    if (!pMaterial || g_eject) return;

    __try {
        // Rain/snow sprite: h_FindMaterial already redirects the NAME for snow mode before
        // calling original, so pName here is "particle/rain" or "particle/snow" either way.
        if (g_weatherTintEnable && WorldFx2_IsWeatherSprite(pName))
            VCall<void>(pMaterial, 28, g_weatherTintColor[0], g_weatherTintColor[1], g_weatherTintColor[2]);

        // botox :823-849 custom smoke/blood colour + :1052-1072 remove smoke/flash. Material names
        // confirmed in css-community-master: weapon_smokegrenade.cpp's client particle system draws
        // "particle/particle_smokegrenade1" (same name cssourcex64/dllmain.cpp:47 NO_DRAWs on the
        // live game), fx_cs_blood.cpp:23-24,348 precaches "effects/blood_gore"/"blood_drop"/
        // "blood_puff"/"blood_core", hud flashbang whiteout is "effects/flashbang" +
        // "effects/flashbang_white" (cssourcex64/dllmain.cpp:45-46). This hook-time apply is now
        // the FALLBACK path only: Fx2_ApplyNamedMaterials() below (WorldFx2_Frame) actively pokes
        // these same names every frame via its own FindMaterial call (cssourcex64's own approach,
        // callable any time -- round-3 fix), so a toggle takes effect live, no map change needed.
        // This block still matters for the instant the ENGINE itself first looks the material up
        // (precache / particle init), ahead of this frame's apply pass.
        if (pName && !_strnicmp(pName, "particle/particle_smokegrenade", 31)) {
            if (g_customSmokeEnable)
                VCall<void>(pMaterial, 28, g_customSmokeColor[0], g_customSmokeColor[1], g_customSmokeColor[2]);
            if (g_removeSmokeEnable)   // both combo modes = NO_DRAW, see u_world.h banner
                VCall<void>(pMaterial, 29, 1 << 2, true);
        } else if (pName && (!_stricmp(pName, "effects/blood_gore") || !_stricmp(pName, "effects/blood_drop") ||
                              !_stricmp(pName, "effects/blood_puff") || !_stricmp(pName, "effects/blood_core"))) {
            if (g_customBloodEnable)
                VCall<void>(pMaterial, 28, g_customBloodColor[0], g_customBloodColor[1], g_customBloodColor[2]);
        } else if (pName && (!_stricmp(pName, "effects/flashbang") || !_stricmp(pName, "effects/flashbang_white"))) {
            if (g_removeFlashEnable) {
                const float scale = 1.f - std::clamp(g_removeFlashOpacity, 0.f, 100.f) * 0.01f;
                VCall<void>(pMaterial, 28, scale, scale, scale);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
