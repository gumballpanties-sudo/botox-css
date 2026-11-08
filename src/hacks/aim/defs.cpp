#include "defs.h"
#include "sdk.h"
#include "core.h"

// ================ SDK/Definitions/Main/CBaseAnimating.cpp ================

int CBaseAnimating::GetNumOfHitboxes()
{
	auto pModel = GetModel();
	if (!pModel) return 0;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return 0;
	auto pSet = pHDR->pHitboxSet(m_nHitboxSet());
	if (!pSet) return 0;

	return pSet->numhitboxes;
}

Vec3 CBaseAnimating::GetHitboxOrigin(matrix3x4* aBones, int nHitbox, Vec3 vOffset)
{
	auto pModel = GetModel();
	if (!pModel) return {};
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return {};
	auto pSet = pHDR->pHitboxSet(m_nHitboxSet());
	if (!pSet || pSet->numhitboxes <= nHitbox) return {};
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox) return {};

	Vec3 vOut; Math::VectorTransform(vOffset, aBones[pBox->bone], vOut);
	return vOut;
}

Vec3 CBaseAnimating::GetHitboxCenter(matrix3x4* aBones, int nHitbox, Vec3 vOffset)
{
	auto pModel = GetModel();
	if (!pModel) return {};
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return {};
	auto pSet = pHDR->pHitboxSet(m_nHitboxSet());
	if (!pSet || pSet->numhitboxes <= nHitbox) return {};
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox) return {};

	Vec3 vOut; Math::VectorTransform((pBox->bbmin + pBox->bbmax) / 2 + vOffset, aBones[pBox->bone], vOut);
	return vOut;
}

void CBaseAnimating::GetHitboxInfo(matrix3x4* aBones, int nHitbox, Vec3* pCenter, Vec3* pMins, Vec3* pMaxs, matrix3x4* pMatrix, Vec3 vOffset)
{
	auto pModel = GetModel();
	if (!pModel) return;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return;
	auto pSet = pHDR->pHitboxSet(m_nHitboxSet());
	if (!pSet || pSet->numhitboxes <= nHitbox) return;
	auto pBox = pSet->pHitbox(nHitbox);
	if (!pBox) return;

	if (pMins)
		*pMins = pBox->bbmin;

	if (pMaxs)
		*pMaxs = pBox->bbmax;

	if (pCenter)
		Math::VectorTransform(vOffset, aBones[pBox->bone], *pCenter);

	if (pMatrix)
		memcpy(*pMatrix, aBones[pBox->bone], sizeof(matrix3x4));
}

// ================ SDK/Definitions/Main/CBaseCombatCharacter.cpp ================
CHandle<CTFWeaponBase>(&CBaseCombatCharacter::m_hMyWeapons())[MAX_WEAPONS]
{
	static int nOffset = U::NetVars.GetNetVar("CBaseCombatCharacter", "m_hMyWeapons");
	return *reinterpret_cast<CHandle<CTFWeaponBase>(*)[MAX_WEAPONS]>(uintptr_t(this) + nOffset);
}

CTFWeaponBase* CBaseCombatCharacter::GetWeaponFromSlot(int nSlot)
{
	if (nSlot < 0 || nSlot >= MAX_WEAPONS)
		return nullptr;
	return m_hMyWeapons()[nSlot].Get();
}

// ================ SDK/Definitions/Main/CBaseEntity.cpp ================
int CBaseEntity::GetIntermediateDataSize()
{
	return std::max(GetPredDescMap()->packed_size, 4);
}

Vec3 CBaseEntity::GetSize()
{
	return m_vecMaxs() - m_vecMins();
}

Vec3 CBaseEntity::GetOffset()
{
	return m_vecMins() + m_vecMaxs();
}

Vec3 CBaseEntity::GetCenter()
{
	return m_vecOrigin() + GetOffset() / 2;
}

int CBaseEntity::IsInValidTeam()
{
	switch (int nTeamNum = m_iTeamNum())
	{
	case TF_TEAM_RED:
	case TF_TEAM_BLUE:
		return nTeamNum;
	}
	return 0;
}

int CBaseEntity::SolidMask()
{
	if (IsPlayer())
	{
		switch (m_iTeamNum())
		{
		case TF_TEAM_RED: return MASK_PLAYERSOLID | CONTENTS_BLUETEAM;
		case TF_TEAM_BLUE: return MASK_PLAYERSOLID | CONTENTS_REDTEAM;
		}
		return MASK_PLAYERSOLID;
	}
	return MASK_SOLID;
}

#pragma region Model hitboxes
// stupid, we shouldn't need to do this

int CBaseEntity::GetHitboxToBase(int nHitbox)
{
	// CSS player models, hitbox set 0. All 8 stock models share this order (tools/css_hitboxes.py
	// dump of cstrike_pak_dir.vpk): 0 pelvis, 1-4 L leg, 5-8 R leg, 9 spine1, 10 spine2, 11 neck,
	// 12 head, 13-15 L arm, 16-18 R arm. The TF2 table read raw index 0 as HEAD = aimed at pelvis.
	static constexpr int aMap[] = {
		HITBOX_PELVIS,
		HITBOX_LEFT_THIGH, HITBOX_LEFT_CALF, HITBOX_LEFT_FOOT, HITBOX_LEFT_FOOT,
		HITBOX_RIGHT_THIGH, HITBOX_RIGHT_CALF, HITBOX_RIGHT_FOOT, HITBOX_RIGHT_FOOT,
		HITBOX_SPINE0, HITBOX_SPINE2, HITBOX_SPINE3,
		HITBOX_HEAD,
		HITBOX_LEFT_UPPERARM, HITBOX_LEFT_FOREARM, HITBOX_LEFT_HAND,
		HITBOX_RIGHT_UPPERARM, HITBOX_RIGHT_FOREARM, HITBOX_RIGHT_HAND,
	};
	return nHitbox >= 0 && nHitbox < int(std::size(aMap)) ? aMap[nHitbox] : -1;
}

#pragma endregion

// ================ SDK/Definitions/Main/CBaseObject.cpp ================

int CObjectSentrygun::MaxAmmoShells()
{
	if (m_iUpgradeLevel() == 1 || m_bMiniBuilding())
		return 150;
	else
		return 200;
}

void CObjectSentrygun::GetAmmoCount(int& iShells, int& iMaxShells, int& iRockets, int& iMaxRockets)
{
	const bool bIsMini = m_bMiniBuilding();

	iShells = m_iAmmoShells();
	iMaxShells = MaxAmmoShells();
	iRockets = bIsMini ? 0 : m_iAmmoRockets();
	iMaxRockets = (bIsMini || m_iUpgradeLevel() < 3) ? 0 : 20;
}

