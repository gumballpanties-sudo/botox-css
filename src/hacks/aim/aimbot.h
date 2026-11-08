#pragma once
#include "sdk.h"

class CAimbot
{
private:
	bool KeyOK();   // activation bind (always on / hold / toggle)
	bool ShouldRun(CCSPlayer* pLocal, CWeaponCSBase* pWeapon);

public:
	void Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd);

	bool m_bRan = false;
	bool m_bToggled = false;          // latch for AimKeyMode == Toggle
};

ADD_FEATURE(CAimbot, Aimbot);
