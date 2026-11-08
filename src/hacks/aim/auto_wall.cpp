#include "auto_wall.h"
#include "css_weapons.h"

#include <algorithm>
#include <cmath>

void DebugLog(const char* fmt, ...);

// Public Source SDK hitgroup constants (source-sdk-2013-master/game/shared/shareddefs.h:376-383),
// not declared anywhere in this tree's defs.h yet.
#define HITGROUP_GENERIC 0
#define HITGROUP_HEAD 1
#define HITGROUP_CHEST 2
#define HITGROUP_STOMACH 3
#define HITGROUP_LEFTARM 4
#define HITGROUP_RIGHTARM 5
#define HITGROUP_LEFTLEG 6
#define HITGROUP_RIGHTLEG 7
#define HITGROUP_GEAR 10

// Public Source SDK vphysics interface (source-sdk-2013-master/public/vphysics_interface.h:882-985).
// Declared locally rather than in defs.h (SDK unit's file) -- the layout is the stock public
// interface, not a guessed vtable: every virtual up to GetSurfaceData is listed in slot order so
// the compiler's own vtable dispatch lands correctly. Only the one field FireBullet needs (game.material)
// is read; the rest of surfacedata_t exists only to keep its size/layout correct.
namespace
{
	struct SurfacePhysicsParams_t { float friction, elasticity, density, thickness, dampening; };
	struct SurfaceAudioParams_t { float reflectivity, hardnessFactor, roughnessFactor, roughThreshold, hardThreshold, hardVelocityThreshold; };
	struct SurfaceSoundNames_t { unsigned short stepleft, stepright, impactSoft, impactHard, scrapeSmooth, scrapeRough, bulletImpact, rolling, breakSound, strainSound; };
	struct SurfaceSoundHandles_t { short stepleft, stepright, impactSoft, impactHard, scrapeSmooth, scrapeRough, bulletImpact, rolling, breakSound, strainSound; };
	struct SurfaceGameProps_t { float maxSpeedFactor, jumpFactor; unsigned short material; unsigned char climbable, pad; };
	struct SurfaceData_t
	{
		SurfacePhysicsParams_t physics;
		SurfaceAudioParams_t audio;
		SurfaceSoundNames_t sounds;
		SurfaceGameProps_t game;
		SurfaceSoundHandles_t soundhandles;
	};

	class IPhysSurfaceProps
	{
	public:
		virtual ~IPhysSurfaceProps() {}
		virtual int ParseSurfaceData(const char*, const char*) = 0;
		virtual int SurfacePropCount() const = 0;
		virtual int GetSurfaceIndex(const char*) const = 0;
		virtual void GetPhysicsProperties(int, float*, float*, float*, float*) const = 0;
		virtual SurfaceData_t* GetSurfaceData(int iIndex) = 0;
	};

	// CS:S FireBullet material table (resources\cssourcex64\Features\autowall.cpp commented
	// GetMaterialParameters -- char material codes, public Source "CHAR_TEX_*" set). Default
	// case (any material not listed, e.g. concrete/dirt/metal) = modifier 1.0, damage loss 0.5
	// per the same reference.
	void MaterialParams(unsigned short iMaterial, float& flPenMod, float& flDmgMod)
	{
		switch (iMaterial)
		{
		case 'C': flPenMod = 0.4f; flDmgMod = 0.25f; break;   // CHAR_TEX_COMPUTER
		case 'G': case 'Y': flPenMod = 0.5f; flDmgMod = 0.3f; break;   // GLASS / GRATE(ish)
		case 'V': flPenMod = 1.0f; flDmgMod = 0.99f; break;   // VENT
		case 'P': flPenMod = 0.4f; flDmgMod = 0.45f; break;   // PLASTIC
		case 'S': flPenMod = 0.65f; flDmgMod = 0.3f; break;   // SOLIDMETAL (M)
		case 'M': flPenMod = 0.5f; flDmgMod = 0.45f; break;   // METAL
		case 'W': flPenMod = 1.0f; flDmgMod = 0.6f; break;    // WOOD
		default: flPenMod = 1.0f; flDmgMod = 0.5f; break;
		}
	}