// ================ SDK/Definitions/Main/CBasePlayer.cpp ================
bool CBasePlayer::IsAlive()
{
	return m_lifeState() == LIFE_ALIVE;
}

Vec3 CBasePlayer::GetShootPos()
{
	return m_vecOrigin() + m_vecViewOffset();
}

Vec3 CBasePlayer::GetEyePosition()
{
	return GetAbsOrigin() + m_vecViewOffset();
}

bool CBasePlayer::IsSwimming()
{
	return m_nWaterLevel() > 1;
}

bool CBasePlayer::IsUnderwater()
{
	return m_nWaterLevel() > 2;
}

// ================ SDK/Definitions/Main/CBaseProjectile.cpp ================
bool CTFProjectile_Arrow::CanHeadshot()
{
	return m_iProjectileType() == TF_PROJECTILE_ARROW || m_iProjectileType() == TF_PROJECTILE_FESTIVE_ARROW;
}

bool CTFGrenadePipebombProjectile::HasStickyEffects()
{
	return m_iType() == TF_GL_MODE_REMOTE_DETONATE || m_iType() == TF_GL_MODE_REMOTE_DETONATE_PRACTICE;
}

// ================ SDK/Definitions/Main/CTFPlayer.cpp ================
Vec3 CTFPlayer::GetEyeAngles()
{
	return { m_angEyeAnglesX(), m_angEyeAnglesY(), 0.f };
}

Vec3 CTFPlayer::GetViewOffset()
{
	if (!IsPlayer())
		return GetOffset() / 2;

	auto getMainOffset = [this]() -> Vec3
		{
			if (IsDucking())
				return { 0.f, 0.f, 45.f };

			switch (m_iClass())
			{
			case TF_CLASS_SCOUT: return { 0.f, 0.f, 65.f };
			case TF_CLASS_SOLDIER: return { 0.f, 0.f, 68.f };
			case TF_CLASS_PYRO: return { 0.f, 0.f, 68.f };
			case TF_CLASS_DEMOMAN: return { 0.f, 0.f, 68.f };
			case TF_CLASS_HEAVY: return { 0.f, 0.f, 75.f };
			case TF_CLASS_ENGINEER: return { 0.f, 0.f, 68.f };
			case TF_CLASS_MEDIC: return { 0.f, 0.f, 75.f };
			case TF_CLASS_SNIPER: return { 0.f, 0.f, 75.f };
			case TF_CLASS_SPY: return { 0.f, 0.f, 75.f };
			}

			const Vec3 vOffset = m_vecViewOffset();
			if (vOffset.z)
				return vOffset;

			return { 0.f, 0.f, 68.f };
		};

	return getMainOffset() * m_flModelScale();
}

bool CTFPlayer::InCond(ETFCond eCond)
{
	switch (eCond / 32)
	{
	case 0:
		return m_nPlayerCond() & (1 << eCond) || _condition_bits() & (1 << eCond);
	case 1:
		return m_nPlayerCondEx() & (1 << (eCond - 32));
	case 2:
		return m_nPlayerCondEx2() & (1 << (eCond - 64));
	case 3:
		return m_nPlayerCondEx3() & (1 << (eCond - 96));
	case 4:
		return m_nPlayerCondEx4() & (1 << (eCond - 128));
	}
	return false;
}

void CTFPlayer::RemoveCond(ETFCond eCond)
{
	switch (eCond / 32)
	{
	case 0:
		m_nPlayerCond() &= ~(1 << eCond), _condition_bits() &= ~(1 << eCond); break;
	case 1:
		m_nPlayerCondEx() &= ~(1 << (eCond - 32)); break;
	case 2:
		m_nPlayerCondEx2() &= ~(1 << (eCond - 64)); break;
	case 3:
		m_nPlayerCondEx3() &= ~(1 << (eCond - 96)); break;
	case 4:
		m_nPlayerCondEx4() &= ~(1 << (eCond - 128)); break;
	}
}

bool CTFPlayer::IsAGhost()
{
	return InCond(TF_COND_HALLOWEEN_GHOST_MODE);
};
bool CTFPlayer::IsTaunting()
{
	return InCond(TF_COND_TAUNTING);
};

bool CTFPlayer::IsInvisible(float flValue)
{
	if (InCond(TF_COND_BURNING)
		|| InCond(TF_COND_BURNING_PYRO)
		|| InCond(TF_COND_MAD_MILK)
		|| InCond(TF_COND_URINE))
		return false;

	float flInvis = GetEffectiveInvisibilityLevel();
	return flInvis && flInvis >= 1.f;
}

bool CTFPlayer::IsInvulnerable()
{
	return InCond(TF_COND_INVULNERABLE)
		|| InCond(TF_COND_INVULNERABLE_CARD_EFFECT)
		|| InCond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED)
		|| InCond(TF_COND_INVULNERABLE_USER_BUFF)
		|| InCond(TF_COND_PHASE);
}

bool CTFPlayer::IsCritBoosted()
{
	return InCond(TF_COND_CRITBOOSTED)
		|| InCond(TF_COND_CRITBOOSTED_PUMPKIN)
		|| InCond(TF_COND_CRITBOOSTED_USER_BUFF)
		|| InCond(TF_COND_CRITBOOSTED_FIRST_BLOOD)
		|| InCond(TF_COND_CRITBOOSTED_BONUS_TIME)
		|| InCond(TF_COND_CRITBOOSTED_CTF_CAPTURE)
		|| InCond(TF_COND_CRITBOOSTED_ON_KILL)
		|| InCond(TF_COND_CRITBOOSTED_RAGE_BUFF)
		|| InCond(TF_COND_CRITBOOSTED_CARD_EFFECT)
		|| InCond(TF_COND_CRITBOOSTED_RUNE_TEMP);
}

bool CTFPlayer::IsMiniCritBoosted()
{
	return InCond(TF_COND_OFFENSEBUFF)
		|| InCond(TF_COND_ENERGY_BUFF)
		|| InCond(TF_COND_NOHEALINGDAMAGEBUFF)
		/*|| InCond(TF_COND_CRITBOOSTED_DEMO_CHARGE)*/;
}

