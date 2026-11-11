#include "screenfx.h"
#include "../../render/render.h"       // Render_PresentTid -- which thread may issue raw d3d
#include "../visuals/visuals.h"      // CachedCVar -- mat_queue_mode
#include "../movement/movement.h"     // Vec3 -- the focus trace's origin and angles come off the view setup
#include "../visuals/flipworld.h"    // FlipWorld_Render -- mirrored at the end of the post slot
#include "../visuals/chams_api.h"    // Glow_Render -- ref glow pass, before the original post slot
#include "shaders/fx_probe.h"     // k_fxProbeBytes    -- compiled from fx_probe.hlsl by fxc
#include "shaders/fx_quad_vs.h"   // k_fxQuadVsBytes   -- compiled from fx_quad_vs.hlsl by fxc
#include "../visuals/reflections.h"  // unit D3: Reflections_Draw -- same depth source, same draw point
#include "../visuals/mc_world.h"      // unit D3: MCWorld_UpdateView -- cloud drift off this view setup
#include "../visuals/lsd_chams.h"     // WeaponSheen_Destroy in ScreenFx_Destroy
#include "screenfx_passes.h" // ScreenFxPasses_DepthView -- emphasize/show depth, same draw point
#include "../web/websurface.h"  // Web_OnPostScreenSpaceEffects -- botox do_post_screen_space_effects.cpp:49

// ==== the depth source ==============================================================
// Ported from coldtires (hacks/visuals/screen/depth_source.{h,cpp}). Two routes, both
// driven by us, both independent of mat_dxlevel because they are driver features and not
// material-system ones:
//
//   1. our own INTZ texture + the RESZ hack        amd / intel
//   2. our own INTZ texture + NvAPI StretchRectEx  nvidia
//
// The engine's own _rt_FullFrameDepth is NOT a third route here. Filling it is a material
// system call (CopyRenderTargetToTextureEx), and the engine only makes it when
// SupportsPixelShaders_2_b() -- false at dxlevel 80, which is one of the two modes this
// has to work in. Both routes above are raw d3d on the device we already own.

static const D3DFORMAT k_fmtINTZ = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
static const D3DFORMAT k_fmtRESZ = static_cast<D3DFORMAT>(MAKEFOURCC('R','E','S','Z'));

// The value the driver watches for in D3DRS_POINTSIZE (shaderapidx8.cpp:12487).
static constexpr DWORD k_reszCode = 0x7fa05000;

// ---- nvapi, loaded by hand ---------------------------------------------------------
// Every entry point comes out of nvapi_QueryInterface by id. All __cdecl, NVAPI_OK = 0.
// Already in the process on any nvidia machine, so this pulls in no new dependency.
using NvQueryInterface_t = void* (__cdecl*)(unsigned int);
using NvInitialize_t     = int   (__cdecl*)();
using NvRegister_t       = int   (__cdecl*)(IDirect3DResource9*);
using NvUnregister_t     = int   (__cdecl*)(IDirect3DResource9*);
using NvStretchRectEx_t  = int   (__cdecl*)(IDirect3DDevice9*, IDirect3DResource9*, const RECT*,
                                            IDirect3DResource9*, const RECT*, D3DTEXTUREFILTERTYPE);

static NvRegister_t      s_nvRegister   = nullptr;
static NvUnregister_t    s_nvUnregister = nullptr;
static NvStretchRectEx_t s_nvStretchEx  = nullptr;

// ---- state -------------------------------------------------------------------------
static IDirect3DTexture9* s_ownDepth = nullptr;   // our INTZ copy of the z buffer
static int  s_texW = 0, s_texH = 0;
static bool s_reszOk       = false;
static bool s_nvapiChecked = false;
static bool s_nvapiOk      = false;
static void* s_nvRegTex    = nullptr;             // registering the same resource twice is an error
static bool s_capsLogged   = false;

// ---- route 3, "universal depth" (botox depth_source.cpp route_live, "z swap") -------------
// No driver copy at all: our INTZ IS the game's z buffer. SetDepthStencilSurface (slot 39) maps
// the engine's own depth surface to ours, GetDepthStencilSurface (slot 40) maps it back so the
// engine never caches our pointer (a map load re-reads it without a Reset). INTZ cannot be
// multisampled, so msaa off only. Forced by the switch, or the fallback when neither driver
// route exists on a non-amd card (amd: the gun sinks into walls through an INTZ z test).
enum FxRoute { FX_ROUTE_NONE = 0, FX_ROUTE_RESZ, FX_ROUTE_NVAPI, FX_ROUTE_LIVE };
static const char* k_fxRouteNames[] = { "none", "resz", "nvapi", "z swap" };
static int  s_route        = FX_ROUTE_NONE;
static bool s_amd          = false;
static bool s_live         = false;               // the swap is in force on the device
static bool s_liveForced   = false;               // g_fxUniversalDepth when the source was built
static IDirect3DSurface9* s_ownSurf   = nullptr;  // s_ownDepth level 0, the stand-in z buffer
static IDirect3DSurface9* s_autoDepth = nullptr;  // the engine's own z buffer it stands in for

using SetDS_t = HRESULT(__stdcall*)(IDirect3DDevice9*, IDirect3DSurface9*);
using GetDS_t = HRESULT(__stdcall*)(IDirect3DDevice9*, IDirect3DSurface9**);
static SetDS_t o_SetDepthStencil = nullptr;
static GetDS_t o_GetDepthStencil = nullptr;

// ---- fetch4 (amd, intel gen6+, dxvk) ------------------------------------------------------
// A POINT sampler left at D3DSAMP_MIPMAPLODBIAS = 'GET4' gathers a 2x2 texel block instead of
// one texel. Source turns it on for its shadow depth on cards that have it, and it sticks to
// the stage -- so on a radeon every INTZ read of ours came back as a neighbour's depth, nvidia
// never saw it. 'GET1' turns it off (amd "advanced dx9 capabilities" paper); the game's value
// goes back after, because its own state cache would never send it again. DF24 is the
// detection the paper and aras-p give: off it, 'GET1' would only be a junk lod bias.
static bool s_fetch4 = false;
static constexpr DWORD k_fetch4Off = MAKEFOURCC('G','E','T','1');

struct FxNoFetch4
{
    IDirect3DDevice9* dev;
    DWORD old[16];
    explicit FxNoFetch4(IDirect3DDevice9* d) : dev(s_fetch4 ? d : nullptr)
    {
        if (!dev) return;
        for (DWORD u = 0; u < 16; ++u) {
            if (FAILED(dev->GetSamplerState(u, D3DSAMP_MIPMAPLODBIAS, &old[u]))) old[u] = 0;
            dev->SetSamplerState(u, D3DSAMP_MIPMAPLODBIAS, k_fetch4Off);
        }
    }
    ~FxNoFetch4()
    {
        if (dev) for (DWORD u = 0; u < 16; ++u) dev->SetSamplerState(u, D3DSAMP_MIPMAPLODBIAS, old[u]);
    }
};

// RESZ / StretchRectEx copy the bound z buffer whole into our INTZ. A z buffer of another size
// is undefined on the retail runtime (only the debug runtime validates it) -- garbage or a
// driver fault on amd. Equal or nothing, logged once with both sizes.
static bool FxDepthFits(IDirect3DSurface9* depthSurf, const D3DSURFACE_DESC& bbDesc)
{
    D3DSURFACE_DESC dd{};
    if (SUCCEEDED(depthSurf->GetDesc(&dd)) && dd.Width == bbDesc.Width && dd.Height == bbDesc.Height)
        return true;
    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("^FX: z buffer %ux%u != frame %ux%u -- depth passes skipped while it differs",
                 (unsigned)dd.Width, (unsigned)dd.Height, (unsigned)bbDesc.Width, (unsigned)bbDesc.Height);
    }
    return false;
}

static int  s_frame        = 0;                   // bumped in h_Present
static int  s_resolvedFrm  = -1;                  // one resolve per frame, whoever asks first
static int  s_bbScene      = 0;                   // backbuffer scenes seen so far this frame
static bool s_inOurResolve = false;               // our RESZ, not the engine's -- see h_SetRenderState
static int  s_vmFrame      = -1;                  // frame the viewmodel pass has run for

// ---- the pre-translucent frame ---------------------------------------------------------
// The colour buffer as it stood when the last opaque pixel was written. Everything that
// differs from it in the finished frame was painted by something that wrote no depth.
static IDirect3DTexture9* s_preTex     = nullptr;
static IDirect3DSurface9* s_preSurf    = nullptr;
static IDirect3DSurface9* s_preResolve = nullptr;   // msaa only: the middle hop
static D3DFORMAT          s_preFmt     = D3DFMT_UNKNOWN;
static int  s_preW = 0, s_preH = 0;
static int  s_preFrame = -1;                        // frame the copy was taken on

// ---- the shared fullscreen quad ------------------------------------------------------
static IDirect3DVertexShader9*      s_quadVs   = nullptr;
static IDirect3DVertexDeclaration9* s_quadDecl = nullptr;

// ---- probe ---------------------------------------------------------------------------
static IDirect3DPixelShader9* s_probePs   = nullptr;
static IDirect3DTexture9*     s_probeTex  = nullptr;
static IDirect3DSurface9*     s_probeSurf = nullptr;
static IDirect3DSurface9*     s_probeSys  = nullptr;   // SYSTEMMEM, the lockable end of the readback
static D3DFORMAT              s_probeFmt  = D3DFMT_UNKNOWN;
static int  s_probeCountdown = 0;                      // frames until the next readback
static int  s_probeFrame     = -1;                     // last frame the countdown ticked
static bool s_probePending   = false;                  // readback owed, whichever call site lands first

inline constexpr int k_probeW = 64;
inline constexpr int k_probeH = 36;
inline constexpr int k_probeInterval = 300;            // ~5s at 60fps

// ---- depth health ----------------------------------------------------------------------
// No pass draws until a probe has seen real depth at that draw point. A dead resolve reads as
// the far plane everywhere, which blurs the whole frame -- exactly what the EndScene fallback
// produced under multicore. Tracked per draw point: [0] EndScene, [1] the game's post slot.
enum class FxHealth { Unknown, Live, Dead };
struct FxHealthState { FxHealth health = FxHealth::Unknown; int deadStreak = 0; int wait = 0; };
static FxHealthState s_health[2];
static bool          s_healthStale = true;          // left the game since the last verdict

inline constexpr int k_healthUnknown = 20;    // frames between probes while undecided
inline constexpr int k_healthDead    = 120;   // ... after a dead verdict, so it recovers
// 20s, not the 60 it was: this is how long a route that died SILENTLY (the resolve reports
// success and writes nothing) keeps the whole frame blurred before the probe notices. One
// 64x36 readback is the cost, and the nvapi route reports its own failures for free below.
inline constexpr int k_healthLive    = 1200;  // ... slow re-check of a live one
inline constexpr int k_healthDeadN   = 3;     // consecutive dead probes (a sky-only view reads dead)

// ---- route recovery --------------------------------------------------------------------
// A DEPTH ROUTE THAT DIES MID-SESSION NEVER CAME BACK. Nothing releases the INTZ or re-registers
// it with nvapi outside a device reset, so once the resolve stops filling the texture every
// later frame samples the same empty depth: the whole frame blurred, then ao and dof held off
// by the probe, until the game is restarted. Both detectors below ask for a rebuild instead --
// ScreenFx_Destroy drops the texture, the probe targets and the registration, and the FxEnsure
// on the next frame builds and re-registers the lot.
static bool s_rebuildWanted = false;
static int  s_rebuildFrame  = -100000;        // s_frame of the last rebuild
static int  s_rebuildTries  = 0;              // in a row, without the depth reading live since
inline constexpr int k_rebuildCooldown = 120; // ~2s between attempts, so a bad route cannot thrash
inline constexpr int k_rebuildMaxTries = 4;   // ... and a route that is gone for good stops asking

