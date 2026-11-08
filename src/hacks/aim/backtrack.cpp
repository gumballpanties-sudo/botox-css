#include "backtrack.h"
#include "aim_global.h"
#include "backtrack_math.h"
#include "css_weapons.h"
#include "../visuals/chams_api.h"
#include "../menu/cfg/u_misctab.h"   // g_perfSkipUnusedRecords (primitives only)

void DebugLog(const char* fmt, ...);

bool CBacktrack::Enabled() { return Vars::Aimbot::Backtrack::Enabled.Value; }
bool CBacktrack::AimAt() { return Enabled() && Vars::Aimbot::Backtrack::AimAt.Value; }

// botox lerp_time = the server's m_fLerpTime off our userinfo. aim_record.h already has the TF2
// server's own formula (player.cpp), shared with the backstab rewind.
float CBacktrack::LerpTime() { return BacktrackLerp(); }

float CBacktrack::MaxUnlag()
{
	// remote sv_maxunlag is never sent to clients; local copy is only real when we host.
	// TF2 default 1.0 (player_lagcompensation.cpp:34; botox: csgo 0.2)
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (pNetChan && !pNetChan->IsLoopback())
		return 1.f;

	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
	return sv_maxunlag ? sv_maxunlag->GetFloat() : 1.f;
}

float CBacktrack::RealLatency() { return AimLatency(); }   // FLOW_OUTGOING + FLOW_INCOMING

float CBacktrack::TimeLimit()
{
	return std::clamp(Vars::Aimbot::Backtrack::TimeLimit.Value / 1000.f, 0.f, kMaxBacktrack);
}

float CBacktrack::CorrectTime() { return std::clamp(RealLatency() + LerpTime(), 0.f, MaxUnlag()); }

float CBacktrack::WindowCenter() { return CorrectTime(); }

float CBacktrack::ServerTime() { return TICKS_TO_TIME(m_iServerTick); }

void CBacktrack::BeginCommand(CUserCmd* pCmd, CCSPlayer* pLocal)
{
	if (pLocal && (!m_pLastCmd || m_pLastCmd->hasbeenpredicted))
		m_iServerTick = pLocal->m_nTickBase();
	else
		m_iServerTick++;

	m_pLastCmd = pCmd;
	m_eShot = EShot::None;
	m_pTarget = nullptr;
}

void CBacktrack::RefreshWindow()
{
	if (const int iFrame = I::GlobalVars->framecount; iFrame != m_iWindowFrame)
	{
		m_iWindowFrame = iFrame;
		m_flWindowCenter = WindowCenter();
		m_flWindowUnlag = MaxUnlag();
		m_flWindowLimit = TimeLimit();
	}
}

bool CBacktrack::IsValid(float flSimTime, float flMargin)
{
	RefreshWindow();
	return bt::WindowValid(flSimTime, ServerTime(), flMargin, m_flWindowUnlag, m_flWindowCenter);
}

bool CBacktrack::Usable(CBaseEntity* pEntity, float flSimTime)
{
	if (!pEntity)
		return false;

	RefreshWindow();
	return bt::Usable(flSimTime, pEntity->m_flSimulationTime(), ServerTime(), kEdgeMargin, m_flWindowUnlag, m_flWindowCenter, m_flWindowLimit);
}

void CBacktrack::CommitShot(TickRecord* pRecord, CUserCmd* pCmd)
{
	if (!pRecord)
	{
		m_eShot = EShot::Live;
		return;
	}

	m_eShot = EShot::Record;
	m_pTarget = pRecord;
	pCmd->tick_count = TIME_TO_TICKS(pRecord->m_flSimTime + LerpTime());
}

bool CBacktrack::RecordInFOV(CBaseEntity* pEntity, const Vec3& vEye, const Vec3& vAngles, float flFOV, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo)
{
	auto pList = Enabled() && pEntity ? GetRecords(pEntity->entindex()) : nullptr;
	if (!pList)
		return false;

	bool bFound = false;
	const Vec3 vHalf = (pEntity->m_vecMins() + pEntity->m_vecMaxs()) / 2;
	for (int i = 0; i < m_iMaxAllocations; i++)
	{
		if (!Usable(pEntity, pList[i].m_flSimTime))
			continue;

		const Vec3 vCenter = pList[i].m_vOrigin + vHalf, vAngle = Math::CalcAngle(vEye, vCenter);
		if (const float flFov = Math::CalcFov(vAngles, vAngle); flFov <= flFOV && (!bFound || flFov < flFOVTo))
			bFound = true, flFOVTo = flFov, vPos = vCenter, vAngleTo = vAngle;
	}
	return bFound;
}

