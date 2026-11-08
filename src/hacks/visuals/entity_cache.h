#pragma once
#include <vector>

// botox hacks/entity_cache/entity_cache.cpp port. botox's version is push/pop from
// OnAddEntity/OnRemoveEntity hooks (client.dll IClientEntityList listener) -- those hooks
// live in src/hacks/aim/hooks.cpp / events.cpp, owned by unit SDK2, not wired to this cache
// (see report NEEDS FROM OTHERS). Ported instead as a lazy per-frame rebuild: cheap (<=2048
// VCalls, same walk visuals.cpp's EntityList_HighestIndex / world_fx2.cpp's FindByClass
// already do every frame for their own purposes) and needs no hook at all, so it works today.
//
// Entities are plain indices (1..highest), not pointers: botox's c_base_entity* is a stable
// object across a dormant/active flip, a raw index is not guaranteed to be -- callers that
// hold one across frames must re-validate with EntityCache_Valid.
namespace EntityCache
{
    // 1..64: player slots. 65..highest: edicts (weapons, grenades, props, ragdolls, ...).
    const std::vector<int>& Players();
    const std::vector<int>& Edicts();

    // Forces a rebuild next call instead of waiting for the frame counter to move (map
    // change / round restart -- stale indices would otherwise survive one extra frame).
    void Invalidate();

    bool Valid(int ent);
}
