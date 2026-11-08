#pragma once
#include "sdk.h"

// botox csgo-sdk hacks/aimbot/aimbot.cpp RCS half, ported 1:1 (standalone recoil comp + camlock),
// re-keyed off CSWeaponID (css_weapons.h) instead of CS:GO's weapon_class_vars. See aim_hitscan.cpp
// for the aim half; this file owns punch-burst tracking, the comp step, and the camlock glide.
//
// Shots-fired tracking (Track()) uses CCSPlayer::m_iShotsFired (css.h) 1:1 with botox
// rcs_track()'s get_shots_fired() -- no longer a punch-edge heuristic.
class CAimbotRCS
{
public:
	// create_move, BEFORE the aimbot's own target scan (botox rcs_track): counts this burst's
	// shots off the rising edge of the weapon's networked shots-fired counter, tracks stale punch
	// across a weapon switch.
	void Track(CCSPlayer* pLocal);

	// standalone comp: runs every tick regardless of whether the aimbot has a target (botox:
	// "rcs runs with the aimbot master switch OFF too"). Silent = absolute (view rebuilt from
	// raw mouse every tick); visible = delta, unwound at a bounded rate as the punch decays.
	void Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd, bool bSilentAimActive);

	// botox rcs_offset(): comp angle for `flClassRcsPct` at the current burst count. 0,0 below
	// the RCS start-shot threshold or with stale punch. Shared with aim_hitscan's silent/camlock path
	// so the aim angle and the standalone comp never fight over the same punch.
	Vec3 Offset(float flClassRcsPct);

	// camlock (botox camlock, salvia-style): camera AND bullets ride the target, no silent-aim
	// split. Called from aim_hitscan when Vars::Aimbot::General::Camlock is set and a target is
	// found; smooth <= 1 snaps immediately, otherwise glides at the tick rate (1 - (1-1/s)^1,
	// i.e. one tick-step per CreateMove). Also stamps the render-frame glide state RenderFrame()
	// below reads (botox camlock_frame, 11-03 addition) -- NEEDS FROM OTHERS wires the call site.
	void Camlock(CUserCmd* pCmd, const Vec3& vAim, float flSmooth);

	// botox camlock_frame(): between-tick glide at FRAME_RENDER_START, same curve as Camlock()'s
	// tick step but split over the frame rate instead of the tick rate, so the camera doesn't
	// visibly step at low tickrate / smooth. No-op if no camlock ran this tick (m_flCamStamp==0),
	// smooth<=1 (nothing to glide), or the stamp is >0.1s stale (paused/disconnected -- 1:1 with
	// botox's own stale-stamp drop). NEEDS FROM OTHERS: call from src/hacks/aim/hooks.cpp's
	// CHLClient_FrameStageNotify (MAKE_HOOK ~line 803, SDK-owned, not this unit's) --
	// add `case FRAME_RENDER_START: F::AimbotRCS.RenderFrame(); break;` to its switch (that hook
	// already handles FRAME_NET_UPDATE_START/END there; FRAME_RENDER_START is the stage
	// hacks/misc/game_extras.cpp's own FrameStageNotify tap treats as "once a frame, after the
	// net update, before paint" -- same stage, same enum, defs.h ClientFrameStage_t).
	void RenderFrame();

	bool m_bAimBurst = false;   // botox m_rcs_aim_burst: true any tick the aim half found a target
	bool m_bAimTick = false;    // aim half wrote a comped angle this tick: Run() adds no second comp

	// bullet = view + punch * RecoilScale() (CSS: hardcoded 2)
	static float RecoilScale();

private:
	int m_iShots = 0;             // botox m_rcs_shots: our own burst counter
	int m_iLastGameShots = 0;
	short m_iLastWeapon = 0;
	bool m_bStalePunch = false;   // punch inherited from the weapon we just switched off
	float m_flAppliedX = 0.f, m_flAppliedY = 0.f;   // comp baked into the visible camera

	// botox m_cam_pitch/m_cam_yaw/m_cam_smooth/m_cam_stamp: this tick's camlock target + glide
	// rate, read back by RenderFrame() for the between-tick step.
	float m_flCamPitch = 0.f, m_flCamYaw = 0.f, m_flCamSmooth = 1.f, m_flCamStamp = 0.f;
};

ADD_FEATURE(CAimbotRCS, AimbotRCS);
