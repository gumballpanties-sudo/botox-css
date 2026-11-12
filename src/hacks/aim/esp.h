#pragma once
#include "sdk.h"
#include "css.h"       // CCSPlayer / CWeaponCSBase / CPlantedC4 / CCSPlayerResource, CSWeaponID
#include "../esp/esp_vars.h"   // EF_COUNT -- EspPlayer::m_flags size
#include <array>
#include <cstdint>
#include <string>
#include <vector>

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

// botox n_players::esp_player_t, ported 1:1 onto the CSS gather. Everything CEsp::DrawPlayer
// reads, gathered from the entity (CEsp::DrawPlayers) or made up (esp preview, on a fake
// player). DrawPlayer must never read H::Entities / pLocal / I::EngineClient: every number it
// needs lives here. esp.h is aim-only (hacks/aim/*.cpp), so this struct is safe to define here
// rather than in a separate aim-only header -- esp_preview.cpp (the orchestrator's unit, also
// under hacks/aim/) includes esp.h to fill one of these.
struct EspPlayer
{
	float m_alpha = 1.f;
	Vec3 m_origin{}, m_mins{}, m_maxs{};   // world hull: ScreenBox (CSS: axis-aligned corners, no view-yaw trim -- ScreenBox never reads I::EngineClient)

	std::string m_name;                    // truncated to 24 + "[bot] " prefix already applied
	int m_health = 0;
	float m_healthFactor = 0.f;            // animated health / 100 (CS:S: fixed 100 max hp), clamped 0..1
	bool m_visible = false;                // visibility trace result (lazy: only run if a vis-colour feature asked)
	float m_distance = 0.f;
	int m_index = 0;                       // entindex: label declutter stack key (esp_declutter.h); 0 = labels fixed

	uint64_t m_steamId = 0;                // 0 = no avatar (bot / lookup failed)
	CSWeaponID m_weaponId = WEAPON_NONE;   // active weapon id; WEAPON_NONE = no weapon -> WeaponName()/icon skipped
	bool m_hasAmmo = false;                // active weapon has a clip and the ammo bar is on (bakes in g_espAmmoBar: it also gates the m_flAmmo animation, a shared mutable state update, not just a draw toggle)
	float m_ammoFactor = 0.f;              // animated clip / max clip, clamped 0..1

	int m_money = 0;                       // Account(); only read by DrawPlayer when g_espFlags + EF_MONEY are both on
	bool m_flags[EF_COUNT] = {};           // EspFlagBit row shown (all false if g_espFlags is off)

	const studiohdr_t* m_studio = nullptr; // null = no skeleton / hitboxes / backtrack trail / head avatar this player
	const matrix3x4* m_bones = nullptr;    // caller-owned array (stack local in DrawPlayers); must outlive the DrawPlayer call
	std::vector<Vec3> m_trail;             // lag compensated trail: world head centre per backtrack record (live: the ring; preview: its fake stream)

	bool m_hasHead = false;                // true only if bones resolved AND the caller's head hitbox was valid
	Vec3 m_head{}, m_viewRight{};          // world head centre + camera right vector, for the head avatar's screen radius
	float m_headRadius = 0.f;              // caller picks which hitbox is the head (CS:S set 0: hitbox 12, 0 is the pelvis) -- DrawPlayer only reads this, never an index
};

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
	// botox n_players::draw_player, split out of DrawPlayers so esp preview can feed it a fake
	// player. Pure draw: every EspQ:: emission below, no entity / H::Entities / pLocal reads.
	void DrawPlayer(const EspPlayer& p);
	// aim/events.cpp player_hurt (our hits only): damage numbers + on-hit hitboxes
	void OnPlayerHurt(CCSPlayer* pVictim, int iDamage);
};

ADD_FEATURE(CEsp, Esp);
