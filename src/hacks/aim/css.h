#pragma once

#include "defs.h"

// ================ CSS game API (unit SDK) ================
// Every CSS-specific class/netvar/weapon id here is sourced from:
//   css-community-master/mp/src/game/{shared,client,server}/cstrike/*
//   (cs_weapon_parse.h, weapon_csbase.h, c_cs_player.h/.cpp, c_plantedc4.h/.cpp,
//    cs_shareddefs.h, cs_player_resource.cpp, c_cs_playerresource.cpp)
// Netvar table names follow this SDK's existing convention (defs.h): the
// server-side C++ class name (e.g. "CCSPlayer", "CPlantedC4"), matched against
// ClientClass::m_pNetworkName by CNetVars::GetNetVar. Confirmed against the
// IMPLEMENT_CLIENTCLASS_DT/IMPLEMENT_NETWORKCLASS_ALIASED 2nd/3rd macro args in
// the .cpp files above (same pattern as this SDK's existing CTFPlayer/CBasePlayer
// netvars, which use the bare server classname, not "C_"-prefixed or "DT_"-prefixed).
//
// Weapon virtual calls GetInaccuracy(382)/GetSpread(383)/UpdateAccuracyPenalty(384) are REAL,
// verified absolute vtable slots on C_WeaponCSBase in the live client.dll (2025 x64 update
// added CS:GO's accuracy-penalty model to CSS). Verified by disassembling the slots directly
// (tools/css_spread_slots.py, docs/port/vtable_check.md), not by trusting cssourcex64's own
// naming -- cssourcex64 Features/Aimbot.cpp:112-160 has 382/383 swapped (calls 382 "spread",
// 383 "inaccuracy"); disassembly of slot 382 shows a velocity term + wpndata[+0x914] (the
// "move inaccuracy" shape) added to this+0xcb0 -- that's GetInaccuracy. Slot 383 shows the
// legacy-cvar check (dword [ConVar+0x58]==1 -> return 0.f) then wpndata[+0x8dc] (a flat
// per-mode table lookup, no velocity/penalty term) -- that's GetSpread. Slot 384 disassembles
// to a straight write of this+0xcb0 (m_fAccuracyPenalty) -- UpdateAccuracyPenalty. See below.

// ---- Teams (cs_shareddefs.h) ----
#define CS_TEAM_SPECTATOR TEAM_SPECTATOR  // 1, generic (defs.h)
#define CS_TEAM_TERRORIST 2
#define CS_TEAM_CT 3

// ---- Player join-state (cs_shareddefs.h CSPlayerState) ----
enum CSPlayerState
{
	CSSTATE_ACTIVE = 0,
	CSSTATE_WELCOME,
	CSSTATE_PICKINGTEAM,
	CSSTATE_PICKINGCLASS,
	CSSTATE_DEATH_ANIM,
	CSSTATE_DEATH_WAIT_FOR_KEY,
	CSSTATE_OBSERVER_MODE,
};

// ---- Addon bits (cs_shareddefs.h, CCSPlayer::m_iAddonBits) ----
#define CS_ADDON_FLASHBANG_1 0x001
#define CS_ADDON_FLASHBANG_2 0x002
#define CS_ADDON_HE_GRENADE  0x004
#define CS_ADDON_SMOKE_GRENADE 0x008
#define CS_ADDON_C4          0x010
#define CS_ADDON_DEFUSEKIT   0x020
#define CS_ADDON_PRIMARY     0x040
#define CS_ADDON_PISTOL      0x080
#define CS_ADDON_PISTOL2     0x100

// ---- Weapon class/type (cs_weapon_parse.h, verbatim) ----
enum CSWeaponType
{
	WEAPONTYPE_KNIFE = 0,
	WEAPONTYPE_PISTOL,
	WEAPONTYPE_SUBMACHINEGUN,
	WEAPONTYPE_RIFLE,
	WEAPONTYPE_SHOTGUN,
	WEAPONTYPE_SNIPER_RIFLE,
	WEAPONTYPE_MACHINEGUN,
	WEAPONTYPE_C4,
	WEAPONTYPE_GRENADE,
	WEAPONTYPE_UNKNOWN
};

