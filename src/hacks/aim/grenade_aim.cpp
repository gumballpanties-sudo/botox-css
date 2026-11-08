#include "grenade_aim.h"
#include "css_weapons.h"
#include "aim_record.h"
#include "grenade_math.h"

#include <algorithm>
#include <cmath>
#include <numbers>

void DebugLog(const char* fmt, ...);

namespace
{
	constexpr float k_launch_offset = 16.f;       // CSS ThrowGrenade: vecSrc + vForward * 16
	// basecsgrenade_projectile.h:54 GetGrenadeElasticity() / :52 GetGrenadeGravity() -- both
	// confirmed present in CSS source (css-community-master), byte-identical to CS:GO's. The
	// earlier "UNVERIFIED, no SetGravity override found" note was wrong: the override exists
	// under GetGrenadeGravity(), just not the exact string that was grepped for.
	constexpr float k_elasticity = 0.45f;
	constexpr float k_gravity_modifier = 0.4f;

	class WorldOnlyFilter : public ITraceFilter
	{
	public:
		CBaseEntity* m_pSkip = nullptr;
		CBaseEntity* m_pTarget = nullptr;
		bool ShouldHitEntity(IHandleEntity* pEnt, int) override
		{
			auto pEntity = reinterpret_cast<CBaseEntity*>(pEnt);
			return pEntity != m_pSkip && pEntity != m_pTarget;
		}
		TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
	};

	// True world-only trace (botox c_trace_filter_world): used only to FIND a wall, never to
	// walk the grenade's own path -- a standing player must never count as "the wall".
	class WorldOnlyRealFilter : public ITraceFilter
	{
	public:
		bool ShouldHitEntity(IHandleEntity*, int) override { return false; }
		TraceType_t GetTraceType() const override { return TRACE_WORLD_ONLY; }
	};

	float MaxFlightTime(const int iItemID)
	{
		const int iSlot = CSGrenadeSlot(iItemID);
		if (iSlot < 0)
			return 0.f;
		switch (iSlot)
		{
		case 0: return Vars::Aimbot::Grenade::Types.Value & Vars::Aimbot::Grenade::TypesEnum::Flash ? 1.5f : 0.f;
		case 1: return Vars::Aimbot::Grenade::Types.Value & Vars::Aimbot::Grenade::TypesEnum::HE ? 2.f : 0.f;
		case 2: return Vars::Aimbot::Grenade::Types.Value & Vars::Aimbot::Grenade::TypesEnum::Smoke ? 3.f : 0.f;
		default: return 0.f;
		}
	}

	// CSS ThrowGrenade pitch remap + speed (weapon_basecsgrenade.cpp:339-354): grenade_math.h.
	using GrenadeMath::ThrowPitchRemap;
	using GrenadeMath::ThrowSpeed;
	using GrenadeMath::ViewPitch;
	using GrenadeMath::NormPitch;

	inline GrenadeMath::V3 M3(const Vec3& v) { return { v.x, v.y, v.z }; }

	struct Throw_t
	{
		Vec3 m_vEye = {};
		Vec3 m_vInherit = {};   // CSS: 1.0x player velocity (CS:GO uses 1.25x)
		float m_flMaxTime = 0.f;
	};

	struct Motion_t
	{
		Vec3 m_vCenter = {};
		Vec3 m_vVelocity = {};
		float m_flLead = 0.f;
		float m_flGravity = 0.f;
		bool m_bAirborne = false;

		Vec3 At(const float t) const
		{
			const float tau = t + m_flLead;
			Vec3 vOut = m_vCenter + m_vVelocity * tau;
			if (m_bAirborne)
				vOut.z -= 0.5f * m_flGravity * tau * tau;
			return vOut;
		}
	};

