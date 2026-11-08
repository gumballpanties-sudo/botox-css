#include "screenfx_passes.h"
#include "shaders/fx_depthview.h"  // k_fxDepthViewBytes -- compiled from fx_depthview.hlsl by fxc
#include "shaders/fx_warp.h"       // k_fxWarpPs / k_fxWarpWrite -- fx_warp_build.sh from fx_warp.hlsl
#include "screenfx.h"      // shared quad -- emphasize/show depth draw through its vs_3_0
#include "../movement/prediction.h"   // g_globalVars (warp clock, main thread)
#include <cmath>           // fmodf (grain phase)

// Sharpen / grain / vignette / invert / posterize / deband / channel shift used to be a
// ps_2_0 pass here. botox runs them inside its colour correction shader, so they moved to
// colorcorrect.cpp (fx_grade.hlsl) with botox's options; ScreenFxPasses_Frame is a no-op kept
// for its render.cpp call sites.
//
// Emphasize and show depth are NOT in that pass: both are depth based (botox depth_view.cpp)
// and draw from screenfx.cpp's post slot through ScreenFxPasses_DepthView below. They used to
// run here, at the vgui point / EndScene, where nothing had resolved the depth for them
// (ScreenFx_Frame only resolved for ao/dof/fb) and the depth there reads far everywhere.

static IDirect3DPixelShader9* s_psDepthView = nullptr;   // fx_depthview.hlsl, ps_3_0
static IDirect3DTexture9*     s_dvFrameTex  = nullptr;   // depth view's own frame copy
static IDirect3DSurface9*     s_dvFrameSurf = nullptr;
static int  s_dvW = 0, s_dvH = 0;
static bool s_dvFailed = false;

static IDirect3DPixelShader9* s_wpPs[11][4]{};
static IDirect3DPixelShader9* s_wpWrite[2]{};
static bool s_wpBad[11][4]{};
static bool s_wpWriteBad[2]{};
static IDirect3DTexture9* s_wpFrameTex = nullptr;
static IDirect3DSurface9* s_wpFrameSurf = nullptr;
static IDirect3DTexture9* s_wpHistTex = nullptr;   // slit scan history, persists across frames
static IDirect3DSurface9* s_wpHistSurf = nullptr;
static int  s_wpW = 0, s_wpH = 0, s_wpHistEffect = -1;
static unsigned s_wpFrame = 0;
static volatile double s_wpClock = 0.0;   // written main thread, read render thread
static int s_wpClockFrame = -1;

static void WarpRelease()
{
    for (auto& row : s_wpPs) for (auto& p : row) if (p) { p->Release(); p = nullptr; }
    for (auto& p : s_wpWrite) if (p) { p->Release(); p = nullptr; }
    if (s_wpHistSurf)  { s_wpHistSurf->Release();  s_wpHistSurf  = nullptr; }
    if (s_wpHistTex)   { s_wpHistTex->Release();   s_wpHistTex   = nullptr; }
    if (s_wpFrameSurf) { s_wpFrameSurf->Release(); s_wpFrameSurf = nullptr; }
    if (s_wpFrameTex)  { s_wpFrameTex->Release();  s_wpFrameTex  = nullptr; }
    s_wpW = s_wpH = 0;
    s_wpHistEffect = -1;
}

void ScreenFxPasses_NewFrame() {}

void ScreenFxPasses_Destroy()
{
    WarpRelease();
    if (s_psDepthView) { s_psDepthView->Release(); s_psDepthView = nullptr; }
    if (s_dvFrameSurf) { s_dvFrameSurf->Release(); s_dvFrameSurf = nullptr; }
    if (s_dvFrameTex)  { s_dvFrameTex->Release();  s_dvFrameTex  = nullptr; }
    s_dvW = s_dvH = 0;
}

static const D3DSAMPLERSTATETYPE k_sfxSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

bool ScreenFxPasses_Frame(IDirect3DDevice9*, bool) { return false; }   // moved into colorcorrect.cpp

