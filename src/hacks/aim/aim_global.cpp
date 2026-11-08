#include "aim_global.h"

#include "aimbot.h"
#include "ticks.h"
#include "aim_fovreject.h"   // bounding-sphere FOV reject (pure, gate-tested)
#include "css_weapons.h"     // CSWeaponClass
#include "../misc/names.h"   // Names_AimAllowed -- player list "aimbot ignore"/"only aimbot" columns, plain types only

void CAimbotGlobal::SortTargets(std::vector<Target_t>& vTargets, int iMethod)
{	// Sort by preference
	std::sort(vTargets.begin(), vTargets.end(), [&](const Target_t& a, const Target_t& b) -> bool
		{
			switch (iMethod)
			{
			case Vars::Aimbot::General::TargetSelectionEnum::FOV: return a.m_flFOVTo < b.m_flFOVTo;
			case Vars::Aimbot::General::TargetSelectionEnum::Distance: return a.m_flDistTo < b.m_flDistTo;
			default: return false;
			}
		});
}

void CAimbotGlobal::SortPriority(std::vector<Target_t>& vTargets)
{	// Sort by priority. STABLE, because this always runs straight after SortTargets and every
	// target's priority is currently 0 (CAimbotGlobal::GetPriority) -- an unstable sort with a
	// comparator that is false for every pair is free to hand back any permutation it likes, and
	// the order it would be destroying is the distance order the knife picks its first swing off.
	std::stable_sort(vTargets.begin(), vTargets.end(), [&](const Target_t& a, const Target_t& b) -> bool
		{
			return a.m_nPriority > b.m_nPriority;
		});
}

// Hitboxes can sit slightly outside the collision bbox (arms out, a taunt), so the reject
// sphere below is padded before it is allowed to say no. Deliberately generous: this test
// exists to drop the far side of the map, not to be tight.
static constexpr float k_fovRejectScale = 1.5f;
static constexpr float k_fovRejectPad   = 16.f;

