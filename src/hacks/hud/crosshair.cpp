#include "crosshair.h"
#include "ch_blend_modes.h"
#include "../screen/shaders/fx_chblend_vs.h"   // k_fxChBlendVsBytes -- fx_chblend_build.sh
#include "../screen/shaders/fx_chblend_ps.h"   // k_fxChBlendPs[]    -- one ps_3_0 per blend mode
#include "killfeed.h"   // KeepKillfeed_Tick/_Restore -- piggybacked here, see killfeed.h
#include "../aim/css_bridge.h"   // CssBridge_IsScoped/_ActiveWeapon/_IsSniperWeapon -- game-thread only, see css_bridge.h banner
#include "../../dependencies/imgui/imgui_internal.h"   // ImClamp/ImLerp/ImMin/IM_PI -- botox custom_crosshair.cpp uses the same header
#include <algorithm>
#include <vector>


// Full botox custom_crosshair.cpp port: sides/outline sides/taper/gradient/dynamic gap/soft
// outline(blur)/dot colour/rotation/menu preview, plus the new sniper crosshair (indicators.cpp)
// and force-crosshair gate (misc.cpp, CSS-adapted -- see report for what that cvar hack could
// not carry over). Also still carries the CSGO panorama/classic reticle emulation below, which
// is CS:S-only work, not part of the botox port.

struct ChArm { float x1, y1, x2, y2; };

static inline ImU32 ChScaleAlpha(ImU32 col, float t)
{
    if (t <= 0.f) return col & 0x00FFFFFFu;
    if (t >= 1.f) return col;
    int a = (int)(((col >> 24) & 0xFF) * t + 0.5f);
    return (col & 0x00FFFFFFu) | ((ImU32)a << 24);
}

// botox custom_crosshair.cpp lerp_color -- per-channel lerp for the gradient arm.
static inline ImU32 ChLerpColor(ImU32 c0, ImU32 c1, float t)
{
    if (t <= 0.f) return c0;
    if (t >= 1.f) return c1;
    auto l = [](int a, int b, float t) { return (int)(a + (b - a) * t + 0.5f); };
    return IM_COL32(l((c0 >> 0) & 0xFF, (c1 >> 0) & 0xFF, t), l((c0 >> 8) & 0xFF, (c1 >> 8) & 0xFF, t),
                    l((c0 >> 16) & 0xFF, (c1 >> 16) & 0xFF, t), l((c0 >> 24) & 0xFF, (c1 >> 24) & 0xFF, t));
}

// botox custom_crosshair.cpp pack_rgba -- shared by the live crosshair and its menu preview.
static inline ImU32 ChPackRGBA(const float c[4])
{
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), (int)(c[3] * 255.f));
}

// One pixel-aligned band of a span, covered by fraction cov.
struct ChSpan { float a, b, cov; };

// Cut exact span [lo,hi] into <=3 pixel-aligned bands + coverage.
// Covers the thin cases: span inside one pixel, span straddling one pixel edge.
static int ChMakeSpans(float lo, float hi, ChSpan out[3])
{
    int n = 0;
    const float fl = floorf(lo), ce = ceilf(hi);

    if (ce - fl <= 1.0f) {                  // whole span sits in one pixel
        out[n++] = { fl, fl + 1.f, hi - lo };
        return n;
    }

    const float cl = ceilf(lo), fh = floorf(hi);
    const float covL = cl - lo;
    if (covL > 0.f) out[n++] = { cl - 1.f, cl, covL };
    if (fh > cl)    out[n++] = { cl, fh, 1.f };
    const float covR = hi - fh;
    if (covR > 0.f) out[n++] = { fh, fh + 1.f, covR };
    return n;
}

// Rect rasterizer. Takes exact float geometry, no pre-rounding by the caller.
// AA on  -> per-pixel coverage, so sub-1px rects fade instead of breaking.
// AA off -> snap both edges the same way, min 1px so thin rects never vanish.
static void ChDrawRect(ImDrawList* dl, float x1, float y1, float x2, float y2, ImU32 col)
{
    if (x2 <= x1 || y2 <= y1) return;

    if (!g_chAntialias) {
        float rx1 = floorf(x1 + 0.5f), rx2 = floorf(x2 + 0.5f);
        float ry1 = floorf(y1 + 0.5f), ry2 = floorf(y2 + 0.5f);
        if (rx2 <= rx1) { rx1 = floorf((x1 + x2) * 0.5f); rx2 = rx1 + 1.f; }
        if (ry2 <= ry1) { ry1 = floorf((y1 + y2) * 0.5f); ry2 = ry1 + 1.f; }
        dl->AddRectFilled(ImVec2(rx1, ry1), ImVec2(rx2, ry2), col);
        return;
    }

    ChSpan xs[3], ys[3];
    const int nx = ChMakeSpans(x1, x2, xs);
    const int ny = ChMakeSpans(y1, y2, ys);
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
        {
            const float cov = xs[i].cov * ys[j].cov;
            if (cov <= 0.002f) continue;
            dl->AddRectFilled(ImVec2(xs[i].a, ys[j].a), ImVec2(xs[i].b, ys[j].b),
                              ChScaleAlpha(col, cov));
        }
}

// sideL/R/T/B let the outline grow on only some edges of the rect (botox custom_crosshair.cpp
// outline_sides_only) -- the disabled edges draw with 0 expansion, same as botox's draw_outline.
static void ChDrawOutline(ImDrawList* dl,
    float ax1, float ay1, float ax2, float ay2, float otUnits, ImU32 col,
    bool sideL = true, bool sideR = true, bool sideT = true, bool sideB = true)
{
    float px = otUnits * kChUnitToPx;
    if (!g_chAntialias && px < 1.f) px = 1.f;   // keep outline visible when snapped
    ChDrawRect(dl, ax1 - (sideL ? px : 0.f), ay1 - (sideT ? px : 0.f),
                   ax2 + (sideR ? px : 0.f), ay2 + (sideB ? px : 0.f), col);
}

// Soft outline / drop shadow (botox custom_crosshair.cpp draw_outline_blur). Builds the glow as
// a stack of OVER-composited rings, each ring's alpha picked so the CUMULATIVE alpha at radius r
// follows a gaussian falloff -- same inverted-compositing trick botox uses, so n blended rings
// read as one smooth glow instead of n visible bands.
static void ChDrawOutlineBlur(ImDrawList* dl,
    float ax1, float ay1, float ax2, float ay2, float otUnits, ImU32 col, float blurRadius,
    bool sideL = true, bool sideR = true, bool sideT = true, bool sideB = true)
{
    const float baseA     = ((col >> 24) & 0xFF) / 255.f;
    const ImU32 rgb        = col & 0x00FFFFFFu;
    const float maxExpand  = blurRadius * kChUnitToPx;
    const float sigma      = maxExpand * 0.4f + 0.5f;
    const int   n          = 16;
    float baseThick        = otUnits * kChUnitToPx;
    if (!g_chAntialias && baseThick < 1.f) baseThick = 1.f;

    float cumTransp = 1.f;
    for (int i = n - 1; i >= 0; --i)
    {
        const float br     = ((float)(i + 1) / n) * maxExpand;
        const float target = baseA * expf(-0.5f * (br / sigma) * (br / sigma));
        if (cumTransp < 1e-4f) break;
        const float layerA = 1.f - (1.f - target) / cumTransp;
        if (layerA <= 0.f) continue;
        const int ai = (int)(layerA * 255.f + 0.5f);
        if (ai <= 0) continue;
        cumTransp *= (1.f - layerA);
        const float px = baseThick + br;
        ChDrawRect(dl, ax1 - (sideL ? px : 0.f), ay1 - (sideT ? px : 0.f),
                       ax2 + (sideR ? px : 0.f), ay2 + (sideB ? px : 0.f),
                       rgb | ((ImU32)ai << 24));
    }
    ChDrawOutline(dl, ax1, ay1, ax2, ay2, otUnits, col, sideL, sideR, sideT, sideB);
}

// One arm, sliced into 1px columns along its length so taper (half-thickness lerp) and
// gradient (colour lerp) both read smoothly -- botox custom_crosshair.cpp draw_arm_fancy.
// dir: 0=L, 1=R, 2=B, 3=T (ChMakeArm's own convention).
static void ChDrawArmFancy(ImDrawList* dl, const ChArm& a, int dir, ImU32 centerCol, ImU32 outerCol,
    float gradStart, float gradEnd, float nearScale, float farScale)
{
    gradStart = ImClamp(gradStart, 0.f, 1.f);
    gradEnd   = ImClamp(gradEnd, gradStart, 1.f);

    const bool  hz         = (dir <= 1);
    const float baseHalf   = hz ? (a.y2 - a.y1) * 0.5f : (a.x2 - a.x1) * 0.5f;
    const float innerHalf  = baseHalf * nearScale;
    const float outerHalf  = baseHalf * farScale;
    const float centerLine = hz ? (a.y1 + a.y2) * 0.5f : (a.x1 + a.x2) * 0.5f;
    const float gRange     = gradEnd - gradStart;

    float axStart, axEnd;
    switch (dir) {
        case 0:  axStart = a.x2; axEnd = a.x1; break;   // L: inner edge -> outer edge
        case 1:  axStart = a.x1; axEnd = a.x2; break;   // R
        case 2:  axStart = a.y1; axEnd = a.y2; break;   // B
        default: axStart = a.y2; axEnd = a.y1; break;   // T
    }
    const float xMin  = fminf(axStart, axEnd), xMax = fmaxf(axStart, axEnd);
    const float axSpan = axEnd - axStart;

    for (float x = floorf(xMin); x < xMax - 1e-4f; x += 1.f)
    {
        const float rx1 = fmaxf(x, xMin), rx2 = fminf(x + 1.f, xMax);
        const float xMid = (rx1 + rx2) * 0.5f;

        const float t = (fabsf(axSpan) > 1e-5f) ? ImClamp((xMid - axStart) / axSpan, 0.f, 1.f) : 0.5f;
        const float h = innerHalf + t * (outerHalf - innerHalf);
        const float g = (gRange > 0.001f) ? ImClamp((t - gradStart) / gRange, 0.f, 1.f) : (t >= gradStart ? 1.f : 0.f);
        const ImU32 col = ChLerpColor(centerCol, outerCol, g);

        if (hz) ChDrawRect(dl, rx1, centerLine - h, rx2, centerLine + h, col);
        else    ChDrawRect(dl, centerLine - h, rx1, centerLine + h, rx2, col);
    }
}

// dir: 0=L, 1=R, 2=B, 3=T. Band is centered on the exact center, so arms stay
// symmetric at any thickness -- the rasterizer handles pixel snapping.
static ChArm ChMakeArm(float cx, float cy, float thPx, float len, float gap, int dir)
{
    const float h = thPx * 0.5f;
    ChArm a{};
    switch (dir) {
        case 0: a = { cx-gap-len, cy-h, cx-gap,     cy+h }; break;
        case 1: a = { cx+gap,     cy-h, cx+gap+len, cy+h }; break;
        case 2: a = { cx-h, cy+gap,     cx+h, cy+gap+len }; break;
        case 3: a = { cx-h, cy-gap-len, cx+h, cy-gap     }; break;
    }
    return a;
}

// Put the bar band on the pixel grid: odd px width wants a pixel center, even
// wants a pixel edge. Without this a 1px band lands on 539.5..540.5 and AA
// smears it 50/50 over two rows -- reads as faded, not thin.
//
// An odd band cannot be centered on an even-width screen: 1920/2 = 960.0 is a pixel
// EDGE, so the snap moves the WHOLE crosshair half a pixel right and down. Under 1 unit
// (2 px) that shove was pure loss -- `thI` is clamped to 1, so every sub-pixel thickness
// took the odd branch, sat off-center, and jumped back the moment thickness hit 1.0
// (2 px = even). Under 1 unit the band is drawn exactly where it belongs and coverage
// thins it, which is what RenderCustomCrosshair already claims it does.
static float ChSnapCenter(float c, float thPx)
{
    if (thPx < 2.f) return c;
    int thI = (int)floorf(thPx + 0.5f);
    return floorf(c) + ((thI & 1) ? 0.5f : 0.0f);
}

// SrgbGammaToLinear, quantised back to the 8 bits a vertex colour carries.
static int ChCsToLinear8(float srgb)
{
    if (srgb < 0.f) srgb = 0.f;
    if (srgb > 1.f) srgb = 1.f;
    const float lin = (srgb <= 0.04045f) ? (srgb / 12.92f)
                                         : powf((srgb + 0.055f) / 1.055f, 2.4f);
    return (int)(lin * 255.f + 0.5f);
}