	Motion_t TargetMotion(CBasePlayer* pEntity, const float flLead, const float flGravity)
	{
		Motion_t tOut = {};
		tOut.m_vCenter = pEntity->m_vecOrigin() + Vec3(0.f, 0.f, 36.f);
		tOut.m_vVelocity = pEntity->m_vecVelocity();
		tOut.m_flLead = flLead;
		tOut.m_flGravity = flGravity;
		tOut.m_bAirborne = !(pEntity->m_fFlags() & FL_ONGROUND);
		if (!tOut.m_bAirborne)
			tOut.m_vVelocity.z = 0.f;
		return tOut;
	}

	// Direct hit at time t: dir from GrenadeMath::DirectLaunch (gravity + 16u spawn, closed form),
	// flSpeed = what CSS actually throws along that dir (pitch-coupled, no remap: dir IS angThrow).
	// Returns need - game speed: 0 = a real throw lands on vTarget at t.
	float SolveMiss(const Throw_t& th, const Vec3& vTarget, const float t, const float flGravity, Vec3& vDir, float& flSpeed)
	{
		GrenadeMath::V3 vD{}; float flNeed = 0.f;
		if (!GrenadeMath::DirectLaunch(M3(th.m_vEye), M3(th.m_vInherit), M3(vTarget), t, flGravity, k_launch_offset, vD, flNeed))
			return 1e9f;
		vDir = Vec3(vD.x, vD.y, vD.z);
		flSpeed = ThrowSpeed(GrenadeMath::DirPitch(vD));
		return flNeed - flSpeed;
	}

	bool SolveDirection(const Throw_t& th, const Vec3& vTarget, const float t, const float flGravity, Vec3& vDir, float& flSpeed)
	{
		// bisection leaves a small residual; > 2 u/s off means FindRoots bracketed a 1e9 edge, not a root
		return std::fabs(SolveMiss(th, vTarget, t, flGravity, vDir, flSpeed)) < 2.f && flSpeed >= 15.f;
	}

	template <typename F>
	int FindRoots(const F& miss, const float flMaxTime, float out[2])
	{
		const float flStep = I::GlobalVars->interval_per_tick;
		if (flStep <= 0.f)
			return 0;

		int iCount = 0;
		float flLastT = flStep;
		float flLast = miss(flLastT);
		for (float t = flStep * 2.f; t <= flMaxTime && iCount < 2; t += flStep)
		{
			const float flNow = miss(t);
			if ((flNow > 0.f) != (flLast > 0.f))
			{
				float lo = flLastT, hi = t;
				for (int i = 0; i < 16; ++i)
				{
					const float mid = (lo + hi) * 0.5f;
					if ((miss(mid) > 0.f) == (flLast > 0.f)) lo = mid; else hi = mid;
				}
				out[iCount++] = (lo + hi) * 0.5f;
			}
			flLastT = t;
			flLast = flNow;
		}
		return iCount;
	}

	// miss(t) = speed the geometry needs - speed CSS throws along that dir. Big at small t (must
	// fly fast), negative once a slow lob fits; the sign changes are the low arc and the lob.
	int FlightTimes(const Throw_t& th, const Motion_t& tMotion, float out[2])
	{
		return FindRoots([&](const float t) {
			Vec3 vDir; float flSpeed;
			return SolveMiss(th, tMotion.At(t), t, tMotion.m_flGravity, vDir, flSpeed);
		}, th.m_flMaxTime, out);
	}

	// botox grenade_aim.cpp:302-360 trace_path, CSS-adapted. pWall == nullptr: direct/lob throw,
	// any mid-flight hit fails the solve (unchanged behaviour). pWall != nullptr: exactly one
	// bounce is allowed, and ONLY off a plane matching pWall's normal (dot > 0.99) -- any other
	// hit (world or player) still fails. After the bounce the last tick must land within
	// k_bounceSlop of vEnd or the solve is rejected (botox: a "bounce" that actually missed the
	// wall it was aimed at is not the shot the menu asked for).
	constexpr float k_bounceSlop = 16.f;