bool CTFPlayer::CanAttack(bool bCloak, bool bLocal)
{
	if (!IsAlive() || IsAGhost() || IsTaunting() || m_bViewingCYOAPDA()
		|| InCond(TF_COND_PHASE) || InCond(TF_COND_HALLOWEEN_KART) || InCond(TF_COND_STUNNED) && m_iStunFlags() & (TF_STUN_CONTROLS | TF_STUN_LOSER_STATE))
		return false;

	if (bCloak)
	{
		if (bLocal
			? (m_flStealthNoAttackExpire() > TICKS_TO_TIME(m_nTickBase()) && !InCond(TF_COND_STEALTHED_USER_BUFF)) || InCond(TF_COND_STEALTHED)
			: m_flInvisibility() && (InCond(TF_COND_STEALTHED) || !InCond(TF_COND_STEALTHED_USER_BUFF) && !InCond(TF_COND_STEALTHED_USER_BUFF_FADING)))
		{
			auto pWeapon = m_hActiveWeapon()->As<CTFWeaponBase>();
			if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_GRAPPLINGHOOK)
				return false;
		}

		if (m_bFeignDeathReady())
			return false;
	}

	auto pGameRules = I::TFGameRules();
	if (pGameRules)
	{
		switch (pGameRules->m_iRoundState())
		{
		case GR_STATE_TEAM_WIN:
			if (m_iTeamNum() != pGameRules->m_iWinningTeam())
				return false;
			break;
		case GR_STATE_BETWEEN_RNDS:
			if (m_fFlags() & FL_FROZEN)
				return false;
			break;
		case GR_STATE_GAME_OVER:
			if (m_fFlags() & FL_FROZEN || m_iTeamNum() != pGameRules->m_iWinningTeam())
				return false;
			break;
		}
	}

	if (SDK::AttribHookValue(0, "no_attack", this))
		return false;

	return true;
}

float CTFPlayer::GetCritMult()
{
	return Math::RemapVal(m_iCritMult(), 0.f, 255.f, 1.f, 4.f);
}

// ================ SDK/Definitions/Main/CTFWeaponBase.cpp ================
bool CTFWeaponBase::HasPrimaryAmmoForShot()
{
	// IsEnergyWeapon removed (TF2-only concept, no CSS counterpart, see defs.h) -- CSS
	// weapons are never energy weapons, so this branch is simply never taken.
	int iClip = m_iClip1();
	auto pOwner = m_hOwnerEntity().Get();
	return (iClip == WEAPON_NOCLIP && pOwner ? pOwner->As<CTFPlayer>()->GetAmmoCount(m_iPrimaryAmmoType()) : iClip) >= GetAmmoPerShot();
}

bool CTFWeaponBase::CanPrimaryAttack()
{
	auto pOwner = m_hOwnerEntity()->As<CTFPlayer>();
	if (!pOwner)
		return false;

	float flCurTime = TICKS_TO_TIME(pOwner->m_nTickBase());
	return m_flNextPrimaryAttack() <= flCurTime && pOwner->m_flNextAttack() <= flCurTime;
}

bool CTFWeaponBase::CanSecondaryAttack()
{
	auto pOwner = m_hOwnerEntity()->As<CTFPlayer>();
	if (!pOwner)
		return false;

	float flCurTime = TICKS_TO_TIME(pOwner->m_nTickBase());
	return m_flNextSecondaryAttack() <= flCurTime && pOwner->m_flNextAttack() <= flCurTime;
}

bool CTFWeaponBase::CanFireCriticalShot(bool bIsHeadshot)
{
	auto pOwner = m_hOwnerEntity()->As<CTFPlayer>();
	if (!pOwner)
		return false;

	int& iFOV = pOwner->m_iFOV(), nFovBackup = iFOV;
	iFOV = 70;
	bool bReturn = U::Memory.CallVirtual<428, bool>(this, bIsHeadshot, nullptr);
	iFOV = nFovBackup;
	return bReturn;
}

bool CTFWeaponBase::CanHeadshot()
{
	// GetDamageType removed (no verified CSS slot, see defs.h) -- CSS damage is always
	// hitlocation-based (no DMG_USE_HITLOCATIONS gate concept), so this collapses to the
	// crit-chance check alone.
	return CanFireCriticalShot(true);
}

bool CTFWeaponBase::AmbassadorCanHeadshot(float flCurTime)
{
	if (GetClassID() == ETFClassID::CTFRevolver && SDK::AttribHookValue(0, "set_weapon_mode", this) == 1)
		return flCurTime - m_flLastFireTime() > 1.f;
	return false;
}

bool CTFWeaponBase::IsInReload()
{
	return m_bInReload() || m_iReloadMode() != 0;
}

//#define WEAPONDATA_USES_WEAPONMODE // would probably just be better to have a mode override

float CTFWeaponBase::GetDamage(bool bAttribHookValue)
{
	if (auto pWeaponInfo = GetWeaponInfo())
	{
		if (!bAttribHookValue)
			return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nDamage;
		return SDK::AttribHookValue(pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nDamage, "mult_dmg", this);
	}
	return 0.f;
}

float CTFWeaponBase::GetFireRate(bool bAttribHookValue)
{
	if (auto pWeaponInfo = GetWeaponInfo())
	{
		if (!bAttribHookValue)
			return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_flTimeFireDelay;
		return SDK::AttribHookValue(pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_flTimeFireDelay, "mult_postfiredelay", this);
	}
	return 0.315f;
}

int CTFWeaponBase::GetBulletsPerShot(bool bAttribHookValue)
{
	if (auto pWeaponInfo = GetWeaponInfo())
	{
		if (!bAttribHookValue)
			return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot;
		return SDK::AttribHookValue(pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot, "mult_bullets_per_shot", this);
	}
	return 1;
}

int CTFWeaponBase::GetAmmoPerShot(bool bAttribHookValue)
{
	if (auto pWeaponInfo = GetWeaponInfo())
	{
		if (!bAttribHookValue)
			return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_iAmmoPerShot;
		int iAmmoPerShot = SDK::AttribHookValue(0, "mod_ammo_per_shot", this);
		return iAmmoPerShot > 0 ? iAmmoPerShot : pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_iAmmoPerShot;
	}
	return 1;
}

bool CTFWeaponBase::IsRapidFire()
{
	if (auto pWeaponInfo = GetWeaponInfo())
		return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_bUseRapidFireCrits;
		return false;
}

float CTFWeaponBase::GetSmackDelay()
{
	if (auto pWeaponInfo = GetWeaponInfo())
		return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_flSmackDelay;
		return 0.2f;
}

