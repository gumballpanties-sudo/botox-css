#include "resolution_spoof.h"
#include "../../globals/globals.h"            // DebugLog, g_aspectRatioEnable/g_aspectRatioValue, g_eject
#include "../visuals/visuals.h"                // CachedCVar, CVar_ForceInt (net graph)
#include "../../dependencies/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

// aspect_stretch.cpp's per-overlay sub-toggles (hud/crosshair/cheat overlay) -- read here so
// AspectHud()/AspectCrosshair()/AspectCheatOverlay() can fold the plain-slider case through the
// same accessor the stretch-res case uses. Declared there (aspect_stretch.h doesn't carry them).
extern bool g_sfxAspectHud;
extern bool g_sfxAspectCrosshair;
extern bool g_sfxAspectCheatOverlay;

namespace
{
    // ---- overlay mode: our own ImGui's small RT (A8R8G8B8 -- composited back with alpha) ----
    IDirect3DTexture9* s_ovTex  = nullptr;
    IDirect3DSurface9* s_ovSurf = nullptr;
    int s_ovW = 0, s_ovH = 0;
    bool s_lastFrameSpoofed = false;   // what OnImGuiNewFrame decided -- WrapImGuiDraw reads it back

    // ---- layer 3: the world/hud/crosshair crunch (backbuffer format -- opaque, no alpha) ----
    IDirect3DTexture9* s_copyTex  = nullptr;   // full-res copy of the backbuffer
    IDirect3DSurface9* s_copySurf = nullptr;
    int s_copyW = 0, s_copyH = 0;
    D3DFORMAT s_copyFmt = D3DFMT_UNKNOWN;
    IDirect3DTexture9* s_lowTex  = nullptr;    // the crunched low-res image
    IDirect3DSurface9* s_lowSurf = nullptr;
    int s_lowW = 0, s_lowH = 0;
    bool s_crunchDone = false;   // same once-a-frame latch Bloom_Frame/ColorCorr_Frame/ScreenFxPasses_Frame use

    void ReleaseOverlay()
    {
        if (s_ovSurf) { s_ovSurf->Release(); s_ovSurf = nullptr; }
        if (s_ovTex)  { s_ovTex->Release();  s_ovTex  = nullptr; }
        s_ovW = s_ovH = 0;
    }

    void ReleaseCrunch()
    {
        if (s_lowSurf)  { s_lowSurf->Release();  s_lowSurf  = nullptr; }
        if (s_lowTex)   { s_lowTex->Release();   s_lowTex   = nullptr; }
        if (s_copySurf) { s_copySurf->Release(); s_copySurf = nullptr; }
        if (s_copyTex)  { s_copyTex->Release();  s_copyTex  = nullptr; }
        s_copyW = s_copyH = s_lowW = s_lowH = 0;
        s_copyFmt = D3DFMT_UNKNOWN;
    }

    void Release() { ReleaseOverlay(); ReleaseCrunch(); }

    // botox resolution_spoof.cpp:322-332 (target_size), aspect-correct unless "stretch".
    void TargetSize(int screenW, int screenH, int& outW, int& outH)
    {
        // parens around max/clamp: windows.h's min/max macros (no NOMINMAX this TU, d3d9.h
        // pulls it in) would otherwise eat std::max/std::clamp -- same guard game_pure.h uses.
        outH = (std::clamp)(ResSpoof::g_height, 32, (std::max)(screenH, 32));
        const int wantedW = ResSpoof::g_stretch || screenH <= 0
            ? ResSpoof::g_width
            : (int)std::lround((float)outH * screenW / screenH);
        outW = (std::clamp)(wantedW, 32, (std::max)(screenW, 32));
    }

