#include "sdk.h"

#include "menu.h"
#include "engineprediction.h"
#include "css_weapons.h"
#include <random>

#pragma warning (disable : 6385)

// CSS_STUB tf2 api: CAttributeManager_AttribHookFloat sig removed (MISSING in CSS,
// sig_check.md) -- TF2's econ item-attribute system has no CSS equivalent at all
// (CS:S weapons carry no attached attribute schema). AttribHookValue() below now
// returns `value` unchanged (the correct "no attribute present" answer for every
// caller, since that's exactly what the real function returns when an item has no
// matching attribute -- not just a compile stopgap).

void SDK::Output(const char* sFunction, const char* sLog, Color_t tColor,
	int iTo, int iMessageBox,
	const char* sLeft, const char* sRight)
{
	if (sLog)
	{
		if (iTo & OUTPUT_CONSOLE)
		{
			I::CVar->ConsoleColorPrintf(tColor, "%s%s%s ", sLeft, sFunction, sRight);
			I::CVar->ConsoleColorPrintf({}, "%s\n", sLog);
		}
		if (iTo & OUTPUT_DEBUG)
			OutputDebugString(std::format("{}{}{} {}\n", sLeft, sFunction, sRight, sLog).c_str());
		if (iTo & OUTPUT_MENU)
			F::Menu.AddOutput(std::format("{}{}{}", sLeft, sFunction, sRight).c_str(), sLog, tColor);
		if (iTo & OUTPUT_CHAT)
			I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{}{}{}{}\x1 {}", tColor.ToHex(), sLeft, sFunction, sRight, sLog).c_str());
		if (iMessageBox != -1)
			MessageBox(nullptr, sLog, sFunction, iMessageBox);
	}
	else
	{
		if (iTo & OUTPUT_CONSOLE)
			I::CVar->ConsoleColorPrintf(tColor, "%s\n", sFunction);
		if (iTo & OUTPUT_DEBUG)
			OutputDebugString(std::format("{}\n", sFunction).c_str());
		if (iTo & OUTPUT_MENU)
			F::Menu.AddOutput("", sFunction, tColor);
		if (iTo & OUTPUT_CHAT)
			I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{}{}\x1", tColor.ToHex(), sFunction).c_str());
		if (iMessageBox != -1)
			MessageBox(nullptr, "", sFunction, iMessageBox);
	}
}

std::string SDK::GetTime()
{
	time_t tTime = time(nullptr);
	tm timeinfo; localtime_s(&timeinfo, &tTime);
	char buffer[16]; strftime(buffer, sizeof(buffer), "%T", &timeinfo);
	return buffer;
}

std::wstring SDK::ConvertUtf8ToWide(const std::string& source)
{
	int size = MultiByteToWideChar(CP_UTF8, 0, source.data(), -1, nullptr, 0);
	std::wstring result(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, source.data(), -1, result.data(), size);
	return result;
}

double SDK::PlatFloatTime()
{
	static auto Plat_FloatTime = U::Memory.GetModuleExport<double(*)()>("tier0.dll", "Plat_FloatTime");
	return Plat_FloatTime();
}

float SDK::StdRandomFloat(float flMin, float flMax)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> distr(flMin, flMax);
	return distr(gen);
}

int SDK::SeedFileLineHash(int iSeed, const char* sName, int iAdditionalSeed)
{
	CRC32_t iReturn;
	CRC32_Init(&iReturn);
	CRC32_ProcessBuffer(&iReturn, &iSeed, sizeof(int));
	CRC32_ProcessBuffer(&iReturn, &iAdditionalSeed, sizeof(int));
	CRC32_ProcessBuffer(&iReturn, sName, int(strlen(sName)));
	CRC32_Final(&iReturn);
	return static_cast<int>(iReturn);
}

void SDK::RandomSeed(int iSeed)
{
	static auto RandomSeed = U::Memory.GetModuleExport<void(*)(uint32_t)>("vstdlib.dll", "RandomSeed");
	RandomSeed(iSeed);
}