// Can the ROP actually blend in linear light? Two things must hold, and the code used to
// check only the first: POSTBLENDSRGBCONVERT (blend, THEN encode -- without it the convert
// happens pre-blend and the whole thing is a no-op) and the render target's own format
// taking an sRGB write. If the format refuses, D3DRS_SRGBWRITEENABLE is silently ignored
// and every linear-encoded vertex colour we hand it lands too dark. Probed once per device.
static IDirect3DDevice9* s_srgbDev = nullptr;
static int               s_srgbOk  = -1;
static bool ChSrgbBlendOk()
{
    if (!g_d3dDev) return false;
    if (s_srgbDev == g_d3dDev && s_srgbOk >= 0) return s_srgbOk == 1;

    s_srgbDev = g_d3dDev;
    s_srgbOk  = 0;

    D3DCAPS9 caps;
    if (FAILED(g_d3dDev->GetDeviceCaps(&caps))) return false;
    const bool postBlend = (caps.PrimitiveMiscCaps & D3DPMISCCAPS_POSTBLENDSRGBCONVERT) != 0;

    D3DSURFACE_DESC rtd; rtd.Format = D3DFMT_UNKNOWN;
    IDirect3DSurface9* rt = nullptr;
    if (SUCCEEDED(g_d3dDev->GetRenderTarget(0, &rt)) && rt) { rt->GetDesc(&rtd); rt->Release(); }

    IDirect3D9* d3d = nullptr;
    HRESULT hrFmt = E_FAIL;
    if (SUCCEEDED(g_d3dDev->GetDirect3D(&d3d)) && d3d)
    {
        D3DDEVICE_CREATION_PARAMETERS cp; D3DDISPLAYMODE dm;
        if (SUCCEEDED(g_d3dDev->GetCreationParameters(&cp))
            && SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &dm)))
            hrFmt = d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, dm.Format,
                                           D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBWRITE,
                                           D3DRTYPE_SURFACE, rtd.Format);
        d3d->Release();
    }

    s_srgbOk = (postBlend && SUCCEEDED(hrFmt)) ? 1 : 0;
    return s_srgbOk == 1;
}

// Additive blend for the custom crosshair. ImGui's default ROP is SRCALPHA/INVSRCALPHA;
// swapping the DEST factor to ONE makes the quad ADD (src*srcAlpha + dst).
//
// Doing that add on sRGB BYTES is what turned the crosshair white: measured on this GPU,
// the user's own mint (128,255,128) added over mid grey clips to 255,255,255 -- every
// channel pinned, the colour gone. The identical add in LINEAR light lands 176,255,176 and
// keeps the hue, because a mid grey is only 0.216 of the light an sRGB 0.5 claims. So the
// arms now drive the same sRGB convert the csgo reticle uses (the ROP decodes the frame,
// adds, re-encodes) and RenderCustomCrosshair hands them linear vertex colours to match.
// The point sampler stays out of it -- that belongs to the atlas art, not untextured rects.
static void ChAddOn(const ImDrawList*, const ImDrawCmd*)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    if (ChSrgbBlendOk()) g_d3dDev->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE);
}
static void ChAddOff(const ImDrawList*, const ImDrawCmd*)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    if (ChSrgbBlendOk()) g_d3dDev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
}

// ==== blend modes 2+ (ch_blend_modes.h) =============================================
// Everything past normal/additive needs the frame UNDER the arms, and no fixed-function
// blend factor can read that. So ChBlendOn copies the render target into s_bmTex and binds
// that mode's ps_3_0 (fx_chblend_ps.hlsl -- Krita's formulas, checked against Krita's own
// code by tools/chblend_test), which reads the copy at VPOS. The shader returns the blended
// colour plus the crosshair's alpha, and ImGui's SRCALPHA/INVSRCALPHA does the opacity lerp.
// Blending is in the frame's sRGB-encoded values, the way Krita blends an 8-bit layer.
//
// The copy is taken after the outline went down and before the arms, so the outline stays a
// normal blend and is IN the frame the arms blend with.
//
// ONE COPY PER IMGUI FRAME PER RENDER TARGET. The same draw lists can be submitted several
// times in one frame (every backbuffer scene when vgui paints off-thread). A fresh copy each
// time would hold the previous pass's crosshair, and blending onto that again is not a no-op:
// difference twice IS the original frame, so the crosshair would blink with the scene count.
// Reusing the first copy makes every pass write the same pixels. The first backbuffer scene
// already holds the world (colour correction grades there for the same reason).
static IDirect3DDevice9*       s_bmDev     = nullptr;
static IDirect3DVertexShader9* s_bmVs      = nullptr;
static IDirect3DPixelShader9*  s_bmPs[kChBlendCount] = {};
static bool                    s_bmPsBad[kChBlendCount] = {};   // refused once: not retried per scene
static IDirect3DTexture9*      s_bmTex     = nullptr;
static IDirect3DSurface9*      s_bmSurf    = nullptr;
static UINT                    s_bmW = 0, s_bmH = 0;
static IDirect3DSurface9*      s_bmResolve = nullptr;           // msaa frame: resolve here, then copy
static D3DSURFACE_DESC         s_bmResolveDesc{};
static UINT                    s_bmFailW = 0, s_bmFailH = 0;    // size whose copy target was refused
static int                     s_bmCopyFrame = -1;              // ImGui frame + target the copy holds
static IDirect3DSurface9*      s_bmCopyRt    = nullptr;         // identity only, never dereferenced
static bool                    s_bmDead    = false;             // no shader model 3: stay normal
static bool                    s_bmLive    = false;             // ChBlendOn bound its state this draw

static void ChBlendReleaseTargets()
{
    if (s_bmResolve) { s_bmResolve->Release(); s_bmResolve = nullptr; }
    if (s_bmSurf)    { s_bmSurf->Release();    s_bmSurf    = nullptr; }
    if (s_bmTex)     { s_bmTex->Release();     s_bmTex     = nullptr; }
    s_bmW = s_bmH = 0;
    s_bmResolveDesc = D3DSURFACE_DESC{};
    s_bmCopyFrame = -1;
    s_bmCopyRt    = nullptr;
}

static void ChBlendRelease()
{
    ChBlendReleaseTargets();
    for (IDirect3DPixelShader9*& p : s_bmPs) if (p) { p->Release(); p = nullptr; }
    for (bool& b : s_bmPsBad) b = false;
    if (s_bmVs) { s_bmVs->Release(); s_bmVs = nullptr; }
    s_bmFailW = s_bmFailH = 0;
    s_bmDev  = nullptr;
    s_bmDead = false;
    s_bmLive = false;
}

// cfg can hold anything; an old cfg only knows the additive checkbox
static int ChBlendMode()
{
    const int m = (g_chBlendMode > 0) ? g_chBlendMode : (g_chAdditive ? 1 : 0);
    return (m >= 0 && m < kChBlendCount) ? m : 0;
}

// Callback payload: mode in the low byte, the ImGui frame it was recorded in above it.
static void* ChBlendPack(int mode)
{
    return (void*)(((intptr_t)ImGui::GetFrameCount() << 8) | (intptr_t)mode);
}

static IDirect3DPixelShader9* ChBlendShader(IDirect3DDevice9* dev, int mode)
{
    if (dev != s_bmDev) { ChBlendRelease(); s_bmDev = dev; }
    if (s_bmDead || s_bmPsBad[mode]) return nullptr;

    if (!s_bmVs)
    {
        D3DCAPS9 caps{};
        if (FAILED(dev->GetDeviceCaps(&caps)) ||
            caps.PixelShaderVersion  < D3DPS_VERSION(3, 0) ||
            caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
        { DebugLog("chblend: no shader model 3 -- blend modes draw normal"); s_bmDead = true; return nullptr; }

        std::vector<DWORD> code((sizeof(k_fxChBlendVsBytes) + 3) / 4);   // BYTE arrays carry no alignment
        memcpy(code.data(), k_fxChBlendVsBytes, sizeof(k_fxChBlendVsBytes));
        if (FAILED(dev->CreateVertexShader(code.data(), &s_bmVs)))
        { DebugLog("chblend: vertex shader refused"); s_bmVs = nullptr; s_bmDead = true; return nullptr; }
    }

    if (!s_bmPs[mode])
    {
        const FxChBlendPs& b = k_fxChBlendPs[mode];
        std::vector<DWORD> code((b.size + 3) / 4);
        if (b.bytes) memcpy(code.data(), b.bytes, b.size);
        if (!b.bytes || FAILED(dev->CreatePixelShader(code.data(), &s_bmPs[mode])))
        {
            DebugLog("chblend: pixel shader for mode %d refused", mode);
            s_bmPs[mode]    = nullptr;
            s_bmPsBad[mode] = true;
            return nullptr;
        }
    }
    return s_bmPs[mode];
}

// The copy is always X8R8G8B8 and keyed on size alone: the game frame (often X8, maybe msaa)
// and fpsoverlay's A8 menu layer can both blend in one frame, and a format-keyed cache would
// rebuild the target twice a frame. StretchRect converts between the two 32-bit formats.
// DEFAULT pool: gone on Reset, rebuilt on the next blended frame.
static bool ChBlendTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& rt)
{
    if (!s_bmSurf || s_bmW != rt.Width || s_bmH != rt.Height)
    {
        if (s_bmFailW == rt.Width && s_bmFailH == rt.Height) return false;
        ChBlendReleaseTargets();
        HRESULT hr = dev->CreateTexture(rt.Width, rt.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                        D3DPOOL_DEFAULT, &s_bmTex, nullptr);
        if (FAILED(hr))
            hr = dev->CreateTexture(rt.Width, rt.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &s_bmTex, nullptr);
        if (SUCCEEDED(hr)) hr = s_bmTex->GetSurfaceLevel(0, &s_bmSurf);
        if (FAILED(hr))
        {
            DebugLog("chblend: copy target %ux%u failed 0x%08lx", rt.Width, rt.Height, (unsigned long)hr);
            ChBlendReleaseTargets();
            s_bmFailW = rt.Width; s_bmFailH = rt.Height;
            return false;
        }
        s_bmW = rt.Width; s_bmH = rt.Height;
        DebugLog("chblend: copy target %ux%u", rt.Width, rt.Height);
    }

    if (rt.MultiSampleType == D3DMULTISAMPLE_NONE) return true;

    // msaa: resolve into a plain surface of the frame's own format first, same as dof does
    if (s_bmResolve && s_bmResolveDesc.Width == rt.Width && s_bmResolveDesc.Height == rt.Height &&
        s_bmResolveDesc.Format == rt.Format)
        return true;
    if (s_bmResolve) { s_bmResolve->Release(); s_bmResolve = nullptr; }
    if (FAILED(dev->CreateRenderTarget(rt.Width, rt.Height, rt.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                       &s_bmResolve, nullptr)))
    {
        DebugLog("chblend: msaa resolve target %ux%u fmt %d failed", rt.Width, rt.Height, (int)rt.Format);
        s_bmResolve = nullptr;
        ChBlendReleaseTargets();
        s_bmFailW = rt.Width; s_bmFailH = rt.Height;
        return false;
    }
    s_bmResolveDesc = rt;
    return true;
}

static void ChBlendOn(const ImDrawList*, const ImDrawCmd* cmd)
{
    s_bmLive = false;
    IDirect3DDevice9* dev = g_d3dDev;
    const intptr_t packed = (intptr_t)cmd->UserCallbackData;
    const int mode  = (int)(packed & 0xFF);
    const int frame = (int)(packed >> 8);
    if (!dev || mode < 2 || mode >= kChBlendCount) return;

    // Anything failing below leaves ImGui's own state untouched: the arms just draw normal.
    IDirect3DPixelShader9* ps = ChBlendShader(dev, mode);
    if (!ps) return;

    IDirect3DSurface9* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return;
    D3DSURFACE_DESC desc{};
    HRESULT hr = rt->GetDesc(&desc);
    if (SUCCEEDED(hr) && !ChBlendTargets(dev, desc)) hr = E_FAIL;
    if (SUCCEEDED(hr) && (s_bmCopyFrame != frame || s_bmCopyRt != rt))
    {
        hr = dev->StretchRect(rt, nullptr, s_bmResolve ? s_bmResolve : s_bmSurf, nullptr, D3DTEXF_NONE);
        if (SUCCEEDED(hr) && s_bmResolve)
            hr = dev->StretchRect(s_bmResolve, nullptr, s_bmSurf, nullptr, D3DTEXF_NONE);
        s_bmCopyFrame = SUCCEEDED(hr) ? frame : -1;
        s_bmCopyRt    = SUCCEEDED(hr) ? rt : nullptr;
    }
    rt->Release();
    if (FAILED(hr))
    {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("chblend: frame copy failed 0x%08lx", (unsigned long)hr); }
        return;
    }

    // The vertex shader must rebuild exactly the ortho ImGui built, and ImGui builds it from
    // DisplaySize -- not from the viewport, which only matches when its SetViewport succeeded.
    // The copy's own size maps VPOS back to texels.
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float vsc[4] = { ds.x, ds.y, 0.f, 0.f };
    const float psc[4] = { 0.f, 1.f / (float)s_bmW, 1.f / (float)s_bmH, 0.f };
    dev->SetVertexShader(s_bmVs);
    dev->SetPixelShader(ps);
    dev->SetVertexShaderConstantF(0, vsc, 1);
    dev->SetPixelShaderConstantF(0, psc, 1);
    // texture first: the engine's SetTexture wrapper turns sRGB decode back on inside it
    // (same Source x64 branch as TF2, confirmed same material-system wrapper behaviour)
    dev->SetTexture(1, s_bmTex);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER,   D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER,   D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);
    s_bmLive = true;
}

static void ChBlendOff(const ImDrawList*, const ImDrawCmd*)
{
    if (!s_bmLive || !g_d3dDev) return;
    s_bmLive = false;
    // back to ImGui's fixed pipeline; its state block puts the rest back at the end
    g_d3dDev->SetVertexShader(nullptr);
    g_d3dDev->SetPixelShader(nullptr);
    g_d3dDev->SetTexture(1, nullptr);
}