float CTFWeaponBase::GetRange()
{
	if (auto pWeaponInfo = GetWeaponInfo())
		return pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_flRange;
	return 8192.f;
}

int CTFPipebombLauncher::GetDetonateType()
{
	return SDK::AttribHookValue(0, "set_detonate_mode", this);
}

int CTFSniperRifle::GetRifleType()
{
	return SDK::AttribHookValue(0, "set_weapon_mode", this);
}

float CTFSniperRifle::GetHeadshotMult(CTFPlayer* pTarget)
{
	auto GetMainMult = [&]()
		{
			auto pOwner = m_hOwnerEntity()->As<CTFPlayer>();
			if (pOwner && pOwner->IsCritBoosted())
				return 3.f;

			if (GetRifleType() == RIFLE_JARATE)
			{
				if (SDK::AttribHookValue(0, "jarate_duration", this) > 0)
					return 1.36f;

				if (pOwner && pOwner->IsMiniCritBoosted()
					|| pTarget && (pTarget->InCond(TF_COND_URINE) || pTarget->InCond(TF_COND_MARKEDFORDEATH)))
					return 1.36f;

				return 1.f;
			}

			return 3.f;
		};

	float flMult = SDK::AttribHookValue(GetMainMult(), "mult_dmg", this);
	if (m_flChargedDamage() == 150.f)
		flMult = SDK::AttribHookValue(flMult, "sniper_full_charge_damage_bonus", this);
	return flMult;
}

float CTFSniperRifle::GetBodyshotMult(CTFPlayer* pTarget)
{
	auto GetMainMult = [&]()
		{
			auto pOwner = m_hOwnerEntity()->As<CTFPlayer>();
			if (pOwner && pOwner->IsCritBoosted())
				return 3.f;

			if (pOwner && pOwner->IsMiniCritBoosted()
				|| pTarget && (pTarget->InCond(TF_COND_URINE) || pTarget->InCond(TF_COND_MARKEDFORDEATH)))
				return 1.36f;

			return 1.f;
		};

	float flMult = SDK::AttribHookValue(GetMainMult(), "mult_dmg", this);
	flMult = SDK::AttribHookValue(flMult, "bodyshot_damage_modify", this);
	if (m_flChargedDamage() == 150.f)
		flMult = SDK::AttribHookValue(flMult, "sniper_full_charge_damage_bonus", this);
	return flMult;
}

int CTFGrenadeLauncher::GetDetonateType()
{
	return SDK::AttribHookValue(0, "set_detonate_mode", this);
}

int CTFFlareGun::GetFlareGunType()
{
	return SDK::AttribHookValue(0, "set_weapon_mode", this);
}

#define ReturnTexture(string) { static CHudTexture* pTexture = H::Draw.GetIcon(string); return pTexture; }

// ================ SDK/Definitions/Main/INetMessage.cpp ================
#include <format>
#include <algorithm>

#define NETMSG_TYPE_BITS 6
#define NUM_NEW_COMMAND_BITS 4
#define NUM_BACKUP_COMMAND_BITS 3

#define NET_TICK_SCALEUP	100000.0f

// ================ SDK/Definitions/Main/KeyValues.cpp ================
#define Q_ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))

MAKE_SIGNATURE(KeyValues_LoadFromBuffer, "engine.dll", "4C 89 4C 24 ? 48 89 4C 24 ? 55 56", 0x0);
// KeyValues::KeyValues(const char*). 2026-09-30 build zeroes +0x23 as a word (was byte) and a new qword at +0x40.
MAKE_SIGNATURE(KeyValues_Initialize, "engine.dll", "40 53 48 83 EC ? 48 8B D9 66 C7 41 ? ? ? 33 C9 48 8B C2", 0x0);
MAKE_SIGNATURE(KeyValues_GetStringForSymbolClassic, "engine.dll", "40 53 48 83 EC ? 8B D9 FF 15", 0x0);
MAKE_SIGNATURE(KeyValues_FindKey, "client.dll", "48 8B C4 53 55 56 48 81 EC ? ? ? ? 41 0F B6 E8", 0x0);

static int UnicodeToUTF8(const wchar_t* unicode, char* ansi, int ansiBufferSize)
{
	int result = WideCharToMultiByte(CP_UTF8, 0, unicode, -1, ansi, ansiBufferSize, NULL, NULL);
	ansi[ansiBufferSize - 1] = 0;
	return result;
}

bool KeyValues::LoadFromBuffer(char const* resource_name, const char* buffer, void* file_system, const char* path_id)
{
	return S::KeyValues_LoadFromBuffer.Call<bool>(this, resource_name, buffer, file_system, path_id);
}

void KeyValues::Initialize(const char* name)
{
	S::KeyValues_Initialize.Call<KeyValues*>(this, name);
}

KeyValues::KeyValues(const char* name)
{
	Initialize(name);
}

void* KeyValues::operator new(size_t iAllocSize)
{
	return I::KeyValuesSystem->AllocKeyValuesMemory((int)iAllocSize);
}

void* KeyValues::operator new(size_t iAllocSize, int nBlockUse, const char* pFileName, int nLine)
{
	void* p = I::KeyValuesSystem->AllocKeyValuesMemory((int)iAllocSize);
	return p;
}

void KeyValues::operator delete(void* pMem)
{
	I::KeyValuesSystem->FreeKeyValuesMemory(pMem);
}

void KeyValues::operator delete(void* pMem, int nBlockUse, const char* pFileName, int nLine)
{
	I::KeyValuesSystem->FreeKeyValuesMemory(pMem);
}

KeyValues *KeyValues::FindKey(const char *keyName, bool bCreate)
{
	return S::KeyValues_FindKey.Call<KeyValues*>(this, keyName, bCreate);
}

const char* KeyValues::GetName() const
{
	return S::KeyValues_GetStringForSymbolClassic.Call<const char*>(m_iKeyName);
}

int KeyValues::GetInt(const char* keyName, int defaultValue)
{
	KeyValues* dat = FindKey(keyName, false);
	if (dat)
	{
		switch (dat->m_iDataType)
		{
		case TYPE_STRING:
			return atoi(dat->m_sValue);
		case TYPE_WSTRING:
			return _wtoi(dat->m_wsValue);
		case TYPE_FLOAT:
			return (int)dat->m_flValue;
		case TYPE_UINT64:
			return 0;
		case TYPE_INT:
		case TYPE_PTR:
		default:
			return dat->m_iValue;
		};
	}
	return defaultValue;
}