	class WallFilter : public ITraceFilter
	{
	public:
		CBaseEntity* m_pSkip = nullptr;
		CBaseEntity* m_pAlsoSkip = nullptr;
		bool ShouldHitEntity(IHandleEntity* pEnt, int) override
		{
			auto pEntity = reinterpret_cast<CBaseEntity*>(pEnt);
			return pEntity != m_pSkip && pEntity != m_pAlsoSkip;
		}
		TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
	};
}

bool CAutoWall::Initialize()
{
	if (m_bTried)
		return m_pPhysProps != nullptr;
	m_bTried = true;

	// VPHYSICS_SURFACEPROPS_INTERFACE_VERSION (vphysics_interface.h:967), same version string
	// cssourcex64's I::PhysProps resolves (gather_interfaces.cpp:23) -- confirmed-working string,
	// standard CreateInterface lookup, not a guessed pointer.
	HMODULE hPhys = GetModuleHandleW(L"vphysics.dll");
	if (!hPhys)
		return false;
	auto pCreateInterface = reinterpret_cast<void* (*)(const char*, int*)>(GetProcAddress(hPhys, "CreateInterface"));
	if (!pCreateInterface)
		return false;
	m_pPhysProps = pCreateInterface("VPhysicsSurfaceProps001", nullptr);
	return m_pPhysProps != nullptr;
}

// botox scale_damage() == CCSPlayer::TraceAttack hitgroup multipliers, verified exact
// (css-community-master game/server/cstrike/cs_player.cpp:1717-1800): head x4, chest/arms x1,
// stomach x1.25, legs x0.75. CS:GO-only heavy armor + mp_damage_scale_* convars do not exist in
// CSS (grep confirms neither). Armor absorb below is cs_player.cpp:1461 OnTakeDamage, 1:1:
// flArmorRatio = 0.5 * the weapon's own WeaponArmorRatio (css_weapons.h, from the weapon's real
// script), armor soaks half the blocked damage (flArmorBonus 0.5), legs never armored,
// head only with a helmet (cs_player.cpp:1405 IsArmored()).
void CAutoWall::ScaleDamage(int iHitGroup, CBaseEntity* pEntity, float& flDamage, int iWeaponID)
{
	switch (iHitGroup)
	{
	case HITGROUP_HEAD: flDamage *= 4.f; break;
	case HITGROUP_STOMACH: flDamage *= 1.25f; break;
	case HITGROUP_LEFTLEG:
	case HITGROUP_RIGHTLEG: flDamage *= 0.75f; break;
	default: break;
	}

	auto pPlayer = pEntity ? pEntity->As<CCSPlayer>() : nullptr;
	if (!pPlayer || pPlayer->Armor() <= 0)
		return;

	bool bArmored;
	switch (iHitGroup)
	{
	case HITGROUP_HEAD: bArmored = pPlayer->HasHelmet(); break;
	case HITGROUP_LEFTLEG: case HITGROUP_RIGHTLEG: bArmored = false; break;
	default: bArmored = true; break;
	}
	if (!bArmored)
		return;

	auto pData = CSWeaponScript(iWeaponID);
	const float flArmorRatio = 0.5f * (pData ? pData->m_flArmorRatio : 1.f);
	const float flArmorBonus = 0.5f;

	float flNew = flDamage * flArmorRatio;
	float flArmorUse = (flDamage - flNew) * flArmorBonus;
	if (flArmorUse > (float)pPlayer->Armor())
	{
		flArmorUse = (float)pPlayer->Armor() * (1.f / flArmorBonus);
		flNew = flDamage - flArmorUse;
	}
	flDamage = flNew;
}

