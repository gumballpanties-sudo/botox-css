// Every engine hook the aim half owns. One TU: each MAKE_HOOK registers itself with
// U::Hooks at static-init time, and the whitelist in aim.cpp decides which of them are
// actually installed -- compiling one is not installing one.

#include "sdk.h"
#include "ticks.h"
#include "engineprediction.h"
#include "aimbot.h"
#include "aim_rcs.h"
#include "nospread.h"
#include "nospread_hitscan.h"
#include "esp.h"
#include "backtrack.h"
#include "scope_fov.h"
#include "hud_aim.h"   // unit hud: botox hud group data, collected in IEngineVGui_Paint

// Remove interp targets: remote players + the viewmodel. ref hits every non-local entity;
// projectiles / props / the local player model stay smooth here.
static bool RemoveInterpTarget(void* rcx)
{
	if (!G::RemoveInterp() || rcx == H::Entities.GetLocal())
		return false;
	auto pEntity = reinterpret_cast<CBaseEntity*>(rcx);
	return pEntity->IsPlayer() || pEntity->GetClassID() == ETFClassID::CTFViewModel;
}

// ================ CBaseAnimating_Interpolate.cpp ================
MAKE_SIGNATURE(CBaseAnimating_Interpolate, "client.dll", "48 8B C4 48 89 70 ? F3 0F 11 48", 0x0);

MAKE_HOOK(CBaseAnimating_Interpolate, S::CBaseAnimating_Interpolate(), bool,
	void* rcx, float currentTime)
{

	if (rcx == H::Entities.GetLocal() ? F::Ticks.m_bRecharge : RemoveInterpTarget(rcx))
		return true;

	// fixes weird stupid flickering with the resolver, but completely disables interpolation
	// if anyone knows of a better solution let me know. doesn't seem to cause nonvisual issues so i'm leaving this commented out for now
	//if (F::Resolver.GetAngles(reinterpret_cast<CCSPlayer*>(rcx)))
	//	return true;

	return CALL_ORIGINAL(rcx, currentTime);
}

// ================ CBaseAnimating_MaintainSequenceTransitions.cpp ================
MAKE_SIGNATURE(CBaseAnimating_MaintainSequenceTransitions, "client.dll", "4C 89 4C 24 ? 41 56", 0x0);

MAKE_HOOK(CBaseAnimating_MaintainSequenceTransitions, S::CBaseAnimating_MaintainSequenceTransitions(), void,
	void* rcx, void* boneSetup, float flCycle, Vec3 pos[], Vector4D q[])
{

	// Stubbing this out entirely (empty body) makes animation poses snap straight to the current
	// sequence with no blend - the usual hitbox technique. But MaintainSequenceTransitions
	// runs for EVERY animating entity (local player + viewmodel included) on every bone setup, so
	// killing it unconditionally makes all anims snap between each other ("choppy / not interpolating")
	// even with nothing enabled. Only drop the transition blend when interpolation removal is actually
	// requested; otherwise run the engine's vanilla sequence-transition blending so anims look
	// non-injected when idle. (The engine itself already early-returns this during prediction, so the
	// local player's predicted pose is unaffected either way.) Same targets as Interpolate.
	if (RemoveInterpTarget(rcx))
		return;

	CALL_ORIGINAL(rcx, boneSetup, flCycle, pos, q);
}

// ================ C_BaseViewModel::Interpolate (not in ref) ================
// sdk c_baseviewmodel.cpp:166. After the base Interpolate it re-derives the cycle from
// GetFinalPredictedTime() + interpolation_amount * TICK_INTERVAL every frame, so skipping the base
// alone leaves the gun animating smoothly. interpolation_amount = 0 for the call -> the cycle comes
// from the tick-aligned predicted time only and steps once per tick. client.dll+0x1f7ab0, found as
// the one direct caller of CBaseAnimating::Interpolate that reads gpGlobals +0x20 * +0x1c.
MAKE_SIGNATURE(CBaseViewModel_Interpolate, "client.dll", "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 0F 29 70 ? 33 FF 48 8B D9 0F 29 78 ? 0F 28 F9", 0x0);

MAKE_HOOK(CBaseViewModel_Interpolate, S::CBaseViewModel_Interpolate(), bool,
	void* rcx, float currentTime)
{
	if (!G::RemoveInterp())
		return CALL_ORIGINAL(rcx, currentTime);

	const float flOldAmount = I::GlobalVars->interpolation_amount;
	I::GlobalVars->interpolation_amount = 0.f;
	const bool bRet = CALL_ORIGINAL(rcx, currentTime);
	I::GlobalVars->interpolation_amount = flOldAmount;
	return bRet;
}

// ================ CBaseAnimating_SetSequence.cpp ================
MAKE_SIGNATURE(CBaseAnimating_SetSequence, "client.dll", "40 53 48 83 EC ? 48 8B D9 39 91 ? ? ? ? 74 ? 89 91", 0x0);

MAKE_HOOK(CBaseAnimating_SetSequence, S::CBaseAnimating_SetSequence(), void,
	void* rcx, int nSequence)
{
	auto pEntity = reinterpret_cast<CBaseAnimating*>(rcx);
	if (pEntity->m_nSequence() != nSequence && !pEntity->m_bSequenceLoops())
		pEntity->m_flCycle() = 0.f; // set on the server but not client

	CALL_ORIGINAL(rcx, nSequence);
}

