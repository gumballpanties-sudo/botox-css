#include "css_bridge.h"
#include "css.h"
#include "css_weapons.h"
#include "globals.h"

#include <cstdio>
#include <cstring>
#include <string>

// ================ internal helpers ================
namespace
{
	CBaseEntity* Ent(int idx)
	{
		if (idx <= 0 || !I::ClientEntityList) return nullptr;
		auto pClient = I::ClientEntityList->GetClientEntity(idx);
		return pClient ? pClient->As<CBaseEntity>() : nullptr;
	}
	CBasePlayer* Ply(int idx) { CBaseEntity* e = Ent(idx); return e ? e->As<CBasePlayer>() : nullptr; }
	CCSPlayer* Css(int idx) { CBaseEntity* e = Ent(idx); return e ? e->As<CCSPlayer>() : nullptr; }
	CWeaponCSBase* Wpn(int idx) { CBaseEntity* e = Ent(idx); return e ? e->As<CWeaponCSBase>() : nullptr; }

	void Put(float out[3], const Vec3& v) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }

	CssChatCallback s_chatCallbacks[8] = {};
	int s_chatCallbackCount = 0;
}

// ================ core state ================
bool CssBridge_InGame() { return I::EngineClient && I::EngineClient->IsInGame(); }
float CssBridge_CurTime() { return I::GlobalVars ? I::GlobalVars->curtime : 0.f; }
int CssBridge_LocalPlayerIndex() { return I::EngineClient ? I::EngineClient->GetLocalPlayer() : 0; }

// hooks.cpp's CPrediction_GetLocalViewAngles hook reads G::FakePOVOffset.x/.y as pitch/yaw.
void CssBridge_SetFakePov(float flYaw, float flPitch, bool bActive)
{
	G::FakePOVOffset.x = flPitch;
	G::FakePOVOffset.y = flYaw;
	G::FakePOVActive = bActive;
}

bool CssBridge_IsAlive(int ent) { auto p = Ply(ent); return p && p->IsAlive(); }
int CssBridge_Health(int ent) { auto p = Ply(ent); return p ? p->m_iHealth() : 0; }
int CssBridge_Armor(int ent) { auto p = Css(ent); return p ? p->Armor() : 0; }
int CssBridge_Account(int ent) { auto p = Css(ent); return p ? p->Account() : 0; }
bool CssBridge_IsScoped(int ent)
{
	// weapon_csbase.cpp:1082 GetFOV() < GetDefaultFOV() -- CSS has no CCSPlayer::m_bIsScoped
	// (that netvar is CS2/CS:GO-only), FOV is the signal CSS zoom actually drives.
	auto p = Ply(ent);
	return p && p->m_iFOV() > 0 && p->m_iFOV() < 90;
}
int CssBridge_Team(int ent) { auto e = Ent(ent); return e ? e->m_iTeamNum() : 0; }
bool CssBridge_GetOrigin(int ent, float out[3])
{
	auto e = Ent(ent);
	if (!e) return false;
	Put(out, e->m_vecOrigin());
	return true;
}
int CssBridge_GetHealth(int ent) { return CssBridge_Health(ent); }
int CssBridge_GetTeam(int ent) { return CssBridge_Team(ent); }

bool CssBridge_GetViewAngles(float outPitchYaw[2])
{
	if (!I::EngineClient) return false;
	Vec3 ang = I::EngineClient->GetViewAngles();
	outPitchYaw[0] = ang.x; outPitchYaw[1] = ang.y;
	return true;
}

// ================ weapons ================
int CssBridge_ActiveWeapon(int ent) { auto p = Ply(ent); return p ? p->m_hActiveWeapon().GetEntryIndex() : 0; }
int CssBridge_WeaponAt(int ent, int i)
{
	auto p = Ply(ent);
	if (!p || i < 0 || i >= CSS_MAX_WEAPONS) return 0;
	return p->m_hMyWeapons()[i].GetEntryIndex();
}

int CssBridge_WeaponCategory(int weaponEnt)
{
	auto w = Wpn(weaponEnt);
	if (!w) return -1;
	switch (w->GetWeaponType())
	{
	case WEAPONTYPE_RIFLE: case WEAPONTYPE_SUBMACHINEGUN: case WEAPONTYPE_SHOTGUN:
	case WEAPONTYPE_SNIPER_RIFLE: case WEAPONTYPE_MACHINEGUN: return 0; // primary
	case WEAPONTYPE_PISTOL: return 1;                                   // secondary
	case WEAPONTYPE_KNIFE: return 2;
	case WEAPONTYPE_GRENADE: return 3;
	case WEAPONTYPE_C4: return 4;
	default: return -1;
	}
}

int CssBridge_Clip1(int weaponEnt) { auto w = Wpn(weaponEnt); return w ? w->m_iClip1() : 0; }
int CssBridge_MaxClip1(int weaponEnt) { auto w = Wpn(weaponEnt); return w ? w->GetMaxClip1() : 0; }
int CssBridge_PrimaryAmmoType(int weaponEnt) { auto w = Wpn(weaponEnt); return w ? w->m_iPrimaryAmmoType() : -1; }
int CssBridge_Ammo(int playerEnt, int ammoType)
{
	auto p = Ply(playerEnt);
	return (p && ammoType >= 0) ? p->GetAmmoCount(ammoType) : 0;
}

