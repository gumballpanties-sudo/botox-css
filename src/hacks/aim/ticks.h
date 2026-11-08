#pragma once
#include "sdk.h"

class CTicks
{
private:

	void Recharge(CCSPlayer* pLocal);
	void Warp();
	void Doubletap(CCSPlayer* pLocal, CUserCmd* pCmd);
	void Speedhack();

	void ManagePacket(CUserCmd* pCmd, bool* pSendPacket);

	bool m_bGoalReached = true;
	Vec3 m_vShootPos = {};

	bool m_bShootAngle = false;
	Vec3 m_vShootAngle = {};

	bool m_bPredictAntiwarp = false;
	bool m_bTimingUnsure = false; // we aren't sure when we'll actually fire, hold aim

public:
	void CreateMove(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket);
	void Draw(CCSPlayer* pLocal);
	void Reset();

	void Start(CCSPlayer* pLocal, CUserCmd* pCmd);
	void End(CCSPlayer* pLocal, CUserCmd* pCmd);

	void AntiWarp(CCSPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove, int iTicks = -1);
	void AntiWarp(CCSPlayer* pLocal, CUserCmd* pCmd);

	bool CanChoke();
	int GetTicks(CWeaponCSBase* pWeapon = nullptr);
	int GetShotsWithinPacket(CWeaponCSBase* pWeapon, int iTicks = Vars::Doubletap::TickLimit.Value);

	void SaveShootPos(CCSPlayer* pLocal);
	Vec3 GetShootPos();
	void SaveShootAngle(CUserCmd* pCmd, bool bSendPacket);
	Vec3* GetShootAngle();
	bool IsTimingUnsure();

	// True when a feature actually needs the DLL's custom local-animation path (per-tick advance in
	// LocalAnimations + skipping the engine's per-frame UpdateClientSideAnimation for the local player).
	bool NeedsLocalAnimationFix();

	bool m_bDoubletap = false;
	bool m_bWarp = false;
	bool m_bRecharge = false;
	bool m_bAntiWarp = false;
	bool m_bSpeedhack = false;

	int m_iShiftedTicks = 0;
	int m_iShiftedGoal = 0;
	int m_iShiftStart = 0;
	bool m_bShifting = false;
	bool m_bShifted = false;

	int m_iWait = 0;
	int m_iMaxShift = 24;
	int m_iDeficit = 0;
};

ADD_FEATURE(CTicks, Ticks);