// ================ CBaseAnimating_UpdateClientSideAnimation.cpp ================
// CSS sig: TF2's tail bytes ("84 C0 75 ? 33 FF 48 3B DF") don't match CSS (sig_check.md:
// MISSING). Re-derived against the live CSS client.dll (cstrike/bin/x64/client.dll):
// the 31-byte prefix up to and including the "FF 90" vtable call is unique (1 hit),
// verified with a relaxed-prefix scan, not guessed.
MAKE_SIGNATURE(CBaseAnimating_UpdateClientSideAnimation, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 8B F8 48 85 C0 74 ? 48 8B 00 48 8B CF FF 90", 0x0);

MAKE_HOOK(CBaseAnimating_UpdateClientSideAnimation, S::CBaseAnimating_UpdateClientSideAnimation(), void,
	void* rcx)
{

	auto pLocal = H::Entities.GetLocal();
	auto pPlayer = reinterpret_cast<CCSPlayer*>(rcx);
	// The local-player skip is what hands its animation over to the custom LocalAnimations() path in
	// CreateMove. Only do that when a feature actually needs it - otherwise let the engine advance the
	// local model per frame (vanilla, smooth). Demo playback already never skips, hence stays smooth.
	// Remove interp: remote players animate only from H::Entities.Store's per-tick loop
	// (G::UpdatingAnims). Local excluded -- LocalAnimations only steps it when a tick feature needs it.
	// CSS_STUB tf2 api: TF_COND_HALLOWEEN_KART dropped, CSS has no kart condition.
	if (G::RemoveInterp() && !G::UpdatingAnims && pPlayer != pLocal && pPlayer->IsPlayer()
		|| pPlayer == pLocal && !I::EngineClient->IsPlayingDemo() && F::Ticks.NeedsLocalAnimationFix())
		return;

	CALL_ORIGINAL(rcx);
}

// ref Entities.cpp per-tick animation catch-up (resolver half dropped). The hook above blocks
// remote players' per-frame update, so their anims advance once per received server tick here.
// Calling the sig address runs through the detour; G::UpdatingAnims lets it pass.
static void RemoveInterpAnims()
{
	if (!G::RemoveInterp())
		return;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CCSPlayer>();
		if (!pPlayer->IsAlive() || pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
			continue;

		int iDeltaTicks = TIME_TO_TICKS(H::Entities.GetDeltaTime(pPlayer->entindex()));
		if (!iDeltaTicks)
			continue;

		float flOldFrameTime = I::GlobalVars->frametime;
		I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
		for (int i = 0; i < iDeltaTicks; i++)
		{
			G::UpdatingAnims = true;
			S::CBaseAnimating_UpdateClientSideAnimation.Call<void>(pPlayer);
			G::UpdatingAnims = false;
		}
		I::GlobalVars->frametime = flOldFrameTime;
	}
}

// ================ CBaseEntity_BaseInterpolatePart1.cpp ================
MAKE_SIGNATURE(CBaseEntity_BaseInterpolatePart1, "client.dll", "48 89 5C 24 ? 56 57 41 55 41 56 41 57 48 83 EC ? 4C 8B BC 24", 0x0);

MAKE_HOOK(CBaseEntity_BaseInterpolatePart1, S::CBaseEntity_BaseInterpolatePart1(), int,
	void* rcx, float& currentTime, Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int& bNoMoreChanges)
{

	auto pEntity = reinterpret_cast<CBaseEntity*>(rcx);
	if (pEntity && pEntity->GetClassID() == ETFClassID::CTFViewModel && F::Ticks.m_bRecharge)
	{
		bNoMoreChanges = 1;
		return 0;
	}

	return CALL_ORIGINAL(rcx, currentTime, oldOrigin, oldAngles, oldVel, bNoMoreChanges);
}

// ================ CBaseEntity_EstimateAbsVelocity.cpp ================
MAKE_HOOK(CBaseEntity_EstimateAbsVelocity, S::CBaseEntity_EstimateAbsVelocity(), void,
	void* rcx, Vector& vel)
{

	auto pPlayer = reinterpret_cast<CCSPlayer*>(rcx);
	if (!pPlayer->IsPlayer())
		return CALL_ORIGINAL(rcx, vel);

	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
	{
		vel = pPlayer->m_vecVelocity();
		return;
	}

	if (!G::RemoveInterp())
	{
		CALL_ORIGINAL(rcx, vel);
		vel.z = pPlayer->m_vecVelocity().z;
	}
	else
		vel = pPlayer->m_vecVelocity();

}

// ================ CBaseEntity_ResetLatched.cpp ================
MAKE_SIGNATURE(CBaseEntity_ResetLatched, "client.dll", "40 56 48 83 EC ? 48 8B 01 48 8B F1 FF 90 ? ? ? ? 84 C0 75", 0x0);

MAKE_HOOK(CBaseEntity_ResetLatched, S::CBaseEntity_ResetLatched(), void,
	void* rcx)
{

	if (rcx == H::Entities.GetLocal())
		return;

	CALL_ORIGINAL(rcx);
}

// ================ CBaseEntity_SetAbsVelocity.cpp ================
class AxisSet
{
public:
	float m_flOldAxisValue = 0.f;
	float m_flOldSimulationTime = 0.f;
	float m_flNewAxisValue = 0.f;
	float m_flNewSimulationTime = 0.f;

public:
	float Get(bool bZ = false) const
	{
		int iDeltaTicks = TIME_TO_TICKS(m_flNewSimulationTime - m_flOldSimulationTime);
		float flGravityCorrection = 0.f;
		if (bZ)
		{
			static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
			float flDeltaTicks = float(iDeltaTicks) + 0.5f; // ?
			flGravityCorrection = (powf(flDeltaTicks, 2.f) - flDeltaTicks) / 2.f * sv_gravity->GetFloat() * powf(TICK_INTERVAL, 2);
		}
		float flDeltaValue = m_flNewAxisValue - m_flOldAxisValue;
		float flTickVelocity = flDeltaValue + (flDeltaValue ? 0.0625f * sign(m_flNewAxisValue) : 0.f) - flGravityCorrection;
		return flTickVelocity / TICKS_TO_TIME(iDeltaTicks);
	}
};

class AxisInfo
{
public:
	AxisSet x = {}, y = {}, z = {};

public:
	AxisSet& operator[](int i)
	{
		return ((AxisSet*)this)[i];
	}

	AxisSet operator[](int i) const
	{
		return ((AxisSet*)this)[i];
	}

	Vec3 Get(bool bGrounded = false) const
	{
		return { x.Get(), y.Get(), z.Get(!bGrounded) };
	}

	float Get(int i) const
	{
		return ((AxisSet*)this)[i].Get(i == 2);
	}
};

MAKE_SIGNATURE(CBasePlayer_PostDataUpdate_SetAbsVelocity_Call, "client.dll", "0F 28 74 24 ? 8B D6", 0x0);

MAKE_HOOK(CBaseEntity_SetAbsVelocity, S::CBaseEntity_SetAbsVelocity(), void,
	void* rcx, const Vec3& vecAbsVelocity)
{

	const auto dwDesired = S::CBasePlayer_PostDataUpdate_SetAbsVelocity_Call();
	const auto dwRetAddr = uintptr_t(_ReturnAddress());

	if (dwRetAddr != dwDesired)
		return CALL_ORIGINAL(rcx, vecAbsVelocity);
	
	const auto pPlayer = reinterpret_cast<CCSPlayer*>(rcx);
	if (pPlayer->IsDormant())
		return CALL_ORIGINAL(rcx, vecAbsVelocity);

	auto pRecords = H::Entities.GetOrigins(pPlayer->entindex());
	if (!pRecords || pRecords->empty())
		return CALL_ORIGINAL(rcx, vecAbsVelocity);

	auto& tOldRecord = pRecords->front();
	auto tNewRecord = VelFixRecord(pPlayer->m_vecOrigin() + Vec3(0, 0, pPlayer->GetSize().z), pPlayer->m_flSimulationTime());

	int iDeltaTicks = TIME_TO_TICKS(tNewRecord.m_flSimulationTime - tOldRecord.m_flSimulationTime);
	float flDeltaTime = TICKS_TO_TIME(iDeltaTicks);
	if (iDeltaTicks <= 0)
		return;

	static auto sv_lagcompensation_teleport_dist = H::ConVars.FindVar("sv_lagcompensation_teleport_dist");
	float flDist = powf(sv_lagcompensation_teleport_dist->GetFloat(), 2.f) * iDeltaTicks;
	if ((tNewRecord.m_vecOrigin - tOldRecord.m_vecOrigin).Length2DSqr() >= flDist)
		return pRecords->clear();

	bool bGrounded = pPlayer->IsOnGround();

	AxisInfo tAxisInfo = {};
	for (int i = 0; i < 3; i++)
	{
		tAxisInfo[i].m_flOldAxisValue = tOldRecord.m_vecOrigin[i];
		tAxisInfo[i].m_flNewAxisValue = tNewRecord.m_vecOrigin[i];
		tAxisInfo[i].m_flOldSimulationTime = tOldRecord.m_flSimulationTime;
		tAxisInfo[i].m_flNewSimulationTime = tNewRecord.m_flSimulationTime;

		if (i == 2 && bGrounded)
			break;

		float flOldPos1 = tOldRecord.m_vecOrigin[i], flOldPos2 = flOldPos1 + 0.125f * sign(flOldPos1);
		float flNewPos1 = tNewRecord.m_vecOrigin[i], flNewPos2 = flNewPos1 + 0.125f * sign(flNewPos1);
		if (!flOldPos1) flOldPos1 = -0.125f, flOldPos2 = 0.125f;
		if (!flNewPos1) flNewPos1 = -0.125f, flNewPos2 = 0.125f;

		FloatRange_t flVelocityRange;
		{
			std::deque<float> vDeltas = { flNewPos1 - flOldPos1, flNewPos2 - flOldPos1, flNewPos1 - flOldPos2, flNewPos2 - flOldPos2 };
			std::sort(vDeltas.begin(), vDeltas.end(), std::less<float>());
			flVelocityRange = { vDeltas.front() / flDeltaTime, vDeltas.back() / flDeltaTime };
		}

		for (auto& tRecord : *pRecords)
		{
			if (tAxisInfo[i].m_flOldSimulationTime <= tRecord.m_flSimulationTime)
				continue;

			float flRewind = -ROUND_TO_TICKS(tNewRecord.m_flSimulationTime - tRecord.m_flSimulationTime);
			FloatRange_t flPositionRange = { tAxisInfo[i].m_flNewAxisValue + flVelocityRange.Max * flRewind, tAxisInfo[i].m_flNewAxisValue + flVelocityRange.Min * flRewind };
			if (i == 2)
			{
				static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
				float flGravityCorrection = sv_gravity->GetFloat() * powf(flRewind + TICK_INTERVAL / 2, 2.f) / 2;
				flPositionRange.Min -= flGravityCorrection, flPositionRange.Max -= flGravityCorrection;
			}
			if (flPositionRange.Min > tRecord.m_vecOrigin[i] || tRecord.m_vecOrigin[i] > flPositionRange.Max)
				break;

			tAxisInfo[i].m_flOldAxisValue = tRecord.m_vecOrigin[i];
			tAxisInfo[i].m_flOldSimulationTime = tRecord.m_flSimulationTime;
		}
	}

	H::Entities.SetAvgVelocity(pPlayer->entindex(), tAxisInfo.Get(bGrounded));
	CALL_ORIGINAL(rcx, (tNewRecord.m_vecOrigin - tOldRecord.m_vecOrigin) / flDeltaTime);
}

// ================ CBasePlayer_ItemPostFrame.cpp ================
MAKE_SIGNATURE(CBasePlayer_ItemPostFrame, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 39 48 8B F1 FF 97", 0x0);
MAKE_SIGNATURE(GetAnimationEvent, "server.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F 29 74 24 ? 0F 28 F3 49 8B F0", 0x0);

struct animevent_t
{
	int				event;
	const char*		options;
	float			cycle;
	float			eventtime;
	int				type;
	CBaseAnimating* pSource;
};

typedef enum
{
	AE_INVALID = -1,
	AE_EMPTY,
	AE_NPC_LEFTFOOT,
	AE_NPC_RIGHTFOOT,
	AE_NPC_BODYDROP_LIGHT,
	AE_NPC_BODYDROP_HEAVY,
	AE_NPC_SWISHSOUND,
	AE_NPC_180TURN,
	AE_NPC_ITEM_PICKUP,
	AE_NPC_WEAPON_DROP,
	AE_NPC_WEAPON_SET_SEQUENCE_NAME,
	AE_NPC_WEAPON_SET_SEQUENCE_NUMBER,
	AE_NPC_WEAPON_SET_ACTIVITY,
	AE_NPC_HOLSTER,
	AE_NPC_DRAW,
	AE_NPC_WEAPON_FIRE,
	AE_CL_PLAYSOUND,
	AE_SV_PLAYSOUND,
	AE_CL_STOPSOUND,
	AE_START_SCRIPTED_EFFECT,
	AE_STOP_SCRIPTED_EFFECT,
	AE_CLIENT_EFFECT_ATTACH,
	AE_MUZZLEFLASH,
	AE_NPC_MUZZLEFLASH,
	AE_THUMPER_THUMP,
	AE_AMMOCRATE_PICKUP_AMMO,
	AE_NPC_RAGDOLL,
	AE_NPC_ADDGESTURE,
	AE_NPC_RESTARTGESTURE,
	AE_NPC_ATTACK_BROADCAST,
	AE_NPC_HURT_INTERACTION_PARTNER,
	AE_NPC_SET_INTERACTION_CANTDIE,
	AE_SV_DUSTTRAIL,
	AE_CL_CREATE_PARTICLE_EFFECT,
	AE_RAGDOLL,
	AE_CL_ENABLE_BODYGROUP,
	AE_CL_DISABLE_BODYGROUP,
	AE_CL_BODYGROUP_SET_VALUE,
	AE_CL_BODYGROUP_SET_VALUE_CMODEL_WPN,
	AE_WPN_PRIMARYATTACK,
	AE_WPN_INCREMENTAMMO,
	AE_WPN_HIDE,
	AE_WPN_UNHIDE,
	AE_WPN_PLAYWPNSOUND,
	AE_RD_ROBOT_POP_PANELS_OFF,
	AE_TAUNT_ENABLE_MOVE,
	AE_TAUNT_DISABLE_MOVE,
	AE_CL_REMOVE_PARTICLE_EFFECT,
	LAST_SHARED_ANIMEVENT,
} Animevent;

// Anim-event-driven single-shell ammo increment (TF2 rocket launcher / direct hit / particle
// cannon reload-cancel mechanic) deleted: no CS:S weapon uses anim-event ammo increment (CS
// clip counts are server-networked, not client-side-incremented off a reload animation event) --
// the switch above only ever matched those 3 TF2 weapon IDs, which don't exist in CSWeaponID.
MAKE_HOOK(CBasePlayer_ItemPostFrame, S::CBasePlayer_ItemPostFrame(), void,
	void* rcx)
{
	CALL_ORIGINAL(rcx);
}

// ================ CClientState_ProcessFixAngle.cpp ================
MAKE_SIGNATURE(CClientState_ProcessFixAngle, "engine.dll", "40 53 48 83 EC ? F3 0F 10 42", 0x0);

// dih build: the F::Spectate stash is gone with that feature. The angle-forcing block stays --
// it is what stops the server snapping the view back out from under a silent aim angle.
MAKE_HOOK(CClientState_ProcessFixAngle, S::CClientState_ProcessFixAngle(), bool,
	CClientState* rcx, SVC_FixAngle* msg)
{
	if (Vars::Visuals::Removals::AngleForcing.Value)
		return false;

	return CALL_ORIGINAL(rcx, msg);
}

// ================ CHLClient_CreateMove.cpp ================
// The movement pipeline (bhop, autostrafe, wall climb, pixel-surf assist, ...) lives in
// dih's own src/hacks/movement/movement.cpp. It used to run from dih's own CHLClient::CreateMove
// hook; MinHook only allows one hook per target, and this one needs the send-packet pointer
// off its own frame, so this file is now the single CreateMove hook and dih's pipeline is
// called from it -- in the slot F::Misc.RunPre had, i.e. BEFORE the aimbot. Order matters:
// dih's wall climb assigns pCmd->buttons wholesale, so it has to be done writing before the
// aimbot ORs in IN_ATTACK, or the shot is eaten.
struct UserCmd_t;
void Movement_RunGuarded(UserCmd_t* pCmd);
void Movement_AirStuckLastWriter(UserCmd_t* pCmd);   // botox air_stuck_final_stamp + shot_proof, after the aimbot
// the movement half's air stuck (airstuck.inl AS_Sync) reads the server's ack to diff its pins
int Aim_LastCommandAck() { return I::ClientState ? I::ClientState->last_command_ack : -1; }

#define MATH_EPSILON (1.f / 16)
#define PSILENT_EPSILON (1.f - MATH_EPSILON)
#define REAL_EPSILON (0.1f + MATH_EPSILON)
#define SNAP_SIZE_EPSILON (10.f - MATH_EPSILON)
#define SNAP_NOISE_EPSILON (0.5f + MATH_EPSILON)

// CSS_STUB tf2 api: IHasGenericMeter_GetMeterMultiplier sig removed (MISSING in CSS,
// sig_check.md) -- TF2 Pyro Dragon's Fury meter, no CSS equivalent. Its one call site
// (TF_WEAPON_FLAME_BALL case below) is dead TF2 weapon-ID logic, see NEEDS FROM OTHERS.

struct CmdHistory_t
{
	Vec3 m_vAngle;
	bool m_bAttack1;
	bool m_bAttack2;
	bool m_bSendingPacket;
};

static inline void UpdateInfo(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd)
{
	G::PSilentAngles = G::SilentAngles = G::Attacking = G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;

	if (!pWeapon)
		return;

	G::CanPrimaryAttack = G::CanSecondaryAttack = G::Reloading = false;

	if (pWeapon->GetMaxClip1() != WEAPON_NOCLIP && !pWeapon->m_bReloadsSingly())
	{	// dumb fix
		float flOldCurtime = I::GlobalVars->curtime;
		I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
		pWeapon->CheckReload();
		I::GlobalVars->curtime = flOldCurtime;
	}

	// CCSPlayer::CanAttack doesn't exist (that's CTFPlayer's, gated on taunt/zoom/condition
	// state) -- CS:S's own gate is simply "alive" (weapon_csbase.cpp CanAttack-equivalent
	// checks run through CBasePlayer::IsAlive() + the per-weapon next-attack times below).
	bool bCanAttack = pLocal->IsAlive();
	{
		static int iStaticItemDefinitionIndex = 0;
		int iOldItemDefinitionIndex = iStaticItemDefinitionIndex;
		int iNewItemDefinitionIndex = iStaticItemDefinitionIndex = pWeapon->m_iItemDefinitionIndex();

		if (iNewItemDefinitionIndex != iOldItemDefinitionIndex || !bCanAttack || !pWeapon->m_iClip1())
			F::Ticks.m_iWait = 1;
	}
	if (bCanAttack)
	{
		// CWeaponCSBase has no CanPrimaryAttack()/CanSecondaryAttack()/IsInReload()/
		// HasPrimaryAmmoForShot()/GetSlot() (all CTFWeaponBase's) -- CBaseCombatWeapon already
		// has the real fields (m_flNextPrimaryAttack/m_flNextSecondaryAttack/m_iClip1/
		// m_bInReload, defs.h, vtable_check.md-verified), weapon_csbasegun.cpp's own
		// CanPrimaryAttack/CanSecondaryAttack are exactly "curtime past the next-attack time".
		// Tickbase time, not frame curtime: ItemPostFrame runs inside RunCommand with curtime =
		// tickbase (weapon_csbase.cpp:499), frame curtime lags it by ~ping -> every "can shoot"
		// tick was the wrong tick and nospread/silent fired on non-shots. Player m_flNextAttack
		// gates ItemPostFrame too (deploy). Same as ref CTFWeaponBase::CanPrimaryAttack.
		const float flCurTime = TICKS_TO_TIME(pLocal->m_nTickBase());
		const bool bPlayerReady = pLocal->m_flNextAttack() <= flCurTime;
		G::CanPrimaryAttack = bPlayerReady && flCurTime >= pWeapon->m_flNextPrimaryAttack();
		G::CanSecondaryAttack = bPlayerReady && flCurTime >= pWeapon->m_flNextSecondaryAttack();

		// CSS rewrite (task: cssourcex64 Features/Aimbot.cpp ShouldRun as model): TF2's
		// per-weapon switch (minigun spin-up, flaregun underwater, particle-cannon charge,
		// medigun/builder/laser-pointer passthrough) has no CS:S equivalent -- CS:S's own
		// weapons (css.h CSWeaponID/CWeaponCSBase) have none of these mechanics, every gun
		// just fires/reloads on ammo+cooldown. Keep only that generic (engine-shared) logic.
		if (!pWeapon->IsKnife())
		{
			bool bAmmo = pWeapon->m_iClip1() > 0 || pWeapon->GetMaxClip1() == WEAPON_NOCLIP;
			bool bReload = pWeapon->m_bInReload();
			if (!bAmmo)
				G::CanPrimaryAttack = G::CanSecondaryAttack = false;
			if (bReload && bAmmo && !G::CanPrimaryAttack)
				G::Reloading = true;
		}
		// CSS has no flamethrower/flaregun underwater restriction (no such weapons exist).
	}

	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd);
	G::PrimaryWeaponType = SDK::GetWeaponType(pWeapon, &G::SecondaryWeaponType);
	// CanHeadshot()/AmbassadorCanHeadshot() (CTFWeaponBase's -- Ambassador is TF2's spy
	// revolver, no CS:S equivalent) deleted: every CS:S hitscan weapon can headshot, there is
	// no per-weapon "can this gun ever headshot" flag to read.
	G::CanHeadshot = true;
}

static inline void LocalAnimations(CCSPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket)
{
	// CLEAN POV DEMOS - record-time bisection guard. This routine FrameAdvances the local player's
	// animation every send-packet tick with curtime forced to the (future) tickbase time.
	if (I::EngineClient && I::EngineClient->IsRecordingDemo() && !I::EngineClient->IsPlayingDemo())
		return;

	// VANILLA WHEN IDLE - when no feature needs the custom local-animation path, skip it entirely so the
	// engine's per-frame UpdateClientSideAnimation runs instead.
	if (!F::Ticks.NeedsLocalAnimationFix())
		return;

	// NEEDS FROM OTHERS (SDK unit, css.h): CCSPlayer has no m_PlayerAnimState exposed (that's
	// CTFPlayer's CTFPlayerAnimState) -- CS:S's own anim state is C_CSPlayer::GetPlayerAnimState()
	// (c_cs_player.cpp, used by FX_FireBullets' DoAnimationEvent calls), a different class with
	// an unverified vtable slot/layout. No guessed slot: this path is a no-op until that accessor
	// exists. IsTaunting()/m_bAllowMoveDuringTaunt()/m_flTauntYaw (TF2-only) deleted outright.
	(void)pCmd; (void)bSendPacket;
}

static inline void AntiCheatCompatibility(CUserCmd* pCmd, bool* pSendPacket)
{
	if (!Vars::Misc::Game::AntiCheatCompatibility.Value)
		return;

	Math::ClampAngles(pCmd->viewangles); // shouldn't happen, but failsafe

	static std::deque<CmdHistory_t> vHistory;
	vHistory.emplace_front(pCmd->viewangles, bool(pCmd->buttons & IN_ATTACK), bool(pCmd->buttons & IN_ATTACK2), *pSendPacket);
	if (vHistory.size() > 5)
		vHistory.pop_back();

	if (vHistory.size() < 3)
		return;

	// prevent trigger checks, though this shouldn't happen ordinarily
	if (!vHistory[0].m_bAttack1 && vHistory[1].m_bAttack1 && !vHistory[2].m_bAttack1)
		pCmd->buttons |= IN_ATTACK;
	if (!vHistory[0].m_bAttack2 && vHistory[1].m_bAttack2 && !vHistory[2].m_bAttack2)
		pCmd->buttons |= IN_ATTACK2;

	// don't care if we are actually attacking or not, a miss is less important than a detection
	if (vHistory[0].m_bAttack1 || vHistory[1].m_bAttack1 || vHistory[2].m_bAttack1)
	{
		// prevent silent aim checks
		if (Math::CalcFov(vHistory[0].m_vAngle, vHistory[1].m_vAngle) > PSILENT_EPSILON
			&& Math::CalcFov(vHistory[0].m_vAngle, vHistory[2].m_vAngle) < REAL_EPSILON)
		{
			pCmd->viewangles = vHistory[1].m_vAngle.LerpAngle(vHistory[0].m_vAngle, 0.5f);
			if (Math::CalcFov(pCmd->viewangles, vHistory[2].m_vAngle) < REAL_EPSILON)
				pCmd->viewangles = vHistory[0].m_vAngle + Vec3(0.f, REAL_EPSILON * 2);
			vHistory[0].m_vAngle = pCmd->viewangles;
			vHistory[0].m_bSendingPacket = *pSendPacket = vHistory[1].m_bSendingPacket;
		}

		// prevent aim snap checks
		if (vHistory.size() == 5)
		{
			float flDelta01 = Math::CalcFov(vHistory[0].m_vAngle, vHistory[1].m_vAngle);
			float flDelta12 = Math::CalcFov(vHistory[1].m_vAngle, vHistory[2].m_vAngle);
			float flDelta23 = Math::CalcFov(vHistory[2].m_vAngle, vHistory[3].m_vAngle);
			float flDelta34 = Math::CalcFov(vHistory[3].m_vAngle, vHistory[4].m_vAngle);

			if ((
				flDelta12 > SNAP_SIZE_EPSILON && flDelta23 < SNAP_NOISE_EPSILON && vHistory[2].m_vAngle != vHistory[3].m_vAngle
				|| flDelta23 > SNAP_SIZE_EPSILON && flDelta12 < SNAP_NOISE_EPSILON && vHistory[1].m_vAngle != vHistory[2].m_vAngle
				)
				&& flDelta01 < SNAP_NOISE_EPSILON && vHistory[0].m_vAngle != vHistory[1].m_vAngle
				&& flDelta34 < SNAP_NOISE_EPSILON && vHistory[3].m_vAngle != vHistory[4].m_vAngle)
			{
				pCmd->viewangles.y += SNAP_NOISE_EPSILON * 2;
				vHistory[0].m_vAngle = pCmd->viewangles;
				vHistory[0].m_bSendingPacket = *pSendPacket = vHistory[1].m_bSendingPacket;
			}
		}
	}
}

MAKE_HOOK(CHLClient_CreateMove, U::Memory.GetVirtual(I::Client, 21), void,
	void* rcx, int sequence_number, float input_sample_frametime, bool active)
{
	CALL_ORIGINAL(rcx, sequence_number, input_sample_frametime, active);

	auto pLocal = H::Entities.GetLocal();
	auto pWeapon = H::Entities.GetWeapon();
	if (!pLocal)
		return;

	bool* pSendPacket = reinterpret_cast<bool*>(uintptr_t(_AddressOfReturnAddress()) + 0x20);
	CUserCmd* pCmd = &I::Input->m_pCommands[sequence_number % MULTIPLAYER_BACKUP];

	I::Prediction->Update(I::ClientState->m_nDeltaTick, I::ClientState->m_nDeltaTick > 0, I::ClientState->last_command_ack, I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands);

	UpdateInfo(pLocal, pWeapon, pCmd);
	F::Backtrack.BeginCommand(pCmd, pLocal);   // botox create_move_proxy: server tick before anything shoots
	ScopeFOV::Run(pLocal);
		Movement_RunGuarded(reinterpret_cast<UserCmd_t*>(pCmd)); // dih movement pipeline, in Misc.RunPre's slot
	// NOTE: EnginePrediction.Start is called inside F::Ticks.Start (and paired with the single
	F::Ticks.Start(pLocal, pCmd);

		F::Aimbot.Run(pLocal, pWeapon, pCmd);
	F::Ticks.End(pLocal, pCmd);
		// F::CritHack.Run deleted -- no CS:S crit concept (AGENT_BRIEF DE-TF2 rule).
		F::NoSpread.Run(pLocal, pWeapon, pCmd);
		F::Ticks.CreateMove(pLocal, pWeapon, pCmd, pSendPacket);
		// F::NoSpreadHitscan.AskForPlayerPerf() deleted -- CSS always has the real seed already
		// (nospread_hitscan.h header), no server round-trip needed.
	F::EnginePrediction.End(pLocal, pCmd);

	// botox create_move_proxy.cpp:481/505: air stuck re-proves the cmd AFTER the aimbot touched it
	Movement_AirStuckLastWriter(reinterpret_cast<UserCmd_t*>(pCmd));

	AntiCheatCompatibility(pCmd, pSendPacket);
	LocalAnimations(pLocal, pCmd, *pSendPacket);

	G::Choking = !*pSendPacket;
	G::LastUserCmd = pCmd;
}

// ================ CHLClient_DispatchUserMessage.cpp ================
// DEAD on CSS: this hook existed only to swallow the server's "playerperf" TextMsg reply
// (CNoSpreadHitscan::AskForPlayerPerf/ParsePlayerPerf, both deleted -- see nospread_hitscan.h
// header: CSS always has the real random_seed already, no server round-trip needed). Kept as a
// pure pass-through install point rather than removed outright -- nothing else currently needs
// this vtable slot, and re-adding a hook here later is one line, not a new MinHook target.
MAKE_HOOK(CHLClient_DispatchUserMessage, U::Memory.GetVirtual(I::Client, 36), bool,
	void* rcx, UserMessageType type, bf_read& msgData)
{
	return CALL_ORIGINAL(rcx, type, msgData);
}

// ================ CHLClient_EncodeUserCmdToBuffer.cpp ================
// Clean POV demos - usercmd path, INTENTIONALLY pass-through now.
MAKE_HOOK(CHLClient_EncodeUserCmdToBuffer, U::Memory.GetVirtual(I::Client, 24), void,
	void* rcx, bf_write& buf, int slot)
{

	CALL_ORIGINAL(rcx, buf, slot);
}

// ================ CHLClient_FrameStageNotify.cpp ================
// The per-net-update spine the aimbot reads: the entity cache, the movement sim's velocity
// history, the crit tracker and the aimbot's own real-path store. Nothing visual runs here --
// dih's own visual features have their own paths.
MAKE_HOOK(CHLClient_FrameStageNotify, U::Memory.GetVirtual(I::Client, 35), void,
	void* rcx, ClientFrameStage_t curStage)
{
	if (G::Unload)
		return CALL_ORIGINAL(rcx, curStage);

	CALL_ORIGINAL(rcx, curStage);

	switch (curStage)
	{
	case FRAME_NET_UPDATE_START:
		H::Entities.Clear();
		break;
	case FRAME_NET_UPDATE_END:
		H::Entities.Store();
		RemoveInterpAnims();
		// F::AimbotMelee.StoreBackstabSnapshots / F::MoveSim.Store / F::CritHack.Store deleted --
		// no CS:S melee-backstab/TF2-movesim/crit concept (AGENT_BRIEF DE-TF2 rule).
		F::Backtrack.Store();                      // botox lagcomp records: network origin, before interpolation
		// F::Aimbot.Store() deleted -- CAimbot (aimbot.h) has no Store(); RCS/backtrack already
		// keep their own history above/below.
		break;
	case FRAME_RENDER_START:
		F::AimbotRCS.RenderFrame();                // aim_rcs.h: between-tick recoil glide step
		break;
	}
}

// ================ CL_ProcessPacketEntities.cpp ================
MAKE_SIGNATURE(CL_ProcessPacketEntities, "engine.dll", "48 89 7C 24 ? 4C 89 64 24 ? 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4C 8B F9", 0x0);

struct CriticalStorage_t
{
	float m_flCritTokenBucket;
	int m_nCritChecks;
	int m_nCritSeedRequests;
};

// NOT a backtrack hook either: a full (non-delta) snapshot wipes the local weapons' crit
// bookkeeping, and this saves/restores it around the update so the crit hack keeps its counts.
MAKE_HOOK(CL_ProcessPacketEntities, S::CL_ProcessPacketEntities(), bool,
	SVC_PacketEntities* entmsg)
{
	if (entmsg->m_bIsDelta) // we won't need to restore
		return CALL_ORIGINAL(entmsg);

	CCSPlayer* pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->m_hMyWeapons())
	{
		SDK::Output("ProcessPacketEntities", "Failed to restore weapon crit data! (1)", { 255, 100, 100 });
		return CALL_ORIGINAL(entmsg);
	}

	std::unordered_map<int, CriticalStorage_t> mCriticalStorage = {};

	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(i);
		if (!pWeapon)
			continue;

		mCriticalStorage[i].m_flCritTokenBucket = pWeapon->m_flCritTokenBucket();
		mCriticalStorage[i].m_nCritChecks = pWeapon->m_nCritChecks();
		mCriticalStorage[i].m_nCritSeedRequests = pWeapon->m_nCritSeedRequests();

		if (Vars::Debug::Logging.Value) I::CVar->ConsolePrintf("\n");
		SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): mCriticalStorage[i].m_flCritTokenBucket = {}", i, uintptr_t(pWeapon), pWeapon->m_flCritTokenBucket()).c_str(), { 100, 150, 255 }, Vars::Debug::Logging.Value);
		SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): mCriticalStorage[i].m_nCritChecks = {}", i, uintptr_t(pWeapon), pWeapon->m_nCritChecks()).c_str(), { 100, 150, 255 }, Vars::Debug::Logging.Value);
		SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): mCriticalStorage[i].m_nCritSeedRequests = {}", i, uintptr_t(pWeapon), pWeapon->m_nCritSeedRequests()).c_str(), { 100, 150, 255 }, Vars::Debug::Logging.Value);
	}

	bool bReturn = CALL_ORIGINAL(entmsg);

	pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->m_hMyWeapons())
	{
		SDK::Output("ProcessPacketEntities", "Failed to restore weapon crit data! (2)", { 255, 100, 100 });
		return bReturn;
	}

	for (auto& [iSlot, tStorage] : mCriticalStorage)
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(iSlot);
		if (!pWeapon)
			break;

		pWeapon->m_flCritTokenBucket() = tStorage.m_flCritTokenBucket;
		pWeapon->m_nCritChecks() = tStorage.m_nCritChecks;
		pWeapon->m_nCritSeedRequests() = tStorage.m_nCritSeedRequests;

		if (Vars::Debug::Logging.Value) I::CVar->ConsolePrintf("\n");
		SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_flCritTokenBucket() = {}", iSlot, uintptr_t(pWeapon), pWeapon->m_flCritTokenBucket()).c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);
		SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_nCritChecks() = {}", iSlot, uintptr_t(pWeapon), pWeapon->m_nCritChecks()).c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);
		SDK::Output("ProcessPacketEntities", std::format("{} ({:#x}): pWeapon->m_nCritSeedRequests() = {}", iSlot, uintptr_t(pWeapon), pWeapon->m_nCritSeedRequests()).c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);
	}

	return bReturn;
}