int CssBridge_GrenadeRank(int weaponEnt)
{
	auto w = Wpn(weaponEnt);
	return w ? CSGrenadeSlot(w->GetWeaponID()) : -1;
}
unsigned int CssBridge_GrenadeTint(int weaponEnt)
{
	auto w = Wpn(weaponEnt);
	if (!w) return 0x385DC6u;
	switch (w->GetWeaponID())
	{
	case WEAPON_HEGRENADE: return 0xA9656Au;
	case WEAPON_FLASHBANG: return 0xF6F6F6u;
	case WEAPON_SMOKEGRENADE: return 0x484D48u;
	default: return 0x385DC6u;
	}
}

const char* CssBridge_WeaponShortName(int weaponEnt)
{
	static char buf[64];
	buf[0] = 0;
	auto w = Wpn(weaponEnt);
	if (!w) return buf;
	const char* raw = w->GetName();
	if (!raw) return buf;
	if (!strncmp(raw, "weapon_", 7)) raw += 7;
	strncpy(buf, raw, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	return buf;
}

void CssBridge_Say(const char* text, bool team)
{
	if (!text || !I::EngineClient) return;
	std::string cmd = std::string(team ? "say_team \"" : "say \"") + text + "\"";
	I::EngineClient->ClientCmd_Unrestricted(cmd.c_str());
}

// ================ world trace ================
namespace
{
	class BridgeTraceFilter : public ITraceFilter
	{
	public:
		CBaseEntity* m_pSkip = nullptr;
		bool ShouldHitEntity(IHandleEntity* pEnt, int) override
		{
			return reinterpret_cast<CBaseEntity*>(pEnt) != m_pSkip;
		}
		TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
	};
}

bool Css_TraceRay(const float start[3], const float dir[3], float maxDist, int skipEnt,
	bool* outHit, float outEnd[3], float outNormal[3], int* outEntHit)
{
	if (outHit) *outHit = false;
	if (!I::EngineTrace) return false;
	Vec3 vStart(start[0], start[1], start[2]), vDir(dir[0], dir[1], dir[2]);
	Ray_t ray; ray.Init(vStart, vStart + vDir * maxDist);
	BridgeTraceFilter filter; filter.m_pSkip = Ent(skipEnt);
	trace_t tr{};
	I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &tr);
	const bool hit = tr.DidHit();
	if (outHit) *outHit = hit;
	if (!hit) return true;
	if (outEnd) Put(outEnd, tr.endpos);
	if (outNormal) Put(outNormal, tr.plane.normal);
	if (outEntHit) *outEntHit = tr.m_pEnt ? reinterpret_cast<CBaseEntity*>(tr.m_pEnt)->entindex() : 0;
	return true;
}

bool CssBridge_TraceWorld(const float start[3], const float dir[3], float maxDist,
	float outHit[3], float outNormal[3])
{
	bool hit = false;
	const bool ok = Css_TraceRay(start, dir, maxDist, CssBridge_LocalPlayerIndex(), &hit, outHit, outNormal, nullptr);
	return ok && hit;
}

// Hull sweep, same Ray_t/BridgeTraceFilter this file's own ray trace above already uses
// (Ray_t::Init(start,end,mins,maxs), defs.h:6900 -- the 4-arg overload movement.cpp's own
// TraceHull calls, verified live there). Round-2 lua port request: bridge it for trace.hull.
bool CssBridge_TraceHullWorld(const float start[3], const float end[3], const float mins[3], const float maxs[3],
	float outEnd[3], float outNormal[3], float* outFraction, bool* outStartSolid, int skipEnt)
{
	if (!I::EngineTrace) return false;
	Vec3 vStart(start[0], start[1], start[2]), vEnd(end[0], end[1], end[2]);
	Vec3 vMins(mins[0], mins[1], mins[2]), vMaxs(maxs[0], maxs[1], maxs[2]);
	Ray_t ray; ray.Init(vStart, vEnd, vMins, vMaxs);
	BridgeTraceFilter filter; filter.m_pSkip = Ent(skipEnt > 0 ? skipEnt : CssBridge_LocalPlayerIndex());
	trace_t tr{};
	I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &tr);
	if (outEnd) Put(outEnd, tr.endpos);
	if (outNormal) Put(outNormal, tr.plane.normal);
	if (outFraction) *outFraction = tr.fraction;
	if (outStartSolid) *outStartSolid = tr.startsolid;
	return true;
}

// ================ view angles / demo (round 2): I::EngineClient->SetViewAngles already runs
// live in aim_rcs.cpp:148 (QAngle&, i.e. this file's Vec3), IsPlayingDemo is the same
// IVEngineClient interface (defs.h, header-ordered next to SetViewAngles/GetViewAngles) ================
void CssBridge_SetViewAngles(float pitch, float yaw, float roll)
{
	if (!I::EngineClient) return;
	Vec3 ang(pitch, yaw, roll);
	I::EngineClient->SetViewAngles(ang);
}
bool CssBridge_IsPlayingDemo() { return I::EngineClient && I::EngineClient->IsPlayingDemo(); }

