#include "bloom.h"
#include "shaders/fx_bloom_bright.h"   // k_fxBloomBrightBytes -- compiled from fx_bloom_bright.hlsl by fxc
#include "shaders/fx_bloom_blur.h"     // k_fxBloomBlurBytes   -- compiled from fx_bloom_blur.hlsl by fxc

// Bloom, in three kinds of pass:
//
//   1. copy    backbuffer -> a full-res render-target texture. StretchRect, no shader: the
//              backbuffer cannot be sampled, so it has to be copied before anything reads it.
//   2. bright  that copy -> a quarter-res buffer, four taps down, everything under the
//              threshold dropped (fx_bloom_bright.hlsl).
//   3. blur    a 5-tap gaussian, x then y, ping-ponged between two quarter-res buffers
//              (fx_bloom_blur.hlsl). The last y pass draws straight onto the frame with an
//              additive blend, reading the quarter-res buffer through the hardware's own
//              bilinear filter -- so the upscale is free and no extra pass pays for it.
//
// QUARTER RES IS THE WHOLE COST STORY. A glow is the one effect that loses nothing to a
// small buffer: it is blurred to a pulp by definition. Six blur passes at 480x270 are
// cheaper than one at full res, which is why the radius slider can reach as far as it does.
//
// INTENSITY RIDES ON THE LAST PASS and not the first, even though a gaussian is linear and
// the two are the same arithmetic: the buffers are 8 bit, and scaling at the start would
// clip every glow past 1.0 into a flat white blob.

static IDirect3DPixelShader9* s_psBright  = nullptr;
static IDirect3DPixelShader9* s_psBlur    = nullptr;
static IDirect3DTexture9*     s_sceneTex  = nullptr;   // full res copy of the frame
static IDirect3DSurface9*     s_sceneSurf = nullptr;
static IDirect3DTexture9*     s_texA      = nullptr;   // quarter res, ping
static IDirect3DSurface9*     s_surfA     = nullptr;
static IDirect3DTexture9*     s_texB      = nullptr;   // quarter res, pong
static IDirect3DSurface9*     s_surfB     = nullptr;
static int  s_w = 0, s_h = 0, s_bw = 0, s_bh = 0;
static bool s_failed = false;      // the card refused a shader -- never ask again
static bool s_done   = false;      // drawn this frame already

void Bloom_NewFrame() { s_done = false; }

void Bloom_Destroy()
{
    if (s_surfB)     { s_surfB->Release();     s_surfB     = nullptr; }
    if (s_texB)      { s_texB->Release();      s_texB      = nullptr; }
    if (s_surfA)     { s_surfA->Release();     s_surfA     = nullptr; }
    if (s_texA)      { s_texA->Release();      s_texA      = nullptr; }
    if (s_sceneSurf) { s_sceneSurf->Release(); s_sceneSurf = nullptr; }
    if (s_sceneTex)  { s_sceneTex->Release();  s_sceneTex  = nullptr; }
    if (s_psBlur)    { s_psBlur->Release();    s_psBlur    = nullptr; }
    if (s_psBright)  { s_psBright->Release();  s_psBright  = nullptr; }
    s_w = s_h = s_bw = s_bh = 0;
}

// Sized to the backbuffer. A resolution change comes through h_Reset, which destroys
// everything; the size test here also covers a swapchain that changed under us.
static bool BloomEnsure(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    D3DSURFACE_DESC desc{};
    if (FAILED(bb->GetDesc(&desc))) return false;

    if (s_psBright && s_psBlur && s_sceneTex && s_texA && s_texB &&
        s_w == (int)desc.Width && s_h == (int)desc.Height)
        return true;

    Bloom_Destroy();

    // Bytecode is built at build time (fx_bloom_*.hlsl -> fx_bloom_*.h), so there is no
    // d3dx9 to find at runtime. Copied into DWORDs because a BYTE array carries no
    // alignment guarantee.
    {
        std::vector<DWORD> code((sizeof(k_fxBloomBrightBytes) + 3) / 4);
        memcpy(code.data(), k_fxBloomBrightBytes, sizeof(k_fxBloomBrightBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_psBright)) || !s_psBright) {
            s_failed = true;
            DebugLog("bloom: the card refused the bright pass shader -- bloom is off");
            Bloom_Destroy();
            return false;
        }
    }
    {
        std::vector<DWORD> code((sizeof(k_fxBloomBlurBytes) + 3) / 4);
        memcpy(code.data(), k_fxBloomBlurBytes, sizeof(k_fxBloomBlurBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_psBlur)) || !s_psBlur) {
            s_failed = true;
            DebugLog("bloom: the card refused the blur shader -- bloom is off");
            Bloom_Destroy();
            return false;
        }
    }

    // The frame copy matches the backbuffer's format, which is what makes the blit a
    // straight copy. X8R8G8B8 is the fallback for the rare format that cannot back a
    // render-target texture.
    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                    desc.Format, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr) || FAILED(s_sceneTex->GetSurfaceLevel(0, &s_sceneSurf))) {
        DebugLog("bloom: frame copy target failed 0x%08lx", (unsigned long)hr);
        Bloom_Destroy();
        return false;
    }

    const int bw = ((int)desc.Width  / 4) < 16 ? 16 : (int)desc.Width  / 4;
    const int bh = ((int)desc.Height / 4) < 16 ? 16 : (int)desc.Height / 4;

    if (FAILED(dev->CreateTexture(bw, bh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_texA, nullptr)) ||
        FAILED(s_texA->GetSurfaceLevel(0, &s_surfA)) ||
        FAILED(dev->CreateTexture(bw, bh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_texB, nullptr)) ||
        FAILED(s_texB->GetSurfaceLevel(0, &s_surfB)))
    {
        DebugLog("bloom: glow buffer creation failed");
        Bloom_Destroy();
        return false;
    }

    s_w = (int)desc.Width; s_h = (int)desc.Height;
    s_bw = bw;             s_bh = bh;
    DebugLog("bloom: built %dx%d, glow buffers %dx%d", s_w, s_h, s_bw, s_bh);
    return true;
}