bool Crosshair_BlendCombo(const char* id)
{
    const int cur = ChBlendMode();
    const bool open = ImGui::BeginCombo(id, kChBlend[cur].name, ImGuiComboFlags_HeightLargest);
    // read now, while the last item is still the combo frame -- after EndCombo it is a popup row
    const bool hovered = !open && ImGui::IsItemHovered();
    if (!open) return hovered;
    const char* cat = nullptr;
    for (int i = 0; i < kChBlendCount; ++i)
    {
        if (!cat || strcmp(cat, kChBlend[i].cat) != 0)
        {
            cat = kChBlend[i].cat;
            if (i > 0) ImGui::SeparatorText(cat);
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(kChBlend[i].name, i == cur))
        {
            g_chBlendMode = i;
            g_chAdditive  = (i == 1);   // old builds reading this cfg still see additive
        }
        ImGui::PopID();
        if (i == cur) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return false;
}

// Local player flags/velocity, read off the verified g_offFlags/g_offVelocity/g_offMaxspeed
// offsets RenderCsgoPanorama/RenderCsgoClassic already use below -- moved up here so the
// custom crosshair's dynamic gap (botox custom_crosshair.cpp s.dynamic) can read speed2D too.
static constexpr float kCsMaxSpeedFallback = 215.0f;   // CS_PLAYER_SPEED_RUN fallback

struct CsPlayerState
{
    bool  valid    = false;
    bool  onGround = true;
    bool  ducking  = false;
    float speed2D  = 0.f;
    float velZ     = 0.f;
    float maxSpeed = kCsMaxSpeedFallback;
};

static CsPlayerState CsGetPlayerState()
{
    CsPlayerState s;
    if (g_offFlags <= 0 || g_offVelocity <= 0) return s;
    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return s;
    void* pEnt = GetClientEntity(idx);
    if (!pEnt) return s;
    const uintptr_t base = reinterpret_cast<uintptr_t>(pEnt);
    if (base < 0x10000) return s;
    __try {
        const int flags = *reinterpret_cast<int*>(base + g_offFlags);
        const float* v  = reinterpret_cast<const float*>(base + g_offVelocity);
        s.onGround = (flags & (1 << 0)) != 0;   // FL_ONGROUND
        s.ducking  = (flags & (1 << 1)) != 0;   // FL_DUCKING
        s.speed2D  = sqrtf(v[0]*v[0] + v[1]*v[1]);
        s.velZ     = v[2];
        if (g_offMaxspeed > 0) {
            const float ms = *reinterpret_cast<const float*>(base + g_offMaxspeed);
            if (ms > 1.f) s.maxSpeed = ms;      // class run speed stands in for GetMaxSpeed
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return s; }
    s.valid = true;
    return s;
}

// The real renderer, shared by the live overlay and the menu preview (botox custom_crosshair.cpp
// draw_custom). speed < 0 = standing (botox convention) -- dynamic gap only grows for speed >= 0.
static void ChDrawCustomCrosshairCore(ImDrawList* dl, float cx, float cy, float scale, float speed)
{
    const int vtxStart = dl->VtxBuffer.Size;

    // Thickness under 1 unit stays exact: AA thins the line by coverage,
    // non-AA clamps it to 1px in ChDrawRect.
    const float thPx = g_chThickness * kChUnitToPx * scale;
    const float len  = g_chLength    * kChUnitToPx * scale;
    const float gap  = g_chGap       * kChUnitToPx * scale;

    // Dynamic gap (botox custom_crosshair.cpp s.dynamic): extra gap that grows with the local
    // player's 2D speed, from 0 at dynamicMin up to dynamicMax at dynamicRef. CS:S has no native
    // weapon-inaccuracy read wired to this unit (see report) -- movement speed is what botox's
    // own on_paint_traverse/draw_preview actually feed draw_custom's `speed` parameter, so this
    // is a 1:1 port of what botox draws, not a simplification.
    float dynGapPx = 0.f;
    if (g_chDynamic && g_chDynamicRef > 0.f && speed >= 0.f && speed > g_chDynamicMin)
    {
        const float range = g_chDynamicRef - g_chDynamicMin;
        float t = (range > 0.f) ? (speed - g_chDynamicMin) / range : 1.f;
        if (t > 1.f) t = 1.f;
        dynGapPx = t * g_chDynamicMax * kChUnitToPx * scale;
    }
    const float effGap = gap + dynGapPx;

    // Arm aspect = w:h of the arm pair. The SHORT axis keeps `length`, the long one is
    // stretched, so the slider never shrinks an arm out from under you: >1 grows the
    // horizontal pair, <1 grows the vertical pair by 1/aspect.
    const float asp  = g_chAspect > 0.01f ? g_chAspect : 0.01f;
    const float hLen = len * (asp >= 1.f ? asp : 1.f);
    const float vLen = len * (asp <= 1.f ? (1.f / asp) : 1.f);
    // same rule on the gap, on its own knob -- a wide arm pair and a wide gap are
    // separate looks, and one aspect driving both can't make either
    const float gAsp = g_chGapAspect > 0.01f ? g_chGapAspect : 0.01f;
    const float hGap = effGap * (gAsp >= 1.f ? gAsp : 1.f);
    const float vGap = effGap * (gAsp <= 1.f ? (1.f / gAsp) : 1.f);
    // dir: 0=L, 1=R, 2=B, 3=T -- 0/1 are the horizontal pair
    auto armLen = [&](int dir) { return (dir <= 1) ? hLen : vLen; };
    auto armGap = [&](int dir) { return (dir <= 1) ? hGap : vGap; };
    // only some sides (botox s.sides_only) -- dir indices line up 1:1 with the named bools
    // (ChMakeArm's own L/R/B/T order), no remap needed.
    auto armEnabled = [&](int dir) {
        if (!g_chSidesOnly) return true;
        switch (dir) { case 0: return g_chSideL; case 1: return g_chSideR;
                       case 2: return g_chSideB; default: return g_chSideT; }
    };

    const float cx2 = (scale == 1.f) ? ChSnapCenter(cx, thPx) : cx;   // only snap the live overlay
    const float cy2 = (scale == 1.f) ? ChSnapCenter(cy, thPx) : cy;

    // Only the additive pass blends in linear light, so only its colour is decoded. The
    // outline goes down before ChAddOn and stays a plain sRGB alpha blend.
    const int  blend  = ChBlendMode();
    const bool addLin = blend == 1 && ChSrgbBlendOk();
    auto toDrawCol = [&](const float c[4]) -> ImU32 {
        return addLin ? IM_COL32(ChCsToLinear8(c[0]), ChCsToLinear8(c[1]), ChCsToLinear8(c[2]), (int)(c[3]*255.f))
                      : ChPackRGBA(c);
    };
    const ImU32 col  = toDrawCol(g_chColor);
    const ImU32 outl = ChPackRGBA(g_chOutlineColor);

    // Taper (half-thickness lerp) and gradient (colour lerp) both ride the fancy-arm path;
    // sub-pixel thickness (<1 unit) skips it, same as botox's fancy_arm = !sub_pixel && (...).
    const bool  subPixel  = g_chThickness < 1.0f;
    const bool  fancyArm  = !subPixel && (g_chGradient || g_chTaper);
    ImU32 gradCenterCol = col, gradOuterCol = col;
    float gradStart = 0.f, gradEnd = 1.f, taperNear = 1.f, taperFar = 1.f;
    if (fancyArm)
    {
        if (g_chGradient)
        {
            gradCenterCol = toDrawCol(g_chGradCenter);
            gradOuterCol  = toDrawCol(g_chGradOuter);
            gradStart     = g_chGradStart;
            gradEnd       = g_chGradEnd;
        }
        taperNear = g_chTaper ? g_chTaperNear : 1.f;
        taperFar  = g_chTaper ? g_chTaperFar  : 1.f;
    }

    const bool sl = !g_chOutlineSidesOnly || g_chOutlineSideL;
    const bool sr = !g_chOutlineSidesOnly || g_chOutlineSideR;
    const bool st = !g_chOutlineSidesOnly || g_chOutlineSideT;
    const bool sb = !g_chOutlineSidesOnly || g_chOutlineSideB;

    if (g_chOutline)
    {
        const float ot = g_chOutlineThick < 0.1f ? 0.1f : g_chOutlineThick;
        const float armBaseHalf = thPx * 0.5f;
        if (g_chOutlineOnLines)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (!armEnabled(i)) continue;
                ChArm a = ChMakeArm(cx2, cy2, thPx, armLen(i), armGap(i), i);
                if (fancyArm && !g_chOutlineBlur)
                {
                    // outline rides the same taper/gradient shape as the arm, just expanded by
                    // the outline thickness -- botox custom_crosshair.cpp draw_custom (outline_on_lines).
                    ChArm outA = a;
                    const float otPx = g_chAntialias ? (ot * kChUnitToPx) : ((ot < 1.f) ? 1.f : floorf(ot * kChUnitToPx));
                    if (i <= 1) { outA.x1 -= otPx; outA.x2 += otPx; } else { outA.y1 -= otPx; outA.y2 += otPx; }
                    const float addScale = armBaseHalf > 0.f ? otPx / armBaseHalf : 0.f;
                    ChDrawArmFancy(dl, outA, i, outl, outl, 0.f, 0.f, taperNear + addScale, taperFar + addScale);
                }
                else if (g_chOutlineBlur)
                    ChDrawOutlineBlur(dl, a.x1, a.y1, a.x2, a.y2, ot, outl, g_chOutlineBlurRadius, sl, sr, st, sb);
                else
                    ChDrawOutline(dl, a.x1, a.y1, a.x2, a.y2, ot, outl, sl, sr, st, sb);
            }
        }
        if (g_chDot && g_chOutlineOnDot)
        {
            const float r = thPx * 0.5f;
            if (g_chDotCircle && g_chOutlineBlur)
            {
                // concentric fading rings around the dot, same inverted-compositing gaussian
                // falloff as ChDrawOutlineBlur -- botox draws the circle case separately because
                // AddCircleFilled has no rect-expansion equivalent.
                const float baseA  = ((outl >> 24) & 0xFF) / 255.f;
                const ImU32 oRgb   = outl & 0x00FFFFFFu;
                const float maxExp = g_chOutlineBlurRadius * kChUnitToPx;
                const float sigma  = maxExp * 0.4f + 0.5f;
                const float otPx   = g_chAntialias ? (ot * kChUnitToPx) : ((ot < 1.f) ? 1.f : floorf(ot * kChUnitToPx));
                float cumT = 1.f;
                for (int i = 15; i >= 0; --i)
                {
                    const float br  = ((float)(i + 1) / 16) * maxExp;
                    const float tgt = baseA * expf(-0.5f * (br / sigma) * (br / sigma));
                    if (cumT < 1e-4f) break;
                    const float la = 1.f - (1.f - tgt) / cumT;
                    if (la <= 0.f) continue;
                    const int ai = (int)(la * 255.f + 0.5f);
                    if (ai <= 0) continue;
                    cumT *= (1.f - la);
                    dl->AddCircleFilled(ImVec2(cx2, cy2), r + otPx + br, oRgb | ((ImU32)ai << 24));
                }
                dl->AddCircleFilled(ImVec2(cx2, cy2), r + otPx, outl);
            }
            else if (g_chDotCircle)
            {
                float px = ot * kChUnitToPx;
                if (!g_chAntialias && px < 1.f) px = 1.f;
                dl->AddCircleFilled(ImVec2(cx2, cy2), r + px, outl);
            }
            else if (g_chOutlineBlur)
                ChDrawOutlineBlur(dl, cx2-r, cy2-r, cx2+r, cy2+r, ot, outl, g_chOutlineBlurRadius, sl, sr, st, sb);
            else
                ChDrawOutline(dl, cx2-r, cy2-r, cx2+r, cy2+r, ot, outl, sl, sr, st, sb);
        }
    }

    // the outline stays NORMAL on purpose -- it is a dark band, and black added to the
    // frame is a no-op, so an additive outline is an outline that isn't there. csgo does
    // the same split (black pips normal, green pips additive). Drawing it underneath a
    // then-additive arm still works: the arm adds onto the darkened pixels. The shader modes
    // follow the same split for the same reason.
    if (blend == 1)      dl->AddCallback(ChAddOn, nullptr);
    else if (blend >= 2) dl->AddCallback(ChBlendOn, ChBlendPack(blend));

    for (int i = 0; i < 4; ++i)
    {
        if (!armEnabled(i)) continue;
        ChArm a = ChMakeArm(cx2, cy2, thPx, armLen(i), armGap(i), i);
        if (fancyArm) ChDrawArmFancy(dl, a, i, gradCenterCol, gradOuterCol, gradStart, gradEnd, taperNear, taperFar);
        else          ChDrawRect(dl, a.x1, a.y1, a.x2, a.y2, col);
    }

    if (g_chDot)
    {
        const float r = thPx * 0.5f;
        const ImU32 dotCol = g_chDotCustomColor ? toDrawCol(g_chDotColor)
                            : ((g_chGradient && fancyArm) ? gradCenterCol : col);
        if (g_chDotCircle)
            dl->AddCircleFilled(ImVec2(cx2, cy2), r, dotCol);
        else
            ChDrawRect(dl, cx2-r, cy2-r, cx2+r, cy2+r, dotCol);
    }

    // Whole-crosshair rotation (botox custom_crosshair.cpp): spin every vertex this draw just
    // emitted about the centre. Done last so taper/gradient/outline math above stays axis-aligned.
    if (fabsf(g_chRotation) > 0.01f)
    {
        const float rad = g_chRotation * IM_PI / 180.f;
        const float ca = cosf(rad), sa = sinf(rad);
        for (int vi = vtxStart; vi < dl->VtxBuffer.Size; ++vi)
        {
            const float dx = dl->VtxBuffer[vi].pos.x - cx2;
            const float dy = dl->VtxBuffer[vi].pos.y - cy2;
            dl->VtxBuffer[vi].pos.x = cx2 + dx * ca - dy * sa;
            dl->VtxBuffer[vi].pos.y = cy2 + dx * sa + dy * ca;
        }
    }

    if (blend == 1)      dl->AddCallback(ChAddOff, nullptr);
    else if (blend >= 2) dl->AddCallback(ChBlendOff, nullptr);
}

