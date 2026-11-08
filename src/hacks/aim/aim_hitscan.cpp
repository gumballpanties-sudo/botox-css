#include "aim_hitscan.h"

#include "aimbot.h"
#include "aim_rcs.h"
#include "auto_wall.h"
#include "css_weapons.h"
#include "nospread_hitscan.h"
#include "ticks.h"
#include "backtrack.h"
#include "aim_pick.h"
#include <algorithm>
#include <cmath>
#include <span>

void DebugLog(const char* fmt, ...);

// botox csgo-sdk hacks/aimbot/aimbot.cpp ported 1:1 onto this unit's existing multi-record FOV
// scan (CanHit/GetTargets kept -- engine-generic bone/record machinery, not TF2 logic). What
// changed from the TF2 build: every TF_WEAPON_*/TF_COND_* branch (minigun spin-up, medigun heal
// target, sniper charge/headshot math, revolver Ambassador, laser pointer, buildings/stickies/
// NPCs/bombs, jarate) is gone -- CS:GO botox's aimbot has none of that, and neither does this one
// any more. In its place: per-weapon-CLASS FOV/smooth/RCS (css_weapons.h + vars.h WeaponClass),
// camlock/silent/visible-smooth (aim_rcs.h), autowall gate, auto-scope for scoped weapons.
//
// No zeus: CSS has no taser/zeus x27 at all (confirmed absent from cs_weapon_parse.h) -- botox's
// run_zeusbug has nothing to port to and was not ported. Said here per user direction.

std::vector<Target_t> CAimbotHitscan::GetTargets(CCSPlayer* pLocal, CWeaponCSBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	// this tick's predicted eye + where the bullets go (view + comp, Run sets m_vComp first), same
	// frame CanHit ranks in. F::Ticks.GetShootPos is saved after the aimbot = last tick's eye.
	Vec3 vLocalPos = pLocal->GetShootPos();
	Vec3 vLocalAngles = G::CurrentUserCmd->viewangles + m_vComp;

	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players))
		return vTargets;

	// botox m_aimbot_target_team: pull teammates into the gather too (ShouldIgnore below still
	// runs, it's the actual gate -- this just has to stop excluding them before that point).
	auto eGroupType = Vars::Aimbot::General::TargetTeam.Value ? EntityEnum::PlayerAll : EntityEnum::PlayerEnemy;

	for (auto pEntity : H::Entities.GetGroup(eGroupType))
	{
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
			continue;

		float flFOVTo; Vec3 vPos, vAngleTo;
		// botox gates every record on its own cosine: a usable record inside the fov keeps
		// a target whose live body is outside it
		if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CCSPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo, m_iHitboxGroups, m_flFOV)
			&& !F::Backtrack.RecordInFOV(pEntity, vLocalPos, vLocalAngles, m_flFOV, flFOVTo, vPos, vAngleTo))
			continue;

		int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());

		vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, 0.f, iPriority);
	}

	return vTargets;
}

std::vector<Target_t> CAimbotHitscan::SortTargets(CCSPlayer* pLocal, CWeaponCSBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	// target selection is always FOV now.
	F::AimbotGlobal.SortTargets(vTargets, Vars::Aimbot::General::TargetSelectionEnum::FOV);
	// no max targets: one player is aimed (Run's lock), the rest are fallbacks walked in fov order
	// until one can be hit -- botox scan_targets scans every player the same way
	F::AimbotGlobal.SortPriority(vTargets);
	return vTargets;
}