// ---- Weapon id (cs_weapon_parse.h / cssourcex64 pch.h, identical in both, verbatim) ----
enum CSWeaponID
{
	WEAPON_NONE = 0,
	WEAPON_P228,
	WEAPON_GLOCK,
	WEAPON_SCOUT,
	WEAPON_HEGRENADE,
	WEAPON_XM1014,
	WEAPON_C4,
	WEAPON_MAC10,
	WEAPON_AUG,
	WEAPON_SMOKEGRENADE,
	WEAPON_ELITE,
	WEAPON_FIVESEVEN,
	WEAPON_UMP45,
	WEAPON_SG550,
	WEAPON_GALIL,
	WEAPON_FAMAS,
	WEAPON_USP,
	WEAPON_AWP,
	WEAPON_MP5NAVY,
	WEAPON_M249,
	WEAPON_M3,
	WEAPON_M4A1,
	WEAPON_TMP,
	WEAPON_G3SG1,
	WEAPON_FLASHBANG,
	WEAPON_DEAGLE,
	WEAPON_SG552,
	WEAPON_AK47,
	WEAPON_KNIFE,
	WEAPON_P90,
	WEAPON_SHIELDGUN, // never shipped, kept for enum-index parity with CSS source
	WEAPON_KEVLAR,    // not an entity, equipment flag only
	WEAPON_ASSAULTSUIT,
	WEAPON_NVG,
	WEAPON_MAX,
};

// ---- Grenade/bomb projectile entity classnames (LINK_ENTITY_TO_CLASS, verified in source) ----
#define CS_CLASSNAME_HEGRENADE_PROJ   "hegrenade_projectile"
#define CS_CLASSNAME_FLASHBANG_PROJ   "flashbang_projectile"
#define CS_CLASSNAME_SMOKEGRENADE_PROJ "smokegrenade_projectile"
#define CS_CLASSNAME_PLANTED_C4       "planted_c4"
#define CS_CLASSNAME_HOSTAGE          "hostage_entity"

// classname (SERVER class name, ClientClass::m_pNetworkName / GetClientClass()->GetName())
// -> CSWeaponID. "DT_*" is a send/recv TABLE name, never what GetName() returns -- it returns
// the 3rd arg of IMPLEMENT_CLIENTCLASS_DT/_NOBASE (c_*.cpp) or the name IMPLEMENT_NETWORKCLASS_
// ALIASED's 1st arg expands to server-side (same string LINK_ENTITY_TO_CLASS's 2nd arg uses --
// see css-community-master weapon_*.cpp). Every entry below confirmed present as a literal
// string in the live client.dll (tools/css_classnames.py).
inline CSWeaponID CSS_WeaponIDFromClientClassName(const char* sName)
{
	if (!sName) return WEAPON_NONE;
	static const struct { const char* sClass; CSWeaponID eID; } kTable[] = {
		{ "CWeaponP228", WEAPON_P228 }, { "CWeaponGlock", WEAPON_GLOCK },
		{ "CWeaponScout", WEAPON_SCOUT }, { "CHEGrenade", WEAPON_HEGRENADE },
		{ "CWeaponXM1014", WEAPON_XM1014 }, { "CC4", WEAPON_C4 },
		{ "CWeaponMAC10", WEAPON_MAC10 }, { "CWeaponAug", WEAPON_AUG },
		{ "CSmokeGrenade", WEAPON_SMOKEGRENADE }, { "CWeaponElite", WEAPON_ELITE },
		{ "CWeaponFiveSeven", WEAPON_FIVESEVEN }, { "CWeaponUMP45", WEAPON_UMP45 },
		{ "CWeaponSG550", WEAPON_SG550 }, { "CWeaponGalil", WEAPON_GALIL },
		{ "CWeaponFamas", WEAPON_FAMAS }, { "CWeaponUSP", WEAPON_USP },
		{ "CWeaponAWP", WEAPON_AWP }, { "CWeaponMP5Navy", WEAPON_MP5NAVY },
		{ "CWeaponM249", WEAPON_M249 }, { "CWeaponM3", WEAPON_M3 },
		{ "CWeaponM4A1", WEAPON_M4A1 }, { "CWeaponTMP", WEAPON_TMP },
		{ "CWeaponG3SG1", WEAPON_G3SG1 }, { "CFlashbang", WEAPON_FLASHBANG },
		{ "CDEagle", WEAPON_DEAGLE }, { "CWeaponSG552", WEAPON_SG552 },
		{ "CAK47", WEAPON_AK47 }, { "CKnife", WEAPON_KNIFE },
		{ "CWeaponP90", WEAPON_P90 },
	};
	for (auto& e : kTable)
		if (!strcmp(sName, e.sClass))
			return e.eID;
	return WEAPON_NONE;
}

