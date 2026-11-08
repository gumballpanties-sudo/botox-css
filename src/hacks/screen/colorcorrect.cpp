#include "colorcorrect.h"
#include "shaders/fx_grade.h"   // k_fxGradeBytes -- compiled from fx_grade.hlsl by fxc
#include "screenfx.h"           // shared vs_3_0 quad (ps_3_0 cannot pair with FF)
#include <algorithm>
#include <cmath>

// ==== curves =======================================================================
// Monotone cubic Hermite (Fritsch-Carlson). A plain Catmull-Rom through the same points
// overshoots between them, and in a grade an overshoot is a halo or a clipped highlight
// the user never asked for. This one cannot leave the range its own control points span.

void CcResetCurve(CcCurve& c)
{
    c.n = 2;
    c.x[0] = 0.f; c.y[0] = 0.f;
    c.x[1] = 1.f; c.y[1] = 1.f;
}

bool CcCurveIsIdentity(const CcCurve& c)
{
    return c.n == 2 && c.x[0] == 0.f && c.y[0] == 0.f && c.x[1] == 1.f && c.y[1] == 1.f;
}

float CcEval(const CcCurve& c, float x)
{
    const int n = (c.n < 2) ? 2 : (c.n > k_ccMaxPts ? k_ccMaxPts : c.n);
    x = (x < 0.f) ? 0.f : (x > 1.f ? 1.f : x);
    if (x <= c.x[0])     return c.y[0];
    if (x >= c.x[n - 1]) return c.y[n - 1];

    // Secant slopes, then the tangents they imply.
    float d[k_ccMaxPts], m[k_ccMaxPts];
    for (int i = 0; i < n - 1; ++i)
    {
        const float h = c.x[i + 1] - c.x[i];
        d[i] = (h > 1e-6f) ? (c.y[i + 1] - c.y[i]) / h : 0.f;
    }
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (int i = 1; i < n - 1; ++i)
        m[i] = (d[i - 1] * d[i] <= 0.f) ? 0.f : (d[i - 1] + d[i]) * 0.5f;

    // Fritsch-Carlson limiter: keep every tangent inside the circle of radius 3 around
    // its segment's secant. This is the step that makes the result monotone.
    for (int i = 0; i < n - 1; ++i)
    {
        if (d[i] == 0.f) { m[i] = 0.f; m[i + 1] = 0.f; continue; }
        const float a = m[i] / d[i], b = m[i + 1] / d[i];
        const float s = a * a + b * b;
        if (s > 9.f)
        {
            const float t = 3.f / sqrtf(s);
            m[i]     = t * a * d[i];
            m[i + 1] = t * b * d[i];
        }
    }

    int i = 0;
    while (i < n - 2 && x > c.x[i + 1]) ++i;
    const float h = c.x[i + 1] - c.x[i];
    if (h <= 1e-6f) return c.y[i + 1];

    const float t  = (x - c.x[i]) / h;
    const float t2 = t * t, t3 = t2 * t;
    const float h00 =  2.f * t3 - 3.f * t2 + 1.f;
    const float h10 =        t3 - 2.f * t2 + t;
    const float h01 = -2.f * t3 + 3.f * t2;
    const float h11 =        t3 -       t2;
    const float y = h00 * c.y[i] + h10 * h * m[i] + h01 * c.y[i + 1] + h11 * h * m[i + 1];
    return (y < 0.f) ? 0.f : (y > 1.f ? 1.f : y);
}

// A point may not cross its neighbours: its x is clamped into the gap between them, so a
// drag never reorders the list. Degenerate (zero-width) segments are impossible too,
// which is what keeps the tangent solve above well conditioned.
inline constexpr float k_ccMinDx = 1.f / 128.f;

void CcMovePoint(CcCurve& c, int i, float x, float y)
{
    if (i < 0 || i >= c.n) return;
    if (y < 0.f) y = 0.f; else if (y > 1.f) y = 1.f;
    c.y[i] = y;

    if (i == 0)          { c.x[0] = 0.f; return; }   // endpoints slide vertically only
    if (i == c.n - 1)    { c.x[i] = 1.f; return; }

    const float lo = c.x[i - 1] + k_ccMinDx;
    const float hi = c.x[i + 1] - k_ccMinDx;
    c.x[i] = (lo >= hi) ? c.x[i] : ((x < lo) ? lo : (x > hi ? hi : x));
}

