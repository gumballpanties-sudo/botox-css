#pragma once
#include "../../globals/common.h"

// ==== matsystemsurface lookup ========================================================
// All TF2-only features that used to live here are gone (11-03 CSS port, DE-TF2 pass):
//   - Steam datacenter region selector: only steered CTFPartyClient::RequestQueueForMatch /
//     CTFGCClientSystem, TF2's own Valve matchmaking GC. CS:S has no GC matchmaking.
//   - "net settings" lerp/rate presets (klown/murmur/boobsy/xcd/spy/koy/lenny/snappy):
//     lifted from CTFGameRules::IsConnectedUserInfoChangeAllowed (tf_gamerules.cpp).
//     No equivalent feature exists in botox csgo-sdk (grepped: no match) -- not a port,
//     a TF2-cheat-base leftover. Deleted with netcvar.h and tests/netcvar_test.cpp.
//   - net_graph recolor / reposition hooks on the ENGINE's own CNetGraphPanel: sigs were
//     verified against tf/bin/x64/client.dll only and botox csgo does not hook the
//     engine panel at all -- it draws its OWN net graph (see hacks/hud/hud_extra.cpp,
//     the real botox n_indicators::impl_t::net_graph 1:1 port). Deleted.
// What is left: FindMatSystemSurface, a plain interface lookup other units' hooks use
// (src/hacks/visuals/world_fx.cpp: DrawPanelIn3DSpace for the low-fps viewmodel panel).
void* FindMatSystemSurface();        // client.dll's g_pMatSystemSurface, CreateInterface fallback
