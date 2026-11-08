#include "aimbot.h"

#include "aim_hitscan.h"
#include "aim_rcs.h"
#include "grenade_aim.h"
#include "backtrack.h"

// botox csgo-sdk hacks/aimbot/aimbot.cpp CAimbot::Run 1:1 shape, minus every TF2 weapon-type
// dispatch (CAimbot::RunAimbot's PROJECTILE/MELEE cases, auto backstab, auto detonate/airblast/
// rocket jump) -- CS:GO botox's aimbot has one gun-aim path (hitscan) plus a SEPARATE, always-
// polled grenade aimbot (botox calls g_grenade_aim.on_create_move() unconditionally, not gated by
// weapon type); CSS knife has no aim at all (no CS:GO/botox melee aimbot feature exists to port).

// activation bind -- the same three modes dih's own movement features use. Edge-detected here
// rather than in the menu so a toggle press is counted once per command.
bool CAimbot::KeyOK()
{
	const int iKey = Vars::Aimbot::General::AimKey.Value;
	const int iMode = Vars::Aimbot::General::AimKeyMode.Value;

	if (iMode != Vars::Aimbot::General::AimKeyModeEnum::OnHotkey
		&& iMode != Vars::Aimbot::General::AimKeyModeEnum::Toggle)
		return true;     // always on
	if (!iKey)
		return true;     // a mode with no key bound would be an off switch nobody can turn back on

	const bool bDown = (GetAsyncKeyState(iKey) & 0x8000) != 0;

	if (iMode == Vars::Aimbot::General::AimKeyModeEnum::OnHotkey)
		return bDown;

	static bool s_bWasDown = false;
	if (bDown && !s_bWasDown)
		m_bToggled = !m_bToggled;
	s_bWasDown = bDown;
	return m_bToggled;
}

bool CAimbot::ShouldRun(CCSPlayer* pLocal, CWeaponCSBase* pWeapon)
{
	// CCSPlayer::CanAttack() doesn't exist (that was CTFPlayer's cond-gated version, TF2-only --
	// cloak/disguise/taunting checks). CSS just needs the player alive.
	if (!pWeapon || !pLocal->IsAlive() || I::EngineVGui->IsGameUIVisible())
		return false;

	return true;
}

void CAimbot::Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd)
{
	m_bRan = false;
	if (abs(G::AimTarget.m_iTickCount - I::GlobalVars->tickcount) > G::AimTarget.m_iDuration)
		G::AimTarget = {};
	if (abs(G::AimPoint.m_iTickCount - I::GlobalVars->tickcount) > G::AimPoint.m_iDuration)
		G::AimPoint = {};

	// botox rcs_track(): BEFORE the aimbot's own scan, both RCS paths read the burst counter it
	// updates. Runs even with the master switch off (botox: "rcs runs with the aimbot master
	// switch OFF too").
	F::AimbotRCS.Track(pLocal);

	// grenade aimbot: own key, own gate, runs regardless of the held weapon's type or the master
	// aim key (botox g_grenade_aim.on_create_move()).
	F::GrenadeAim.OnCreateMove(pLocal, pCmd);

	// botox: aim not allowed (off / key up / dead) = no lock, the next press picks fresh
	if (ShouldRun(pLocal, pWeapon) && Vars::Aimbot::General::Enabled.Value && KeyOK())
		F::AimbotHitscan.Run(pLocal, pWeapon, pCmd);
	else
		F::AimbotHitscan.ResetLock();

	// standalone RCS: runs with the aimbot off too, strength comes from the per-class sliders.
	F::AimbotRCS.Run(pLocal, pWeapon, pCmd, Vars::Aimbot::General::Silent.Value && !Vars::Aimbot::General::Camlock.Value);

	// grenade_aim.Apply() must run after every other view writer in this tick or the throw-tick
	// angle gets overwritten by whatever ran after it.
	F::GrenadeAim.Apply(pCmd);

	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
	F::Backtrack.RunBacktrack(pLocal, pWeapon, pCmd);   // botox run_backtrack: fire cmds the aim did not commit
}