int SDK::RandomInt(int iMinVal, int iMaxVal)
{
	static auto RandomInt = U::Memory.GetModuleExport<int(*)(int, int)>("vstdlib.dll", "RandomInt");
	return RandomInt(iMinVal, iMaxVal);
}

float SDK::RandomFloat(float flMinVal, float flMaxVal)
{
	static auto RandomFloat = U::Memory.GetModuleExport<float(*)(float, float)>("vstdlib.dll", "RandomFloat");
	return RandomFloat(flMinVal, flMaxVal);
}

bool SDK::W2S(const Vec3& vOrigin, Vec3& vScreen, bool bAlways)
{
	const auto& worldToScreen = H::Draw.m_mWorldToProjection.As3x4();

	float flW = worldToScreen[3][0] * vOrigin.x + worldToScreen[3][1] * vOrigin.y + worldToScreen[3][2] * vOrigin.z + worldToScreen[3][3];
	vScreen.z = 0;

	bool bOnScreen = flW > 0.f;
	if (bAlways || bOnScreen)
	{
		const float fl1DBw = 1 / fabs(flW);
		vScreen.x = (H::Draw.m_nScreenW / 2.f) + ((worldToScreen[0][0] * vOrigin.x + worldToScreen[0][1] * vOrigin.y + worldToScreen[0][2] * vOrigin.z + worldToScreen[0][3]) * fl1DBw) * H::Draw.m_nScreenW / 2 + 0.5f;
		vScreen.y = (H::Draw.m_nScreenH / 2.f) - ((worldToScreen[1][0] * vOrigin.x + worldToScreen[1][1] * vOrigin.y + worldToScreen[1][2] * vOrigin.z + worldToScreen[1][3]) * fl1DBw) * H::Draw.m_nScreenH / 2 + 0.5f;
	}

	return bOnScreen;
}

bool SDK::IsOnScreen(CBaseEntity* pEntity, const matrix3x4& mTransform, float* pLeft, float* pRight, float* pTop, float* pBottom, bool bAll)
{
	float flLeft = 0.f, flRight = 0.f, flTop = 0.f, flBottom = 0.f;

	Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	const Vec3 vPoints[] = {
		Vec3(0.f, 0.f, vMins.z),
		Vec3(0.f, 0.f, vMaxs.z),
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f)
	};

	bool bInit = false;
	for (int n = 0; n < 6; n++)
	{
		Vec3 vPoint; Math::VectorTransform(vPoints[n], mTransform, vPoint);

		Vec3 vScreen;
		if (!W2S(vPoint, vScreen))
		{
			if (bAll)
				return false;
			continue;
		}

		flLeft = bInit ? std::min(flLeft, vScreen.x) : vScreen.x;
		flRight = bInit ? std::max(flRight, vScreen.x) : vScreen.x;
		flTop = bInit ? std::max(flTop, vScreen.y) : vScreen.y;
		flBottom = bInit ? std::min(flBottom, vScreen.y) : vScreen.y;
		bInit = true;
	}
	if (!bInit)
		return false;

	if (pLeft) *pLeft = flLeft;
	if (pRight) *pRight = flRight;
	if (pTop) *pTop = flTop;
	if (pBottom) *pBottom = flBottom;

	return !(flRight < 0 || flLeft > H::Draw.m_nScreenW || flTop < 0 || flBottom > H::Draw.m_nScreenH);
}

bool SDK::IsOnScreen(CBaseEntity* pEntity, Vec3 vOrigin, bool bAll)
{
	float flLeft = 0.f, flRight = 0.f, flTop = 0.f, flBottom = 0.f;

	Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	const Vec3 vPoints[] = {
		Vec3(0.f, 0.f, vMins.z),
		Vec3(0.f, 0.f, vMaxs.z),
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f)
	};

	bool bInit = false;
	for (int n = 0; n < 6; n++)
	{
		Vec3 vPoint = vOrigin + vPoints[n];

		Vec3 vScreen;
		if (!W2S(vPoint, vScreen))
		{
			if (bAll)
				return false;
			continue;
		}

		flLeft = bInit ? std::min(flLeft, vScreen.x) : vScreen.x;
		flRight = bInit ? std::max(flRight, vScreen.x) : vScreen.x;
		flTop = bInit ? std::max(flTop, vScreen.y) : vScreen.y;
		flBottom = bInit ? std::min(flBottom, vScreen.y) : vScreen.y;
		bInit = true;
	}
	if (!bInit)
		return false;

	return !(flRight < 0 || flLeft > H::Draw.m_nScreenW || flTop < 0 || flBottom > H::Draw.m_nScreenH);
}