static void RenderCustomCrosshair()
{
    ImGuiIO& io = ImGui::GetIO();
    const float speed = CsGetPlayerState().speed2D;
    ChDrawCustomCrosshairCore(ImGui::GetBackgroundDrawList(), io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f, 1.f, speed);
}

// Menu preview: shrinks the crosshair to fit [min,max] the same way botox's draw_preview does
// (shrink until the widest arm fits the box), then draws through the same core as the live
// overlay -- so the preview is never allowed to drift from what actually renders in-game.
void CustomCrosshair_DrawPreview(ImDrawList* dl, ImVec2 min, ImVec2 max, float testSpeed)
{
    const float w = max.x - min.x, h = max.y - min.y;
    if (w < 4.f || h < 4.f) return;

    const float asp         = g_chAspect > 0.01f ? g_chAspect : 0.01f;
    const float longerScale = asp >= 1.f ? asp : 1.f / asp;
    const float extentPx    = fmaxf(0.f, floorf(g_chGap * kChUnitToPx)) + floorf(g_chLength * kChUnitToPx * longerScale) +
                               floorf(g_chThickness * kChUnitToPx * 0.5f);
    const float maxExtent = (w < h ? w : h) * 0.45f;
    const float scale     = (extentPx > maxExtent && extentPx > 0.f) ? maxExtent / extentPx : 1.f;

    dl->PushClipRect(min, max, true);
    ChDrawCustomCrosshairCore(dl, min.x + w * 0.5f, min.y + h * 0.5f, scale, testSpeed);
    dl->PopClipRect();
}

// Animated preview background (botox custom_crosshair.cpp draw_preview_scene): iq gradient
// noise shaded into a soft rotating blob, purely cosmetic -- lets the colour/blend pickers be
// judged against a moving, non-uniform background instead of the menu's flat one.
void CustomCrosshair_DrawPreviewScene(ImDrawList* dl, ImVec2 p0, float side)
{
    static float s_t = 0.f;
    s_t += ImMin(ImGui::GetIO().DeltaTime, 0.1f);

    auto fract = [](double x) { return (float)(x - floor(x)); };
    auto smooth = [](float a, float b, float x) {
        const float t = ImClamp((x - a) / (b - a), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    auto noise = [&](float px, float py) {
        const float ix = floorf(px), iy = floorf(py), fx = px - ix, fy = py - iy;
        const float ux = fx * fx * (3.f - 2.f * fx), uy = fy * fy * (3.f - 2.f * fy);
        auto g = [&](float ox, float oy) {
            const double hx = (ix + ox) * 2127.1 + (iy + oy) * 81.17;
            const double hy = (ix + ox) * 1269.5 + (iy + oy) * 283.37;
            return (fract(sin(hx) * 43758.5453) * 2.f - 1.f) * (fx - ox) + (fract(sin(hy) * 43758.5453) * 2.f - 1.f) * (fy - oy);
        };
        const float n = ImLerp(ImLerp(g(0.f, 0.f), g(1.f, 0.f), ux), ImLerp(g(0.f, 1.f), g(1.f, 1.f), ux), uy);
        return 0.5f + 0.5f * n;
    };

    const float c5 = cosf(-5.f * IM_PI / 180.f), s5 = sinf(-5.f * IM_PI / 180.f);
    auto shade = [&](float u, float v) {
        float x = u - 0.5f, y = v - 0.5f;
        const float deg = noise(s_t * 0.1f, x * y);
        const float a = ((deg - 0.5f) * 720.f + 180.f) * IM_PI / 180.f;
        const float c = cosf(a), s = sinf(a);
        const float rx = x * c - y * s, ry = x * s + y * c;
        x = rx; y = ry;

        const float speed = s_t * 2.f;
        x += sinf(y * 5.f + speed) / 30.f;
        y += sinf(x * 7.5f + speed) / 15.f;

        const float k = smooth(-0.3f, 0.2f, x * c5 - y * s5);
        const float m = smooth(0.5f, -0.3f, y);
        const float l1[3] = { ImLerp(0.800f, 0.550f, k), ImLerp(0.660f, 0.700f, k), ImLerp(0.470f, 0.820f, k) };
        const float l2[3] = { ImLerp(0.660f, 0.330f, k), ImLerp(0.250f, 0.450f, k), ImLerp(0.220f, 0.550f, k) };
        return IM_COL32((int)(ImLerp(l1[0], l2[0], m) * 255.f), (int)(ImLerp(l1[1], l2[1], m) * 255.f),
                        (int)(ImLerp(l1[2], l2[2], m) * 255.f), 255);
    };

    constexpr int n = 32;
    static ImU32 col[n + 1][n + 1];
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            col[j][i] = shade((float)i / n, 1.f - (float)j / n);

    const float cell = side / n;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            dl->AddRectFilledMultiColor(ImVec2(p0.x + i * cell, p0.y + j * cell), ImVec2(p0.x + (i + 1) * cell, p0.y + (j + 1) * cell),
                                        col[j][i], col[j][i + 1], col[j + 1][i + 1], col[j + 1][i]);
}

// ==== CSGO default crosshair (styles 0 / 1) ==========================================
// cl_crosshairstyle 0/1 is NOT the vgui bar crosshair -- CHudWeapon::ShouldDraw returns
// false below style 2 (hud_weapon.cpp:73). It is a panorama panel tree,
// panorama/layout/hud/crosshair.xml + styles/crosshair.css (both live in
// csgo/panorama/code.pbin), positioned by CCSGO_HudReticle::UpdateCrosshairUI
// (csgo_hudreticle.cpp:894-1010). Five parts, in xml document order:
//
//   green pip x4  crosshairpip2.png  5x12   additive, wash = crosshairColor1
//   arc x4        bare 9x1 / 1x9 panel     additive, background = crosshairColor1
//   black pip x4  reticlepip.png     1x6    normal,   wash = #000000AA
//   black circle  reticlecircle.png  8x8 -> 7x7 stretch, normal, wash = #00000088
//   dot           1x1 panel, additive, background-color = crosshairColor1
//
// The dot's own <Image> adds nothing: it is reticlecircle.png (rgb 0,0,0) under
// wash-color-fast, and wash-color-fast is a plain multiply into the quad colour
// (source2surface.cpp:2091 m_multColor), so black art x any wash = black, and black
// added to the frame is a no-op. All the dot's green is the panel's background-color,
// which is opaque -- it is a hard, full-strength pixel, not the 96/255 the art averages to.
//
// The Crosshair panel runs SetUIScale(1/windowScale), so every size above is in real
// screen pixels at any resolution; only the gap/spread offsets are res-scaled.
// The panel itself carries transform: translate3d(0.5px, 0.5px, 0), which is what puts
// the odd-width parts on whole pixels.
static constexpr float kCsPipOffset      = 7.0f;   // @define cppPipDefaultOffset
static constexpr float kCsBlackPipOffset = 2.0f;   // @define cppBlackPipDefaultOffset
static constexpr float kCsArcOffset      = 4.0f;   // @define cppArcDefaultOffset
static constexpr float kCsArcSize        = 9.0f;   // @define arcSize
static constexpr float kCsFixedGap       = 3.0f;   // cl_fixedcrosshairgap default

// The art of materials/panorama/images/hud/reticle/, as it lands on screen -- NOT as it
// sits in the png. The .crosshair panel is shifted by translate3d(0.5px, 0.5px, 0), so a
// part whose panel is an ODD number of pixels along an axis lands on whole pixels, and a
// part whose panel is EVEN lands on a half pixel and the bilinear sampler splits every
// texel across two screen pixels. The pips are 12 and 6 long -- even -- so csgo's own
// crosshair shows them box-filtered down their length: one extra faded pixel at each end
// and a softer ramp into the tip. That IS the taper. Baking it in here (12 -> 13 rows,
// 6 -> 7) means point-sampling at scale > 1 blows up what csgo actually puts on screen
// instead of the raw art. A symmetric 2-tap filter commutes with the 180 degree flip, so
// one baked sprite still serves all four arms.
// RGB is kept as shipped (the faint texels are not pure white) and the sampler decodes it
// as sRGB, like TEXTURE_BINDFLAGS_SRGBREAD does in panorama_cshader.cpp:162.
static const unsigned char kCsPip[13][5][4] = {    // crosshairpip2.png, 5 x 12 -> 5 x 13
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255, 23}, {255,255,255,128}, {255,255,255, 23}, {255,255,255,  0} },
    { {255,255,255,  0}, {247,247,247, 42}, {255,255,255,255}, {247,247,247, 42}, {255,255,255,  0} },
    { {255,255,255,  0}, {231,231,231, 34}, {255,255,255,255}, {231,231,231, 34}, {255,255,255,  0} },
    { {255,255,255,  0}, {240,240,240, 14}, {255,255,255,255}, {240,240,240, 14}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,255}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,255}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {247,247,247,206}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {232,232,232,127}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {241,241,241, 48}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
};
static const unsigned char kCsBlackPip[7][1][4] = {   // reticlepip.png, 1 x 6 -> 1 x 7
    { {255,255,255,117} },
    { {255,255,255,213} },
    { {255,255,255,170} },
    { {255,255,255,128} },
    { {255,255,255, 86} },
    { {255,255,255, 42} },
    { {255,255,255, 10} },
};
// reticlecircle.png is 8 x 8 in a 7 x 7 panel with scaling="stretch": odd, so it lands on
// whole pixels, and the 8 -> 7 squeeze is the sampler's, baked here for the same reason.
static const unsigned char kCsCircle[7][7][4] = {
    { {  0,  0,  0,  5}, {  0,  0,  0, 70}, {  0,  0,  0,185}, {  0,  0,  0,218}, {  0,  0,  0,184}, {  0,  0,  0, 70}, {  0,  0,  0,  5} },
    { {  0,  0,  0, 70}, {  0,  0,  0,200}, {  0,  0,  0, 97}, {  0,  0,  0, 48}, {  0,  0,  0, 98}, {  0,  0,  0,200}, {  0,  0,  0, 69} },
    { {  0,  0,  0,185}, {  0,  0,  0, 98}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,100}, {  0,  0,  0,184} },
    { {  0,  0,  0,218}, {  0,  0,  0, 50}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0, 50}, {  0,  0,  0,218} },
    { {  0,  0,  0,185}, {  0,  0,  0,100}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,101}, {  0,  0,  0,183} },
    { {  0,  0,  0, 70}, {  0,  0,  0,200}, {  0,  0,  0, 99}, {  0,  0,  0, 49}, {  0,  0,  0,100}, {  0,  0,  0,200}, {  0,  0,  0, 68} },
    { {  0,  0,  0,  5}, {  0,  0,  0, 69}, {  0,  0,  0,184}, {  0,  0,  0,217}, {  0,  0,  0,184}, {  0,  0,  0, 69}, {  0,  0,  0,  5} },
};
static constexpr float kCsPipOpacity   = 0.99f;    // .crosshair__pip--green opacity
static constexpr float kCsBlackPipWash = 170.0f;   // wash-color-fast #000000AA
static constexpr float kCsCircleWash   = 136.0f;   // wash-color-fast #00000088

// AK-47 (weapon_ak47_prefab, items_game.txt:24892), the CS:GO schema numbers
// CWeaponCSBase feeds GetReticleWeaponSpread there (csgo cs_weapon_parse.cpp:1052-1060).
// CS:S's own CWeaponCSBase::DrawCrosshair (weapon_csbase.cpp:879) uses a different, simpler
// model (CrosshairMinDistance/CrosshairDeltaDistance, cs_weapon_parse.cpp:281-282) that this
// botox-style reticle does not read -- this is a straight csgo visual port (botox 1:1), not
// tied to whatever weapon is out, so the arcs ride these fixed numbers. The movement STATE
// driving them is still the local player's, live.
static constexpr float kCsInaccStand       = 0.00641f;
static constexpr float kCsInaccCrouch      = 0.00481f;
static constexpr float kCsInaccMove        = 0.17506f;
static constexpr float kCsInaccJump        = 0.14076f;
static constexpr float kCsInaccJumpInitial = 0.10094f;
static constexpr float kCsInaccLand        = 0.000242f;
static constexpr float kCsSpread           = 0.00060f;
static constexpr float kCsRecoveryStand    = 0.368f;
static constexpr float kCsRecoveryCrouch   = 0.305257f;
static constexpr float kCsDuckSpeedMod     = 0.34f;      // CS_PLAYER_SPEED_DUCK_MODIFIER
static constexpr float kCsMoveCurveExp     = 0.25f;      // MOVEMENT_CURVE01_EXPONENT
static constexpr float kCsJumpImpulse      = 301.993377f;// sv_jump_impulse

static float CsRemapClamped(float v, float a, float b, float ra, float rb)
{
    if (a == b) return v >= b ? rb : ra;
    float t = (v - a) / (b - a);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return ra + (rb - ra) * t;
}

// Exponential ease toward a new value. csgo snaps: the gap jumps between four fixed
// steps and the spread lands wherever the frame's inaccuracy puts it. tau > 0 spreads
// that jump over time instead -- pure display smoothing, nothing here feeds the game.
static float ChCsEase(float& cur, float target, float dt, float tau)
{
    if (tau <= 0.001f || dt <= 0.f) { cur = target; return cur; }
    float a = 1.f - expf(-dt / tau);
    if (a > 1.f) a = 1.f;
    cur += (target - cur) * a;
    return cur;
}

