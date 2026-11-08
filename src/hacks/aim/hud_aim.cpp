#include "hud_aim.h"
#include "sdk.h"
#include "css_weapons.h"  // CSWeaponIsSniperClass, CSSIsScoped
#include "aimbot.h"       // F::Aimbot.m_bToggled: the aim key's toggle latch
#include "aim_global.h"   // WeaponSettings: per-class fov
#include "aim_record.h"   // BacktrackLerp: m_fLerpTime as the server computes it
#include "../hud/hud_snap.h"
#include "../menu/cfg/u_hud.h"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

void DebugLog(const char* fmt, ...);   // dllmain.cpp

// ============================================================================================
// Unit hud, game thread half. botox reads these straight off g_ctx/g_interfaces inside its
// paint; here the frame may be built on the render thread, so the numbers are taken where the
// typed SDK is (IEngineVGui::Paint, hooks.cpp) and published as a POD copy (hud/hud_snap.h).
//   fov circle        botox aimbot_fov_circle (projection probe, same 5 deg / 8192 u)
//   net graph         botox net_graph: CNetGraphPanel::GetFrameData's inputs
// ============================================================================================

namespace
{
	void Copy(char* dst, size_t n, const char* s) { strncpy_s(dst, n, s ? s : "", _TRUNCATE); }

	// ---- net_graphtext: engine text off while ours draws (botox stored_text) -------------------
	// Raw value write, not SetValue: net_graphtext is FCVAR_ARCHIVE, and DrawTextFields reads
	// GetBool (m_nValue). The string keeps the player's value, so a config save mid-session
	// writes their setting back, never our 0.
	ConVar* s_ngText = nullptr;
	int s_ngStored = INT_MIN;

	void NgTextPin(bool on)
	{
		if (!s_ngText) s_ngText = H::ConVars.FindVar("net_graphtext");
		if (!s_ngText || !s_ngText->m_pParent) return;
		ConVar* p = s_ngText->m_pParent;
		if (on)
		{
			if (s_ngStored == INT_MIN) s_ngStored = p->m_nValue;
			p->m_nValue = 0; p->m_fValue = 0.f;
		}
		else if (s_ngStored != INT_MIN)
		{
			p->m_nValue = s_ngStored; p->m_fValue = float(s_ngStored);
			s_ngStored = INT_MIN;
		}
	}

	// botox aimbot_fov_circle: project a point 5 deg above the view and measure it, so the
	// radius follows fov / aspect / viewmodel-fov changes without knowing any of them
	float FovRadius(CCSPlayer* pLocal, CWeaponCSBase* pWeapon)
	{
		if (!Vars::Aimbot::General::Enabled.Value || !pLocal->IsAlive())
			return 0.f;

		if (g_hudFovCircleKeyOnly)
		{
			const int iKey = Vars::Aimbot::General::AimKey.Value;
			const int iMode = Vars::Aimbot::General::AimKeyMode.Value;
			if (iKey && iMode == Vars::Aimbot::General::AimKeyModeEnum::OnHotkey && !(GetAsyncKeyState(iKey) & 0x8000))
				return 0.f;
			if (iKey && iMode == Vars::Aimbot::General::AimKeyModeEnum::Toggle && !F::Aimbot.m_bToggled)
				return 0.f;
		}

		// botox active_fov(): the held weapon's weapon_settings() fov (class override or general)
		const float flFov = pWeapon ? std::min(F::AimbotGlobal.WeaponSettings(pWeapon->GetWeaponID()).m_flFOV, 88.f) : 0.f;
		if (flFov <= 0.f)
			return 0.f;

		constexpr float kProbeDeg = 5.f, kProbeRange = 8192.f;
		const Vec3 vAng = I::EngineClient->GetViewAngles();
		Vec3 vProbeAng = vAng;
		vProbeAng.x -= kProbeDeg;

		Vec3 vFwd, vProbe;
		Math::AngleVectors(vAng, &vFwd);
		Math::AngleVectors(vProbeAng, &vProbe);
		const Vec3 vEye = pLocal->GetEyePosition();

		Vec3 s0, s1;
		if (!SDK::W2S(vEye + vFwd * kProbeRange, s0) || !SDK::W2S(vEye + vProbe * kProbeRange, s1))
			return 0.f;

		const float flDist = std::sqrt((s0.x - s1.x) * (s0.x - s1.x) + (s0.y - s1.y) * (s0.y - s1.y));
		const float flRad = flDist / std::tan(DEG2RAD(kProbeDeg)) * std::tan(DEG2RAD(flFov));
		return flRad >= 1.f ? flRad : 0.f;
	}

