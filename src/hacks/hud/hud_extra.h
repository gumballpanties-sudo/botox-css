#pragma once
#include <cstdint>

// Unit hud (11-01), render side of botox's "hud" group. Data comes from the game thread through
// hud_snap.h (aim/hud_aim.cpp); everything here only draws.
struct ImFont;

void RenderHudExtras();          // BuildImGuiFrame: aimbot fov circle + custom net graph

// fonts: net graph face (fonts tab > net graph, botox default lucon 14)
void HudFonts_Build();           // render.cpp BuildFontAtlas, before io.Fonts->Build()
bool HudFonts_Due();             // BuildImGuiFrame's atlas-rebuild test (debounced like the others)
void HudFonts_ResetNetGraph();   // fonts tab "reset to default"
