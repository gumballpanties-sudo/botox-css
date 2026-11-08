#include "ticks.h"

#include "engineprediction.h"
#include "css_weapons.h"

void CTicks::Reset()
{
	m_bSpeedhack = m_bDoubletap = m_bRecharge = m_bWarp = false;
	m_iShiftedTicks = m_iShiftedGoal = 0;
}

bool CTicks::NeedsLocalAnimationFix()
{
	// The custom local-animation path only exists for features that manipulate the local model's
	// animation/yaw or hold back packets. Only the tick-shift half of that exists here.
	return m_bDoubletap || m_bWarp || m_bRecharge || m_bSpeedhack
		|| m_iShiftedTicks != 0 || m_iShiftedGoal != 0;
}

void CTicks::Recharge(CCSPlayer* pLocal)
{
	if (!m_bGoalReached)
		return;

	bool bPassive = m_bRecharge = false;

	static float flPassiveTime = 0.f;
	flPassiveTime = std::max(flPassiveTime - TICK_INTERVAL, -TICK_INTERVAL);
	if (Vars::Doubletap::PassiveRecharge.Value && 0.f >= flPassiveTime)
	{
		bPassive = true;
		flPassiveTime += 1.f / Vars::Doubletap::PassiveRecharge.Value;
	}

	if (m_iDeficit)
	{
		bPassive = true;
		m_iDeficit--, m_iShiftedTicks--;
	}

	if (!Vars::Doubletap::RechargeTicks.Value && !bPassive
		|| m_bDoubletap || m_bWarp || m_iShiftedTicks == m_iMaxShift || m_bSpeedhack)
		return;

	m_bRecharge = true;
	m_iShiftedGoal = m_iShiftedTicks + 1;
}

void CTicks::Warp()
{
	if (!m_bGoalReached)
		return;

	m_bWarp = false;
	if (!Vars::Doubletap::Warp.Value
		|| !m_iShiftedTicks || m_bDoubletap || m_bRecharge || m_bSpeedhack)
		return;

	m_bWarp = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::WarpRate.Value + 1, 0);
}

void CTicks::Doubletap(CCSPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_bGoalReached)
		return;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack)
		return;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return;

	bool bAttacking = G::PrimaryWeaponType == EWeaponType::MELEE ? pCmd->buttons & IN_ATTACK : G::Attacking;
	if (!G::CanPrimaryAttack && !G::Reloading || !bAttacking && !m_bDoubletap)
		return;

	m_bDoubletap = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::TickLimit.Value + 1, 0);
	if (Vars::Doubletap::AntiWarp.Value)
		m_bAntiWarp = pLocal->m_hGroundEntity();
}

void CTicks::Speedhack()
{
	m_bSpeedhack = Vars::Speedhack::Enabled.Value;
	if (!m_bSpeedhack)
		return;

	m_bDoubletap = m_bWarp = m_bRecharge = false;
}

static Vec3 s_vVelocity = {};
static int s_iMaxTicks = 0;
void CTicks::AntiWarp(CCSPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove, int iTicks)
{
	if (iTicks == -1)
		iTicks = GetTicks();
	s_iMaxTicks = std::max(iTicks + 1, s_iMaxTicks);

	Vec3 vAngles; Math::VectorAngles(s_vVelocity, vAngles);
	vAngles.y = flYaw - vAngles.y;
	Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
	vForward *= s_vVelocity.Length2D();

	if (iTicks > std::max(s_iMaxTicks - 8, 3))
		flForwardMove = -vForward.x, flSideMove = -vForward.y;
	else if (iTicks > 3)
		flForwardMove = flSideMove = 0.f;
	else
		flForwardMove = vForward.x, flSideMove = vForward.y;
}
void CTicks::AntiWarp(CCSPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bAntiWarp)
		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	else
	{
		s_vVelocity = pLocal->m_vecVelocity();
		s_iMaxTicks = 0;
	}
}