// ================ CPrediction_GetLocalViewAngles.cpp ================
// Clean POV demos (primary fix - mirrors the get_local_view_angles_hook).
// The engine demo recorder fills each demo cmdinfo's local view angle from CPrediction::GetLocalViewAngles
MAKE_SIGNATURE(CPrediction_GetLocalViewAngles, "client.dll", "40 53 48 83 EC ? 48 8B DA E8 ? ? ? ? 48 8B C8 48 85 C0 75 ? 0F 57 C0", 0x0);

MAKE_HOOK(CPrediction_GetLocalViewAngles, S::CPrediction_GetLocalViewAngles(), void,
	void* rcx, Vec3* ang)
{

	CALL_ORIGINAL(rcx, ang);

	if (!ang)
		return;
	// Clean POV demos cleans the silent snap; Fake POV (Movement > FAKE POV) adds a deliberate offset.
	// Either one wants to drive the recorded demo camera.
	if (!Vars::Misc::Game::CleanPOVDemos.Value && !G::FakePOVActive)
		return;
	if (!I::Prediction || I::Prediction->m_bInPrediction)
		return;
	if (!I::EngineClient || !I::EngineClient->IsRecordingDemo() || I::EngineClient->IsPlayingDemo())
		return;
	if (!H::Entities.GetLocal())
		return;

	// Engine view = what the screen shows. Every cmd-only angle write (silent/psilent aim, backstab,
	// auto rocket jump, airblast, edgebug/airstuck/psassist/texturebug, anticheat compat) never
	// reaches it, so none of them can leak into the demo -- flagged or not. Deliberate visible
	// writes (non-silent aim, EngineSetViewAngles) do, matching what you saw live.
	Vec3 vAng = I::EngineClient->GetViewAngles();
	if (G::FakePOVActive)
	{	// Add the smoothed fake-POV offset (relative to the real view), clamping/normalizing the result.
		float flPitch = vAng.x + G::FakePOVOffset.x;
		vAng.x = flPitch < -89.f ? -89.f : (flPitch > 89.f ? 89.f : flPitch);
		vAng.y += G::FakePOVOffset.y;
		while (vAng.y > 180.f) vAng.y -= 360.f;
		while (vAng.y < -180.f) vAng.y += 360.f;
	}
	*ang = vAng;
}

