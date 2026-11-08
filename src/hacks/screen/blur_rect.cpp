#include "blur_rect.h"
#include "shaders/fx_bloom_blur.h"   // k_fxBloomBlurBytes -- same compiled shader bloom.cpp uses
#include <algorithm>

// ==== per-frame job queue ========================================================
// Fixed array, not a vector: AddCallback keeps the pointer until the draw list is replayed
// later this same frame, and a vector could reallocate and move it out from under that.
struct BlurJob { ImVec2 min, max; float strength; float rounding; float alpha = 1.f; };
static constexpr int kMaxJobsPerFrame = 16;   // ponytail: fixed cap, bump if a menu ever blurs more rects in one frame
static BlurJob s_jobs[kMaxJobsPerFrame];
static int     s_jobCount = 0;

// ==== scratch render targets (device resources, sized to the biggest rect this cache has seen) ====
static IDirect3DPixelShader9* s_ps    = nullptr;
static IDirect3DTexture9*     s_texA  = nullptr;
static IDirect3DSurface9*     s_surfA = nullptr;
static IDirect3DTexture9*     s_texB  = nullptr;
static IDirect3DSurface9*     s_surfB = nullptr;
static int s_bufW = 0, s_bufH = 0;
static bool s_failed = false;

void BlurRect_Destroy()
{
    if (s_surfB) { s_surfB->Release(); s_surfB = nullptr; }
    if (s_texB)  { s_texB->Release();  s_texB  = nullptr; }
    if (s_surfA) { s_surfA->Release(); s_surfA = nullptr; }
    if (s_texA)  { s_texA->Release();  s_texA  = nullptr; }
    if (s_ps)    { s_ps->Release();    s_ps    = nullptr; }
    s_bufW = s_bufH = 0;
}

static bool BlurEnsure(IDirect3DDevice9* dev, int w, int h)
{
    if (s_failed) return false;
    // Grow-only. Was "== w && == h": two panels of different size in one frame (watermark +
    // spectator list), or a watermark whose fps/time text changed width, recreated both
    // targets AND the shader every time -- a CreateTexture stall per frame. The rect is
    // stretched over the whole buffer instead (StretchRect dst = full surface, UVs 0..1), so
    // clamp edges and the per-pass UV step (1/rect px) are unchanged.
    // ponytail: grows to the biggest rect seen (boot screen = 1024^2) and stays there; bucket
    // per size if the fill cost of a small panel on a 1024^2 buffer ever shows up.
    if (s_ps && s_texA && s_texB && w <= s_bufW && h <= s_bufH) return true;
    w = (std::max)(w, s_bufW);
    h = (std::max)(h, s_bufH);

    BlurRect_Destroy();

    std::vector<DWORD> code((sizeof(k_fxBloomBlurBytes) + 3) / 4);
    memcpy(code.data(), k_fxBloomBlurBytes, sizeof(k_fxBloomBlurBytes));
    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps)) || !s_ps)
    {
        s_failed = true;
        DebugLog("blur_rect: the card refused the blur shader -- panel blur is off");
        return false;
    }

    if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_texA, nullptr)) ||
        FAILED(s_texA->GetSurfaceLevel(0, &s_surfA)) ||
        FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_texB, nullptr)) ||
        FAILED(s_texB->GetSurfaceLevel(0, &s_surfB)))
    {
        DebugLog("blur_rect: scratch target %dx%d failed", w, h);
        BlurRect_Destroy();
        return false;
    }

    s_bufW = w; s_bufH = h;
    return true;
}

// ==== state save/restore -- same technique bloom.cpp uses: ask the device what was bound
// BEFORE we touch anything, put exactly that back after. Correct regardless of who the
// caller is, which is why this works fine called from inside ImGui_ImplDX9_RenderDrawData.
struct BlurSaved
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
    DWORD  rs[14];
    DWORD  samp[6];
    float  psc[2][4];
};

static const D3DRENDERSTATETYPE k_brRS[14] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE,
    D3DRS_FILLMODE, D3DRS_SRCBLEND,
};
static const DWORD k_brRSVal[14] = {
    FALSE, FALSE, FALSE, FALSE,
    FALSE, D3DCULL_NONE, FALSE, FALSE,
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA,
    FALSE, FALSE, 0,
    D3DFILL_SOLID, D3DBLEND_ONE,
};
static const D3DSAMPLERSTATETYPE k_brSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void BlurSave(IDirect3DDevice9* dev, BlurSaved& s)
{
    if (FAILED(dev->GetRenderTarget(0, &s.rt)))     s.rt = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&s.ds))) s.ds = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 14; ++i) dev->GetRenderState(k_brRS[i], &s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_brSamp[i], &s.samp[i]);
    dev->GetPixelShaderConstantF(0, s.psc[0], 2);
}