// ---- the view setup, snapshotted off the game's own post pass -------------------------
// Written on the main thread by the DoPostScreenSpaceEffects hook, read on the render
// thread by the passes. Plain floats: a torn read costs one frame of a slightly wrong
// focal distance, never a crash, and a lock on the render thread costs every frame.
static volatile float s_viewFov   = 0.f;
static volatile float s_viewNear  = 0.f;
static volatile float s_viewFar   = 0.f;
static volatile float s_viewNearVm = 0.f;
static volatile float s_viewFarVm  = 0.f;
static volatile int   s_viewW     = 0;
static volatile int   s_viewH     = 0;
static volatile bool  s_viewValid = false;
// camera world-space origin + angles, same snapshot -- added 11-04 so ssao.cpp can place a
// world-space smoke grenade centre (hud_aim.cpp CollectSmoke, via HudSnap) into the AO
// composite shader's camera-local frame. Same torn-read tolerance as the rest of this block:
// worst case one frame late.
static volatile float s_viewOriginX = 0.f, s_viewOriginY = 0.f, s_viewOriginZ = 0.f;
static volatile float s_viewAngPitch = 0.f, s_viewAngYaw = 0.f, s_viewAngRoll = 0.f;

// Widest setup this session: the main view. Cleared with the device, because a resolution
// change makes the old maximum a filter nothing can pass.
static int   s_maxSetupW   = 0;
static DWORD s_setupTid    = 0;   // thread the game's post slot runs on
static DWORD s_endSceneTid = 0;   // thread our EndScene draw runs on

// EVERY pass that samples the depth source. One list, asked by the post-slot hook, ScreenFx_Frame
// and the queue-mode force alike: reflections, emphasize and show depth were missing from the
// hook's copy, so with only those ticked nothing ran at the post slot, the EndScene fallback
// took them -- and EndScene's depth reads far everywhere (probe: 2290 of 2304 taps), so the
// health check held them off and its LIVE/DEAD flapping rebuilt the depth source every few
// seconds.
static bool FxWantDepthPass()
{
    return g_aoEnable || g_dofEnable || g_fbEnable || g_reflEnable || g_emphasizeEnable || g_showDepthEnable ||
           ScreenFxPasses_WarpWanted();
}

bool ScreenFx_View(FxViewSetup& out)
{
    if (!s_viewValid) return false;
    out.fov    = s_viewFov;
    out.zNear  = s_viewNear;
    out.zFar   = s_viewFar;
    out.width  = s_viewW;
    out.height = s_viewH;
    // The gun's range, with the main view's own as the stand-in when the setup carried
    // nothing usable there -- the viewmodel pass then blurs by a wrong distance rather than
    // dividing by zero.
    out.zNearVm = (s_viewNearVm > 0.f && s_viewFarVm > s_viewNearVm) ? s_viewNearVm : s_viewNear;
    out.zFarVm  = (s_viewNearVm > 0.f && s_viewFarVm > s_viewNearVm) ? s_viewFarVm  : s_viewFar;
    return out.zFar > out.zNear && out.zNear > 0.f;
}

bool ScreenFx_ViewOrigin(float out[3])
{
    if (!s_viewValid) return false;
    out[0] = s_viewOriginX; out[1] = s_viewOriginY; out[2] = s_viewOriginZ;
    return true;
}

bool ScreenFx_ViewAngles(float out[3])   // pitch, yaw, roll -- QAngle order
{
    if (!s_viewValid) return false;
    out[0] = s_viewAngPitch; out[1] = s_viewAngYaw; out[2] = s_viewAngRoll;
    return true;
}

IDirect3DTexture9* ScreenFx_DepthTexture()
{
    if (!s_ownDepth) return nullptr;
    // z swap: only once the swap is live -- before that the INTZ holds nothing
    if (s_route == FX_ROUTE_LIVE) return s_live ? s_ownDepth : nullptr;
    return (s_route != FX_ROUTE_NONE) ? s_ownDepth : nullptr;
}

const char* ScreenFx_RouteName()
{
    return k_fxRouteNames[(s_route >= 0 && s_route <= FX_ROUTE_LIVE) ? s_route : 0];
}

// botox verify_name, off our probe's verdict at the post slot (the draw point that counts)
const char* ScreenFx_VerifyName()
{
    if (s_route == FX_ROUTE_NONE) {
        if (!s_texW) return "";   // never built: no depth pass has run yet
        return s_amd ? "amd: turn msaa on" : "msaa on + no driver route";
    }
    if (s_route == FX_ROUTE_LIVE && !s_live) return "checking";
    switch (s_health[1].health) {
        case FxHealth::Live: return "ok";
        case FxHealth::Dead: return "dead";
        default:             return "checking";
    }
}

// THIS FRAME'S copy or nothing. A stale one is worse than none: the camera has moved, so
// every pixel differs from it and the guard would hold the entire screen sharp.
IDirect3DTexture9* ScreenFx_PreTranslucentTexture()
{
    return (s_preTex && s_preFrame == s_frame) ? s_preTex : nullptr;
}

static void FxWantRebuild(const char* why)
{
    if (s_rebuildWanted) return;
    // Rebuilding on a loop is worse than staying dark: it is a resource churn every two seconds
    // for the rest of the session. Give up after four in a row; a live verdict zeroes the count,
    // and so does a device reset or a resolution change, which rebuild everything anyway.
    if (s_rebuildTries >= k_rebuildMaxTries) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("^FX: depth route stayed dead over %d rebuilds -- leaving it off", k_rebuildMaxTries); }
        return;
    }
    s_rebuildWanted = true;
    DebugLog("^FX: depth route rebuild asked for -- %s", why);
}

void ScreenFx_NewFrame()
{
    ++s_frame;
    // the id the bone sets and the camera history pair on: a set measured on the frame before
    // this one is the pair the gun's velocity is measured across
    FrameBlend_NewFrame();
    s_bbScene      = 0;
    // A probe frame owes ONE line per backbuffer scene, so the flag dies with the frame and
    // not with the first readback.
    s_probePending = false;

    // z swap, between frames (h_Present, the render thread). Nothing reads the depth any more,
    // or the switch went off: hand the engine its own z buffer back -- Destroy does the swap
    // back. Otherwise go live: from here the hook maps every bind of the engine's z to ours.
    if (s_route == FX_ROUTE_LIVE && g_d3dDev) {
        if (g_eject || !(FxWantDepthPass() || g_fxDepthProbe) || g_fxUniversalDepth != s_liveForced) {
            ScreenFx_Destroy();
        } else if (!s_live && s_ownSurf && s_autoDepth && o_GetDepthStencil) {
            s_live = true;
            IDirect3DSurface9* bound = nullptr;
            if (SUCCEEDED(o_GetDepthStencil(g_d3dDev, &bound)) && bound) {
                if (bound == s_autoDepth) g_d3dDev->SetDepthStencilSurface(s_ownSurf);
                bound->Release();
            }
            DebugLog("^FX: z swap live -- own INTZ is the z buffer from this frame");
        }
    }
}

// ==== nvapi =========================================================================
static bool FxNvapiReady()
{
    if (s_nvapiChecked) return s_nvapiOk;
    s_nvapiChecked = true;

    HMODULE mod = GetModuleHandleA("nvapi64.dll");
    if (!mod) mod = GetModuleHandleA("nvapi.dll");
    if (!mod) mod = LoadLibraryA("nvapi64.dll");
    if (!mod) {
        DebugLog("screenfx: no nvapi64.dll -- depth resolve has no nvidia route");
        return false;
    }

    const auto query = reinterpret_cast<NvQueryInterface_t>(GetProcAddress(mod, "nvapi_QueryInterface"));
    if (!query) { DebugLog("screenfx: nvapi_QueryInterface missing"); return false; }

    NvInitialize_t init = nullptr;
    __try {
        init           = reinterpret_cast<NvInitialize_t>(query(0x0150E828));
        s_nvRegister   = reinterpret_cast<NvRegister_t>(query(0xA064BDFC));
        s_nvUnregister = reinterpret_cast<NvUnregister_t>(query(0xBB2B17AA));
        s_nvStretchEx  = reinterpret_cast<NvStretchRectEx_t>(query(0x22DE03AA));
    } __except(EXCEPTION_EXECUTE_HANDLER) { init = nullptr; }

    if (!init || !s_nvRegister || !s_nvStretchEx) {
        DebugLog("screenfx: nvapi entry points missing (init=%p reg=%p stretch=%p)",
                 (void*)init, (void*)s_nvRegister, (void*)s_nvStretchEx);
        return false;
    }

    int status = -1;
    __try { status = init(); } __except(EXCEPTION_EXECUTE_HANDLER) { status = -1; }

    s_nvapiOk = (status == 0);
    DebugLog("screenfx: nvapi init %d", status);
    return s_nvapiOk;
}

static void FxResolveNvapi(IDirect3DDevice9* dev, IDirect3DSurface9* depthSurf)
{
    if (!dev || !depthSurf || !s_ownDepth || !s_nvStretchEx || !s_nvRegister) return;

    // Our own texture: registered once and kept, because we own its lifetime and
    // ScreenFx_Destroy unregisters it before it is released.
    // A non-zero status is KEPT: the engine registers the same surfaces for its own
    // resolve and "already registered" must not read as a dead end. StretchRectEx below
    // reports NVAPI_UNREGISTERED_RESOURCE if it really was not.
    if (s_nvRegTex != s_ownDepth) {
        if (s_nvRegTex && s_nvUnregister) s_nvUnregister((IDirect3DResource9*)s_nvRegTex);
        const int st = s_nvRegister(s_ownDepth);
        s_nvRegTex = s_ownDepth;
        DebugLog("screenfx: nvapi register texture %d", st);
    }

    // THE ENGINE'S DEPTH SURFACE IS NOT OURS, so its registration lives and dies inside this
    // one call, while the caller still holds a reference to it. A map load makes the material
    // system restore the device without ever calling IDirect3DDevice9::Reset, so that surface
    // is freed and remade with nothing to tell us. Keeping the registration across it left the
    // driver holding a freed surface and then unregistered that freed pointer next frame --
    // the load-in crash with dof/ao on, landing in materialsystem/shaderapidx9 with no frame
    // of ours in the stack.
    const int reg = s_nvRegister((IDirect3DResource9*)depthSurf);
    const int st = s_nvStretchEx(dev, depthSurf, nullptr, s_ownDepth, nullptr, D3DTEXF_POINT);
    if (s_nvUnregister) s_nvUnregister((IDirect3DResource9*)depthSurf);

    static int s_lastStatus = -1;
    if (st != s_lastStatus) { s_lastStatus = st; DebugLog("screenfx: nvapi resolve %d (register %d)", st, reg); }

    // NVAPI_OK is 0. A failed StretchRectEx wrote NOTHING, so the texture the passes are about
    // to sample is whatever was in it last -- and it fails every frame from then on, because
    // the registration it needs is remade here and refused every time. Two in a row before
    // asking, so one bad frame during a resolution change is not worth a rebuild.
    static int s_failStreak = 0;
    if (st != 0) { if (++s_failStreak >= 2) FxWantRebuild("nvapi resolve is failing"); }
    else s_failStreak = 0;
}