    bool EnsureOverlay(IDirect3DDevice9* dev, int w, int h)
    {
        if (s_ovTex && s_ovSurf && s_ovW == w && s_ovH == h) return true;
        ReleaseOverlay();
        if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_ovTex, nullptr)) ||
            FAILED(s_ovTex->GetSurfaceLevel(0, &s_ovSurf)))
        {
            DebugLog("resolution spoof: overlay target %dx%d failed", w, h);
            ReleaseOverlay();
            return false;
        }
        s_ovW = w; s_ovH = h;
        return true;
    }

    // never StretchRect INTO anything MSAA: both copy + low targets below are plain
    // D3DPOOL_DEFAULT RENDERTARGET textures, never the multisampled backbuffer itself.
    bool EnsureCrunch(IDirect3DDevice9* dev, int screenW, int screenH, D3DFORMAT fmt, int lowW, int lowH)
    {
        if (!s_copyTex || s_copyW != screenW || s_copyH != screenH || s_copyFmt != fmt)
        {
            if (s_copySurf) { s_copySurf->Release(); s_copySurf = nullptr; }
            if (s_copyTex)  { s_copyTex->Release();  s_copyTex  = nullptr; }
            HRESULT hr = dev->CreateTexture(screenW, screenH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_copyTex, nullptr);
            if (FAILED(hr))   // rare format that can't back a render-target texture (bloom.cpp's same fallback)
                hr = dev->CreateTexture(screenW, screenH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_copyTex, nullptr);
            if (FAILED(hr) || FAILED(s_copyTex->GetSurfaceLevel(0, &s_copySurf)))
            {
                DebugLog("resolution spoof: backbuffer copy target %dx%d failed 0x%08lx", screenW, screenH, (unsigned long)hr);
                ReleaseCrunch();
                return false;
            }
            s_copyW = screenW; s_copyH = screenH; s_copyFmt = fmt;
        }
        if (!s_lowTex || s_lowW != lowW || s_lowH != lowH)
        {
            if (s_lowSurf) { s_lowSurf->Release(); s_lowSurf = nullptr; }
            if (s_lowTex)  { s_lowTex->Release();  s_lowTex  = nullptr; }
            if (FAILED(dev->CreateTexture(lowW, lowH, 1, D3DUSAGE_RENDERTARGET, s_copyFmt, D3DPOOL_DEFAULT, &s_lowTex, nullptr)) ||
                FAILED(s_lowTex->GetSurfaceLevel(0, &s_lowSurf)))
            {
                DebugLog("resolution spoof: crunch target %dx%d failed", lowW, lowH);
                ReleaseCrunch();
                return false;
            }
            s_lowW = lowW; s_lowH = lowH;
            DebugLog("resolution spoof: crunch built %dx%d -> %dx%d -> %dx%d", screenW, screenH, lowW, lowH, screenW, screenH);
        }
        return true;
    }

    struct Saved
    {
        IDirect3DSurface9* rt;
        D3DVIEWPORT9 vp;
        DWORD blendEnable, srcBlend, destBlend, alphaBlend, zEnable, scissor;
        IDirect3DBaseTexture9* tex0;
    };

    void SaveState(IDirect3DDevice9* dev, Saved& s)
    {
        if (FAILED(dev->GetRenderTarget(0, &s.rt))) s.rt = nullptr;
        dev->GetViewport(&s.vp);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &s.blendEnable);
        dev->GetRenderState(D3DRS_SRCBLEND, &s.srcBlend);
        dev->GetRenderState(D3DRS_DESTBLEND, &s.destBlend);
        dev->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &s.alphaBlend);
        dev->GetRenderState(D3DRS_ZENABLE, &s.zEnable);
        dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &s.scissor);
        dev->GetTexture(0, &s.tex0);
    }

    void RestoreState(IDirect3DDevice9* dev, Saved& s)
    {
        dev->SetRenderTarget(0, s.rt);
        dev->SetViewport(&s.vp);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, s.blendEnable);
        dev->SetRenderState(D3DRS_SRCBLEND, s.srcBlend);
        dev->SetRenderState(D3DRS_DESTBLEND, s.destBlend);
        dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, s.alphaBlend);
        dev->SetRenderState(D3DRS_ZENABLE, s.zEnable);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, s.scissor);
        dev->SetTexture(0, s.tex0);
        if (s.rt)   s.rt->Release();
        if (s.tex0) s.tex0->Release();
    }

    struct QuadVert { float x, y, z, rhw, u, v; };

    // Shared FFP textured quad -- legal on amd (no pixel shader), same idiom bloom.cpp/the
    // overlay path above already use. opaque: no alpha blend, straight sample + replace.
    void DrawQuad(IDirect3DDevice9* dev, IDirect3DTexture9* tex, int targetW, int targetH, bool linear, bool blend)
    {
        const float l = -0.5f, t = -0.5f, r = (float)targetW - 0.5f, b = (float)targetH - 0.5f;
        const QuadVert q[4] = {
            { l, t, 0.f, 1.f, 0.f, 0.f }, { r, t, 0.f, 1.f, 1.f, 0.f },
            { l, b, 0.f, 1.f, 0.f, 1.f }, { r, b, 0.f, 1.f, 1.f, 1.f },
        };
        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(nullptr);
        dev->SetVertexDeclaration(nullptr);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        dev->SetTexture(0, tex);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        const DWORD filter = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        if (blend)
        {
            // straight (non-premultiplied) alpha -- same blend ImGui_ImplDX9 itself draws with
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        else
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QuadVert));
    }
}

// ================ aspect accessor RULE ================================================
bool ResSpoof::UpscaleStretches() { return ResSpoof::g_enable && ResSpoof::g_stretch && !ResSpoof::g_debugBypass; }

bool ResSpoof::AspectOn()
{
    return ResSpoof::UpscaleStretches() || (g_aspectRatioEnable && !g_eject);
}

