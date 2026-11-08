#pragma once
#include "sdk.h"

#include <array>
#include <vector>

// botox csgo-sdk hacks/aimbot/grenade_aim.cpp/h, re-derived against CSS's OWN throw model
// (resources\css-community-master\mp\src\game\shared\cstrike\weapon_basecsgrenade.cpp:332-372
// CBaseCSGrenade::ThrowGrenade), which is SIMPLER than CS:GO's and verified from source, not
// guessed:
//   - no throw-strength slider / underhand toggle at all (CS:GO-only addition) -- CSS always
//     throws at full strength, speed set purely by view pitch.
//   - speed = clamp((90 - angThrow.x) * 6, ..., 750) where angThrow.x is the remapped pitch
//     (see ThrowPitchRemap below) -- NOT CS:GO's data->m_throw_velocity * 0.9 formula.
//   - inherited velocity is 1.0x player velocity (CS:GO uses 1.25x) -- vecThrow = fwd*flVel +
//     GetAbsVelocity(), no multiplier.
//   - launch offset 16u (matches CS:GO, same CBaseCSGrenade lineage).
//   - elasticity 0.45 (basecsgrenade_projectile.h:54 GetGrenadeElasticity -- byte-identical
//     constant to CS:GO's, confirming shared lineage).
//   - grenade types: CSS has flashbang / HE / smoke ONLY (css_weapons.h CSGrenadeSlot) -- no
//     molotov/incendiary, decoy or tactical/tagrenade (CS:GO-only weapons, confirmed absent from
//     cs_weapon_parse.h). Said here and in the port report per user direction ("can't exist in
//     CSS, say so, don't fake it").
class CGrenadeAim
{
public:
	// create_move, inside prediction begin/end (post-move origin/velocity, matches botox).
	void OnCreateMove(CCSPlayer* pLocal, CUserCmd* pCmd);

	// after every other view writer (movement fix wrapped), or the throw-tick angle gets
	// overwritten by whatever ran after it in the pipeline.
	void Apply(CUserCmd* pCmd);

	void OnPaintTraverse();

private:
	void Aim(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, int iItemID, float flMaxTime, bool bPinPulled, bool bThrowTick, CUserCmd* pCmd);

	bool m_bThrow = false;
	bool m_bAutoReleased = false;
	bool m_bToggled = false;          // latch for Grenade::KeyMode == Toggle
	float m_flReleaseTime = 0.f;
	int m_iLock = 0;
	Vec3 m_vView = {};

	// 3s max flight at 66 tick (CSS tickrate) + launch/end slack.
	std::array<Vec3, 220> m_vPath = {};
	int m_iPathCount = 0;
};

ADD_FEATURE(CGrenadeAim, GrenadeAim);

// Shared CSS grenade-throw physics (grenade_aim.cpp), reused by esp.cpp's grenade-path draw
// (DrawGrenadePath/DrawProjectilePaths) instead of the deleted TF2 ProjSim. See grenade_aim.cpp
// for the formula sources.
void CssGrenadeThrow(const Vec3& vEye, const Vec3& vViewAngles, const Vec3& vPlayerVel, CBaseEntity* pSkip, Vec3& vOrigin, Vec3& vVel);
void CssGrenadeBouncePath(Vec3 vOrigin, Vec3 vVel, float flGravity, float flElasticity, float flMaxTime,
	CBaseEntity* pSkip, std::vector<Vec3>& outPath, std::vector<bool>& outBounce, bool& outDetonated, Vec3& outDetonatePos);
