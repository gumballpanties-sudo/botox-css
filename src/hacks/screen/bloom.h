#pragma once
#include "../../globals/globals.h"

// Bloom -- the bright part of the frame bled into a glow (hacks/screen/bloom.cpp).
//
// Runs at the same call sites as the colour grade and just before it: the glow is a lens,
// the grade is the film. That is inside the game's own render, so every capture path
// records it -- and the hud is already in the backbuffer there, so it glows too.
//
// Bloom_Frame is idempotent per frame: the first caller whose render target is the
// backbuffer draws and latches; Bloom_NewFrame clears the latch at the end of the frame.

// forceBackbuffer: bind the backbuffer as the render target instead of standing down when
// something else is bound. For a call site at Present, where the frame is finished and
// whatever is bound is not the point. Mid-frame call sites leave it false, or a water
// reflection's own target would be taken for the frame.
bool Bloom_Frame(IDirect3DDevice9* dev, bool forceBackbuffer = false);
void Bloom_NewFrame();
void Bloom_Destroy();   // device Reset, device change, eject