inline CSWeaponType CSS_WeaponTypeFromID(CSWeaponID eID)
{
	switch (eID)
	{
	case WEAPON_KNIFE: return WEAPONTYPE_KNIFE;
	case WEAPON_P228: case WEAPON_GLOCK: case WEAPON_ELITE: case WEAPON_FIVESEVEN:
	case WEAPON_USP: case WEAPON_DEAGLE: return WEAPONTYPE_PISTOL;
	case WEAPON_MAC10: case WEAPON_UMP45: case WEAPON_TMP: case WEAPON_MP5NAVY:
	case WEAPON_P90: return WEAPONTYPE_SUBMACHINEGUN;
	case WEAPON_AUG: case WEAPON_GALIL: case WEAPON_FAMAS: case WEAPON_SG552:
	case WEAPON_AK47: case WEAPON_M4A1: return WEAPONTYPE_RIFLE;
	case WEAPON_XM1014: case WEAPON_M3: return WEAPONTYPE_SHOTGUN;
	case WEAPON_SCOUT: case WEAPON_SG550: case WEAPON_AWP: case WEAPON_G3SG1:
		return WEAPONTYPE_SNIPER_RIFLE;
	case WEAPON_M249: return WEAPONTYPE_MACHINEGUN;
	case WEAPON_C4: return WEAPONTYPE_C4;
	case WEAPON_HEGRENADE: case WEAPON_SMOKEGRENADE: case WEAPON_FLASHBANG:
		return WEAPONTYPE_GRENADE;
	default: return WEAPONTYPE_UNKNOWN;
	}
}

// ---- C_CSPlayer (c_cs_player.h/.cpp, DT_CSPlayer / DT_CSLocalPlayerExclusive) ----
class CCSPlayer : public CBasePlayer
{
public:
	// DT_CSPlayer (c_cs_player.cpp BEGIN_RECV_TABLE(C_CSPlayer, DT_CSPlayer))
	NETVAR(m_iAccount, int, "CCSPlayer", "m_iAccount");
	NETVAR(m_bInBombZone, bool, "CCSPlayer", "m_bInBombZone");
	NETVAR(m_bInBuyZone, bool, "CCSPlayer", "m_bInBuyZone");
	NETVAR(m_iClass, int, "CCSPlayer", "m_iClass"); // CS_CLASS_*, unused by stock gameplay
	NETVAR(m_ArmorValue, int, "CCSPlayer", "m_ArmorValue");
	NETVAR(m_angEyeAnglesX, float, "CCSPlayer", "m_angEyeAngles[0]");
	NETVAR(m_angEyeAnglesY, float, "CCSPlayer", "m_angEyeAngles[1]");
	NETVAR(m_bHasDefuser, bool, "CCSPlayer", "m_bHasDefuser");
	NETVAR(m_bNightVisionOn, bool, "CCSPlayer", "m_bNightVisionOn");
	NETVAR(m_bHasNightVision, bool, "CCSPlayer", "m_bHasNightVision");
	NETVAR(m_bInHostageRescueZone, bool, "CCSPlayer", "m_bInHostageRescueZone");
	NETVAR(m_bIsDefusing, bool, "CCSPlayer", "m_bIsDefusing");
	NETVAR(m_bHasHelmet, bool, "CCSPlayer", "m_bHasHelmet");
	NETVAR(m_flFlashDuration, float, "CCSPlayer", "m_flFlashDuration");
	NETVAR(m_flFlashMaxAlpha, float, "CCSPlayer", "m_flFlashMaxAlpha");
	NETVAR(m_iAddonBits, int, "CCSPlayer", "m_iAddonBits");
	NETVAR(m_iPrimaryAddon, int, "CCSPlayer", "m_iPrimaryAddon");
	NETVAR(m_iSecondaryAddon, int, "CCSPlayer", "m_iSecondaryAddon");
	NETVAR(m_iPlayerState, int, "CCSPlayer", "m_iPlayerState"); // CSPlayerState

