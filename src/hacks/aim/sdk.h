#pragma once

#include "vars.h"
#include "globals.h"
#include "helpers.h"
#include "defs.h"
#include "utils.h"
#include <intrin.h>

// Console/toast/menu output colours follow the menu accent so a single accent change recolours everything.

template <typename T> int sign(T val)
{
	return (val > T(0)) - (val < T(0));
}

#define OUTPUT_CONSOLE 1 << 0
#define OUTPUT_DEBUG 1 << 1
#define OUTPUT_MENU 1 << 3
#define OUTPUT_CHAT 1 << 4

namespace SDK
{
	void Output(const char* sFunction, const char* sLog = nullptr, Color_t tColor = { 255, 255, 255, 255 },
		int iTo = OUTPUT_CONSOLE, int iMessageBox = -1,
		const char* sLeft = "[", const char* sRight = "]");

	std::string GetTime();

	std::wstring ConvertUtf8ToWide(const std::string& source);

	double PlatFloatTime();
	float StdRandomFloat(float flMin, float flMax);

	int SeedFileLineHash(int iSeed, const char* sName, int iAdditionalSeed);
	void RandomSeed(int iSeed);
	int RandomInt(int iMinVal = 0, int iMaxVal = VALVE_RAND_MAX);
	float RandomFloat(float flMinVal = 0.f, float flMaxVal = 1.f);

	bool W2S(const Vec3& vOrigin, Vec3& vScreen, bool bAlways = false);
	bool IsOnScreen(CBaseEntity* pEntity, const matrix3x4& mTransform, float* pLeft = nullptr, float* pRight = nullptr, float* pTop = nullptr, float* pBottom = nullptr, bool bAll = false);
	bool IsOnScreen(CBaseEntity* pEntity, Vec3 vOrigin, bool bAll = false);
	bool IsOnScreen(CBaseEntity* pEntity, bool bShouldGetOwner = true);

	void Trace(const Vec3& vStart, const Vec3& vEnd, unsigned int nMask, ITraceFilter* pFilter, CGameTrace* pTrace);
	void TraceHull(const Vec3& vStart, const Vec3& vEnd, const Vec3& vHullMin, const Vec3& vHullMax, unsigned int nMask, ITraceFilter* pFilter, CGameTrace* pTrace);

	bool VisPos(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask = MASK_SHOT | CONTENTS_GRATE);
	bool VisPosCollideable(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask = MASK_SHOT | CONTENTS_GRATE);
	bool VisPosWorld(CBaseEntity* pSkip, const CBaseEntity* pEntity, const Vec3& vFrom, const Vec3& vTo, unsigned int nMask = MASK_SHOT | CONTENTS_GRATE);

	Vec3 PredictOrigin(Vec3& vOrigin, Vec3 vVelocity, float flLatency, bool bTrace = true, Vec3 vMins = {}, Vec3 vMaxs = {}, unsigned int nMask = MASK_SOLID, float flNormal = 0.f);
	bool PredictOrigin(Vec3& vOut, Vec3& vOrigin, Vec3 vVelocity, float flLatency, bool bTrace = true, Vec3 vMins = {}, Vec3 vMaxs = {}, unsigned int nMask = MASK_SOLID, float flNormal = 0.f);

	bool IsLoopback();
	EWeaponType GetWeaponType(CWeaponCSBase* pWeapon, EWeaponType* pSecondaryType = nullptr);

	int IsAttacking(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, const CUserCmd* pCmd, bool bTickBase = false);
	float MaxSpeed(CCSPlayer* pPlayer, bool bIncludeCrouch = false, bool bIgnoreSpecialAbility = false);
	float AttribHookValue(float value, const char* name, void* econent, void* buffer = nullptr, bool isGlobalConstString = true);

	void FixMovement(CUserCmd* pCmd, const Vec3& vCurAngle, const Vec3& vTargetAngle);
	void FixMovement(CUserCmd* pCmd, const Vec3& vTargetAngle);

	Vec3 ComputeMove(const CUserCmd* pCmd, CCSPlayer* pLocal, Vec3& vFrom, Vec3& vTo);
	void WalkTo(CUserCmd* pCmd, CCSPlayer* pLocal, Vec3& vFrom, Vec3& vTo, float flScale = 1.f);
	void WalkTo(CUserCmd* pCmd, CCSPlayer* pLocal, Vec3& vTo, float flScale = 1.f);

	void GetProjectileFireSetup(CCSPlayer* pPlayer, const Vec3& vAngIn, Vec3 vOffset, Vec3& vPosOut, Vec3& vAngOut, bool bPipes = false, bool bInterp = false, bool bAllowFlip = true);

	bool CleanScreenshot();
}