static void BlurRestore(IDirect3DDevice9* dev, BlurSaved& s)
{
    dev->SetRenderTarget(0, s.rt);
    dev->SetDepthStencilSurface(s.ds);
    dev->SetViewport(&s.vp);
    for (int i = 0; i < 14; ++i) dev->SetRenderState(k_brRS[i], s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_brSamp[i], s.samp[i]);
    dev->SetPixelShaderConstantF(0, s.psc[0], 2);
    dev->SetTexture(0, s.tex0);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    if (s.rt)      s.rt->Release();
    if (s.ds)      s.ds->Release();
    if (s.tex0)    s.tex0->Release();
    if (s.ps)      s.ps->Release();
    if (s.vs)      s.vs->Release();
    if (s.decl)    s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

struct BlurVert { float x, y, z, rhw, u, v; };

// Local space (0..w, 0..h) -- used for the ping-pong passes between the scratch targets (whole buffer).
static void BlurQuadLocal(BlurVert (&q)[4], float w, float h)
{
    q[0] = { -0.5f,    -0.5f,     0.f, 1.f, 0.f, 0.f };
    q[1] = { w - 0.5f, -0.5f,     0.f, 1.f, 1.f, 0.f };
    q[2] = { -0.5f,     h - 0.5f, 0.f, 1.f, 0.f, 1.f };
    q[3] = { w - 0.5f,  h - 0.5f, 0.f, 1.f, 1.f, 1.f };
}

// Absolute screen space -- used for the final pass, which draws straight onto the saved
// render target at the panel's own position.
static void BlurQuadAbs(BlurVert (&q)[4], float x0, float y0, float x1, float y1)
{
    q[0] = { x0 - 0.5f, y0 - 0.5f, 0.f, 1.f, 0.f, 0.f };
    q[1] = { x1 - 0.5f, y0 - 0.5f, 0.f, 1.f, 1.f, 0.f };
    q[2] = { x0 - 0.5f, y1 - 0.5f, 0.f, 1.f, 0.f, 1.f };
    q[3] = { x1 - 0.5f, y1 - 0.5f, 0.f, 1.f, 1.f, 1.f };
}

static void BlurRectDraw(IDirect3DDevice9* dev, ImVec2 mn, ImVec2 mx, float strength, float alpha = 1.f)
{
    const int rw = (int)(mx.x - mn.x), rh = (int)(mx.y - mn.y);
    if (rw < 2 || rh < 2) return;
    // Scratch caps at 1024: a bigger rect (boot screen = whole screen) is downsampled into it
    // and stretched back, like botox's small-texture fullscreen blur. Was cropped to 1024 px,
    // which left the right side of a 1920 screen unblurred.
    const int sw = (std::min)(rw, 1024), sh = (std::min)(rh, 1024);

    if (!BlurEnsure(dev, sw, sh)) return;

    IDirect3DSurface9* rtSaved = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &rtSaved)) || !rtSaved) return;

    const RECT src{ (LONG)mn.x, (LONG)mn.y, (LONG)mn.x + rw, (LONG)mn.y + rh };
    if (FAILED(dev->StretchRect(rtSaved, &src, s_surfA, nullptr, D3DTEXF_LINEAR))) { rtSaved->Release(); return; }

    BlurSaved st{};
    BlurSave(dev, st);

    for (int i = 0; i < 14; ++i) dev->SetRenderState(k_brRS[i], k_brRSVal[i]);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
    dev->SetPixelShader(s_ps);

    const float str    = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
    const int   passes = 1 + (int)(str * 5.f + 0.5f);   // 1..6, same radius->pass map bloom uses
    const float neutral[4] = { 1.f, 1.f, 1.f, 1.f };
    const D3DVIEWPORT9 vpRect{ 0, 0, (DWORD)s_bufW, (DWORD)s_bufH, 0.f, 1.f };

    BlurVert qLocal[4]; BlurQuadLocal(qLocal, (float)s_bufW, (float)s_bufH);
    BlurVert qAbs[4];   BlurQuadAbs(qAbs, mn.x, mn.y, mn.x + rw, mn.y + rh);

    for (int i = 0; i < passes; ++i)
    {
        const float step = (float)(i + 1);
        const bool  last = (i == passes - 1);

        // x: A -> B
        const float cx[4] = { step / (float)sw, 0.f, 0.f, 0.f };
        dev->SetPixelShaderConstantF(0, cx, 1);
        dev->SetPixelShaderConstantF(1, neutral, 1);
        dev->SetRenderTarget(0, s_surfB);
        dev->SetViewport(&vpRect);
        dev->SetTexture(0, s_texA);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qLocal, sizeof(BlurVert));

        // y: B -> A, or on the last pass B -> the panel's own position on the saved target
        const float cy[4] = { 0.f, step / (float)sh, 0.f, 0.f };
        dev->SetPixelShaderConstantF(0, cy, 1);
        dev->SetTexture(0, s_texB);
        dev->SetPixelShaderConstantF(1, neutral, 1);

        if (!last)
        {
            dev->SetRenderTarget(0, s_surfA);
            dev->SetViewport(&vpRect);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qLocal, sizeof(BlurVert));
        }
        else
        {
            dev->SetRenderTarget(0, rtSaved);
            dev->SetViewport(&st.vp);
            if (alpha >= 0.999f)
            {
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);   // straight replace, not bloom's additive
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qAbs, sizeof(BlurVert));
            }
            else
            {
                // BlurRectFade (unit game): botox screen_blur tint alpha = crossfade over the
                // sharp frame through the blend factor. SRCBLEND/ALPHABLEND go back in BlurRestore.
                DWORD dst = 0, op = 0, factor = 0;
                dev->GetRenderState(D3DRS_DESTBLEND, &dst);
                dev->GetRenderState(D3DRS_BLENDOP, &op);
                dev->GetRenderState(D3DRS_BLENDFACTOR, &factor);
                const DWORD a = (DWORD)((alpha < 0.f ? 0.f : alpha) * 255.f + 0.5f);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
                dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
                dev->SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(a, a, a, a));
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, qAbs, sizeof(BlurVert));
                dev->SetRenderState(D3DRS_DESTBLEND, dst);
                dev->SetRenderState(D3DRS_BLENDOP, op);
                dev->SetRenderState(D3DRS_BLENDFACTOR, factor);
            }
        }
    }

    BlurRestore(dev, st);
    rtSaved->Release();
}

