#pragma once
// botox hacks/visuals/screen/player_stencil.{h,cpp}: "player stencil" (crisp outline of a set px
// width, visible / occluded colours), "internal player glow" (blurred edge or bone capsules inside
// the silhouette) and botox's own full-res "player glow" (gradient / wave / normal blending),
// all off one stencil mask in d3d9.
//
// TF2 wiring: aim/chams.cpp Glow_Render (DoPostScreenSpaceEffects, main thread) marks the
// players into the engine stencil -- 2 where a pixel is visible, 1 where it is behind something
// (botox's CS:GO glow values: red = stencil 2..127, green = 1) -- fills PlayerStencilSettings and
// calls PlayerStencil_Submit, which queues the pass onto the render thread right behind those
// draws (material system call queue, screenfx.cpp ScreenFx_QueueFunctor). Plain POD: the aim TU
// includes this header.

struct PlayerStencilCapsule { float a[2], b[2], sigma[2]; };   // ndc endpoints, sigma in ndc y units

struct PlayerStencilSettings
{
	static constexpr int kMaxCapsules = 768;   // 32 players x the TF2 hitbox set (17-18) + headroom
	bool  outline = false, glow = false, inner = false, innerBones = false;
	float visible[4] = {}, invisible[4] = {};                  // c1 c2: stencil colours
	float thickness = 1.5f;                                    // c0.w, c0.z = ceil
	float glowVisible[4] = {}, glowInvisible[4] = {};          // c3 c4
	float glowVisibleOuter[4] = {}, glowInvisibleOuter[4] = {};// c5 c6
	float gradient[4] = {};                                    // c7: start, 1/(1-start), radius, additive
	float wave[4] = {};                                        // c8: on, phase, 1/size, strength
	float waveColor[4] = {};                                   // c9
	float innerVisible[4] = {}, innerInvisible[4] = {};        // c10 c11
	float innerReach[4] = {};                                  // c7 for the inner passes: intensity, -, size
	int   nCapsules = 0;
	PlayerStencilCapsule capsules[kMaxCapsules];
};

// Main thread, after the stencil marking draws. Copies s. bPreview = false routes to the live,
// screen-sized target set (unchanged default: every existing call site keeps drawing the live
// instance); true routes to the second, esp-preview-sized target set (aim/chams.cpp
// Glow_RenderPreview) so the two never thrash rebuilding each other's D3DPOOL_DEFAULT textures
// every frame over their differing sizes.
void PlayerStencil_Submit(const PlayerStencilSettings& s, bool bPreview = false);
// botox glow_enabled(): our full-res glow drew last time -> the material system halo stays off.
bool PlayerStencil_GlowLive();
// shaders refused / targets failed for good: outline + inner glow off, glow back to the halo.
// Device-level (shaders are shared by both instances), so this covers the preview path too.
bool PlayerStencil_Failed();
// render.cpp h_Reset: the DEFAULT-pool targets must be gone before the device resets (shaders
// survive). Releases both instances' targets.
void PlayerStencil_ReleaseTargets();
