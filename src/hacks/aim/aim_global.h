#pragma once
#include "sdk.h"
#include "aim_record.h"

// BOUNDS_HITBOXES / IsHitboxValid(int,int) (Projectile::Hitboxes bounding-box hitboxes) DELETED:
// Vars::Aimbot::Projectile was a TF2-only rocket/sticky-sim namespace with no botox csgo
// counterpart and no implementing .cpp left in this tree (aim_projectile.cpp/.h already removed);
// this overload had zero callers anywhere.

Enum(Target, Unknown, Player, Sentry, Dispenser, Teleporter, Sticky, NPC, Bomb)

struct Target_t
{
	CBaseEntity* m_pEntity = nullptr;
	int m_iTargetType = TargetEnum::Unknown;
	Vec3 m_vPos = {};
	Vec3 m_vAngleTo = {};
	float m_flFOVTo = std::numeric_limits<float>::max();
	float m_flDistTo = std::numeric_limits<float>::max();
	int m_nPriority = 0;
	int m_nAimedHitbox = -1;

	TickRecord* m_pRecord = nullptr;
	bool m_bBacktrack = false;
};

class CAimbotGlobal
{
public:
	void SortTargets(std::vector<Target_t>&, int iMethod);
	void SortPriority(std::vector<Target_t>&);

	bool PlayerBoneInFOV(CCSPlayer* pTarget, Vec3 vLocalPos, Vec3 vLocalAngles, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo, int iHitboxes = Vars::Aimbot::Hitscan::HitboxGroupsEnum::All, float flFOVOverride = -1.f);
	bool IsHitboxValid(CBaseEntity* pEntity, int nHitbox, int iHitboxes = Vars::Aimbot::Hitscan::HitboxGroupsEnum::All);
	bool ShouldMultipoint(CBaseEntity* pEntity = nullptr, int nHitbox = -1, int iHitboxes = 0b11111);   // MultipointHitboxes bits

	// iIgnoreOverride: pass a Vars::Aimbot::General::IgnoreEnum bitmask to use INSTEAD of the General
	// aim Ignore dropdown (e.g. the knife's own Backstab ignore). -1 = use General::Ignore as normal.
	bool ShouldIgnore(CBaseEntity* pTarget, CCSPlayer* pLocal, CWeaponCSBase* pWeapon, int iIgnoreOverride = -1);
	int GetPriority(int iIndex);
	bool FriendlyFire();

	bool ShouldAim(float flSmooth);

	// botox aimbot.cpp:118 scale_fov_with_scope -- shrinks an aim FOV by the scope's zoom ratio.
	// General::ScaleFovWithScope gates it; flFOV <= 0 or not actually scoped returns it unchanged.
	float ScaleFovWithScope(float flFOV);

	// botox weapon_class_vars() + weapon_settings() + enabled_hitboxes(): the held weapon's class
	// override when its "override general" is on, else general. FOV is NOT scope-scaled here.
	// Non-gun (knife/grenade/c4) = botox nullptr class = fov 0 (no aim), smooth 1, rcs 0.
	struct WeaponSettings_t { float m_flFOV = 0.f, m_flSmooth = 1.f, m_flRCS = 0.f; int m_iHitboxGroups = 0; bool m_bSniper = false; };
	WeaponSettings_t WeaponSettings(int nWeaponID);
};

ADD_FEATURE(CAimbotGlobal, AimbotGlobal);