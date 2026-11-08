#include "events.h"

#include "core.h"
#include "esp.h"   // damage numbers + on-hit hitboxes (botox n_players on_player_hurt / hitboxes_on_hurt)

void DebugLog(const char* fmt, ...);

bool CEventListener::Initialize()
{
	std::vector<const char*> vEvents = { 
		// only event FireGameEvent handles; the TF2 list (teamplay_round_start, mvm_reset_stats,
		// player_changeclass, revive_player_notify, ...) listened for nothing
		"player_hurt",
		// world_fx3 tracers/impacts read it in the FireEvent hook (unitE_events.cpp); the server only
		// networks events some client listener asked for, and stock CS:S client code asks for none
		"bullet_impact"
	};

	for (auto szEvent : vEvents)
	{
		I::GameEventManager->AddListener(this, szEvent, false);

		if (!I::GameEventManager->FindListener(this, szEvent))
		{
			U::Core.AppendFailText(std::format("Failed to add listener: {}", szEvent).c_str());
			m_bFailed = true;
		}
	}

	return !m_bFailed;
}

void CEventListener::Unload()
{
	I::GameEventManager->RemoveListener(this);
}

void CEventListener::FireGameEvent(IGameEvent* pEvent)
{
	if (!pEvent)
		return;

	auto pLocal = H::Entities.GetLocal();
	auto uHash = FNV1A::Hash32(pEvent->GetName());

	if (I::EngineClient->IsPlayingDemo())
		return;

	// F::CritHack / F::AimbotMelee (crithack.h/aim_melee.h, TF2-only crits + melee auto backstab)
	// deleted -- no CS:S analog (AGENT_BRIEF DE-TF2 rule).
	switch (uHash)
	{
	// TF2 MvM instant revive (revive_player_notify) deleted: no Mann vs. Machine in CSS
	case FNV1A::Hash32Const("player_hurt"):
	{
		const int iDamage = pEvent->GetInt("dmg_health");   // CS:S modevents.res ("damageamount" is TF2's)
		if (iDamage <= 0)
			break;

		// own damage only, every other player's hits would spam the screen
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker")) != I::EngineClient->GetLocalPlayer())
			break;
		const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		if (iVictim <= 0 || iVictim == I::EngineClient->GetLocalPlayer())   // self-damage (fall, own HE)
			break;

		auto pEntity = I::ClientEntityList->GetClientEntity(iVictim);
		auto pVictim = pEntity ? pEntity->As<CCSPlayer>() : nullptr;
		if (!pVictim)
			break;

		F::Esp.OnPlayerHurt(pVictim, iDamage);
		// pairs with aim_hitscan.cpp "AIM: shot" (hg = server hitgroup: 1 head, 2 chest, 3 stomach)
		DebugLog("AIM: hurt ent=%d hg=%d dmg=%d", iVictim, pEvent->GetInt("hitgroup"), iDamage);
		break;
	}
	}
}