// ================ CPrediction_RunSimulation.cpp ================
MAKE_SIGNATURE(CPrediction_RunSimulation, "client.dll", "48 83 EC 38 4C 8B 44", 0x0);

struct TickbaseFix_t
{
	CUserCmd* m_pCmd;
	int m_iLastOutgoingCommand;
	int m_iTickbaseShift;
};
static std::vector<TickbaseFix_t> s_vTickbaseFixes = {};

MAKE_HOOK(CPrediction_RunSimulation, S::CPrediction_RunSimulation(), void,
	void* rcx, int current_command, float curtime, CUserCmd* cmd, CCSPlayer* localPlayer)
{

	if (F::Ticks.m_bShifting && F::Ticks.m_iShiftedTicks + 1 == F::Ticks.m_iShiftStart)
		s_vTickbaseFixes.emplace_back(cmd, I::ClientState->lastoutgoingcommand, F::Ticks.m_iShiftStart - F::Ticks.m_iShiftedGoal);

	for (auto it = s_vTickbaseFixes.begin(); it != s_vTickbaseFixes.end();)
	{
		if (it->m_iLastOutgoingCommand < I::ClientState->last_command_ack)
		{
			it = s_vTickbaseFixes.erase(it);
			continue;
		}
		if (cmd == it->m_pCmd)
		{
			localPlayer->m_nTickBase() -= it->m_iTickbaseShift;
			break;
		}
		++it;
	}

	F::EnginePrediction.AdjustPlayers(localPlayer);
	CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
	F::EnginePrediction.RestorePlayers();
}

