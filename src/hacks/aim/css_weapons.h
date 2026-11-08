#pragma once
#include "css.h"   // SDK unit: CSWeaponID (cs_weapon_parse.h verbatim), CWeaponCSBase, CCSPlayer
#include <unordered_map>
#include <algorithm>

// Weapon-CLASS table for the aim half, on top of the SDK unit's css.h CSWeaponID (this file used
// to carry its own duplicate copy of that enum -- removed once css.h landed, see report).
//
// botox csgo-sdk hacks/aimbot/aimbot.cpp weapon_class_vars() 1:1, re-keyed to CS:Source's weapon
// list (CSS has no CZ75/P2000/Tec-9/MP9/MP5SD/M4A1-S/SCAR-20/MAG-7/SG556/Negev/R8 -- those are
// CS:GO-only additions; CSS equivalents below are its actual 2004-era weapon set, per
// cs_weapon_parse.h).
enum class ECSWeaponClass
{
	None,
	Pistol,       // P228, Glock, USP, Five-Seven, Elite
	HeavyPistol,  // Deagle (botox weapon_deagle/weapon_revolver -> deagle only, CSS has no revolver)
	SMG,          // MAC-10, MP5 Navy, UMP45, TMP, P90
	Rifle,        // Galil, Famas, AK47, AUG, M4A1, SG552
	Sniper,       // AWP, SG550, G3SG1 (botox weapon_awp/g3sg1/scar20)
	Scout,        // Scout (botox weapon_ssg08)
	Heavy,        // M3, XM1014, M249 (botox nova/xm1014/sawedoff/m249/negev/mag7)
};

inline ECSWeaponClass CSWeaponClass(const int id)
{
	switch (id)
	{
	case WEAPON_P228:
	case WEAPON_GLOCK:
	case WEAPON_USP:
	case WEAPON_FIVESEVEN:
	case WEAPON_ELITE:
		return ECSWeaponClass::Pistol;
	case WEAPON_DEAGLE:
		return ECSWeaponClass::HeavyPistol;
	case WEAPON_MAC10:
	case WEAPON_MP5NAVY:
	case WEAPON_UMP45:
	case WEAPON_TMP:
	case WEAPON_P90:
		return ECSWeaponClass::SMG;
	case WEAPON_GALIL:
	case WEAPON_FAMAS:
	case WEAPON_AK47:
	case WEAPON_AUG:
	case WEAPON_M4A1:
	case WEAPON_SG552:
		return ECSWeaponClass::Rifle;
	case WEAPON_AWP:
	case WEAPON_SG550:
	case WEAPON_G3SG1:
		return ECSWeaponClass::Sniper;
	case WEAPON_SCOUT:
		return ECSWeaponClass::Scout;
	case WEAPON_M3:
	case WEAPON_XM1014:
	case WEAPON_M249:
		return ECSWeaponClass::Heavy;
	default:
		return ECSWeaponClass::None;
	}
}

// botox weapon_class_vars()->m_sniper gate (scope-only hitbox narrowing)
inline bool CSWeaponIsSniperClass(const int id)
{
	const auto cls = CSWeaponClass(id);
	return cls == ECSWeaponClass::Sniper || cls == ECSWeaponClass::Scout;
}

inline bool CSWeaponIsGun(const int id)
{
	return CSWeaponClass(id) != ECSWeaponClass::None;
}

// botox grenade_slot(): menu order flash/HE/smoke. CSS has NO molotov/incendiary, decoy or
// tagrenade (those are CS:GO additions, cs_weapon_parse.h confirms no such entries) -- 3 slots,
// not 6. User direction: "if something truly can't exist in CSS, say so" -- said here + in report.
// css_bridge.cpp CssBridge_IsScoped, 1:1: CSS has no m_bIsScoped netvar (CS2/CS:GO-only) -- FOV
// dropping below the default 90 (weapon_csbase.cpp GetFOV()/GetDefaultFOV()) is the real signal.
inline bool CSSIsScoped(CBasePlayer* pPlayer)
{
	return pPlayer && pPlayer->m_iFOV() > 0 && pPlayer->m_iFOV() < 90;
}

inline int CSGrenadeSlot(const int id)
{
	switch (id)
	{
	case WEAPON_FLASHBANG: return 0;
	case WEAPON_HEGRENADE: return 1;
	case WEAPON_SMOKEGRENADE: return 2;
	default: return -1;
	}
}