static void BlurRectCallback(const ImDrawList*, const ImDrawCmd* cmd)
{
    const BlurJob* j = static_cast<const BlurJob*>(cmd->UserCallbackData);
    if (j && g_d3dDev) BlurRectDraw(g_d3dDev, j->min, j->max, j->strength, j->alpha);
}

void BlurRectFade(ImDrawList* dl, ImVec2 min, ImVec2 max, float strength, float alpha)
{
    if (!dl || alpha <= 0.f || s_jobCount >= kMaxJobsPerFrame) return;
    BlurJob& j = s_jobs[s_jobCount++];
    j.min = min; j.max = max; j.strength = strength; j.rounding = 0.f; j.alpha = alpha;
    dl->AddCallback(&BlurRectCallback, &j);
}

void BlurRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float strength, float rounding)
{
    if (!dl || s_jobCount >= kMaxJobsPerFrame) return;
    BlurJob& j = s_jobs[s_jobCount++];
    j.min = min; j.max = max; j.strength = strength; j.rounding = rounding;
    dl->AddCallback(&BlurRectCallback, &j);
}

void BlurRect_ScaleJob(const ImDrawCmd& cmd, ImVec2 pivot, float scale)
{
    if (cmd.UserCallback != &BlurRectCallback || !cmd.UserCallbackData) return;
    BlurJob& j = *static_cast<BlurJob*>(cmd.UserCallbackData);   // s_jobs: ours, mutable
    j.min = ImVec2(pivot.x + (j.min.x - pivot.x) * scale, pivot.y + (j.min.y - pivot.y) * scale);
    j.max = ImVec2(pivot.x + (j.max.x - pivot.x) * scale, pivot.y + (j.max.y - pivot.y) * scale);
}

void BlurRect_NewFrame() { s_jobCount = 0; }
