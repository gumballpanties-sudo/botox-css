#include "aim_rcs.h"
#include "aim_global.h"   // WeaponSettings (per-class rcs)
#include "css.h"

#include <algorithm>
#include <cmath>

void DebugLog(const char* fmt, ...);

namespace
{
	// botox rcs_unwind(): comp may grow in one tick (bullets must land) but only shrink at a
	// bounded rate, so an artificial collapse (weapon switch, aim losing its target) never hands
	// back a whole spray's worth of comp on one frame. A normal punch decay is far slower than this
	// limiter and never touches it.
	float Unwind(const float flApplied, const float flWant, const float flDt)
	{
		if (std::fabsf(flWant) >= std::fabsf(flApplied))
			return flWant;
		const float flMaxStep = (std::max)(std::fabsf(flApplied) * (1.f - std::exp(-20.f * flDt)), 20.f * flDt);
		if (std::fabsf(flApplied - flWant) <= flMaxStep)
			return flWant;
		return flApplied + (flWant > flApplied ? flMaxStep : -flMaxStep);
	}
}

// botox recoil_scale(): weapon_recoil_scale convar, default 2.0. CSS has no such convar, the 2 is
// hardcoded: bullet = EyeAngles + 2 * punch (weapon_csbasegun.cpp:141, live client.dll FX_FireBullets
// callers `addss xmm6, xmm6` on the punch). 1.0 here comped half the recoil.
float CAimbotRCS::RecoilScale()
{
	static const auto convar = H::ConVars.FindVar("weapon_recoil_scale");
	return convar ? convar->GetFloat() : 2.f;
}

void CAimbotRCS::Track(CCSPlayer* pLocal)
{
	m_bAimTick = false;
	if (!pLocal || !pLocal->IsAlive())
	{
		m_iShots = m_iLastGameShots = 0;
		m_flAppliedX = m_flAppliedY = 0.f;
		m_bStalePunch = false;
		m_bAimBurst = false;
		return;
	}

	auto pWeapon = pLocal->m_hActiveWeapon().Get() ? pLocal->m_hActiveWeapon()->As<CWeaponCSBase>() : nullptr;
	const short iWeaponID = pWeapon ? (short)pWeapon->GetWeaponID() : 0;

	const Vec3 vPunch = pLocal->m_vecPunchAngle();
	const bool bPunchUp = std::fabsf(vPunch.x) + std::fabsf(vPunch.y) >= 0.02f;

	if (iWeaponID != m_iLastWeapon)
	{
		m_iLastWeapon = iWeaponID;
		m_iShots = 0;
		m_bStalePunch = bPunchUp;
	}

	if (!bPunchUp)
	{
		m_iShots = 0;
		m_bStalePunch = false;
		m_bAimBurst = false;
	}

	// botox rcs_track() (aimbot.cpp:1454-1459): counts the networked shots-fired counter's rise
	// (CS:GO m_iShotsFired == CCSPlayer::m_iShotsFired here, css.h, player-side
	// DT_CSLocalPlayerExclusive -- same field, same role). Delta not a plain edge, 1:1 with botox.
	const int iGameShots = pLocal->As<CCSPlayer>()->m_iShotsFired();
	if (iGameShots > m_iLastGameShots)
		m_iShots += iGameShots - m_iLastGameShots;
	m_iLastGameShots = iGameShots;
}

Vec3 CAimbotRCS::Offset(const float flClassRcsPct)
{
	auto pLocal = H::Entities.GetLocal();
	if (flClassRcsPct <= 0.f || !pLocal || m_bStalePunch)
		return {};

	if (m_iShots < Vars::Aimbot::RCS::Start.Value)
		return {};

	const float flMaster = (flClassRcsPct / 100.f) * RecoilScale();
	const Vec3 vPunch = pLocal->m_vecPunchAngle();

	return Vec3(vPunch.x * flMaster * (Vars::Aimbot::RCS::Pitch.Value / 100.f), vPunch.y * flMaster * (Vars::Aimbot::RCS::Yaw.Value / 100.f), 0.f);
}

