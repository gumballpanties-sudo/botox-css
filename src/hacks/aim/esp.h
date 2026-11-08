#pragma once
#include "sdk.h"
#include "css.h"       // CCSPlayer / CWeaponCSBase / CPlantedC4 / CCSPlayerResource, CSWeaponID
#include <array>

// Player + world esp = botox hacks/visuals/players/players.cpp + edicts/edicts.cpp +
// grenade/grenade_path.cpp + players/sound_esp/sound_esp.cpp, ported 1:1 onto the CSS SDK
// (css.h). Recorded into hacks/esp/esp_queue.h (botox m_draw_data) on the game thread, replayed
// by render.cpp RenderEspQueue in the ImGui frame. World entities (dropped weapons, grenades,
// paths, sound pings, the planted C4) stay on the vgui surface (H::Draw) they were written for.
// Vars: hacks/esp/esp_vars.h (plain primitives, dih side), not the aim ConfigVar tree.
//
// NEEDS FROM OTHERS (see report): CBaseEntity::IsPlayer()/IsBuilding()/IsNPC() (defs.h ~9831)
// compare GetClassID() against ETFClassID::CTFPlayer etc, a TF2-only networked-class-id enum
// that never matches a CS:S client class -- IsPlayer() always returns false for a CCSPlayer.
// DrawPlayers below therefore walks entindex 1..maxplayers directly (GetClientEntity) instead
// of H::Entities.GetGroup(PlayerAll)/IsPlayer(), which depends on that same broken check.
class CEsp
{
private:
	void DrawPlayers(CCSPlayer* pLocal);
	void DrawBomb(CCSPlayer* pLocal);            // botox edicts bomb(): planted C4 world text + timer bar
	void DrawSoundArrows(CCSPlayer* pLocal);     // botox sound_arrows
	void DrawDamageNumbers();                    // botox damage_numbers
	void DrawHitHitboxes();                      // botox hit_hitboxes
	void DrawWorld(CCSPlayer* pLocal);           // botox edicts dropped_weapons()
	void DrawThrown(CCSPlayer* pLocal);          // botox edicts projectiles(): live grenade name
	void DrawGrenadePath(CCSPlayer* pLocal);     // botox grenade_path: your held grenade's thrown arc
	void DrawProjectilePaths(CCSPlayer* pLocal);
	void DrawSoundPings();

	// botox damage_number_t / hit_hitboxes_t; game thread only (events + paint)
	struct DamageNumber { Vec3 vOrigin; int iDamage = 0, iVictim = 0; float flTime = 0.f; };
	struct HitBoxes { std::vector<std::array<Vec3, 8>> vBoxes; float flTime = 0.f; };
	std::vector<DamageNumber> m_vDamage;
	std::vector<HitBoxes> m_vHitBoxes;

	// botox m_backup_player_data m_animated_health / m_ammo
	float m_flAnimHealth[65] = {};
	float m_flAmmo[65] = {};

	int m_nDrawn = 0;   // boxes drawn this frame (debug.log proof)

public:
	// aim/hooks.cpp IEngineVGui::Paint, game thread, once a frame
	void Draw(CCSPlayer* pLocal);
	// aim/events.cpp player_hurt (our hits only): damage numbers + on-hit hitboxes
	void OnPlayerHurt(CCSPlayer* pVictim, int iDamage);
};

ADD_FEATURE(CEsp, Esp);