// ==== RESZ ==========================================================================
// Bind an INTZ texture to sampler 0, draw one dummy point, then write the magic code into
// D3DRS_POINTSIZE -- the driver resolves the CURRENTLY BOUND depth surface into it, msaa
// or not. The sequence is CShaderAPIDx8::CopyRenderTargetToTextureEx's own.
static void FxResolveResz(IDirect3DDevice9* dev)
{
    if (!dev || !s_ownDepth) return;

    static const float dummy[3] = { 0.f, 0.f, 0.f };

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZ);
    dev->SetTexture(0, s_ownDepth);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);

    // The point is never rasterised -- colour writes are off. It exists only to make sure
    // sampler 0 is really bound before the trigger below.
    dev->DrawPrimitiveUP(D3DPT_POINTLIST, 1, dummy, 3 * sizeof(float));

    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);

    // This pair IS the resolve. Without the second write it only fires on the first frame.
    // s_inOurResolve keeps h_SetRenderState from reading OUR trigger as the engine's own --
    // the two write the identical code and only the flag tells them apart.
    s_inOurResolve = true;
    dev->SetRenderState(D3DRS_POINTSIZE, k_reszCode);
    dev->SetRenderState(D3DRS_POINTSIZE, 0);
    s_inOurResolve = false;

    dev->SetTexture(0, nullptr);
}

// ==== resources =====================================================================
void ScreenFx_Destroy()
{
    // nvapi has to let go BEFORE the resource dies, or it holds a freed pointer. Only our own
    // texture is ever registered past the end of a resolve (see FxResolveNvapi).
    if (s_nvUnregister && s_nvRegTex) s_nvUnregister((IDirect3DResource9*)s_nvRegTex);
    s_nvRegTex = nullptr;

    Ssao_Destroy();
    Dof_Destroy();
    FrameBlend_Destroy();
    Reflections_Destroy();   // DEFAULT-pool scene copy: a live one fails the Reset
    MCWorld_Destroy();       // cloud/sun textures may be DEFAULT-pool on a d3d9ex device
    WeaponSheen_Destroy();   // sheen mask frames + cubemaps, DEFAULT-pool

    if (s_preResolve){ s_preResolve->Release();s_preResolve= nullptr; }
    if (s_preSurf)   { s_preSurf->Release();   s_preSurf   = nullptr; }
    if (s_preTex)    { s_preTex->Release();    s_preTex    = nullptr; }
    if (s_quadDecl)  { s_quadDecl->Release();  s_quadDecl  = nullptr; }
    if (s_quadVs)    { s_quadVs->Release();    s_quadVs    = nullptr; }
    if (s_probeSys)  { s_probeSys->Release();  s_probeSys  = nullptr; }
    if (s_probeSurf) { s_probeSurf->Release(); s_probeSurf = nullptr; }
    if (s_probeTex)  { s_probeTex->Release();  s_probeTex  = nullptr; }
    if (s_probePs)   { s_probePs->Release();   s_probePs   = nullptr; }

    // z swap: the engine's z buffer goes back on the device FIRST -- a bound INTZ is a live
    // DEFAULT-pool reference and the Reset after this would fail on it. Off the render thread
    // (eject, poll thread) the hooks are already disabled and the engine's next bind of its own
    // surface goes straight through, so the device is not touched from there.
    s_live = false;
    if (s_ownSurf) {
        const bool renderThread = !g_eject || GetCurrentThreadId() == Render_PresentTid();
        IDirect3DSurface9* bound = nullptr;
        if (renderThread && g_d3dDev && o_GetDepthStencil &&
            SUCCEEDED(o_GetDepthStencil(g_d3dDev, &bound)) && bound) {
            if (bound == s_ownSurf && s_autoDepth) g_d3dDev->SetDepthStencilSurface(s_autoDepth);
            bound->Release();
        }
        s_ownSurf->Release(); s_ownSurf = nullptr;
    }
    if (s_autoDepth) { s_autoDepth->Release(); s_autoDepth = nullptr; }
    s_route = FX_ROUTE_NONE;

    if (s_ownDepth)  { s_ownDepth->Release();  s_ownDepth  = nullptr; }

    s_texW = s_texH = 0;
    s_probeFmt      = D3DFMT_UNKNOWN;
    s_resolvedFrm   = -1;
    s_rebuildWanted = false;   // whatever asked for one, this IS it
    s_health[0] = s_health[1] = FxHealthState{};
    s_maxSetupW   = 0;
    s_preW = s_preH = 0;
    s_preFmt   = D3DFMT_UNKNOWN;
    s_preFrame = -1;
    s_vmFrame  = -1;
}

// Our own INTZ at frame size, plus the probe's targets. False = there is no depth route
// this frame and the caller has nothing to sample.
static bool FxEnsure(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    if (!dev) return false;
    // Built already: the answer is whether a route came out of it, not whether the texture
    // exists -- an INTZ nothing can resolve into is not a depth source.
    if (s_ownDepth && s_texW == (int)desc.Width && s_texH == (int)desc.Height &&
        s_liveForced == g_fxUniversalDepth)
        return s_route != FX_ROUTE_NONE;

    // A new frame size means a device reset or a video setting change, and a route that was
    // dead before it is worth trying again -- so the give-up count starts over here.
    if (s_texW) s_rebuildTries = 0;

    ScreenFx_Destroy();

    IDirect3D9* d3d = nullptr;
    if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d)
    {
        const bool resz = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                        D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET,
                                        D3DRTYPE_SURFACE, k_fmtRESZ));
        const bool intz = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                        D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL,
                                        D3DRTYPE_TEXTURE, k_fmtINTZ));
        s_reszOk = resz && intz;
        s_fetch4 = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                        D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
                                        static_cast<D3DFORMAT>(MAKEFOURCC('D','F','2','4'))));

        D3DADAPTER_IDENTIFIER9 id{};
        if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
            s_amd = (id.VendorId == 0x1002);
            if (!s_capsLogged) {
                s_capsLogged = true;
                DebugLog("screenfx: adapter %s vendor 0x%04x", id.Description, (unsigned)id.VendorId);
            }
        }
        DebugLog("screenfx: resz=%d intz=%d fetch4=%d", (int)resz, (int)intz, (int)s_fetch4);
        d3d->Release();
    }

    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_DEPTHSTENCIL,
                                    k_fmtINTZ, D3DPOOL_DEFAULT, &s_ownDepth, nullptr);
    if (FAILED(hr) || !s_ownDepth) {
        s_ownDepth = nullptr;
        s_reszOk   = false;
        DebugLog("screenfx: own INTZ creation FAILED hr=0x%08lx -- no depth route", (unsigned long)hr);
        return false;
    }

    // Asked HERE and not at the resolve: ScreenFx_DepthTexture answers off the result, and
    // a caller that bails on it before the first resolve leaves nvidia permanently unchecked.
    if (!s_reszOk) FxNvapiReady();

    s_texW = (int)desc.Width;
    s_texH = (int)desc.Height;
    s_liveForced = g_fxUniversalDepth;

    s_route = s_reszOk ? FX_ROUTE_RESZ : (s_nvapiOk ? FX_ROUTE_NVAPI : FX_ROUTE_NONE);

    // botox ensure(): forced, or no driver route on a non-amd card
    if ((s_liveForced || (s_route == FX_ROUTE_NONE && !s_amd)) && o_SetDepthStencil && o_GetDepthStencil)
    {
        IDirect3DSurface9* bound = nullptr;
        D3DSURFACE_DESC bd{};
        if (SUCCEEDED(o_GetDepthStencil(dev, &bound)) && bound && SUCCEEDED(bound->GetDesc(&bd)) &&
            bd.MultiSampleType == D3DMULTISAMPLE_NONE && bd.Format != k_fmtINTZ &&
            bd.Width == desc.Width && bd.Height == desc.Height &&
            SUCCEEDED(s_ownDepth->GetSurfaceLevel(0, &s_ownSurf)) && s_ownSurf)
        {
            s_autoDepth = bound;   // keeps the ref
            bound = nullptr;
            s_route = FX_ROUTE_LIVE;
            DebugLog("^FX: z swap picked (forced=%d) -- own INTZ becomes the z buffer next frame",
                     (int)s_liveForced);
        }
        else
        {
            DebugLog("^FX: z swap skipped -- z buffer %s (msaa %u fmt %u %ux%u), universal depth needs msaa off",
                     bound ? "unusable" : "not bound", (unsigned)bd.MultiSampleType, (unsigned)bd.Format,
                     (unsigned)bd.Width, (unsigned)bd.Height);
        }
        if (bound) bound->Release();
    }

    if (s_route == FX_ROUTE_NONE) {
        DebugLog("screenfx: neither RESZ nor NvAPI -- depth resolve impossible on this card%s",
                 s_amd ? " (amd: turn msaa on)" : "");
        return false;
    }
    DebugLog("screenfx: depth source up %dx%d route=%s", s_texW, s_texH, k_fxRouteNames[s_route]);
    return true;
}

// ==== the shared quad ===============================================================
bool ScreenFx_QuadReady(IDirect3DDevice9* dev)
{
    if (s_quadVs && s_quadDecl) return true;
    if (!dev) return false;

    if (!s_quadVs) {
        std::vector<DWORD> code((sizeof(k_fxQuadVsBytes) + 3) / 4);
        memcpy(code.data(), k_fxQuadVsBytes, sizeof(k_fxQuadVsBytes));
        if (FAILED(dev->CreateVertexShader(code.data(), &s_quadVs))) {
            DebugLog("screenfx: quad vertex shader creation failed");
            s_quadVs = nullptr;
            return false;
        }
    }

    if (!s_quadDecl) {
        static const D3DVERTEXELEMENT9 k_elems[] = {
            { 0,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            D3DDECL_END()
        };
        if (FAILED(dev->CreateVertexDeclaration(k_elems, &s_quadDecl))) {
            DebugLog("screenfx: quad vertex declaration creation failed");
            s_quadDecl = nullptr;
            return false;
        }
    }
    return true;
}

void ScreenFx_BindQuad(IDirect3DDevice9* dev)
{
    dev->SetVertexShader(s_quadVs);
    dev->SetVertexDeclaration(s_quadDecl);
}

// The half-texel shift is on the POSITION, in clip space, and it is the target's texel and
// not the source's: without it every pass samples half a pixel off and the whole screen
// creeps diagonally.
void ScreenFx_DrawQuad(IDirect3DDevice9* dev, int targetW, int targetH)
{
    const float dx = (targetW > 0) ? 1.f / (float)targetW : 0.f;
    const float dy = (targetH > 0) ? 1.f / (float)targetH : 0.f;

    const FxQuadVert quad[4] = {
        { -1.f - dx,  1.f + dy, 0.f, 1.f, 0.f, 0.f },
        {  1.f - dx,  1.f + dy, 0.f, 1.f, 1.f, 0.f },
        { -1.f - dx, -1.f + dy, 0.f, 1.f, 0.f, 1.f },
        {  1.f - dx, -1.f + dy, 0.f, 1.f, 1.f, 1.f },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(FxQuadVert));
}

// ==== state save / restore ==========================================================
// Everything the resolve and the probe touch. DrawPrimitiveUP is in the list for the
// reason most hooks miss: D3D9 nulls stream source 0 and the index buffer behind it, and
// the material system caches both -- the next engine mesh that lands on one of those cache
// hits then draws out of a NULL stream and renders nothing, silently.
struct FxSavedState
{
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex0;
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    IDirect3DSurface9*           rt0;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DVIEWPORT9 vp;
    DWORD  rs[10];
    DWORD  samp[6];
};

static const D3DRENDERSTATETYPE k_fxRS[10] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_POINTSIZE,
};
static const D3DSAMPLERSTATETYPE k_fxSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void FxSave(IDirect3DDevice9* dev, FxSavedState& s)
{
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetRenderTarget(0, &s.rt0);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 10; ++i) dev->GetRenderState(k_fxRS[i], &s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_fxSamp[i], &s.samp[i]);
}

