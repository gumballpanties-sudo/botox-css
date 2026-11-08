#pragma once
#include "sdk.h"
#include "server_lerp.h"

// The two things every aim path needs from a target, with no tick history behind them:
//
//   TickRecord  - the bone-matrix container the aim paths pass around. They only ever read
//                 m_aBones / m_flSimTime / m_vOrigin off it. Each target is judged on ONE
//                 record: its live bones.
//
//   AimLatency  - the real (measured) latency in ticks. Projectile lead and auto-detonate
//                 read it.
//
// Anticipated choke is 0 here: nothing in this build chokes commands on purpose.

struct TickRecord
{
	float m_flSimTime = 0.f;
	Vec3 m_vOrigin = {};
	Vec3 m_vMins = {};
	Vec3 m_vMaxs = {};
	bool m_bOnShot = false;
	bool m_bInvalid = false;
	matrix3x4 m_aBones[MAXSTUDIOBONES];
};

// Scratch record the aim paths fill with the target's live bones. Single-threaded: filled and
// consumed inside one CanHit call, never held across one.
inline TickRecord g_tLiveRecord = {};

inline float AimLatency(int iFlow = MAX_FLOWS)
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return 0.f;

	if (iFlow != MAX_FLOWS)
		return pNetChan->GetLatency(iFlow);
	return pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING);
}

// m_fLerpTime as the SERVER computes it from our userinfo (player.cpp:3484-3515), feeding
// hitscan's backtrack tick_count write (aim_hitscan.cpp) and backtrack.cpp's window.
inline float BacktrackLerp()
{
	static auto cl_interpolate = H::ConVars.FindVar("cl_interpolate");
	static auto cl_interp = H::ConVars.FindVar("cl_interp");
	static auto cl_interp_ratio = H::ConVars.FindVar("cl_interp_ratio");
	static auto cl_updaterate = H::ConVars.FindVar("cl_updaterate");
	static auto sv_minupdaterate = H::ConVars.FindVar("sv_minupdaterate");
	static auto sv_maxupdaterate = H::ConVars.FindVar("sv_maxupdaterate");
	static auto sv_min_ratio = H::ConVars.FindVar("sv_client_min_interp_ratio");
	static auto sv_max_ratio = H::ConVars.FindVar("sv_client_max_interp_ratio");
	return Lerp::ServerLerp(
		cl_interpolate ? cl_interpolate->GetInt() != 0 : true,
		cl_interp ? cl_interp->GetFloat() : 0.1f,
		cl_interp_ratio ? cl_interp_ratio->GetFloat() : 1.f,
		cl_updaterate ? cl_updaterate->GetFloat() : 66.f,
		sv_minupdaterate ? sv_minupdaterate->GetFloat() : 0.f,
		sv_maxupdaterate ? sv_maxupdaterate->GetFloat() : 0.f,
		sv_min_ratio && sv_max_ratio ? sv_min_ratio->GetFloat() : -1.f,
		sv_max_ratio ? sv_max_ratio->GetFloat() : 5.f);
}

// correct, exactly as player_lagcompensation.cpp:379-398 builds it: FLOW_OUTGOING latency (not
// round-trip) + lerp, clamped to [0, sv_maxunlag]. This is the real window the server's own
// 0.2s sanity check (lagcompensation.cpp:402-412) will accept a requested tick_count inside.
inline float BacktrackCorrect()
{
	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
	float flCorrect = AimLatency(FLOW_OUTGOING) + BacktrackLerp();
	return std::clamp(flCorrect, 0.f, sv_maxunlag ? sv_maxunlag->GetFloat() : 0.2f);
}