float CAimbotHitscan::HitChance(const mstudiohitboxset_t* pSet, const matrix3x4* aBones, float flModelScale, const Vec3& vPoint)
{
	Vec3 vForward, vRight, vUp;
	Math::AngleVectors(Math::CalcAngle(m_vEyePos, vPoint), &vForward, &vRight, &vUp);
	return aimpick::HitChance(m_flInaccuracy, m_flSpread, [&](float x, float y)
		{
			// FX_FireBullets: dir = forward + x * right + y * up, any hitbox of the body counts
			const Vec3 vDir = vForward + vRight * x + vUp * y;
			for (int i = 0; i < pSet->numhitboxes; i++)
			{
				auto pBox = pSet->pHitbox(i);
				if (pBox && Math::RayToOBB(m_vEyePos, vDir, pBox->bbmin, pBox->bbmax, aBones[pBox->bone], flModelScale))
					return true;
			}
			return false;
		});
}

int CAimbotHitscan::CanHit(Target_t& tTarget, CCSPlayer* pLocal, CWeaponCSBase* pWeapon)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	m_vEyePos = pLocal->GetShootPos();
	// CWeaponCSBase has no GetRange() vfunc/netvar -- m_flRange is script data (css_weapons.h),
	// not a live getter.
	auto pRangeData = CSWeaponScript(pWeapon->GetWeaponID());
	const float flMaxRange = powf(pRangeData ? pRangeData->m_flRange : 8192.f, 2.f);

	auto pModel = tTarget.m_pEntity->GetModel();
	if (!pModel) return false;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return false;
	auto pSet = pHDR->pHitboxSet(tTarget.m_pEntity->As<CBaseAnimating>()->m_nHitboxSet());
	if (!pSet) return false;

	// No tick history: one record, the target's live bones.
	g_tLiveRecord.m_flSimTime = tTarget.m_pEntity->m_flSimulationTime();
	g_tLiveRecord.m_vOrigin = tTarget.m_pEntity->m_vecOrigin();
	g_tLiveRecord.m_vMins = g_tLiveRecord.m_vMaxs = Vec3();
	g_tLiveRecord.m_bOnShot = g_tLiveRecord.m_bInvalid = false;
	if (!tTarget.m_pEntity->SetupBones(g_tLiveRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, tTarget.m_pEntity->m_flSimulationTime()))
		return false;

	static TickRecord* vRecordBuf[1 + CBacktrack::kMaxRecords];
	std::span<TickRecord*> vRecords;
	{
		int nRecords = 0;
		if (m_iBtPass == 0)
			vRecordBuf[nRecords++] = &g_tLiveRecord;
		if (tTarget.m_iTargetType == TargetEnum::Player && (m_iBtPass == 1 || F::Backtrack.AimAt()))
		{
			if (auto pList = F::Backtrack.GetRecords(tTarget.m_pEntity->entindex()))
			{
				for (int i = 0; i < F::Backtrack.Count(); i++)
					if (F::Backtrack.Usable(tTarget.m_pEntity, pList[i].m_flSimTime))
						vRecordBuf[nRecords++] = &pList[i];
			}
		}

		if (nRecords > 1)
		{
			const Vec3 vView = G::CurrentUserCmd->viewangles, vHalf = (tTarget.m_pEntity->m_vecMins() + tTarget.m_pEntity->m_vecMaxs()) / 2;
			auto Fov = [&](const TickRecord* p) { return Math::CalcFov(vView, Math::CalcAngle(m_vEyePos, p->m_vOrigin + vHalf)); };
			std::sort(vRecordBuf, vRecordBuf + nRecords, [&](const TickRecord* a, const TickRecord* b) { return Fov(a) < Fov(b); });
		}
		vRecords = { vRecordBuf, size_t(nRecords) };
	}
	if (vRecords.empty())
		return false;

	bool bPeekCheck = false;
	if (Vars::Aimbot::Hitscan::PeekAmount.Value && pWeapon->HasSpread())
	{
		switch (Vars::Aimbot::Hitscan::PeekCheck.Value)
		{
		case Vars::Aimbot::Hitscan::PeekCheckEnum::Off: break;
		case Vars::Aimbot::Hitscan::PeekCheckEnum::DoubletapOnly: bPeekCheck = F::Ticks.GetTicks(pWeapon); break;
		case Vars::Aimbot::Hitscan::PeekCheckEnum::Always: bPeekCheck = true; break;
		}
	}
	Vec3 vPeekPos = bPeekCheck ? m_vEyePos + pLocal->m_vecVelocity() * TICKS_TO_TIME(-Vars::Aimbot::Hitscan::PeekAmount.Value) : Vec3();

	const bool bAutowall = Vars::Aimbot::General::Autowall.Value;
	const int iMinDamage = Vars::Aimbot::General::AutowallMinDamage.Value;

	if (tTarget.m_iTargetType != TargetEnum::Player)
		return false;

	const float flModelScale = tTarget.m_pEntity->As<CBaseAnimating>()->m_flModelScale();
	const float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

	// CSS player hull (view_vectors) -- no per-game-rules override known yet for CS (botox/TF2
	// read it off TFGameRules()); using the stock CS hull (-16,-16,0 / 16,16,72, source-sdk-2013
	// game/shared/cstrike/cs_shareddefs.h VEC_HULL_MIN/MAX) as a fixed fallback. UNVERIFIED css:
	// confirm against cs_gamemovement.cpp if this port needs exact crouch-hull precision.
	const Vec3 vHullMins = Vec3(-16.f, -16.f, 0.f) * flModelScale;
	const Vec3 vHullMaxs = Vec3(16.f, 16.f, 72.f) * flModelScale;

	// botox scan_targets(): every enabled hitbox is a candidate, ranked by angle off where the
	// bullets go (view + comp), NOT a fixed head-first order -- that flicked head <-> chest every
	// time the head's visibility blinked. Wide cone = the hit-chance pick (aim_pick.h).
	const Vec3 vBullet = G::CurrentUserCmd->viewangles + m_vComp;
	const bool bWide = aimpick::WideCone(m_flInaccuracy, m_flSpread);
	const bool bLocked = tTarget.m_pEntity->entindex() == m_iLock;

	struct Cand_t { const mstudiobbox_t* m_pBox; int m_nHitbox; Vec3 m_vPos; float m_flFOV; float m_flBoneScale; };
	static std::vector<Cand_t> vCands;   // CreateMove only, reused so the scan never allocates

	int iReturn = false;
	for (auto pRecord : vRecords)
	{
		auto aBones = pRecord->m_aBones;
		if (!aBones)
			continue;

		vCands.clear();
		for (int nHitbox = 0; nHitbox < pSet->numhitboxes; nHitbox++)
		{
			if (!F::AimbotGlobal.IsHitboxValid(tTarget.m_pEntity, nHitbox, m_iHitboxGroups))
				continue;

			auto pBox = pSet->pHitbox(nHitbox);
			if (!pBox) continue;

			const Vec3 vMins = pBox->bbmin, vMaxs = pBox->bbmax;
			Vec3 vOffset;
			{
				Vec3 vOrigin, vCenter;
				Math::VectorTransform({}, aBones[pBox->bone], vOrigin);
				Math::VectorTransform((vMins + vMaxs) / 2, aBones[pBox->bone], vCenter);
				vOffset = vCenter - vOrigin;
			}

			float flBoneScale = Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value;
			std::vector<Vec3> vPoints = { Vec3() };
			if (F::AimbotGlobal.ShouldMultipoint(tTarget.m_pEntity, nHitbox, Vars::Aimbot::Hitscan::MultipointHitboxes.Value))
			{
				flBoneScale = std::max(flBoneScale, Vars::Aimbot::Hitscan::MultipointScale.Value / 100.f);
				bool bTriggerbot = !Vars::Aimbot::General::Silent.Value && !Vars::Aimbot::General::Camlock.Value && m_flSmooth <= 1.f;

				if (!bTriggerbot)
				{
					float flScale = Vars::Aimbot::Hitscan::MultipointScale.Value / 100;
					Vec3 vMinsS = (vMins - vMaxs) / 2 * flScale;
					Vec3 vMaxsS = (vMaxs - vMins) / 2 * flScale;

					vPoints = {
						Vec3(),
						Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
						Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
						Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
						Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
						Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
						Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
						Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
						Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
					};
				}
			}

			for (auto& vPoint : vPoints)
			{
				Vec3 vOrigin; Math::VectorTransform(vPoint, aBones[pBox->bone], vOrigin); vOrigin += vOffset;
				if (m_vEyePos.DistToSqr(vOrigin) > flMaxRange)
					continue;
				vCands.push_back({ pBox, nHitbox, vOrigin, Math::CalcFov(vBullet, Math::CalcAngle(m_vEyePos, vOrigin)), flBoneScale });
			}
		}
		if (vCands.empty())
			continue;
		std::stable_sort(vCands.begin(), vCands.end(), [](const Cand_t& a, const Cand_t& b) { return a.m_flFOV < b.m_flFOV; });

		if (bPeekCheck && !SDK::VisPos(pLocal, tTarget.m_pEntity, vPeekPos, vCands.front().m_vPos))
			continue;

		// botox passes_gate(): autowall min-damage OR plain visibility, never both.
		auto Gate = [&](int i)
			{
				const auto& c = vCands[i];
				if (!bAutowall)
					return SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, c.m_vPos);
				const float flDamage = pRecord == &g_tLiveRecord ? F::AutoWall.GetDamage(pLocal, pWeapon, c.m_vPos)
					: F::AutoWall.GetRecordDamage(pLocal, pWeapon, c.m_vPos, tTarget.m_pEntity, c.m_pBox->group);
				return flDamage >= (float)iMinDamage;
			};
		float flHc = 1.f;
		const int iPick = aimpick::Pick(int(vCands.size()), bWide,
			[&](int i) { return HitChance(pSet, aBones, flModelScale, vCands[i].m_vPos); },
			Gate,
			[&](int i) { return bLocked && vCands[i].m_nHitbox == m_iLockHitbox; }, &flHc);
		if (iPick < 0)
			continue;
		const auto& c = vCands[iPick];

		Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, c.m_vPos), vAngles, m_flSmooth);
		Vec3 vForward; Math::AngleVectors(vAngles + m_vComp, &vForward);   // where the bullet goes, not the comped view
		const float flDist = m_vEyePos.DistTo(c.m_vPos);
		const Vec3 vCheckMins = (c.m_pBox->bbmin + flBoneSubtract / flModelScale) * c.m_flBoneScale;
		const Vec3 vCheckMaxs = (c.m_pBox->bbmax - flBoneSubtract / flModelScale) * c.m_flBoneScale;
		const matrix3x4 mTransform = { { 1, 0, 0, pRecord->m_vOrigin.x }, { 0, 1, 0, pRecord->m_vOrigin.y }, { 0, 0, 1, pRecord->m_vOrigin.z } };

		// instant/silent: the gate already saw the point. smoothed/stepped: fire only once the
		// moved crosshair is on the box itself.
		if ((!bChanged || Math::RayToOBB(m_vEyePos, vForward, vCheckMins, vCheckMaxs, aBones[c.m_pBox->bone], flModelScale) && SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, m_vEyePos + vForward * flDist))
			&& Math::RayToOBB(m_vEyePos, vForward, vHullMins, vHullMaxs, mTransform))
		{
			tTarget.m_vAngleTo = vAngles;
			tTarget.m_pRecord = pRecord;
			tTarget.m_vPos = c.m_vPos;
			tTarget.m_nAimedHitbox = c.m_nHitbox;
			m_flPickHc = flHc;
			return true;
		}
		if (bChanged && (iReturn != 2 || vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D()))
		{
			tTarget.m_vAngleTo = vAngles;
			tTarget.m_nAimedHitbox = c.m_nHitbox;
			m_flPickHc = flHc;
			iReturn = 2;
		}
	}

	return iReturn;
}