static void FxRestore(IDirect3DDevice9* dev, FxSavedState& s)
{
    for (int i = 0; i < 10; ++i) dev->SetRenderState(k_fxRS[i], s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_fxSamp[i], s.samp[i]);
    if (s.rt0) dev->SetRenderTarget(0, s.rt0);
    dev->SetViewport(&s.vp);
    dev->SetTexture(0, s.tex0);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    // SetFVF and SetVertexDeclaration write the same slot, so only the one in force goes
    // back: a decl if there was one, otherwise the FVF.
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    if (s.tex0)    s.tex0->Release();
    if (s.ps)      s.ps->Release();
    if (s.vs)      s.vs->Release();
    if (s.decl)    s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
    if (s.rt0)     s.rt0->Release();
}

// ==== the call queue -- the bypass for mat_queue_mode ===============================
// THE ENGINE'S OWN ANSWER to "run this on the render thread at this exact point in the
// frame". With multicore rendering the material system records the main thread's calls and
// replays them on the render thread, and ICallQueue is the seam: anything queued through it
// replays IN ORDER, at the position it was queued. viewpostprocess.cpp:2241 does exactly
// this with the engine's own DoEnginePostProcessing when r_queued_post_processing is on.
//
// So the passes do not need mat_queue_mode 0 at all. Queued from the game's post slot on
// the main thread, the functor runs on the render thread at the post slot's own position --
// which is the one place measured to hold live depth (see the wipe block below). That is
// the whole ~100fps: multicore rendering stays on.
//
// SLOTS. IMaterialSystem::GetRenderContext 98 is already confirmed in session_facts.
// IMatRenderContext::GetCallQueue counts to 147 by the method that reproduces all nine
// IMatRenderContext slots confirmed there (SetRenderTarget 6, GetRenderTarget 7,
// ClearBuffers 12, Viewport 38, CullMode 40, ClearColor4ub 73, OverrideDepthEnable 74,
// DrawScreenSpaceRectangle 103, SetStencilEnable 117) with no X360-only virtual in front of
// it. QueueFunctorInternal is ICallQueue's ONLY virtual, so slot 0.
//
// THE FUNCTOR IS FAKED, because CFunctor is the engine's type and we cannot construct one.
// Its vtable is IRefCounted's two entries plus the two CFunctor declares, in order:
//   0 AddRef   1 Release   2 ~CFunctor (scalar deleting)   3 operator()
// Ours is a file-static that is never freed, so AddRef/Release just answer 1 and the
// destructor is a no-op -- QueueFunctor AddRefs on the way in and the queue Releases after
// the call, and neither may free something that was never allocated.
inline constexpr int k_slotGetRenderContext = 98;
inline constexpr int k_slotGetCallQueue     = 147;
inline constexpr int k_slotQueueFunctor     = 0;

static volatile bool s_cqRan     = false;   // a queued functor has actually executed
static volatile int  s_cqQueued  = 0;       // functors handed to the queue
static bool          s_cqRefused = false;   // the queue said no, or never ran what it took

struct FxFunctor { void** vfptr; unsigned userId; };

static int   __fastcall FxFn_AddRef (void*)           { return 1; }
static int   __fastcall FxFn_Release(void*)           { return 1; }
static void* __fastcall FxFn_Dtor   (void* p, unsigned) { return p; }   // never freed

static void __fastcall FxFn_Call(void*)
{
    // The render thread, at the frame position the post slot was queued from. Everything
    // ScreenFx_Frame does from the post slot under mat_queue_mode 0 is legal here.
    s_cqRan = true;
    ScreenFx_Frame(g_d3dDev, true);
}

// The gun's pass, queued the same way from IVRenderView::ViewDrawFade. Its own functor
// because the queue calls operator() with nothing to say which site put it there.
static void __fastcall FxFn_CallVm(void*)
{
    s_cqRan = true;
    ScreenFx_ViewmodelFrame(g_d3dDev);
}

static void* s_fxFnVt[4] = {
    (void*)&FxFn_AddRef, (void*)&FxFn_Release, (void*)&FxFn_Dtor, (void*)&FxFn_Call
};
static void* s_fxFnVmVt[4] = {
    (void*)&FxFn_AddRef, (void*)&FxFn_Release, (void*)&FxFn_Dtor, (void*)&FxFn_CallVm
};
static FxFunctor s_fxFunctor   = { s_fxFnVt,   0 };
static FxFunctor s_fxFunctorVm = { s_fxFnVmVt, 1 };

// Main thread, from the post slot. False = no queue (single threaded already, or the slots
// did not lead anywhere), and the EndScene fallback stands.
static bool FxQueueOnRenderThread(FxFunctor* fn)
{
    if (!g_matSystem) return false;

    __try {
        void** mvt = *(void***)g_matSystem;
        if (reinterpret_cast<uintptr_t>(mvt) < 0x10000) return false;

        using GetRc_t = void*(__fastcall*)(void*);
        void* rc = ((GetRc_t)mvt[k_slotGetRenderContext])(g_matSystem);
        if (reinterpret_cast<uintptr_t>(rc) < 0x10000) return false;

        void** rvt = *(void***)rc;
        if (reinterpret_cast<uintptr_t>(rvt) < 0x10000) return false;

        // NULL here is not a failure: it is the material system saying this context is not
        // queued, which is what mat_queue_mode 0 looks like from the inside.
        using GetCq_t = void*(__fastcall*)(void*);
        void* cq = ((GetCq_t)rvt[k_slotGetCallQueue])(rc);
        if (reinterpret_cast<uintptr_t>(cq) < 0x10000) return false;

        void** qvt = *(void***)cq;
        if (reinterpret_cast<uintptr_t>(qvt) < 0x10000) return false;

        using Queue_t = void(__fastcall*)(void*, void*);
        ((Queue_t)qvt[k_slotQueueFunctor])(cq, fn);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ScreenFx_CallQueueWorking() { return s_cqRan; }

// IMatRenderContext::Flush(false) -- slot 4, counted off public/materialsystem/imaterialsystem.h
// with IRefCounted's AddRef 0 / Release 1 in front: BeginRender 2, EndRender 3, Flush 4,
// BindLocalCubemap 5, SetRenderTarget 6 -- and 6, 7 and 12 are the confirmed anchors above.
// Pushes the shaderapi's buffered dynamic-mesh primitives out to d3d, so raw d3d issued right
// after an engine draw really lands after it. Queued context: the flush is queued too, ahead of
// whatever is queued next. False = no material system or the slots led nowhere.
inline constexpr int k_slotRcFlush = 4;
bool ScreenFx_FlushRenderContext()
{
    if (!g_matSystem) return false;
    __try {
        void** mvt = *(void***)g_matSystem;
        if (reinterpret_cast<uintptr_t>(mvt) < 0x10000) return false;
        using GetRc_t = void*(__fastcall*)(void*);
        void* rc = ((GetRc_t)mvt[k_slotGetRenderContext])(g_matSystem);
        if (reinterpret_cast<uintptr_t>(rc) < 0x10000) return false;
        void** rvt = *(void***)rc;
        if (reinterpret_cast<uintptr_t>(rvt) < 0x10000) return false;
        using Flush_t = void(__fastcall*)(void*, bool);
        ((Flush_t)rvt[k_slotRcFlush])(rc, false);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Same queue for other callers (render.cpp's overlay functor). fn must start with the fake
// CFunctor vtable described above and never be freed.
bool ScreenFx_QueueFunctor(void* fn) { return FxQueueOnRenderThread(static_cast<FxFunctor*>(fn)); }

// ==== the depth wipe =================================================================
// MEASURED (2026-09-09, msaa 8x): the game's post slot reads live depth (min=0.911); EndScene
// reads 1.0 on every tap. A resolve hooked in front of full-size depth clear #2 read 1.0 as
// well -- that clear follows the 3D skybox, so it held skybox depth, and marking the frame
// resolved there made the post slot skip its own good resolve. So: resolve at the post slot
// position only (direct, or replayed there by the call queue), never from a Clear hook.

// ==== the pre-translucent frame copy ================================================
// WHAT THIS IS FOR. Crits, muzzle flashes, rockets, explosions, beams and every other
// particle draw with z writes OFF -- viewrender.cpp:4520 says so in the engine's own words
// ("opaques generally write depth, and translucents generally don't"). So the depth under a
// crit spark is the wall thirty metres behind it, the circle of confusion is computed off
// that wall, and the spark comes out as blurred as the wall is. Nothing in the depth buffer
// can tell the two apart, because as far as it is concerned the spark was never drawn.
//
// The colour buffer can. Copy the frame at the instant the last opaque pixel is written and
// compare at the end: whatever changed was painted by something with no depth of its own.
// That is the mask, and fx_dof_prepare.hlsl divides the blur out of it.
//
// THE INSTANT. viewrender.cpp:4523 -- DrawTranslucentRenderables opens by calling
// UpdateFullScreenDepthTexture() for VIEW_MAIN, before one translucent renderable is
// touched. That fills _rt_FullFrameDepth through CopyRenderTargetToTextureEx, which on a
// depth texture is the RESZ hack: bind, dummy point, D3DRS_POINTSIZE = 0x7fa05000. So the
// engine writing that one value into that one render state IS the boundary, and it arrives
// on the render thread at the right frame position no matter what mat_queue_mode is doing.
//
// It is not free of conditions: UpdateFullScreenDepthTexture returns immediately unless
// SupportsPixelShaders_2_b(), and a driver whose depth copy takes another route never
// writes the code. Both cases just leave the copy untaken, ScreenFx_PreTranslucentTexture
// answers null, and the blur behaves exactly as it did before. The log says which happened.
static bool FxEnsurePre(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    if (s_preTex && s_preW == (int)desc.Width && s_preH == (int)desc.Height &&
        s_preFmt == desc.Format &&
        ((desc.MultiSampleType == D3DMULTISAMPLE_NONE) == (s_preResolve == nullptr)))
        return true;

    if (s_preResolve){ s_preResolve->Release();s_preResolve= nullptr; }
    if (s_preSurf)   { s_preSurf->Release();   s_preSurf   = nullptr; }
    if (s_preTex)    { s_preTex->Release();    s_preTex    = nullptr; }
    s_preW = s_preH = 0;
    s_preFmt = D3DFMT_UNKNOWN;

    // The back buffer's own format, so StretchRect takes it without a conversion.
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                  D3DPOOL_DEFAULT, &s_preTex, nullptr)) || !s_preTex ||
        FAILED(s_preTex->GetSurfaceLevel(0, &s_preSurf)))
    {
        if (s_preTex) { s_preTex->Release(); s_preTex = nullptr; }
        s_preSurf = nullptr;
        DebugLog("screenfx: pre-translucent copy target creation failed -- particles stay in the blur");
        return false;
    }

    // msaa: a multisampled surface cannot be blitted into a texture, only resolved into a
    // plain render target first. Same two hops the dof's own frame copy takes.
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE &&
        FAILED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE,
                                       0, FALSE, &s_preResolve, nullptr)))
    {
        s_preSurf->Release(); s_preSurf = nullptr;
        s_preTex->Release();  s_preTex  = nullptr;
        DebugLog("screenfx: pre-translucent msaa resolve target failed -- particles stay in the blur");
        return false;
    }

    s_preW   = (int)desc.Width;
    s_preH   = (int)desc.Height;
    s_preFmt = desc.Format;
    DebugLog("screenfx: pre-translucent copy up %dx%d msaa %d", s_preW, s_preH, (int)desc.MultiSampleType);
    return true;
}

