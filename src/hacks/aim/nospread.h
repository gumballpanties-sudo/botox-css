#pragma once
#include "sdk.h"

class CNoSpread
{
private:
	bool ShouldRun(CCSPlayer* pLocal, CWeaponCSBase* pWeapon);

public:
	void Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CNoSpread, NoSpread);