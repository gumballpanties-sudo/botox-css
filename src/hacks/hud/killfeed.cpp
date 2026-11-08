#include "killfeed.h"
#include "../misc/unitE_events.h"   // hit marker/sound, death particles, score popup -- piggyback, no new hook
#include "../misc/names.h"          // custom names: on-kill cycle trigger
#include "../lua/lua.h"             // g_lua.on_event -- botox fire_event_intern.cpp:33

// Server events (player_death etc.) reach CHudBaseDeathNotice through slot 8, which in TF2 x64
// engine.dll is FireEventClientSide (CGameEventManager vtable: 6 CreateEvent, 7 FireEvent,
// 8 FireEventClientSide, 9 DuplicateEvent, 10 FreeEvent). Return false = swallow entry.
FireEvent_t o_FireEvent = nullptr;

// The manager owns a fired event and frees it on every path; swallowing one without calling the
// original leaked it. FreeEvent (slot 10) is what the engine would have done.
__declspec(noinline) static bool DropEvent(void* mgr, void* pEvent)
{
    __try {
        using FreeEvent_t = void(__fastcall*)(void*, void*);
        ((FreeEvent_t)(*(void***)mgr)[10])(mgr, pEvent);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// body in _impl: uses __try, so the MiscHookGuard cannot live here (C2712)
static bool __fastcall h_FireEvent_impl(void* thisptr, void* pEvent, bool bDontBroadcast)
{
    if (pEvent && g_selfKillfeedOnly && !g_eject)
    {
        __try
        {
            const char* name = IGameEventVtbl::GetName(pEvent);
            if (name)
            {
                const int localIdx = GetLocalPlayerIndex();
                if (localIdx >= 1)
                {
                    auto uid2idx = [](int uid) -> int {
                        return (uid > 0) ? GetPlayerForUserID(uid) : 0;
                    };

                    // kill notices (CSS has no fish/slap/building events -- player_death only)
                    if (strcmp(name, "player_death") == 0)
                    {
                        int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                        int victimIdx   = uid2idx(IGameEventVtbl::GetInt(pEvent, "userid",   0));
                        int assisterIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "assister", 0));
                        bool involved = (attackerIdx == localIdx) ||
                                        (victimIdx   == localIdx) ||
                                        (assisterIdx == localIdx && assisterIdx > 0);
                        if (!involved)
                            return DropEvent(thisptr, pEvent);
                    }
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return o_FireEvent(thisptr, pEvent, bDontBroadcast);
}

bool __fastcall h_FireEvent(void* thisptr, void* pEvent, bool bDontBroadcast)
{
    MiscHookGuard _g;
    UnitE_OnGameEvent(pEvent);
    Names_OnGameEvent(pEvent);   // custom names: on-kill cycle
    g_lua.on_event(pEvent);      // botox fire_event_intern.cpp:33 -- before the self-killfeed filter decides whether to swallow it
    return h_FireEvent_impl(thisptr, pEvent, bDontBroadcast);
}

// hud_deathnotice.cpp:29, default "6", FCVAR 0 (not even archive) -- a normal clientside
// convar, safe to pin/restore the same way crosshair.cpp pins "crosshair".
static ConVar* s_deathNoticeCv = nullptr;
static bool    s_dnPinned      = false;
static float   s_dnOrig        = 6.f;

void KeepKillfeed_Tick()
{
    const bool want = g_vsKeepKillfeed && !g_eject;
    if (!s_deathNoticeCv && g_cvar) s_deathNoticeCv = g_cvar->FindVarWalk("hud_deathnotice_time");
    if (!s_deathNoticeCv) return;

    if (want && s_dnPinned) { s_deathNoticeCv->ForceFloat(9999.f); return; }
    if (want == s_dnPinned) return;

    if (want) { s_dnOrig = s_deathNoticeCv->GetFloat(); s_deathNoticeCv->ForceFloat(9999.f); }
    else      { s_deathNoticeCv->ForceFloat(s_dnOrig); }
    s_dnPinned = want;
}

void KeepKillfeed_Restore()
{
    if (s_dnPinned && s_deathNoticeCv) s_deathNoticeCv->ForceFloat(s_dnOrig);
    s_dnPinned = false;
}
