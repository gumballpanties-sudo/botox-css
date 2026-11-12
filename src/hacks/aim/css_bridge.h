#pragma once

// ================ css_bridge: CSS game API for non-aim code (unit SDK) ================
// Plain free functions over an entity index, NO SDK includes (no defs.h/css.h types leak
// out). Implemented in css_bridge.cpp (an aim/ TU, uses css.h there). GAME THREAD ONLY --
// the d3d/render thread must never call these directly (botox RULE: d3d thread never reads
// game-thread state), snapshot the values you need on the game thread instead (see
// src/hacks/gamehud/mc/mc_hud_game.cpp for the pattern: on_paint_traverse() fills a
// snapshot_t under a mutex, the render thread only reads that struct).
//
// Every non-aim TU that used to include "../aim/css.h" or "../aim/defs.h" for CBasePlayer/
// CCSPlayer/CWeaponCSBase/I::EngineClient etc. should include this instead -- those two
// headers define their OWN RecvTable/RecvProp/ConVar/ICvar/IClientEntityList/ClientClass/
// INetChannelInfo, which collide with src/globals/* 's versions of the same names in the
// same translation unit (verified compile error). This header carries none of that.

#include <cstdint>

// botox: CSS's CBaseCombatCharacter::m_hMyWeapons array length (defs.h MAX_WEAPONS, verbatim).
inline constexpr int CSS_MAX_WEAPONS = 48;

// Plain mirror of the handful of IGameEvent fields the gamehud TUs (cod4/urt/chud) read. Filled by
// the caller translating a real IGameEvent* before calling on_event() -- moved here from the now-
// deleted css_bridge_shim.h (BRIDGE unit 11-03) so every gamehud TU can get it from one real header.
struct GameHudEventArgs {
	const char* m_name = nullptr;                       // event->GetName()
	int m_userid = 0, m_attacker = 0, m_assister = 0;    // event->GetInt(...)
	int m_winner = 0, m_reason = 0, m_hitgroup = 0, m_health = 0;
	const char* m_weapon = nullptr;                      // event->GetString("weapon")
	bool m_headshot = false;
};

// ---- entity / player state -----------------------------------------------------------
bool CssBridge_InGame();
float CssBridge_CurTime();
int  CssBridge_LocalPlayerIndex();

bool CssBridge_IsAlive(int ent);
int  CssBridge_Health(int ent);
int  CssBridge_Armor(int ent);          // CCSPlayer only, 0 for non-players
int  CssBridge_Account(int ent);        // CCSPlayer only, 0 for non-players
bool CssBridge_IsScoped(int ent);       // FOV < default FOV (weapon_csbase.cpp:1082), CCSPlayer only
int  CssBridge_Team(int ent);           // CBaseEntity::m_iTeamNum, works on any entity
bool CssBridge_GetOrigin(int ent, float out[3]);

// aliases some callers expect under these exact names (same values as above)
int  CssBridge_GetHealth(int ent);
int  CssBridge_GetTeam(int ent);

bool CssBridge_GetViewAngles(float outPitchYaw[2]);   // local player's current view angles

// ---- weapons --------------------------------------------------------------------------
int CssBridge_ActiveWeapon(int ent);             // active weapon entindex, 0 = none
int CssBridge_WeaponAt(int ent, int i);           // m_hMyWeapons[i] entindex, 0 <= i < CSS_MAX_WEAPONS

// 0 primary, 1 secondary, 2 knife, 3 grenade, 4 c4, -1 other/none (css_weapons.h ECSWeaponClass
// re-bucketed to GHUD-MC's 5 hotbar slots)
int CssBridge_WeaponCategory(int weaponEnt);

int CssBridge_Clip1(int weaponEnt);
int CssBridge_MaxClip1(int weaponEnt);
int CssBridge_PrimaryAmmoType(int weaponEnt);
int CssBridge_Ammo(int playerEnt, int ammoType);  // reserve ammo pool, CBasePlayer::m_iAmmo[type]

