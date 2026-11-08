#pragma once
#include <d3d9.h>

// botox hacks/visuals/screen/frame_dump.cpp port, 1:1 (own PNG encoder, no d3dx9 at runtime).
// d3d thread only; stalls the pipeline (GetRenderTargetData), one shot only -- a debug dump,
// never called every frame. false = not a 32 bit surface, readback failed, or the file would
// not open.
bool FrameDump_Save(IDirect3DDevice9* device, IDirect3DSurface9* surface, const char* fileName);
