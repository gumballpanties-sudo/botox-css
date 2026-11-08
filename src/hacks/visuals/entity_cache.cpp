#include "entity_cache.h"
#include "../movement/prediction.h"   // g_globalVars, VCall, g_entityList, GetClientEntity

namespace
{
    std::vector<int> s_players;
    std::vector<int> s_edicts;
    int  s_builtFrame = -1;

    // Same slot numbers visuals.cpp's EntityList_HighestIndex / world_fx2.cpp's FindByClass
    // already rely on: IClientEntityList::GetClientNetworkable 0, GetHighestEntityIndex 6.
    void Rebuild()
    {
        s_players.clear();
        s_edicts.clear();
        if (!g_entityList) return;

        int hi = VCall<int>(g_entityList, 6);
        if (hi < 33)   hi = 32;
        if (hi > 2048) hi = 2048;

        for (int i = 1; i <= hi; ++i)
        {
            if (!VCall<void*>(g_entityList, 0, i)) continue;   // GetClientNetworkable: slot unused
            if (i <= 64) s_players.push_back(i);
            else         s_edicts.push_back(i);
        }
    }

    void EnsureFresh()
    {
        const int frame = g_globalVars ? g_globalVars->framecount : -1;
        if (frame == s_builtFrame && s_builtFrame != -1) return;
        s_builtFrame = frame;
        Rebuild();
    }
}

const std::vector<int>& EntityCache::Players() { EnsureFresh(); return s_players; }
const std::vector<int>& EntityCache::Edicts()  { EnsureFresh(); return s_edicts; }

void EntityCache::Invalidate() { s_builtFrame = -1; }

bool EntityCache::Valid(int ent)
{
    return g_entityList && ent > 0 && ent <= 2048 && VCall<void*>(g_entityList, 0, ent) != nullptr;
}