// csgo CWeaponCSBase::UpdateAccuracyPenalty (:3810) + GetInaccuracy (:1258), minus the
// firing/reload/ladder/turning terms (not read here -- see CsUpdateInaccuracy callers;
// this stays a movement-driven visual approximation, not a live weapon-state read, same
// on CS:S as it was on TF2). Runs off frame time, not
// TICK_INTERVAL, since it is only ever a display value here.
// velSens scales every term that rides the player's motion -- the landing bank, the air
// penalty, the speed curve and the takeoff/apex ramp. 1 = csgo, 0 = a still reticle.
static float CsUpdateInaccuracy(const CsPlayerState& st, float dt, float velSens)
{
    static float s_penalty  = 0.f;
    static bool  s_wasGround = true;
    static float s_lastVelZ  = 0.f;

    // OnLand (:3941): the fall speed is banked into the penalty the frame we touch down.
    if (st.onGround && !s_wasGround)
        s_penalty += kCsInaccLand * fabsf(s_lastVelZ) * velSens;
    s_wasGround = st.onGround;
    s_lastVelZ  = st.velZ;

    float target = 0.f;
    if (!st.onGround)    target = kCsInaccStand + kCsInaccJump * velSens;  // weapon_air_spread_scale 1
    else if (st.ducking) target = kCsInaccCrouch;
    else                 target = kCsInaccStand;

    if (target > s_penalty)
    {
        s_penalty = target;                                     // penalties snap on
    }
    else
    {
        const float recovery = !st.onGround ? kCsRecoveryCrouch * 4.0f     // 400% air penalty
                             : st.ducking   ? kCsRecoveryCrouch
                                            : kCsRecoveryStand;
        const float decay = logf(10.0f) / recovery;
        const float t = expf(dt * -decay);                       // Lerp(t, target, penalty)
        s_penalty = target + (s_penalty - target) * t;
    }

    float acc = s_penalty;

    // Instantaneous movement term -- MOVEMENT_ACCURACY_DECAYED is 0, so this one does
    // NOT persist: it tracks speed frame by frame and vanishes the moment you stop.
    const float moveScale = CsRemapClamped(st.speed2D,
                                           st.maxSpeed * kCsDuckSpeedMod,
                                           st.maxSpeed * 0.95f, 0.f, 1.f);
    if (moveScale > 0.f)
        acc += powf(moveScale, kCsMoveCurveExp) * kCsInaccMove * velSens;

    // Airborne: worst at takeoff/landing speed, best at the apex, capped at 2x.
    if (!st.onGround)
    {
        const float sqMax = sqrtf(kCsJumpImpulse);
        float air = CsRemapClamped(sqrtf(fabsf(st.velZ)), sqMax * 0.25f, sqMax,
                                   0.f, kCsInaccJumpInitial);
        if (air > 2.0f * kCsInaccJumpInitial) air = 2.0f * kCsInaccJumpInitial;
        acc += air * velSens;
    }

    if (acc > 1.0f) acc = 1.0f;
    return acc;
}

// hudreticle paints pips, arcs and the dot with -s2-mix-blend-mode: SRGBadditive.
// SRCALPHA/ONE is that equation; ImGui_ImplDX9's own state is SRCALPHA/INVSRCALPHA.
// (panorama's own state is ONE/ONE over a premultiplied source -- same product, since
// ImGui's src is texel x vertex colour and its src alpha is texel a x vertex a.)
//
// Light: panorama composites in LINEAR light. panorama_cshader.cpp:53 turns on sRGB
// WRITE for the target, :162 binds the art SRGBREAD, and every css colour goes through
// SrgbGammaToLinear on the way to the vertex (source2surface.cpp:291). So the add and
// the black wash both happen on linear values and only the final store is re-encoded.
// Doing the same arithmetic straight on sRGB bytes -- which is what an ImGui overlay
// does by default -- crushes exactly the two things that read as wrong: a faint additive
// texel (the pip's taper, alpha 14..48) lands far dimmer than it should, and a black
// wash over a lit frame bites far harder, AND a bright colour clips to white where csgo
// keeps its hue. D3DRS_SRGBWRITEENABLE + linear vertex colours hands that back to the ROP,
// but only where ChSrgbBlendOk() says the ROP will take it -- otherwise the encode is
// undone (colours) or absorbed into the alpha (washes) rather than shipped blind.
//
// Filter: at scale 1 the art lands texel-on-pixel and point == linear. POINT keeps the
// pip's flare and the ring as hard shapes at any zoom -- the look of a low-res capture
// scaled up. LINEAR is the soft version, and it no longer mushes, because the enlarging
// is baked into the atlas (CsWantAtlasN) and the sampler is left a ratio <= 1.
// ImGui's own draws need LINEAR back, so ChCsEnd restores it.
static void ChCsSetFilter(DWORD f)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetSamplerState(0, D3DSAMP_MINFILTER, f);
    g_d3dDev->SetSamplerState(0, D3DSAMP_MAGFILTER, f);
}
// g_chCsgoLinear is only a REQUEST -- ChSrgbBlendOk decides whether the ROP can serve it.
// Asking for it on a target that refuses the sRGB write used to leave the linear-encoded
// vertex colours blending in gamma space: crosshair too dark, black washes ~30% too heavy.
static inline bool ChCsLinear() { return g_chCsgoLinear && ChSrgbBlendOk(); }

static void ChCsSetState(DWORD destBlend)
{
    if (!g_d3dDev) return;
    const BOOL lin = ChCsLinear() ? TRUE : FALSE;
    g_d3dDev->SetRenderState(D3DRS_DESTBLEND, destBlend);
    g_d3dDev->SetRenderState(D3DRS_SRGBWRITEENABLE, lin);
    g_d3dDev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, lin);
    ChCsSetFilter(g_chCsgoCrisp ? D3DTEXF_POINT : D3DTEXF_LINEAR);
}
static void ChCsBlendAdd(const ImDrawList*, const ImDrawCmd*)
{
    ChCsSetState(D3DBLEND_ONE);
}
static void ChCsBlendNormal(const ImDrawList*, const ImDrawCmd*)
{
    ChCsSetState(D3DBLEND_INVSRCALPHA);
}
static void ChCsEnd(const ImDrawList*, const ImDrawCmd*)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_d3dDev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    g_d3dDev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    ChCsSetFilter(D3DTEXF_LINEAR);
}

// The parts csgo paints -s2-mix-blend-mode: SRGBadditive. An add cannot make anything
// darker, so a near-white crosshair colour pins every channel and comes out white no
// matter which light it is added in -- that is what additive MEANS, and csgo does the
// same with its own #82b116. Turning this off blends them normally, which is the only
// way a colour that bright lands on screen as the colour that was picked.
static void ChCsBlendLit(ImDrawList* dl)
{
    dl->AddCallback(g_chCsgoAdditive ? ChCsBlendAdd : ChCsBlendNormal, nullptr);
}

// The two black washes, as an 8-bit coverage. g_chCsgoBlack scales them (1 = csgo).
//
// A wash of PURE BLACK is a plain scale on the destination, and a scale commutes with the
// gamma curve exactly: dst*(1-a) in linear light is the same picture as dst*(1-a)^(1/2.2)
// written straight to sRGB bytes. So when linear light was asked for but the ROP cannot
// deliver it, the wash is reproduced by softening the alpha instead -- 170 -> 100,
// 136 -> 76 -- rather than letting it bite a third harder than csgo's does.
static float ChCsWashAlpha(float a255)
{
    float k = g_chCsgoBlack;
    if (k < 0.f) k = 0.f;
    if (k > 1.f) k = 1.f;
    float a = a255 * k / 255.f;
    if (a > 1.f) a = 1.f;
    if (g_chCsgoLinear && !ChSrgbBlendOk())
        a = 1.f - powf(1.f - a, 1.f / 2.2f);
    return a * 255.f;
}

// One texel of a part, at 1:1 scale. dir 0=top,1=right,2=bottom,3=left; the Image
// panels carry orientation 0/90/180/270, i.e. the art rotated clockwise about the
// panel, which swaps the panel's own width/height on the horizontal pair.
static void ChCsTexel(ImDrawList* dl, float ox, float oy, int lx, int ly, ImU32 col, float s)
{
    dl->AddRectFilled(ImVec2(ox + lx * s, oy + ly * s),
                      ImVec2(ox + (lx + 1) * s, oy + (ly + 1) * s), col);
}

// Fallback for a device that would not give us the atlas: one hard rect per baked texel,
// tinted the same way the sampler would. dir 0=top,1=right,2=bottom,3=left; the Image
// panels carry orientation 0/90/180/270, i.e. the art rotated clockwise about the panel,
// which swaps the panel's own width/height on the horizontal pair.
static void ChCsDrawTexels(ImDrawList* dl, const unsigned char* art, int W, int H,
                           float cx, float cy, float off, int dir,
                           int r, int g, int b, float alphaMul, float s)
{
    const bool  horiz = (dir & 1) != 0;
    const float pcx = (dir == 1) ? cx + off : (dir == 3) ? cx - off : cx;
    const float pcy = (dir == 0) ? cy - off : (dir == 2) ? cy + off : cy;
    const float ox = pcx - (horiz ? H : W) * 0.5f * s;
    const float oy = pcy - (horiz ? W : H) * 0.5f * s;

    for (int ty = 0; ty < H; ++ty)
        for (int tx = 0; tx < W; ++tx)
        {
            const unsigned char* t = art + (ty * W + tx) * 4;
            const int a = (int)(t[3] * alphaMul + 0.5f);
            if (a <= 0) continue;
            int lx, ly;
            switch (dir) {
                case 1:  lx = H-1-ty; ly = tx;     break;
                case 2:  lx = W-1-tx; ly = H-1-ty; break;
                case 3:  lx = ty;     ly = W-1-tx; break;
                default: lx = tx;     ly = ty;     break;
            }
            ChCsTexel(dl, ox, oy, lx, ly,
                      IM_COL32(r * t[0] / 255, g * t[1] / 255, b * t[2] / 255, a), s);
        }
}

// ---- the art as real textures ------------------------------------------------------
// One quad per part, sampled, instead of a rect per texel: at scale 1 the baked art is
// already what the screen shows. Above it the ATLAS does the enlarging (see CsWantAtlasN)
// and the sampler only finishes the fraction -- POINT for the look of an upscaled
// capture, LINEAR for a smooth one that still keeps its shape.
// All three live in one power-of-two atlas (32x32 at prescale 1, up to 128x128 at 8):
// the D3D9 device turned down the 5x13 and 1x7 art as separate textures ("texture create
// failed" in the log), which is the non-pow2 conditional support DX9 hardware is allowed
// to refuse (same D3D9Ex device class as TF2 x64's). Each sprite carries a
// 2px border of its own edge texels so the bilinear tap at a sprite edge sees what
// D3DTADDRESS_CLAMP would have shown it, not the neighbouring sprite.
struct CsSprite { int x, y, w, h; };
static constexpr int kCsSpritePad = 2;
static constexpr int kCsAtlasNMax = 8;
// The three source sprites, in art texels (post-bake): pip 5x13, circle 7x7, black pip 1x7.
static CsSprite s_spPip      { 2, 2, 5, 13 };   // crosshairpip2.png, baked
static CsSprite s_spCircle   { 12, 2, 7,  7 };  // reticlecircle.png, baked
static CsSprite s_spBlackPip { 24, 2, 1,  7 };  // reticlepip.png, baked

static IDirect3DTexture9* s_csAtlas    = nullptr;
static IDirect3DDevice9*  s_csTexDev   = nullptr;
static bool               s_csTexTried = false;
static int                s_csAtlasDim = 32;
static int                s_csAtlasN   = 0;     // art prescale baked into the atlas

// ---- prescale ----------------------------------------------------------------------
// LINEAR at scale > 1 smears ONE texel across `scale` screen pixels, so the pip's
// 234 -> 21 taper turns into a plain gradient and the ring's hole closes up -- the shape
// is gone, which is exactly what "blurry when zoomed" means. The blur is not the filter's
// fault, it is the RATIO: a bilinear tap always ramps over one SOURCE texel, however many
// pixels that texel now covers.
//
// So do the enlarging where the art is built instead. The atlas is baked at an integer
// factor N with a SHARP bilinear -- an ordinary bilinear whose blend window is squeezed to
// 1/N of a source texel, i.e. one screen pixel -- so every edge stays where it was and
// gets ONE pixel of ramp rather than `scale` pixels of it. N = ceil(scale) leaves the
// sampler a ratio <= 1, so LINEAR only cleans up the fraction.
// crisp wants the raw art under a POINT tap, so it pins N to 1 and nothing changes there.
static int CsWantAtlasN()
{
    if (g_chCsgoCrisp) return 1;
    const float s = (g_chCsgoScale > 0.05f) ? g_chCsgoScale : 1.f;
    int n = (int)ceilf(s - 0.001f);
    if (n < 1) n = 1;
    if (n > kCsAtlasNMax) n = kCsAtlasNMax;
    return n;
}

// Blend weight for one axis. f = distance past the lower source texel's centre; the ramp
// is centred on the texel boundary and 1/N wide, so at N = 1 it collapses to 0 or 1 and
// the bake is a straight copy of the art.
static inline float CsSharpW(float f, int N)
{
    const float w = (f - 0.5f) * (float)N + 0.5f;
    return (w < 0.f) ? 0.f : (w > 1.f ? 1.f : w);
}

