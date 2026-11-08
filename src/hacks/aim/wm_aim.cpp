#include "sdk.h"
#include "../menu/cfg/u_wm.h"
#include <cstring>

// Unit wm: the two engine reads the botox watermark styles need that only the aim SDK layer has.

// botox draw_watermark_onetap: client_state->m_choked_commands. ref CClientState::chokedcommands
// (defs.h), the same field hooks.cpp feeds CPrediction::Update with. A plain int read off the render
// thread: worst case one frame stale.
int Aim_ChokedCommands()
{
	return I::ClientState ? I::ClientState->chokedcommands : 0;
}

// botox draw_watermark_legendware: net_channel->get_address(), "loopback" / none -> "Local server".
// Through the SDK classes (IVEngineClient::GetNetChannelInfo, INetChannelInfo::GetAddress in defs.h)
// so no slot number is hand-counted. false = no channel (not connected).
bool Aim_NetServer(char* out, size_t size)
{
	if (!I::EngineClient || !out || !size)
		return false;
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return false;
	const char* szAddress = pNetChan->GetAddress();
	strncpy_s(out, size, !szAddress || !strcmp(szAddress, "loopback") ? "Local server" : szAddress, _TRUNCATE);
	return true;
}

// botox dna_enemy_spotted: any living, non-dormant enemy with m_bSpotted set. TF2 has no radar /
// m_bSpotted netvar (tf/ has no spotted field anywhere in source-sdk-2013), so the closest TF2
// equivalent is "an enemy our eye can see": the same eye -> his networked eye SDK::VisPos trace
// esp.cpp uses for its visible colours. Game thread (IEngineVGui::Paint), so the trace is safe;
// only runs while the dna glow-on-spotted row is live, 20 Hz, stops at the first hit.
void WmAim_Tick(CCSPlayer* pLocal)
{
	if (!g_wmDnaGlow || !g_wmDnaGlowSpotted || !pLocal || !pLocal->IsAlive())
	{
		g_wmEnemySpotted.store(false, std::memory_order_relaxed);
		return;
	}

	static float flNext = 0.f;
	const float flNow = I::GlobalVars->realtime;
	if (flNow >= flNext - 1.f && flNow < flNext)   // realtime restarts on map change: resync then
		return;
	flNext = flNow + 0.05f;

	const Vec3 vEye = pLocal->GetEyePosition();
	bool bSpotted = false;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CCSPlayer>();
		if (!pPlayer || pPlayer == pLocal || !pPlayer->IsAlive() || pPlayer->IsDormant())
			continue;
		if (SDK::VisPos(pLocal, pPlayer, vEye, pPlayer->GetAbsOrigin() + pPlayer->m_vecViewOffset()))
		{
			bSpotted = true;
			break;
		}
	}
	g_wmEnemySpotted.store(bSpotted, std::memory_order_relaxed);
}