// CSS_STUB tf2 api: five hooks removed, all MISSING in CSS per sig_check.md and all
// pure TF2 gameplay with no CSS equivalent (conditions/zoom-scope/taunt/kart InCond
// patch sites, TF2 bone-duck-flag BuildTransformations fixup, rocket launcher reload
// misfire, random/ranged crit calc). Removed, not stubbed: nothing outside this file
// called these hook functions by name (MAKE_HOOK self-registers via its own CHook
// ctor, see utils.h), so there is no external caller to keep compiling.
//   - CTFPlayerShared_InCond (+ 5 *_InCond_Call patch-site sigs)
//   - CTFPlayer_BuildTransformations
//   - CTFRocketLauncher_CheckReloadMisfire
//   - CTFWeaponBase_CalcIsAttackCritical
//   - CTFWeaponBase_CanFireRandomCriticalShot
// F::CritHack (crithack.cpp, aim unit) and the scope/taunt/kart Vars::* removal
// toggles are now unreachable from this file; still compile (own translation units).

// ================ DataTable_Warning.cpp ================
MAKE_SIGNATURE(DataTable_Warning, "engine.dll", "48 89 4C 24 ? 48 89 54 24 ? 4C 89 44 24 ? 4C 89 4C 24 ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4C 8B C1 4C 8D 8C 24 ? ? ? ? 48 8D 4C 24 ? 8D 50", 0x0);