void CAimbotRCS::Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd, const bool bSilentAimActive)
{
	if (!pLocal || !pCmd || !pLocal->IsAlive())
		return;

	// botox rcs_strength(): gated on the "only with aimbot" burst latch, not raw aiming-this-tick
	// (the aim half only writes its angle on can-shoot ticks, so gating on that directly would
	// drop comp in and out between shots -- m_bAimBurst is OR'd in by the aim half every tick it
	// found a target and stays latched for the whole burst).
	if (Vars::Aimbot::RCS::OnlyWithAimbot.Value && !m_bAimBurst)
		return;

	// botox aimbot.cpp:1474 rcs_strength: weapon_settings(class).m_rcs -- the held class's override
	// rcs, else general; non-gun = 0
	const float flStrength = m_bStalePunch || !pWeapon ? 0.f : F::AimbotGlobal.WeaponSettings(pWeapon->GetWeaponID()).m_flRCS;
	const Vec3 vWant = Offset(flStrength);

	// the aim half already wrote a comped angle this tick (botox: aim -= silent ? punch * scale : want,
	// then publish(aim) -- no second comp). Visible keeps `applied` in step so the unwind starts from
	// what the camera really carries once the target is gone.
	if (m_bAimTick)
	{
		if (!bSilentAimActive)
		{
			m_flAppliedX = vWant.x;
			m_flAppliedY = vWant.y;
		}
		return;
	}

	if (bSilentAimActive)
	{
		// silent: the engine rebuilds the view from raw mouse every tick, so the comp never
		// accumulates there -- it must be the FULL current punch, applied absolutely, every tick.
		// aim_hitscan's own silent path already does this for the aim angle itself; this covers
		// every OTHER tick (not aiming, just spraying).
		pCmd->viewangles -= vWant;
		Math::ClampAngles(pCmd->viewangles);
		return;
	}

	// visible: comp lives IN the camera, walked toward `want` (grows exact, shrinks paced).
	const float flInterval = I::GlobalVars->interval_per_tick;
	const float flNextX = Unwind(m_flAppliedX, vWant.x, flInterval);
	const float flNextY = Unwind(m_flAppliedY, vWant.y, flInterval);

	Vec3 vAngles; I::EngineClient->GetViewAngles(vAngles);
	vAngles.x -= flNextX - m_flAppliedX;
	vAngles.y -= flNextY - m_flAppliedY;
	Math::ClampAngles(vAngles);

	m_flAppliedX = flNextX;
	m_flAppliedY = flNextY;

	if (std::isfinite(vAngles.x) && std::isfinite(vAngles.y))
	{
		I::EngineClient->SetViewAngles(vAngles);
		pCmd->viewangles = vAngles;
	}
}

void CAimbotRCS::Camlock(CUserCmd* pCmd, const Vec3& vAim, const float flSmooth)
{
	Vec3 vCamera; I::EngineClient->GetViewAngles(vCamera);

	if (flSmooth <= 1.f)
		vCamera = vAim;
	else
	{
		Vec3 vDelta = vAim - vCamera;
		Math::ClampAngles(vDelta);
		vCamera += vDelta * (1.f / flSmooth);
	}
	Math::ClampAngles(vCamera);

	if (!std::isfinite(vCamera.x) || !std::isfinite(vCamera.y))
		return;

	I::EngineClient->SetViewAngles(vCamera);
	pCmd->viewangles = vCamera;

	// botox on_create_move_post: stamp the target this tick glided toward so RenderFrame() can
	// keep gliding the same direction between ticks.
	m_flCamPitch = vAim.x;
	m_flCamYaw = vAim.y;
	m_flCamSmooth = flSmooth;
	m_flCamStamp = (std::max)(I::GlobalVars->realtime, 1e-3f);
}

// botox camlock_frame(), 1:1.
void CAimbotRCS::RenderFrame()
{
	if (m_flCamStamp == 0.f || m_flCamSmooth <= 1.f)
		return;

	auto pLocal = H::Entities.GetLocal();
	const float flRealTime = I::GlobalVars->realtime;

	// create_move stopped ticking (pause, menu, disconnect): no fresh aim, drop the glide.
	if (flRealTime - m_flCamStamp > 0.1f || !pLocal || !pLocal->IsAlive())
	{
		m_flCamStamp = 0.f;
		return;
	}

	Vec3 vCamera; I::EngineClient->GetViewAngles(vCamera);

	Vec3 vDelta = Vec3(m_flCamPitch, m_flCamYaw, vCamera.z) - vCamera;
	Math::ClampAngles(vDelta);

	// same rate as the tick step (1 - (1 - 1/s)^1), split over frames instead of ticks.
	const float flFrameTime = std::clamp(I::GlobalVars->frametime, 0.f, 0.1f);
	vCamera += vDelta * (1.f - std::pow(1.f - 1.f / m_flCamSmooth, flFrameTime * 64.f));
	Math::ClampAngles(vCamera);

	if (std::isfinite(vCamera.x) && std::isfinite(vCamera.y))
		I::EngineClient->SetViewAngles(vCamera);
}