void CTicks::CreateMove(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket)
{
	// Doubletap()/AntiWarp() arming removed (tickbase exploits). m_bDoubletap/m_bWarp/m_bAntiWarp stay
	// false, so ManagePacket() takes the clean (PSilent-only) choke path below.
	ManagePacket(pCmd, pSendPacket);

	SaveShootPos(pLocal);
	SaveShootAngle(pCmd, *pSendPacket);

	// Doubletap m_bTimingUnsure arming deleted: m_bDoubletap is permanently false (TF2-only,
	// see ManagePacket below), so this branch never ran; CWeaponCSBase also has no IsInReload()
	// (that's CTFWeaponBase's, defs.h).
	(void)pWeapon;
}

void CTicks::ManagePacket(CUserCmd* pCmd, bool* pSendPacket)
{
	if (!m_bDoubletap && !m_bWarp && !m_bSpeedhack)
	{
		// The ONLY thing that chokes in this build is psilent aim, and G::PSilentAngles is
		// cleared by UpdateInfo at the top of every CreateMove -- so with no aim feature
		// running it is false on every tick and nothing here fires.
		//
		// The old `else if (bWasSet || !bCanChoke)` still WROTE `*pSendPacket = true` on a
		// tick nobody had choked, purely because the choke budget was spent. That overrides
		// the engine's own send decision -- CL_Move skips a send on frames that would run
		// past cl_cmdrate -- so at fps above cmdrate it pushed out commands TF2 had chosen to
		// hold. A choke we never opened is not ours to release: only bWasSet reopens the
		// write, and the failsafe survives inside it (a spent budget stops the choke, and the
		// release that follows is the one that sets the flag back).
		static bool bWasSet = false;
		if (G::PSilentAngles && F::Ticks.CanChoke())
			*pSendPacket = false, bWasSet = true;
		else if (bWasSet)
			*pSendPacket = true, bWasSet = false;
	}
	else
	{
		if ((m_bSpeedhack || m_bWarp) && G::Attacking == 1)
		{
			*pSendPacket = true;
			return;
		}

		*pSendPacket = m_iShiftedGoal == m_iShiftedTicks;
		if (I::ClientState->chokedcommands >= 21) // prevent overchoking
			*pSendPacket = true;
	}
}

void CTicks::Start(CCSPlayer* pLocal, CUserCmd* pCmd)
{
	Vec2 vOriginalMove; int iOriginalButtons;
	if (m_bPredictAntiwarp = m_bAntiWarp || GetTicks(H::Entities.GetWeapon()) && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
	{
		vOriginalMove = { pCmd->forwardmove, pCmd->sidemove };
		iOriginalButtons = pCmd->buttons;

		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	}

	F::EnginePrediction.Start(pLocal, pCmd);

	if (m_bPredictAntiwarp)
	{
		pCmd->forwardmove = vOriginalMove.x, pCmd->sidemove = vOriginalMove.y;
		pCmd->buttons = iOriginalButtons;
	}
}

void CTicks::End(CCSPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bPredictAntiwarp && !m_bAntiWarp && !G::Attacking)
	{
		F::EnginePrediction.End(pLocal, pCmd);
		F::EnginePrediction.Start(pLocal, pCmd);
	}
}

bool CTicks::CanChoke()
{
	static auto sv_maxusrcmdprocessticks = H::ConVars.FindVar("sv_maxusrcmdprocessticks");
	int iMaxTicks = sv_maxusrcmdprocessticks->GetInt();
	if (Vars::Misc::Game::AntiCheatCompatibility.Value)
		iMaxTicks = std::min(iMaxTicks, 8);

	return I::ClientState->chokedcommands < 21 && m_iShiftedTicks + I::ClientState->chokedcommands < iMaxTicks;
}

int CTicks::GetTicks(CWeaponCSBase* pWeapon)
{
	if (m_bDoubletap && m_iShiftedGoal < m_iShiftedTicks)
		return m_iShiftedTicks - m_iShiftedGoal;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack)
		return 0;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return 0;
	
	return std::min(Vars::Doubletap::TickLimit.Value - 1, m_iMaxShift);
}