MAKE_HOOK(DataTable_Warning, S::DataTable_Warning(), void,
	const char* pInMessage, ...)
{
	// dont send datatable warnings
}

// ================ IEngineVGui_Paint.cpp ================
// The aim-side overlays: the tick bar, the doubletap/shift-tick indicator, the crit-hack
// readout and the no-spread state. This is the engine surface pass, not dih's ImGui overlay,
// and it is a different target from dih's vgui IPanel::PaintTraverse hook -- no collision.
MAKE_HOOK(IEngineVGui_Paint, U::Memory.GetVirtual(I::EngineVGui, 14), void,
	void* rcx, int iMode)
{
	if (G::Unload)
		return CALL_ORIGINAL(rcx, iMode);

	if (iMode & PAINT_INGAMEPANELS && !SDK::CleanScreenshot())
	{
		H::Draw.UpdateScreenSize();
		H::Draw.UpdateW2SMatrix();
		H::Draw.Start(true);
		if (auto pLocal = H::Entities.GetLocal())
		{
			F::NoSpreadHitscan.Draw(pLocal);
			// F::CritHack.Draw deleted -- no CS:S crit concept (AGENT_BRIEF DE-TF2 rule).
			F::Ticks.Draw(pLocal);
			F::Esp.Draw(pLocal);   // Unit C: player/world ESP, piggybacks this hook, no new install
			HudAim_Collect(pLocal);   // unit hud: snapshot for the ImGui hud overlays (hud/hud_snap.h)
			void WmAim_Tick(CCSPlayer*); WmAim_Tick(pLocal);   // unit wm: dna watermark "spotted" (wm_aim.cpp)
		}
		H::Draw.End();
	}

	CALL_ORIGINAL(rcx, iMode);
}