// botox grenade cycle rank (css_weapons.h CSGrenadeSlot: flash=0/HE=1/smoke=2, -1 = not a
// grenade) and MobEffects-style tint used by mc_hud_game.cpp's potion_tint()
int          CssBridge_GrenadeRank(int weaponEnt);
unsigned int CssBridge_GrenadeTint(int weaponEnt);

const char* CssBridge_WeaponShortName(int weaponEnt); // "weapon_" prefix stripped, static buffer

void CssBridge_Say(const char* text, bool team);

// ---- world trace ------------------------------------------------------------------------
// world+props only (MASK_SOLID), skips the local player. true = hit.
bool CssBridge_TraceWorld(const float start[3], const float dir[3], float maxDist,
                           float outHit[3], float outNormal[3]);

// ---- chat line callback (SayText/SayText2/TextMsg) --------------------------------------
// The DispatchUserMessage hook itself lives in src/hacks/visuals/visuals.cpp (not owned by
// this unit) -- it already hooks CHLClient::DispatchUserMessage (slot 36, confirmed SAME in
// CSS by direct disasm, see docs/port/vtable_check.md). That hook must call
// CssBridge_DispatchChat() when it sees a SayText/SayText2/TextMsg message (NEEDS FROM
// OTHERS, see report). Subscribers (mc hud, chud, chat translator) register here.
using CssChatCallback = void (*)(const char* text, int client);
void CssBridge_RegisterChatCallback(CssChatCallback cb);
void CssBridge_DispatchChat(const char* text, int client); // called by the DispatchUserMessage hook

// Best-effort VK -> Source ButtonCode_t (public/buttoncode.h: KEY_0..KEY_9 then KEY_A..KEY_Z,
// contiguous) -> IVEngineClient::Key_BindingForKey. Only covers 0-9/A-Z (enough for chat
// binds); returns nullptr for anything else or if unbound. UNVERIFIED beyond that range.
const char* CssBridge_KeyBindingForKey(int vk);

// ================ full cod4/urt/chud gamehud contract ================
// Everything below is a real, verified accessor; nothing here is a guess.
int  CssBridge_PlayerForUserId(int userId);
bool CssBridge_GetPlayerName(int ent, char* outUtf8, int outSize);
bool CssBridge_ConVarBool(const char* name, bool defaultValue);
int  CssBridge_ConVarInt(const char* name, int defaultValue);
bool CssBridge_IsConsoleVisible();
int  CssBridge_HighestEntityIndex();
bool CssBridge_EntityExists(int ent);
bool CssBridge_IsDormant(int ent);
int  CssBridge_GetArmor(int ent);
float CssBridge_GetFlashDuration(int ent);
bool CssBridge_IsOnGround(int ent);
bool CssBridge_GetEyeYaw(int ent, float& outYaw);
int  CssBridge_GetObserverTarget(int ent);
bool CssBridge_IsObserverFreeLook(int ent);          // OBS_MODE_ROAMING, public/const.h observer_mode enum
bool CssBridge_GetLocalViewAngles(float outPitchYawRoll[3]);
int  CssBridge_GetWeaponID(int weaponEnt);
int  CssBridge_GetClip1(int weaponEnt);
bool CssBridge_GetBombCarrier(int& outEnt);
bool CssBridge_GetBombsites(float outA[2], float outB[2]);
void CssBridge_ClientCmdUnrestricted(const char* command);
int  CssBridge_FindEntityByNetworkName(const char* dtName, int startAfter); // iterate: last result or 0 to start
bool CssBridge_GetVelocity(int ent, float outXYZ[3]);
int  CssBridge_GetOwnerEntity(int ent);
bool CssBridge_GetWeaponInventory(int playerEnt, int* outWeaponIds, int maxCount, int& outCount);
int  CssBridge_GetActiveWeaponAmmo(int playerEnt, int& outClip, int& outClipMax, int& outReserve);
bool CssBridge_IsDefusing(int ent);
bool CssBridge_IsWeaponFullAuto(int weaponEnt);      // C_WeaponCSBase own virtual, slot 369