int CcAddPoint(CcCurve& c, float x, float y)
{
    if (c.n >= k_ccMaxPts) return -1;
    if (x <= c.x[0] || x >= c.x[c.n - 1]) return -1;

    int i = 1;
    while (i < c.n && c.x[i] < x) ++i;          // insert before the first point past x
    if (x - c.x[i - 1] < k_ccMinDx || c.x[i] - x < k_ccMinDx) return -1;

    for (int k = c.n; k > i; --k) { c.x[k] = c.x[k - 1]; c.y[k] = c.y[k - 1]; }
    c.x[i] = x;
    c.y[i] = (y < 0.f) ? 0.f : (y > 1.f ? 1.f : y);
    ++c.n;
    return i;
}

void CcRemovePoint(CcCurve& c, int i)
{
    if (c.n <= 2) return;                       // the two endpoints always stay
    if (i <= 0 || i >= c.n - 1) return;
    for (int k = i; k < c.n - 1; ++k) { c.x[k] = c.x[k + 1]; c.y[k] = c.y[k + 1]; }
    --c.n;
}

void CcResetAll()
{
    g_ccGainR = g_ccGainG = g_ccGainB = 1.f;
    g_ccExposure = 0.f;
    g_ccTemp     = 0.f;
    g_ccHue      = 0.f;
    g_ccSat      = 1.f;
    g_ccContrast = 1.f;
    g_ccLevInBlack  = 0.f; g_ccLevInWhite  = 1.f;
    g_ccGamma       = 1.f;
    g_ccLevOutBlack = 0.f; g_ccLevOutWhite = 1.f;
    g_ccHighlights  = 0.f; g_ccShadows     = 0.f;
    for (int i = 0; i < 4; ++i) CcResetCurve(g_ccCurve[i]);
}

// Exact compares are the right test: every default below is an exact float and any touch
// of a slider moves it off that value. Neutral means the whole pass -- both fullscreen
// passes of it -- is skipped, so a ticked checkbox on default settings costs nothing.
bool CcIsNeutral()
{
    if (g_ccGainR != 1.f || g_ccGainG != 1.f || g_ccGainB != 1.f) return false;
    if (g_ccExposure != 0.f || g_ccTemp != 0.f || g_ccHue != 0.f) return false;
    if (g_ccSat != 1.f || g_ccContrast != 1.f)                    return false;
    if (g_ccLevInBlack != 0.f || g_ccLevInWhite != 1.f)           return false;
    if (g_ccGamma != 1.f)                                         return false;
    if (g_ccLevOutBlack != 0.f || g_ccLevOutWhite != 1.f)         return false;
    if (g_ccHighlights != 0.f || g_ccShadows != 0.f)              return false;
    for (int i = 0; i < 4; ++i) if (!CcCurveIsIdentity(g_ccCurve[i])) return false;
    return true;
}

// ==== device objects ================================================================
// botox color_correction.cpp 1:1: the grade AND sharpen / film grain / vignette / invert /
// posterize / deband / channel shift are one shader over one copy of the finished frame
// (fx_grade.hlsl). screenfx_passes.cpp's old simplefx pass is gone; its entry is a no-op.
static IDirect3DPixelShader9* s_shader   = nullptr;
static IDirect3DTexture9*     s_sceneTex = nullptr;   // backbuffer copy, sampled by the pass
static IDirect3DSurface9*     s_sceneSurf= nullptr;   // level 0 of s_sceneTex
static IDirect3DTexture9*     s_lutTex   = nullptr;   // 256x1 curves (master, then R/G/B)
static int  s_texW = 0, s_texH = 0;
static bool s_done = false;                           // graded this frame already
static bool s_failed = false;                         // the card refused the shader: never retry
static unsigned s_grainFrame = 0;