	// DT_CSLocalPlayerExclusive (local-player-only, c_cs_player.cpp line ~660)
	NETVAR(m_iShotsFired, int, "CCSPlayer", "m_iShotsFired");
	NETVAR(m_iDirection, int, "CCSPlayer", "m_iDirection");
	NETVAR(m_flVelocityModifier, float, "CCSPlayer", "m_flVelocityModifier");
	// CSS HAS stamina (cs_player.h:569 m_flStamina, predicted c_cs_player.cpp:130; networked
	// twice -- DT_CSLocalPlayerExclusive AND top-level DT_CSPlayer, c_cs_player.cpp:661/690 --
	// same "CCSPlayer" table-name convention as the fields above). Drives jump-height scale
	// (cs_gamemovement.cpp:683-692) and duck speed (cs_gamemovement.cpp:414-418).
	NETVAR(m_flStamina, float, "CCSPlayer", "m_flStamina");

	// generic CBasePlayer observer fields (SDK 2013-standard, not CS-specific,
	// not yet declared on CBasePlayer in defs.h -- added here since CCSPlayer
	// is the only class that currently needs them).
	NETVAR(m_hObserverTarget, EHANDLE, "CCSPlayer", "m_hObserverTarget");
	NETVAR(m_iObserverMode, int, "CCSPlayer", "m_iObserverMode");

	bool IsDefusing() { return m_bIsDefusing(); }
	bool HasDefuser() { return m_bHasDefuser(); }
	bool HasHelmet() { return m_bHasHelmet(); }
	bool HasNightVision() { return m_bHasNightVision(); }
	bool NightVisionOn() { return m_bNightVisionOn(); }
	int Armor() { return m_ArmorValue(); }
	int Account() { return m_iAccount(); }
	int Team() { return m_iTeamNum(); } // CBaseEntity netvar, 2=T 3=CT
	bool IsInBuyZone() { return m_bInBuyZone(); }
	bool IsInBombZone() { return m_bInBombZone(); }
	Vec3 GetEyeAngles() { return { m_angEyeAnglesX(), m_angEyeAnglesY(), 0.f }; }
	CSPlayerState State() { return (CSPlayerState)m_iPlayerState(); }

	// IsVIP() needs CCSPlayerResource::m_iPlayerVIP compared to this entindex();
	// see CCSPlayerResource::IsVIP(iEntIndex) below (matches C_CSPlayer::IsVIP()
	// in c_cs_player.cpp, which goes through the player resource, not a local netvar).
};

// ---- C_WeaponCSBase (weapon_csbase.h, DT_WeaponCSBase) ----
class CWeaponCSBase : public CBaseCombatWeapon
{
public:
	// m_iClip1, m_flNextPrimaryAttack, m_flNextSecondaryAttack, m_hOwner already
	// on CBaseCombatWeapon (defs.h) -- CS:S's DT_WeaponCSBase does not add new
	// top-level nets beyond what weapon_csbase.h shows as plain (unnetworked)
	// members (m_flAccuracy, m_iShotsFired-equivalent is player-side, see above).

	CSWeaponID GetWeaponID()
	{
		auto pClass = GetClientClass();
		return pClass ? CSS_WeaponIDFromClientClassName(pClass->GetName()) : WEAPON_NONE;
	}
	CSWeaponType GetWeaponType() { return CSS_WeaponTypeFromID(GetWeaponID()); }
	bool IsKnife() { return GetWeaponType() == WEAPONTYPE_KNIFE; }
	bool IsPistol() { return GetWeaponType() == WEAPONTYPE_PISTOL; }
	bool IsSniper() { return GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE; }
	bool IsGrenade() { return GetWeaponType() == WEAPONTYPE_GRENADE; }