	Vec3 Reflect(const Vec3& vIn, const Vec3& vNormal)
	{
		Vec3 vOut = vIn - vNormal * (vIn.Dot(vNormal) * 2.f);
		if (std::fabs(vOut.x) < 0.1f) vOut.x = 0.f;
		if (std::fabs(vOut.y) < 0.1f) vOut.y = 0.f;
		if (std::fabs(vOut.z) < 0.1f) vOut.z = 0.f;
		return vOut;
	}

	bool TracePath(const Throw_t& th, const Vec3& vDir, const float flSpeed, const float flGravity, const float flTime, const Vec3& vEnd,
		CBaseEntity* pSkip, CBaseEntity* pTarget, std::array<Vec3, 220>& vPath, int& iCount, const GrenadeMath::Wall* pWall = nullptr)
	{
		const Vec3 vMins(-2.f, -2.f, -2.f), vMaxs(2.f, 2.f, 2.f);
		const float flStep = I::GlobalVars->interval_per_tick;

		WorldOnlyFilter tFilter; tFilter.m_pSkip = pSkip; tFilter.m_pTarget = pTarget;
		trace_t tTrace = {};

		Ray_t tRay; tRay.Init(th.m_vEye, th.m_vEye + vDir * 22.f, vMins, vMaxs);
		I::EngineTrace->TraceRay(tRay, MASK_SOLID, &tFilter, &tTrace);
		if (tTrace.fraction < 1.f)
			return false;

		Vec3 vPos = tTrace.endpos - vDir * 6.f;
		Vec3 vVel = vDir * flSpeed + th.m_vInherit;

		iCount = 0;
		vPath[iCount++] = vPos;

		const Vec3 vWallNormal = pWall ? Vec3(pWall->m_vNormal.x, pWall->m_vNormal.y, pWall->m_vNormal.z) : Vec3();
		bool bBounced = false;

		for (float t = flStep; t <= flTime && iCount < 216; t += flStep)
		{
			float vz = 0.f;
			const float flZMove = GrenadeMath::GravityStep(vVel.z, flGravity, flStep, vz);
			const Vec3 vMove(vVel.x * flStep, vVel.y * flStep, flZMove);
			vVel.z = vz;

			Ray_t tStep; tStep.Init(vPos, vPos + vMove, vMins, vMaxs);
			I::EngineTrace->TraceRay(tStep, MASK_SOLID, &tFilter, &tTrace);

			if (tTrace.fraction < 1.f)
			{
				if (!pWall || bBounced || tTrace.startsolid
					|| Vec3(tTrace.plane.normal.x, tTrace.plane.normal.y, tTrace.plane.normal.z).Dot(vWallNormal) < 0.99f)
					return false;

				bBounced = true;
				vPos = tTrace.endpos;
				vPath[iCount++] = vPos;
				vVel = Reflect(vVel, Vec3(tTrace.plane.normal.x, tTrace.plane.normal.y, tTrace.plane.normal.z)) * k_elasticity;

				Ray_t tRest; tRest.Init(vPos, vPos + vVel * ((1.f - tTrace.fraction) * flStep), vMins, vMaxs);
				I::EngineTrace->TraceRay(tRest, MASK_SOLID, &tFilter, &tTrace);
				if (tTrace.fraction < 1.f)
					return false;
			}

			vPos = tTrace.endpos;
			vPath[iCount++] = vPos;
		}

		if (pWall && (!bBounced || (vPos - vEnd).Length() > k_bounceSlop))
			return false;

		Ray_t tLast; tLast.Init(vPos, vEnd, vMins, vMaxs);
		I::EngineTrace->TraceRay(tLast, MASK_SOLID, &tFilter, &tTrace);
		if (tTrace.fraction < 1.f)
			return false;

		vPath[iCount++] = vEnd;
		return true;
	}

	// botox grenade_aim.cpp:192-220 find_walls, CSS-adapted: 12 flat rays from the target, world-
	// only (a player standing near the target must never be mistaken for a wall).
	constexpr int k_wallRays = 12;
	constexpr float k_wallRange = 512.f;
	constexpr float k_hull = 2.f;