TickRecord* CBacktrack::OldestRecord(CBaseEntity* pEntity)
{
	auto pList = pEntity ? GetRecords(pEntity->entindex()) : nullptr;
	if (!pList)
		return nullptr;

	TickRecord* pOldest = nullptr;
	for (int i = 0; i < m_iMaxAllocations; i++)
		if (Usable(pEntity, pList[i].m_flSimTime) && (!pOldest || pList[i].m_flSimTime < pOldest->m_flSimTime))
			pOldest = &pList[i];
	return pOldest;
}

// GetInterpolationAmount (c_baseentity.cpp): the model draws lerp + 1 tick behind
float CBacktrack::ModelTime()
{
	return I::GlobalVars->curtime - TICKS_TO_TIME(TIME_TO_TICKS(LerpTime()) + 1) + 1e-3f;
}

// botox prediction.cpp update(): ring slots in use = the window in ticks
void CBacktrack::UpdateAllocations()
{
	if (!I::EngineClient->GetNetChannelInfo())
	{
		m_iMaxAllocations = 0;
		return;
	}

	const float flWindow = std::max(TimeLimit(), WindowCenter() + 0.2f);
	m_iMaxAllocations = std::clamp(TIME_TO_TICKS(std::min(flWindow, MaxUnlag() + 0.2f)), 3, kMaxRecords);
}

void CBacktrack::Reset(int iIndex)
{
	if (auto pList = m_aRecords[iIndex])
		for (int k = 0; k < kMaxRecords; k++)
			pList[k].m_flSimTime = 0.f;
	m_aLocation[iIndex] = 0;
}