bool CAutoWall::TraceToExit(trace_t& tEnter, trace_t& tExit, const Vec3& vPos, const Vec3& vDir, CBaseEntity* pSkip)
{
	float flDistance = 0.f;
	while (flDistance <= 90.f)
	{
		flDistance += 4.f;
		Vec3 vStart = vPos + vDir * flDistance;

		int iContents = I::EngineTrace->GetPointContents(vStart);
		if (!(iContents & MASK_SHOT) || (iContents & CONTENTS_HITBOX))
		{
			Vec3 vEnd = vStart - vDir * 4.f;
			Ray_t tRay; tRay.Init(vStart, vEnd);
			WallFilter tFilter; tFilter.m_pSkip = pSkip;
			I::EngineTrace->TraceRay(tRay, MASK_SHOT | CONTENTS_HITBOX, &tFilter, &tExit);

			if (tExit.DidHit() && !tExit.startsolid)
				return true;

			if (!tExit.DidHit() || tExit.startsolid)
			{
				if (!tEnter.m_pEnt)
					return false;
				continue;
			}
		}
	}
	return false;
}

bool CAutoWall::HandlePenetration(CWeaponCSBase* pWeapon, FireBulletData_t& tData)
{
	if (!m_pPhysProps)
		return false;

	auto pSurfaceProps = reinterpret_cast<IPhysSurfaceProps*>(m_pPhysProps);
	SurfaceData_t* pEnterSurf = pSurfaceProps->GetSurfaceData(tData.m_tEnterTrace.surface.surfaceProps);
	if (!pEnterSurf)
		return false;

	const unsigned short iEnterMaterial = pEnterSurf->game.material;
	const bool bGrate = (tData.m_tEnterTrace.contents & CONTENTS_GRATE) != 0;

	// botox weapon_data->m_penetration: css_weapons.h CSWeaponScript, the weapon's own real script
	// Penetration field (cs_player_shared.cpp GetBulletTypeParameters() uses an ammo-type-keyed
	// penetration POWER for the per-wall-thickness math below; this is the per-weapon PENETRATE
	// COUNT field (cs_weapon_parse.cpp m_iPenetration) that gates whether another wall can be hit
	// at all -- same role as botox's data->m_penetration <= 0 check).
	const auto pData = pWeapon ? CSWeaponScript(pWeapon->GetWeaponID()) : nullptr;
	const float flPenetrationPower = 39.f;   // UNVERIFIED css: cs_player_shared.cpp GetBulletTypeParameters() keys this off ammo TYPE (9mm/762mm/...), not weapon id -- not yet mapped, see report NEEDS FROM OTHERS
	if (tData.m_iPenetrateCount <= 0 || !pData || pData->m_iPenetration <= 0)
		return false;

	trace_t tExit = {};
	if (!TraceToExit(tData.m_tEnterTrace, tExit, tData.m_tEnterTrace.endpos, tData.m_vDirection, nullptr))
		return false;

	SurfaceData_t* pExitSurf = pSurfaceProps->GetSurfaceData(tExit.surface.surfaceProps);
	const unsigned short iExitMaterial = pExitSurf ? pExitSurf->game.material : iEnterMaterial;

	float flPenMod = 1.f, flDmgMod = 0.5f;
	MaterialParams(iEnterMaterial, flPenMod, flDmgMod);
	if (bGrate)
	{
		flPenMod = 1.f;
		flDmgMod = 0.99f;
	}

	const float flTraceDistance = (tExit.endpos - tData.m_tEnterTrace.endpos).LengthSqr();
	const float flModifier = flPenMod > 0.f ? 1.f / flPenMod : 0.f;
	const float flLostDamage = tData.m_flCurrentDamage * flDmgMod + (flPenetrationPower > 0.f ? 3.75f / flPenetrationPower : 0.f) * (flModifier * 3.f)
		+ (flModifier * flTraceDistance) / 24.f;

	if (flLostDamage > tData.m_flCurrentDamage)
		return false;
	if (flLostDamage > 0.f)
		tData.m_flCurrentDamage -= flLostDamage;
	if (tData.m_flCurrentDamage < 1.f)
		return false;

	tData.m_vPosition = tExit.endpos;
	--tData.m_iPenetrateCount;
	(void)iExitMaterial;
	return true;
}