uint64_t KeyValues::GetUint64(const char* keyName, uint64_t defaultValue)
{
	KeyValues* dat = FindKey(keyName, false);
	if (dat)
	{
		switch (dat->m_iDataType)
		{
		case TYPE_STRING:
			return (uint64_t)_atoi64(dat->m_sValue);
		case TYPE_WSTRING:
			return _wtoi64(dat->m_wsValue);
		case TYPE_FLOAT:
			return (int)dat->m_flValue;
		case TYPE_UINT64:
			return *((uint64_t*)dat->m_sValue);
		case TYPE_INT:
		case TYPE_PTR:
		default:
			return dat->m_iValue;
		};
	}
	return defaultValue;
}

float KeyValues::GetFloat(const char* keyName, float defaultValue)
{
	KeyValues* dat = FindKey(keyName, false);
	if (dat)
	{
		switch (dat->m_iDataType)
		{
		case TYPE_STRING:
			return (float)atof(dat->m_sValue);
		case TYPE_WSTRING:
			return (float)_wtof(dat->m_wsValue);
		case TYPE_FLOAT:
			return dat->m_flValue;
		case TYPE_INT:
			return (float)dat->m_iValue;
		case TYPE_UINT64:
			return (float)(*((uint64_t*)dat->m_sValue));
		case TYPE_PTR:
		default:
			return 0.0f;
		};
	}
	return defaultValue;
}

const char* KeyValues::GetString(const char* keyName, const char* defaultValue)
{
	KeyValues* dat = FindKey(keyName, false);
	if (dat)
	{
		char buf[64];
		switch (dat->m_iDataType)
		{
		case TYPE_FLOAT:
			snprintf(buf, sizeof(buf), "%f", dat->m_flValue);
			SetString(keyName, buf);
			break;
		case TYPE_PTR:
			snprintf(buf, sizeof(buf), "%lld", (int64_t)(size_t)dat->m_pValue);
			SetString(keyName, buf);
			break;
		case TYPE_INT:
			snprintf(buf, sizeof(buf), "%d", dat->m_iValue);
			SetString(keyName, buf);
			break;
		case TYPE_UINT64:
			snprintf(buf, sizeof(buf), "%lld", *((uint64_t*)(dat->m_sValue)));
			SetString(keyName, buf);
			break;
		case TYPE_WSTRING:
		{
			char wideBuf[512];
			int result = UnicodeToUTF8(dat->m_wsValue, wideBuf, 512);
			if (result)
			{
				SetString(keyName, wideBuf);
			}
			else
			{
				return defaultValue;
			}
			break;
		}
		case TYPE_STRING:
			break;
		default:
			return defaultValue;
		};

		return dat->m_sValue;
	}
	return defaultValue;
}

bool KeyValues::GetBool(const char* keyName, bool defaultValue /*, bool* optGotDefault*/)
{
	if (FindKey(keyName))
	{
		/*if (optGotDefault)
			(*optGotDefault) = false;*/

		return 0 != GetInt(keyName, 0);
	}

	/*if (optGotDefault)
		(*optGotDefault) = true;*/

	return defaultValue;
}

bool KeyValues::IsEmpty(const char* keyName)
{
	KeyValues* dat = FindKey(keyName, false);
	if (!dat)
		return true;

	if (dat->m_iDataType == TYPE_NONE && dat->m_pSub == NULL)
		return true;

	return false;
}

void KeyValues::SetString(const char* keyName, const char* value)
{
	KeyValues* dat = FindKey(keyName, true);

	if (dat)
	{
		if (dat->m_iDataType == TYPE_STRING && dat->m_sValue == value)
		{
			return;
		}

		delete[] dat->m_sValue;
		delete[] dat->m_wsValue;
		dat->m_wsValue = NULL;

		if (!value)
		{
			value = "";
		}

		int len = int(strlen(value));
		dat->m_sValue = new char[len + 1];
		memcpy(dat->m_sValue, value, len + 1);

		dat->m_iDataType = TYPE_STRING;
	}
}

void KeyValues::SetInt(const char* keyName, int value)
{
	KeyValues* dat = FindKey(keyName, true);

	if (dat)
	{
		dat->m_iValue = value;
		dat->m_iDataType = TYPE_INT;
	}
}

void KeyValues::SetFloat(const char* keyName, float value)
{
	KeyValues* dat = FindKey(keyName, true);

	if (dat)
	{
		dat->m_flValue = value;
		dat->m_iDataType = TYPE_FLOAT;
	}
}

void KeyValues::SetColor(const char* keyName, Color_t value)
{
	KeyValues* dat = FindKey(keyName, true);

	if (dat)
	{
		dat->m_iDataType = TYPE_COLOR;
		dat->m_Color[0] = value.r;
		dat->m_Color[1] = value.g;
		dat->m_Color[2] = value.b;
		dat->m_Color[3] = value.a;
	}
}

void KeyValues::SetBool(const char* keyName, bool value)
{
	SetInt(keyName, value ? 1 : 0);
}

void KeyValues::DeleteThis()
{
	delete this;
}

// ================ SDK/Definitions/Main/MD5.cpp ================
#include <Windows.h>

#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

#define MD5STEP(f, w, x, y, z, data, s) ( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

#pragma warning (disable : 5033)