	int FindWalls(const Vec3& vCenter, GrenadeMath::Wall out[k_wallRays])
	{
		WorldOnlyRealFilter tFilter;
		int iCount = 0;

		for (int i = 0; i < k_wallRays; ++i)
		{
			const float flYaw = 2.f * std::numbers::pi_v<float> *float(i) / float(k_wallRays);
			const Vec3 vDir(std::cos(flYaw), std::sin(flYaw), 0.f);

			trace_t tTrace = {};
			Ray_t tRay; tRay.Init(vCenter, vCenter + vDir * k_wallRange);
			I::EngineTrace->TraceRay(tRay, MASK_SOLID, &tFilter, &tTrace);

			if (tTrace.fraction >= 1.f || tTrace.startsolid || std::fabs(tTrace.plane.normal.z) > 0.05f)
				continue;

			Vec3 vNormal(tTrace.plane.normal.x, tTrace.plane.normal.y, 0.f);
			vNormal = vNormal / vNormal.Length();
			const float flDist = Vec3(tTrace.endpos.x, tTrace.endpos.y, tTrace.endpos.z).Dot(vNormal)
				+ k_hull * (std::fabs(vNormal.x) + std::fabs(vNormal.y));

			bool bSeen = false;
			for (int j = 0; j < iCount; ++j)
				if (out[j].m_vNormal.x * vNormal.x + out[j].m_vNormal.y * vNormal.y + out[j].m_vNormal.z * vNormal.z > 0.99f
					&& std::fabs(out[j].m_flDist - flDist) < 4.f)
				{
					bSeen = true;
					break;
				}

			if (!bSeen)
				out[iCount++] = { GrenadeMath::V3{ vNormal.x, vNormal.y, vNormal.z }, flDist };
		}

		return iCount;
	}

	// botox grenade_aim.cpp:267-276 bounce_times -- CSS-adapted residual (see grenade_math.h):
	// the implied launch speed from the solved geometry vs. CSS's pitch-coupled ThrowSpeed for
	// that same direction, instead of botox's fixed th.m_speed.
	int BounceTimes(const Throw_t& th, const GrenadeMath::Wall& tWall, const Motion_t& tMotion, const float flGravity, float out[2])
	{
		return FindRoots([&](const float t) {
			const GrenadeMath::V3 vEye{ th.m_vEye.x, th.m_vEye.y, th.m_vEye.z };
			const GrenadeMath::V3 vInherit{ th.m_vInherit.x, th.m_vInherit.y, th.m_vInherit.z };
			const Vec3 vTargetAt = tMotion.At(t);
			const GrenadeMath::V3 vTarget{ vTargetAt.x, vTargetAt.y, vTargetAt.z };

			GrenadeMath::V3 vVelocity{}; float flHitTime = 0.f;
			if (!GrenadeMath::BounceLaunch(vEye, vInherit, tWall, vTarget, t, flGravity, k_elasticity, k_launch_offset, vVelocity, flHitTime))
				return 1e9f;

			const GrenadeMath::V3 vLaunch = vVelocity - vInherit;
			const float flLen = GrenadeMath::Length(vLaunch);
			if (flLen < 1.f)
				return 1e9f;

			return flLen - ThrowSpeed(GrenadeMath::DirPitch(vLaunch * (1.f / flLen)));
		}, th.m_flMaxTime, out);
	}
}