	void NetGraph(HudSnap& s)
	{
		static ConVar *net_graph = nullptr, *net_graphpos = nullptr, *net_graphheight = nullptr, *cl_updaterate = nullptr, *cl_cmdrate = nullptr;
		if (!net_graph)
		{
			net_graph       = H::ConVars.FindVar("net_graph");
			net_graphpos    = H::ConVars.FindVar("net_graphpos");
			net_graphheight = H::ConVars.FindVar("net_graphheight");
			cl_updaterate   = H::ConVars.FindVar("cl_updaterate");
			cl_cmdrate      = H::ConVars.FindVar("cl_cmdrate");
		}

		// CNetGraphPanel::DrawTextFields m_Framerate, plus a running std dev for botox's "var"
		// (CS:GO reads absoluteframestarttimestddev, TF2's globals have no such field)
		static float s_frame = 0.f, s_var = 0.f;
		const float ft = I::GlobalVars->absoluteframetime;
		s_frame = 0.9f * s_frame + 0.1f * ft;
		if (s_frame <= 0.f) s_frame = 1.f;
		s_var = 0.9f * s_var + 0.1f * (ft - s_frame) * (ft - s_frame);
		s.fps = 1.f / s_frame;
		s.frameVarMs = std::sqrt(s_var) * 1000.f;

		s.netGraph       = net_graph ? net_graph->GetInt() : 0;
		s.netGraphPos    = net_graphpos ? net_graphpos->GetInt() : 1;
		s.netGraphHeight = net_graphheight ? net_graphheight->GetInt() : 64;
		s.updateRate     = cl_updaterate ? cl_updaterate->GetFloat() : 66.f;
		s.cmdRate        = cl_cmdrate ? cl_cmdrate->GetFloat() : 66.f;
		s.tickInterval   = I::GlobalVars->interval_per_tick;
		s.interp         = BacktrackLerp();

		constexpr int kTotal = 13;   // INetChannelInfo::TOTAL (public/inetchannelinfo.h)
		static int s_outBytes = 0;
		auto pNci = I::EngineClient->IsConnected() ? I::EngineClient->GetNetChannelInfo() : nullptr;
		if (pNci)
		{
			s.loss       = pNci->GetAvgLoss(FLOW_INCOMING);
			s.choke      = pNci->GetAvgChoke(FLOW_INCOMING);
			s.latency    = pNci->GetAvgLatency(FLOW_OUTGOING);
			s.inData     = pNci->GetAvgData(FLOW_INCOMING) / 1024.f;
			s.outData    = pNci->GetAvgData(FLOW_OUTGOING) / 1024.f;
			s.inPackets  = pNci->GetAvgPackets(FLOW_INCOMING);
			s.outPackets = pNci->GetAvgPackets(FLOW_OUTGOING);
			s.inBytes    = pNci->GetPacketBytes(FLOW_INCOMING, pNci->GetSequenceNr(FLOW_INCOMING), kTotal);
			if (const int out = pNci->GetPacketBytes(FLOW_OUTGOING, pNci->GetSequenceNr(FLOW_OUTGOING) - 1, kTotal))
				s_outBytes = out;
			pNci->GetRemoteFramerate(&s.svTime, &s.svDev);
			if (s.updateRate > 0.001f)
				s.latency -= 0.5f / s.updateRate;
			s.latency = std::max(s.latency, 0.f);
		}
		s.outBytes = s_outBytes;
		if (I::EngineClient->IsPlayingDemo())
			s.latency = 0.f;

		// CNetGraphPanel::DrawServerType, TF2 shape (no valve-ds proxy flag, no p2p)
		const char* type = "offline";
		if (I::EngineClient->IsPlayingDemo())             type = "demo";
		else if (pNci && pNci->IsLoopback())              type = "local";
		else if (I::EngineClient->IsInGame())             type = I::EngineClient->IsHLTV() ? "SourceTV" : "online";
		else if (I::EngineClient->IsConnected())          type = "loading";
		Copy(s.serverType, sizeof(s.serverType), type);
	}