void CBacktrack::Store()
{
	UpdateAllocations();

	const bool bChams = Chams_BacktrackActive();
	// botox lagcomp.cpp:191 m_performance/m_performance_skip_unused_records: OFF (default) keeps
	// building every tick regardless of whether backtrack/chams currently read them, so a warm
	// record history already exists the instant backtrack gets toggled on mid-fight; ON trims it
	// to only-when-consumed. No CSS "players backtrack trail" ESP var exists in this tree (not
	// ported), so unlike botox's 3-way OR this is just Enabled() || chams.
	const bool bNeeded = Enabled() || bChams;
	if ((g_perfSkipUnusedRecords && !bNeeded) || m_iMaxAllocations <= 0 || !I::EngineClient->IsInGame())
		return;
	// ghosts render these bones = every bone (botox 0x7FF00); aim alone only reads hitbox bones
	const int iMask = bChams ? BONE_USED_BY_ANYTHING : BONE_USED_BY_HITBOX;

	const int iLocal = I::EngineClient->GetLocalPlayer();
	for (auto pEntity : H::Entities.GetGroup(Vars::Aimbot::General::TargetTeam.Value ? EntityEnum::PlayerAll : EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CCSPlayer>();
		const int iIndex = pPlayer->entindex();
		if (iIndex <= 0 || iIndex >= 65 || iIndex == iLocal || pPlayer->IsDormant() || !pPlayer->IsAlive())
			continue;

		auto& pList = m_aRecords[iIndex];
		auto& iLocation = m_aLocation[iIndex];

		if (!pList)
		{
			pList = new TickRecord[kMaxRecords];
			iLocation = 0;
		}

		if (iLocation >= m_iMaxAllocations)
			iLocation = 0;

		const int iPrevious = (iLocation > 0 ? iLocation : std::max(1, m_iMaxAllocations)) - 1;
		const float flSimTime = pPlayer->m_flSimulationTime();
		if (pList[iPrevious].m_flSimTime == flSimTime)
			continue;

		const Vec3 vOrigin = pPlayer->m_vecOrigin();

		// server breaks lag comp past 64u in one update (sv_lagcompensation_teleport_dist)
		if (const auto& tPrev = pList[iPrevious]; tPrev.m_flSimTime > 0.f && (vOrigin - tPrev.m_vOrigin).LengthSqr() > 64.f * 64.f)
			Reset(iIndex);

		// botox: abs origin = network origin + forced rebuild, else setup_bones hands back the
		// cached (last drawn, interpolated) pose labelled with this tick's sim time. TF2: the
		// cache key is m_iMostRecentModelBoneCounter, InvalidateBoneCache drops it.
		auto& tRecord = pList[iLocation];
		const Vec3 vSavedAbs = pPlayer->GetAbsOrigin();
		pPlayer->SetAbsOrigin(vOrigin);
		pPlayer->InvalidateBoneCache();
		// HITBOX unless chams: ANYTHING upgrades the render path's mask for good, see PlayerBoneInFOV
		const bool bBuilt = pPlayer->SetupBones(tRecord.m_aBones, MAXSTUDIOBONES, iMask, flSimTime);
		pPlayer->InvalidateBoneCache();   // render rebuilds its own pose
		pPlayer->SetAbsOrigin(vSavedAbs);

		if (!bBuilt)
		{
			tRecord.m_flSimTime = 0.f;
			continue;
		}

		tRecord.m_flSimTime = flSimTime;
		tRecord.m_vOrigin = vOrigin;
		tRecord.m_vMins = pPlayer->m_vecMins();
		tRecord.m_vMaxs = pPlayer->m_vecMaxs();
		tRecord.m_bOnShot = tRecord.m_bInvalid = false;

		iLocation++;
	}
}

// botox ray_segment_distance_sq
static float RaySegmentDistSq(const Vec3& vEye, const Vec3& vDir, float flRange, const Vec3& a, const Vec3& b)
{
	const Vec3 vAxis = b - a;
	const Vec3 vOffset = vEye - a;

	const float e = vAxis.LengthSqr(), f = vAxis.Dot(vOffset), c = vDir.Dot(vOffset), d = vDir.Dot(vAxis);

	float s = 0.f, t = 0.f;
	if (e <= 1e-4f)
		s = std::clamp(-c, 0.f, flRange);
	else
	{
		const float flDenom = e - d * d;
		s = flDenom > 1e-4f ? std::clamp((d * f - c * e) / flDenom, 0.f, flRange) : 0.f;
		t = (d * s + f) / e;
		if (t < 0.f)
		{
			t = 0.f;
			s = std::clamp(-c, 0.f, flRange);
		}
		else if (t > 1.f)
		{
			t = 1.f;
			s = std::clamp(d - c, 0.f, flRange);
		}
	}

	return ((vEye + vDir * s) - (a + vAxis * t)).LengthSqr();
}

// botox ray_depth: max over hitboxes of (radius - ray distance), > 0 = the ray is inside one.
// botox hitboxes are capsules; TF2's are boxes. ponytail: each box = the capsule along its long
// axis (radius = mean of the other two half sizes). Corners are off by ~0.3 of a half size;
// a true ray/OBB distance if the pick ever lands on the wrong record.
static float RayDepth(mstudiohitboxset_t* pSet, const matrix3x4* aBones, const Vec3& vEye, const Vec3& vDir, float flRange, int& iHitbox)
{
	float flBest = -1e9f;
	for (int i = 0; i < pSet->numhitboxes; i++)
	{
		auto pBox = pSet->pHitbox(i);
		if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES)
			continue;

		const Vec3 vMin = pBox->bbmin, vMax = pBox->bbmax;
		const Vec3 vHalf = (vMax - vMin) / 2, vCenter = (vMax + vMin) / 2;
		const int iAxis = vHalf.x >= vHalf.y && vHalf.x >= vHalf.z ? 0 : vHalf.y >= vHalf.z ? 1 : 2;
		const float flRadius = (vHalf.x + vHalf.y + vHalf.z - vHalf[iAxis]) / 2;
		Vec3 vExtent; vExtent[iAxis] = std::max(vHalf[iAxis] - flRadius, 0.f);

		Vec3 a, b;
		Math::VectorTransform(vCenter - vExtent, aBones[pBox->bone], a);
		Math::VectorTransform(vCenter + vExtent, aBones[pBox->bone], b);

		if (const float flDepth = flRadius - sqrtf(RaySegmentDistSq(vEye, vDir, flRange, a, b)); flDepth > flBest)
		{
			flBest = flDepth;
			iHitbox = i;
		}
	}
	return flBest;
}

