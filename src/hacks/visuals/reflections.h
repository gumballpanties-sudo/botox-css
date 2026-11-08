#pragma once
#include "../../globals/globals.h"
#include "../screen/screenfx.h"   // FxViewSetup

// Screen-space reflections -- botox hacks/visuals/screen/reflections.cpp 1:1 (5 passes +
// the map's env_cubemap fallback, see fx_refl.hlsl). Draws into target, which is already the
// bound render target screenfx.cpp's own AO/DoF/frame-blend passes write into.
void Reflections_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                       const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                       const FxViewSetup& view);
void Reflections_Destroy();   // device reset / eject
// Main thread, once per main view setup (screenfx.cpp FxReadViewSetup): nearest cubemap pick +
// file loads, view basis for the cube lookup. Handed to the render-thread draw under a lock.
void Reflections_UpdateView(const float origin[3], const float angles[3]);