static void FxCapturePreTranslucent(IDirect3DDevice9* dev)
{
    // Only the scene that IS the back buffer. Water reflection, refraction and rendertarget
    // cameras all reach this same boundary with their own colour buffer, and a copy of one
    // of those would mark the whole real frame as changed.
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) { rt->Release(); return; }

    const bool onBB = (rt == bb);
    D3DSURFACE_DESC desc{};
    const bool described = SUCCEEDED(bb->GetDesc(&desc));
    bb->Release();

    if (onBB && described && FxEnsurePre(dev, desc))
    {
        // StretchRect touches no device state at all, so this needs no save/restore and
        // cannot disturb the engine's resolve it is standing in the middle of.
        HRESULT hr = dev->StretchRect(rt, nullptr, s_preResolve ? s_preResolve : s_preSurf,
                                      nullptr, D3DTEXF_NONE);
        if (SUCCEEDED(hr) && s_preResolve)
            hr = dev->StretchRect(s_preResolve, nullptr, s_preSurf, nullptr, D3DTEXF_NONE);

        if (SUCCEEDED(hr)) {
            s_preFrame = s_frame;
            static bool logged = false;
            if (!logged) {
                logged = true;
                DebugLog("^FX: pre-translucent copy taken off the engine's own depth resolve -- "
                         "particles are now told apart from the geometry behind them");
            }
        } else {
            static bool logged = false;
            if (!logged) { logged = true; DebugLog("screenfx: pre-translucent blit failed 0x%08lx", (unsigned long)hr); }
        }
    }
    rt->Release();
}

// Slot 57, with Reset=16 / Present=17 / EndScene=42 / Clear=43 as the anchors.
using SetRenderState_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
static SetRenderState_t o_SetRenderState = nullptr;

static bool s_inPreCapture = false;

static HRESULT __stdcall h_SetRenderState(IDirect3DDevice9* dev, D3DRENDERSTATETYPE state, DWORD value)
{
    // THE WHOLE HOT PATH IS THIS ONE COMPARE. The material system only calls SetRenderState
    // on a value that actually changed, so this runs a few hundred times a frame, and every
    // one of them that is not the trigger costs a compare and a tail call.
    const bool beacon = (state == D3DRS_POINTSIZE) && (value == k_reszCode) &&
                        !s_inOurResolve && !s_inPreCapture && !g_eject &&
                        s_preFrame != s_frame &&
                        g_fxParticleGuard > 0.001f && g_dofEnable;

    const HRESULT hr = o_SetRenderState(dev, state, value);

    // AFTER, so the driver has already done the engine's resolve and our blit cannot land
    // between the bind and the trigger.
    if (beacon) {
        MiscHookGuard _g;
        s_inPreCapture = true;
        FxCapturePreTranslucent(dev);
        s_inPreCapture = false;
    }
    return hr;
}

void ScreenFx_InstallRenderStateHook(void* setRenderStateSlot)
{
    if (o_SetRenderState || !setRenderStateSlot) return;
    if (MH_CreateHook(setRenderStateSlot, (LPVOID)&h_SetRenderState, (LPVOID*)&o_SetRenderState) == MH_OK)
        DebugLog("screenfx: SetRenderState hook installed (slot 57) -- the pre-translucent frame "
                 "copy rides on the engine's own depth resolve");
    else
        DebugLog("screenfx: SetRenderState MH_CreateHook FAILED -- particles stay inside the blur");
}

// ---- z swap hooks: IDirect3DDevice9 slots 39 / 40 (d3d9.h, EndScene 42 / SetRenderState 57
// as the anchors; botox hooks.cpp:248 hooks 39 too). One compare each when the swap is off.
static HRESULT __stdcall h_SetDepthStencilSurface(IDirect3DDevice9* dev, IDirect3DSurface9* surf)
{
    if (s_live && surf && surf == s_autoDepth) surf = s_ownSurf;
    return o_SetDepthStencil(dev, surf);
}

static HRESULT __stdcall h_GetDepthStencilSurface(IDirect3DDevice9* dev, IDirect3DSurface9** out)
{
    const HRESULT hr = o_GetDepthStencil(dev, out);
    // the engine (and every save/restore of ours) only ever sees its own z buffer
    if (SUCCEEDED(hr) && out && *out && s_ownSurf && *out == s_ownSurf && s_autoDepth) {
        (*out)->Release();
        s_autoDepth->AddRef();
        *out = s_autoDepth;
    }
    return hr;
}

void ScreenFx_InstallDepthStencilHooks(void* setSlot, void* getSlot)
{
    if (o_SetDepthStencil || !setSlot || !getSlot) return;
    if (MH_CreateHook(setSlot, (LPVOID)&h_SetDepthStencilSurface, (LPVOID*)&o_SetDepthStencil) != MH_OK) {
        o_SetDepthStencil = nullptr;
        DebugLog("screenfx: SetDepthStencilSurface MH_CreateHook FAILED -- no universal depth");
        return;
    }
    if (MH_CreateHook(getSlot, (LPVOID)&h_GetDepthStencilSurface, (LPVOID*)&o_GetDepthStencil) != MH_OK) {
        // without the reverse map the engine could cache our INTZ: no z swap at all
        MH_RemoveHook(setSlot);
        o_SetDepthStencil = nullptr;
        o_GetDepthStencil = nullptr;
        DebugLog("screenfx: GetDepthStencilSurface MH_CreateHook FAILED -- no universal depth");
        return;
    }
    DebugLog("screenfx: depth stencil hooks installed (slots 39/40) -- universal depth available");
}

// ==== the probe =====================================================================
// A 64x36 grid of depth taps, rendered with the probe shader, pulled back to system
// memory and read. This is the one honest test that the resolve worked: an INTZ texture
// cannot be StretchRect'd or locked, so a shader is the only way to see inside it.
static bool FxEnsureProbe(IDirect3DDevice9* dev)
{
    if (s_probePs && s_probeTex && s_probeSurf && s_probeSys) return true;

    if (!s_probePs) {
        std::vector<DWORD> code((sizeof(k_fxProbeBytes) + 3) / 4);
        memcpy(code.data(), k_fxProbeBytes, sizeof(k_fxProbeBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_probePs))) {
            DebugLog("screenfx: probe shader creation failed");
            return false;
        }
    }

    // A32B32G32R32F first: the readback is then the resolved depth exactly, with no
    // 8-bit quantisation to argue with. A8R8G8B8 is the fallback and still tells sane
    // from insane, one part in 255.
    if (!s_probeTex) {
        s_probeFmt = D3DFMT_A32B32G32R32F;
        HRESULT hr = dev->CreateTexture(k_probeW, k_probeH, 1, D3DUSAGE_RENDERTARGET,
                                        s_probeFmt, D3DPOOL_DEFAULT, &s_probeTex, nullptr);
        if (FAILED(hr)) {
            s_probeFmt = D3DFMT_A8R8G8B8;
            hr = dev->CreateTexture(k_probeW, k_probeH, 1, D3DUSAGE_RENDERTARGET,
                                    s_probeFmt, D3DPOOL_DEFAULT, &s_probeTex, nullptr);
        }
        if (FAILED(hr)) { DebugLog("screenfx: probe RT creation failed"); return false; }
    }
    if (!s_probeSurf && FAILED(s_probeTex->GetSurfaceLevel(0, &s_probeSurf))) return false;

    // GetRenderTargetData needs the destination in SYSTEMMEM at the same size and format.
    if (!s_probeSys &&
        FAILED(dev->CreateOffscreenPlainSurface(k_probeW, k_probeH, s_probeFmt,
                                                D3DPOOL_SYSTEMMEM, &s_probeSys, nullptr)))
    { DebugLog("screenfx: probe sysmem surface failed"); return false; }

    return s_probePs && s_probeTex && s_probeSurf && s_probeSys;
}

struct FxProbeStats { float lo, hi, centre, sum; int zeros, ones, n; };