// What the baked LUT was built from. Compared with memcmp, so it is only ever written
// with memcpy -- padding bytes then match too.
struct CcLutKey { CcCurve cv[4]; };
static CcLutKey s_lutKey;
static bool     s_lutValid = false;

void ColorCorr_NewFrame() { s_done = false; }

void ColorCorr_Destroy()
{
    if (s_lutTex)    { s_lutTex->Release();    s_lutTex    = nullptr; }
    if (s_sceneSurf) { s_sceneSurf->Release(); s_sceneSurf = nullptr; }
    if (s_sceneTex)  { s_sceneTex->Release();  s_sceneTex  = nullptr; }
    if (s_shader)    { s_shader->Release();    s_shader    = nullptr; }
    s_texW = s_texH = 0;
    s_lutValid = false;
}

// botox grade_flags(): which stages run. A stage off sends identity constants (see Draw).
struct CcFlags { bool temp, levels, hue, curves, tone, basic, sharpen, grain, vignette, invert, posterize, deband, shift; };

static CcFlags CcGradeFlags()
{
    CcFlags f{};
    f.sharpen   = g_sharpenEnable && g_sfxSharpenStrength > 0.f;
    f.grain     = g_filmGrainEnable && g_sfxGrainIntensity > 0.f;
    f.vignette  = g_vignetteEnable && (g_sfxVignetteColor[3] > 0.f || g_sfxVignetteBlur > 0.f);
    f.invert    = g_invertFilterEnable && g_sfxInvertStrength > 0.f;
    f.posterize = g_posterizeEnable && g_sfxPosterizeStrength > 0.f;
    f.deband    = g_debandEnable;
    // depth based shift is drawn by the depth distort pass instead (botox 63c5358)
    f.shift     = g_channelShiftEnable && !g_channelShiftDepth &&
                  (g_sfxShiftRedX != 0.f || g_sfxShiftRedY != 0.f || g_sfxShiftGreenX != 0.f ||
                   g_sfxShiftGreenY != 0.f || g_sfxShiftBlueX != 0.f || g_sfxShiftBlueY != 0.f);
    if (!g_ccEnable) return f;

    f.temp   = g_ccTemp != 0.f;
    f.levels = g_ccLevInBlack != 0.f || g_ccLevInWhite != 1.f || g_ccGamma != 1.f;
    f.hue    = g_ccHue != 0.f;
    for (int i = 0; i < 4; ++i) if (!CcCurveIsIdentity(g_ccCurve[i])) f.curves = true;
    f.tone   = g_ccHighlights != 0.f || g_ccShadows != 0.f;
    f.basic  = g_ccGainR != 1.f || g_ccGainG != 1.f || g_ccGainB != 1.f || g_ccExposure != 0.f ||
               g_ccSat != 1.f || g_ccContrast != 1.f || g_ccLevOutBlack != 0.f || g_ccLevOutWhite != 1.f;
    return f;
}

static bool CcAny(const CcFlags& f)
{
    return f.temp || f.levels || f.hue || f.curves || f.tone || f.basic || f.sharpen || f.grain ||
           f.vignette || f.invert || f.posterize || f.deband || f.shift;
}