bool CAimbotHitscan::ShouldFire(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget)
{
	if (!Vars::Aimbot::General::AutoShoot.Value)
		return false;

	// semi-auto pistol still holding from the last shot: leave the trigger up this tick so
	// m_iShotsFired resets and the next tick can fire (weapon_csbase.cpp:578)
	if (pWeapon->IsPistol() && pLocal->m_iShotsFired() > 0)
		return false;

	// botox class_vars->m_sniper / is_scoped(): sniper-class weapons (AWP/SG550/G3SG1/Scout) can
	// be gated to only fire zoomed. CSS AWP/SG550/G3SG1/Scout all have unscoped fire too (unlike
	// CS:GO's "sniper_only_fire_zoomed" convar which some of theirs force) -- so this is a user
	// choice here (Modifiers::ScopedOnly), not an engine rule, same as botox's own ScopedOnly knob.
	if (CSWeaponIsSniperClass(pWeapon->GetWeaponID()))
	{
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ScopedOnly && !CSSIsScoped(pLocal))
			return false;
	}

	return true;
}

bool CAimbotHitscan::Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, float flSmooth)
{
	bool bReturn = false;
	vToAngle -= m_vComp;

	if (Vars::Aimbot::General::Camlock.Value)
		vOut = vToAngle;
	else if (Vars::Aimbot::General::Silent.Value)
	{
		vOut = vToAngle;

		// botox aimbot.cpp:1195 m_aimbot_silent_step: 0 = uncapped, >0 clamps the per-tick
		// pitch/yaw delta off the raw mouse view instead of letting the bullet land anywhere in
		// the FOV cone every tick.
		if (const float flStep = Vars::Aimbot::General::SilentStep.Value; flStep > 0.f)
		{
			const Vec3 vFull = vToAngle.DeltaAngle(vCurAngle);
			Vec3 vDelta = vFull;
			vDelta.x = std::clamp(vDelta.x, -flStep, flStep);
			vDelta.y = std::clamp(vDelta.y, -flStep, flStep);
			vOut = vCurAngle + vDelta;
			// clamped = the bullet is NOT on the point: CanHit must ray-check it like a smoothed
			// angle (was "unchanged" -> hull check failed -> no aim at all past the step)
			bReturn = vDelta.x != vFull.x || vDelta.y != vFull.y;
		}
	}
	else if (flSmooth <= 1.f)
		vOut = vToAngle;
	else
	{
		vOut = vCurAngle.LerpAngle(vToAngle, 1.f / flSmooth);
		bReturn = true;
	}

	Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAimbotHitscan::Aim(CUserCmd* pCmd, Vec3& vAngle, bool bCamlock, bool bSilent, float flSmooth)
{
	bool bUnsure = F::Ticks.IsTimingUnsure();

	if (bCamlock)
	{
		// botox camlock (salvia-style): camera AND bullets ride the target, no silent split.
		F::AimbotRCS.Camlock(pCmd, vAngle, flSmooth);
		F::AimbotRCS.m_bAimTick = true;
		return;
	}

	if (bSilent)
	{
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			G::SilentAngles = true;
			F::AimbotRCS.m_bAimTick = true;
		}
		return;
	}

	// visible, smoothed or instant
	F::AimbotRCS.m_bAimTick = true;
	pCmd->viewangles = vAngle;
	I::EngineClient->SetViewAngles(vAngle);
}

