#include "nospread_hitscan.h"
#include "ticks.h"
#include <cmath>

constexpr float M_PI = 3.14159265358979323846f;

bool CNoSpreadHitscan::ShouldRun(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, bool bCreateMove)
{
	if (!pLocal || !pWeapon || G::PrimaryWeaponType != EWeaponType::HITSCAN)
		return false;

	if (!pWeapon->HasSpread())
		return false;

	return bCreateMove ? G::Attacking == 1 : true;
}

// Mirrors aim_rcs.cpp's CAimbotRCS::Track burst-edge detection (same networked m_iShotsFired
// rising edge, css.h), kept separate so nospread's recoil-buildup term (css_weapons.h
// InaccuracyFire) works even with RCS/the aimbot both off.
static void UpdateBurstCount(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, short& iLastWeapon, int& iLastGameShots, int& iShotsThisBurst)
{
	if (!pLocal || !pLocal->IsAlive())
	{
		iShotsThisBurst = iLastGameShots = 0;
		return;
	}

	const short iWeaponID = pWeapon ? (short)pWeapon->GetWeaponID() : 0;
	if (iWeaponID != iLastWeapon)
	{
		iLastWeapon = iWeaponID;
		iShotsThisBurst = 0;
	}

	const int iGameShots = pLocal->m_iShotsFired();
	if (iGameShots > iLastGameShots)
		iShotsThisBurst += iGameShots - iLastGameShots;
	else if (iGameShots < iLastGameShots)   // weapon switch / round reset: counter went backwards
		iShotsThisBurst = 0;
	iLastGameShots = iGameShots;
}

// cssourcex64 Features/Aimbot.cpp:112-160 Nospread() shape, every step checked against the live
// client.dll (11-04):
//  - CWeaponCSBase::ItemPostFrame (vtable 252) calls UpdateAccuracyPenalty (384) first thing, then
// PrimaryAttack -> GetSpread (383) + GetInaccuracy (382) -> FX_FireBullets .
//  - callers pass angles = EyeAngles + 2 * punch, seed = predictionRandomSeed & 255,
//    arg7 = inaccuracy, arg8 = spread.
//  - FX_FireBullets: RandomSeed(seed + 1); theta0 = RF(0, 2pi); r0 = RF(0, inaccuracy);
//    per bullet theta1 = RF(0, 2pi); r1 = RF(0, spread); offset = r0*(cos,sin)theta0 + r1*(cos,sin)theta1.
// cssourcex64 skips 384 (its void call on 382 is a getter) = one tick of penalty decay off.
void CNoSpreadHitscan::Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd)
{
	UpdateBurstCount(pLocal, pWeapon, m_iLastWeapon, m_iLastGameShots, m_iShotsThisBurst);

	if (!ShouldRun(pLocal, pWeapon, true))
		return;

	// run the game's own pre-shot step, then put the field back: the weapon is outside the
	// engine-prediction player copy, an un-restored call decays the predicted penalty twice
	const float flOldPenalty = pWeapon->m_fAccuracyPenalty();
	pWeapon->UpdateAccuracyPenalty();
	const float flInaccuracy = pWeapon->GetInaccuracy();
	const float flSpread = pWeapon->GetSpread();
	pWeapon->m_fAccuracyPenalty() = flOldPenalty;

	// the server rebuilds random_seed from command_number (usercmd.cpp ReadUsercmd), so use that
	const int iSeed = int(MD5_PseudoRandom(pCmd->command_number) & 0x7FFFFFFF & 255) + 1;
	SDK::RandomSeed(iSeed);

	const float flTheta0 = SDK::RandomFloat(0.f, 2.f * (float)M_PI);
	const float flR0 = SDK::RandomFloat(0.f, flInaccuracy);
	const float flTheta1 = SDK::RandomFloat(0.f, 2.f * (float)M_PI);
	const float flR1 = SDK::RandomFloat(0.f, flSpread);

	const float x = flR0 * cosf(flTheta0) + flR1 * cosf(flTheta1);
	const float y = flR0 * sinf(flTheta0) + flR1 * sinf(flTheta1);

	// spread is applied in the frame the bullet leaves in (view + 2 * punch), not the raw view.
	// Pass 1 = cssourcex64's reflection; passes 2-4 re-fire from the corrected view and take the
	// leftover off (one pass leaves >0.25 deg on a moving/jumping cone, tests/nospread_test.cpp).
	const Vec3 vPunch2 = pLocal->m_vecPunchAngle() * 2.f;
	Vec3 vBase = pCmd->viewangles + vPunch2;
	vBase.z = 0.f;

	Vec3 vFinal = pCmd->viewangles;
	for (int i = 0; i < 4; i++)
	{
		Vec3 vFrom = vFinal + vPunch2;
		vFrom.z = 0.f;

		Vec3 vForward, vRight, vUp;
		Math::AngleVectors(vFrom, &vForward, &vRight, &vUp);

		Vec3 vSpreadDir = vForward + vRight * x + vUp * y;
		vSpreadDir.Normalize();

		const Vec3 vBullet = Math::VectorAngles(vSpreadDir);
		vFinal.x += Math::NormalizeAngle(vBase.x - vBullet.x);
		vFinal.y += Math::NormalizeAngle(vBase.y - vBullet.y);
	}
	vFinal.z = 0.f;
	Math::ClampAngles(vFinal);

	if (!std::isfinite(vFinal.x) || !std::isfinite(vFinal.y))
		return;

	SDK::FixMovement(pCmd, vFinal);
	pCmd->viewangles = vFinal;
	G::SilentAngles = true;
}

void CNoSpreadHitscan::Draw(CCSPlayer* pLocal)
{
	// botox seed-prediction HUD readout dropped with the playerperf sync it displayed (see
	// header) -- CSS's seed is read straight off pCmd->random_seed every tick, nothing to show
	// an "uptime/synced" state for any more.
	(void)pLocal;
}
