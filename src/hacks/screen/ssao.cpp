#include "screenfx.h"
#include "shaders/fx_ao_prepare.h"     // k_fxAoPrepareBytes
#include "shaders/fx_ao_occlusion.h"   // k_fxAoOcclusionBytes
#include "shaders/fx_ao_composite.h"   // k_fxAoCompositeBytes
#include "../hud/hud_snap.h"           // HudSnap_Read -- smoke centre/radius, game-thread producer
#include <algorithm>   // std::clamp -- smoke fade (fade under smoke)
#include <cmath>       // sqrtf, sinf/cosf

// Ambient occlusion -- corners and creases shaded from the real z buffer. Ported from
// coldtires (hacks/visuals/screen/ambient_occlusion.cpp), which models itself on qUINT_mxao.
//
// The engine has nothing to offer here, so this is our own, and it is the same shape of
// pass the depth source was built for: the shared INTZ copy of the z buffer goes in, three
// fullscreen ps_3_0 passes turn it into a shadow term multiplied onto the frame.
//
//   1. prepare      depth -> view-space normals, full res, rebuilt from four neighbours
//   2. occlusion    the gather, at the render scale, a golden-angle disc per pixel
//   3. composite    bilateral blur of the occlusion + hardware multiply onto the frame
//
// WHY THREE AND NOT MXAO'S FIVE: the reshade shader this is modelled on spends two passes
// on mip chains and a stencil cull that only pay off with indirect lighting on, and its
// blur runs twice because it hops resolutions. One 8-tap bilateral pass at full res is the
// same picture here.
//
// FOG. This runs after the world is drawn, and fog in a forward renderer is mixed in by
// every material's own shader -- there is no point in the frame where the picture exists
// and the fog does not, so no post pass can land underneath it. What CAN be done is to stop
// the occlusion where the fog has already taken over. dih reads no fog controller (nothing
// in this DLL walks the entity list for one), so the fade follows the fog THIS DLL is
// forcing when the fog override is on, and is a no-op otherwise. The plain distance fade
// carries the rest.

// ---- device objects -----------------------------------------------------------------
enum AoShader { AO_PREPARE = 0, AO_OCCLUSION, AO_COMPOSITE, AO_SHADER_MAX };

static IDirect3DPixelShader9* s_ps[AO_SHADER_MAX] = {};
static IDirect3DTexture9*     s_normalTex = nullptr;
static IDirect3DSurface9*     s_normalSurf = nullptr;
static IDirect3DTexture9*     s_occTex  = nullptr;
static IDirect3DSurface9*     s_occSurf = nullptr;
static int   s_w = 0, s_h = 0, s_occW = 0, s_occH = 0;
static float s_scale = 0.f;
static bool  s_shaderFailed = false;

void Ssao_Destroy()
{
    if (s_occSurf)    { s_occSurf->Release();    s_occSurf    = nullptr; }
    if (s_occTex)     { s_occTex->Release();     s_occTex     = nullptr; }
    if (s_normalSurf) { s_normalSurf->Release(); s_normalSurf = nullptr; }
    if (s_normalTex)  { s_normalTex->Release();  s_normalTex  = nullptr; }
    for (auto& ps : s_ps) { if (ps) { ps->Release(); ps = nullptr; } }
    s_w = s_h = s_occW = s_occH = 0;
    s_scale = 0.f;
}

static IDirect3DPixelShader9* AoShaderFor(IDirect3DDevice9* dev, AoShader which)
{
    if (s_ps[which]) return s_ps[which];
    if (s_shaderFailed) return nullptr;

    const BYTE* bytes = nullptr;
    size_t size = 0;
    switch (which) {
        case AO_PREPARE:   bytes = k_fxAoPrepareBytes;   size = sizeof(k_fxAoPrepareBytes);   break;
        case AO_OCCLUSION: bytes = k_fxAoOcclusionBytes; size = sizeof(k_fxAoOcclusionBytes); break;
        default:           bytes = k_fxAoCompositeBytes; size = sizeof(k_fxAoCompositeBytes); break;
    }

    std::vector<DWORD> code((size + 3) / 4);
    memcpy(code.data(), bytes, size);

    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps[which])) || !s_ps[which]) {
        s_ps[which] = nullptr;
        // Every pass is needed for a picture, so one failing takes the feature with it. On a
        // card with no ps_3_0 this is where the whole thing stops, once, with a line saying so.
        s_shaderFailed = true;
        DebugLog("ssao: pixel shader %d refused by the card -- ambient occlusion is off", (int)which);
        return nullptr;
    }
    DebugLog("ssao: shader %d built", (int)which);
    return s_ps[which];
}