// ==== emphasize + show depth (botox depth_view.cpp) ======================================
// Render thread, from ScreenFx_Frame at the game's post slot, right after the depth resolve --
// botox runs it first in its DoPostScreenSpaceEffects, ahead of reflections/ao/dof.
void ScreenFxPasses_DepthView(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                              const FxViewSetup& view)
{
    if (!dev || !target || !depth || s_dvFailed || (!g_emphasizeEnable && !g_showDepthEnable)) return;
    if (!ScreenFx_QuadReady(dev)) return;   // ps_3_0: the shared passthrough vs, not FF

    // must be frame sized, else it is not the scene's depth
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) return;

    if (!s_psDepthView) {
        std::vector<DWORD> code((sizeof(k_fxDepthViewBytes) + 3) / 4);
        memcpy(code.data(), k_fxDepthViewBytes, sizeof(k_fxDepthViewBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_psDepthView)) || !s_psDepthView) {
            s_psDepthView = nullptr;
            s_dvFailed = true;
            DebugLog("screenfx_passes: the card refused the depth view shader -- emphasize/show depth are off");
            return;
        }
        DebugLog("screenfx_passes: depth view shader built");
    }
    if (!s_dvFrameTex || s_dvW != (int)desc.Width || s_dvH != (int)desc.Height) {
        if (s_dvFrameSurf) { s_dvFrameSurf->Release(); s_dvFrameSurf = nullptr; }
        if (s_dvFrameTex)  { s_dvFrameTex->Release();  s_dvFrameTex  = nullptr; }
        HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                        D3DPOOL_DEFAULT, &s_dvFrameTex, nullptr);
        if (FAILED(hr))
            hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                    D3DPOOL_DEFAULT, &s_dvFrameTex, nullptr);
        if (FAILED(hr) || FAILED(s_dvFrameTex->GetSurfaceLevel(0, &s_dvFrameSurf))) {
            static bool logged = false;
            if (!logged) { logged = true; DebugLog("screenfx_passes: depth view frame copy failed 0x%08lx", (unsigned long)hr); }
            if (s_dvFrameTex) { s_dvFrameTex->Release(); s_dvFrameTex = nullptr; }
            return;
        }
        s_dvW = (int)desc.Width; s_dvH = (int)desc.Height;
    }

    if (FAILED(dev->StretchRect(target, nullptr, s_dvFrameSurf, nullptr, D3DTEXF_NONE))) return;

    // ---- save (botox's list). The shaderapi caches all of it, ps constants included.
    static const D3DRENDERSTATETYPE k_rs[] = {
        D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE, D3DRS_ZENABLE,
        D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
        D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    };
    const DWORD k_rsVal[] = {
        FALSE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE, 0, D3DFILL_SOLID,
        FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, FALSE, FALSE,
    };
    constexpr int kRs = (int)(sizeof(k_rs) / sizeof(k_rs[0]));
    static_assert(sizeof(k_rsVal) / sizeof(k_rsVal[0]) == sizeof(k_rs) / sizeof(k_rs[0]), "state list");

    DWORD oldRs[kRs]{}, oldSamp[2][6]{}, oldFvf = 0;
    float oldC[9][4]{};
    IDirect3DSurface9* oldDs = nullptr;
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DBaseTexture9* oldTex[2]{};
    IDirect3DVertexBuffer9* oldStream = nullptr;
    IDirect3DIndexBuffer9* oldIdx = nullptr;
    UINT oldOff = 0, oldStride = 0;
    D3DVIEWPORT9 oldVp{};

    if (FAILED(dev->GetDepthStencilSurface(&oldDs))) oldDs = nullptr;
    for (int i = 0; i < kRs; ++i) dev->GetRenderState(k_rs[i], &oldRs[i]);
    for (int u = 0; u < 2; ++u) {
        dev->GetTexture(u, &oldTex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_sfxSamp[i], &oldSamp[u][i]);
    }
    dev->GetPixelShader(&oldPs);
    dev->GetVertexShader(&oldVs);
    dev->GetVertexDeclaration(&oldDecl);
    dev->GetFVF(&oldFvf);
    dev->GetStreamSource(0, &oldStream, &oldOff, &oldStride);
    dev->GetIndices(&oldIdx);
    dev->GetViewport(&oldVp);
    dev->GetPixelShaderConstantF(0, oldC[0], 9);

    // ---- draw, constants laid out exactly as botox's
    const float W = (float)desc.Width, H = (float)desc.Height;
    const float sx = g_showDepthScaleX > 0.001f ? g_showDepthScaleX : 0.001f;
    const float sy = g_showDepthScaleY > 0.001f ? g_showDepthScaleY : 0.001f;
    const float c[9][4] = {
        { g_emphasizeEnable ? 1.f : 0.f, g_showDepthEnable ? 1.f : 0.f, 1.f / W, 1.f / H },
        { g_emphasizeFocusDepth, g_emphasizeFocusRange, g_emphasizeFocusEdge, g_emphasizeSpherical ? 1.f : 0.f },
        { (float)g_emphasizeSphereFov, g_emphasizeSphereX, g_emphasizeSphereY, g_emphasizeAmount },
        { g_emphasizeBlendColor[0], g_emphasizeBlendColor[1], g_emphasizeBlendColor[2], g_emphasizeBlendFactor },
        { (float)g_showDepthType, g_showDepthBlend ? 1.f : 0.f, g_showDepthLive ? 1.f : 0.f, 0.f },
        { g_showDepthUpsideDown ? 1.f : 0.f, g_showDepthReversed ? 1.f : 0.f, g_showDepthLog ? 1.f : 0.f, g_showDepthFarPlane },
        { sx, sy, (float)g_showDepthOffsetX, (float)g_showDepthOffsetY },
        { g_showDepthMultiplier, W, H, 0.f },
        { view.zNear, view.zFar, 0.f, 0.f },   // c8: lin() unprojects with the real z range
    };

    dev->SetDepthStencilSurface(nullptr);
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], k_rsVal[i]);
    for (int u = 0; u < 2; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    }
    dev->SetPixelShaderConstantF(0, c[0], 9);
    dev->SetTexture(0, depth);
    dev->SetTexture(1, s_dvFrameTex);
    dev->SetRenderTarget(0, target);
    const D3DVIEWPORT9 vp{ 0, 0, desc.Width, desc.Height, 0.f, 1.f };
    dev->SetViewport(&vp);
    dev->SetPixelShader(s_psDepthView);
    ScreenFx_BindQuad(dev);
    ScreenFx_DrawQuad(dev, (int)desc.Width, (int)desc.Height);

    // ---- restore
    dev->SetPixelShaderConstantF(0, oldC[0], 9);
    dev->SetPixelShader(oldPs);
    dev->SetVertexShader(oldVs);
    if (oldDecl) dev->SetVertexDeclaration(oldDecl); else dev->SetFVF(oldFvf);
    dev->SetStreamSource(0, oldStream, oldOff, oldStride);
    dev->SetIndices(oldIdx);
    for (int u = 0; u < 2; ++u) {
        dev->SetTexture(u, oldTex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_sfxSamp[i], oldSamp[u][i]);
    }
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], oldRs[i]);
    dev->SetViewport(&oldVp);
    dev->SetDepthStencilSurface(oldDs);

    for (IUnknown* held : { (IUnknown*)oldDs, (IUnknown*)oldPs, (IUnknown*)oldVs, (IUnknown*)oldDecl,
                            (IUnknown*)oldTex[0], (IUnknown*)oldTex[1], (IUnknown*)oldStream, (IUnknown*)oldIdx })
        if (held) held->Release();

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("screenfx_passes: depth view drew %ux%u (emphasize=%d show depth=%d) at the post slot",
                 (unsigned)desc.Width, (unsigned)desc.Height, (int)g_emphasizeEnable, (int)g_showDepthEnable);
    }
}

