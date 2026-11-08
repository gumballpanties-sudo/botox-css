#pragma once
#include <d3d9.h>

// botox hacks/visuals/screen/resolution_spoof.cpp port (+ docs/HANDOFF_resolution_spoofer.md,
// memory proj-resolution-spoofer.md). 3 layers, botox's own split:
//   1. world r_aspectratio       -- ALREADY in world_fx.cpp (WorldFx_Tick), now driven by the
//                                    accessors below instead of the plain aspect-ratio slider.
//   2. hud aspect                -- botox: scaleform CSGOHud root width + scale3d. CSS has NO
//                                    scaleform (classic vgui HUD only, grepped css-community-
//                                    master: no Panorama/scaleform anywhere) -- there is no
//                                    separate "hud" layer to drive. CSS's vgui HUD/crosshair
//                                    read the engine's real ScreenWidth()/Height() and are
//                                    swept into layer 3's whole-backbuffer crunch for free, so
//                                    AspectHud() below exists only to satisfy the accessor RULE
//                                    (every caller goes through it, never a raw var) and folds
//                                    into the same r_aspectratio/crunch path. NOT FAKED: there is
//                                    truly nothing CS:GO-scaleform-shaped to port here.
//   3. pixel crunch on Present   -- CrunchBackbuffer() below: StretchRect the real backbuffer to
//                                    a same-format full-res copy (never StretchRect INTO the
//                                    backbuffer itself, and never into anything MSAA -- the copy
//                                    target is always D3DPOOL_DEFAULT plain RENDERTARGET), draw
//                                    THAT down into a small RT (XYZRHW quad, FFP pixel, legal on
//                                    amd), then draw the small RT back up over the real
//                                    backbuffer. Manual Get/Set of exactly the states the quad
//                                    touches -- NEVER a D3DSBT_ALL CreateStateBlock/Apply (that
//                                    moved world textures under the engine's own state cache,
//                                    the 08-14 shelve cause, memory ref-csgo-pure-device-get-
//                                    state). Our own ImGui overlay draws AFTER this call (see
//                                    render.cpp call sites below) so it is never double-crunched
//                                    by layer 3; "overlay mode" (g_overlayMode) instead gives the
//                                    overlay its OWN small-RT crunch via WrapImGuiDraw, same
//                                    technique, separate resources.
//
// NEEDS FROM OTHERS / NOT PORTED (see report): the classic-crosshair rect-rebuild-at-spoofed-
// height refinement (botox crosshair_capture/crosshair_flush, HudWeapon paint hook) -- CSS's own
// cl_crosshairstyle geometry needs separate verification against weapon_csbase.cpp before that
// math can be trusted, and the paint hook isn't one this unit owns. Without it the crosshair is
// still correct (vgui draws it at native res, then layer 3 crunches it with everything else) --
// just not pixel-snapped to the low-res grid the way botox's refinement does.
namespace ResSpoof
{
    // ---- menu-facing vars (self-hosted here, see report CONFIG/MENU) ----
    inline bool  g_enable       = false;   // "resolution spoofer"
    inline bool  g_stretch      = true;    // "stretch to fill" -- botox default ON
    inline int   g_width        = 640;
    inline int   g_height       = 480;
    inline bool  g_smooth       = false;   // upscale filter: false = POINT (blocky), true = LINEAR
    inline bool  g_overlayMode  = false;   // "resolution spoofer: overlay" -- our own ImGui gets its own small-RT crunch too
    inline bool  g_netGraph     = false;   // "scale net graph"
    inline bool  g_debugBypass  = false;   // forces the passthrough path (debug)

    // ---- aspect accessor RULE (memory proj-resolution-spoofer.md) --------------------------
    // every aspect READ in this codebase goes through these, never g_aspectRatioEnable/
    // g_aspectRatioValue (world_fx.cpp) or g_sfxAspect* (aspect_stretch.cpp) directly -- stretch
    // mode overrides the plain aspect-ratio slider with the res's own width:height.
    bool  AspectOn();             // stretch-to-fill on, or the plain aspect-ratio toggle
    float AspectWanted();         // stretch-to-fill: res width/height; else the plain slider value
    bool  AspectHud();            // folds into AspectOn() -- see banner, no separate CSS layer
    bool  AspectCrosshair();
    bool  AspectCheatOverlay();   // false while stretch-to-fill's crunch is already widening it (upscale_stretches)
    bool  UpscaleStretches();     // true while CrunchBackbuffer() is the thing doing the widening (stretch-to-fill on)

    // ---- layer 3: the backbuffer crunch ----------------------------------------------------
    // Call AFTER the game + vgui + crosshair have drawn to the backbuffer and BEFORE our own
    // ImGui overlay (render.cpp: right after ScreenFxPasses_Frame(dev), before BuildImGuiFrame()
    // -- all 3 call sites, mutually exclusive per real frame EXCEPT the h_EndScene one, which
    // can run before a later site's s_vguiDrew check skips it; latched the same way Bloom_Frame/
    // ColorCorr_Frame/ScreenFxPasses_Frame already are, so a frame never crunches twice).
    void CrunchBackbuffer(IDirect3DDevice9* dev);
    // h_Present's once-a-frame latch reset block, alongside Bloom_NewFrame()/ColorCorr_NewFrame().
    void NewFrame();

    // ---- overlay mode: our own ImGui through its own small RT ------------------------------
    // Call right after ImGui::NewFrame(), before any ImGui:: draw call.
    void OnImGuiNewFrame();
    // Wraps one ImGui_ImplDX9_RenderDrawData call. dev must have an open BeginScene.
    bool WrapImGuiDraw(IDirect3DDevice9* dev, void (*drawFn)());

    // ---- net graph (botox net_graph_scale) ------------------------------------------------
    // Game thread tick (net_graphheight is a convar write) -- called from world_fx.cpp's
    // existing 100ms tick (WorldFx_Tick), not a new hook.
    void NetGraphTick();

    // ---- preset list (menu combo) ----------------------------------------------------------
    struct Preset { int w, h; const char* label; };
    // Returns botox's 20-entry preset table, filtered to res strictly smaller than
    // (screenW, screenH) in BOTH dimensions (orchestrator's combo: outCount entries, apply via
    // g_width/g_height = list[i].w/h).
    const Preset* Presets(int screenW, int screenH, int& outCount);

    // d3d thread, before Reset (default pool resources) and at eject.
    void OnDeviceLost();
    void Destroy();
}