static void MD5Transform(unsigned int buf[4], unsigned int const in[16])
{
	/*register*/ unsigned int a, b, c, d;

	a = buf[0];
	b = buf[1];
	c = buf[2];
	d = buf[3];

	MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

void MD5Init(MD5Context_t* ctx)
{
	ctx->buf[0] = 0x67452301;
	ctx->buf[1] = 0xefcdab89;
	ctx->buf[2] = 0x98badcfe;
	ctx->buf[3] = 0x10325476;

	ctx->bits[0] = 0;
	ctx->bits[1] = 0;
}

void MD5Update(MD5Context_t* ctx, unsigned char const* buf, unsigned int len)
{
	unsigned int t;

	t = ctx->bits[0];

	if ((ctx->bits[0] = t + ((unsigned int)len << 3)) < t)
		ctx->bits[1]++;

	ctx->bits[1] += len >> 29;

	t = (t >> 3) & 0x3f;

	if (t)
	{
		unsigned char* p = (unsigned char*)ctx->in + t;

		t = 64 - t;
		if (len < t)
		{
			memcpy(p, buf, len);
			return;
		}
		memcpy(p, buf, t);
		MD5Transform(ctx->buf, (unsigned int*)ctx->in);
		buf += t;
		len -= t;
	}

	while (len >= 64)
	{
		memcpy(ctx->in, buf, 64);
		MD5Transform(ctx->buf, (unsigned int*)ctx->in);
		buf += 64;
		len -= 64;
	}

	memcpy(ctx->in, buf, len);
}

void MD5Final(unsigned char digest[MD5_DIGEST_LENGTH], MD5Context_t* ctx)
{
	unsigned count;
	unsigned char* p;

	count = (ctx->bits[0] >> 3) & 0x3F;
	p = ctx->in + count;
	*p++ = 0x80;

	count = 64 - 1 - count;

	if (count < 8)
	{
		memset(p, 0, count);
		MD5Transform(ctx->buf, (unsigned int*)ctx->in);
		memset(ctx->in, 0, 56);
	}

	else
	{
		memset(p, 0, count - 8);
	}

	((unsigned int*)ctx->in)[14] = ctx->bits[0];
	((unsigned int*)ctx->in)[15] = ctx->bits[1];

	MD5Transform(ctx->buf, (unsigned int*)ctx->in);
	memcpy(digest, ctx->buf, MD5_DIGEST_LENGTH);
	memset(ctx, 0, sizeof(*ctx));
}

unsigned int MD5_PseudoRandom(unsigned int nSeed)
{
	MD5Context_t ctx;
	unsigned char digest[MD5_DIGEST_LENGTH];

	memset(&ctx, 0, sizeof(ctx));

	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char*)&nSeed, sizeof(nSeed));
	MD5Final(digest, &ctx);

	return *(unsigned int*)(digest + 6);
}

bool MD5_Compare(const MD5Value_t& data, const MD5Value_t& compare)
{
	return memcmp(data.bits, compare.bits, MD5_DIGEST_LENGTH) == 0;
}

void MD5Value_t::Zero()
{
	memset(bits, 0, sizeof(bits));
}

// ================ SDK/Definitions/Misc/ChecksumCRC.cpp ================
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Generic CRC functions
//
//=============================================================================//

#include <cstdint>

#define CRC32_INIT_VALUE 0xFFFFFFFFUL
#define CRC32_XOR_VALUE  0xFFFFFFFFUL
#define LittleLong( val )( val )

#define NUM_BYTES 256
static const CRC32_t pulCRCTable[NUM_BYTES] =
{
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

void CRC32_Init(CRC32_t* pulCRC)
{
    *pulCRC = CRC32_INIT_VALUE;
}

void CRC32_Final(CRC32_t* pulCRC)
{
    *pulCRC ^= CRC32_XOR_VALUE;
}

CRC32_t	CRC32_GetTableEntry(unsigned int slot)
{
    return pulCRCTable[(unsigned char)slot];
}

#pragma warning (disable : 26819)

void CRC32_ProcessBuffer(CRC32_t* pulCRC, const void* pBuffer, int nBuffer)
{
    CRC32_t ulCrc = *pulCRC;
    unsigned char* pb = (unsigned char*)pBuffer;
    unsigned int nFront;
    int nMain;

JustAfew:

    switch (nBuffer)
    {
    case 7:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);

    case 6:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);

    case 5:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);

    case 4:
        ulCrc ^= LittleLong(*(CRC32_t*)pb);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        *pulCRC = ulCrc;
        return;

    case 3:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);

    case 2:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);

    case 1:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);

    case 0:
        *pulCRC = ulCrc;
        return;
    }

    // We may need to do some alignment work up front, and at the end, so that
    // the main loop is aligned and only has to worry about 8 byte at a time.
    //
    // The low-order two bits of pb and nBuffer in total control the
    // upfront work.
    //
    nFront = uintptr_t(pb) & 3;
    nBuffer -= nFront;
    switch (nFront)
    {
    case 3:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);
    case 2:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);
    case 1:
        ulCrc = pulCRCTable[*pb++ ^ (unsigned char)ulCrc] ^ (ulCrc >> 8);
    }

    nMain = nBuffer >> 3;
    while (nMain--)
    {
        ulCrc ^= LittleLong(*(CRC32_t*)pb);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc ^= LittleLong(*(CRC32_t*)(pb + 4));
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        ulCrc = pulCRCTable[(unsigned char)ulCrc] ^ (ulCrc >> 8);
        pb += 8;
    }

    nBuffer &= 7;
    goto JustAfew;
}

// ================ SDK/Definitions/Misc/bitbuf.cpp ================
inline void Q_memcpy(void* dest, const void* src, int count)
{
	int i;
	if (((uintptr_t(dest) | uintptr_t(src) | count) & 3) == 0)
	{
		count >>= 2;
		for (i = 0; i < count; i++)
			static_cast<int*>(dest)[i] = ((int*)src)[i];
	}
	else
	{
		for (i = 0; i < count; i++)
			static_cast<char*>(dest)[i] = ((char*)src)[i];
	}
}

// GetBitForBitnum lives in defs.h (bitvec) -- one definition for the whole TU.

void bf_write::StartWriting(void* pData, int nBytes, int iStartBit, int nBits)
{
	//using fn = int(__fastcall *)(bf_write *, void *, int, int, int);
	//static fn FN = reinterpret_cast<fn>(g_Pattern.Find(_(L"engine.dll"), _(L"55 8B EC 8B 45 08 8B 55 0C 83 E2 FC 89 01 8B 45 14")));
	//return FN(this, pData, nBytes, iStartBit, nBits);
	if (!(nBytes % 4 == 0))
		return;

	if (!((uintptr_t(pData) & 3) == 0))
		return;

	nBytes &= ~3;

	m_pData = (unsigned long*)pData;
	m_nDataBytes = nBytes;

	if (nBits == -1)
		m_nDataBits = nBytes << 3;
	else if (nBits <= nBytes * 8)
		m_nDataBits = nBits;

	m_iCurBit = iStartBit;
	m_bOverflow = false;
}

#include <stdint.h>

//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

// FIXME: Can't use this until we get multithreaded allocations in tier0 working for tools
// This is used by VVIS and fails to link
// NOTE: This must be the last file included!!!
//#include "tier0/memdbgon.h"

#define	COORD_INTEGER_BITS			14
#define COORD_FRACTIONAL_BITS		5
#define COORD_DENOMINATOR			(1<<(COORD_FRACTIONAL_BITS))
#define COORD_RESOLUTION			(1.0/(COORD_DENOMINATOR))