// botox aimbot.cpp run_backtrack (clarity FUN_3c531170): every fire cmd the aimbot did not
// commit goes out on the tick of the candidate (live or record) nearest the bullet.
void CBacktrack::RunBacktrack(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd)
{
	if (!Enabled() || m_eShot != EShot::None || !pLocal || !pWeapon || G::Attacking != 1)
		return;
	if (G::PrimaryWeaponType != EWeaponType::HITSCAN)
		return;
	// TF2's medigun/laser-pointer exclusion deleted: CSS has neither weapon.

	// weapon_csbasegun.cpp CSBaseGunFire -> FX_FireBullets(EyeAngles() + 2.0f*GetPunchAngle()),
	// verified exact (was 1x, TF2's own multiplier).
	Vec3 vDir; Math::AngleVectors(pCmd->viewangles + pLocal->m_vecPunchAngle() * 2.f, &vDir);
	const Vec3 vEye = pLocal->GetShootPos();
	// CWeaponCSBase has no GetRange() vfunc/netvar -- m_flRange is script data (css_weapons.h).
	auto pRangeData = CSWeaponScript(pWeapon->GetWeaponID());
	const float flRange = pRangeData ? pRangeData->m_flRange : 8192.f;
	constexpr float kBodyCenter = 36.f;   // botox k_body_center (TF2 hull 82u: ~half)

	struct { CBaseEntity* m_pEntity = nullptr; TickRecord* m_pRecord = nullptr; float m_flScore = -1e9f, m_flDepth = 0.f; int m_iHitbox = -1; } tBest;
	int iRecords = 0, iUsable = 0;

	for (auto pEntity : H::Entities.GetGroup(Vars::Aimbot::General::TargetTeam.Value ? EntityEnum::PlayerAll : EntityEnum::PlayerEnemy))
	{
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
			continue;

		auto pModel = pEntity->GetModel();
		auto pHDR = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		auto pSet = pHDR ? pHDR->pHitboxSet(pEntity->As<CBaseAnimating>()->m_nHitboxSet()) : nullptr;
		if (!pSet)
			continue;

		auto Consider = [&](TickRecord* pRecord, const matrix3x4* aBones, const Vec3& vOrigin)
		{
			int iHitbox = -1;
			const float flDepth = RayDepth(pSet, aBones, vEye, vDir, flRange, iHitbox);
			const float flScore = flDepth / std::max((vOrigin + Vec3(0.f, 0.f, kBodyCenter) - vEye).Length(), 1.f);
			if (iHitbox >= 0 && flScore > tBest.m_flScore)
				tBest = { pEntity, pRecord, flScore, flDepth, iHitbox };
		};

		// live bones: same call CanHit makes
		if (pEntity->SetupBones(g_tLiveRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, pEntity->m_flSimulationTime()))
			Consider(nullptr, g_tLiveRecord.m_aBones, pEntity->m_vecOrigin());

		auto pList = GetRecords(pEntity->entindex());
		if (!pList)
			continue;

		for (int i = 0; i < m_iMaxAllocations; i++)
		{
			auto pRecord = &pList[i];
			if (pRecord->m_flSimTime <= 0.f)
				continue;

			iRecords++;
			if (!Usable(pEntity, pRecord->m_flSimTime))
				continue;

			iUsable++;
			Consider(pRecord, pRecord->m_aBones, pRecord->m_vOrigin);
		}
	}

	if (!tBest.m_pEntity)
	{
		if (iRecords > 0)
			DebugLog("BT: none recs=%d ok=%d centre=%.0fms", iRecords, iUsable, WindowCenter() * 1000.f);
		return;
	}

	CommitShot(tBest.m_pRecord, pCmd);

	DebugLog("BT: pick=%s ent=%d hb=%d age=%.0fms depth=%.1f recs=%d ok=%d tick=%d", tBest.m_pRecord ? "rec" : "live",
		tBest.m_pEntity->entindex(), tBest.m_iHitbox, tBest.m_pRecord ? (ServerTime() - tBest.m_pRecord->m_flSimTime) * 1000.f : -1.f,
		tBest.m_flDepth, iRecords, iUsable, pCmd->tick_count);
}

// RunMeleeSmack REMOVED -- see backtrack.h.

void CBacktrack::Release()
{
	for (int i = 0; i < 65; i++)
	{
		delete[] m_aRecords[i];
		m_aRecords[i] = nullptr;
		m_aLocation[i] = 0;
	}
	m_pLastCmd = nullptr;
	m_pTarget = nullptr;
}
