#pragma once
#include "../../globals/globals.h"

// Bloom -- the bright part of the frame bled into a glow (hacks/screen/bloom.cpp).
//
// botox order: after true motion blur, before the hud (do_post_screen_space_effects.cpp, which
// runs after DrawViewModels in csgo). CSS twin = the tail of the gun pass from ViewDrawFade
// (screenfx.cpp FxViewmodelPassDispatch): gun in, hud out. The grade's call sites below are
// the fallback for a frame that pass missed (call queue refused): there the hud glows too.
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