// ==== depth distort (reshade Warp-FX on depth) =============================================
bool ScreenFxPasses_WarpWanted() { return g_warpEnable || (g_channelShiftEnable && g_channelShiftDepth); }

// engine frame time, not wall clock: host_framerate recordings animate at their own rate
void ScreenFxPasses_WarpTick()
{
    if (!g_globalVars || g_globalVars->framecount == s_wpClockFrame) return;
    float dt = g_globalVars->absoluteframetime;
    dt = dt < 0.f ? 0.f : (dt > 0.25f ? 0.25f : dt);
    if (g_globalVars->framecount - s_wpClockFrame == 1) s_wpClock = s_wpClock + dt;
    s_wpClockFrame = g_globalVars->framecount;
}

static float PingPong(double t)
{
    const double p = fmod(t, 2.0);
    return (float)(p < 1.0 ? p : 2.0 - p);
}

static int WarpBlendGroup(int m) { return m <= 0 ? 0 : (m <= 10 ? 1 : (m <= 19 ? 2 : 3)); }

static IDirect3DPixelShader9* WarpShader(IDirect3DDevice9* dev, const FxWarpPs& src, IDirect3DPixelShader9*& slot, bool& bad)
{
    if (slot || bad) return slot;
    std::vector<DWORD> code((src.size + 3) / 4);
    memcpy(code.data(), src.bytes, src.size);
    if (FAILED(dev->CreatePixelShader(code.data(), &slot)) || !slot) {
        slot = nullptr;
        bad = true;
        DebugLog("screenfx_passes: the card refused a depth distort shader");
    }
    return slot;
}

