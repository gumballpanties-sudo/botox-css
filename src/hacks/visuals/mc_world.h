#pragma once
#include "../../globals/globals.h"

// Minecraft sky + clouds -- botox hacks/visuals/screen/mc_clouds.cpp + edicts/mc_sky.h, as one
// pass (fx_mcsky.hlsl) drawn right behind the engine's own 2D sky in every view that draws one,
// quad at the depth clear value so only sky pixels take it. Same draw point botox uses
// (behind DrawWorldLists' skybox), so world, particles and the hud all land on top of it.

// Main thread, from screenfx.cpp's DoPostScreenSpaceEffects hook: the cloud layer's drift off
// the view origin and curtime (g_globalVars is main-thread only).
void MCWorld_UpdateView(const float origin[3]);

// From h_R_DrawSkyBox, after the original drew the 2D sky. Draws now on the render thread, or
// queues the draw onto it at this exact frame position under multicore.
void MCWorld_OnSkyBoxDrawn();

void MCWorld_Destroy();   // device reset / eject -- called from ScreenFx_Destroy
