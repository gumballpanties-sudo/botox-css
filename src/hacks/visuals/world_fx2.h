#pragma once
#include "../../globals/globals.h"
#include <cstring>

// Unit D2 port: the features Unit D pass 1 listed BLOCKED (new hook, or infra it did not
// own) and this unit's own brief covers -- remove visual recoil, dlight, sun angle, custom
// weather particle colour, world/prop texture (colour) replace. Reflections, remove hud
// elements, custom blood colour, minecraft hud and web on surface stay blocked here too --
// see the Unit D2 return report for why each one is blocked.

// CBasePlayer::CalcView, client.dll, pattern-hooked (new -- distinct function address, not
// a second hook on an owned vtable slot). Sig verified against the SDK
// Hooks/CBasePlayer_CalcView.cpp on this same TF2 x64 client.dll, not guessed. Zeroes the
// CBasePlayer::m_vecPunchAngle netvar for the duration of the original call (which folds
// punch into eyeAngles), then restores it -- the netvar itself, and therefore every other
// reader of it (aim, HUD), is untouched outside that one call.
using CalcView_t = void(__fastcall*)(void*, void*, void*, float&, float&, float&);
extern CalcView_t o_CalcView;
void __fastcall h_CalcView(void* rcx, void* eyeOrigin, void* eyeAngles, float& zNear, float& zFar, float& fov);

// Installs the CalcView pattern hook. Called once from PollThread, same place
// WorldFx_InstallViewHook installs slot 16 on the same interface family.
void WorldFx2_InstallHooks();

// 100ms tick (visuals.cpp's UpdateVisualsCvars, same cadence world_fx.cpp's WorldFx_Tick
// rides) -- dlight (IVEfx::CL_AllocDlight, slot 4, "VEngineEffects001") and sun angle
// (env_sun's DT_Sun::m_vDirection netvar, resolved by name like every other netvar in this
// DLL, not a hardcoded offset).
void WorldFx2_Tick();

// Main thread, once a frame (world_fx.cpp h_OverrideView): named material pokes (smoke/blood/
// flash). Weapon sheen moved to lsd_chams.h's WeaponSheen_Begin/End (round-5 fix, real DIP port).
void WorldFx2_Frame();

// Pure, engine-free: classifies FindMaterial's own pTextureGroup string (engine-assigned,
// public/texture_group_names.h -- not a name-pattern guess). 0 = neither, 1 = world
// textures, 2 = model (prop) textures. Header-only so tests/worldfx2_test.cpp can check it
// without linking this DLL's engine globals.
inline int WorldFx2_ClassifyTextureGroup(const char* pTextureGroup)
{
    if (!pTextureGroup) return 0;
    if (strcmp(pTextureGroup, "World textures") == 0) return 1;
    if (strcmp(pTextureGroup, "Model textures") == 0) return 2;
    return 0;
}

// Pure: is this FindMaterial name the rain/snow sprite (post weather-redirect, so always
// "particle/rain" or "particle/snow" either way)?
inline bool WorldFx2_IsWeatherSprite(const char* pName)
{
    return pName && (strcmp(pName, "particle/rain") == 0 || strcmp(pName, "particle/snow") == 0);
}

// Pure: 0..1 float to an 0..255 byte, clamped -- NaN/out-of-range input must not wrap or
// produce an undefined cast (dlight_t's ColorRGBExp32 fields are plain bytes).
inline unsigned char WorldFx2_ColorByte(float f)
{
    if (!(f > 0.f)) return 0;          // false for NaN too
    if (f >= 1.f) return 255;
    return (unsigned char)(f * 255.f);
}

// Appended to the existing h_FindMaterial hook (visuals.cpp, already hooks
// VMaterialSystem082 slot 71 for the snow sprite swap) -- world/prop texture colour replace
// and weather particle colour. pTextureGroup is the engine's OWN classification
// (TEXTURE_GROUP_WORLD = "World textures", TEXTURE_GROUP_MODEL = "Model textures",
// public/texture_group_names.h) -- not a name-pattern guess. pName is FindMaterial's own
// (post weather-redirect) name, used only to match the rain/snow sprite ("particle/rain" /
// "particle/snow"). pMaterial is FindMaterial's return value; ColorModulate is IMaterial
// vtable slot 28, counted off public/materialsystem/imaterial.h's virtual order and
// cross-checked identical in the SDK Definitions/Main/IMaterial.h.
void WorldFx2_OnFindMaterial(void* pMaterial, const char* pTextureGroup, const char* pName);
