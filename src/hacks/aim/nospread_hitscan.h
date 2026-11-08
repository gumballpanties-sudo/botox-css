#pragma once
#include "sdk.h"

// botox-css 1:1-shape port of cssourcex64 Features/Aimbot.cpp:112-160 Nospread(), calling the
// REAL live CWeaponCSBase::UpdateAccuracyPenalty/GetInaccuracy/GetSpread vtable slots (css.h,
// verified by direct disassembly -- docs/port/vtable_check.md), not cssourcex64's own slot
// labels (112-160 has 382/383 swapped -- see css.h file header for the disassembly evidence).
//
// The TF2 build's whole playerperf/server-uptime-guessing path (AskForPlayerPerf/ParsePlayerPerf,
// the "m_bSynced" gate) is GONE, not ported: that machinery existed only because TF2's engine
// branch could run with sv_usercmd_custom_random_seed off, in which case CUserCmd::random_seed is
// NOT the real server seed and has to be reconstructed from the server's wall-clock time (hence
// asking it via the "playerperf" concmd). CSS has no such concmd and ships with
// sv_usercmd_custom_random_seed defaulting on (verified: source-sdk-2013-master
// game/shared/usercmd.cpp / in_main.cpp CreateCmd sets cmd->random_seed unconditionally from
// MD5_PseudoRandom(sequence_number) -- the client always has the real seed already, every tick).
class CNoSpreadHitscan
{
public:
	bool ShouldRun(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, bool bCreateMove = false);
	void Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd);
	void Draw(CCSPlayer* pLocal);

	// shots fired this burst (burst-edge detection only now -- the live GetInaccuracy/
	// GetSpread/UpdateAccuracyPenalty vtable trio tracks the real recoil-buildup state itself,
	// aim_rcs.cpp's Track() shares this same counter for its own burst-edge read).
	int m_iShotsThisBurst = 0;

private:
	short m_iLastWeapon = 0;
	int m_iLastGameShots = 0;
};

ADD_FEATURE(CNoSpreadHitscan, NoSpreadHitscan);