void CAimbotHitscan::Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();
	if (!CSWeaponIsGun(nWeaponID))
		return;

	// botox weapon_class_vars()/weapon_settings()/enabled_hitboxes(): per-class override falls back
	// to General. Every reader below (target gather, hitbox pick, smoothing) uses these members.
	const auto tSettings = F::AimbotGlobal.WeaponSettings(nWeaponID);
	const float flSmooth = tSettings.m_flSmooth, flRCS = tSettings.m_flRCS;

	if (!Vars::Aimbot::Hitscan::Enabled.Value || !F::AimbotGlobal.ShouldAim(flSmooth))
		return;
	m_flSmooth = flSmooth;
	m_iHitboxGroups = tSettings.m_iHitboxGroups;
	// botox aimbot.cpp:1085: sniper/scout unscoped = stomach + chest + pelvis only
	if (tSettings.m_bSniper && !CSSIsScoped(pLocal))
	{
		using namespace Vars::Aimbot::Hitscan;
		m_iHitboxGroups = HitboxGroupsEnum::Stomach | HitboxGroupsEnum::Chest | HitboxGroupsEnum::Pelvis;
	}

	// botox aimbot.cpp:118 scale_fov_with_scope -- wraps weapon_settings()'s fov before the
	// zero-check below, same order here.
	const float flFOV = m_flFOV = F::AimbotGlobal.ScaleFovWithScope(tSettings.m_flFOV);
	if (flFOV <= 0.f)
		return;

	// botox Modifiers::AutoScope: sniper-class weapon, not zoomed yet -> request the zoom, skip
	// this tick's aim (same as botox's attack2 request).
	if (CSWeaponIsSniperClass(nWeaponID) && Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoScope && !CSSIsScoped(pLocal))
	{
		pCmd->buttons |= IN_ATTACK2;
		return;
	}

	const bool bCamlock = Vars::Aimbot::General::Camlock.Value;
	const bool bSilent = Vars::Aimbot::General::Silent.Value && !bCamlock;

	// botox aimbot.cpp:1166 -- silent: the camera never carries comp, take ALL of it (bullet = view +
	// punch * scale); camlock/visible: the class RCS want, the rest is the player's. Before the
	// gather: botox's scorer gates fov off view + comp too (where the bullets go mid-spray).
	m_vComp = bSilent ? pLocal->m_vecPunchAngle() * CAimbotRCS::RecoilScale() : F::AimbotRCS.Offset(flRCS);
	m_vComp.z = 0.f;

	auto vTargets = SortTargets(pLocal, pWeapon);

	// botox scan_all() (aimbot.cpp:1098): the locked player is scanned first and kept while he can
	// be hit, so the aim never hops between two players near the crosshair; a lock that can't be
	// hit falls through to the next in fov order. botox :1036 m_aimbot_no_switch: hold him while he
	// is a valid target at all -- out of fov / behind a wall = no aim, never another player.
	bool bHold = false;
	if (m_iLock)
	{
		auto pLock = I::ClientEntityList->GetClientEntity(m_iLock);
		bHold = Vars::Aimbot::General::NoSwitch.Value && pLock && !F::AimbotGlobal.ShouldIgnore(pLock->As<CBaseEntity>(), pLocal, pWeapon);

		auto it = std::find_if(vTargets.begin(), vTargets.end(), [&](const Target_t& t) { return t.m_pEntity->entindex() == m_iLock; });
		if (it != vTargets.end())
			std::rotate(vTargets.begin(), it, it + 1);
		if (bHold)
			vTargets.resize(it != vTargets.end() ? 1 : 0);
	}
	if (vTargets.empty())
	{
		if (!bHold)
			ResetLock();
		return;
	}

	if (!G::AimTarget.m_iEntIndex)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	// botox weapon_cone(): this tick's real cone for the hit-chance pick, read the way
	// nospread_hitscan.cpp reads it (penalty step run then restored). Nospread on = no cone.
	m_flInaccuracy = m_flSpread = 0.f;
	if (!Vars::Aimbot::General::NoSpread.Value)
	{
		const float flOldPenalty = pWeapon->m_fAccuracyPenalty();
		pWeapon->UpdateAccuracyPenalty();
		const float flInaccuracy = pWeapon->GetInaccuracy(), flSpread = pWeapon->GetSpread();
		pWeapon->m_fAccuracyPenalty() = flOldPenalty;
		if (std::isfinite(flInaccuracy) && std::isfinite(flSpread))
			m_flInaccuracy = std::clamp(flInaccuracy, 0.f, 1.f), m_flSpread = std::clamp(flSpread, 0.f, 1.f);
	}

	// botox :1140 lock bookkeeping + its "AIM: lock" trace, on the target actually aimed at
	const auto Lock = [&](const Target_t& t)
		{
			if (const int i = t.m_pEntity->entindex(); i != m_iLock)
			{
				DebugLog("AIM: lock %d -> %d hb=%d silent=%d camlock=%d rec=%d cone=%.4f", m_iLock, i, t.m_nAimedHitbox,
					int(bSilent), int(bCamlock), int(t.m_pRecord && t.m_pRecord != &g_tLiveRecord), m_flInaccuracy + m_flSpread);
				m_iLock = i;
			}
			m_iLockHitbox = t.m_nAimedHitbox;
		};

	bool bDone = false;
	for (m_iBtPass = 0; m_iBtPass < 2 && !bDone; m_iBtPass++)
	{
		if (m_iBtPass == 1 && (!F::Backtrack.Enabled() || F::Backtrack.AimAt()))
			break;

		for (auto& tTarget : vTargets)
		{
			const auto iResult = CanHit(tTarget, pLocal, pWeapon);
			if (!iResult) continue;
			Lock(tTarget);
			if (iResult == 2)
			{
				G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
				Aim(pCmd, tTarget.m_vAngleTo, bCamlock, bSilent, flSmooth);
				bDone = true;
				break;
			}

			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			F::Backtrack.m_pTarget = tTarget.m_pRecord != &g_tLiveRecord ? tTarget.m_pRecord : nullptr;

			// comp already folded into m_vAngleTo by Aim() (m_vComp); standalone RCS (aim_rcs.cpp)
			// covers every other tick and skips this one (m_bAimTick).
			F::AimbotRCS.m_bAimBurst = true;

			if (ShouldFire(pLocal, pWeapon, pCmd, tTarget))
				pCmd->buttons |= IN_ATTACK;

			// Tapfire: hold fire past max range, same gate as botox -- CSS has no m_flLastFireTime
			// netvar (TF2-only), so "just fired" is read off m_flNextPrimaryAttack (CBaseCombatWeapon,
			// shared) being freshly pushed out instead.
			if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::Tapfire
				&& pWeapon->HasSpread()
				&& m_vEyePos.DistTo(tTarget.m_vPos) > Vars::Aimbot::Hitscan::TapfireDistance.Value)
			{
				auto pData = CSWeaponScript(pWeapon->GetWeaponID());
				const float flCycle = pData ? pData->m_flCycleTime : 0.15f;
				if (pWeapon->m_flNextPrimaryAttack() - I::GlobalVars->curtime > flCycle * 0.5f)
					pCmd->buttons &= ~IN_ATTACK;
			}

			G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
			if (G::Attacking == 1 && tTarget.m_iTargetType == TargetEnum::Player)
			{
				tTarget.m_bBacktrack = tTarget.m_pRecord && tTarget.m_pRecord != &g_tLiveRecord;
				if (F::Backtrack.Enabled())
					F::Backtrack.CommitShot(tTarget.m_bBacktrack ? tTarget.m_pRecord : nullptr, pCmd);
				// pairs with events.cpp "AIM: hurt": a shot with no hurt line after it = a miss
				DebugLog("AIM: shot ent=%d hb=%d hc=%.2f cone=%.4f rec=%d", tTarget.m_pEntity->entindex(), tTarget.m_nAimedHitbox,
					m_flPickHc, m_flInaccuracy + m_flSpread, int(tTarget.m_bBacktrack));
			}

			Aim(pCmd, tTarget.m_vAngleTo, bCamlock, bSilent, flSmooth);
			bDone = true;
			break;
		}
	}

	if (!bDone && !bHold)
		ResetLock();
}