float ResSpoof::AspectWanted()
{
    if (ResSpoof::UpscaleStretches())
        return (float)(std::max)(ResSpoof::g_width, 32) / (float)(std::max)(ResSpoof::g_height, 32);
    return g_aspectRatioValue;
}

bool ResSpoof::AspectHud()
{
    // see resolution_spoof.h banner: CSS has no scaleform hud layer to drive separately --
    // folds into the same AspectOn() path (r_aspectratio / the crunch) every other world
    // element already takes.
    return ResSpoof::UpscaleStretches() || (g_aspectRatioEnable && !g_eject && g_sfxAspectHud);
}

bool ResSpoof::AspectCrosshair()
{
    return ResSpoof::UpscaleStretches() || (g_aspectRatioEnable && !g_eject && g_sfxAspectCrosshair);
}

bool ResSpoof::AspectCheatOverlay()
{
    // stretch-to-fill: the crunch's own upscale already widens our overlay -- widening it a
    // second time here would be the "never twice" the memory note warns about. Unless overlay
    // mode is on: that overlay is NOT swept into the crunch (render.cpp draws it after), so it
    // still needs this to widen it the one time.
    if (ResSpoof::UpscaleStretches())
        return ResSpoof::g_overlayMode;
    return g_aspectRatioEnable && !g_eject && g_sfxAspectCheatOverlay;
}

// ================ layer 3: the backbuffer crunch =======================================
void ResSpoof::CrunchBackbuffer(IDirect3DDevice9* dev)
{
    if (!dev || s_crunchDone) return;
    if (!ResSpoof::g_enable || ResSpoof::g_debugBypass || g_eject)
    {
        ReleaseCrunch();
        return;
    }
    s_crunchDone = true;

    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
        return;

    D3DSURFACE_DESC desc{};
    if (FAILED(backBuffer->GetDesc(&desc))) { backBuffer->Release(); return; }

    int w = 0, h = 0;
    TargetSize((int)desc.Width, (int)desc.Height, w, h);
    if (w >= (int)desc.Width && h >= (int)desc.Height) { backBuffer->Release(); ReleaseCrunch(); return; }   // no shrink, nothing to gain

    if (!EnsureCrunch(dev, (int)desc.Width, (int)desc.Height, desc.Format, w, h))
    {
        backBuffer->Release();
        return;
    }

    Saved saved{};
    SaveState(dev, saved);

    // 1. real backbuffer -> full-res copy (StretchRect, no shader -- the backbuffer cannot be
    //    sampled directly; a plain non-MSAA RENDERTARGET texture IS a legal StretchRect dest
    //    from an MSAA source, same resolve bloom.cpp's frame copy already relies on).
    if (SUCCEEDED(dev->StretchRect(backBuffer, nullptr, s_copySurf, nullptr, D3DTEXF_NONE)))
    {
        // 2. copy -> low res (downscale, always LINEAR regardless of g_smooth: box-filtering the
        //    DOWNSCALE is what avoids aliasing; g_smooth only picks the UPSCALE's look).
        dev->SetRenderTarget(0, s_lowSurf);
        const D3DVIEWPORT9 lowVp{ 0, 0, (DWORD)w, (DWORD)h, 0.f, 1.f };
        dev->SetViewport(&lowVp);
        DrawQuad(dev, s_copyTex, w, h, true, false);

        // 3. low res -> real backbuffer (upscale, POINT by default = the blocky "low res" look;
        //    LINEAR if g_smooth). Opaque: no alpha blend, this REPLACES the frame.
        dev->SetRenderTarget(0, backBuffer);
        const D3DVIEWPORT9 fullVp{ 0, 0, desc.Width, desc.Height, 0.f, 1.f };
        dev->SetViewport(&fullVp);
        DrawQuad(dev, s_lowTex, (int)desc.Width, (int)desc.Height, ResSpoof::g_smooth, false);
    }
    else
    {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("resolution spoof: backbuffer copy (StretchRect) failed -- crunch skipped this frame"); }
    }

    RestoreState(dev, saved);
    backBuffer->Release();

    static bool loggedOnce = false;
    if (!loggedOnce) { loggedOnce = true; DebugLog("resolution spoof: crunch reached, %dx%d -> %dx%d -> %dx%d", (int)desc.Width, (int)desc.Height, w, h, (int)desc.Width, (int)desc.Height); }
}