// ---- Shared with esp.cpp's grenade-path draw (DrawGrenadePath/DrawProjectilePaths) -- same CSS
// throw formula and bounce physics as the solve above, no TF2 ProjSim involved. ----
// CSS CBaseCSGrenade::ThrowGrenade (weapon_basecsgrenade.cpp:340-372): dir AND spawn come from
// the REMAPPED pitch (AngleVectors(angThrow)), spawn hull-traced 16u out, vel = dir*speed + player vel.
void CssGrenadeThrow(const Vec3& vEye, const Vec3& vViewAngles, const Vec3& vPlayerVel, CBaseEntity* pSkip, Vec3& vOrigin, Vec3& vVel)
{
	const float flThrown = ThrowPitchRemap(vViewAngles.x);
	Vec3 vForward; Math::AngleVectors(Vec3(flThrown, vViewAngles.y, 0.f), &vForward, nullptr, nullptr);

	WorldOnlyFilter tFilter; tFilter.m_pSkip = pSkip;
	trace_t tTrace = {};
	Ray_t tRay; tRay.Init(vEye, vEye + vForward * k_launch_offset, Vec3(-2.f, -2.f, -2.f), Vec3(2.f, 2.f, 2.f));
	I::EngineTrace->TraceRay(tRay, MASK_SOLID, &tFilter, &tTrace);
	vOrigin = tTrace.endpos;
	vVel = vForward * ThrowSpeed(flThrown) + vPlayerVel;
}

// Forward-integrates a launched (or live) grenade, CSS PhysicsToss per tick: GravityStep
// (PhysicsAddGravityMove half step), trace, on hit CBaseCSGrenadeProjectile::ResolveFlyCollisionCustom
// (basecsgrenade_projectile.cpp:154-270): reflect (PhysicsClipVelocity backoff 2) * 0.45 (players
// * 0.3), under 30 u/s = stop, floor (n.z > 0.7) pushes the rest of the tick on the new velocity.
// Ends when the grenade stops (smoke detonates there; HE/flash sit there until their fuse).
void CssGrenadeBouncePath(Vec3 vOrigin, Vec3 vVel, const float flGravity, const float flElasticity, const float flMaxTime,
	CBaseEntity* pSkip, std::vector<Vec3>& outPath, std::vector<bool>& outBounce, bool& outDetonated, Vec3& outDetonatePos)
{
	outPath.clear(); outBounce.clear(); outDetonated = false;
	outPath.push_back(vOrigin);

	WorldOnlyFilter tFilter; tFilter.m_pSkip = pSkip;
	const Vec3 vMins(-2.f, -2.f, -2.f), vMaxs(2.f, 2.f, 2.f);
	const float flStep = (I::GlobalVars->interval_per_tick > 0.f) ? I::GlobalVars->interval_per_tick : (1.f / 66.f);

	for (float t = 0.f; t < flMaxTime && outPath.size() < 220; t += flStep)
	{
		if (vVel.Length() < 0.1f) { outDetonated = true; outDetonatePos = vOrigin; break; }

		float vz = 0.f;
		const float flZMove = GrenadeMath::GravityStep(vVel.z, flGravity, flStep, vz);
		const Vec3 vEnd = vOrigin + Vec3(vVel.x * flStep, vVel.y * flStep, flZMove);
		vVel.z = vz;

		trace_t tTrace = {};
		Ray_t tRay; tRay.Init(vOrigin, vEnd, vMins, vMaxs);
		I::EngineTrace->TraceRay(tRay, MASK_SOLID, &tFilter, &tTrace);

		if (tTrace.fraction >= 1.f)
		{
			vOrigin = vEnd;
			outPath.push_back(vOrigin); outBounce.push_back(false);
			continue;
		}

		vOrigin = tTrace.endpos;
		const Vec3 n(tTrace.plane.normal.x, tTrace.plane.normal.y, tTrace.plane.normal.z);
		const float flSurface = (tTrace.m_pEnt && tTrace.m_pEnt->IsPlayer()) ? 0.3f : 1.f;
		const float flTotal = std::clamp(flElasticity * flSurface, 0.f, 0.9f);
		vVel = Reflect(vVel, n) * flTotal;
		const bool bSlow = vVel.Dot(vVel) < 30.f * 30.f;

		if (n.z > 0.7f && !bSlow)
		{
			trace_t tRest = {};
			Ray_t tRayRest; tRayRest.Init(vOrigin, vOrigin + vVel * ((1.f - tTrace.fraction) * flStep), vMins, vMaxs);
			I::EngineTrace->TraceRay(tRayRest, MASK_SOLID, &tFilter, &tRest);
			vOrigin = tRest.endpos;
		}
		else if (bSlow)
			vVel = Vec3();

		outPath.push_back(vOrigin); outBounce.push_back(true);
	}
}