// ================ bone / hitbox positions (round 2): same SetupBones + pHitboxSet/pHitbox
// call esp.cpp:545-548 and aim_hitscan.cpp:104-109 already run live every frame/shot. No new
// vtable slot -- this is the exact same call, just bridged for one bone/hitbox at a time. ================
bool CssBridge_GetBonePosition(int ent, int bone, float out[3])
{
	auto e = Ent(ent);
	auto pAnim = e ? e->As<CBaseAnimating>() : nullptr;
	if (!pAnim || bone < 0 || bone >= MAXSTUDIOBONES) return false;

	auto pModel = pAnim->GetModel();
	auto pHdr = pModel && I::ModelInfoClient ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
	if (!pHdr || bone >= pHdr->numbones) return false;

	static matrix3x4 aBones[MAXSTUDIOBONES];
	if (!pAnim->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
		return false;

	Vec3 pos{};
	Math::VectorTransform(Vec3(0.f, 0.f, 0.f), aBones[bone], pos);
	Put(out, pos);
	return true;
}

bool CssBridge_GetHitboxPosition(int ent, int hitbox, float out[3])
{
	auto e = Ent(ent);
	auto pAnim = e ? e->As<CBaseAnimating>() : nullptr;
	if (!pAnim) return false;

	auto pModel = pAnim->GetModel();
	auto pHdr = pModel && I::ModelInfoClient ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
	if (!pHdr) return false;

	auto pSet = pHdr->pHitboxSet(pAnim->m_nHitboxSet());
	if (!pSet || hitbox < 0 || hitbox >= pSet->numhitboxes) return false;
	auto pBox = pSet->pHitbox(hitbox);
	if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES) return false;

	static matrix3x4 aBones[MAXSTUDIOBONES];
	if (!pAnim->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		return false;

	Vec3 pos{};
	Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, aBones[pBox->bone], pos);
	Put(out, pos);
	return true;
}

// ================ class id / generic class name (round 2): e->GetClientClass() is the exact
// verified call Css_ClassnameIs below already makes on every entity -- generalised here instead
// of only comparing against one fixed string. ================
int CssBridge_GetClassId(int ent)
{
	auto e = Ent(ent);
	auto pClass = e ? e->GetClientClass() : nullptr;
	return pClass ? pClass->m_ClassID : -1;
}
const char* CssBridge_GetClassName(int ent)
{
	auto e = Ent(ent);
	auto pClass = e ? e->GetClientClass() : nullptr;
	return pClass ? pClass->GetName() : nullptr;
}

// ================ chat callback ================
void CssBridge_RegisterChatCallback(CssChatCallback cb)
{
	if (!cb || s_chatCallbackCount >= int(std::size(s_chatCallbacks))) return;
	s_chatCallbacks[s_chatCallbackCount++] = cb;
}
void CssBridge_DispatchChat(const char* text, int client)
{
	for (int i = 0; i < s_chatCallbackCount; ++i)
		s_chatCallbacks[i](text, client);
}

// Best-effort VK -> Source ButtonCode_t, public/buttoncode.h order: KEY_0..KEY_9 (10 slots)
// then KEY_A..KEY_Z (26 slots), contiguous from KEY_0=0. UNVERIFIED beyond 0-9/A-Z.
const char* CssBridge_KeyBindingForKey(int vk)
{
	if (!I::EngineClient) return nullptr;
	int code = -1;
	if (vk >= '0' && vk <= '9') code = vk - '0';
	else if (vk >= 'A' && vk <= 'Z') code = 10 + (vk - 'A');
	else if (vk >= 'a' && vk <= 'z') code = 10 + (vk - 'a');
	if (code < 0) return nullptr;
	return I::EngineClient->Key_BindingForKey(code);
}

// ================ bomb / observer / player extras ================
bool Css_InBuyZone(int ent) { auto p = Css(ent); return p && p->IsInBuyZone(); }
bool Css_InBombZone(int ent) { auto p = Css(ent); return p && p->IsInBombZone(); }
bool Css_HasDefuseKit(int ent) { auto p = Css(ent); return p && p->HasDefuser(); }
bool Css_IsDefusing(int ent) { auto p = Css(ent); return p && p->IsDefusing(); }
bool Css_HasHelmet(int ent) { auto p = Css(ent); return p && p->HasHelmet(); }
float Css_FlashAlpha(int ent)
{
	auto p = Css(ent);
	if (!p || !I::GlobalVars) return 0.f;
	const float dur = p->m_flFlashDuration();
	if (dur <= 0.f) return 0.f;
	return p->m_flFlashMaxAlpha();
}

namespace
{
	CPlantedC4* FindPlantedC4()
	{
		int idx = CssBridge_FindEntityByNetworkName("CPlantedC4", 0);
		return idx > 0 ? Ent(idx)->As<CPlantedC4>() : nullptr;
	}
	// CCSPlayerResource is a singleton-per-map entity (DT_CSPlayerResource); cached by
	// entindex since it never changes mid-map.
	CCSPlayerResource* FindPlayerResource()
	{
		static int s_idx = 0;
		if (s_idx <= 0 || !Css_ClassnameIs(s_idx, "CCSPlayerResource"))
			s_idx = CssBridge_FindEntityByNetworkName("CCSPlayerResource", 0);
		return s_idx > 0 ? Ent(s_idx)->As<CCSPlayerResource>() : nullptr;
	}
}

bool Css_C4Planted() { return FindPlantedC4() != nullptr; }
float Css_C4BlowTime() { auto c4 = FindPlantedC4(); return c4 ? c4->SecondsToBlow() : -1.f; }
float Css_C4DefuseProgress() { auto c4 = FindPlantedC4(); return c4 ? c4->DefuseProgress() : 1.f; }
int Css_C4CarrierIndex()
{
	auto res = FindPlayerResource();
	if (!res) return 0;
	for (int i = 1; i <= CssBridge_HighestEntityIndex(); ++i)
		if (res->HasC4(i)) return i;
	return 0;
}
bool Css_BombsiteCenter(int which, float out[3])
{
	auto res = FindPlayerResource();
	if (!res) return false;
	Put(out, which == 0 ? res->m_bombsiteCenterA() : res->m_bombsiteCenterB());
	return true;
}
bool Css_IsVIP(int ent)
{
	auto res = FindPlayerResource();
	return res && res->IsVIP(ent);
}