// Sized to the backbuffer; a resolution change comes through h_Reset, which destroys
// everything, but the size check here also covers a swapchain that changed under us.
static bool CcEnsureDevice(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    D3DSURFACE_DESC desc{};
    if (FAILED(bb->GetDesc(&desc))) return false;

    if (s_shader && s_sceneTex && s_lutTex &&
        s_texW == (int)desc.Width && s_texH == (int)desc.Height)
        return true;

    ColorCorr_Destroy();

    // Bytecode is compiled at build time (fx_grade.hlsl -> fx_grade.h): no d3dx9 at runtime.
    // Copied into DWORDs because a BYTE array carries no alignment guarantee.
    {
        std::vector<DWORD> code((sizeof(k_fxGradeBytes) + 3) / 4);
        memcpy(code.data(), k_fxGradeBytes, sizeof(k_fxGradeBytes));
        HRESULT hr = dev->CreatePixelShader(code.data(), &s_shader);
        if (FAILED(hr) || !s_shader) {
            s_shader = nullptr;
            s_failed = true;
            DebugLog("color correction: the card refused the grade shader 0x%08lx (ps_3_0, ~552 slots) -- "
                     "grade / sharpen / grain / vignette / filters are off", (unsigned long)hr);
            return false;
        }
    }

    // StretchRect needs a render-target destination in the default pool. Matching the
    // backbuffer format is what makes the blit a straight copy; X8R8G8B8 is the fallback
    // for the rare backbuffer format that cannot back a render-target texture.
    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                    desc.Format, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr)) { ColorCorr_Destroy(); return false; }
    if (FAILED(s_sceneTex->GetSurfaceLevel(0, &s_sceneSurf))) { ColorCorr_Destroy(); return false; }

    // DYNAMIC so the rebuild is a DISCARD lock. Default pool, not managed: managed is
    // illegal on a D3D9Ex device and TF2 can be started with -d3d9ex.
    hr = dev->CreateTexture(256, 1, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                            D3DPOOL_DEFAULT, &s_lutTex, nullptr);
    if (FAILED(hr)) { ColorCorr_Destroy(); return false; }

    s_texW = (int)desc.Width;
    s_texH = (int)desc.Height;
    s_lutValid = false;
    DebugLog("color correction: built %dx%d", s_texW, s_texH);
    return true;
}

// botox update_lut: curves only (master maps first, each channel maps the master's output).
// Levels out is the BASIC stage's c6 now, as in botox. Returns false while the table has
// never been built: a default-pool texture comes back with undefined contents.
static bool CcUpdateLut()
{
    CcLutKey key;
    memcpy(key.cv, g_ccCurve, sizeof(key.cv));
    if (s_lutValid && memcmp(&key, &s_lutKey, sizeof(key)) == 0) return true;

    D3DLOCKED_RECT lr{};
    if (FAILED(s_lutTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) return s_lutValid;

    // Baked from the snapshot, never from the globals: the menu edits these on the Present
    // thread while this runs on the render thread, and a table built half from one state and
    // half from the next would be stamped with a key that matches neither.
    uint8_t* row = (uint8_t*)lr.pBits;
    for (int i = 0; i < 256; ++i)
    {
        const float m = CcEval(key.cv[0], i / 255.f);
        for (int ch = 0; ch < 3; ++ch)
            row[i * 4 + (2 - ch)] = (uint8_t)(CcEval(key.cv[1 + ch], m) * 255.f + 0.5f);   // B,G,R,A
        row[i * 4 + 3] = 255;
    }
    s_lutTex->UnlockRect(0);

    memcpy(&s_lutKey, &key, sizeof(key));
    s_lutValid = true;
    return true;
}

// botox compute_hue_matrix: Rodrigues rotation about the luminance axis.
static void CcHueMat(float theta, float m[3][3])
{
    const float c = cosf(theta), s = sinf(theta), ic = 1.f - c;
    const float lx = 0.213f, ly = 0.715f, lz = 0.072f;
    m[0][0] = c + ic*lx*lx;    m[0][1] = ic*lx*ly - s*lz;  m[0][2] = ic*lx*lz + s*ly;
    m[1][0] = ic*ly*lx + s*lz; m[1][1] = c + ic*ly*ly;     m[1][2] = ic*ly*lz - s*lx;
    m[2][0] = ic*lz*lx - s*ly; m[2][1] = ic*lz*ly + s*lx;  m[2][2] = c + ic*lz*lz;
}

// Everything the pass touches is saved and put back (botox's list + the bool constants the
// stages branch on). DrawPrimitiveUP nulls stream source 0 and the index buffer behind it.
struct CcSavedState
{
    IDirect3DSurface9*           ds;
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex[2];
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DVIEWPORT9 vp;
    DWORD  rs[13];
    DWORD  samp[2][6];
    float  psc[19][4];
    BOOL   psb[12];
};

static const D3DRENDERSTATETYPE k_ccRS[13] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE,
    D3DRS_FILLMODE,
};
static const DWORD k_ccRSVal[13] = {
    FALSE, FALSE, FALSE, FALSE,
    FALSE, D3DCULL_NONE, FALSE, FALSE,
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE |
        D3DCOLORWRITEENABLE_ALPHA,
    FALSE, FALSE, 0,
    D3DFILL_SOLID,
};
static const D3DSAMPLERSTATETYPE k_ccSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void CcSave(IDirect3DDevice9* dev, CcSavedState& s)
{
    if (FAILED(dev->GetDepthStencilSurface(&s.ds))) s.ds = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex[0]);
    dev->GetTexture(1, &s.tex[1]);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 13; ++i) dev->GetRenderState(k_ccRS[i], &s.rs[i]);
    for (int u = 0; u < 2; ++u)
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_ccSamp[i], &s.samp[u][i]);
    dev->GetPixelShaderConstantF(0, s.psc[0], 19);
    dev->GetPixelShaderConstantB(0, s.psb, 12);
}