// ================================================================================================
// Per-weapon script data (damage/range/penetration/cycle time only -- AGENT_BRIEF item 1 moved
// the spread/inaccuracy model OFF these baked script numbers and onto the real live vtable calls,
// CWeaponCSBase::GetSpread()/GetInaccuracy()/UpdateAccuracyPenalty() in css.h, slots 383/382/384,
// verified against the live client.dll disassembly (docs/port/vtable_check.md) -- not guessed).
// Remaining fields verbatim from the real shipped scripts,
// resources\css-community-master\mp\game\community\scripts\weapon_<name>.txt. NOT loaded at
// runtime via IFileSystem: this SDK's IFileSystem/IBaseFileSystem (defs.h ~7056) is a
// forward-declared, body-dropped stub with no Open/Read/Close surface to call -- wiring it is a
// real ask, not a guess, and belongs with the SDK unit (NEEDS FROM OTHERS, see report). Baking the
// file's own numbers in here (same values the engine would have parsed) is the correct stopgap.
struct CSWeaponScriptData
{
	int   m_iPenetration = 0;
	int   m_iDamage = 0;
	float m_flRange = 8192.f;
	float m_flRangeModifier = 1.f;
	int   m_iBullets = 1;
	float m_flCycleTime = 0.15f;
	float m_flArmorRatio = 1.f;
};

// weapon_<name>.txt, verbatim field-for-field (penetration, damage, range, rangeModifier, bullets,
// cycleTime, armorRatio)
inline const CSWeaponScriptData* CSWeaponScript(const int id)
{
	static const std::unordered_map<int, CSWeaponScriptData> kTable =
	{
		{ WEAPON_AK47,     { 2, 36,  8192.f, 0.98f,  1, 0.100f,  1.55f } },
		{ WEAPON_AUG,      { 2, 32,  8192.f, 0.96f,  1, 0.090f,  1.40f } },
		{ WEAPON_AWP,      { 3, 115, 8192.f, 0.99f,  1, 1.500f,  1.95f } },
		{ WEAPON_DEAGLE,   { 2, 54,  4096.f, 0.81f,  1, 0.225f,  1.50f } },
		{ WEAPON_ELITE,    { 1, 45,  4096.f, 0.75f,  1, 0.120f,  1.05f } },
		{ WEAPON_FAMAS,    { 2, 30,  8192.f, 0.96f,  1, 0.090f,  1.40f } },
		{ WEAPON_FIVESEVEN,{ 1, 25,  4096.f, 0.885f, 1, 0.150f,  1.50f } },
		{ WEAPON_G3SG1,    { 3, 80,  8192.f, 0.98f,  1, 0.250f,  1.65f } },
		{ WEAPON_GALIL,    { 2, 30,  8192.f, 0.98f,  1, 0.090f,  1.55f } },
		{ WEAPON_GLOCK,    { 1, 25,  4096.f, 0.75f,  1, 0.150f,  1.05f } },
		{ WEAPON_USP,      { 1, 34,  4096.f, 0.79f,  1, 0.150f,  1.00f } },
		{ WEAPON_M249,     { 2, 35,  8192.f, 0.97f,  1, 0.080f,  1.60f } },
		{ WEAPON_M3,       { 1, 26,  3000.f, 0.70f,  9, 0.880f,  1.00f } },
		{ WEAPON_M4A1,     { 2, 33,  8192.f, 0.97f,  1, 0.090f,  1.40f } },
		{ WEAPON_MAC10,    { 1, 29,  4096.f, 0.82f,  1, 0.075f,  0.95f } },
		{ WEAPON_MP5NAVY,  { 1, 26,  4096.f, 0.84f,  1, 0.080f,  1.00f } },
		{ WEAPON_P228,     { 1, 40,  4096.f, 0.80f,  1, 0.150f,  1.25f } },
		{ WEAPON_P90,      { 1, 26,  4096.f, 0.84f,  1, 0.070f,  1.50f } },
		{ WEAPON_SCOUT,    { 3, 75,  8192.f, 0.98f,  1, 1.250f,  1.70f } },
		{ WEAPON_SG550,    { 2, 70,  8192.f, 0.98f,  1, 0.250f,  1.45f } },
		{ WEAPON_SG552,    { 2, 33,  8192.f, 0.955f, 1, 0.090f,  1.40f } },
		{ WEAPON_TMP,      { 1, 26,  4096.f, 0.84f,  1, 0.070f,  1.00f } },
		{ WEAPON_UMP45,    { 1, 30,  4096.f, 0.82f,  1, 0.105f,  1.00f } },
		{ WEAPON_XM1014,   { 1, 22,  3000.f, 0.70f,  6, 0.250f,  1.00f } },
		// grenades/knife/C4: not fired weapons, penetration/damage/range kept for completeness.
		{ WEAPON_HEGRENADE,    { 1, 50, 4096.f, 0.99f, 1, 0.f, 1.475f } },
		{ WEAPON_FLASHBANG,    { 1, 50, 4096.f, 0.99f, 1, 0.f, 1.f    } },
		{ WEAPON_SMOKEGRENADE, { 0, 50, 4096.f, 0.99f, 1, 0.f, 1.f    } },
		{ WEAPON_KNIFE,        { 1, 50, 4096.f, 0.99f, 1, 0.f, 1.70f  } },
		{ WEAPON_C4,           { 1, 50, 4096.f, 0.99f, 1, 0.f, 1.f    } },
	};
	auto it = kTable.find(id);
	return it != kTable.end() ? &it->second : nullptr;
}