int Css_ObserverMode(int ent) { auto p = Css(ent); return p ? p->m_iObserverMode() : 0; }
int Css_ObserverTargetIndex(int ent) { auto p = Css(ent); return p ? p->m_hObserverTarget().GetEntryIndex() : 0; }

bool Css_PunchAngle(int ent, float out[3]) { auto p = Ply(ent); if (!p) return false; Put(out, p->m_vecPunchAngle()); return true; }
bool Css_ViewOffset(int ent, float out[3]) { auto p = Ply(ent); if (!p) return false; Put(out, p->m_vecViewOffset()); return true; }
bool Css_EyeAngles(int ent, float outPitchYaw[2])
{
	auto p = Css(ent);
	if (!p) return false;
	Vec3 ang = p->GetEyeAngles();
	outPitchYaw[0] = ang.x; outPitchYaw[1] = ang.y;
	return true;
}
int Css_Flags(int ent) { auto p = Ply(ent); return p ? p->m_fFlags() : 0; }
bool Css_OnGround(int ent) { auto p = Ply(ent); return p && p->IsOnGround(); }

bool Css_PlayerName(int ent, char* buf, int bufSize)
{
	if (!buf || bufSize <= 0) return false;
	buf[0] = 0;
	if (ent <= 0 || !I::EngineClient) return false;
	player_info_t info{};
	if (!I::EngineClient->GetPlayerInfo(ent, &info)) return false;
	strncpy(buf, info.name, bufSize - 1);
	buf[bufSize - 1] = 0;
	return true;
}

// ================ grenade projectile / hostage ================
int Css_GrenadeProjectileKind(int ent)
{
	auto e = Ent(ent);
	if (!e) return -1;
	auto pClass = e->GetClientClass();
	if (!pClass || strcmp(pClass->GetName(), "CBaseCSGrenadeProjectile") != 0)
		return -1;
	const model_t* pModel = e->GetModel();
	const char* name = (pModel && I::ModelInfoClient) ? I::ModelInfoClient->GetModelName(pModel) : nullptr;
	if (!name) return -1;
	if (strstr(name, "fraggrenade")) return 0;      // HE: w_eq_fraggrenade_thrown.mdl
	if (strstr(name, "flashbang")) return 1;         // w_eq_flashbang_thrown.mdl
	if (strstr(name, "smokegrenade")) return 2;      // w_eq_smokegrenade_thrown.mdl
	return -1;
}

bool Css_IsHostage(int ent)
{
	auto e = Ent(ent);
	if (!e) return false;
	auto pClass = e->GetClientClass();
	return pClass && !strcmp(pClass->GetName(), "CHostage");
}
int Css_HostageLeaderIndex(int ent)
{
	// CHostage class (css.h) exposes m_leader -- see css.h CHostage below.
	auto e = Ent(ent);
	CHostage* h = e ? e->As<CHostage>() : nullptr;
	return h ? h->LeaderIndex() : 0;
}

// ================ SKIN unit asks ================
int Css_EntityTeam(int ent) { return CssBridge_Team(ent); }
bool Css_EntityAlive(int ent) { return CssBridge_IsAlive(ent); }
int Css_ActiveWeaponIndex(int ent) { return CssBridge_ActiveWeapon(ent); }
int Css_ViewmodelIndex(int ent) { auto p = Ply(ent); return p ? p->m_hViewModel().GetEntryIndex() : 0; }

int Css_GetModelIndex(int ent)
{
	auto e = Ent(ent);
	if (!e) return 0;
	// stopgap netvar read (AGENT_BRIEF: ok when no typed accessor exists yet) -- generic
	// Source engine field, present regardless of game.
	static int off = U::NetVars.GetNetVar("CBaseEntity", "m_nModelIndex");
	if (off <= 0) return 0;
	return *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(e) + off);
}
bool Css_SetModelIndex(int ent, int modelIndex)
{
	auto e = Ent(ent);
	if (!e) return false;
	static int off = U::NetVars.GetNetVar("CBaseEntity", "m_nModelIndex");
	if (off <= 0) return false;
	*reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(e) + off) = modelIndex;
	return true;
}
void Css_InvalidateBoneCache(int ent)
{
	auto e = Ent(ent);
	if (e) e->InvalidateBoneCache();
}
bool Css_ClassnameIs(int ent, const char* dtName)
{
	auto e = Ent(ent);
	if (!e || !dtName) return false;
	auto pClass = e->GetClientClass();
	return pClass && pClass->GetName() && !strcmp(pClass->GetName(), dtName);
}
int Css_OwnedWeapons(int ent, int* outIndices, int maxCount)
{
	auto p = Ply(ent);
	if (!p || !outIndices || maxCount <= 0) return 0;
	int n = 0;
	for (int i = 0; i < CSS_MAX_WEAPONS && n < maxCount; ++i)
	{
		int idx = p->m_hMyWeapons()[i].GetEntryIndex();
		if (idx > 0) outIndices[n++] = idx;
	}
	return n;
}