void CGrenadeAim::OnCreateMove(CCSPlayer* pLocal, CUserCmd* pCmd)
{
	m_bThrow = false;
	m_iPathCount = 0;

	if (!pLocal)
		return;

	auto pWeapon = pLocal->m_hActiveWeapon().Get() ? pLocal->m_hActiveWeapon()->As<CWeaponCSBase>() : nullptr;
	const int iItemID = pWeapon ? pWeapon->GetWeaponID() : 0;
	const float flMaxTime = MaxFlightTime(iItemID);

	if (!Vars::Aimbot::Grenade::Enabled.Value || flMaxTime <= 0.f)
	{
		m_bAutoReleased = false;
		m_flReleaseTime = 0.f;
		return;
	}

	// NEEDS FROM OTHERS (SDK unit, css.h): a typed CBaseCSGrenade accessor for m_bPinPulled.
	// Stopgap per AGENT_BRIEF: local netvar read via the existing netvar helper.
	static const unsigned int iPinOffset = U::NetVars.GetNetVar("CBaseCSGrenade", "m_bPinPulled");
	const bool bPinPulled = iPinOffset && pWeapon ? *reinterpret_cast<bool*>(uintptr_t(pWeapon) + iPinOffset) : false;
	const float flCurTime = I::GlobalVars->curtime;

	if (m_flReleaseTime > 0.f && flCurTime - m_flReleaseTime > 0.5f)
	{
		m_flReleaseTime = 0.f;
		m_bAutoReleased = false;
	}

	if (!bPinPulled && m_flReleaseTime <= 0.f)
	{
		m_bAutoReleased = false;
		return;
	}

	// CSS StartGrenadeThrow sets m_fThrowTime = curtime + 0.1f (weapon_basecsgrenade.cpp:302/326,
	// verified) -- m_fThrowTime itself is not a prediction field here either, so mirror it off the
	// predicted pin the same way botox mirrors CS:GO's.
	const bool bThrowTick = m_flReleaseTime > 0.f && m_flReleaseTime < flCurTime;

	Aim(pLocal, pWeapon, iItemID, flMaxTime, bPinPulled, bThrowTick, pCmd);

	if (bThrowTick)
		m_flReleaseTime = 0.f;
	else if (bPinPulled && m_flReleaseTime <= 0.f && !(pCmd->buttons & (IN_ATTACK | IN_ATTACK2)))
		m_flReleaseTime = flCurTime + 0.1f;
}