// One sprite, resampled to N x. RGB is kept as shipped (the faint texels are not pure
// white), and the four taps are mixed PREMULTIPLIED so a covered texel can never pull rgb
// out of a fully transparent neighbour. Source reads clamp, which is also what fills the
// 2px border: the bilinear tap at a sprite edge sees what D3DTADDRESS_CLAMP would show.
static void CsBlitSprite(unsigned char* bits, int pitch, const CsSprite& sp,
                         const unsigned char* rgba, int srcW, int srcH, int N)
{
    for (int y = -kCsSpritePad; y < sp.h + kCsSpritePad; ++y)
    {
        unsigned int* row = (unsigned int*)(bits + (sp.y + y) * pitch);
        const float  fy = (y + 0.5f) / (float)N - 0.5f;
        const int    y0 = (int)floorf(fy);
        const float  wy = CsSharpW(fy - (float)y0, N);

        for (int x = -kCsSpritePad; x < sp.w + kCsSpritePad; ++x)
        {
            const float fx = (x + 0.5f) / (float)N - 0.5f;
            const int   x0 = (int)floorf(fx);
            const float wx = CsSharpW(fx - (float)x0, N);

            float acc[4] = { 0.f, 0.f, 0.f, 0.f };
            for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
            {
                int sx = x0 + i, sy = y0 + j;
                if (sx < 0) sx = 0; else if (sx >= srcW) sx = srcW - 1;
                if (sy < 0) sy = 0; else if (sy >= srcH) sy = srcH - 1;
                const unsigned char* t = rgba + (sy * srcW + sx) * 4;
                const float w = (i ? wx : 1.f - wx) * (j ? wy : 1.f - wy);
                if (w <= 0.f) continue;
                const float a = t[3] * (1.f / 255.f);
                acc[0] += w * t[0] * a;
                acc[1] += w * t[1] * a;
                acc[2] += w * t[2] * a;
                acc[3] += w * a;
            }

            unsigned int px = 0;
            if (acc[3] > 0.0001f)
            {
                const float inv = 1.f / acc[3];
                auto ch = [](float v) -> unsigned int {
                    int c = (int)(v + 0.5f);
                    if (c < 0) c = 0; else if (c > 255) c = 255;
                    return (unsigned int)c;
                };
                px = (ch(acc[3] * 255.f) << 24) | (ch(acc[0] * inv) << 16)
                   | (ch(acc[1] * inv) << 8)    |  ch(acc[2] * inv);
            }
            row[sp.x + x] = px;
        }
    }
}

// Place the three sprites for a given prescale and return the pow2 atlas edge they need.
static int CsLayoutAtlas(int N)
{
    const int p = kCsSpritePad;
    int x = p;
    s_spPip      = { x, p, 5 * N, 13 * N }; x += 5 * N + p * 2;
    s_spCircle   = { x, p, 7 * N,  7 * N }; x += 7 * N + p * 2;
    s_spBlackPip = { x, p, 1 * N,  7 * N }; x += 1 * N + p;

    int need = 13 * N + p * 2;              // the tallest sprite plus both borders
    if (x > need) need = x;
    int dim = 32;
    while (dim < need) dim <<= 1;
    return dim;
}

void CsgoCrosshair_ReleaseTextures()
{
    ChBlendRelease();   // blend-mode shaders + DEFAULT-pool frame copy ride the same Reset/eject path
    if (s_csAtlas) { s_csAtlas->Release(); s_csAtlas = nullptr; }
    s_csTexDev   = nullptr;
    s_csTexTried = false;
    s_csAtlasN   = 0;
}

static bool CsEnsureTextures()
{
    if (!g_d3dDev) return false;
    // A scale (or crisp) change rebakes the art -- the atlas IS the enlarging now. Done
    // before any AddImage of this frame, so no draw list can hold the dropped texture.
    const int wantN = CsWantAtlasN();
    if (s_csTexDev != g_d3dDev || (s_csTexTried && wantN != s_csAtlasN))
    {
        CsgoCrosshair_ReleaseTextures();
        s_csTexDev = g_d3dDev;
    }
    if (s_csTexTried) return s_csAtlas != nullptr;

    s_csTexTried = true;
    s_csTexDev   = g_d3dDev;
    s_csAtlasN   = wantN;                   // set before any early-out: no rebake spin
    s_csAtlasDim = CsLayoutAtlas(wantN);
    const int kCsAtlasDim = s_csAtlasDim;

    // MANAGED survives a device Reset; DEFAULT+DYNAMIC is the fallback for wrappers that
    // refuse MANAGED, and h_Reset drops the atlas so that one gets rebuilt after a Reset.
    DWORD usage = 0;
    HRESULT hr = g_d3dDev->CreateTexture(kCsAtlasDim, kCsAtlasDim, 1, 0, D3DFMT_A8R8G8B8,
                                         D3DPOOL_MANAGED, &s_csAtlas, nullptr);
    if (FAILED(hr) || !s_csAtlas)
    {
        s_csAtlas = nullptr;
        usage = D3DUSAGE_DYNAMIC;
        hr = g_d3dDev->CreateTexture(kCsAtlasDim, kCsAtlasDim, 1, usage, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, &s_csAtlas, nullptr);
    }
    if (FAILED(hr) || !s_csAtlas)
    {
        s_csAtlas = nullptr;
        return false;
    }

    D3DLOCKED_RECT lr{};
    hr = s_csAtlas->LockRect(0, &lr, nullptr, (usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0);
    if (FAILED(hr))
    {
        s_csAtlas->Release(); s_csAtlas = nullptr;
        return false;
    }

    unsigned char* bits = (unsigned char*)lr.pBits;
    for (int y = 0; y < kCsAtlasDim; ++y)
        memset(bits + y * lr.Pitch, 0, kCsAtlasDim * 4);
    CsBlitSprite(bits, lr.Pitch, s_spPip,      &kCsPip[0][0][0],      5, 13, wantN);
    CsBlitSprite(bits, lr.Pitch, s_spCircle,   &kCsCircle[0][0][0],   7,  7, wantN);
    CsBlitSprite(bits, lr.Pitch, s_spBlackPip, &kCsBlackPip[0][0][0], 1,  7, wantN);
    s_csAtlas->UnlockRect(0);
    return true;
}

// One Image panel. orient = the xml orientation attribute: 0/90/180/270, the art
// turned clockwise inside the panel, so the panel's own w/h are already swapped on
// the horizontal pair.
static void CsDrawImage(ImDrawList* dl, const CsSprite& sp, float pcx, float pcy,
                        float w, float h, int orient, ImU32 col)
{
    if (!s_csAtlas) return;
    const float x0 = pcx - w * 0.5f, y0 = pcy - h * 0.5f;
    const float x1 = pcx + w * 0.5f, y1 = pcy + h * 0.5f;
    const ImVec2 p1(x0, y0), p2(x1, y0), p3(x1, y1), p4(x0, y1);

    const float u0 = sp.x / (float)s_csAtlasDim,          v0 = sp.y / (float)s_csAtlasDim;
    const float u1 = (sp.x + sp.w) / (float)s_csAtlasDim, v1 = (sp.y + sp.h) / (float)s_csAtlasDim;
    const ImVec2 A(u0, v0), B(u1, v0), C(u1, v1), D(u0, v1);
    const ImTextureID id = (ImTextureID)s_csAtlas;

    switch (orient) {
        case 1:  dl->AddImageQuad(id, p1, p2, p3, p4, D, A, B, C, col); break;   // 90
        case 2:  dl->AddImageQuad(id, p1, p2, p3, p4, C, D, A, B, col); break;   // 180
        case 3:  dl->AddImageQuad(id, p1, p2, p3, p4, B, C, D, A, col); break;   // 270
        default: dl->AddImageQuad(id, p1, p2, p3, p4, A, B, C, D, col); break;   // 0
    }
}

// Part centre for one arm. dir 0=top,1=right,2=bottom,3=left.
static inline ImVec2 CsPartCenter(float cx, float cy, float off, int dir)
{
    return ImVec2((dir == 1) ? cx + off : (dir == 3) ? cx - off : cx,
                  (dir == 0) ? cy - off : (dir == 2) ? cy + off : cy);
}

static void RenderCsgoPanorama(bool bStatic)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    const int scrH = (int)io.DisplaySize.y;
    if (scrH <= 0) return;

    const CsPlayerState st = CsGetPlayerState();

    float dt = io.DeltaTime;
    if (dt < 0.f)     dt = 0.f;
    if (dt > 0.25f)   dt = 0.25f;

    const float velSens = (g_chCsgoVelSens < 0.f) ? 0.f : g_chCsgoVelSens;
    const float tau     = (g_chCsgoSmooth  < 0.f) ? 0.f : g_chCsgoSmooth;

    // GetReticleCrosshairGap (weapon_csbase.cpp:1912). Style 1 ignores it and parks the
    // pips on cl_fixedcrosshairgap instead (csgo_hudreticle.cpp:1004).
    float gap;
    if (bStatic)
    {
        gap = kCsFixedGap;
    }
    else
    {
        float gapUnits = 10.f;
        if (st.valid)
        {
            if (!st.onGround)     gapUnits = 30.f;
            else if (st.ducking)  gapUnits = 3.f;
            else if (sqrtf(st.speed2D*st.speed2D + st.velZ*st.velZ) > 100.f) gapUnits = 20.f;
        }
        // 10 is the standing step. velSens rescales only how far the other three travel
        // from it, so 0 parks the arms where a standing player sees them and 1 is csgo.
        gapUnits = 10.f + (gapUnits - 10.f) * velSens;
        gap = floorf(scrH * gapUnits / 768.f);   // floor == the original's integer divide
    }
    gap += g_chCsgoGap;

    // csgo cuts straight to the new step; tau eases the four steps into each other.
    static float s_gapSm = 0.f;
    gap = ChCsEase(s_gapSm, gap, dt, tau);

    // GetReticleWeaponSpread (:1890). FOV comes off fov_desired so a zoom moves the arcs
    // the same way GetFOV() does in csgo.
    float arcOff = 0.f;
    bool  drawArcs = !bStatic && g_chCsgoArcs;
    if (drawArcs)
    {
        static ConVar* s_fov = nullptr;
        if (!s_fov && g_cvar) s_fov = g_cvar->FindVarWalk("fov_desired");
        float fov = s_fov ? s_fov->GetFloat() : 90.f;
        if (fov < 1.f || fov > 179.f) fov = 90.f;

        const float inacc = st.valid ? CsUpdateInaccuracy(st, dt, velSens) : 0.f;

        const float halfFov = fov * 0.5f * 3.14159265f / 180.f;
        const float spreadDist = (inacc + kCsSpread) * 320.0f / tanf(halfFov);
        arcOff = floorf(spreadDist * scrH / 480.0f + 0.5f) + kCsArcOffset;

        static float s_arcSm = 0.f;
        arcOff = ChCsEase(s_arcSm, arcOff, dt, tau);
    }

    // .crosshair carries translate3d(0.5px, 0.5px, 0) on top of a full-screen panel.
    const float cx = floorf(io.DisplaySize.x * 0.5f) + 0.5f;
    const float cy = floorf(io.DisplaySize.y * 0.5f) + 0.5f;

    // Every css colour reaches the vertex as linear light, so when we are blending in
    // linear light the wash has to be decoded the same way. Black decodes to black, so
    // the two black washes below need no branch.
    const bool  lin = ChCsLinear();
    const int   r = lin ? ChCsToLinear8(g_chCsgoColor[0]) : (int)(g_chCsgoColor[0] * 255.f);
    const int   g = lin ? ChCsToLinear8(g_chCsgoColor[1]) : (int)(g_chCsgoColor[1] * 255.f);
    const int   b = lin ? ChCsToLinear8(g_chCsgoColor[2]) : (int)(g_chCsgoColor[2] * 255.f);
    const float aMul = g_chCsgoColor[3];
    const ImU32 litCol = IM_COL32(r, g, b, (int)(255.f * aMul + 0.5f));
    const float pipAlpha = aMul * kCsPipOpacity;
    const ImU32 pipCol = IM_COL32(r, g, b, (int)(255.f * pipAlpha + 0.5f));

    // csgo draws this at 1:1 screen pixels at any resolution (SetUIScale undoes the
    // window scale), so it is tiny by design. s blows the whole reticle up around its
    // centre -- art, gap and spread together -- the same way an upscaled screenshot does.
    const bool  bTex = CsEnsureTextures();
    const float s    = (g_chCsgoScale > 0.05f) ? g_chCsgoScale : 1.f;
    const float pipOff   = (gap + kCsPipOffset) * s;
    const float blackOff = (gap + kCsBlackPipOffset) * s;
    // Arm aspect. The pip art is 5x13 and the 13 is the arm's LENGTH, so the stretch
    // goes on that axis only. Growing it from the pip's centre would eat the gap, so
    // the centre walks out by half the added length and the inner edge stays put.
    const float cAsp  = g_chCsgoAspect > 0.01f ? g_chCsgoAspect : 0.01f;
    const float pipHM = (cAsp >= 1.f) ? cAsp : 1.f;             // horizontal pair
    const float pipVM = (cAsp <= 1.f) ? (1.f / cAsp) : 1.f;     // vertical pair
    // Gap aspect pushes the whole offset -- green pip and its black stub together, or
    // the stub is left sitting in the middle of a gap the arm has walked out of.
    const float gAsp  = g_chCsgoGapAspect > 0.01f ? g_chCsgoGapAspect : 0.01f;
    const float gapHM = (gAsp >= 1.f) ? gAsp : 1.f;
    const float gapVM = (gAsp <= 1.f) ? (1.f / gAsp) : 1.f;
    const float arcOffS  = arcOff * s;
    const float blackPipA   = ChCsWashAlpha(kCsBlackPipWash);
    const float circleA     = ChCsWashAlpha(kCsCircleWash);
    const ImU32 blackPipCol = IM_COL32(0, 0, 0, (int)(blackPipA + 0.5f));
    const ImU32 circleCol   = IM_COL32(0, 0, 0, (int)(circleA   + 0.5f));

    ChCsBlendLit(dl);

    if (g_chCsgoPips)
    for (int i = 0; i < 4; ++i)
    {
        const bool  horiz = (i & 1) != 0;
        const float off   = pipOff * (horiz ? gapHM : gapVM);
        if (!bTex) { ChCsDrawTexels(dl, &kCsPip[0][0][0], 5, 13,
                                    cx, cy, off, i, r, g, b, pipAlpha, s); continue; }
        const float mul   = horiz ? pipHM : pipVM;
        const float pipL  = 13.f * mul * s;
        const ImVec2 c = CsPartCenter(cx, cy, off + (pipL - 13.f * s) * 0.5f, i);
        CsDrawImage(dl, s_spPip, c.x, c.y,
                    horiz ? pipL : 5.f * s, horiz ? 5.f * s : pipL, i, pipCol);
    }

    if (drawArcs)
    {
        const float h = kCsArcSize * 0.5f * s;
        const float t = 0.5f * s;                                       // 1px bar
        dl->AddRectFilled(ImVec2(cx - h, cy - arcOffS - t),
                          ImVec2(cx + h, cy - arcOffS + t), litCol);    // top
        dl->AddRectFilled(ImVec2(cx - h, cy + arcOffS - t),
                          ImVec2(cx + h, cy + arcOffS + t), litCol);    // bottom
        dl->AddRectFilled(ImVec2(cx - arcOffS - t, cy - h),
                          ImVec2(cx - arcOffS + t, cy + h), litCol);    // left
        dl->AddRectFilled(ImVec2(cx + arcOffS - t, cy - h),
                          ImVec2(cx + arcOffS + t, cy + h), litCol);    // right
    }

    dl->AddCallback(ChCsBlendNormal, nullptr);

    // the black pips are the arms' own shadow -- drop them with the arms, or four dark
    // nubs are left riding the gap with nothing on top of them.
    if (g_chCsgoPips)
    for (int i = 0; i < 4; ++i)
    {
        const bool  horiz = (i & 1) != 0;
        const float off   = blackOff * (horiz ? gapHM : gapVM);
        if (!bTex) { ChCsDrawTexels(dl, &kCsBlackPip[0][0][0], 1, 7,
                                    cx, cy, off, i, 0, 0, 0,
                                    blackPipA / 255.f, s); continue; }
        const ImVec2 c = CsPartCenter(cx, cy, off, i);
        CsDrawImage(dl, s_spBlackPip, c.x, c.y,
                    (horiz ? 7.f : 1.f) * s, (horiz ? 1.f : 7.f) * s, i, blackPipCol);
    }

    if (bTex)
        CsDrawImage(dl, s_spCircle, cx, cy, 7.f * s, 7.f * s, 0, circleCol);
    else
        ChCsDrawTexels(dl, &kCsCircle[0][0][0], 7, 7, cx, cy, 0.f, 0,
                       0, 0, 0, circleA / 255.f, s);

    // The dot is the panel's own background-color: opaque, additive, one pixel.
    ChCsBlendLit(dl);
    dl->AddRectFilled(ImVec2(cx - 0.5f * s, cy - 0.5f * s),
                      ImVec2(cx + 0.5f * s, cy + 0.5f * s), litCol);
    dl->AddCallback(ChCsEnd, nullptr);
}