// ================ cod4/urt/chud gamehud contract ================
int CssBridge_PlayerForUserId(int userId)
{
	return I::EngineClient ? I::EngineClient->GetPlayerForUserID(userId) : 0;
}
bool CssBridge_GetPlayerName(int ent, char* outUtf8, int outSize) { return Css_PlayerName(ent, outUtf8, outSize); }
bool CssBridge_ConVarBool(const char* name, bool defaultValue)
{
	if (!I::CVar) return defaultValue;
	auto pVar = I::CVar->FindVar(name);
	return pVar ? pVar->GetBool() : defaultValue;
}
int CssBridge_ConVarInt(const char* name, int defaultValue)
{
	if (!I::CVar) return defaultValue;
	auto pVar = I::CVar->FindVar(name);
	return pVar ? pVar->GetInt() : defaultValue;
}
bool CssBridge_IsConsoleVisible() { return I::EngineClient && I::EngineClient->Con_IsVisible(); }
int CssBridge_HighestEntityIndex() { return I::ClientEntityList ? I::ClientEntityList->GetHighestEntityIndex() : 0; }
bool CssBridge_EntityExists(int ent) { return Ent(ent) != nullptr; }
bool CssBridge_IsDormant(int ent) { auto e = Ent(ent); return !e || e->IsDormant(); }
int CssBridge_GetArmor(int ent) { return CssBridge_Armor(ent); }
float CssBridge_GetFlashDuration(int ent) { auto p = Css(ent); return p ? p->m_flFlashDuration() : 0.f; }
bool CssBridge_IsOnGround(int ent) { return Css_OnGround(ent); }
bool CssBridge_GetEyeYaw(int ent, float& outYaw)
{
	auto p = Css(ent);
	if (!p) return false;
	outYaw = p->GetEyeAngles().y;
	return true;
}
int CssBridge_GetObserverTarget(int ent) { return Css_ObserverTargetIndex(ent); }
bool CssBridge_IsObserverFreeLook(int ent)
{
	// public observer_mode.h: OBS_MODE_ROAMING = 6 (NONE/DEATHCAM/FREEZECAM/FIXED/IN_EYE/
	// CHASE/ROAMING). Free-look is the only mode with no fixed/followed target.
	return Css_ObserverMode(ent) == 6;
}
bool CssBridge_GetLocalViewAngles(float outPitchYawRoll[3])
{
	if (!I::EngineClient) return false;
	Vec3 ang = I::EngineClient->GetViewAngles();
	outPitchYawRoll[0] = ang.x; outPitchYawRoll[1] = ang.y; outPitchYawRoll[2] = ang.z;
	return true;
}
int CssBridge_GetWeaponID(int weaponEnt) { auto w = Wpn(weaponEnt); return w ? (int)w->GetWeaponID() : 0; }
// botox custom_crosshair.cpp/indicators.cpp sniper-weapon gate (awp/ssg08/scar20/g3sg1) --
// CS:S has no ssg08/scar20 (CS:GO-only), CSWeaponIsSniperClass's AWP/SG550/G3SG1/Scout is the
// CSS weapon-set equivalent (css_weapons.h comment, verified against cs_weapon_parse.h).
bool CssBridge_IsSniperWeapon(int weaponEnt) { return CSWeaponIsSniperClass(CssBridge_GetWeaponID(weaponEnt)); }
int CssBridge_GetClip1(int weaponEnt) { return CssBridge_Clip1(weaponEnt); }
bool CssBridge_GetBombCarrier(int& outEnt) { outEnt = Css_C4CarrierIndex(); return outEnt != 0; }
bool CssBridge_GetBombsites(float outA[2], float outB[2])
{
	float a[3], b[3];
	if (!Css_BombsiteCenter(0, a) || !Css_BombsiteCenter(1, b)) return false;
	outA[0] = a[0]; outA[1] = a[1]; outB[0] = b[0]; outB[1] = b[1];
	return true;
}
void CssBridge_ClientCmdUnrestricted(const char* command)
{
	if (command && I::EngineClient) I::EngineClient->ClientCmd_Unrestricted(command);
}
int CssBridge_FindEntityByNetworkName(const char* dtName, int startAfter)
{
	if (!dtName || !I::ClientEntityList) return 0;
	const int highest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = startAfter + 1; i <= highest; ++i)
		if (Css_ClassnameIs(i, dtName)) return i;
	return 0;
}
bool CssBridge_GetVelocity(int ent, float outXYZ[3])
{
	auto p = Ply(ent);
	if (!p) return false;
	Put(outXYZ, p->m_vecVelocity());
	return true;
}
int CssBridge_GetOwnerEntity(int ent) { auto e = Ent(ent); return e ? e->m_hOwnerEntity().GetEntryIndex() : 0; }
bool CssBridge_GetWeaponInventory(int playerEnt, int* outWeaponIds, int maxCount, int& outCount)
{
	outCount = 0;
	int idx[CSS_MAX_WEAPONS];
	int n = Css_OwnedWeapons(playerEnt, idx, CSS_MAX_WEAPONS);
	for (int i = 0; i < n && outCount < maxCount; ++i)
		outWeaponIds[outCount++] = CssBridge_GetWeaponID(idx[i]);
	return outCount > 0;
}
int CssBridge_GetActiveWeaponAmmo(int playerEnt, int& outClip, int& outClipMax, int& outReserve)
{
	int wpn = CssBridge_ActiveWeapon(playerEnt);
	outClip = CssBridge_Clip1(wpn);
	outClipMax = CssBridge_MaxClip1(wpn);
	outReserve = CssBridge_Ammo(playerEnt, CssBridge_PrimaryAmmoType(wpn));
	return wpn;
}
bool CssBridge_IsDefusing(int ent) { return Css_IsDefusing(ent); }
bool CssBridge_IsWeaponFullAuto(int weaponEnt) { auto w = Wpn(weaponEnt); return w && w->IsFullAuto(); }

