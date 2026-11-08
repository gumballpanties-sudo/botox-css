#include "unitE_events.h"
#include "../hud/hitmarker.h"
#include "killeffects.h"
#include "../visuals/world_fx3.h"   // unit world: world hit marker on player_hurt
#include "misc_overlays.h"   // healthshot kill trigger + displayed logs

void UnitE_OnGameEvent(void* pEvent)
{
    if (!pEvent || !IsInGame()) return;

    __try
    {
        const char* name = IGameEventVtbl::GetName(pEvent);
        if (!name) return;

        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return;

        MiscLogs_OnGameEvent(pEvent, name, localIdx);

        if (strcmp(name, "round_start") == 0)
            AutoBuy_OnRoundStart(localIdx);
        else if (strcmp(name, "bullet_impact") == 0)
        {
            // cs_player_shared.cpp CCSPlayer::FireBullet: userid (shooter), x, y, z (impact).
            // unit world: tracers + client impact boxes (world_fx3.cpp's own FireBullet hook
            // never matched here -- CS:S's FireBullet has a different signature than TF2's).
            WorldFx3_OnBulletImpact(GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0)),
                                     IGameEventVtbl::GetFloat(pEvent, "x", 0.f),
                                     IGameEventVtbl::GetFloat(pEvent, "y", 0.f),
                                     IGameEventVtbl::GetFloat(pEvent, "z", 0.f));
        }
        else if (strcmp(name, "player_hurt") == 0)
        {
            // CS:S player_hurt (modevents.res): userid, attacker, health, armor, weapon,
            // dmg_health, dmg_armor, hitgroup -- no crit/minicrit (CS has no crit mechanic).
            const int attackerIdx = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
            if (attackerIdx != localIdx) return;   // hit marker/sound are OUR feedback only
            const int dmg    = IGameEventVtbl::GetInt(pEvent, "dmg_health", 0);
            const bool killed = IGameEventVtbl::GetInt(pEvent, "health", 1) <= 0;
            Hitmarker_OnHurt(dmg, killed);
            WorldFx3_OnPlayerHurt(GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0)));   // unit world: world hit marker
        }
        else if (strcmp(name, "player_death") == 0)
        {
            const int victimIdx   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
            const int attackerIdx = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
            KillEffects_OnDeath(victimIdx, attackerIdx, victimIdx == localIdx);
            // frag / capture score popups gone: points is botox's trick counter now (detections.cpp)
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