bool SDK::IsOnScreen(CBaseEntity* pEntity, bool bShouldGetOwner)
{
	if (bShouldGetOwner)
	{
		if (auto pOwner = pEntity->m_hOwnerEntity().Get())
			pEntity = pOwner;
	}

	return IsOnScreen(pEntity, pEntity->entindex() == I::EngineClient->GetLocalPlayer() && !I::EngineClient->IsPlayingDemo() ? F::EnginePrediction.m_vOrigin : pEntity->GetAbsOrigin());
}

void SDK::Trace(const Vec3& vStart, const Vec3& vEnd, unsigned int nMask, ITraceFilter* pFilter, CGameTrace* pTrace)
{
	Ray_t ray;
	ray.Init(vStart, vEnd);
	I::EngineTrace->TraceRay(ray, nMask, pFilter, pTrace);

}

void SDK::TraceHull(const Vec3& vStart, const Vec3& vEnd, const Vec3& vHullMin, const Vec3& vHullMax, unsigned int nMask, ITraceFilter* pFilter, CGameTrace* pTrace)
{
	Ray_t ray;
	ray.Init(vStart, vEnd, vHullMin, vHullMax);
	I::EngineTrace->TraceRay(ray, nMask, pFilter, pTrace);

}

bool SDK::VisPos(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterHitscan filter = {};
	filter.pSkip = pSkip;
	Trace(vFrom, vTo, nMask, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt && trace.m_pEnt == pEntity;
	return true;
}
bool SDK::VisPosCollideable(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterCollideable filter = {};
	filter.pSkip = pSkip;
	filter.iType = SKIP_CHECK;
	Trace(vFrom, vTo, nMask, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt && trace.m_pEnt == pEntity;
	return true;
}
bool SDK::VisPosWorld(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};
	filter.pSkip = pSkip;
	Trace(vFrom, vTo, nMask, &filter, &trace);
	if (trace.DidHit())
		return trace.m_pEnt && trace.m_pEnt == pEntity;
	return true;
}

Vec3 SDK::PredictOrigin(Vec3& vOrigin, Vec3 vVelocity, float flLatency, bool bTrace, Vec3 vMins, Vec3 vMaxs, unsigned int nMask, float flNormal)
{
	if (vVelocity.IsZero() || !flLatency)
		return vOrigin;

	Vec3 vOut = vOrigin + vVelocity * flLatency;
	if (!bTrace)
		return vOut;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	SDK::TraceHull(vOrigin, vOut, vMins, vMaxs, nMask, &filter, &trace);
	return trace.endpos + (flNormal ? trace.plane.normal * flNormal : Vec3());
}

bool SDK::PredictOrigin(Vec3& vOut, Vec3& vOrigin, Vec3 vVelocity, float flLatency, bool bTrace, Vec3 vMins, Vec3 vMaxs, unsigned int nMask, float flNormal)
{
	vOut = vOrigin;
	if (vVelocity.IsZero() || !flLatency)
		return true;

	vOut = vOrigin + vVelocity * flLatency;
	if (!bTrace)
		return true;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	SDK::TraceHull(vOrigin, vOut, vMins, vMaxs, nMask, &filter, &trace);
	vOut = trace.endpos + (flNormal ? trace.plane.normal * flNormal : Vec3());
	return !trace.DidHit();
}

bool SDK::IsLoopback()
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	return pNetChan && pNetChan->IsLoopback();
}