// ================ overlay mode: our own ImGui's small RT ================================
void ResSpoof::OnImGuiNewFrame()
{
    s_lastFrameSpoofed = false;

    ImGuiIO& io = ImGui::GetIO();
    if (!ResSpoof::g_enable || !ResSpoof::g_overlayMode || ResSpoof::g_debugBypass) return;
    if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f) return;

    int w = 0, h = 0;
    TargetSize((int)io.DisplaySize.x, (int)io.DisplaySize.y, w, h);
    if (w >= (int)io.DisplaySize.x && h >= (int)io.DisplaySize.y) return;   // no shrink, nothing to gain

    const float scaleX = (float)w / io.DisplaySize.x;
    const float scaleY = (float)h / io.DisplaySize.y;
    io.DisplaySize = ImVec2((float)w, (float)h);
    if (io.MousePos.x > -FLT_MAX && io.MousePos.y > -FLT_MAX)
    {
        io.MousePos.x *= scaleX;
        io.MousePos.y *= scaleY;
    }
    s_lastFrameSpoofed = true;
}

bool ResSpoof::WrapImGuiDraw(IDirect3DDevice9* dev, void (*drawFn)())
{
    if (!dev || !drawFn) return false;
    if (!s_lastFrameSpoofed) { drawFn(); return false; }

    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
    {
        drawFn();
        return false;
    }

    D3DSURFACE_DESC desc{};
    backBuffer->GetDesc(&desc);

    int w = 0, h = 0;
    TargetSize((int)desc.Width, (int)desc.Height, w, h);

    if (!EnsureOverlay(dev, w, h))
    {
        backBuffer->Release();
        drawFn();
        return false;
    }

    Saved saved{};
    SaveState(dev, saved);

    dev->SetRenderTarget(0, s_ovSurf);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    const D3DVIEWPORT9 lowVp{ 0, 0, (DWORD)w, (DWORD)h, 0.f, 1.f };
    dev->SetViewport(&lowVp);

    drawFn();   // the overlay, laid out at w x h (OnImGuiNewFrame already set io.DisplaySize)

    RestoreState(dev, saved);   // back on the real backbuffer / viewport before the upscale draw
    dev->SetRenderTarget(0, backBuffer);
    DrawQuad(dev, s_ovTex, (int)desc.Width, (int)desc.Height, true, true);

    backBuffer->Release();
    return true;
}

// ================ net graph (botox net_graph_scale) =====================================
void ResSpoof::NetGraphTick()
{
    static CachedCVar cc;
    static int storedDefault = 0;

    const bool wanted = ResSpoof::g_enable && ResSpoof::g_netGraph && !g_eject && !ResSpoof::g_debugBypass;
    ConVar* cv = cc.Get("net_graphheight");
    if (!cv) return;

    if (!wanted)
    {
        if (storedDefault != 0) { CVar_ForceInt(cv, storedDefault); storedDefault = 0; }
        return;
    }

    if (ResSpoof::g_height <= 0) return;
    if (storedDefault == 0)
    {
        const int current = (int)cv->GetFloat();   // ConVar has no GetInt() here, GetFloat() only
        storedDefault = current > 0 ? current : 64;
    }
    // scaled by the SCREEN/res ratio, same direction botox's net_graph_scale uses: the graph
    // stays the same fraction of the (smaller) spoofed screen, so it doesn't shrink to nothing.
    // s_copyW/s_copyH are CrunchBackbuffer's last-seen real screen size (same TU, set there);
    // net_graphheight is a game-thread convar write and this tick has no device of its own.
    if (s_copyH <= 0) return;
    const int wantedPx = (int)(storedDefault * ((float)s_copyH / (float)ResSpoof::g_height));
    if (wantedPx > 0 && (int)cv->GetFloat() != wantedPx) CVar_ForceInt(cv, wantedPx);
}

// ================ preset list (menu combo) ==============================================
namespace
{
    constexpr ResSpoof::Preset kPresets[] = {
        { 320, 240, "320x240" }, { 400, 300, "400x300" }, { 480, 360, "480x360" },
        { 512, 384, "512x384" }, { 640, 360, "640x360" }, { 640, 400, "640x400" },
        { 640, 480, "640x480" }, { 720, 480, "720x480" }, { 800, 450, "800x450" },
        { 800, 600, "800x600" }, { 960, 540, "960x540" }, { 1024, 576, "1024x576" },
        { 1024, 768, "1024x768" }, { 1152, 648, "1152x648" }, { 1152, 864, "1152x864" },
        { 1280, 720, "1280x720" }, { 1280, 800, "1280x800" }, { 1280, 960, "1280x960" },
        { 1280, 1024, "1280x1024" }, { 1366, 768, "1366x768" },
    };
    static_assert(std::size(kPresets) == 20, "botox preset combo is 20 entries");
    ResSpoof::Preset s_filtered[20];
}

const ResSpoof::Preset* ResSpoof::Presets(int screenW, int screenH, int& outCount)
{
    outCount = 0;
    for (const auto& p : kPresets)
        if (p.w < screenW && p.h < screenH)
            s_filtered[outCount++] = p;
    return s_filtered;
}

void ResSpoof::NewFrame() { s_crunchDone = false; }

void ResSpoof::OnDeviceLost() { Release(); }
void ResSpoof::Destroy()      { Release(); }