// ================ BRIDGE unit additions (round/score, net, duck/stamina, bomb, trace) ================
// Exact names/signatures cod4_hud.cpp/urt_hud.cpp/chud_hud.cpp are written against. See this unit's
// report for per-function source citation.

const char* CssBridge_LevelNameRaw();                 // engine->GetLevelName(), e.g. "maps/de_dust2.bsp"
float       CssBridge_Curtime();                      // alias of CssBridge_CurTime()
int         CssBridge_GetActiveWeaponEnt(int playerEnt); // alias of CssBridge_ActiveWeapon()

// round / score -- CTeam (c_team.cpp, DT_Team) for team scores, CCSPlayerResource (base
// CPlayerResource's m_iScore/m_iDeaths) for per-player, CCSGameRulesProxy for round clock.
bool CssBridge_GetTeamScores(int& outTScore, int& outCtScore);
bool CssBridge_GetPlayerScore(int ent, int& outScore);
bool CssBridge_GetAllPlayerScores(int* outArray, int maxCount, int& outCount);
bool CssBridge_GetRoundTime(float& outRoundSeconds, float& outStartTime);
bool CssBridge_GetWarmup(bool& outWarmup, float& outWarmupEnd); // CS:S has no warmup fields -- always false

// bomb
bool  CssBridge_GetBombState(bool& outPlanted, bool& outTicking, float outPos[3], float& outDefuseProgress);
float CssBridge_GetDefuseEndTime(int plantedC4Ent);   // CPlantedC4::m_flDefuseCountDown
float CssBridge_GetDefuseLength(int plantedC4Ent);    // CPlantedC4::m_flDefuseLength
bool  CssBridge_IsBombTicking(int plantedC4Ent);

// player/weapon extras
float CssBridge_GetDuckAmount(int ent);               // 0..1, CBasePlayer::m_flDucktime-derived
float CssBridge_GetStaminaFraction(int ent);           // 0 fresh .. 1 spent, CCSPlayer::m_flStamina-derived
float CssBridge_Stamina(int ent);                      // alias of CssBridge_GetStaminaFraction()
const char* CssBridge_GetLastPlace(int ent);           // CBasePlayer::m_szLastPlaceName, "" if none
int   CssBridge_GetButtons(int ent);                   // local player only, else 0
float CssBridge_GetAccuracyPenalty(int weaponEnt);     // m_fAccuracyPenalty, raw read @ +0xcb0 (verified)
float CssBridge_GetInaccuracy(int weaponEnt);          // GetInaccuracy, vtable slot 382 (disasm-verified)
bool  CssBridge_ReloadingOrElites(int weaponEnt);      // CS:GO vm accuracy-shift skip (weapon_csbase.cpp:3398)
bool  CssBridge_IsWeaponBurstMode(int weaponEnt);      // famas/glock only (weapon_famas/glock.cpp)
bool  CssBridge_IsSniperWeapon(int weaponEnt);         // AWP/SG550/G3SG1/Scout (css_weapons.h CSWeaponIsSniperClass) --
                                                        // CSS-adapted botox awp/ssg08/scar20/g3sg1 gate for crosshair.cpp

// net
bool CssBridge_GetNetPing(int& outPingMs, float& outSinceReceivedSec, bool& outLost, bool& outChoked);

// world
bool CssBridge_GetWorldBounds(float outMins[3], float outMaxs[3]);
bool CssBridge_TraceWorldDown(const float start[3], const float end[3], float outEnd[3], float& outFraction, bool& outStartSolid);
bool CssBridge_PointInSolid(const float point[3]);
bool CssBridge_TraceAimEntity(const float eye[3], const float viewPitchYaw[2], int& outEnt);