// ================ RecvProxy_SimulationTime.cpp ================
MAKE_SIGNATURE(RecvProxy_SimulationTime, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 8B 59 ? 48 8B FA 48 8B 42 ? 48 8D 4A ? FF 50 ? 4C 8B 0D ? ? ? ? 99 41 F7 79 ? 41 8B 49 ? 44 8B C1 44 2B C2 41 8B C0 99 41 F7 79 ? 8D 41 ? 44 2B C2 44 03 C3 44 3B C0 7D ? 41 2B C0 41 81 C0 ? ? ? ? FF C8 25 ? ? ? ? 44 03 C0 8D 41 ? 44 3B C0 7E ? 41 8B C8 2B C8 B8 ? ? ? ? FF C9 81 E1 ? ? ? ? 2B C1 44 03 C0 48 8B 5C 24 ? 66 41 0F 6E C0 0F 5B C0 F3 41 0F 59 41 ? F3 0F 11 87 ? ? ? ? 48 83 C4 ? 5F C3 CC CC CC CC CC 48 89 5C 24", 0x0);

static inline int GetNetworkBase(int nTick, int nEntity)
{
	int nEntityMod = nEntity % I::GlobalVars->nTimestampRandomizeWindow;
	int nBaseTick = I::GlobalVars->nTimestampNetworkingBase * int((nTick - nEntityMod) / I::GlobalVars->nTimestampNetworkingBase);
	return nBaseTick;
}

// Not a history hook, despite what the name suggests. This is what makes another
// player's m_flSimulationTime correct while they choke -- and H::Entities::Store derives
// m_mDeltaTimes / m_mLagTimes / m_mChokes and the m_mOrigins velocity history from exactly that
// delta. The projectile aimbot leads targets off that history, so without this hook the lead
// has nothing to predict on.
MAKE_HOOK(RecvProxy_SimulationTime, S::RecvProxy_SimulationTime(), void,
	const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	auto pEntity = reinterpret_cast<CBaseEntity*>(pStruct);
	if (!pEntity || !pEntity->IsPlayer() || pEntity->entindex() == I::EngineClient->GetLocalPlayer())
		return CALL_ORIGINAL(pData, pStruct, pOut);

	if (!pData->m_Value.m_Int) // fix setting invalid simtime every 100 ticks if choking
		return;

	int addt = pData->m_Value.m_Int;
	int t = GetNetworkBase(I::GlobalVars->tickcount, pEntity->entindex());
	t += addt;

	while (t < I::GlobalVars->tickcount - 127)
		t += 256;
	while (t > I::GlobalVars->tickcount + 127)
		t -= 256;

	pEntity->m_flSimulationTime() = TICKS_TO_TIME(t);
}