// CS:GO/CSS has no secondary-weapon-type concept (that was TF2's melee-with-a-ranged-alt-fire,
// e.g. Bat/Giftwrap) -- pSecondaryType always comes back UNKNOWN. Classification is just
// css_weapons.h's CSWeaponType: knife = MELEE (no aimbot path at all), flash/HE/smoke grenades =
// PROJECTILE (handled entirely by grenade_aim.cpp, not aim_hitscan), C4 = UNKNOWN (not a weapon
// the aimbot ever holds-and-fires), every gun = HITSCAN.
EWeaponType SDK::GetWeaponType(CWeaponCSBase* pWeapon, EWeaponType* pSecondaryType)
{
	if (pSecondaryType)
		*pSecondaryType = EWeaponType::UNKNOWN;
	if (!pWeapon)
		return EWeaponType::UNKNOWN;

	switch (CSS_WeaponTypeFromID(pWeapon->GetWeaponID()))
	{
	case WEAPONTYPE_KNIFE: return EWeaponType::MELEE;
	case WEAPONTYPE_GRENADE: return EWeaponType::PROJECTILE;
	case WEAPONTYPE_C4:
	case WEAPONTYPE_UNKNOWN: return EWeaponType::UNKNOWN;
	default: return EWeaponType::HITSCAN;
	}
}

int SDK::IsAttacking(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const CUserCmd* pCmd, bool bTickBase)
{
	(void)bTickBase;   // CSS path never needs a tickbase-vs-curtime distinction (no charge weapons left, see below)
	if (!pLocal || !pWeapon || pCmd->weaponselect)
		return false;

	// CSS has none of TF2's charge/spinup/throw-delay weapons (sticky launcher charge, minigun
	// spinup, flamethrower airblast charge, cleaver/jar throw windup, grapplinghook, beggar's
	// bazooka auto-load...) -- every CS gun fires the instant IN_ATTACK is down and ammo allows
	// (weapon_csbasegun.cpp PrimaryAttack has no charge/windup state at all). Knife is its own
	// path only because it has no ammo/reload gate to check.
	if (pWeapon->GetWeaponID() == WEAPON_KNIFE)
		return G::CanPrimaryAttack && (pCmd->buttons & IN_ATTACK) != 0;

	// pistols are semi-auto: PrimaryAttack bails while m_iShotsFired > 0 (weapon_glock.cpp:244 etc),
	// it only resets on a command with no fire button (weapon_csbase.cpp:578)
	if (pWeapon->IsPistol() && pLocal->m_iShotsFired() > 0)
		return G::Reloading && pCmd->buttons & IN_ATTACK ? 2 : 0;

	return G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK ? 1 : G::Reloading && pCmd->buttons & IN_ATTACK ? 2 : 0;
}

// CSS has no speed-boost condition (no m_nPlayerCond at all) and no CalculateMaxSpeed() vfunc
// (that's CTFPlayer's) -- m_flMaxspeed (CBasePlayer netvar, defs.h) is the server's own already-
// computed current max speed (weapon-carry, flash, etc. per cs_gamemovement.cpp folded in); the
// duck divide-by-3 below matches CBasePlayer::GetPlayerMaxSpeed's crouch handling, kept as-is.
float SDK::MaxSpeed(CCSPlayer* pPlayer, bool bIncludeCrouch, bool bIgnoreSpecialAbility)
{
	(void)bIgnoreSpecialAbility;   // CSS has no special-ability speed modifier to ignore
	float flSpeed = pPlayer->m_flMaxspeed();

	if (bIncludeCrouch && pPlayer->IsDucking() && pPlayer->IsOnGround())
		flSpeed /= 3;

	return flSpeed;
}

float SDK::AttribHookValue(float value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
	return value; // CSS_STUB tf2 api
}