// Render the probe grid and read it back. False = no probe target or the readback failed.
static bool FxProbeRead(IDirect3DDevice9* dev, FxProbeStats& r)
{
    if (!ScreenFx_DepthTexture() || !FxEnsureProbe(dev)) return false;

    struct Vert { float x, y, z, rhw, u, v; };
    const float W = (float)k_probeW, H = (float)k_probeH;
    const Vert quad[4] = {
        { -0.5f,    -0.5f,     0.f, 1.f, 0.f, 0.f },
        { W - 0.5f, -0.5f,     0.f, 1.f, 1.f, 0.f },
        { -0.5f,    H - 0.5f,  0.f, 1.f, 0.f, 1.f },
        { W - 0.5f, H - 0.5f,  0.f, 1.f, 1.f, 1.f },
    };

    FxSavedState st{};
    FxSave(dev, st);

    // The depth surface goes first, and it is not optional: it is the frame's, so with msaa
    // on it is multisampled, and SetRenderTarget refuses a plain 64x36 target underneath one.
    IDirect3DSurface9* oldDepth = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&oldDepth))) oldDepth = nullptr;
    dev->SetDepthStencilSurface(nullptr);

    dev->SetRenderTarget(0, s_probeSurf);
    const D3DVIEWPORT9 vp{ 0, 0, k_probeW, k_probeH, 0.f, 1.f };
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
    dev->SetVertexShader(nullptr);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetPixelShader(s_probePs);
    dev->SetTexture(0, ScreenFx_DepthTexture());
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    // POINT everywhere: a depth texture must never be filtered, and INTZ on some drivers
    // silently returns 0 through a linear sampler.
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vert));

    FxRestore(dev, st);
    dev->SetDepthStencilSurface(oldDepth);
    if (oldDepth) oldDepth->Release();

    // The readback. A full pipeline stall, which is exactly why this runs once every few
    // seconds and only while the probe is ticked.
    if (FAILED(dev->GetRenderTargetData(s_probeSurf, s_probeSys))) return false;

    D3DLOCKED_RECT lr{};
    if (FAILED(s_probeSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;

    float lo = 1e9f, hi = -1e9f, centre = 0.f, sum = 0.f;
    int   zeros = 0, ones = 0, n = 0;
    for (int y = 0; y < k_probeH; ++y)
    {
        const uint8_t* row = (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch;
        for (int x = 0; x < k_probeW; ++x)
        {
            float d;
            if (s_probeFmt == D3DFMT_A32B32G32R32F)
                d = ((const float*)row)[x * 4];                 // rgba32f, r first
            else
                d = ((const uint8_t*)row)[x * 4 + 2] / 255.f;   // a8r8g8b8 is b,g,r,a

            if (d < lo) lo = d;
            if (d > hi) hi = d;
            sum += d;
            if (d <= 0.0001f) ++zeros;
            if (d >= 0.9999f) ++ones;
            if (y == k_probeH / 2 && x == k_probeW / 2) centre = d;
            ++n;
        }
    }
    s_probeSys->UnlockRect();
    r = { lo, hi, centre, sum, zeros, ones, n };
    return true;
}

static void FxProbe(IDirect3DDevice9* dev, IDirect3DSurface9* sceneDepth, int scene, bool fromPostSlot)
{
    FxProbeStats r{};
    if (!FxProbeRead(dev, r)) { DebugLog("screenfx: probe readback failed"); return; }
    const float lo = r.lo, hi = r.hi, centre = r.centre, sum = r.sum;
    const int   zeros = r.zeros, ones = r.ones, n = r.n;

    // Unprojected with the game's own z range, so the number is in source units and can be
    // checked against what is actually in front of the player. No view setup yet (the hook
    // never fired) leaves the raw value to speak for itself.
    FxViewSetup view{};
    char linear[96];
    linear[0] = '\0';
    if (ScreenFx_View(view))
    {
        auto lin = [&](float d) {
            const float den = view.zFar - d * (view.zFar - view.zNear);
            return (den > 0.0001f) ? (view.zNear * view.zFar) / den : -1.f;
        };
        snprintf(linear, sizeof(linear), " lin_centre=%.1fu lin_min=%.1fu", lin(centre), lin(lo));
    }

    // WHAT WAS ACTUALLY RESOLVED. A dead read (min=max=1.0) has two different causes and
    // this is what tells them apart: the depth bound at the draw point is the scene's and
    // was discarded, or it is a different surface entirely (wrong size / wrong msaa / a
    // rendertarget's own). Same line carries the slot, because under multicore the passes
    // land in EndScene and the answer is expected to change.
    char src[144];
    src[0] = '\0';
    if (sceneDepth) {
        D3DSURFACE_DESC sd{};
        if (SUCCEEDED(sceneDepth->GetDesc(&sd)))
            snprintf(src, sizeof(src), " ds=%p %ux%u fmt=%u msaa=%u own=%dx%d",
                     (void*)sceneDepth, (unsigned)sd.Width, (unsigned)sd.Height,
                     (unsigned)sd.Format, (unsigned)sd.MultiSampleType, s_texW, s_texH);
    }

    DebugLog("^FX: probe scene=%d fmt=%s min=%.6f max=%.6f avg=%.6f centre=%.6f zeros=%d/%d "
             "ones=%d%s%s slot=%s",
             scene, (s_probeFmt == D3DFMT_A32B32G32R32F) ? "f32" : "u8",
             lo, hi, sum / (n ? n : 1), centre, zeros, n, ones, linear, src,
             fromPostSlot ? "post" : "endscene");
}

// One health step per resolved frame at a draw point. True = the passes may draw.
static bool FxHealthStep(IDirect3DDevice9* dev, bool fromPostSlot)
{
    if (s_healthStale) {
        s_healthStale = false;
        s_health[0] = s_health[1] = FxHealthState{};
    }
    FxHealthState& h = s_health[fromPostSlot ? 1 : 0];
    if (--h.wait > 0) return h.health == FxHealth::Live;

    FxProbeStats r{};
    if (!FxProbeRead(dev, r)) { h.wait = k_healthDead; return h.health == FxHealth::Live; }

    // real depth = enough taps that are neither the far plane nor zero
    const bool live = (r.n - r.ones - r.zeros) >= 8;
    const FxHealth before = h.health;
    if (live)                                  { h.health = FxHealth::Live; h.deadStreak = 0; h.wait = k_healthLive; s_rebuildTries = 0; }
    else if (++h.deadStreak >= k_healthDeadN)  { h.health = FxHealth::Dead; h.wait = k_healthDead; }
    else                                       { h.wait = k_healthUnknown; }

    // ONLY ON LIVE -> DEAD. Depth that has been proven and then reads empty is a route that
    // broke; depth that has never read live yet is usually a view with nothing but sky in it,
    // and rebuilding the resources every two seconds for as long as someone looks up would
    // cost a hitch each time and fix nothing.
    if (before == FxHealth::Live && h.health == FxHealth::Dead)
        FxWantRebuild("the probe reads the depth as empty after it had been live");

    if (h.health != before)
        DebugLog("^FX: depth at %s is %s -- min=%.4f max=%.4f centre=%.4f far=%d zero=%d of %d",
                 fromPostSlot ? "the post slot" : "EndScene",
                 h.health == FxHealth::Live ? "LIVE" : (h.health == FxHealth::Dead ? "DEAD, passes held off" : "unknown"),
                 r.lo, r.hi, r.centre, r.ones, r.zeros, r.n);
    return h.health == FxHealth::Live;
}

// ==== the frame =====================================================================
void ScreenFx_Frame(IDirect3DDevice9* dev, bool fromPostSlot)
{
    const bool wantAo    = g_aoEnable;
    const bool wantDof   = g_dofEnable;
    const bool wantFb    = g_fbEnable;
    const bool wantProbe = g_fxDepthProbe;
    const bool wantRefl  = g_reflEnable;                          // unit D3
    const bool wantDv    = g_emphasizeEnable || g_showDepthEnable; // botox depth_view
    const bool wantWarp  = ScreenFxPasses_WarpWanted();            // depth distort + depth channel shift
    const bool wantPass  = FxWantDepthPass();

    if (!dev || (!wantPass && !wantProbe)) return;
    if (!IsInGame()) { s_healthStale = true; return; }

    // The EndScene fallback runs after flip world's mirror (dih's own, or fpsoverlay's when that DLL
    // owns flip world): the colour it would blur is mirrored and
    // the depth it reads is not, so AO and the focus mask would land on the mirror image of their
    // true places. The post slot and its queued replay run before the mirror and are unaffected.
    if (!fromPostSlot && FlipWorld_FrameMirroredAnywhere()) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            DebugLog("^FX: EndScene fallback skipped while flip world mirrors -- its depth would not match the "
                     "mirrored colour. Tick 'queue passes onto the render thread' or use mat_queue_mode 0.");
        }
        return;
    }

    // BEFORE ANYTHING TOUCHES THE TEXTURE. Nothing of ours is in flight at this point, which is
    // the same promise h_Reset makes when it calls the identical pair.
    if (s_rebuildWanted && (s_frame - s_rebuildFrame) > k_rebuildCooldown) {
        s_rebuildFrame = s_frame;
        ++s_rebuildTries;
        DebugLog("^FX: rebuilding the depth source (attempt %d)", s_rebuildTries);
        ScreenFx_Destroy();   // clears s_rebuildWanted; FxEnsure below remakes and re-registers
    }

    if (!s_endSceneTid) {
        s_endSceneTid = GetCurrentThreadId();
        DebugLog("^FX: passes run on thread %lu, game post-pass thread %lu, present thread %lu (%s)",
                 s_endSceneTid, s_setupTid, Render_PresentTid(),
                 (s_setupTid && s_setupTid == s_endSceneTid)
                     ? "in the game's own post slot -- hud and viewmodel stay sharp"
                     : "EndScene fallback -- the hud is already in the frame");
    }

    // THE COUNTDOWN TICKS ONCE A FRAME, not once a call: this runs from the game's own post
    // slot and from EndScene both, and a per-call decrement halved the interval.
    if (wantProbe) {
        if (s_probeFrame != s_frame) {
            s_probeFrame = s_frame;
            if (--s_probeCountdown <= 0) { s_probeCountdown = k_probeInterval; s_probePending = true; }
        }
    } else {
        s_probePending = false;
    }

    // NOTHING READS THE DEPTH THIS FRAME, SO NOTHING RESOLVES IT. With the probe alone
    // ticked that is 299 frames in 300 costing one branch instead of a full-res depth
    // resolve, the state block around it and four COM round trips.
    if (!wantPass && !s_probePending) return;

    // Only the scene whose target IS the backbuffer. Reflection, refraction and
    // rendertarget passes go through the same device, and their depth is not the frame's.
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = (rt == bb);

    D3DSURFACE_DESC desc{};
    const bool described = bb && SUCCEEDED(bb->GetDesc(&desc));

    if (bb) bb->Release();
    // rt is held to the end of this function -- it is the surface the passes draw into.
    if (!onBB || !described) {
        if (rt) rt->Release();
        return;
    }

    // WHICH backbuffer scene this is. The engine opens more than one a frame, and the
    // fallback slot latching on the first is the open question: an untouched depth buffer
    // reads exactly 1.0 on every tap, which is what the log shows under multicore.
    const int  scene = ++s_bbScene;
    const bool first = (s_frame != s_resolvedFrm);   // one set of passes per frame, still

    // The latch is LIFTED on a probe frame so every backbuffer scene gets its own resolve,
    // readback and index. One frame in 300, and only while the probe is ticked.
    if (!first && !s_probePending) {
        if (rt) rt->Release();
        return;
    }

    // The RESZ hack resolves the CURRENTLY BOUND depth surface, so there has to be one --
    // and a frame where the engine left none bound is a frame with no depth to read.
    IDirect3DSurface9* depthSurf = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&depthSurf)) || !depthSurf) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("^FX: no depth surface bound at the draw point"); }
        if (rt) rt->Release();
        return;
    }

    if (FxDepthFits(depthSurf, desc) && FxEnsure(dev, desc) && ScreenFx_DepthTexture())   // z swap: nothing until it is live
    {
        s_resolvedFrm = s_frame;
        FxNoFetch4 noFetch4(dev);   // probe + every pass below samples the INTZ

        // z swap: nothing to resolve, the INTZ is the z buffer itself
        if (s_route != FX_ROUTE_LIVE) {
            FxSavedState st{};
            FxSave(dev, st);
            if (s_route == FX_ROUTE_RESZ) FxResolveResz(dev);
            else                          FxResolveNvapi(dev, depthSurf);
            FxRestore(dev, st);
        }

        if (s_probePending) FxProbe(dev, depthSurf, scene, fromPostSlot);

        // The passes. Every one of them needs the game's own z range and fov, and nothing
        // on this thread can answer for them -- so no view setup means no pass, not a pass
        // built on guesses.
        FxViewSetup view{};
        if (first && wantPass && rt && FxHealthStep(dev, fromPostSlot))
        {
            if (ScreenFx_View(view)) {
                // botox's own DoPostScreenSpaceEffects order: depth view, reflections, ao, dof,
                // motion blur. Reflections ahead of ao/dof so they never shade a mirror image
                // that itself still needs occlusion and a focus blur.
                if (wantDv)   ScreenFxPasses_DepthView(dev, rt, desc, ScreenFx_DepthTexture(), view);
                if (wantRefl) Reflections_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view);
                // OCCLUSION FIRST. It belongs on sharp geometry: the other way round the
                // blur would be finished before the creases were drawn, and every shadow
                // would come out pin sharp on top of an out of focus wall.
                if (wantAo)  Ssao_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view);
                // Then the lens, the way a camera has them: the lens decides what is sharp.
                if (wantDof) Dof_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view);
                // AND THE SHUTTER LAST, which is the order a camera has them in: the lens
                // decides what is sharp, the shutter decides how far it smeared while open.
                // The gun is not in this frame yet -- ScreenFx_ViewmodelFrame owns that half.
                if (wantFb)  FrameBlend_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view, false);
                // distort last: ao/dof/blur above were computed on the undistorted depth
                if (wantWarp) ScreenFxPasses_Warp(dev, rt, desc, ScreenFx_DepthTexture(), view);
            } else {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    DebugLog("^FX: no view setup -- the DoPostScreenSpaceEffects hook never "
                             "delivered one, so ao/dof stay off");
                }
            }
        }
    }

    depthSurf->Release();
    if (rt) rt->Release();
}

