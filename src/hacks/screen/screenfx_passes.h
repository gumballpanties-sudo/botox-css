#pragma once
#include "../../globals/globals.h"

// The simple passes (sharpen / film grain / vignette / invert / posterize / deband / channel
// shift) live in colorcorrect.cpp now -- botox draws them inside its colour correction shader.
// ScreenFxPasses_Frame / _NewFrame are no-ops kept for render.cpp's call sites; _Destroy drops
// the depth view + depth distort device objects below.

bool ScreenFxPasses_Frame(IDirect3DDevice9* dev, bool forceBackbuffer = false);
void ScreenFxPasses_NewFrame();
void ScreenFxPasses_Destroy();

// Emphasize + show depth (botox depth_view.cpp). Depth based, so screenfx.cpp's ScreenFx_Frame
// draws it from the game's post slot right after its depth resolve, into target. view = the
// frame's z range, so the shader's lin() means the same distance on every map.
struct FxViewSetup;   // screenfx.h
void ScreenFxPasses_DepthView(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                              const FxViewSetup& view);

// Depth distort (reshade Warp-FX on depth) + depth based channel shift. Same draw point as depth view,
// last in the chain (after motion blur, before the gun). WarpTick = main thread, advances the anim clock.
bool ScreenFxPasses_WarpWanted();
void ScreenFxPasses_WarpTick();
void ScreenFxPasses_Warp(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                         const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                         const FxViewSetup& view);