// Everything the pass touches is saved and put back. DrawPrimitiveUP is on that list for
// the reason most of these hooks miss: D3D9 sets stream source 0 to NULL behind it.
struct BloomSaved
{
    IDirect3DSurface9*           rt;
    IDirect3DSurface9*           ds;
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex0;
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DVIEWPORT9 vp;
    DWORD  rs[16];
    DWORD  samp[6];
    float  psc[2][4];
};

static const D3DRENDERSTATETYPE k_blRS[16] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE,
    D3DRS_FILLMODE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
};
static const DWORD k_blRSVal[16] = {
    FALSE, FALSE, FALSE, FALSE,
    FALSE, D3DCULL_NONE, FALSE, FALSE,
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE |
        D3DCOLORWRITEENABLE_ALPHA,
    FALSE, FALSE, 0,
    D3DFILL_SOLID, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD,
};
static const D3DSAMPLERSTATETYPE k_blSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void BloomSave(IDirect3DDevice9* dev, BloomSaved& s)
{
    if (FAILED(dev->GetRenderTarget(0, &s.rt)))        s.rt = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&s.ds)))    s.ds = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    // DrawPrimitiveUP nulls the index buffer as well as stream 0, and the material system's
    // own binding cache still believes both are bound.
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->GetRenderState(k_blRS[i], &s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_blSamp[i], &s.samp[i]);
    dev->GetPixelShaderConstantF(0, s.psc[0], 2);
}

static void BloomRestore(IDirect3DDevice9* dev, BloomSaved& s)
{
    // The render target goes back FIRST: the viewport is part of a target's state, so
    // putting the viewport back before the target would leave the saved numbers on the
    // wrong surface and the engine's next draw clipped to a quarter of the screen.
    dev->SetRenderTarget(0, s.rt);
    dev->SetDepthStencilSurface(s.ds);
    dev->SetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_blRS[i], s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_blSamp[i], s.samp[i]);
    dev->SetPixelShaderConstantF(0, s.psc[0], 2);
    dev->SetTexture(0, s.tex0);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    // SetFVF and SetVertexDeclaration write the same slot, so only the one that was in
    // force goes back: a decl if there was one, otherwise the FVF.
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    if (s.rt)     s.rt->Release();
    if (s.ds)     s.ds->Release();
    if (s.tex0)   s.tex0->Release();
    if (s.ps)     s.ps->Release();
    if (s.vs)     s.vs->Release();
    if (s.decl)   s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

struct BloomVert { float x, y, z, rhw, u, v; };

static void BloomQuad(BloomVert (&q)[4], int w, int h)
{
    const float W = (float)w, H = (float)h;
    q[0] = { -0.5f,    -0.5f,     0.f, 1.f, 0.f, 0.f };
    q[1] = { W - 0.5f, -0.5f,     0.f, 1.f, 1.f, 0.f };
    q[2] = { -0.5f,     H - 0.5f, 0.f, 1.f, 0.f, 1.f };
    q[3] = { W - 0.5f,  H - 0.5f, 0.f, 1.f, 1.f, 1.f };
}