// ==== the gun's own pass ============================================================
// THE VIEWMODEL IS NOT IN THE FRAME WHERE THE FIRST PASS RUNS. viewrender.cpp:2157 hands
// the post slot a scene with the world and every particle in it and the gun still to come:
// DrawViewModels is the very next line, 2160. So "viewmodel stays sharp" was never a choice
// the first pass could make -- the gun is drawn after it either way, and comes out sharp.
//
// Turning the switch OFF now buys a SECOND pass, run from ViewDrawFade (2185), which is
// after DrawViewModels and before the hud. The depth is resolved again there, so it carries
// the gun in the 0..0.1 slice DrawViewModels forced with DepthRange (viewrender.cpp:1109),
// and Dof_Draw's viewmodel mode gives a circle of confusion to exactly those pixels and to
// nothing else. The rest of the frame is already finished and passes straight through.
void ScreenFx_ViewmodelFrame(IDirect3DDevice9* dev)
{
    // Two passes want this window, for the same reason: it is the only point in the frame where
    // the gun is painted and the hud is not. The lens blurs it by its distance; frame blending
    // smears it by what its bones did.
    const bool wantDofVm = g_dofEnable && !g_dofViewmodelSharp;
    const bool wantFbVm  = g_fbEnable && g_fbViewmodel;

    if (!dev || g_eject || (!wantDofVm && !wantFbVm)) return;
    if (s_vmFrame == s_frame) return;      // one gun pass a frame, whichever call site lands
    if (!IsInGame()) return;
    if (s_health[1].health != FxHealth::Live) return;   // no proven depth, no blur

    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = (rt == bb);

    D3DSURFACE_DESC desc{};
    const bool described = bb && SUCCEEDED(bb->GetDesc(&desc));
    if (bb) bb->Release();
    if (!onBB || !described) { if (rt) rt->Release(); return; }

    IDirect3DSurface9* depthSurf = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&depthSurf)) || !depthSurf) { if (rt) rt->Release(); return; }

    if (FxDepthFits(depthSurf, desc) && FxEnsure(dev, desc) && ScreenFx_DepthTexture())   // z swap: nothing until it is live
    {
        s_vmFrame = s_frame;
        FxNoFetch4 noFetch4(dev);

        // ALWAYS RESOLVED HERE, and this is the point of the whole pass: the copy the first
        // pass worked off was taken before DrawViewModels, so the gun is not in it. This one
        // is taken after, and it is the only depth that knows where the gun is.
        if (s_route != FX_ROUTE_LIVE) {
            FxSavedState st{};
            FxSave(dev, st);
            if (s_route == FX_ROUTE_RESZ) FxResolveResz(dev);
            else                          FxResolveNvapi(dev, depthSurf);
            FxRestore(dev, st);
        }

        FxViewSetup view{};
        if (ScreenFx_View(view)) {
            if (wantDofVm) Dof_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view, true);
            if (wantFbVm)  FrameBlend_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view, true);
        }
    }

    depthSurf->Release();
    if (rt) rt->Release();
}

// IVRenderView::ViewDrawFade -- slot 24, counted off public/ivrenderview.h from
// DrawBrushModel at 0 (the interface has no virtual destructor). SetBlend 4 and GetBlend 5
// out of that same count are the ones visuals.cpp already calls every frame, so the count
// is not a guess. viewrender.cpp:2185 calls it once per RenderView, unconditionally,
// straight after DrawViewModels and DrawUnderwaterOverlay and before the engine's post
// processing -- the one window where the gun is painted and the hud is not.
inline constexpr int k_slotViewDrawFade = 24;

using ViewDrawFade_t = void(__fastcall*)(void*, unsigned char*, void*);
static ViewDrawFade_t o_ViewDrawFade = nullptr;

static void FxViewmodelPassDispatch()
{
    // The same fork the post slot takes, for the same reason: raw d3d issued off the main
    // thread while the material system is recording would execute inside the PREVIOUS
    // frame's playback. Queued, it replays on the render thread at this exact position.
    const DWORD present = Render_PresentTid();
    if (!present) return;

    if (GetCurrentThreadId() == present) {
        ScreenFx_ViewmodelFrame(g_d3dDev);
    } else if (g_fxUseCallQueue) {
        const bool queued = FxQueueOnRenderThread(&s_fxFunctorVm);
        static bool logged = false;
        if (!logged) {
            logged = true;
            DebugLog("^FX: viewmodel pass %s from ViewDrawFade (thread %lu, render thread %lu)",
                     queued ? "QUEUED onto the render thread" : "REFUSED by the call queue -- the gun stays sharp",
                     GetCurrentThreadId(), present);
        }
    }
}

static void __fastcall h_ViewDrawFade(void* thisptr, unsigned char* color, void* material)
{
    MiscHookGuard _g;

    o_ViewDrawFade(thisptr, color, material);

    // Flip world (either DLL) mirrors before DrawViewModels, so the gun's colour and its 0..0.1 depth slice
    // are both unmirrored here: the gun's blur mask lines up either way.
    if (!g_eject && ((g_dofEnable && !g_dofViewmodelSharp) || (g_fbEnable && g_fbViewmodel)))
        FxViewmodelPassDispatch();
}

// ==== mat_queue_mode ================================================================
// WHY THIS EXISTS, and it is measured, not assumed (dih, 2026-09-06).
//
// With multicore rendering on, the game's own screen-space slot runs on the MAIN thread
// while the material system replays d3d on the render thread, so the passes fall back to
// h_EndScene -- and the depth surface bound THERE holds nothing: the probe read
// min=max=1.000000 on all 2304 taps, i.e. every pixel at the far plane. That one fact is
// both symptoms at once: depth of field blurred the whole frame (everything maximally out
// of focus) and ambient occlusion drew nothing (its sky test threw every pixel out).
// mat_queue_mode 0 fixed it outright, which puts the game's post slot back on the render
// thread -- where the scene's depth is still bound and the hud is not drawn yet.
//
// THROUGH THE CONSOLE, NOT BY WRITING THE VALUE. mat_queue_mode's effect is applied by its
// change callback; a straight write to the ConVar's float lands the number in memory and
// changes nothing. ClientCmd_Unrestricted (slot 106) is the same road the skinchanger's
// cl_fullupdate takes, and it is the unrestricted one because a plain ClientCmd only runs
// commands flagged FCVAR_CLIENTCMD_CAN_EXECUTE, which this is not.
static bool s_queueForced   = false;
static int  s_queueOriginal = -1;    // what the user had; -1 is the game's own default (auto)

static void FxEngineExec(const char* cmd)
{
    if (!g_engine) return;
    __try {
        void** vt = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vt) < 0x10000) return;
        if (reinterpret_cast<uintptr_t>(vt[106]) < 0x10000) return;
        using Fn = void(__fastcall*)(void*, const char*);
        ((Fn)vt[106])(g_engine, cmd);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== motion blur: the engine's own =================================================
// botox "motion blur" re-implements Source's DoImageSpaceMotionBlur because csgo cut it. TF2
// still runs it (viewrender.cpp:2146 -> viewpostprocess.cpp:2702) off these cvars, read with
// GetInt/GetFloat every frame, so a raw write is enough -- no change callback involved. Roll
// intensity has no cvar in TF2 (commented out at viewpostprocess.cpp:2698, hardcoded 0.3; the
// string is not in tf/bin/x64/client.dll). Saved on the first apply, handed back on off / eject.
struct FxMbCvar { const char* name; CachedCVar cc; float saved; bool have; };
static FxMbCvar s_mbCvars[] = {
    { "mat_motion_blur_enabled" },           { "mat_motion_blur_forward_enabled" },
    { "mat_motion_blur_strength" },          { "mat_motion_blur_rotation_intensity" },
    { "mat_motion_blur_falling_intensity" }, { "mat_motion_blur_falling_min" },
    { "mat_motion_blur_falling_max" },
};
static bool s_mbApplied = false;

static void FxMotionBlurRestore(const char* why)
{
    if (!s_mbApplied) return;
    s_mbApplied = false;
    for (FxMbCvar& c : s_mbCvars) {
        if (!c.have) continue;
        c.have = false;
        if (ConVar* cv = c.cc.Get(c.name)) cv->ForceFloat(c.saved);
    }
    DebugLog("screenfx: motion blur cvars put back (%s)", why);
}

static void FxMotionBlurTick()
{
    if (!g_mbEnable || g_eject) { FxMotionBlurRestore("off"); return; }

    // the engine divides by (max - min): equal ends are a division by zero
    const float fmin = g_mbFallMin;
    const float fmax = (g_mbFallMax > fmin + 0.01f) ? g_mbFallMax : fmin + 0.01f;
    const float want[] = { 1.f, g_mbForward ? 1.f : 0.f, g_mbStrength, g_mbRotation, g_mbFalling, fmin, fmax };

    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
        FxMbCvar& c = s_mbCvars[i];
        ConVar* cv = c.cc.Get(c.name);
        if (!cv) continue;
        if (!c.have) { c.have = true; c.saved = cv->GetFloat(); }
        // re-asserted: a config exec or the video menu puts mat_motion_blur_enabled back
        if (cv->GetFloat() != want[i]) cv->ForceFloat(want[i]);
    }
    if (!s_mbApplied) {
        s_mbApplied = true;
        DebugLog("screenfx: motion blur on -- mat_motion_blur_* strength %.2f rotation %.2f falling %.2f (%.1f..%.1f) forward %d",
                 g_mbStrength, g_mbRotation, g_mbFalling, fmin, fmax, (int)g_mbForward);
    }
}

void ScreenFx_Tick()
{
    FxMotionBlurTick();

    static CachedCVar cc_queue;
    ConVar* cv = cc_queue.Get("mat_queue_mode");
    if (!cv) return;

    // THE PROBE IS NOT IN THIS LIST, and that is the point. Forcing 0 turns multicore
    // rendering off, which is the single most expensive thing this DLL can do to a frame --
    // measured at ~100fps on this machine. A diagnostic that changes the thing it measures
    // is worth nothing: with the probe alone ticked the queue mode stays where the user had
    // it and the log says what the depth really looks like there.
    //
    // AND NOT ONCE THE CALL QUEUE HAS RUN. A queued functor executing is the proof that the
    // passes already reach the render thread at the right point, which is the only thing
    // mat_queue_mode 0 was ever bought for -- so it is handed straight back.
    const bool want = g_fxForceQueueMode && FxWantDepthPass() &&
                      (s_cqRefused || !g_fxUseCallQueue);
    const int  now  = (int)cv->GetFloat();

    if (want) {
        if (!s_queueForced) {
            s_queueForced   = true;
            s_queueOriginal = now;
            DebugLog("screenfx: forcing mat_queue_mode 0 (was %d) -- the depth passes need the "
                     "game's post slot on the render thread", s_queueOriginal);
        }
        // Re-asserted, because a config exec or the video options menu will put it back and
        // the depth would go quietly dead again. Only the first force is logged.
        if (now != 0) FxEngineExec("mat_queue_mode 0");
    }
    else if (s_queueForced) {
        s_queueForced = false;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "mat_queue_mode %d", s_queueOriginal);
        FxEngineExec(cmd);
        DebugLog("screenfx: mat_queue_mode put back to %d", s_queueOriginal);
    }
}

void ScreenFx_RestoreQueueMode()
{
    FxMotionBlurRestore("eject");
    if (!s_queueForced) return;
    s_queueForced = false;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "mat_queue_mode %d", s_queueOriginal);
    FxEngineExec(cmd);
    DebugLog("screenfx: eject -- mat_queue_mode put back to %d", s_queueOriginal);
}

bool ScreenFx_QueueForced() { return s_queueForced; }   // unit game: misc performance "multicore rendering" yields to it

// ==== IClientMode::DoPostScreenSpaceEffects =========================================
// The game's own screen-space slot: the 3D scene and the engine's post are done, the HUD
// is not drawn yet (viewrender.cpp:2157). What it is worth here is the CViewSetup it is
// handed -- fov and the z range, which nothing else on the render thread can answer -- and
// the thread it runs on, which decides whether raw d3d may be issued from it at all.
//
// SLOT 39, counted off the SDK's own game/client/iclientmode.h (dtor 0, InitViewport 1,
// ... SetMapName 38, DoPostScreenSpaceEffects 39), NOT off aim/defs.h. The numbers below
// are logged on the first call: a wrong slot shows up as garbage fov / z range, not as a
// crash, because everything read here is SEH-guarded and sanity-checked.
inline constexpr int k_slotDoPostSSE = 39;