void SDK::FixMovement(CUserCmd* pCmd, const Vec3& vCurAngle, const Vec3& vTargetAngle)
{
	bool bCurOOB = fabsf(Math::NormalizeAngle(vCurAngle.x)) > 90.f;
	bool bTargetOOB = fabsf(Math::NormalizeAngle(vTargetAngle.x)) > 90.f;

	Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove * (bCurOOB ? -1 : 1), pCmd->upmove};
	float flSpeed = vMove.Length2D();
	Vec3 vMoveAng = Math::VectorAngles(vMove);

	float flCurYaw = vCurAngle.y + (bCurOOB ? 180.f : 0.f);
	float flTargetYaw = vTargetAngle.y + (bTargetOOB ? 180.f : 0.f);
	float flYaw = DEG2RAD(flTargetYaw - flCurYaw + vMoveAng.y);

	pCmd->forwardmove = cos(flYaw) * flSpeed;
	pCmd->sidemove = sin(flYaw) * flSpeed * (bTargetOOB ? -1 : 1);
}

void SDK::FixMovement(CUserCmd* pCmd, const Vec3& vTargetAngle)
{
	FixMovement(pCmd, pCmd->viewangles, vTargetAngle);
}

Vec3 SDK::ComputeMove(const CUserCmd* pCmd, CCSPlayer* pLocal, Vec3& vFrom, Vec3& vTo)
{
	const Vec3 vDiff = vTo - vFrom;
	if (!vDiff.Length2D())
		return {};

	Vec3 vSilent = vDiff.To2D();
	Vec3 vAngle = Math::VectorAngles(vSilent);
	const float flYaw = DEG2RAD(vAngle.y - pCmd->viewangles.y);
	const float flPitch = DEG2RAD(vAngle.x - pCmd->viewangles.x);

	Vec3 vMove = { cos(flYaw) * 450.f, -sin(flYaw) * 450.f, -cos(flPitch) * 450.f };
	if (!(I::EngineTrace->GetPointContents(pLocal->GetShootPos()) & CONTENTS_WATER)) // only apply upmove in water
		vMove.z = pCmd->upmove;

	return vMove;
}

void SDK::WalkTo(CUserCmd* pCmd, CCSPlayer* pLocal, Vec3& vFrom, Vec3& vTo, float flScale)
{
	const auto vResult = ComputeMove(pCmd, pLocal, vFrom, vTo);

	pCmd->forwardmove = vResult.x * flScale;
	pCmd->sidemove = vResult.y * flScale;
	pCmd->upmove = vResult.z * flScale;
}

void SDK::WalkTo(CUserCmd* pCmd, CCSPlayer* pLocal, Vec3& vTo, float flScale)
{
	Vec3 vLocalPos = pLocal->m_vecOrigin();
	WalkTo(pCmd, pLocal, vLocalPos, vTo, flScale);
}

void SDK::GetProjectileFireSetup(CCSPlayer* pPlayer, const Vec3& vAngIn, Vec3 vOffset, Vec3& vPosOut, Vec3& vAngOut, bool bPipes, bool bInterp, bool bAllowFlip)
{
	static auto cl_flipviewmodels = H::ConVars.FindVar("cl_flipviewmodels");
	if (bAllowFlip && FNV1A::Hash32(cl_flipviewmodels->GetString()) == FNV1A::Hash32Const("1"))
		vOffset.y *= -1.f;

	const Vec3 vShootPos = bInterp ? pPlayer->GetEyePosition() : pPlayer->GetShootPos();

	Vec3 vForward, vRight, vUp; Math::AngleVectors(vAngIn, &vForward, &vRight, &vUp);
	vPosOut = vShootPos + (vForward * vOffset.x) + (vRight * vOffset.y) + (vUp * vOffset.z);

	if (bPipes)
		vAngOut = vAngIn;
	else
	{
		Vec3 vEndPos = vShootPos + vForward * 2000.f;

		CGameTrace trace = {};
		CTraceFilterCollideable filter = {};
		filter.pSkip = pPlayer;
		filter.iType = SKIP_CHECK;
		Trace(vShootPos, vEndPos, MASK_SOLID, &filter, &trace);
		if (trace.DidHit() && trace.fraction > 0.1f)
			vEndPos = trace.endpos;

		vAngOut = Math::VectorAngles(vEndPos - vPosOut);
	}
}

bool SDK::CleanScreenshot()
{
	return Vars::Visuals::UI::CleanScreenshots.Value && I::EngineClient->IsTakingScreenshot();
}