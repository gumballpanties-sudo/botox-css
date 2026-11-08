#include "nospread.h"

#include "nospread_hitscan.h"
// nospread_projectile.h DELETED: TF2-only (rocket/pipe/arrow projectile spread removal). CSS
// hitscan weapons are the only ones with a spread/inaccuracy model (css_weapons.h) -- grenades
// have none (see CSWeaponScript table comment).

bool CNoSpread::ShouldRun(CCSPlayer* pLocal, CWeaponCSBase* pWeapon)
{
	return Vars::Aimbot::General::NoSpread.Value && pLocal && pLocal->IsAlive() && pWeapon;
}

void CNoSpread::Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd)
{
	if (!ShouldRun(pLocal, pWeapon))
		return;

	F::NoSpreadHitscan.Run(pLocal, pWeapon, pCmd);
}