	// Verified live vtable slots (file header, docs/port/vtable_check.md). UpdateAccuracyPenalty
	// MUST run before reading either getter -- it's the only thing that advances this+0xcb0
	// (m_fAccuracyPenalty), matching FX_FireBullets' own per-shot call order.
	VIRTUAL(GetInaccuracy, float, 382, this);
	VIRTUAL(GetSpread, float, 383, this);
	VIRTUAL(UpdateAccuracyPenalty, void, 384, this);
	// the only field 384 writes (this+0xcb0, disasm 11-04)
	NETVAR(m_fAccuracyPenalty, float, "CWeaponCSBase", "m_fAccuracyPenalty");
	// botox-style "does this weapon even have a spread cone" gate (aim_hitscan.cpp peek/tapfire
	// checks) -- real values, not the old baked-table CSSWeaponCone.
	bool HasSpread() { return GetSpread() + GetInaccuracy() > 0.f; }

	// IsFullAuto: C_WeaponCSBase's own first new virtual (vtable_check.md derive_weapon_slots,
	// slot 369 = C_BaseCombatWeapon's CSS length, disasm head not flagged SUSPECT unlike
	// GetSpread/GetInaccuracy above).
	VIRTUAL(IsFullAuto, bool, 369, this);

	// Burst fire: CSS has it ONLY on famas/glock (weapon_famas.cpp:46/56,
	// weapon_glock.cpp:54/65 -- CNetworkVar(bool, m_bBurstMode), own per-weapon
	// RecvTable, not on DT_WeaponCSBase). Every other weapon has no burst concept.
	NETVAR(m_bBurstModeFamas, bool, "CWeaponFamas", "m_bBurstMode");
	NETVAR(m_bBurstModeGlock, bool, "CWeaponGlock", "m_bBurstMode");
	bool IsBurstMode()
	{
		switch (GetWeaponID())
		{
		case WEAPON_FAMAS: return m_bBurstModeFamas();
		case WEAPON_GLOCK: return m_bBurstModeGlock();
		default: return false;
		}
	}
};

// ---- C_Team (c_team.cpp, DT_Team, IMPLEMENT_CLIENTCLASS_DT_NOBASE -- its OWN
// m_iTeamNum/m_iScore, NOT CBaseEntity's generic m_iTeamNum). Server class name is "CCSTeam" on
// this binary (AIM3, verified live-binary ClientClass::m_pNetworkName, 11-03), not the community
// source's literal 3rd macro arg "CTeam" -- use the verified name for both the NETVAR table and
// any classname compare. ----
class CTeam : public CBaseEntity
{
public:
	NETVAR(m_iTeamNum2, int, "CCSTeam", "m_iTeamNum"); // shadows CBaseEntity::m_iTeamNum on purpose
	NETVAR(m_iScore, int, "CCSTeam", "m_iScore");
};

// ---- CCSGameRules (cs_gamerules.h/.cpp, DT_CSGameRules, embedded via the
// CCSGameRulesProxy entity's RecvPropDataTable("cs_gamerules_data", ..., proxy
// fn) -- the proxy fn redirects to the CCSGameRules global singleton, NOT an
// inline struct on the proxy entity, so this is read off the SAME global
// pointer this SDK already resolves for TF2 (S::TFGameRules sig, verified to
// also hit exactly once in CS:S's client.dll and vtable-match C_CSGameRules,
// see docs/port/sig_check.md:22 and docs/port/vtable_check.md:31 -- same
// engine-generated global-gamerules-accessor shape on this branch). Cast only,
// no new signature. ----
class CCSGameRules
{
public:
	NETVAR(m_bFreezePeriod, bool, "CCSGameRulesProxy", "m_bFreezePeriod");
	NETVAR(m_iRoundTime, int, "CCSGameRulesProxy", "m_iRoundTime");       // seconds, mp_roundtime*60
	NETVAR(m_fRoundStartTime, float, "CCSGameRulesProxy", "m_fRoundStartTime"); // curtime when freeze ends
};
inline CCSGameRules* CSGameRules() { return reinterpret_cast<CCSGameRules*>(I::TFGameRules()); }

// ---- C_PlantedC4 (c_plantedc4.h/.cpp, DT_PlantedC4) ----
class CPlantedC4 : public CBaseAnimating
{
public:
	NETVAR(m_bBombTicking, bool, "CPlantedC4", "m_bBombTicking");
	NETVAR(m_flC4Blow, float, "CPlantedC4", "m_flC4Blow");
	NETVAR(m_flTimerLength, float, "CPlantedC4", "m_flTimerLength");
	NETVAR(m_flDefuseLength, float, "CPlantedC4", "m_flDefuseLength");
	NETVAR(m_flDefuseCountDown, float, "CPlantedC4", "m_flDefuseCountDown");