static void BloomDraw(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    // The frame, copied out where a sampler can reach it.
    if (FAILED(dev->StretchRect(bb, nullptr, s_sceneSurf, nullptr, D3DTEXF_NONE))) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("bloom: the frame copy blit failed -- bloom draws nothing"); }
        return;
    }

    BloomVert quadFull[4], quadGlow[4];
    BloomQuad(quadFull, s_w,  s_h);
    BloomQuad(quadGlow, s_bw, s_bh);

    const D3DVIEWPORT9 vpFull{ 0, 0, (DWORD)s_w,  (DWORD)s_h,  0.f, 1.f };
    const D3DVIEWPORT9 vpGlow{ 0, 0, (DWORD)s_bw, (DWORD)s_bh, 0.f, 1.f };

    BloomSaved st{};
    BloomSave(dev, st);

    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_blRS[i], k_blRSVal[i]);
    dev->SetDepthStencilSurface(nullptr);   // no pass here tests or writes depth
    dev->SetVertexShader(nullptr);          // a bound vertex shader would ignore the FVF
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);   // sampling has to be raw, or the
                                                       // glow is measured off a different
                                                       // image than the one on screen

    // PASS ONE: the bright part, down to the glow buffer.
    {
        const float thr = (g_bloomThreshold < 0.f) ? 0.f : (g_bloomThreshold > 1.f ? 1.f : g_bloomThreshold);
        const float c0[4] = { 1.f / (float)s_w, 1.f / (float)s_h, thr, 0.f };
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetRenderTarget(0, s_surfA);
        dev->SetViewport(&vpGlow);
        dev->SetTexture(0, s_sceneTex);
        dev->SetPixelShader(s_psBright);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadGlow, sizeof(BloomVert));
    }

    // PASSES TWO AND ON: x, y, x, y, each pair reaching further than the last. Widening the
    // step instead of adding taps is what makes a radius this large affordable -- the pass
    // before already smoothed the detail the wider step would otherwise alias on.
    const float rad    = (g_bloomRadius < 0.f) ? 0.f : (g_bloomRadius > 1.f ? 1.f : g_bloomRadius);
    const int   passes = 1 + (int)(rad * 5.f + 0.5f);          // 1..6
    const float sat    = (g_bloomSat < 0.f) ? 0.f : (g_bloomSat > 3.f ? 3.f : g_bloomSat);
    const float amount = (g_bloomIntensity < 0.f) ? 0.f : g_bloomIntensity;

    const float neutral[4] = { 1.f, 1.f, 1.f, 1.f };
    dev->SetPixelShader(s_psBlur);

    // botox "aspect ratio" (bloom.cpp:519): + = wide (y steps shrink), - = tall (x steps shrink).
    // One axis only ever shrinks, so no tap lands further from its neighbour than it did.
    const float aspect = (g_bloomAspect < -1.f) ? -1.f : (g_bloomAspect > 1.f ? 1.f : g_bloomAspect);
    const float scaleX = 1.f - ((aspect < 0.f) ? -aspect : 0.f);
    const float scaleY = 1.f - ((aspect > 0.f) ? aspect : 0.f);

    for (int i = 0; i < passes; ++i)
    {
        const float step = (float)(i + 1);
        const bool  last = (i == passes - 1);

        // x: A -> B
        const float cx[4] = { step * scaleX / (float)s_bw, 0.f, 0.f, 0.f };
        dev->SetPixelShaderConstantF(0, cx, 1);
        dev->SetPixelShaderConstantF(1, neutral, 1);
        dev->SetRenderTarget(0, s_surfB);
        dev->SetViewport(&vpGlow);
        dev->SetTexture(0, s_texA);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadGlow, sizeof(BloomVert));

        // y: B -> A, or on the last pass B -> the frame itself, added
        const float cy[4] = { 0.f, step * scaleY / (float)s_bh, 0.f, 0.f };
        dev->SetPixelShaderConstantF(0, cy, 1);
        dev->SetTexture(0, s_texB);

        if (!last) {
            dev->SetPixelShaderConstantF(1, neutral, 1);
            dev->SetRenderTarget(0, s_surfA);
            dev->SetViewport(&vpGlow);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadGlow, sizeof(BloomVert));
        } else {
            const float c1[4] = { amount, amount, amount, sat };
            dev->SetPixelShaderConstantF(1, c1, 1);
            dev->SetRenderTarget(0, bb);
            dev->SetViewport(&vpFull);
            // ONE + ONE: light added to light, which is what a glow is. Alpha is left alone
            // -- in integer hdr the backbuffer's alpha carries the engine's own dest-alpha
            // depth and other passes still read it.
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                                D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadFull, sizeof(BloomVert));
        }
    }

    BloomRestore(dev, st);

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("^FX: bloom drew %dx%d glow %dx%d passes=%d intensity=%.2f threshold=%.2f",
                 s_w, s_h, s_bw, s_bh, passes, amount, g_bloomThreshold);
    }
}

bool Bloom_Frame(IDirect3DDevice9* dev, bool forceBackbuffer)
{
    if (s_done || !dev || !g_bloomEnable || s_failed) return false;
    if (g_bloomIntensity <= 0.001f) { s_done = true; return false; }
    if (!IsInGame()) return false;              // the main menu is left alone

    // Only the scene whose target IS the backbuffer, unless the caller says the frame is
    // already finished. Reflection, refraction and rendertarget cameras go through the same
    // device, and a glow built from one of those is built from the wrong picture.
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        ok = forceBackbuffer || (rt == bb);

    bool drew = false;
    if (ok && BloomEnsure(dev, bb)) {
        BloomDraw(dev, bb);
        s_done = true;
        drew   = true;
    }

    if (rt) rt->Release();
    if (bb) bb->Release();
    return drew;
}