// ================ BRIDGE unit additions ================
const char* CssBridge_LevelNameRaw() { return I::EngineClient ? I::EngineClient->GetLevelName() : ""; }
float CssBridge_Curtime() { return CssBridge_CurTime(); }
int CssBridge_GetActiveWeaponEnt(int playerEnt) { return CssBridge_ActiveWeapon(playerEnt); }
float CssBridge_Stamina(int ent) { return CssBridge_GetStaminaFraction(ent); }

// ---- round / score ----
bool CssBridge_GetTeamScores(int& outTScore, int& outCtScore)
{
	outTScore = outCtScore = 0;
	bool found = false;
	const int highest = CssBridge_HighestEntityIndex();
	for (int i = 1; i <= highest; ++i)
	{
		if (!Css_ClassnameIs(i, "CCSTeam")) continue; // verified server class name (AIM3, 11-03), not "DT_Team"
		auto t = Ent(i)->As<CTeam>();
		if (!t) continue;
		const int side = t->m_iTeamNum2();
		if (side == CS_TEAM_TERRORIST) { outTScore = t->m_iScore(); found = true; }
		else if (side == CS_TEAM_CT) { outCtScore = t->m_iScore(); found = true; }
	}
	return found;
}
bool CssBridge_GetPlayerScore(int ent, int& outScore)
{
	outScore = 0;
	auto res = FindPlayerResource();
	if (!res || ent <= 0) return false;
	outScore = res->m_iScore(ent);
	return true;
}
bool CssBridge_GetAllPlayerScores(int* outArray, int maxCount, int& outCount)
{
	outCount = 0;
	if (!outArray || maxCount <= 0) return false;
	auto res = FindPlayerResource();
	if (!res) return false;
	const int highest = CssBridge_HighestEntityIndex();
	for (int i = 1; i <= highest && outCount < maxCount; ++i)
		if (CssBridge_EntityExists(i))
			outArray[outCount++] = res->m_iScore(i);
	return outCount > 0;
}
bool CssBridge_GetRoundTime(float& outRoundSeconds, float& outStartTime)
{
	outRoundSeconds = outStartTime = 0.f;
	auto pRules = CSGameRules();
	if (!pRules) return false;
	outRoundSeconds = (float)pRules->m_iRoundTime();
	outStartTime = pRules->m_fRoundStartTime();
	return outRoundSeconds > 0.f;
}
bool CssBridge_GetWarmup(bool& outWarmup, float& outWarmupEnd)
{
	// CS:S has no warmup-period fields on DT_CSGameRules (grepped cs_gamerules.cpp's
	// BEGIN_NETWORK_TABLE(CCSGameRules, DT_CSGameRules): no m_bWarmupPeriod/
	// m_fWarmupPeriodEnd). Genuine gameplay absence, not a port gap -- always false.
	outWarmup = false; outWarmupEnd = 0.f;
	return false;
}

// ---- bomb ----
bool CssBridge_GetBombState(bool& outPlanted, bool& outTicking, float outPos[3], float& outDefuseProgress)
{
	outPlanted = outTicking = false; outDefuseProgress = 1.f;
	outPos[0] = outPos[1] = outPos[2] = 0.f;
	auto c4 = FindPlantedC4();
	if (!c4) return false;
	outPlanted = true;
	outTicking = c4->IsTicking();
	Put(outPos, c4->m_vecOrigin());
	outDefuseProgress = c4->DefuseProgress();
	return true;
}
float CssBridge_GetDefuseEndTime(int plantedC4Ent) { auto c4 = Ent(plantedC4Ent); auto p = c4 ? c4->As<CPlantedC4>() : nullptr; return p ? p->m_flDefuseCountDown() : 0.f; }
float CssBridge_GetDefuseLength(int plantedC4Ent) { auto c4 = Ent(plantedC4Ent); auto p = c4 ? c4->As<CPlantedC4>() : nullptr; return p ? p->m_flDefuseLength() : 0.f; }
bool CssBridge_IsBombTicking(int plantedC4Ent) { auto c4 = Ent(plantedC4Ent); auto p = c4 ? c4->As<CPlantedC4>() : nullptr; return p && p->IsTicking(); }