// mathlib_base.cpp AngleVectors (pitch/yaw/roll, degrees) -- verified against source-sdk-2013,
// not guessed. Only "right" and "forward" are needed to place a world-space point into the AO
// composite shader's camera-local frame (fx_ao_composite.hlsl view_pos); "up" is kept too since
// a verified function is cheaper to use whole than to half-copy wrong.
static void SsaoAngleVectors(const float pitchYawRoll[3], float fwd[3], float right[3], float up[3])
{
    const float deg2rad = 3.14159265f / 180.f;
    const float sp = sinf(pitchYawRoll[0] * deg2rad), cp = cosf(pitchYawRoll[0] * deg2rad);
    const float sy = sinf(pitchYawRoll[1] * deg2rad), cy = cosf(pitchYawRoll[1] * deg2rad);
    const float sr = sinf(pitchYawRoll[2] * deg2rad), cr = cosf(pitchYawRoll[2] * deg2rad);
    fwd[0] = cp * cy;                      fwd[1] = cp * sy;                      fwd[2] = -sp;
    right[0] = -sr * sp * cy + -cr * -sy;  right[1] = -sr * sp * sy + -cr * cy;   right[2] = -sr * cp;
    up[0] = cr * sp * cy + -sr * -sy;      up[1] = cr * sp * sy + -sr * cy;       up[2] = cr * cp;
}

// Both targets are frame-sized (the gather's is a fraction of it), so a resolution change
// or a scale change rebuilds them. The shaders have no size baked in and are kept.
static bool AoBuild(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    const int   width  = (int)desc.Width;
    const int   height = (int)desc.Height;
    const float scale  = (g_aoScale < 0.25f) ? 0.25f : (g_aoScale > 1.f ? 1.f : g_aoScale);

    if (s_normalTex && s_w == width && s_h == height && s_scale == scale)
        return true;

    if (s_occSurf)    { s_occSurf->Release();    s_occSurf    = nullptr; }
    if (s_occTex)     { s_occTex->Release();     s_occTex     = nullptr; }
    if (s_normalSurf) { s_normalSurf->Release(); s_normalSurf = nullptr; }
    if (s_normalTex)  { s_normalTex->Release();  s_normalTex  = nullptr; }

    // A8R8G8B8 and not a float format on purpose: a view-space normal is a unit vector,
    // 8 bits an axis is a quarter of a degree, and every card that runs this game can
    // render to it.
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                  D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_normalTex, nullptr)) ||
        FAILED(s_normalTex->GetSurfaceLevel(0, &s_normalSurf)))
    {
        DebugLog("ssao: normal target creation failed");
        Ssao_Destroy();
        return false;
    }

    const int occW = (int)(width  * scale) < 16 ? 16 : (int)(width  * scale);
    const int occH = (int)(height * scale) < 16 ? 16 : (int)(height * scale);

    if (FAILED(dev->CreateTexture(occW, occH, 1, D3DUSAGE_RENDERTARGET,
                                  D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_occTex, nullptr)) ||
        FAILED(s_occTex->GetSurfaceLevel(0, &s_occSurf)))
    {
        DebugLog("ssao: occlusion target creation failed");
        Ssao_Destroy();
        return false;
    }

    s_w = width; s_h = height; s_occW = occW; s_occH = occH; s_scale = scale;
    DebugLog("ssao: built %dx%d, occlusion %dx%d", width, height, occW, occH);
    return true;
}

// ---- the state this pass borrows ------------------------------------------------------
// Saved and put back exactly. The list is the union of what the three passes touch plus
// what DrawPrimitiveUP nulls behind the material system's back (stream 0 + the index
// buffer, both of which the engine's own binding cache believes are still bound).
struct AoSaved
{
    IDirect3DSurface9*           depthStencil;
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex[3];
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT  streamOffset, streamStride;
    DWORD fvf;
    D3DVIEWPORT9 vp;
    DWORD rs[16];
    DWORD samp[3][6];
    float psc[4][4];   // c0..c3, the registers all three passes write
};

static const D3DRENDERSTATETYPE k_aoRS[16] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_CULLMODE,
    D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE,
    // point size is only in this list because the RESZ depth resolve writes its trigger
    // code into it, and the resolve runs in the same hook a moment before this pass
    D3DRS_POINTSIZE, D3DRS_SEPARATEALPHABLENDENABLE,
};
static const D3DSAMPLERSTATETYPE k_aoSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void AoSave(IDirect3DDevice9* dev, AoSaved& s)
{
    if (FAILED(dev->GetDepthStencilSurface(&s.depthStencil))) s.depthStencil = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->GetRenderState(k_aoRS[i], &s.rs[i]);
    for (int u = 0; u < 3; ++u) {
        dev->GetTexture(u, &s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_aoSamp[i], &s.samp[u][i]);
    }
    dev->GetPixelShaderConstantF(0, s.psc[0], 4);
}

