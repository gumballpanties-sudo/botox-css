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

// one mask pass: px whose stencil passes (D3DCMPFUNC func, ref, test mask) write 1 into channels (D3DCOLORWRITEENABLE
// bits, r g b = class 1 2 3); zero = clear them there instead. d3d values as plain ints: the aim TU includes this
struct PlayerStencilFill { unsigned func = 3 /*D3DCMP_EQUAL*/, ref = 0, channels = 1 /*RED*/; bool zero = false; unsigned mask = 0xFFFFFFFFu; };

// wipe a source's refs right after its mask read: where (stencil & test) == ref the write bits go to 0, every
// other bit stays (botox clear_t). write 0 = no clear (players: the engine-side refs are theirs to leave)
struct PlayerStencilClear { unsigned test = 0, ref = 0, write = 0; };

// model glow's ref band (aim/chams.cpp kRef*: 0x50..0x57 in the low 7 bits). A pass that never reaches Draw
// (target / depth-stencil mismatch, build fail) skips its clear: the players' mask zeroes red over this band
// (kPlayerFills) so a leftover gun never reads as a visible player. Players' own refs (1, 2) never fall in it
constexpr unsigned kModelBandTest = 0x78, kModelBandRef = 0x50;

struct PlayerStencilSettings
{
	static constexpr int kMaxCapsules = 768;   // 32 players x the TF2 hitbox set (17-18) + headroom
	bool  outline = false, glow = false, inner = false, innerBones = false;
	// model glow (aim/chams.cpp ModelGlow_*): own mask program, never touches PlayerStencil_GlowLive.
	// nFills 0 = the players' program (player_stencil.cpp kPlayerFills: red 2..127, green 1)
	bool  model = false;
	static constexpr int kMaxFills = 4;
	PlayerStencilFill fills[kMaxFills] = {};
	int   nFills = 0;
	PlayerStencilClear clear = {};
	float third[4] = {}, glowThird[4] = {}, glowThirdOuter[4] = {};   // c80 c81 c82: class 3 outline / glow inner / outer
	float visible[4] = {}, invisible[4] = {};                  // c1 c2: stencil colours
	float thickness = 1.5f;                                    // c0.w, c0.z = ceil
	float glowVisible[4] = {}, glowInvisible[4] = {};          // c3 c4
	float glowVisibleOuter[4] = {}, glowInvisibleOuter[4] = {};// c5 c6
	float gradient[4] = {};                                    // c7: start, 1/(1-start), radius, additive
	float wave[4] = {};                                        // c8: on, phase, 1/size, strength
	float waveColor[4] = {};                                   // c9
	float innerVisible[4] = {}, innerInvisible[4] = {};        // c10 c11
	float outlineWave[4] = {}, outlineWaveColor[4] = {};       // c12 c13: stencil outline accent wave
	// wave anchored to each player's body: c15 = glow anchor, box count, outline anchor;
	// boxes = ndc bounds per player (x0 y0 x1 y1, main thread) -> c16.., phase offsets -> c48..
	static constexpr int kMaxBoxes = 32;
	float waveAnchor[4] = {};
	float waveAngle[4] = { 1.f, 0.f, 1.f, 0.f };               // c14: glow cos sin, outline cos sin (angle slider)
	float boxes[kMaxBoxes][4] = {}, boxPhase[kMaxBoxes][4] = {};
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
// every shader built on the current device: a pass submitted now runs (and clears what it reads). Model glow
// stamps nothing until this is true -- nothing would read or clear the refs. The shaders are only made inside a
// queued pass (render thread), so a source that wants them first queues PlayerStencil_Warm.
bool PlayerStencil_Ready();
void PlayerStencil_Warm();   // main thread: queue a shader-only pass (no mask, no draw)
// the last model glow pass reached Draw (so it cleared its refs). False after a pass that did not (target /
// depth-stencil mismatch, build fail): stamp nothing, queue PlayerStencil_Probe at the read point each frame
// until its checks (shaders + drawable, no draw) pass again
bool PlayerStencil_ModelLive();
void PlayerStencil_Probe();
// shaders refused / targets failed for good: outline + inner glow off, glow back to the halo.
// Device-level (shaders are shared by both instances), so this covers the preview path too.
bool PlayerStencil_Failed();
// render.cpp h_Reset: the DEFAULT-pool targets must be gone before the device resets (shaders
// survive). Releases both instances' targets.
void PlayerStencil_ReleaseTargets();