// ---- player/weapon extras ----
float CssBridge_GetDuckAmount(int ent)
{
	// cs_gamemovement.cpp:943/978/1008 set m_flDucktime=1000 at duck start, counting down to 0
	// as the duck completes (gamemovement.cpp:1095-1100 ticks it toward 0). m_bDucked (CBasePlayer,
	// already NETVAR'd) disambiguates "fully ducked, ducktime==0" from "fully standing, ducktime==0".
	auto p = Ply(ent);
	if (!p) return 0.f;
	if (p->m_bDucked()) return 1.f;
	if (!p->m_bDucking()) return 0.f;
	float frac = 1.f - (p->m_flDucktime() / 1000.f);
	return frac < 0.f ? 0.f : (frac > 1.f ? 1.f : frac);
}
float CssBridge_GetStaminaFraction(int ent)
{
	// cs_gamemovement.cpp:21-24 STAMINA_MAX=100, STAMINA_RECOVER_RATE=19; flRatio (fraction of
	// stamina REMAINING) = (STAMINA_MAX - (m_flStamina/1000)*STAMINA_RECOVER_RATE) / STAMINA_MAX.
	// "Spent" fraction (0 fresh .. 1 spent, per shim contract) = 1 - flRatio = m_flStamina*19/100000.
	auto p = Css(ent);
	if (!p) return 0.f;
	float spent = p->m_flStamina() * 19.f / 100000.f;
	return spent < 0.f ? 0.f : (spent > 1.f ? 1.f : spent);
}
const char* CssBridge_GetLastPlace(int ent)
{
	// c_baseplayer.h:600 char m_szLastPlaceName[MAX_PLACE_NAME_LENGTH] -- fixed char buffer,
	// not pointer/array-of-entries, no existing NETVAR_EMBED covers it. Stopgap netvar read
	// (AGENT_BRIEF: ok when no typed accessor exists), table "CBasePlayer" per this SDK's
	// established bare-classname convention for that class.
	static char buf[32];
	buf[0] = 0;
	auto e = Ent(ent);
	if (!e) return buf;
	static int off = U::NetVars.GetNetVar("CBasePlayer", "m_szLastPlaceName");
	if (off <= 0) return buf;
	const char* p = reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(e) + off);
	strncpy(buf, p, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	return buf;
}
int CssBridge_GetButtons(int ent)
{
	// m_nButtons (defs.h NETVAR_OFF hack) only reflects the LOCAL player's own command state --
	// shim contract says "local only", so non-local callers get 0 rather than a wrong value.
	if (ent != CssBridge_LocalPlayerIndex()) return 0;
	auto p = Ply(ent);
	return p ? p->m_nButtons() : 0;
}
float CssBridge_GetAccuracyPenalty(int weaponEnt)
{
	// m_fAccuracyPenalty, read straight off the raw member (this+0xcb0) -- the same field
	// UpdateAccuracyPenalty (css.h, vtable slot 384) writes and GetInaccuracy (382) adds into.
	// Verified by disassembly (docs/port/vtable_check.md): slot 384's body is a plain write to
	// [rbx+0xcb0], no accessor virtual exists for a read-only fetch, so this reads the member
	// directly instead of calling a vfunc for a single float.
	auto w = Wpn(weaponEnt);
	if (!w) return 0.f;
	return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(w) + 0xcb0);
}
float CssBridge_GetInaccuracy(int weaponEnt)
{
	// botox's real weapon_csbase.cpp:3399 signal (shift left/right, vm_csgo_hook.inl). css.h
	// slot 254-256: UpdateAccuracyPenalty (384) MUST run before GetInaccuracy (382) reads
	// this+0xcb0 -- same per-shot order FX_FireBullets uses, same comment css.h already carries.
	auto w = Wpn(weaponEnt);
	if (!w) return 0.f;
	// put the penalty back: this runs every FRAME (viewmodel hook), and an un-restored call
	// decayed the predicted penalty once per frame on top of prediction's own per-tick decay --
	// nospread + the aim cone then read a cone smaller than the server's
	float& flPenalty = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(w) + 0xcb0);
	const float flOld = flPenalty;
	w->UpdateAccuracyPenalty();
	const float flInaccuracy = w->GetInaccuracy();
	flPenalty = flOld;
	return flInaccuracy;
}
bool CssBridge_IsWeaponBurstMode(int weaponEnt) { auto w = Wpn(weaponEnt); return w && w->IsBurstMode(); }

// ---- net ----
bool CssBridge_GetNetPing(int& outPingMs, float& outSinceReceivedSec, bool& outLost, bool& outChoked)
{
	outPingMs = 0; outSinceReceivedSec = 0.f; outLost = outChoked = false;
	if (!I::EngineClient) return false;
	auto pChan = I::EngineClient->GetNetChannelInfo();
	if (!pChan) return false;
	outPingMs = int(pChan->GetLatency(FLOW_OUTGOING) * 1000.f);
	outSinceReceivedSec = pChan->GetTimeSinceLastReceived();
	outLost = pChan->GetAvgLoss(FLOW_INCOMING) > 0.f;
	outChoked = pChan->GetAvgChoke(FLOW_INCOMING) > 0.f;
	return true;
}

