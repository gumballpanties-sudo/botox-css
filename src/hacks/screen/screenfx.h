#pragma once
#include "../../globals/globals.h"

// dih screen effects -- the shared depth source, and the passes that read it.
//
// The z buffer made readable: our own INTZ texture, filled by the RESZ driver hack
// (amd/intel) or NvAPI StretchRectEx (nvidia). Ambient occlusion and depth of field
// both want the same texture in the same frame, so the resolve runs once and they
// share it. Ported from coldtires (hacks/visuals/screen/depth_source.cpp).
//
// STAGE A is the depth source plus a readback probe, and the probe is the point: it
// answers, in one build, whether a route works on this card, whether the scene's depth
// surface is still bound where we draw, and which thread the game's own post pass runs
// on. Nothing is drawn over the frame yet.

// Render thread, from h_EndScene before the colour grade, and from the game's own post slot
// when that lands on the render thread. fromPostSlot says which, because the two see
// different depth and the probe has to name the one it read.
void ScreenFx_Frame(IDirect3DDevice9* dev, bool fromPostSlot = false);

// Every D3DPOOL_DEFAULT resource. Dropped before Reset and at eject.
void ScreenFx_Destroy();

// Clears the once-a-frame resolve latch. Called from h_Present.
void ScreenFx_NewFrame();

// IClientMode::DoPostScreenSpaceEffects -- the view setup (fov, z range) and the thread
// the game's own screen-space slot runs on. Called once from PollThread.
void ScreenFx_Install();

// g_pClientMode (client.dll call-site sig), or null. Shared with flip world's CreateMove hook.
void* ScreenFx_FindClientMode();

// IDirect3DDevice9::SetRenderState, slot 57. Watches ONE value: the RESZ trigger code the
// engine writes into D3DRS_POINTSIZE from its own UpdateFullScreenDepthTexture, which
// viewrender.cpp:4523 calls at the top of DrawTranslucentRenderables -- i.e. the exact
// instant opaque geometry is finished and nothing translucent has drawn yet. That is where
// the pre-translucent frame copy is taken. Pass it vt[57].
void ScreenFx_InstallRenderStateHook(void* setRenderStateSlot);

// IDirect3DDevice9::SetDepthStencilSurface 39 / GetDepthStencilSurface 40 -- the "universal
// depth" z swap (botox depth_source route_live). Pass vt[39], vt[40].
void ScreenFx_InstallDepthStencilHooks(void* setSlot, void* getSlot);

// The "depth: %s %s" readout under the dof / true motion blur popups.
const char* ScreenFx_RouteName();
const char* ScreenFx_VerifyName();

// The frame as it stood before any particle drew, or null if the beacon above never fired
// this frame (dx level too low for the engine's depth copy, or a driver that does not take
// the RESZ route). Sampled by the depth of field's prepare pass to tell painted-by-a-
// particle pixels from real geometry.
IDirect3DTexture9* ScreenFx_PreTranslucentTexture();

// IVRenderView::ViewDrawFade -- the gun's own blur pass. Runs after DrawViewModels and
// before the hud, which is the only window where the viewmodel is in the frame and the hud
// is not. Only does anything with dof on and "viewmodel stays sharp" off.
void ScreenFx_ViewmodelFrame(IDirect3DDevice9* dev);

// True once a functor queued through IMatRenderContext::GetCallQueue has actually run on
// the render thread. That is the proof the passes no longer need mat_queue_mode 0.
bool ScreenFx_CallQueueWorking();
// MAIN thread: replay fn on the render thread at this point of the frame. false = not queued
// (mat_queue_mode 0, or no queue) -- caller draws directly. fn = fake CFunctor, never freed.
bool ScreenFx_QueueFunctor(void* fn);
// IMatRenderContext::Flush(false): buffered engine primitives out to d3d (queued under multicore).
bool ScreenFx_FlushRenderContext();

// mat_queue_mode, off the 100ms tick. Forced to 0 while a depth pass is on AND the call
// queue has not proved itself; put back the moment either stops being true.
void ScreenFx_Tick();
void ScreenFx_RestoreQueueMode();   // eject
bool ScreenFx_QueueForced();        // true while the tick above holds mat_queue_mode at 0