static bool WarpMake(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, IDirect3DTexture9*& tex, IDirect3DSurface9*& surf)
{
    if (tex) return true;
    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                    D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr) || FAILED(tex->GetSurfaceLevel(0, &surf))) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("screenfx_passes: depth distort target failed 0x%08lx", (unsigned long)hr); }
        if (tex) { tex->Release(); tex = nullptr; }
        return false;
    }
    return true;
}

static bool WarpTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, bool hist)
{
    if (s_wpW != (int)desc.Width || s_wpH != (int)desc.Height) {
        if (s_wpHistSurf)  { s_wpHistSurf->Release();  s_wpHistSurf  = nullptr; }
        if (s_wpHistTex)   { s_wpHistTex->Release();   s_wpHistTex   = nullptr; }
        if (s_wpFrameSurf) { s_wpFrameSurf->Release(); s_wpFrameSurf = nullptr; }
        if (s_wpFrameTex)  { s_wpFrameTex->Release();  s_wpFrameTex  = nullptr; }
        s_wpHistEffect = -1;
        s_wpW = (int)desc.Width; s_wpH = (int)desc.Height;
    }
    if (!WarpMake(dev, desc, s_wpFrameTex, s_wpFrameSurf)) return false;
    if (hist && !s_wpHistTex) {
        if (!WarpMake(dev, desc, s_wpHistTex, s_wpHistSurf)) return false;
        s_wpHistEffect = -1;
    }
    return true;
}