// steamID64 = 76561197960265728 (base) + friendsID (player_info_t, engine->GetPlayerInfo). Same
// conversion avatar_cache.cpp/scoreboard.cpp already use. 0 for bots or an invalid/unresolvable index.
uint64_t CssBridge_GetSteamID64(int ent);

// botox misc/fake_pov.cpp angle/arrow math lives in src/hacks/movement/movement_ex.inl
// (FakePov_Tick/FakePov_GetState, MVCORE) -- that file can't include aim/globals.h (its own
// Vec3/movement types clash with aim's), so it pushes the smoothed state through here instead.
// The aim-side hook (hooks.cpp CPrediction_GetLocalViewAngles) reads it straight off
// G::FakePOVOffset/G::FakePOVActive, which this just writes. Call once per frame/cmd with
// FakePov_GetState()'s yaw/pitch/active (arrowAng/arrowLen are overlay-only, not needed here).
void CssBridge_SetFakePov(float flYaw, float flPitch, bool bActive);

// ================ broader Css_* surface (bomb, observer, bridge extras) ================
// Same rules (game thread only, plain types). Covers the rest of css.h's game API for
// botoxnet/websurface/cod4_hud and anything else that only needs a read, not the vtable
// slot bugs that live in defs.h/css.h proper.

bool  Css_InBuyZone(int ent);
bool  Css_InBombZone(int ent);
bool  Css_HasDefuseKit(int ent);
bool  Css_IsDefusing(int ent);
bool  Css_HasHelmet(int ent);
float Css_FlashAlpha(int ent);     // 0..1, (duration vs curtime) * maxalpha, 0 if not flashed
bool  Css_IsVIP(int ent);

bool Css_C4Planted();
float Css_C4BlowTime();            // seconds until blow, see CPlantedC4::SecondsToBlow
float Css_C4DefuseProgress();      // 0..1, 1 = no bomb/not defusing
int  Css_C4CarrierIndex();         // entindex carrying the bomb, 0 = none
bool Css_BombsiteCenter(int which, float out[3]); // which: 0 = A, 1 = B

int  Css_ObserverMode(int ent);
int  Css_ObserverTargetIndex(int ent);

bool Css_PunchAngle(int ent, float out[3]);
bool Css_ViewOffset(int ent, float out[3]);
bool Css_EyeAngles(int ent, float outPitchYaw[2]);
int  Css_Flags(int ent);
bool Css_OnGround(int ent);

bool Css_PlayerName(int ent, char* buf, int bufSize);

// generic trace with an explicit skip entity (websurface needs this; CssBridge_TraceWorld
// above always skips the local player, this is the same trace with caller-chosen skip)
bool Css_TraceRay(const float start[3], const float dir[3], float maxDist, int skipEnt,
                   bool* outHit, float outEnd[3], float outNormal[3], int* outEntHit);

// ================ round 2 additions (lua port): bridged per the same rule as everything else in
// this file -- a real, already-verified aim-tree call, never a new vtable slot/offset guess ================

// Hull sweep (world+props), same Ray_t 4-arg Init (defs.h:6900) + filter movement.cpp's own
// TraceHull/TraceFilterWorldProps already use. skipEnt <= 0 = local player (CssBridge_TraceWorld's rule).
bool CssBridge_TraceHullWorld(const float start[3], const float end[3], const float mins[3], const float maxs[3],
                               float outEnd[3], float outNormal[3], float* outFraction, bool* outStartSolid, int skipEnt);

void CssBridge_SetViewAngles(float pitch, float yaw, float roll);   // I::EngineClient->SetViewAngles, live in aim_rcs.cpp:148
bool CssBridge_IsPlayingDemo();                                      // same IVEngineClient interface
bool CssBridge_IsLoopback();                                         // SDK::IsLoopback: hosting the server we play on