static void AoRestore(IDirect3DDevice9* dev, AoSaved& s)
{
    dev->SetPixelShaderConstantF(0, s.psc[0], 4);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);

    // FVF FIRST, DECLARATION LAST, and the fvf only when there was one: SetFVF builds a
    // declaration of its own, so the other order throws the game's decl away -- and
    // SetFVF(0) with a decl bound is how you end up with none at all.
    if (s.fvf) dev->SetFVF(s.fvf);
    dev->SetVertexDeclaration(s.decl);

    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    for (int u = 0; u < 3; ++u) {
        dev->SetTexture(u, s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_aoSamp[i], s.samp[u][i]);
    }
    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_aoRS[i], s.rs[i]);
    dev->SetViewport(&s.vp);
    dev->SetDepthStencilSurface(s.depthStencil);

    if (s.depthStencil) s.depthStencil->Release();
    if (s.decl)         s.decl->Release();
    for (auto* t : s.tex) if (t) t->Release();
    if (s.vs)      s.vs->Release();
    if (s.ps)      s.ps->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

void Ssao_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
               const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
               const FxViewSetup& view)
{
    if (!dev || !target || !depth || s_shaderFailed) return;
    if (!ScreenFx_QuadReady(dev)) return;
    if (!AoBuild(dev, desc)) return;

    IDirect3DPixelShader9* prepare   = AoShaderFor(dev, AO_PREPARE);
    IDirect3DPixelShader9* occlusion = AoShaderFor(dev, AO_OCCLUSION);
    IDirect3DPixelShader9* composite = AoShaderFor(dev, AO_COMPOSITE);
    if (!prepare || !occlusion || !composite) return;

    // Full frame sized, or we are sampling something that was never the scene's depth.
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("ssao: depth target size does not match the frame"); }
        return;
    }

    AoSaved st{};
    AoSave(dev, st);

    const float width  = (float)s_w;
    const float height = (float)s_h;

    const D3DVIEWPORT9 fullVp{ 0, 0, (DWORD)s_w, (DWORD)s_h, 0.f, 1.f };
    const D3DVIEWPORT9 occVp { 0, 0, (DWORD)s_occW, (DWORD)s_occH, 0.f, 1.f };

    // No depth test, no depth surface: every pass here is a fullscreen quad over a target
    // whose depth is the thing being read, not written.
    dev->SetDepthStencilSurface(nullptr);
    ScreenFx_BindQuad(dev);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);

    for (DWORD u = 0; u < 3; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        // an INTZ surface is not filterable and a packed normal must not be averaged
        // across an edge
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    }

    // The projection the whole pass reconstructs positions with. The view setup's fov is
    // HORIZONTAL, so the vertical half angle comes off the aspect ratio -- and that is
    // taken from the frame itself, not from the setup.
    const float aspect   = (height > 0.f) ? width / height : 1.777778f;
    const float fovClamp = (view.fov < 1.f) ? 1.f : (view.fov > 170.f ? 170.f : view.fov);
    const float tanX     = tanf(fovClamp * 0.5f * 3.14159265f / 180.f);
    const float tanY     = tanX / aspect;

    const float c0[4] = { view.zNear, view.zFar, tanX, tanY };
    dev->SetPixelShaderConstantF(0, c0, 1);

    const float fadeStart = g_aoFadeStart;
    const float fadeEnd   = (g_aoFadeEnd > fadeStart + 1.f) ? g_aoFadeEnd : fadeStart + 1.f;
    const float vmCutoff  = g_aoViewmodel ? 0.1f : 0.f;   // the raw depth range the engine draws it into

    // PASS ONE: normals off the depth, at full res
    {
        const float c1[4] = { 1.f / width, 1.f / height, 0.f, 0.f };
        dev->SetPixelShaderConstantF(1, c1, 1);
        dev->SetRenderTarget(0, s_normalSurf);
        dev->SetViewport(&fullVp);
        dev->SetTexture(0, depth);
        dev->SetPixelShader(prepare);
        ScreenFx_DrawQuad(dev, s_w, s_h);
    }

    // PASS TWO: the gather, at the render scale
    {
        const float radius  = (g_aoRadius < 1.f) ? 1.f : g_aoRadius;
        const int   samples = (g_aoSamples < 4) ? 4 : (g_aoSamples > 64 ? 64 : g_aoSamples);
        const float bias    = (g_aoBias < 0.f) ? 0.f : (g_aoBias > 0.8f ? 0.8f : g_aoBias);

        // c1..c3 are contiguous: one upload instead of three round trips through the runtime
        const float block[3][4] = {
            { aspect, (float)samples, radius, -1.f / (radius * radius) },
            // the uv clamp caps how much of the screen one pixel may scan; the last is the
            // self-occlusion bias as a fraction of the distance to the surface
            { bias, vmCutoff, 0.1f, 0.0015f },
            { (float)s_occW, (float)s_occH, fadeEnd, 0.f },
        };
        dev->SetPixelShaderConstantF(1, block[0], 3);
        dev->SetRenderTarget(0, s_occSurf);
        dev->SetViewport(&occVp);
        dev->SetTexture(0, s_normalTex);
        dev->SetTexture(1, depth);
        dev->SetPixelShader(occlusion);
        ScreenFx_DrawQuad(dev, s_occW, s_occH);
    }

    // PASS THREE: blur it and multiply it onto the frame
    {
        // dih's own fog override is the only fog this DLL can read numbers for. Density 0
        // makes the whole term a no-op, which is what a map whose fog we are not driving
        // gives.
        const bool  fogKnown = g_fogEnable && g_aoFogFade && g_fogEnd > g_fogStart;
        const float fogStart = fogKnown ? g_fogStart : 0.f;
        const float fogInv   = fogKnown ? 1.f / (g_fogEnd - g_fogStart) : 0.f;
        const float fogDens  = fogKnown ? ((g_fogDensity < 0.f) ? 0.f : (g_fogDensity > 1.f ? 1.f : g_fogDensity)) : 0.f;

        // "fade under smoke" (botox menu_tab_visuals.cpp:1810) -- per-pixel sphere test in the
        // shader (fx_ao_composite.hlsl c4/c5), matching botox's own per-pixel approach. World
        // -space smoke centre (hud_aim.cpp CollectSmoke, via HudSnap) placed into the camera
        // -local frame fx_ao_composite.hlsl's view_pos already uses (world delta dotted with
        // the camera's right/up/forward) -- done once here, not per pixel.
        float smokeCam[3] = { 0.f, 0.f, 0.f };
        float smokeRadius = 0.f, smokeBand = 1.f;
        if (g_vsAoSmokeFade) {
            HudSnap snap;
            float camOrigin[3];
            if (HudSnap_Read(snap, GetTickCount64()) && snap.smokeRadius > 0.f && ScreenFx_ViewOrigin(camOrigin)) {
                float angles[3];
                if (ScreenFx_ViewAngles(angles)) {
                    float fwd[3], right[3], up[3];
                    SsaoAngleVectors(angles, fwd, right, up);
                    const float delta[3] = { snap.smokeCenter[0] - camOrigin[0], snap.smokeCenter[1] - camOrigin[1],
                                              snap.smokeCenter[2] - camOrigin[2] };
                    smokeCam[0] = delta[0] * right[0] + delta[1] * right[1] + delta[2] * right[2];
                    smokeCam[1] = delta[0] * up[0]    + delta[1] * up[1]    + delta[2] * up[2];
                    smokeCam[2] = delta[0] * fwd[0]   + delta[1] * fwd[1]   + delta[2] * fwd[2];
                    smokeRadius = snap.smokeRadius;
                    smokeBand   = snap.smokeRadius * 0.5f;   // fade band width outside the radius
                }
            }
        }

        const float block[4][4] = {
            { view.zNear, view.zFar, 1.f / (float)s_occW, 1.f / (float)s_occH },
            { (g_aoBlur < 0.f) ? 0.f : g_aoBlur, fadeEnd, tanX, tanY },
            { (g_aoIntensity < 0.f) ? 0.f : g_aoIntensity, fadeStart, 1.f / (fadeEnd - fadeStart), vmCutoff },
            { fogStart, fogInv, fogDens, 0.f },
        };
        dev->SetPixelShaderConstantF(0, block[0], 4);
        const float smokeBlock[2][4] = {
            { smokeCam[0], smokeCam[1], smokeCam[2], 0.f },
            { smokeRadius, smokeBand, 0.f, 0.f },
        };
        dev->SetPixelShaderConstantF(4, smokeBlock[0], 2);

        dev->SetRenderTarget(0, target);
        dev->SetViewport(&fullVp);
        dev->SetTexture(0, s_occTex);
        dev->SetTexture(1, depth);
        dev->SetTexture(2, s_normalTex);

        // the occlusion is the one texture that WANTS filtering -- it is smaller than the frame
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

        // THE COMPOSITE. dest * src, which is the frame times one minus the occlusion -- no
        // read back of the frame anywhere. Alpha is left alone: in integer hdr it carries
        // the engine's own dest-alpha depth and other passes still read it.
        if (!g_aoDebug) {
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
        }
        dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                            D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
        dev->SetPixelShader(composite);
        ScreenFx_DrawQuad(dev, s_w, s_h);
    }

    AoRestore(dev, st);

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        DebugLog("^FX: ao drew %dx%d occ %dx%d fov=%.1f zn=%.2f zf=%.0f samples=%d radius=%.0f",
                 s_w, s_h, s_occW, s_occH, view.fov, view.zNear, view.zFar, g_aoSamples, g_aoRadius);
    }
}