static void CcRestore(IDirect3DDevice9* dev, CcSavedState& s)
{
    for (int i = 0; i < 13; ++i) dev->SetRenderState(k_ccRS[i], s.rs[i]);
    for (int u = 0; u < 2; ++u)
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_ccSamp[i], s.samp[u][i]);
    dev->SetViewport(&s.vp);
    dev->SetPixelShaderConstantF(0, s.psc[0], 19);
    dev->SetPixelShaderConstantB(0, s.psb, 12);
    dev->SetTexture(0, s.tex[0]);
    dev->SetTexture(1, s.tex[1]);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    // SetFVF and SetVertexDeclaration write the same slot, so only the one that was in
    // force goes back: a decl if there was one, otherwise the FVF.
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);
    dev->SetDepthStencilSurface(s.ds);

    for (IUnknown* held : { (IUnknown*)s.ds, (IUnknown*)s.ps, (IUnknown*)s.vs, (IUnknown*)s.decl,
                            (IUnknown*)s.tex[0], (IUnknown*)s.tex[1], (IUnknown*)s.stream, (IUnknown*)s.indices })
        if (held) held->Release();
}

static void CcDraw(IDirect3DDevice9* dev, IDirect3DSurface9* bb, const CcFlags& f)
{
    // One copy out, one draw back: the render target is already the backbuffer.
    if (FAILED(dev->StretchRect(bb, nullptr, s_sceneSurf, nullptr, D3DTEXF_NONE)))
    {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("color correction: back buffer blit failed"); }
        return;
    }

    const float width = (float)s_texW, height = (float)s_texH;

    // ---- constants, botox on_end_scene_post 1:1. Stages that are not branched in the shader
    // get their identity here while off.
    const float exposure = f.basic ? powf(2.f, g_ccExposure) : 1.f;
    const float c0[4] = { f.basic ? g_ccGainR : 1.f, f.basic ? g_ccGainG : 1.f, f.basic ? g_ccGainB : 1.f,
                          f.basic ? g_ccContrast : 1.f };
    const float c1[4] = { f.basic ? g_ccSat : 1.f, f.temp ? g_ccTemp : 0.f, exposure, 0.f };

    float hm[3][3] = { { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } };
    if (f.hue) CcHueMat(g_ccHue * (3.14159265f / 180.f), hm);

    const float inB   = g_ccLevInBlack;
    const float inW   = (std::max)(g_ccLevInWhite, inB + 0.001f);
    const float gamma = g_ccGamma;
    const float c5[4] = { inB, inW, gamma > 0.001f ? 1.f / gamma : 1.f, 0.f };
    const float outB  = f.basic ? g_ccLevOutBlack : 0.f;
    const float c6[4] = { outB, f.basic ? g_ccLevOutWhite - outB : 1.f,
                          f.tone ? g_ccHighlights : 0.f, f.tone ? g_ccShadows : 0.f };

    const float c13[4] = { 1.f / width, 1.f / height, g_sfxSharpenStrength, g_sfxSharpenThreshold };

    const unsigned grainFrame = s_grainFrame++;
    const float grainAngle = fmodf((float)(grainFrame % 4096u) * 2.39996323f, 6.28318531f);
    const float grainSize  = (std::max)(g_sfxGrainSize, 0.5f);
    const float c14[4] = { cosf(grainAngle), sinf(grainAngle), (float)(grainFrame % 256u) / 256.f + 0.5f / 256.f,
                           g_sfxGrainIntensity };
    const float c15[4] = { width / grainSize, height / grainSize, width / height, 0.f };
    // range floor: 1 / 0 would be inf * 0 = nan at the position
    const float c12[4] = { g_sfxGrainRespPos, 1.f / (std::max)(g_sfxGrainRespRange, 0.01f), g_sfxGrainRespMin, 0.f };

    const float vigAspect = (std::max)(g_sfxVignetteAspect, 0.1f);
    const float vigBlur   = f.vignette ? (std::max)(g_sfxVignetteBlur, 0.f) : 0.f;
    const float c7[4] = { g_sfxVignetteRadius, g_sfxVignetteRadius + (std::max)(g_sfxVignetteFeather, 0.001f),
                          (width / height) / vigAspect, g_vignetteInvert ? 1.f : 0.f };
    const float c8[4] = { g_sfxVignetteColor[0], g_sfxVignetteColor[1], g_sfxVignetteColor[2], g_sfxVignetteColor[3] };

    // deband tap growth: 12 taps from 1 px, r = max(r + 1, r * q), q bisected so tap 12 lands on 5 * radius
    const float dbRadius = std::clamp(g_sfxDebandRadius, 4.f, 48.f);
    const int   dbRays   = std::clamp(g_sfxDebandIterations, 1, 4) * 4;
    float dbLo = 1.f, dbHi = 4.f;
    for (int pass = 0; pass < 32; pass++) {
        const float growth = (dbLo + dbHi) * 0.5f;
        float reach = 1.f;
        for (int tap = 1; tap < 12; tap++) reach = (std::max)(reach + 1.f, reach * growth);
        (reach < 5.f * dbRadius ? dbLo : dbHi) = growth;
    }
    // same r walk + weight as the shader, summed so it can multiply instead of tracking dw
    const float dbGrowth = (dbLo + dbHi) * 0.5f;
    float dbWeight = 0.f, dbReach = 1.f, dbLast = 0.f;
    for (int tap = 0; tap < 12; tap++) {
        dbWeight += sqrtf(dbReach) * exp2f(-dbReach * 1.4426950f / dbRadius) * (dbReach - dbLast);
        dbLast  = dbReach;
        dbReach = (std::max)(dbReach + 1.f, dbReach * dbGrowth);
    }
    const float dbTurn = 6.28318531f / (float)dbRays;

    const float c9[4] = { vigBlur, vigBlur > 0.f ? 1.f : 0.f, cosf(dbTurn), sinf(dbTurn) };

    // shift off = zero offsets: the shader's src() then reads the same texel three times
    const float c10[4] = { f.shift ? g_sfxShiftRedX / width : 0.f,   f.shift ? g_sfxShiftRedY / height : 0.f,
                           f.shift ? g_sfxShiftGreenX / width : 0.f, f.shift ? g_sfxShiftGreenY / height : 0.f };
    const float c11[4] = { f.shift ? g_sfxShiftBlueX / width : 0.f,  f.shift ? g_sfxShiftBlueY / height : 0.f, width, height };

    // x = boris thres (strength * 0.001); y = log2e / radius; z = dither, 16 = 1 step; w = ign temporal offset
    const float c16[4] = { std::clamp(g_sfxDebandStrength, 1.f, 50.f) * 0.001f, 1.4426950f / dbRadius,
                           g_sfxDebandGrain / (16.f * 255.f), (float)(grainFrame % 64u) * 5.588238f };
    const float posterSteps = (float)(std::clamp(g_sfxPosterizeLevels, 2, 64) - 1);
    const float c17[4] = { (float)dbRays, posterSteps, g_sfxPosterizeDither, g_sfxPosterizeStrength };
    const float c18[4] = { g_sfxInvertStrength, g_sfxInvertMode == 1 ? 1.f : 0.f, dbGrowth,
                           1.f / ((float)dbRays * dbWeight) };

    const float block[19][4] = {
        { c0[0], c0[1], c0[2], c0[3] }, { c1[0], c1[1], c1[2], c1[3] },
        { hm[0][0], hm[0][1], hm[0][2], 0.f }, { hm[1][0], hm[1][1], hm[1][2], 0.f }, { hm[2][0], hm[2][1], hm[2][2], 0.f },
        { c5[0], c5[1], c5[2], c5[3] }, { c6[0], c6[1], c6[2], c6[3] }, { c7[0], c7[1], c7[2], c7[3] },
        { c8[0], c8[1], c8[2], c8[3] }, { c9[0], c9[1], c9[2], c9[3] }, { c10[0], c10[1], c10[2], c10[3] },
        { c11[0], c11[1], c11[2], c11[3] }, { c12[0], c12[1], c12[2], c12[3] }, { c13[0], c13[1], c13[2], c13[3] },
        { c14[0], c14[1], c14[2], c14[3] }, { c15[0], c15[1], c15[2], c15[3] }, { c16[0], c16[1], c16[2], c16[3] },
        { c17[0], c17[1], c17[2], c17[3] }, { c18[0], c18[1], c18[2], c18[3] },
    };
    // fx_grade.hlsl bool registers = botox's grade_* bit numbers
    const BOOL bools[12] = { FALSE, f.levels, FALSE, f.curves, FALSE, FALSE,
                             f.sharpen, f.grain, f.vignette, f.invert, f.posterize, f.deband };

    CcSavedState st{};
    CcSave(dev, st);

    dev->SetDepthStencilSurface(nullptr);
    for (int i = 0; i < 13; ++i) dev->SetRenderState(k_ccRS[i], k_ccRSVal[i]);
    const D3DVIEWPORT9 vp{ 0, 0, (DWORD)s_texW, (DWORD)s_texH, 0.f, 1.f };
    dev->SetViewport(&vp);
    dev->SetRenderTarget(0, bb);
    dev->SetPixelShader(s_shader);
    ScreenFx_BindQuad(dev);

    // Textures first, then their sampler states. Sampling has to be raw: an sRGB decode here
    // would grade a different image than the one on screen.
    dev->SetTexture(0, s_sceneTex);
    dev->SetTexture(1, f.curves ? s_lutTex : nullptr);
    for (int u = 0; u < 2; ++u)
    {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
    }
    dev->SetPixelShaderConstantF(0, block[0], 19);
    dev->SetPixelShaderConstantB(0, bools, 12);

    ScreenFx_DrawQuad(dev, s_texW, s_texH);

    CcRestore(dev, st);

    static unsigned s_loggedMask = 0xffffffffu;
    const unsigned mask = (f.temp << 0) | (f.levels << 1) | (f.hue << 2) | (f.curves << 3) | (f.tone << 4) |
                          (f.basic << 5) | (f.sharpen << 6) | (f.grain << 7) | (f.vignette << 8) |
                          (f.invert << 9) | (f.posterize << 10) | (f.deband << 11) | (f.shift << 12);
    if (mask != s_loggedMask) {
        s_loggedMask = mask;
        DebugLog("color correction: grade pass drew %dx%d, stages 0x%x", s_texW, s_texH, mask);
    }
}

bool ColorCorr_Frame(IDirect3DDevice9* dev)
{
    if (s_done || !dev || s_failed) return false;
    if (!IsInGame()) return false;              // the main menu is not graded
    const CcFlags f = CcGradeFlags();
    if (!CcAny(f)) { s_done = true; return false; }
    if (!ScreenFx_QuadReady(dev)) return false; // ps_3_0: the shared vs_3_0 quad, not FF

    // Only the scene whose target IS the backbuffer. Reflection, refraction and rendertarget
    // passes go through the same device, and grading those would put the correction inside
    // water reflections and camera monitors.
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = (rt == bb);

    bool graded = false;
    if (onBB && CcEnsureDevice(dev, bb) && (!f.curves || CcUpdateLut()))
    {
        CcDraw(dev, bb, f);
        s_done = true;
        graded = true;
    }

    if (rt) rt->Release();
    if (bb) bb->Release();

    return graded;
}