// SetupBones + pHitboxSet/pHitbox, the exact call esp.cpp/aim_hitscan.cpp already run per frame/shot.
bool CssBridge_GetBonePosition(int ent, int bone, float out[3]);
bool CssBridge_GetHitboxPosition(int ent, int hitbox, float out[3]);   // hitbox 12 = head (MEMORY ref-css-weapon-spread-slots)

int CssBridge_GetClassId(int ent);           // ClientClass::m_ClassID, e->GetClientClass() (same call Css_ClassnameIs makes)
const char* CssBridge_GetClassName(int ent); // generic e->GetClientClass()->GetName(), nullptr if none

// grenade projectile kind (DT_BaseCSGrenadeProjectile is shared by all 3 -- HE/flash/smoke
// only differ by world model, css-community-master/game/shared/cstrike/*grenade_projectile.cpp
// GRENADE_MODEL). 0 = HE, 1 = flashbang, 2 = smoke, -1 = not a grenade projectile.
int Css_GrenadeProjectileKind(int ent);

bool Css_IsHostage(int ent);          // DT_CHostage (c_cs_hostage.cpp, real client class)
int  Css_HostageLeaderIndex(int ent); // C_CHostage::m_leader entindex, 0 = none/not following

// ================ sun angle (UNIT D2) ================
// botox menu_tab_visuals.cpp:762-777 + edicts.cpp:858 sun_angle() poked CS:GO's cl_csm_rot_x/y/z
// engine convars directly -- CSS has no CSM, so there is no convar path. CSS's real sink is
// env_shadow_control (DT_ShadowControl: m_shadowDirection Vector, m_flShadowMaxDist float),
// applied once by C_ShadowControl::OnDataChanged -> g_pClientShadowMgr->SetShadowDirection/
// SetShadowDistance (source-sdk-2013 game/client/c_shadowcontrol.cpp:50-57). g_pClientShadowMgr
// itself is an unexported client.dll global with no confirmed pattern (CLAUDE.md: never guess a
// pattern) -- so instead of reaching it directly, this writes the LIVE env_shadow_control
// entity's own netvars (same FindNetworkTable-by-name + raw offset write every other netvar in
// this DLL uses) and re-fires its IClientNetworkable::OnDataChanged via
// IClientEntityList::GetClientNetworkable (slot 0, same already-used interface as GetClientEntity/
// GetHighestEntityIndex, defs.h:6604-6618) -- that virtual call is what makes the entity's
// PostDataUpdate rebroadcast into the real shadow manager, same as a server netvar push would.
// Requires the current map to actually place an env_shadow_control entity; if none exists these
// return false (nothing to drive) -- see UnitD2 report, SUN: log line states this per-map.
bool CssBridge_SunAngleSet(const float dir[3], float maxDist);  // false = no shadow_control entity on this map
void CssBridge_SunAngleRestore();                                // writes back the original (pre-hack) values once

// SKIN unit (visuals/skins.cpp) asks -- generic entity helpers, independent of player/weapon
// typed classes above.
int  Css_EntityTeam(int ent);                 // same as CssBridge_Team, generic entity
bool Css_EntityAlive(int ent);                // same as CssBridge_IsAlive
int  Css_ActiveWeaponIndex(int ent);          // same as CssBridge_ActiveWeapon
int  Css_ViewmodelIndex(int ent);             // CBasePlayer::m_hViewModel[0] entindex
int  Css_GetModelIndex(int ent);              // CBaseEntity::m_nModelIndex (stopgap netvar read)
bool Css_SetModelIndex(int ent, int modelIndex);
void Css_InvalidateBoneCache(int ent);        // CBaseEntity::InvalidateBoneCache() (defs.h SIGNATURE)
bool Css_ClassnameIs(int ent, const char* dtName); // GetClientClass()->GetName() == dtName
int  Css_OwnedWeapons(int ent, int* outIndices, int maxCount); // m_hMyWeapons -> entindices, returns count