// ---- world / trace ----
namespace
{
	class WorldOnlyFilter : public ITraceFilter
	{
	public:
		bool ShouldHitEntity(IHandleEntity*, int) override { return true; }
		TraceType_t GetTraceType() const override { return TRACE_WORLD_ONLY; }
	};
}
bool CssBridge_GetWorldBounds(float outMins[3], float outMaxs[3])
{
	if (!I::ClientEntityList || !I::ModelInfoClient) return false;
	auto pClient = I::ClientEntityList->GetClientEntity(0); // worldspawn, entindex 0
	auto pWorld = pClient ? pClient->As<CBaseEntity>() : nullptr;
	if (!pWorld) return false;
	auto pModel = pWorld->GetModel();
	if (!pModel) return false;
	Vector mn, mx;
	I::ModelInfoClient->GetModelBounds(pModel, mn, mx);
	Put(outMins, mn);
	Put(outMaxs, mx);
	return true;
}
bool CssBridge_TraceWorldDown(const float start[3], const float end[3], float outEnd[3], float& outFraction, bool& outStartSolid)
{
	outFraction = 1.f; outStartSolid = false;
	if (outEnd) { outEnd[0] = end[0]; outEnd[1] = end[1]; outEnd[2] = end[2]; }
	if (!I::EngineTrace) return false;
	Ray_t ray; ray.Init(Vec3(start[0], start[1], start[2]), Vec3(end[0], end[1], end[2]));
	WorldOnlyFilter filter;
	trace_t tr{};
	I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &tr);
	if (outEnd) Put(outEnd, tr.endpos);
	outFraction = tr.fraction;
	outStartSolid = tr.startsolid;
	return true;
}
bool CssBridge_PointInSolid(const float point[3])
{
	if (!I::EngineTrace) return false;
	Vector p(point[0], point[1], point[2]);
	return (I::EngineTrace->GetPointContents(p) & CONTENTS_SOLID) != 0;
}
bool CssBridge_TraceAimEntity(const float eye[3], const float viewPitchYaw[2], int& outEnt)
{
	outEnt = 0;
	if (!I::EngineTrace) return false;
	Vec3 fwd;
	Math::AngleVectors(Vec3(viewPitchYaw[0], viewPitchYaw[1], 0.f), &fwd);
	Vec3 start(eye[0], eye[1], eye[2]);
	Vec3 end = start + fwd * 8192.f;
	Ray_t ray; ray.Init(start, end);
	BridgeTraceFilter filter; filter.m_pSkip = Ent(CssBridge_LocalPlayerIndex());
	trace_t tr{};
	I::EngineTrace->TraceRay(ray, MASK_SHOT | CONTENTS_GRATE, &filter, &tr); // shim contract: MASK_SHOT|CONTENTS_GRATE
	if (tr.m_pEnt) outEnt = reinterpret_cast<CBaseEntity*>(tr.m_pEnt)->entindex();
	return tr.DidHit();
}

uint64_t CssBridge_GetSteamID64(int ent)
{
	if (ent <= 0 || !I::EngineClient) return 0;
	player_info_t info{};
	if (!I::EngineClient->GetPlayerInfo(ent, &info)) return 0;
	if (info.fakeplayer) return 0;
	constexpr uint64_t kSteamID64Base = 76561197960265728ull;
	return info.friendsID ? kSteamID64Base + info.friendsID : 0;
}

// ================ sun angle (UNIT D2, see css_bridge.h banner) ================
namespace
{
	int   s_sunEntIdx = 0;
	float s_sunOrigDir[3] = {};
	float s_sunOrigDist = 0.f;
	bool  s_sunCaptured = false;

	// No SEH guard here (matches Css_SetModelIndex in this same file: a plain raw-offset write,
	// no __try) -- idx was just confirmed live by the caller's Css_ClassnameIs/
	// FindEntityByNetworkName this same call, same-frame TOCTOU risk as every other netvar
	// write in this bridge.
	bool SunAngle_WriteAndFire(int idx, const float dir[3], float maxDist, float* outOldDir, float* outOldDist)
	{
		auto e = Ent(idx);
		if (!e) return false;

		static int offDir  = U::NetVars.GetNetVar("CShadowControl", "m_shadowDirection");
		static int offDist = U::NetVars.GetNetVar("CShadowControl", "m_flShadowMaxDist");
		if (offDir <= 0 || offDist <= 0) return false;

		float* pDir  = reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(e) + offDir);
		float* pDist = reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(e) + offDist);
		if (outOldDir)  { outOldDir[0] = pDir[0]; outOldDir[1] = pDir[1]; outOldDir[2] = pDir[2]; }
		if (outOldDist) *outOldDist = *pDist;
		pDir[0] = dir[0]; pDir[1] = dir[1]; pDir[2] = dir[2];
		*pDist  = maxDist;

		// Re-fire OnDataChanged (IClientNetworkable slot 5, defs.h:9519-9535) via
		// IClientEntityList::GetClientNetworkable (slot 0, defs.h:6604-6618 -- same interface
		// GetClientEntity/GetHighestEntityIndex already use elsewhere in this DLL) so
		// C_ShadowControl::OnDataChanged pushes our values into g_pClientShadowMgr, the same
		// path a real server netvar update would take.
		if (I::ClientEntityList) {
			IClientNetworkable* net = I::ClientEntityList->GetClientNetworkable(idx);
			if (net) net->OnDataChanged(DATA_UPDATE_DATATABLE_CHANGED);
		}
		return true;
	}
}

bool CssBridge_SunAngleSet(const float dir[3], float maxDist)
{
	if (!dir || !I::ClientEntityList) return false;

	if (s_sunEntIdx <= 0 || !Css_ClassnameIs(s_sunEntIdx, "CShadowControl"))
		s_sunEntIdx = CssBridge_FindEntityByNetworkName("CShadowControl", 0);
	if (s_sunEntIdx <= 0) return false;   // no env_shadow_control on this map -- nothing to drive

	float capturedDir[3]; float capturedDist = 0.f;
	if (!SunAngle_WriteAndFire(s_sunEntIdx, dir, maxDist,
	                           s_sunCaptured ? nullptr : capturedDir,
	                           s_sunCaptured ? nullptr : &capturedDist))
		return false;

	if (!s_sunCaptured) {
		s_sunOrigDir[0] = capturedDir[0]; s_sunOrigDir[1] = capturedDir[1]; s_sunOrigDir[2] = capturedDir[2];
		s_sunOrigDist = capturedDist;
		s_sunCaptured = true;
	}
	return true;
}

void CssBridge_SunAngleRestore()
{
	if (!s_sunCaptured) { s_sunEntIdx = 0; return; }
	if (s_sunEntIdx > 0)
		SunAngle_WriteAndFire(s_sunEntIdx, s_sunOrigDir, s_sunOrigDist, nullptr, nullptr);
	s_sunCaptured = false;
	s_sunEntIdx = 0;
}