// ==== CSGO classic crosshair (styles 2 / 3 / 4) =====================================
// CWeaponCSBase::DrawCrosshair (weapon_csbase.cpp:1969) + DrawCrosshairRect (:407).
// Nothing to do with the panorama reticle above: plain bars over a black outline rect,
// sized by cl_crosshairsize / cl_crosshairthickness / cl_crosshairgap. This is the
// crosshair in nearly every csgo clip.
//   2 = classic standard (spread-driven, splits past cl_crosshair_dynamic_splitdist)
//   3 = classic dynamic  (spread-driven, no split)
//   4 = classic static   (never moves)
// The one term not wired here is the per-shot bump (m_iShotsFired / its own
// CrosshairDeltaDistance, cs_weapon_parse.cpp:282) -- CS:S's CCSPlayer does carry
// m_iShotsFired for real (unlike TF2), but reading it needs a verified netvar offset
// from the SDK unit (aim/css.h); not guessed here. See report NEEDS FROM OTHERS.
// Until wired, the bars don't kick out while firing.
static constexpr float kCsXDistGoal = 4.0f;   // cl_crosshairgap_useweaponvalue 0

// vgui draws the outline with plain alpha and, when cl_crosshairusealpha is 0, the bar
// itself additively off the whiteAdditive texture -- so the blend flips per rect.
static void CsXRect(ImDrawList* dl, int r, int g, int b, int a,
                    float x0, float y0, float x1, float y1, bool bAdditive)
{
    if (a <= 0) return;
    if (g_csXOutline)
    {
        // csgo's outline is a vgui DrawFilledRect on INTEGER screen coords (:407), and
        // every bar coord here already comes out of int math -- the thickness was the
        // one float in the path, so a 0.5 or 1.5 put the outline edge on a half pixel
        // and imgui feathered it into a grey haze instead of a hard black band. Whole
        // pixels only, min 1 (csgo's own floor for "outline on").
        float t = floorf(g_csXOutlineTh + 0.5f);
        if (t < 1.f) t = 1.f;
        if (bAdditive) dl->AddCallback(ChCsBlendNormal, nullptr);
        dl->AddRectFilled(ImVec2(x0 - t, y0 - t), ImVec2(x1 + t, y1 + t), IM_COL32(0, 0, 0, a));
        if (bAdditive) dl->AddCallback(ChCsBlendAdd, nullptr);
    }
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(r, g, b, a));
}

static void RenderCsgoClassic(int style)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    const int scrW = (int)io.DisplaySize.x, scrH = (int)io.DisplaySize.y;
    if (scrH <= 0) return;

    const CsPlayerState st = CsGetPlayerState();
    const bool  bDyn = (style == 2 || style == 3);

    float dt = io.DeltaTime;
    if (dt < 0.f)   dt = 0.f;
    if (dt > 0.25f) dt = 0.25f;

    const float velSens = (g_chCsgoVelSens < 0.f) ? 0.f : g_chCsgoVelSens;
    const float tau     = (g_chCsgoSmooth  < 0.f) ? 0.f : g_chCsgoSmooth;

    // fCrosshairDistanceGoal (:2083) -- the movement multipliers only apply to 2/3
    float goal = kCsXDistGoal;
    if (bDyn && st.valid)
    {
        float mul = 1.0f;
        if (!st.onGround)     mul = 2.0f;
        else if (st.ducking)  mul = 0.5f;
        else if (sqrtf(st.speed2D*st.speed2D + st.velZ*st.velZ) > 100.f) mul = 1.5f;
        goal *= 1.0f + (mul - 1.0f) * velSens;   // velSens 0 -> the standing goal, always
    }

    static float s_dist = kCsXDistGoal;
    if (s_dist > goal)                                   // Lerp(t, goal, dist) (:2119)
        s_dist = goal + (s_dist - goal) * (dt / 0.025f);
    if (s_dist < goal)  s_dist = goal;
    if (s_dist > 25.f)  s_dist = 25.f;

    // GetInaccuracy() + GetSpread(), same numbers the panorama arcs ride
    static ConVar* s_fov = nullptr;
    if (!s_fov && g_cvar) s_fov = g_cvar->FindVarWalk("fov_desired");
    float fov = s_fov ? s_fov->GetFloat() : 90.f;
    if (fov < 1.f || fov > 179.f) fov = 90.f;
    const float inacc = st.valid ? CsUpdateInaccuracy(st, dt, velSens) : 0.f;
    const float halfFov = fov * 0.5f * 3.14159265f / 180.f;
    const float spreadDist = (inacc + kCsSpread) * 320.0f / tanf(halfFov);

    const float maxCross = g_csXSplitDist;
    const float cappedSpreadDist = (spreadDist > maxCross) ? maxCross : spreadDist;
    const float yres = scrH / 480.0f;
    const int iSpread       = (style < 4) ? (int)floorf(spreadDist       * yres + 0.5f) : 2;
    const int iCappedSpread = (style < 4) ? (int)floorf(cappedSpreadDist * yres + 0.5f) : 2;

    // 2132 -- note the gap rides ScreenHeight/1200 while the bars ride YRES
    int iDist   = (int)floorf(s_dist * scrH / 1200.0f + g_csXGap + 0.5f);
    int iCapped = 0;
    // arm aspect, w:h -- the short axis keeps cl_crosshairsize, the long one stretches
    const float cAsp     = g_chCsgoAspect > 0.01f ? g_chCsgoAspect : 0.01f;
    const int iBarSizeH  = (int)floorf(g_csXSize * (cAsp >= 1.f ? cAsp : 1.f) * yres + 0.5f);
    const int iBarSizeV  = (int)floorf(g_csXSize * (cAsp <= 1.f ? (1.f / cAsp) : 1.f) * yres + 0.5f);
    int iBarThickness    = (int)floorf(g_csXThick * yres + 0.5f);
    if (iBarThickness < 1) iBarThickness = 1;

    if (iSpread > 0 && bDyn)
    {
        iDist = iSpread + (int)g_csXGap;
        if (style == 2) iCapped = iCappedSpread + (int)g_csXGap;
    }
    else if (style == 4 || (iSpread == 0 && bDyn))
    {
        iDist   = (int)(goal + g_csXGap);
        iCapped = (int)(4.f + g_csXGap);
    }

    // csgo drops the bars wherever this frame's spread puts them; tau eases that walk.
    static float s_distSm = 0.f, s_cappedSm = 0.f;
    iDist   = (int)floorf(ChCsEase(s_distSm,   (float)iDist,   dt, tau) + 0.5f);
    iCapped = (int)floorf(ChCsEase(s_cappedSm, (float)iCapped, dt, tau) + 0.5f);

    // gap aspect, w:h -- rounded back to whole pixels here, so the bars keep landing on
    // the same pixel grid the outline below is snapped to
    const float gAsp   = g_chCsgoGapAspect > 0.01f ? g_chCsgoGapAspect : 0.01f;
    const float gapHM  = (gAsp >= 1.f) ? gAsp : 1.f;
    const float gapVM  = (gAsp <= 1.f) ? (1.f / gAsp) : 1.f;
    const int iDistH   = (int)floorf(iDist   * gapHM + 0.5f);
    const int iDistV   = (int)floorf(iDist   * gapVM + 0.5f);
    const int iCappedH = (int)floorf(iCapped * gapHM + 0.5f);
    const int iCappedV = (int)floorf(iCapped * gapVM + 0.5f);

    const int cxi = scrW / 2, cyi = scrH / 2;

    int   r = (int)(g_chCsgoColor[0] * 255.f);
    int   g = (int)(g_chCsgoColor[1] * 255.f);
    int   b = (int)(g_chCsgoColor[2] * 255.f);
    const bool bAdditive = !g_csXUseAlpha;
    int alpha = g_csXAlpha;
    if (alpha < 0) alpha = 0; else if (alpha > 255) alpha = 255;
    if (bAdditive) alpha = 200;                       // :2036
    alpha = (int)(alpha * g_chCsgoColor[3] + 0.5f);

    int   innerDistH    = iDistH;
    int   innerDistV    = iDistV;
    float alphaInner    = (float)alpha;
    float alphaOuter    = (float)alpha;
    int   barSizeInnerH = iBarSizeH;
    int   barSizeInnerV = iBarSizeV;

    if (bAdditive) dl->AddCallback(ChCsBlendAdd, nullptr);

    // style 2 past the split distance grows a second pair of pips outside the first
    if (style == 2 && spreadDist > maxCross)
    {
        innerDistH    = iCappedH;
        innerDistV    = iCappedV;
        alphaInner    = alpha * g_csXInnerMod;
        alphaOuter    = alpha * g_csXOuterMod;
        barSizeInnerH = (int)ceilf ((float)iBarSizeH * (1.0f - g_csXSplitRatio));
        barSizeInnerV = (int)ceilf ((float)iBarSizeV * (1.0f - g_csXSplitRatio));
        const int barSizeOuterH = (int)floorf((float)iBarSizeH * g_csXSplitRatio);
        const int barSizeOuterV = (int)floorf((float)iBarSizeV * g_csXSplitRatio);

        const int innerLeft  = (cxi - iDistH - iBarThickness / 2) - barSizeInnerH;
        const int innerRight = innerLeft + 2 * (iDistH + barSizeInnerH) + iBarThickness;
        const int outerLeft  = innerLeft  - barSizeOuterH;
        const int outerRight = innerRight + barSizeOuterH;
        const int y0 = cyi - iBarThickness / 2, y1 = y0 + iBarThickness;
        CsXRect(dl, r, g, b, (int)alphaOuter, (float)outerLeft,  (float)y0, (float)innerLeft,  (float)y1, bAdditive);
        CsXRect(dl, r, g, b, (int)alphaOuter, (float)innerRight, (float)y0, (float)outerRight, (float)y1, bAdditive);

        const int innerTop    = (cyi - iDistV - iBarThickness / 2) - barSizeInnerV;
        const int innerBottom = innerTop + 2 * (iDistV + barSizeInnerV) + iBarThickness;
        const int outerTop    = innerTop    - barSizeOuterV;
        const int outerBottom = innerBottom + barSizeOuterV;
        const int x0 = cxi - iBarThickness / 2, x1 = x0 + iBarThickness;
        if (!g_csXT)
            CsXRect(dl, r, g, b, (int)alphaOuter, (float)x0, (float)outerTop, (float)x1, (float)innerTop, bAdditive);
        CsXRect(dl, r, g, b, (int)alphaOuter, (float)x0, (float)innerBottom, (float)x1, (float)outerBottom, bAdditive);
    }

    // horizontal bars
    const int innerLeft  = cxi - innerDistH - iBarThickness / 2;
    const int innerRight = innerLeft + 2 * innerDistH + iBarThickness;
    const int outerLeft  = innerLeft  - barSizeInnerH;
    const int outerRight = innerRight + barSizeInnerH;
    const int y0 = cyi - iBarThickness / 2, y1 = y0 + iBarThickness;
    CsXRect(dl, r, g, b, (int)alphaInner, (float)outerLeft,  (float)y0, (float)innerLeft,  (float)y1, bAdditive);
    CsXRect(dl, r, g, b, (int)alphaInner, (float)innerRight, (float)y0, (float)outerRight, (float)y1, bAdditive);

    // vertical bars -- cl_crosshair_t drops the top one
    const int innerTop    = cyi - innerDistV - iBarThickness / 2;
    const int innerBottom = innerTop + 2 * innerDistV + iBarThickness;
    const int outerTop    = innerTop    - barSizeInnerV;
    const int outerBottom = innerBottom + barSizeInnerV;
    const int x0 = cxi - iBarThickness / 2, x1 = x0 + iBarThickness;
    if (!g_csXT)
        CsXRect(dl, r, g, b, (int)alphaInner, (float)x0, (float)outerTop, (float)x1, (float)innerTop, bAdditive);
    CsXRect(dl, r, g, b, (int)alphaInner, (float)x0, (float)innerBottom, (float)x1, (float)outerBottom, bAdditive);

    if (g_csXDot)
    {
        const int dx0 = cxi - iBarThickness / 2, dx1 = dx0 + iBarThickness;
        const int dy0 = cyi - iBarThickness / 2, dy1 = dy0 + iBarThickness;
        CsXRect(dl, r, g, b, alpha, (float)dx0, (float)dy0, (float)dx1, (float)dy1, bAdditive);
    }

    if (bAdditive) dl->AddCallback(ChCsEnd, nullptr);
}