// this won't prevent shooting bones outside of fov
bool CAimbotGlobal::PlayerBoneInFOV(CCSPlayer* pTarget, Vec3 vLocalPos, Vec3 vLocalAngles, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo, int iHitboxes, float flFOVOverride)
{
	const float flFOV = flFOVOverride >= 0.f ? flFOVOverride : Vars::Aimbot::General::AimFOV.Value;

	// ---- BOUNDING-SPHERE REJECT, BEFORE ANY BONE WORK -------------------------------------
	// This runs for EVERY enemy on the server, every command, from all three target gathers
	// (hitscan/projectile/melee). SetupBones below is a full bone rebuild (see the mask note),
	// and on a target whose every hitbox is 60 degrees off-screen the whole rebuild is thrown
	// away. Bound the player by a SPHERE and ask whether the sphere's nearest edge is still
	// outside the FOV cone: a sphere, unlike the bbox, cannot be rotated out from under the
	// test, so nothing that would have passed the loop below can be rejected here.
	{
		const Vec3 vMins = pTarget->m_vecMins(), vMaxs = pTarget->m_vecMaxs();
		const Vec3 vCenter = pTarget->m_vecOrigin() + (vMins + vMaxs) / 2;
		const float flRadius = (vMaxs - vMins).Length() / 2 * k_fovRejectScale + k_fovRejectPad;
		const float flDist = vLocalPos.DistTo(vCenter);
		const float flFovToCenter = Math::CalcFov(vLocalAngles, Math::CalcAngle(vLocalPos, vCenter));
		if (aimfov::SphereOutsideFov(flFovToCenter, flDist, flRadius, flFOV))
			return false;
	}

	// static, not a local: matrix3x4[MAXSTUDIOBONES] is 6144 bytes of stack per call and this
	// is called once per enemy per command. Single-threaded (CreateMove), consumed before return
	static matrix3x4 aBones[MAXSTUDIOBONES];
	// ---- BONE_USED_BY_HITBOX, NOT BONE_USED_BY_ANYTHING ---------------------------------
	// c_baseanimating.cpp:2911 hits the bone cache only when
	// `(GetReadableBones() & boneMask) == boneMask`. BONE_USED_BY_ANYTHING is 0x0007FF00, all
	// eleven "used by" bits, so it is NEVER a cache hit: every call was a full rebuild
	// (StandardBlendingRules + CalculateIKLocks + BuildTransformations).
	// Worse, c_baseanimating.cpp:2904 does `m_iAccumulatedBoneMask |= boneMask` and the next
	// frame does `boneMask |= m_iPrevBoneMask` -- so asking for ANYTHING once upgraded the
	// GAME'S OWN render-path SetupBones for that player to full-bone setup, permanently. That
	// cost frames nowhere near the aim path.
	// BONE_USED_BY_HITBOX (0x100, studio.h:415) is a subset the renderer already builds for any
	// player on screen, and it is exactly what every read here needs: every consumer of this
	// array indexes it as aBones[pBox->bone], hitbox bones and nothing else.
	if (!pTarget->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		return false;

	// ---- THE HITBOX SET, RESOLVED ONCE --------------------------------------------------
	// GetNumOfHitboxes() was the loop CONDITION and GetHitboxCenter() ran inside the body, and
	// each of them re-walks GetModel() -> GetStudiomodel() -> pHitboxSet() -> pHitbox()
	// (defs.cpp:7-70). Two of those are virtual calls into the engine and GetStudiomodel goes
	// through the MDL cache. ~18 hitboxes x ~7 lookups x every enemy = thousands per command,
	// for a pointer that cannot change inside one loop. Resolve it once, exactly as
	// CAimbotHitscan::CanHit already does (aim_hitscan.cpp, its own hitbox loop).
	auto pModel = pTarget->GetModel();
	if (!pModel) return false;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return false;
	auto pSet = pHDR->pHitboxSet(pTarget->m_nHitboxSet());
	if (!pSet) return false;

	float flMinFOV = 180.f;
	for (int nHitbox = 0; nHitbox < pSet->numhitboxes; nHitbox++)
	{
		if (!IsHitboxValid(pTarget, nHitbox, iHitboxes))
			continue;

		auto pBox = pSet->pHitbox(nHitbox);
		if (!pBox)
			continue;

		Vec3 vCurPos; Math::VectorTransform((pBox->bbmin + pBox->bbmax) / 2, aBones[pBox->bone], vCurPos);
		Vec3 vCurAngleTo = Math::CalcAngle(vLocalPos, vCurPos);
		float flCurFOVTo = Math::CalcFov(vLocalAngles, vCurAngleTo);

		if (flCurFOVTo < flMinFOV)
		{
			vPos = vCurPos;
			vAngleTo = vCurAngleTo;
			flFOVTo = flMinFOV = flCurFOVTo;
		}
	}

	return flMinFOV < flFOV;
}

// iHitboxes = botox hitbox groups (Hitscan::HitboxGroupsEnum). botox enabled_hitboxes() scans one
// box per group: head, neck, chest, stomach, pelvis, upper arms, thighs -- forearms/hands/calves/
// feet are never aim points. CSS set 0 via GetHitboxToBase: SPINE0 = spine1 (stomach), SPINE2 =
// spine2 (chest), SPINE3 = neck (defs.cpp).
bool CAimbotGlobal::IsHitboxValid(CBaseEntity* pEntity, int nHitbox, int iHitboxes)
{
	using namespace Vars::Aimbot::Hitscan;
	switch (pEntity->GetHitboxToBase(nHitbox))
	{
	case HITBOX_HEAD: return iHitboxes & HitboxGroupsEnum::Head;
	case HITBOX_SPINE3: return iHitboxes & HitboxGroupsEnum::Neck;
	case HITBOX_SPINE2: return iHitboxes & HitboxGroupsEnum::Chest;
	case HITBOX_SPINE0: return iHitboxes & HitboxGroupsEnum::Stomach;
	case HITBOX_PELVIS: return iHitboxes & HitboxGroupsEnum::Pelvis;
	case HITBOX_LEFT_UPPERARM:
	case HITBOX_RIGHT_UPPERARM: return iHitboxes & HitboxGroupsEnum::Arms;
	case HITBOX_LEFT_THIGH:
	case HITBOX_RIGHT_THIGH: return iHitboxes & HitboxGroupsEnum::Legs;
	}

	return false;
}

bool CAimbotGlobal::ShouldMultipoint(CBaseEntity* pEntity, int nHitbox, int iHitboxes)
{
	if (Vars::Aimbot::Hitscan::MultipointScale.Value <= 0.f)
		return false;

	if (!iHitboxes)
		return true;

	// Hitscan::MultipointHitboxes bits: Head, Body, Pelvis, Arms, Legs (vars.h VA_LIST order)
	enum { kHead = 1 << 0, kBody = 1 << 1, kPelvis = 1 << 2, kArms = 1 << 3, kLegs = 1 << 4 };
	switch (pEntity->GetHitboxToBase(nHitbox))
	{
	case HITBOX_HEAD: return iHitboxes & kHead;
	case HITBOX_SPINE0:
	case HITBOX_SPINE1:
	case HITBOX_SPINE2:
	case HITBOX_SPINE3: return iHitboxes & kBody;
	case HITBOX_PELVIS: return iHitboxes & kPelvis;
	case HITBOX_LEFT_UPPERARM:
	case HITBOX_LEFT_FOREARM:
	case HITBOX_LEFT_HAND:
	case HITBOX_RIGHT_UPPERARM:
	case HITBOX_RIGHT_FOREARM:
	case HITBOX_RIGHT_HAND: return iHitboxes & kArms;
	case HITBOX_LEFT_THIGH:
	case HITBOX_LEFT_CALF:
	case HITBOX_LEFT_FOOT:
	case HITBOX_RIGHT_THIGH:
	case HITBOX_RIGHT_CALF:
	case HITBOX_RIGHT_FOOT: return iHitboxes & kLegs;
	}

	return false;
}

// CS:GO/CSS botox aimbot targets players only -- no buildings (CSS has none), no stickies/NPCs/
// bombs (TF2-only target types, deleted from Vars::Aimbot::General::Target/Ignore -- see vars.h).
// GetClassID()/ETFClassID (TF2's DT class-index netprop scheme) has no CSS counterpart at all
// (CSS sends no m_iClassID), so classification is just CBaseEntity::IsPlayer(). Every TF2
// condition check (invuln/invis/feign-death/taunt/disguise/vaccinator-resist) is deleted: CSS has
// no condition system (confirmed absent from cs_player_shared.h -- no m_nPlayerCond field).
bool CAimbotGlobal::ShouldIgnore(CBaseEntity* pEntity, CCSPlayer* pLocal, CWeaponCSBase* pWeapon, int iIgnoreOverride)
{
	(void)pWeapon; (void)iIgnoreOverride;   // no CSS-specific per-weapon ignore left (backstab ignore deleted with the knife aimbot)

	if (!pEntity || pEntity->IsDormant() || !pEntity->IsPlayer())
		return true;

	auto pPlayer = pEntity->As<CCSPlayer>();
	if (pPlayer == pLocal || !pPlayer->IsAlive())
		return true;

	// botox entity.cpp:251 is_valid_aim_target: target_team || is_enemy. mp_friendlyfire plays no
	// part (it let teammates through with target team OFF on ff servers).
	if (!Vars::Aimbot::General::TargetTeam.Value && pLocal->m_iTeamNum() == pEntity->m_iTeamNum())
		return true;

	// botox misc.cpp player_list_aim_allowed (menu_tab_settings.cpp player_list_page's "aimbot
	// ignore"/"only aimbot" columns) -- the single choke point every target passes through, so
	// one check here covers aimbot.cpp/aim_hitscan.cpp/grenade_aim.cpp/auto_wall.cpp alike, same
	// as botox routes all 3 callers through player_list_aim_allowed instead of its own copy each.
	if (!Names_AimAllowed(pEntity->entindex()))
		return true;

	return false;
}

int CAimbotGlobal::GetPriority(int iIndex)
{
	return 0; // every target has the same priority
}

bool CAimbotGlobal::FriendlyFire()
{
	static auto mp_friendlyfire = H::ConVars.FindVar("mp_friendlyfire");
	return mp_friendlyfire->GetBool();
}

// botox: an aim mode that actually MOVES THE CAMERA (camlock, or visible aim with no smoothing --
// botox's old "Plain") only computes a target while about to fire; otherwise it's a snap to a
// target with nothing fired, the single most obvious cheat tell there is. Smoothed visible
// tracking and pure silent aim (camera never moves, see aim_hitscan.cpp's silent branch) are fine
// running every tick. Replaces the old General::AimType switch (Off/Plain/Smooth/Silent/Locking/
// Assistive) -- that enum predated the CS:GO 1:1 port's Silent/Camlock bools and had drifted out
// of sync with them (AimType stuck at its default "Off" let Camlock snap the camera unconditionally,
// 24/7, regardless of firing -- a real bug, not just dead code). AimType/AssistStrength removed
// from vars.h, both had zero other readers.
// flSmooth = the held class's smooth (WeaponSettings), not General::Smooth: a class override
// with smooth 1 snapped the camera every tick while general sat at 5.
bool CAimbotGlobal::ShouldAim(float flSmooth)
{
	const bool bCamSnap = Vars::Aimbot::General::Camlock.Value
		|| (!Vars::Aimbot::General::Silent.Value && flSmooth <= 1.f);

	if (bCamSnap && !G::CanPrimaryAttack && !G::Reloading && !F::Ticks.IsTimingUnsure())
		return false;

	return true;
}

// botox aimbot.cpp:118 scale_fov_with_scope. CSS has no per-player default-FOV netvar (CS:GO/CS2
// FOV-changing item feature, confirmed absent from css.h) -- base is always the server default, 90.
float CAimbotGlobal::ScaleFovWithScope(float flFOV)
{
	auto pLocal = H::Entities.GetLocal();
	if (flFOV <= 0.f || !Vars::Aimbot::General::ScaleFovWithScope.Value || !pLocal)
		return flFOV;

	constexpr float flBase = 90.f;
	const float flView = float(pLocal->m_iFOV());
	if (flView <= 0.f || flView >= flBase)
		return flFOV;

	const float flRatio = tanf(DEG2RAD(flView * 0.5f)) / tanf(DEG2RAD(flBase * 0.5f));
	return RAD2DEG(atanf(tanf(DEG2RAD(std::min(flFOV, 89.f))) * flRatio));
}

// ValidBomb (TF2 sticky/pumpkin-bomb detonate-on-nearby-enemy check) DELETED: CSS's planted C4
// is not an aimbot target (no detonate-near-enemy mechanic on it at all) and CSS has no
// pipebomb/pumpkin-bomb equivalent. No caller remains (ShouldIgnore above no longer has a
// Bombs case).

// botox aimbot.cpp:53-146 + :162-193 (weapon_class_vars / weapon_settings / enabled_hitboxes).
CAimbotGlobal::WeaponSettings_t CAimbotGlobal::WeaponSettings(int nWeaponID)
{
	using namespace Vars::Aimbot;
	struct Class_t { ConfigVar<bool>* m_pOverride; ConfigVar<float>* m_pFOV, *m_pSmooth, *m_pRCS; ConfigVar<int>* m_pHitboxes; bool m_bSniper; };
	static const Class_t aPistol{ &WeaponClass::PistolOverride, &WeaponClass::PistolFOV, &WeaponClass::PistolSmooth, &WeaponClass::PistolRCS, &WeaponClass::PistolHitboxes, false };
	static const Class_t aHeavyPistol{ &WeaponClass::HeavyPistolOverride, &WeaponClass::HeavyPistolFOV, &WeaponClass::HeavyPistolSmooth, &WeaponClass::HeavyPistolRCS, &WeaponClass::HeavyPistolHitboxes, false };
	static const Class_t aSMG{ &WeaponClass::SMGOverride, &WeaponClass::SMGFOV, &WeaponClass::SMGSmooth, &WeaponClass::SMGRCS, &WeaponClass::SMGHitboxes, false };
	static const Class_t aRifle{ &WeaponClass::RifleOverride, &WeaponClass::RifleFOV, &WeaponClass::RifleSmooth, &WeaponClass::RifleRCS, &WeaponClass::RifleHitboxes, false };
	static const Class_t aSniper{ &WeaponClass::SniperOverride, &WeaponClass::SniperFOV, &WeaponClass::SniperSmooth, &WeaponClass::SniperRCS, &WeaponClass::SniperHitboxes, true };
	static const Class_t aScout{ &WeaponClass::ScoutOverride, &WeaponClass::ScoutFOV, &WeaponClass::ScoutSmooth, &WeaponClass::ScoutRCS, &WeaponClass::ScoutHitboxes, true };
	static const Class_t aHeavy{ &WeaponClass::HeavyOverride, &WeaponClass::HeavyFOV, &WeaponClass::HeavySmooth, &WeaponClass::HeavyRCS, &WeaponClass::HeavyHitboxes, false };

	const Class_t* pClass = nullptr;
	switch (CSWeaponClass(nWeaponID))
	{
	case ECSWeaponClass::Pistol: pClass = &aPistol; break;
	case ECSWeaponClass::HeavyPistol: pClass = &aHeavyPistol; break;
	case ECSWeaponClass::SMG: pClass = &aSMG; break;
	case ECSWeaponClass::Rifle: pClass = &aRifle; break;
	case ECSWeaponClass::Sniper: pClass = &aSniper; break;
	case ECSWeaponClass::Scout: pClass = &aScout; break;
	case ECSWeaponClass::Heavy: pClass = &aHeavy; break;
	default: return {};
	}

	WeaponSettings_t t;
	t.m_bSniper = pClass->m_bSniper;
	if (pClass->m_pOverride->Value)
	{
		t.m_flFOV = pClass->m_pFOV->Value; t.m_flSmooth = pClass->m_pSmooth->Value; t.m_flRCS = pClass->m_pRCS->Value;
		t.m_iHitboxGroups = pClass->m_pHitboxes->Value;
	}
	else
	{
		t.m_flFOV = General::AimFOV.Value; t.m_flSmooth = General::Smooth.Value; t.m_flRCS = RCS::General.Value;
		t.m_iHitboxGroups = Hitscan::HitboxGroups.Value;
	}
	if (!(t.m_iHitboxGroups & Hitscan::HitboxGroupsEnum::All))   // botox: never scan nothing
		t.m_iHitboxGroups = Hitscan::HitboxGroupsEnum::Head;
	return t;
}
