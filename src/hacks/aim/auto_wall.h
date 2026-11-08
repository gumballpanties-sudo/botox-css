#pragma once
#include "sdk.h"

// botox csgo-sdk hacks/auto_wall/auto_wall.cpp, ported for CS:Source. Formula cross-checked
// against resources\cssourcex64\Features\autowall.cpp (commented-out CS:S reference: same
// ScaleDamage head x4 / stomach x1.25 / legs x0.75, armor ratio x0.5, same penetration-distance
// walk) and resources\css-community-master\mp\src\game\shared\cstrike\cs_player_shared.cpp
// FireBullet (penetration loop shape). CS:GO's formula (botox g_auto_wall.scale_damage: heavy
// armor halving, ct/t per-hitgroup damage-scale convars) does NOT apply -- CSS has no heavy armor
// and no mp_damage_scale_* convars (grep confirms neither exists in css-community-master), so this
// is the CS:S-correct formula, not a simplified CS:GO one.
//
// NEEDS FROM OTHERS (SDK unit, defs.h/css.h), both BLOCKED without a verified signature -- not
// guessed, so not written:
//   1. Per-weapon script data (CCSWeaponInfo: m_iPenetration, m_flArmorRatio, m_flRangeModifier).
//      cssourcex64's own Fn::GetCsWpnData sig is commented out/unresolved (gather_interfaces.cpp:53)
//      -- nobody has found this pointer yet. Stopgap below: CWeaponCSBase::GetDamage()/GetRange()
//      (already-existing accessors, currently return TF2 numbers) for damage/range,
//      k_fallback_penetration/k_fallback_armor_ratio constants for the rest.
//   2. CWeaponCSBase has no `m_iWeaponMode`-equivalent accurate for CS (burst/secondary mode) --
//      not needed here (CSS has no dual-fire-mode weapons in the P228/Elite sense requiring it).
class CAutoWall
{
public:
	struct FireBulletData_t
	{
		Vec3 m_vPosition = {};
		Vec3 m_vDirection = {};
		trace_t m_tEnterTrace = {};
		float m_flCurrentDamage = 0.f;
		int m_iPenetrateCount = 0;
	};

	// botox get_damage(): along the local player's own weapon, from his eye, to vPoint.
	float GetDamage(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const Vec3& vPoint);

	// botox get_record_damage(): backtrack record point -- the record's target is skipped by the
	// trace filter (server already moved him there), live model must not block the shot.
	float GetRecordDamage(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const Vec3& vPoint, CBaseEntity* pTarget, int iHitGroup);

	bool Initialize();   // self-contained IPhysicsSurfaceProps fetch; see .cpp

private:
	bool SimulateFireBullet(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, FireBulletData_t& tData, CBaseEntity* pRecordTarget = nullptr,
		int iHitGroup = 0, const Vec3* pRecordPoint = nullptr);
	void ScaleDamage(int iHitGroup, CBaseEntity* pEntity, float& flDamage, int iWeaponID);
	bool TraceToExit(trace_t& tEnter, trace_t& tExit, const Vec3& vPos, const Vec3& vDir, CBaseEntity* pSkip);
	bool HandlePenetration(CWeaponCSBase* pWeapon, FireBulletData_t& tData);

	void* m_pPhysProps = nullptr;   // IPhysicsSurfaceProps*, resolved lazily -- see .cpp
	bool m_bTried = false;
};

ADD_FEATURE(CAutoWall, AutoWall);