// Special threshold for networking multiplayer origins
#define COORD_INTEGER_BITS_MP		11
#define COORD_FRACTIONAL_BITS_MP_LOWPRECISION 3
#define COORD_DENOMINATOR_LOWPRECISION			(1<<(COORD_FRACTIONAL_BITS_MP_LOWPRECISION))
#define COORD_RESOLUTION_LOWPRECISION			(1.0/(COORD_DENOMINATOR_LOWPRECISION))

#define NORMAL_FRACTIONAL_BITS		11
#define NORMAL_DENOMINATOR			( (1<<(NORMAL_FRACTIONAL_BITS)) - 1 )
#define NORMAL_RESOLUTION			(1.0/(NORMAL_DENOMINATOR))

// this is limited by the network fractional bits used for coords
// because net coords will be only be accurate to 5 bits fractional
// Standard collision test epsilon
// 1/32nd inch collision epsilon

#define FAST_BIT_SCAN 1
#include <intrin.h>
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)

static BitBufErrorHandler g_BitBufErrorHandler = 0;

inline int BitForBitnum(int bitnum)
{
	return GetBitForBitnum(bitnum);
}

void InternalBitBufErrorHandler(BitBufErrorType errorType, const char* pDebugName)
{
	if (g_BitBufErrorHandler)
		g_BitBufErrorHandler(errorType, pDebugName);
}

void SetBitBufErrorHandler(BitBufErrorHandler fn)
{
	g_BitBufErrorHandler = fn;
}

// #define BB_PROFILING

unsigned long g_LittleBits[32];

// Precalculated bit masks for WriteUBitLong. Using these tables instead of 
// doing the calculations gives a 33% speedup in WriteUBitLong.
unsigned long g_BitWriteMasks[32][33];

// (1 << i) - 1
unsigned long g_ExtraMasks[33];

class CBitWriteMasksInit
{
public:
	CBitWriteMasksInit()
	{
		for (unsigned int startbit = 0; startbit < 32; startbit++)
		{
			for (unsigned int nBitsLeft = 0; nBitsLeft < 33; nBitsLeft++)
			{
				unsigned int endbit = startbit + nBitsLeft;
				g_BitWriteMasks[startbit][nBitsLeft] = BitForBitnum(startbit) - 1;
				if (endbit < 32)
					g_BitWriteMasks[startbit][nBitsLeft] |= ~(BitForBitnum(endbit) - 1);
			}
		}

		for (unsigned int maskBit = 0; maskBit < 32; maskBit++)
			g_ExtraMasks[maskBit] = BitForBitnum(maskBit) - 1;
		g_ExtraMasks[32] = ~0ul;

		for (unsigned int littleBit = 0; littleBit < 32; littleBit++)
			StoreLittleDWord(&g_LittleBits[littleBit], 0, 1u << littleBit);
	}
};
static CBitWriteMasksInit g_BitWriteMasksInit;

// ---------------------------------------------------------------------------------------- //
// bf_write
// ---------------------------------------------------------------------------------------- //

bf_write::bf_write()
{
	m_pData = NULL;
	m_nDataBytes = 0;
	m_nDataBits = -1; // set to -1 so we generate overflow on any operation
	m_iCurBit = 0;
	m_bOverflow = false;
	m_bAssertOnOverflow = true;
	m_pDebugName = NULL;
}

bf_write::bf_write(const char* pDebugName, void* pData, int nBytes, int nBits)
{
	m_bAssertOnOverflow = true;
	m_pDebugName = pDebugName;
	StartWriting(pData, nBytes, 0, nBits);
}

bf_write::bf_write(void* pData, int nBytes, int nBits)
{
	m_bAssertOnOverflow = true;
	m_pDebugName = NULL;
	StartWriting(pData, nBytes, 0, nBits);
}

void bf_write::Reset()
{
	m_iCurBit = 0;
	m_bOverflow = false;
}

void bf_write::SetAssertOnOverflow(bool bAssert)
{
	m_bAssertOnOverflow = bAssert;
}

const char* bf_write::GetDebugName()
{
	return m_pDebugName;
}

void bf_write::SetDebugName(const char* pDebugName)
{
	m_pDebugName = pDebugName;
}

// Sign bit comes first
void bf_write::WriteSBitLong(int data, int numbits)
{
	// Force the sign-extension bit to be correct even in the case of overflow.
	int nValue = data;
	int nPreserveBits = (0x7FFFFFFF >> (32 - numbits));
	int nSignExtension = (nValue >> 31) & ~nPreserveBits;
	nValue &= nPreserveBits;
	nValue |= nSignExtension;

	nullAssert(nValue == data, "WriteSBitLong: 0x%08x does not fit in %d bits", data, numbits);

	WriteUBitLong(nValue, numbits, false);
}

int	bf_write::ByteSizeVarInt32(uint32_t data)
{
	int size = 1;
	while (data > 0x7F)
	{
		size++;
		data >>= 7;
	}
	return size;
}

int	bf_write::ByteSizeVarInt64(uint64_t data)
{
	int size = 1;
	while (data > 0x7F)
	{
		size++;
		data >>= 7;
	}
	return size;
}

//int bf_write::ByteSizeSignedVarInt32(int32_t data)
//{
//	return ByteSizeVarInt32(bitbuf::ZigZagEncode32(data));
//}
//
//int bf_write::ByteSizeSignedVarInt64(int64_t data)
//{
//	return ByteSizeVarInt64(bitbuf::ZigZagEncode64(data));
//}

bool bf_write::WriteBits(const void* pInData, int nBits)
{

	unsigned char* pOut = (unsigned char*)pInData;
	int nBitsLeft = nBits;

	// Bounds checking..
	if ((m_iCurBit + nBits) > m_nDataBits)
	{
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
		return false;
	}

	// Align output to dword boundary
	while ((uintptr_t(pOut) & 3) != 0 && nBitsLeft >= 8)
	{

		WriteUBitLong(*pOut, 8, false);
		++pOut;
		nBitsLeft -= 8;
	}

	if ((nBitsLeft >= 32) && (m_iCurBit & 7) == 0)
	{
		// current bit is char aligned, do block copy
		int numbytes = nBitsLeft >> 3;
		int numbits = numbytes << 3;

		Q_memcpy((char*)m_pData + (m_iCurBit >> 3), pOut, numbytes);
		pOut += numbytes;
		nBitsLeft -= numbits;
		m_iCurBit += numbits;
	}

	// X360TBD: Can't write dwords in WriteBits because they'll get swapped
	if (nBitsLeft >= 32)
	{
		unsigned long iBitsRight = (m_iCurBit & 31);
		unsigned long iBitsLeft = 32 - iBitsRight;
		unsigned long bitMaskLeft = g_BitWriteMasks[iBitsRight][32];
		unsigned long bitMaskRight = g_BitWriteMasks[0][iBitsRight];

		unsigned long* pData = &m_pData[m_iCurBit >> 5];

		// Read dwords.
		while (nBitsLeft >= 32)
		{
			unsigned long curData = *(unsigned long*)pOut;
			pOut += sizeof(unsigned long);

			*pData &= bitMaskLeft;
			*pData |= curData << iBitsRight;

			pData++;

			if (iBitsLeft < 32)
			{
				curData >>= iBitsLeft;
				*pData &= bitMaskRight;
				*pData |= curData;
			}

			nBitsLeft -= 32;
			m_iCurBit += 32;
		}
	}

	// write remaining bytes
	while (nBitsLeft >= 8)
	{
		WriteUBitLong(*pOut, 8, false);
		++pOut;
		nBitsLeft -= 8;
	}

	// write remaining bits
	if (nBitsLeft)
	{
		WriteUBitLong(*pOut, nBitsLeft, false);
	}

	return !IsOverflowed();
}