	// "fade under smoke" (SCREEN, 11-04): nearest active smoke cloud -> HudSnap.smokeCenter/
	// Radius, read by ssao.cpp on the render thread. botox's radius is a 140u grow/shrink keyed
	// to CSmokeGrenadeProjectile's own tick-begin (csgo-sdk ambient_occlusion.cpp:367-386); CSS's
	// active-smoke entity networks the real per-instance fade window instead (m_flSpawnTime /
	// m_FadeStartTime / m_FadeEndTime, RecvPropTime/RecvPropFloat, css-community-master
	// c_particle_smokegrenade.cpp:219-221) -- more accurate than guessing botox's constants, so
	// this grows over the first second off m_flSpawnTime and shrinks over CSS's own fade window
	// instead of botox's hardcoded 17.5s/3s. Network name "ParticleSmokeGrenade" confirmed by
	// IMPLEMENT_CLIENTCLASS_DT's 3rd arg (same file:218) -- not the "DT_..." table name.
	void CollectSmoke(HudSnap& s, CCSPlayer* pLocal)
	{
		s.smokeRadius = 0.f;
		if (!I::ClientEntityList || !I::GlobalVars)
			return;

		static int s_offSpawn = U::NetVars.GetNetVar("ParticleSmokeGrenade", "m_flSpawnTime");
		static int s_offFadeS = U::NetVars.GetNetVar("ParticleSmokeGrenade", "m_FadeStartTime");
		static int s_offFadeE = U::NetVars.GetNetVar("ParticleSmokeGrenade", "m_FadeEndTime");
		if (!s_offSpawn || !s_offFadeS || !s_offFadeE)
			return;   // netvar offset not found -- never draw a guessed radius

		const float now = I::GlobalVars->curtime;
		const Vec3  eye = pLocal ? pLocal->GetAbsOrigin() : Vec3();
		float bestDistSq = FLT_MAX;

		const int highest = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 1; i <= highest; i++)
		{
			auto pClient = I::ClientEntityList->GetClientEntity(i);
			auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
			if (!pEnt || pEnt->IsDormant())
				continue;
			auto pClass = pEnt->GetClientClass();
			if (!pClass || strcmp(pClass->GetName(), "ParticleSmokeGrenade") != 0)
				continue;

			const float spawn = *reinterpret_cast<float*>(uintptr_t(pEnt) + s_offSpawn);
			const float fadeS = *reinterpret_cast<float*>(uintptr_t(pEnt) + s_offFadeS);
			const float fadeE = *reinterpret_cast<float*>(uintptr_t(pEnt) + s_offFadeE);
			if (fadeE > 0.f && now >= fadeE)
				continue;   // fully faded, not a live cloud

			float radius = 140.f;
			const float age = now - spawn;
			if (age < 1.f)
				radius *= std::clamp(age, 0.f, 1.f);                                   // grow, first second
			else if (fadeS > 0.f && fadeE > fadeS && now > fadeS)
				radius *= std::clamp((fadeE - now) / (fadeE - fadeS), 0.f, 1.f);        // shrink, real fade window
			if (radius <= 1.f)
				continue;

			const Vec3 origin = pEnt->GetAbsOrigin();
			const Vec3 d = origin - eye;
			const float distSq = d.x * d.x + d.y * d.y + d.z * d.z;
			if (distSq < bestDistSq)   // nearest to the local eye wins when 2+ smokes are live
			{
				bestDistSq = distSq;
				s.smokeCenter[0] = origin.x; s.smokeCenter[1] = origin.y; s.smokeCenter[2] = origin.z;
				s.smokeRadius = radius;
			}
		}
	}
}

void HudAim_Collect(CCSPlayer* pLocal)
{
	NgTextPin(g_hudNg);

	HudSnap s;
	s.srcW = float(H::Draw.m_nScreenW);
	s.srcH = float(H::Draw.m_nScreenH);
	s.alive = pLocal && pLocal->IsAlive();
	if (pLocal)
	{
		CWeaponCSBase* pWeapon = H::Entities.GetWeapon();
		if (g_hudFovCircle)
			s.fovRadius = FovRadius(pLocal, pWeapon);
		if (g_hudNg)
			NetGraph(s);
	}
	CollectSmoke(s, pLocal);
	s.stampMs = GetTickCount64();

	static bool s_logged = false;
	if (!s_logged && (g_hudNg || g_hudFovCircle))
	{
		s_logged = true;
		DebugLog("hud: collect live (screen %dx%d, ng=%d fov=%d)", H::Draw.m_nScreenW, H::Draw.m_nScreenH,
			int(g_hudNg), int(g_hudFovCircle));
	}

	HudSnapGuard g;
	g_hudSnap = s;
}

void HudAim_Restore()
{
	NgTextPin(false);
}