	bool IsTicking() { return m_bBombTicking(); }
	float SecondsToBlow() { return m_flC4Blow() - I::GlobalVars->curtime; }
	float DefuseProgress()
	{
		if (m_flDefuseLength() <= 0.f) return 1.f;
		return (m_flDefuseCountDown() - I::GlobalVars->curtime) / m_flDefuseLength();
	}
	// No networked bomb-site (A/B) field exists on DT_PlantedC4 in this source --
	// use CCSPlayerResource::m_vecBombsiteCenterA/B (below) + nearest-distance
	// compare instead. Confirmed absent by grepping c_plantedc4.cpp's RecvTable.
};

// ---- C_CSPlayerResource (c_cs_playerresource.cpp, DT_CSPlayerResource) ----
class CCSPlayerResource : public CPlayerResource
{
public:
	NETVAR(m_iPlayerC4, int, "CCSPlayerResource", "m_iPlayerC4");   // entindex carrying the bomb, 0 = none
	NETVAR(m_iPlayerVIP, int, "CCSPlayerResource", "m_iPlayerVIP"); // entindex of the VIP, 0 = none
	NETVAR(m_vecC4, Vec3, "CCSPlayerResource", "m_vecC4");
	NETVAR(m_bombsiteCenterA, Vec3, "CCSPlayerResource", "m_bombsiteCenterA");
	NETVAR(m_bombsiteCenterB, Vec3, "CCSPlayerResource", "m_bombsiteCenterB");
	NETVAR(m_bBombSpotted, bool, "CCSPlayerResource", "m_bBombSpotted");
	NETVAR_ARRAY(m_bHostageAlive, bool, "CCSPlayerResource", "m_bHostageAlive");
	NETVAR_ARRAY(m_bPlayerSpotted, bool, "CCSPlayerResource", "m_bPlayerSpotted");

	// Base multiplayer player resource fields (player_resource.h / c_playerresource.cpp,
	// RecvPropArray3 m_iPing/m_iScore/m_iDeaths) -- CS:S's DT_CSPlayerResource inherits
	// these from the generic DT_PlayerResource, same table-name convention this SDK
	// already uses for m_iPing/m_bAlive/m_iHealth/m_iUserID on the base CPlayerResource
	// class (defs.h). Declared here (not defs.h, not owned) since only CS:S callers need them.
	NETVAR_ARRAY(m_iScore, int, "CPlayerResource", "m_iScore");
	NETVAR_ARRAY(m_iDeaths, int, "CPlayerResource", "m_iDeaths");

	bool IsVIP(int iEntIndex) { return iEntIndex != 0 && m_iPlayerVIP() == iEntIndex; }
	bool HasC4(int iEntIndex) { return iEntIndex != 0 && m_iPlayerC4() == iEntIndex; }
};

// ---- CHostage (c_cs_hostage.h/.cpp, DT_CHostage) ----
// CORRECTION: a real client class DOES exist (c_cs_hostage.cpp:184
// IMPLEMENT_CLIENTCLASS_DT(C_CHostage, DT_CHostage, CHostage)) -- the earlier claim here
// that hostages have no dedicated client class was wrong, fixed after re-check.
class CHostage : public CBaseCombatCharacter
{
public:
	// c_cs_hostage.h:70 CNetworkVar(EHANDLE, m_leader) -- who the hostage is following.
	NETVAR(m_leader, EHANDLE, "CHostage", "m_leader");
	NETVAR(m_isRescued, bool, "CHostage", "m_isRescued");

	int LeaderIndex() { return m_leader().GetEntryIndex(); }
};

// c_cs_player.cpp:188 IMPLEMENT_CLIENTCLASS_DT_NOBASE(C_CSRagdoll, DT_CSRagdoll, CCSRagdoll) --
// the death ragdoll entity, networks m_hPlayer (EHANDLE back to the player who died).
class CCSRagdoll : public CBaseAnimating
{
public:
	NETVAR(m_hPlayer, EHANDLE, "CCSRagdoll", "m_hPlayer");
};