bool CAutoWall::SimulateFireBullet(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, FireBulletData_t& tData, CBaseEntity* pRecordTarget,
	int iHitGroup, const Vec3* pRecordPoint)
{
	if (!Initialize())
		return false;

	const bool bRecord = pRecordTarget && pRecordPoint;

	// botox data.m_penetrate_count = 4, damage from weapon_data->m_damage -- css_weapons.h
	// CSWeaponScript, the weapon's own real script Damage/Range fields (not CWeaponCSBase's old
	// TF2-numbers GetDamage()/GetRange(), which no longer exist on the CSS weapon type).
	const auto pData = pWeapon ? CSWeaponScript(pWeapon->GetWeaponID()) : nullptr;
	tData.m_iPenetrateCount = 4;
	tData.m_flCurrentDamage = pData ? (float)pData->m_iDamage : 0.f;
	if (tData.m_flCurrentDamage <= 0.f)
		return false;
	const float flWeaponRange = pData ? pData->m_flRange : 8192.f;

	float flTraceLength = 0.f;
	WallFilter tFilter; tFilter.m_pSkip = pLocal; tFilter.m_pAlsoSkip = pRecordTarget;

	while (tData.m_iPenetrateCount > 0 && tData.m_flCurrentDamage >= 1.f)
	{
		const float flSegment = bRecord ? std::max(0.f, (*pRecordPoint - tData.m_vPosition).Dot(tData.m_vDirection)) : flWeaponRange - flTraceLength;
		if (!bRecord && flSegment <= 0.f)
			break;

		const Vec3 vEnd = tData.m_vPosition + tData.m_vDirection * flSegment;
		Ray_t tRay; tRay.Init(tData.m_vPosition, vEnd);
		I::EngineTrace->TraceRay(tRay, MASK_SHOT | CONTENTS_HITBOX, &tFilter, &tData.m_tEnterTrace);

		if (tData.m_tEnterTrace.fraction == 1.f)
		{
			if (!bRecord)
				break;
			flTraceLength += flSegment;
			if (pData)
				tData.m_flCurrentDamage *= powf(pData->m_flRangeModifier, flTraceLength / 500.f);
			ScaleDamage(iHitGroup, pRecordTarget, tData.m_flCurrentDamage, pWeapon ? pWeapon->GetWeaponID() : 0);
			return tData.m_flCurrentDamage >= 1.f;
		}

		flTraceLength += tData.m_tEnterTrace.fraction * flSegment;

		// cs_player_shared.cpp FireBullet range falloff, exact: damage *= rangeModifier^(dist/500).
		if (pData)
			tData.m_flCurrentDamage *= powf(pData->m_flRangeModifier, flTraceLength / 500.f);

		if (tData.m_tEnterTrace.hitgroup != HITGROUP_GENERIC && tData.m_tEnterTrace.hitgroup != HITGROUP_GEAR && tData.m_tEnterTrace.m_pEnt
			&& tData.m_tEnterTrace.m_pEnt->IsPlayer())
		{
			ScaleDamage(tData.m_tEnterTrace.hitgroup, tData.m_tEnterTrace.m_pEnt, tData.m_flCurrentDamage, pWeapon ? pWeapon->GetWeaponID() : 0);
			return true;
		}

		if (flTraceLength > 3000.f || !HandlePenetration(pWeapon, tData))
			break;
	}

	return false;
}

float CAutoWall::GetDamage(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const Vec3& vPoint)
{
	if (!pLocal || !pWeapon)
		return -1.f;

	FireBulletData_t tData = {};
	tData.m_vPosition = pLocal->GetShootPos();
	tData.m_vDirection = (vPoint - tData.m_vPosition).Normalized();

	if (!SimulateFireBullet(pLocal, pWeapon, tData))
		return -1.f;
	return tData.m_flCurrentDamage;
}

float CAutoWall::GetRecordDamage(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const Vec3& vPoint, CBaseEntity* pTarget, int iHitGroup)
{
	if (!pLocal || !pWeapon || !pTarget)
		return -1.f;

	FireBulletData_t tData = {};
	tData.m_vPosition = pLocal->GetShootPos();
	tData.m_vDirection = (vPoint - tData.m_vPosition).Normalized();

	if (!SimulateFireBullet(pLocal, pWeapon, tData, pTarget, iHitGroup, &vPoint))
		return -1.f;
	return tData.m_flCurrentDamage;
}