// ---------------------------------------------------------------------------------------- //
// bf_read
// ---------------------------------------------------------------------------------------- //

bf_read::bf_read()
{
	m_pData = NULL;
	m_nDataBytes = 0;
	m_nDataBits = -1; // set to -1 so we overflow on any operation
	m_iCurBit = 0;
	m_bOverflow = false;
	m_bAssertOnOverflow = true;
	m_pDebugName = NULL;
}

bf_read::bf_read(const void* pData, int nBytes, int nBits)
{
	m_bAssertOnOverflow = true;
	StartReading(pData, nBytes, 0, nBits);
}

bf_read::bf_read(const char* pDebugName, const void* pData, int nBytes, int nBits)
{
	m_bAssertOnOverflow = true;
	m_pDebugName = pDebugName;
	StartReading(pData, nBytes, 0, nBits);
}

void bf_read::StartReading(const void* pData, int nBytes, int iStartBit, int nBits)
{
	// Make sure we're dword aligned.
	nullAssert(((size_t)pData & 3) == 0);

	m_pData = (unsigned long*)pData;
	m_nDataBytes = nBytes;

	if (nBits == -1)
	{
		m_nDataBits = m_nDataBytes << 3;
	}
	else
	{
		nullAssert(nBits <= nBytes * 8);
		m_nDataBits = nBits;
	}

	m_iCurBit = iStartBit;
	m_bOverflow = false;
}

void bf_read::Reset()
{
	m_iCurBit = 0;
	m_bOverflow = false;
}

void bf_read::SetAssertOnOverflow(bool bAssert)
{
	m_bAssertOnOverflow = bAssert;
}

void bf_read::SetDebugName(const char* pName)
{
	m_pDebugName = pName;
}

void bf_read::SetOverflowFlag()
{
	if (m_bAssertOnOverflow)
	{
		nullAssert(false);
	}
	m_bOverflow = true;
}

void bf_read::ReadBits(void* pOutData, int nBits)
{

	unsigned char* pOut = (unsigned char*)pOutData;
	int nBitsLeft = nBits;

	// align output to dword boundary
	while (((size_t)pOut & 3) != 0 && nBitsLeft >= 8)
	{
		*pOut = (unsigned char)ReadUBitLong(8);
		++pOut;
		nBitsLeft -= 8;
	}

	// read dwords
	while (nBitsLeft >= 32)
	{
		*((unsigned long*)pOut) = ReadUBitLong(32);
		pOut += sizeof(unsigned long);
		nBitsLeft -= 32;
	}

	// read remaining bytes
	while (nBitsLeft >= 8)
	{
		*pOut = ReadUBitLong(8);
		++pOut;
		nBitsLeft -= 8;
	}

	// read remaining bits
	if (nBitsLeft)
	{
		*pOut = ReadUBitLong(nBitsLeft);
	}

}

int bf_read::ReadBitsClamped_ptr(void* pOutData, size_t outSizeBytes, size_t nBits)
{
	size_t outSizeBits = outSizeBytes * 8;
	size_t readSizeBits = nBits;
	int skippedBits = 0;
	if (readSizeBits > outSizeBits)
	{
		// Should we print a message when we clamp the data being read? Only
		// in debug builds I think.
		nullAssert(0, "Oversized network packet received, and clamped.");
		readSizeBits = outSizeBits;
		skippedBits = (int)(nBits - outSizeBits);
		// What should we do in this case, which should only happen if nBits
		// is negative for some reason?
		//if ( skippedBits < 0 )
		//	return 0;
	}

	ReadBits(pOutData, int(readSizeBits));
	SeekRelative(skippedBits);

	// Return the number of bits actually read.
	return int(readSizeBits);
}

bool bf_read::ReadString(char* pStr, int maxLen, bool bLine, int* pOutNumChars)
{
	nullAssert(maxLen != 0);

	bool bTooSmall = false;
	int iChar = 0;
	while (1)
	{
		char val = ReadChar();
		if (val == 0)
			break;
		else if (bLine && val == '\n')
			break;

		if (iChar < (maxLen - 1))
		{
			pStr[iChar] = val;
			++iChar;
		}
		else
		{
			bTooSmall = true;
		}
	}

	// Make sure it's null-terminated.
	nullAssert(iChar < maxLen);
	pStr[iChar] = 0;

	if (pOutNumChars)
		*pOutNumChars = iChar;

	return !IsOverflowed() && !bTooSmall;
}

char* bf_read::ReadAndAllocateString(bool* pOverflow)
{
	char str[2048];

	int nChars;
	bool bOverflow = !ReadString(str, sizeof(str), false, &nChars);
	if (pOverflow)
		*pOverflow = bOverflow;

	// Now copy into the output and return it;
	char* pRet = new char[nChars + 1];
	for (int i = 0; i <= nChars; i++)
		pRet[i] = str[i];

	return pRet;
}

// ================ SDK/Definitions/NullInterfaces.cpp ================
#define Validate(x) if (!x) { U::Core.AppendFailText("CNullInterfaces::Initialize() failed to initialize "#x); m_bFailed = true; }

// Interfaces that are not in the engine's CreateInterface tables and have to be fetched by hand.
bool CNullInterfaces::Initialize()
{
	I::KeyValuesSystem = U::Memory.GetModuleExport<IKeyValuesSystem*(*)()>("vstdlib.dll", "KeyValuesSystem")();
	Validate(I::KeyValuesSystem);

	return !m_bFailed;
}