int CTicks::GetShotsWithinPacket(CWeaponCSBase* pWeapon, int iTicks)
{
	iTicks = std::min(m_iMaxShift + 1, iTicks);

	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	// CWeaponCSBase has no GetFireRate() (that's CTFWeaponBase's) -- css_weapons.h's
	// m_flCycleTime is the same "seconds between shots" value.
	auto pRateData = CSWeaponScript(pWeapon->GetWeaponID());
	const float flCycle = pRateData && pRateData->m_flCycleTime > 0.f ? pRateData->m_flCycleTime : 0.1f;
	return 1 + (iTicks - iDelay) / std::ceilf(flCycle / TICK_INTERVAL);
}

void CTicks::SaveShootPos(CCSPlayer* pLocal)
{
	if (m_iShiftedTicks == m_iShiftStart)
		m_vShootPos = pLocal->GetShootPos();
}
Vec3 CTicks::GetShootPos()
{
	return m_vShootPos;
}

void CTicks::SaveShootAngle(CUserCmd* pCmd, bool bSendPacket)
{
	static auto sv_maxusrcmdprocessticks_holdaim = H::ConVars.FindVar("sv_maxusrcmdprocessticks_holdaim");

	if (bSendPacket)
		m_bShootAngle = false;
	else if (!m_bShootAngle && G::Attacking == 1 && sv_maxusrcmdprocessticks_holdaim->GetBool())
		m_vShootAngle = pCmd->viewangles, m_bShootAngle = true;
}
Vec3* CTicks::GetShootAngle()
{
	if (m_bShootAngle && I::ClientState->chokedcommands)
		return &m_vShootAngle;
	return nullptr;
}

bool CTicks::IsTimingUnsure()
{	// actually knowing when we'll shoot would be better than this, but this is fine for now
	return m_bTimingUnsure || m_bSpeedhack /*|| m_bWarp*/;
}

void CTicks::Draw(CCSPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ticks) || !pLocal->IsAlive())
		return;

	const DragBox_t dtPos = Vars::Menu::TicksDisplay.Value;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);

	if (!m_bSpeedhack)
	{
		int iChoke = std::max(I::ClientState->chokedcommands, 0); // nothing subtracts from the choke count here
		int iTicks = std::clamp(m_iShiftedTicks + iChoke, 0, m_iMaxShift);
		float flRatio = float(iTicks) / m_iMaxShift;
		int iSizeX = H::Draw.Scale(100, Scale_Round), iSizeY = H::Draw.Scale(12, Scale_Round);
		int iPosX = dtPos.x - iSizeX / 2, iPosY = dtPos.y + fFont.m_nTall + H::Draw.Scale(4) + 1;

		H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + 2, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOP, std::format("Ticks {} / {}", iTicks, m_iMaxShift).c_str());
		if (m_iWait)
			H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + fFont.m_nTall + H::Draw.Scale(18, Scale_Round) + 1, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOP, "Not Ready");

		H::Draw.LineRoundRect(iPosX, iPosY, iSizeX, iSizeY, H::Draw.Scale(4, Scale_Round), Vars::Menu::Theme::Accent.Value, 16);
		if (flRatio)
		{
			iSizeX -= H::Draw.Scale(2, Scale_Ceil) * 2, iSizeY -= H::Draw.Scale(2, Scale_Ceil) * 2;
			iPosX += H::Draw.Scale(2, Scale_Round), iPosY += H::Draw.Scale(2, Scale_Round);
			H::Draw.StartClipping(iPosX, iPosY, iSizeX * flRatio, iSizeY);
			H::Draw.FillRoundRect(iPosX, iPosY, iSizeX, iSizeY, H::Draw.Scale(3, Scale_Round), Vars::Menu::Theme::Accent.Value, 16);
			H::Draw.EndClipping();
		}
	}
	else
		H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + 2, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOP, std::format("Speedhack x{}", Vars::Speedhack::Amount.Value).c_str());
}