// CS:S's own stock crosshair: "crosshair" ConVar (game/client/hud_crosshair.cpp:32,
// FCVAR_ARCHIVE). Hold it off while either of ours is drawing, put the player's value
// back the moment we stop. SOLE writer of this convar -- visuals.cpp used to force it
// too, which fought this pin and stomped the player's own setting.
static ConVar* s_chGameCv   = nullptr;
static bool    s_chPinned   = false;
static float   s_chOrig     = 1.f;

static void ApplyNativeCrosshairState(bool suppress)
{
    if (!s_chGameCv && g_cvar) s_chGameCv = g_cvar->FindVarWalk("crosshair");
    if (!s_chGameCv) return;

    if (suppress && s_chPinned) { s_chGameCv->ForceFloat(0.f); return; }  // keep it pinned
    if (suppress == s_chPinned) return;

    if (suppress) { s_chOrig = s_chGameCv->GetFloat(); s_chGameCv->ForceFloat(0.f); }
    else          { s_chGameCv->ForceFloat(s_chOrig); }
    s_chPinned = suppress;
}

// Eject: hand the convar back exactly as we found it.
void Crosshair_Restore()
{
    ApplyNativeCrosshairState(false);
    KeepKillfeed_Restore();
}

// ==== Sniper crosshair (botox indicators.cpp sniper_crosshair) =========================
// Drawn in place of the custom crosshair while a scoped (sniper-class) weapon is held and the
// player is NOT currently zoomed in (CssBridge_IsScoped false) -- csgo/botox hide every HUD
// crosshair once actually scoped, so none of the 4 styles below ever draw there either.

// style 0/1 ("clarity"): a tight 2x2 dot, outline style adds two faint expanding rings.
static void SniperCrosshair_Clarity(ImDrawList* dl, float cx, float cy, bool outline)
{
    const float x = floorf(cx), y = floorf(cy);
    if (outline)
    {
        const ImU32 oc = ChPackRGBA(g_sniperChOutlineColor);
        dl->AddRectFilled(ImVec2(x - 3.f, y - 3.f), ImVec2(x + 3.f, y + 3.f), ChScaleAlpha(oc, 0.15f));
        dl->AddRectFilled(ImVec2(x - 2.f, y - 2.f), ImVec2(x + 2.f, y + 2.f), oc);
    }
    dl->AddRectFilled(ImVec2(x - 1.f, y - 1.f), ImVec2(x + 1.f, y + 1.f), ChPackRGBA(g_sniperChColor));
}

struct SniperArm { float x1, y1, x2, y2; };

static void SniperCrosshair_Ring(ImDrawList* dl, float x1, float y1, float x2, float y2, ImU32 col, bool corners)
{
    if (corners) { dl->AddRectFilled(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), col); return; }
    dl->AddRectFilled(ImVec2(x1, y1 - 1.f), ImVec2(x2, y1), col);
    dl->AddRectFilled(ImVec2(x1, y2), ImVec2(x2, y2 + 1.f), col);
    dl->AddRectFilled(ImVec2(x1 - 1.f, y1), ImVec2(x1, y2), col);
    dl->AddRectFilled(ImVec2(x2, y1), ImVec2(x2 + 1.f, y2), col);
}

// style 2 ("interwebz"): a thick outlined box with 4 outlined arms -- gap measured from the
// core box edge so raising thickness never eats it (botox comment, ported verbatim).
static void SniperCrosshair_Interwebz(ImDrawList* dl, float cx, float cy)
{
    const float size   = g_sniperChInterwebzSize;
    const float length = floorf(7.f * size + 0.5f);
    const float gap    = (float)g_sniperChInterwebzGap;
    const float thickness = (float)(g_sniperChInterwebzThickness < 1 ? 1 : g_sniperChInterwebzThickness);

    const float loX = floorf(cx) - floorf(thickness * 0.5f), hiX = loX + thickness;
    const float loY = floorf(cy) - floorf(thickness * 0.5f), hiY = loY + thickness;

    const ImU32 color       = ChPackRGBA(g_sniperChColor);
    const ImU32 outlineCol  = ChPackRGBA(g_sniperChOutlineColor);
    const bool  armCorners  = g_sniperChInterwebzArmCorners;

    const SniperArm arms[4] = {
        { loX - gap - length, loY, loX - gap, hiY },
        { hiX + gap, loY, hiX + gap + length, hiY },
        { loX, loY - gap - length, hiX, loY - gap },
        { loX, hiY + gap, hiX, hiY + gap + length },
    };

    for (const auto& a : arms) SniperCrosshair_Ring(dl, a.x1, a.y1, a.x2, a.y2, outlineCol, armCorners);
    for (const auto& a : arms) dl->AddRectFilled(ImVec2(a.x1, a.y1), ImVec2(a.x2, a.y2), color);

    SniperCrosshair_Ring(dl, loX, loY, hiX, hiY, outlineCol, g_sniperChInterwebzDotCorners);
    dl->AddRectFilled(ImVec2(loX, loY), ImVec2(hiX, hiY), ChPackRGBA(g_sniperChDotColor));
}

static int SniperCrosshair_Round(float v) { return (int)floorf(v + 0.5f); }

static void SniperCrosshair_Rect(ImDrawList* dl, int x0, int y0, int x1, int y1, ImU32 color,
    bool outline, float outlineThickness, ImU32 outlineColor)
{
    if (outline)
        dl->AddRectFilled(ImVec2((float)(int)(x0 - outlineThickness), (float)(int)(y0 - outlineThickness)),
                          ImVec2((float)(int)(x1 + outlineThickness), (float)(int)(y1 + outlineThickness)), outlineColor);
    dl->AddRectFilled(ImVec2((float)x0, (float)y0), ImVec2((float)x1, (float)y1), color);
}

// style 3 ("custom"): the csgo-classic-style 4 bars + optional dot, botox's own fixed-gap
// variant (never expands with spread -- distance is a fixed 4-unit minimum + the gap knob).
static void SniperCrosshair_Custom(ImDrawList* dl)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float yres = io.DisplaySize.y / 480.f;

    float colorA[4]; memcpy(colorA, g_sniperChColor, sizeof(colorA));
    float outlineA[4]; memcpy(outlineA, g_sniperChOutlineColor, sizeof(outlineA));
    outlineA[3] = colorA[3];   // outline alpha pinned to the body colour's alpha

    const ImU32 color   = ChPackRGBA(colorA);
    const ImU32 outline = ChPackRGBA(outlineA);

    const bool  doOutline = g_sniperChOutline;
    float outlineThickness = g_sniperChOutlineThickness;
    outlineThickness = outlineThickness < 0.1f ? 0.1f : (outlineThickness > 3.f ? 3.f : outlineThickness);

    const int distance   = (int)(4.f + g_sniperChGap);
    const int barSize    = SniperCrosshair_Round(g_sniperChSize * yres);
    int barThickness     = SniperCrosshair_Round(g_sniperChThickness * yres);
    if (barThickness < 1) barThickness = 1;

    const int centerX = (int)io.DisplaySize.x / 2, centerY = (int)io.DisplaySize.y / 2;
    const bool tStyle = g_sniperChT;

    const int innerLeft  = centerX - distance - barThickness / 2;
    const int innerRight = innerLeft + 2 * distance + barThickness;
    const int outerLeft  = innerLeft - barSize, outerRight = innerRight + barSize;
    int y0 = centerY - barThickness / 2, y1 = y0 + barThickness;
    SniperCrosshair_Rect(dl, outerLeft, y0, innerLeft, y1, color, doOutline, outlineThickness, outline);
    SniperCrosshair_Rect(dl, innerRight, y0, outerRight, y1, color, doOutline, outlineThickness, outline);

    const int innerTop = centerY - distance - barThickness / 2;
    const int innerBottom = innerTop + 2 * distance + barThickness;
    const int outerTop = innerTop - barSize, outerBottom = innerBottom + barSize;
    int x0 = centerX - barThickness / 2, x1 = x0 + barThickness;
    if (!tStyle) SniperCrosshair_Rect(dl, x0, outerTop, x1, innerTop, color, doOutline, outlineThickness, outline);
    SniperCrosshair_Rect(dl, x0, innerBottom, x1, outerBottom, color, doOutline, outlineThickness, outline);

    if (g_sniperChDot)
    {
        x0 = centerX - barThickness / 2; x1 = x0 + barThickness;
        y0 = centerY - barThickness / 2; y1 = y0 + barThickness;
        SniperCrosshair_Rect(dl, x0, y0, x1, y1, color, doOutline, outlineThickness, outline);
    }
}

static void SniperCrosshair_Render()
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;

    switch (g_sniperChStyle)
    {
    case 1: SniperCrosshair_Clarity(dl, cx, cy, true); break;
    case 2: SniperCrosshair_Interwebz(dl, cx, cy); break;
    case 3: SniperCrosshair_Custom(dl); break;
    default: SniperCrosshair_Clarity(dl, cx, cy, false); break;
    }
}

// botox custom_crosshair.cpp on_paint_traverse's hide rule (minus the scope check, which is
// shared with the sniper crosshair below): never draw while holding an unscoped sniper weapon,
// unless force_crosshair says otherwise. g_forceCrosshair is the CSS-adapted half of botox's
// force_crosshair -- misc.cpp's half ALSO forced CSGO's weapon_debug_spread_show debug convar so
// a crosshair-like overlay survived being actually zoomed in; grepped for that cvar across
// source-sdk-2013 and resources/cssourcex64, no match -- CS:S has no such debug path, so that
// half is dropped (see report). Being actually scoped still hides this crosshair either way,
// same as botox's on_paint_traverse (its own force check never overrides is_scoped()).
static bool ChCustomCrosshairHeld(int localIdx)
{
    if (g_forceCrosshair) return true;
    const int wpn = CssBridge_ActiveWeapon(localIdx);
    return !(wpn && CssBridge_IsSniperWeapon(wpn));
}

void RenderCrosshair()
{
    KeepKillfeed_Tick();   // piggyback: no tick hook of its own, see killfeed.h
    // IsInGame is a virtual call into the engine and this runs every frame -- it was
    // asked twice, and IsLocalPlayerAlive walks GetLocalPlayerIndex + GetClientEntity
    // again right after. Ask once, and only ask about the player when we'd actually draw.
    const bool inGame = IsInGame();
    const bool wanted = (g_chEnable || g_chCsgoEnable || g_sniperChEnable) && !g_eject;
    ApplyNativeCrosshairState(wanted && inGame);

    if (!wanted || !inGame || !IsLocalPlayerAlive()) return;

    const int localIdx = GetLocalPlayerIndex();
    const bool scoped  = localIdx > 0 && CssBridge_IsScoped(localIdx);

    // botox: both crosshairs are hidden outright while actually scoped (is_scoped() check runs
    // before either one's own logic in on_paint_traverse/sniper_crosshair).
    if (g_chEnable && !scoped && ChCustomCrosshairHeld(localIdx)) RenderCustomCrosshair();
    if (g_sniperChEnable && !scoped && localIdx > 0)
    {
        const int wpn = CssBridge_ActiveWeapon(localIdx);
        if (wpn && CssBridge_IsSniperWeapon(wpn)) SniperCrosshair_Render();
    }
    if (g_chCsgoEnable)
    {
        if (g_chCsgoStyle <= 1) RenderCsgoPanorama(g_chCsgoStyle == 1);   // panorama reticle
        else                    RenderCsgoClassic(g_chCsgoStyle);         // vgui bars
    }
}