// The depth texture a pass samples, or null when no route produced one this frame.
IDirect3DTexture9* ScreenFx_DepthTexture();

// The last CViewSetup the game handed its own post pass. False = the hook never fired or
// the numbers it read were not plausible, and no pass may run without them.
struct FxViewSetup {
    float fov;      // horizontal, degrees
    float zNear;
    float zFar;
    int   width;
    int   height;
    // The gun's own z range (CViewSetup zNearViewmodel / zFarViewmodel). It is drawn through
    // a SEPARATE projection, so a raw depth out of its 0..0.1 slice has to be unprojected
    // with these or the gun reads as sitting on the near plane.
    float zNearVm;
    float zFarVm;
};
bool ScreenFx_View(FxViewSetup& out);
bool ScreenFx_ViewOrigin(float out[3]);   // camera world position, same snapshot as ScreenFx_View
bool ScreenFx_ViewAngles(float out[3]);   // pitch, yaw, roll (QAngle order), same snapshot

// ==== the shared fullscreen quad =====================================================
// ps_3_0 will not pair with fixed-function vertex processing, so every pass here draws
// through one passthrough vs_3_0 and one vertex declaration, both built once.
// Position arrives in clip space; the caller's target size does the half-texel shift.
struct FxQuadVert { float x, y, z, w, u, v; };

bool ScreenFx_QuadReady(IDirect3DDevice9* dev);   // builds the vs + decl on first use
void ScreenFx_BindQuad(IDirect3DDevice9* dev);    // vs, decl -- render states are the caller's
void ScreenFx_DrawQuad(IDirect3DDevice9* dev, int targetW, int targetH);

// ==== ambient occlusion (hacks/screen/ssao.cpp) ==========================================
// Draws into the bound render target. depth is the resolved z buffer, desc the target's.
void Ssao_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
               const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
               const FxViewSetup& view);
void Ssao_Destroy();

// ==== depth of field (hacks/screen/dof.cpp) ==============================================
struct Vec3;   // hacks/movement/movement.h

// Main thread, from the game's own post-processing slot: traces for the focus and smooths
// it. The draw half only ever reads the number this leaves behind.
void Dof_UpdateFocus(const Vec3& origin, const Vec3& angles, float fov, float aspect);   // fov horizontal, aspect w/h -- the focus point x/y ray

// viewmodelPass: the second run, from ViewDrawFade. Only pixels the engine drew into the
// viewmodel's 0..0.1 depth slice get a circle of confusion; everything else comes through
// untouched, so the frame the first pass already finished is not blurred twice.
void Dof_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
              const FxViewSetup& view, bool viewmodelPass = false);
void Dof_Destroy();

// ==== frame blending (hacks/screen/frameblend.cpp) =======================================
// Motion blur off the same depth: a velocity buffer, a tile chain and a reconstruction
// gather. It runs TWICE a frame and the two runs see different frames --
//   gunPass false   the game's post slot. World and players; the gun is not drawn yet.
//   gunPass true    ViewDrawFade, after the gun is drawn and before the hud. The gun only,
//                   and its velocity is its BONES, so a reload or a swing smears while your
//                   own walking and turning do not.
void FrameBlend_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                     const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                     const FxViewSetup& view, bool gunPass = false);

// Main thread, from the same post slot the focus trace runs in: the camera history, the
// reprojection and the players' boxes. The draw half only reads what this leaves behind.
void FrameBlend_UpdateView(const float origin[3], const float angles[3],
                           float fov, float fovVm, float zNearVm, float zFarVm);

// Draw thread, from the DrawModelExecute detour -- one viewmodel part per call. `weapon`
// separates the c_ weapon attachment from the arms stack: they get different shares of the
// sixteen bone slots, or the gun loses every one of them to a hand.
void FrameBlend_CollectBones(const void* pState, const void* pInfo, const void* pBones, bool weapon);

void FrameBlend_NewFrame();   // h_Present: the frame id the bones and the camera pair on
void FrameBlend_Reset();      // device reset / map change: the history is a lie after either
void FrameBlend_Destroy();