void CGrenadeAim::Aim(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const int iItemID, const float flMaxTime, const bool bPinPulled,
	const bool bThrowTick, CUserCmd* pCmd)
{
	// Same hold/hotkey/toggle shape as CAimbot::KeyOK (aimbot.cpp) -- own latch, own key.
	const int iKey = Vars::Aimbot::Grenade::Key.Value;
	const int iMode = Vars::Aimbot::Grenade::KeyMode.Value;
	bool bKey;
	if (iMode != Vars::Aimbot::Grenade::KeyModeEnum::OnHotkey && iMode != Vars::Aimbot::Grenade::KeyModeEnum::Toggle)
		bKey = true; // always on
	else if (!iKey)
		bKey = true; // a mode with no key bound would be an off switch nobody can turn back on
	else
	{
		const bool bDown = (GetAsyncKeyState(iKey) & 0x8000) != 0;
		if (iMode == Vars::Aimbot::Grenade::KeyModeEnum::OnHotkey)
			bKey = bDown;
		else
		{
			static bool s_bWasDown = false;
			if (bDown && !s_bWasDown)
				m_bToggled = !m_bToggled;
			s_bWasDown = bDown;
			bKey = m_bToggled;
		}
	}
	if (!bKey && !m_bAutoReleased)
	{
		m_iLock = 0;
		return;
	}

	Throw_t th = {};
	th.m_vEye = pLocal->m_vecOrigin() + pLocal->m_vecViewOffset();
	th.m_vInherit = pLocal->m_vecVelocity();   // CSS: 1.0x, see header
	th.m_flMaxTime = flMaxTime;

	static const auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flGravity = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * k_gravity_modifier;

	const float flLead = AimLatency(FLOW_OUTGOING) + BacktrackLerp();
	const float flFovLimit = Vars::Aimbot::Grenade::FOV.Value;
	const int iArc = Vars::Aimbot::Grenade::Arc.Value;

	struct Best_t
	{
		CBaseEntity* m_pEntity = nullptr;
		float m_flFov = 1e9f;
		float m_flTime = 0.f;
		int m_iRoot = 0;
		Vec3 m_vFinal = {};
	} tBest = {};

	std::array<Vec3, 220> vScratch = {};

	for (auto pBaseEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		// GetGroup returns the base pointer -- m_vecVelocity/m_fFlags/IsAlive live on
		// CBasePlayer, not CBaseEntity.
		auto pEntity = pBaseEntity ? pBaseEntity->As<CBasePlayer>() : nullptr;
		if (!pEntity || !pEntity->IsAlive())
			continue;

		const Motion_t tMotion = TargetMotion(pEntity, flLead, flGravity);

		const float flFov = Math::CalcFov(G::CurrentUserCmd->viewangles, Math::CalcAngle(th.m_vEye, tMotion.m_vCenter));
		if ((flFov > flFovLimit && pEntity->entindex() != m_iLock) || flFov >= tBest.m_flFov)
			continue;

		float flTimes[2] = {};
		const int iRoots = FlightTimes(th, tMotion, flTimes);

		bool bFound = false;
		for (int iRoot = 0; iRoot < iRoots; ++iRoot)
		{
			if ((iArc == 1 && iRoot != 0) || (iArc == 2 && iRoot != 1))
				continue;

			Vec3 vDir; float flSpeed;
			if (!SolveDirection(th, tMotion.At(flTimes[iRoot]), flTimes[iRoot], flGravity, vDir, flSpeed))
				continue;

			const Vec3 vThrown = Math::VectorAngles(vDir);
			float flViewPitch = 0.f;
			if (!ViewPitch(NormPitch(vThrown.x), flViewPitch))
				continue;

			int iCount = 0;
			if (!TracePath(th, vDir, flSpeed, flGravity, flTimes[iRoot], tMotion.At(flTimes[iRoot]), pLocal, pEntity, vScratch, iCount))
				continue;

			tBest = { pEntity, flFov, flTimes[iRoot], iRoot, Vec3(flViewPitch, vThrown.y, 0.f) };
			m_vPath = vScratch;
			m_iPathCount = iCount;
			bFound = true;
			break;
		}

		// botox grenade_aim.cpp:495-516 wall bounce: no direct/lob path connects, try one
		// reflection off a nearby wall instead. iRoot is marked 2 + the bounce root (0/1) so
		// callers can tell a bounce solution from a direct one if they ever need to.
		if (!bFound && Vars::Aimbot::Grenade::WallBounce.Value)
		{
			GrenadeMath::Wall tWalls[k_wallRays] = {};
			const int iWallCount = FindWalls(tMotion.m_vCenter, tWalls);

			for (int w = 0; w < iWallCount && !bFound; ++w)
			{
				float flTimes2[2] = {};
				const int iBounceRoots = BounceTimes(th, tWalls[w], tMotion, flGravity, flTimes2);

				for (int iRoot = 0; iRoot < iBounceRoots && !bFound; ++iRoot)
				{
					const GrenadeMath::V3 vEyeM{ th.m_vEye.x, th.m_vEye.y, th.m_vEye.z };
					const GrenadeMath::V3 vInheritM{ th.m_vInherit.x, th.m_vInherit.y, th.m_vInherit.z };
					const Vec3 vTargetAt = tMotion.At(flTimes2[iRoot]);
					const GrenadeMath::V3 vTargetM{ vTargetAt.x, vTargetAt.y, vTargetAt.z };

					GrenadeMath::V3 vVelocityM{}; float flHitTime = 0.f;
					if (!GrenadeMath::BounceLaunch(vEyeM, vInheritM, tWalls[w], vTargetM, flTimes2[iRoot], flGravity, k_elasticity,
						k_launch_offset, vVelocityM, flHitTime))
						continue;

					const Vec3 vLaunch = Vec3(vVelocityM.x, vVelocityM.y, vVelocityM.z) - th.m_vInherit;
					const float flLaunchLen = vLaunch.Length();
					if (flLaunchLen < 1.f)
						continue;

					const Vec3 vDir = vLaunch / flLaunchLen;
					const Vec3 vThrown = Math::VectorAngles(vDir);
					float flViewPitch = 0.f;
					if (!ViewPitch(NormPitch(vThrown.x), flViewPitch))
						continue;

					const GrenadeMath::Wall tWallCopy = tWalls[w];
					int iCount = 0;
					if (!TracePath(th, vDir, flLaunchLen, flGravity, flTimes2[iRoot], vTargetAt, pLocal, pEntity, vScratch, iCount, &tWallCopy))
						continue;

					tBest = { pEntity, flFov, flTimes2[iRoot], 2 + iRoot, Vec3(flViewPitch, vThrown.y, 0.f) };
					m_vPath = vScratch;
					m_iPathCount = iCount;
					bFound = true;
				}
			}
		}
	}

	m_iLock = tBest.m_pEntity ? tBest.m_pEntity->entindex() : 0;

	if (!tBest.m_pEntity)
	{
		if (bThrowTick)
			DebugLog("GAIM: throw no solution item=%d", iItemID);
		return;
	}

	// CSS punch is applied straight to the view with no separate recoil-scale multiplier the way
	// CS:GO's weapon_recoil_scale convar works (UNVERIFIED css, see aim_rcs.h header).
	Vec3 vView = tBest.m_vFinal - pLocal->m_vecPunchAngle();
	Math::ClampAngles(vView);

	if (!std::isfinite(vView.x) || !std::isfinite(vView.y))
	{
		m_iPathCount = 0;
		return;
	}

	m_vView = vView;

	if (!Vars::Aimbot::Grenade::Silent.Value)
		I::EngineClient->SetViewAngles(vView);

	if (Vars::Aimbot::Grenade::AutoThrow.Value && bKey && bPinPulled && (pCmd->buttons & (IN_ATTACK | IN_ATTACK2)))
	{
		pCmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
		if (!m_bAutoReleased)
			DebugLog("GAIM: auto release ent=%d t=%.2f arc=%d", tBest.m_pEntity->entindex(), tBest.m_flTime, tBest.m_iRoot);
		m_bAutoReleased = true;
	}

	if (bThrowTick)
	{
		m_bThrow = true;
		DebugLog("GAIM: throw ent=%d t=%.2f arc=%d view=%.2f/%.2f lead=%.0fms", tBest.m_pEntity->entindex(), tBest.m_flTime, tBest.m_iRoot,
			vView.x, vView.y, flLead * 1000.f);
	}
}

void CGrenadeAim::Apply(CUserCmd* pCmd)
{
	if (!m_bThrow)
		return;
	m_bThrow = false;

	const Vec3 vView(m_vView.x, m_vView.y, pCmd->viewangles.z);
	SDK::FixMovement(pCmd, vView);   // botox start/end_movement_fix bracket
	pCmd->viewangles = vView;
}

void CGrenadeAim::OnPaintTraverse()
{
	if (!Vars::Aimbot::Grenade::DrawPath.Value || m_iPathCount < 2)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return;

	const auto tColor = Vars::Aimbot::Grenade::PathColor.Value;
	if (!tColor.a)
		return;

	for (int i = 1; i < m_iPathCount; ++i)
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(m_vPath[i - 1], m_vPath[i]), I::GlobalVars->curtime + 0.01f, tColor);
}