// CViewSetup field offsets, public/view_shared.h. Every member up to zFar is a 4-byte int,
// float or bool, so the layout is the same on x64 as it is in the 32-bit SDK.
inline constexpr size_t k_vsWidth  = 16;
inline constexpr size_t k_vsHeight = 24;
inline constexpr size_t k_vsFov    = 56;
inline constexpr size_t k_vsFovVm  = 60;   // fovViewmodel -- the gun is drawn through its own
inline constexpr size_t k_vsOrigin = 64;   // Vector, 3 floats
inline constexpr size_t k_vsAngles = 76;   // QAngle, 3 floats
inline constexpr size_t k_vsZNear  = 88;
inline constexpr size_t k_vsZFar   = 92;
inline constexpr size_t k_vsZNearVm = 96;    // zNearViewmodel -- the gun's own projection
inline constexpr size_t k_vsZFarVm  = 100;   // zFarViewmodel

using DoPostSSE_t = bool(__fastcall*)(void*, const void*);
static DoPostSSE_t o_DoPostSSE = nullptr;

static void FxReadViewSetup(const void* setup)
{
    const uintptr_t p = reinterpret_cast<uintptr_t>(setup);
    float fov = 0.f, zn = 0.f, zf = 0.f, znVm = 0.f, zfVm = 0.f, fovVm = 0.f;
    int   w = 0, h = 0;
    Vec3  origin, angles;
    __try {
        fov = *reinterpret_cast<const float*>(p + k_vsFov);
        fovVm = *reinterpret_cast<const float*>(p + k_vsFovVm);
        zn  = *reinterpret_cast<const float*>(p + k_vsZNear);
        zf  = *reinterpret_cast<const float*>(p + k_vsZFar);
        znVm = *reinterpret_cast<const float*>(p + k_vsZNearVm);
        zfVm = *reinterpret_cast<const float*>(p + k_vsZFarVm);
        w   = *reinterpret_cast<const int*>  (p + k_vsWidth);
        h   = *reinterpret_cast<const int*>  (p + k_vsHeight);
        const float* o = reinterpret_cast<const float*>(p + k_vsOrigin);
        const float* a = reinterpret_cast<const float*>(p + k_vsAngles);
        origin = Vec3(o[0], o[1], o[2]);
        angles = Vec3(a[0], a[1], a[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    // A plausible view, or nothing. Out-of-range numbers mean the slot or the offsets are
    // wrong, and a focal distance built on them would be worse than no pass at all.
    const bool sane = fov > 1.f && fov < 179.f &&
                      zn  > 0.f && zn  < 64.f  &&
                      zf  > zn  && zf  < 1.0e6f &&
                      w   > 0   && h   > 0;

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        DebugLog("^FX: view setup fov=%.2f zNear=%.3f zFar=%.1f vm=%.3f..%.1f %dx%d "
                 "origin=%.0f,%.0f,%.0f angles=%.1f,%.1f sane=%d tid=%lu",
                 fov, zn, zf, znVm, zfVm, w, h, origin.x, origin.y, origin.z, angles.x, angles.y,
                 (int)sane, GetCurrentThreadId());
    }
    if (!sane) return;

    // SUB-VIEWS ARE NOT THE FRAME WE DRAW OVER. ViewDrawScene runs this slot for water
    // reflection and refraction and for rendertarget cameras too, each with its own small
    // setup -- and a focus traced from a reflected camera would drag the whole frame's focal
    // distance around. The main view is the biggest one the frame ever asks for.
    if (w > s_maxSetupW) s_maxSetupW = w;
    if (w < s_maxSetupW) return;

    s_viewOriginX = origin.x; s_viewOriginY = origin.y; s_viewOriginZ = origin.z;
    s_viewAngPitch = angles.x; s_viewAngYaw = angles.y; s_viewAngRoll = angles.z;
    s_viewFov  = fov;
    s_viewNear = zn;
    s_viewFar  = zf;
    // Only if they read as a range at all -- ScreenFx_View falls back to the main view's own
    // when they do not, and the gun then blurs by the wrong distance instead of by NaN.
    if (znVm > 0.f && zfVm > znVm && zfVm < 1.0e6f) { s_viewNearVm = znVm; s_viewFarVm = zfVm; }
    s_viewW    = w;
    s_viewH    = h;
    s_viewValid = true;

    // The focus trace belongs on THIS thread: it walks the engine's collision world, and the
    // draw half runs on the render thread a frame later where none of that is safe to touch.
    Dof_UpdateFocus(origin, angles, fov, (h > 0) ? (float)w / (float)h : 1.f);

    // Same story, and the entity list is the same reason: the camera history, the reprojection
    // built off it and the players' boxes all belong to this thread.
    const float fbOrigin[3] = { origin.x, origin.y, origin.z };
    const float fbAngles[3] = { angles.x, angles.y, angles.z };
    FrameBlend_UpdateView(fbOrigin, fbAngles, fov, fovVm, s_viewNearVm, s_viewFarVm);

    // unit D3: the sky/clouds pass's world-space cloud drift -- same thread, same reason
    // (g_globalVars->curtime and the player's own origin are main-thread only).
    MCWorld_UpdateView(fbOrigin);

    // unit screen: reflections' nearest env_cubemap pick + file loads (botox pick_cubemap, main thread)
    Reflections_UpdateView(fbOrigin, fbAngles);
}

static bool __fastcall h_DoPostScreenSpaceEffects(void* thisptr, const void* setup)
{
    MiscHookGuard _g;

    const DWORD tid = GetCurrentThreadId();

    FlipWorld_WorldPass();   // this frame draws the world: the mirror must follow before the next frame

    if (!g_eject && setup) {
        s_setupTid = tid;
        FxReadViewSetup(setup);
        ScreenFxPasses_WarpTick();
    }

    // Player glow (aim/chams.cpp): ref runs CGlow::RenderMain here, before the original --
    // scene complete, viewmodel + HUD still to come. Material-system calls only, so queued mode is fine.
    // (Chams_RenderCloaked removed: no Spy-cloak concept in CS:S.)
    if (!g_eject) { Glow_Render(); }

    const bool ret = o_DoPostSSE(thisptr, setup);

    if (!g_eject) Web_OnPostScreenSpaceEffects(setup);   // botox do_post_screen_space_effects.cpp:49

    /* THE GOOD PLACE TO DRAW, when we are allowed to. Here the 3D scene is finished and the
       HUD and the viewmodel are not drawn yet (viewrender.cpp:2157, DrawViewModels at 2160),
       so a blur leaves both of them sharp -- which the EndScene fallback cannot, since vgui
       has painted by then.

       ONLY ON THE RENDER THREAD. With multicore rendering the material system records this
       thread's calls and replays them later, so raw d3d issued here would execute in the
       middle of the PREVIOUS frame's playback. That case falls through to the EndScene
       fallback instead, and says so once. */
    if (!g_eject && (FxWantDepthPass() || g_fxDepthProbe)) {
        const DWORD present = Render_PresentTid();

        if (present && tid == present) {
            ScreenFx_Frame(g_d3dDev, true);
        } else if (present) {
            // MULTICORE. This slot is on the main thread and raw d3d issued here would land
            // in the middle of the previous frame's playback -- so hand the work to the
            // material system's own call queue instead, which replays it on the render
            // thread at THIS position. Only if that is refused does EndScene stand in.
            bool queued = false;
            if (g_fxUseCallQueue && !s_cqRefused) {
                queued = FxQueueOnRenderThread(&s_fxFunctor);
                // refused outright, or taken 240 times and never run: the slots are wrong here
                if (!queued || (++s_cqQueued > 240 && !s_cqRan)) {
                    s_cqRefused = true;
                    DebugLog("^FX: call queue unusable (queued=%d ran=%d) -- mat_queue_mode 0 takes over",
                             s_cqQueued, (int)s_cqRan);
                }
            }

            static bool logged = false;
            if (!logged) {
                logged = true;
                if (queued)
                    DebugLog("^FX: post slot on thread %lu, render thread %lu -- multicore is on "
                             "and the passes are QUEUED onto the render thread at this exact "
                             "point. mat_queue_mode stays where you had it.", tid, present);
                else
                    DebugLog("^FX: post slot on thread %lu, render thread %lu -- multicore is on "
                             "and the call queue was refused, so the passes fall back to EndScene "
                             "and the hud is inside them. mat_queue_mode 0 puts them before it.",
                             tid, present);
            }
        }
    }

    // Flip world: the world is finished and DrawViewModels is next (viewrender.cpp:2160), so the gun
    // draws unmirrored over it. After the passes above so they still read a z buffer that matches the colour.
    if (!g_eject) FlipWorld_Render(setup);

    return ret;
}

// g_pClientMode, off the same `mov rcx, [rip+x]` the aim tree uses to find it
// (aim/defs.h:3251). The pattern is a call site, so the pointer it loads is the client
// mode singleton itself.
void* ScreenFx_FindClientMode()
{
    static void* s_mode = nullptr;
    if (s_mode) return s_mode;
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return nullptr;

    // CSS x64 GetClientModeNormal() (client+0x1F0600): `lea rbx, g_ClientModeNormal` then the
    // lazy-init ctor call. The TF2 pattern used before hit `mov rcx, g_pHaptics` in CSS -- every
    // "ClientMode" vtable hook landed on IHaptics (OverrideMouseInput/CreateMove on ICF-shared
    // empty bodies). Static object: the lea IS the address, no deref.
    uint8_t* at = PatternScan(hClient, "48 8D 1D ? ? ? ? 48 8B CB 89 05 ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8D 05");
    if (!at) { DebugLog("screenfx: ClientMode pattern NOT FOUND"); return nullptr; }

    void* mode = nullptr;
    void* vt = nullptr;
    __try {
        const int32_t disp = *reinterpret_cast<const int32_t*>(at + 3);
        mode = at + 7 + disp;
        vt = *reinterpret_cast<void**>(mode);   // null = static ctor not run yet
    } __except(EXCEPTION_EXECUTE_HANDLER) { mode = nullptr; }

    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), hClient, &mi, sizeof(mi));
    const uintptr_t lo = reinterpret_cast<uintptr_t>(hClient), hi = lo + mi.SizeOfImage;
    if (!mode || reinterpret_cast<uintptr_t>(vt) < lo || reinterpret_cast<uintptr_t>(vt) >= hi) {
        DebugLog("screenfx: ClientMode not constructed (obj %p vt %p)", mode, vt);
        return nullptr;
    }
    DebugLog("screenfx: ClientMode client.dll+0x%llx vt +0x%llx",
             (unsigned long long)(reinterpret_cast<uintptr_t>(mode) - lo),
             (unsigned long long)(reinterpret_cast<uintptr_t>(vt) - lo));
    s_mode = mode;
    return mode;
}

void ScreenFx_Install()
{
    // The gun's pass. Independent of the client mode hook below: a failure there must not
    // take this with it, and the other way round.
    if (!o_ViewDrawFade && g_renderView)
        InstallVTableHook(g_renderView, k_slotViewDrawFade, "screenfx: ViewDrawFade",
                          (void*)h_ViewDrawFade, (void**)&o_ViewDrawFade, true, nullptr);

    if (o_DoPostSSE) return;

    void* mode = ScreenFx_FindClientMode();
    if (!mode) return;

    // CSS never overrides DoPostScreenSpaceEffects: slot 39 is the ICF-shared `mov al,1; ret`,
    // so the vtable entry is swapped -- a MinHook detour there would fire for every `return true`.
    SwapVTableHook(mode, k_slotDoPostSSE, "screenfx: DoPostScreenSpaceEffects",
                   (void*)h_DoPostScreenSpaceEffects, (void**)&o_DoPostSSE);
}