void ScreenFxPasses_Warp(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                         const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                         const FxViewSetup& view)
{
    if (!dev || !target || !depth || !ScreenFxPasses_WarpWanted()) return;
    if (!ScreenFx_QuadReady(dev)) return;

    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) return;

    const int  effect = g_warpEnable ? std::clamp(g_warpEffect, 0, 9) + 1 : 0;
    const int  blend  = std::clamp(g_warpBlend, 0, 30);
    const bool scan   = effect == 9 || effect == 10;
    const int  group  = effect ? WarpBlendGroup(blend) : 0;

    IDirect3DPixelShader9* ps = WarpShader(dev, k_fxWarpPs[effect][group], s_wpPs[effect][group], s_wpBad[effect][group]);
    IDirect3DPixelShader9* wr = scan ? WarpShader(dev, k_fxWarpWrite[effect - 9], s_wpWrite[effect - 9], s_wpWriteBad[effect - 9]) : nullptr;
    if (!ps || (scan && !wr)) return;
    if (!WarpTargets(dev, desc, scan)) return;
    if (FAILED(dev->StretchRect(target, nullptr, s_wpFrameSurf, nullptr, D3DTEXF_NONE))) return;

    if (scan && s_wpHistEffect != effect) {
        dev->ColorFill(s_wpHistSurf, nullptr, D3DCOLOR_XRGB(0, 0, 0));
        s_wpHistEffect = effect;
    }

    // ---- constants, layout in fx_warp.hlsl
    const float W = (float)desc.Width, H = (float)desc.Height;
    const double clock = s_wpClock;
    ++s_wpFrame;

    float amount = 0.f, angle = 0.f, period = 0.f, option = 0.f;
    switch (effect) {
    case 1: amount = g_warpMagnitude; break;
    case 2: angle = g_warpAngle; option = g_warpInverse ? 1.f : 0.f; break;
    case 3: angle = g_warpAngle; break;
    case 4: case 5: amount = g_warpAmplitude; angle = g_warpAngle; period = g_warpPeriod; break;
    case 6: amount = g_warpWaveAmplitude; angle = g_warpWaveAngle; period = g_warpWavePeriod; option = (float)g_warpWaveType; break;
    case 9: option = (float)std::clamp(g_warpSlitDir, 0, 3); break;
    default: break;
    }

    float slit[4] = { 0.f, 0.f, 0.f, 0.f };
    if (effect == 9) {
        const int dir = (int)option;
        const double pix = std::max(g_warpSlitSpeed, 0.01f) / (dir < 2 ? W : H);
        const double f = fmod((double)s_wpFrame * pix, 1.0);
        const float col = g_warpAnimate ? PingPong(clock * g_warpAnimSpeed) : g_warpSlitPos;
        slit[0] = col;
        slit[1] = (dir == 0 || dir == 2) ? (float)f : (float)fabs(1.0 - f);
        slit[2] = (dir == 0 || dir == 2) ? (float)(f - col) : (float)(f - fmod(1.0 - col, 1.0));
        slit[3] = (float)pix;
    }
    const float fill = g_warpAnimate ? PingPong(clock * 0.1 * g_warpAnimSpeed) : g_warpSlitPos;

    // botox depth_warp.cpp: per channel x/y offsets in uv, red/green in c9, blue in c10
    const bool shift = g_channelShiftEnable && g_channelShiftDepth;
    const float sh[6] = { shift ? g_sfxShiftRedX / W : 0.f,   shift ? g_sfxShiftRedY / H : 0.f,
                          shift ? g_sfxShiftGreenX / W : 0.f, shift ? g_sfxShiftGreenY / H : 0.f,
                          shift ? g_sfxShiftBlueX / W : 0.f,  shift ? g_sfxShiftBlueY / H : 0.f };
    const float* center = effect == 8 ? g_warpPlanetRot : g_warpCenter;

    constexpr int kC = 18;
    const float c[kC][4] = {
        { 1.f / W, 1.f / H, W, H },
        { (float)(clock * 1000.0), (float)g_warpAnimate, g_warpAnimSpeed, H / W },
        { center[0], center[1], g_warpOffset[0], g_warpOffset[1] },
        { g_warpRadius, g_warpTension, amount, angle },
        { period, g_warpPhase, g_warpInnerRadius, g_warpAspect },
        { g_warpAspectAngle, g_warpUseOffset ? 1.f : 0.f, option, (float)std::max(g_warpSplices, 1) },
        { g_warpDepthDist, g_warpDepthRange, 1.f / std::max(g_warpDepthRamp, 1.f), (float)g_warpDepthMode },
        { (float)blend, g_warpEnable ? g_warpOpacity : 0.f, g_warpFade ? 1.f : 0.f, g_warpDepthProtect ? 1.f : 0.f },
        { view.zNear, view.zFar, 0.f, 0.f },
        { sh[0], sh[1], sh[2], sh[3] },
        { sh[4], sh[5], shift ? 1.f : 0.f, 0.f },
        { g_channelShiftDepthDist, g_channelShiftDepthRange, 1.f / std::max(g_channelShiftDepthRamp, 1.f), (float)g_channelShiftDepthMode },
        { g_warpDrunkStrength[0], g_warpDrunkStrength[1], g_warpDrunkSpeed[0], g_warpDrunkSpeed[1] },
        { g_warpDrunkAngle, g_warpDrunkAngleSpeed, g_warpPlanetScale, g_warpPlanetRot[2] },
        { g_warpPlanetOffset[0], g_warpPlanetOffset[1], std::clamp(g_warpPlanetSeam, 0.5f, 1.f), 0.f },
        { slit[0], slit[1], slit[2], slit[3] },
        { g_warpBorderColor[0], g_warpBorderColor[1], g_warpBorderColor[2], g_warpBorderColor[3] },
        { fill, 0.f, 0.f, 0.f },
    };

    // ---- save (depth view's list + a third stage). The shaderapi caches all of it.
    static const D3DRENDERSTATETYPE k_rs[] = {
        D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE, D3DRS_ZENABLE,
        D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
        D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    };
    const DWORD k_rsVal[] = {
        FALSE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE, 0, D3DFILL_SOLID,
        FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, FALSE, FALSE,
    };
    constexpr int kRs = (int)(sizeof(k_rs) / sizeof(k_rs[0]));
    static_assert(sizeof(k_rsVal) / sizeof(k_rsVal[0]) == sizeof(k_rs) / sizeof(k_rs[0]), "state list");

    DWORD oldRs[kRs]{}, oldSamp[3][6]{}, oldFvf = 0;
    float oldC[kC][4]{};
    IDirect3DSurface9* oldDs = nullptr;
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DBaseTexture9* oldTex[3]{};
    IDirect3DVertexBuffer9* oldStream = nullptr;
    IDirect3DIndexBuffer9* oldIdx = nullptr;
    UINT oldOff = 0, oldStride = 0;
    D3DVIEWPORT9 oldVp{};

    if (FAILED(dev->GetDepthStencilSurface(&oldDs))) oldDs = nullptr;
    for (int i = 0; i < kRs; ++i) dev->GetRenderState(k_rs[i], &oldRs[i]);
    for (int u = 0; u < 3; ++u) {
        dev->GetTexture(u, &oldTex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_sfxSamp[i], &oldSamp[u][i]);
    }
    dev->GetPixelShader(&oldPs);
    dev->GetVertexShader(&oldVs);
    dev->GetVertexDeclaration(&oldDecl);
    dev->GetFVF(&oldFvf);
    dev->GetStreamSource(0, &oldStream, &oldOff, &oldStride);
    dev->GetIndices(&oldIdx);
    dev->GetViewport(&oldVp);
    dev->GetPixelShaderConstantF(0, oldC[0], kC);

    // ---- draw: s0 depth point clamp, s1 frame linear mirror, s2 history linear wrap
    static const DWORD k_addr[3] = { D3DTADDRESS_CLAMP, D3DTADDRESS_MIRROR, D3DTADDRESS_WRAP };
    dev->SetDepthStencilSurface(nullptr);
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], k_rsVal[i]);
    for (int u = 0; u < 3; ++u) {
        const DWORD filter = u ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, k_addr[u]);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, k_addr[u]);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, filter);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, filter);
    }
    dev->SetPixelShaderConstantF(0, c[0], kC);
    dev->SetTexture(0, depth);
    dev->SetTexture(1, s_wpFrameTex);
    dev->SetTexture(2, nullptr);
    const D3DVIEWPORT9 vp{ 0, 0, desc.Width, desc.Height, 0.f, 1.f };
    ScreenFx_BindQuad(dev);

    if (scan) {
        dev->SetRenderTarget(0, s_wpHistSurf);
        dev->SetViewport(&vp);
        dev->SetPixelShader(wr);
        ScreenFx_DrawQuad(dev, (int)desc.Width, (int)desc.Height);
        dev->SetTexture(2, s_wpHistTex);
    }

    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetPixelShader(ps);
    ScreenFx_DrawQuad(dev, (int)desc.Width, (int)desc.Height);

    // ---- restore
    dev->SetPixelShaderConstantF(0, oldC[0], kC);
    dev->SetPixelShader(oldPs);
    dev->SetVertexShader(oldVs);
    if (oldDecl) dev->SetVertexDeclaration(oldDecl); else dev->SetFVF(oldFvf);
    dev->SetStreamSource(0, oldStream, oldOff, oldStride);
    dev->SetIndices(oldIdx);
    for (int u = 0; u < 3; ++u) {
        dev->SetTexture(u, oldTex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_sfxSamp[i], oldSamp[u][i]);
    }
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], oldRs[i]);
    dev->SetViewport(&oldVp);
    dev->SetDepthStencilSurface(oldDs);

    for (IUnknown* held : { (IUnknown*)oldDs, (IUnknown*)oldPs, (IUnknown*)oldVs, (IUnknown*)oldDecl,
                            (IUnknown*)oldTex[0], (IUnknown*)oldTex[1], (IUnknown*)oldTex[2],
                            (IUnknown*)oldStream, (IUnknown*)oldIdx })
        if (held) held->Release();

    static int loggedEffect = -2;
    if (loggedEffect != effect) {
        loggedEffect = effect;
        DebugLog("screenfx_passes: depth distort effect %d blend %d shift %d drew %ux%u", effect, blend, (int)shift,
                 (unsigned)desc.Width, (unsigned)desc.Height);
    }
}
