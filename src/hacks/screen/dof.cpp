#include "screenfx.h"
#include "../movement/movement.h"          // Ray_mv / GameTrace_t / kTrace_TraceRay -- the focus trace
#include "shaders/fx_dof_prepare.h"    // k_fxDofPrepareBytes
#include "shaders/fx_dof_gather.h"     // k_fxDofGatherBytes
#include "shaders/fx_dof_coc.h"        // k_fxDofCocBytes
#include "shaders/fx_dof_near.h"       // k_fxDofNearBytes
#include "shaders/fx_dof_smooth.h"     // k_fxDofSmoothBytes

// Depth of field -- a real one, off the depth buffer, not a screen-space fake. Ported from
// coldtires (hacks/visuals/screen/depth_of_field.cpp), whose gather is qUINT ADoF's and
// whose near plane is CinematicDOF's.
//
// TF2 already HAS a depth of field pass (viewpostprocess.cpp DoDepthOfField) and it is
// unreachable: IsDepthOfFieldEnabled wants HDR_TYPE_FLOAT, that wants mat_hdr_level 3, and
// the engine clamps that convar to 2 (matsys_interface.cpp:210). Its shader also reads depth
// out of the frame buffer's alpha, which in integer hdr is 8 bits over a 192 unit range --
// a couple of metres. So: our own pass, off the real z buffer.
//
//   1. prepare   the frame + its SIGNED circle of confusion packed into alpha
//   2. coc       the near field, blurred, half res, two separable draws  (near bleed only)
//   3. gather    the far plane, one spiral disc per pixel, tap count following the radius
//   4. near      the foreground over the finished far plane, radius off the blurred field
//   5. smooth    a separable gaussian that closes the gaps the spiral leaves
//
// Nothing of the game is touched: the depth we read is a copy, the colour we write is the
// frame already on the card.

// ---- device objects -------------------------------------------------------------------
enum DofShader { DOF_PREPARE = 0, DOF_GATHER, DOF_COC, DOF_NEAR, DOF_SMOOTH, DOF_SHADER_MAX };

static IDirect3DPixelShader9* s_ps[DOF_SHADER_MAX] = {};
static bool s_shaderFailed = false;

// the frame lands in the stage texture first (back buffer format, so StretchRect accepts
// it), then the prepare pass packs colour + coc into the colour texture's alpha
static IDirect3DTexture9* s_stageTex  = nullptr;
static IDirect3DSurface9* s_stageSurf = nullptr;
static IDirect3DTexture9* s_colTex    = nullptr;
static IDirect3DSurface9* s_colSurf   = nullptr;
static IDirect3DTexture9* s_blurTex   = nullptr;
static IDirect3DSurface9* s_blurSurf  = nullptr;
static IDirect3DTexture9* s_cocTex    = nullptr;
static IDirect3DSurface9* s_cocSurf   = nullptr;
static IDirect3DTexture9* s_cocTmpTex = nullptr;
static IDirect3DSurface9* s_cocTmpSurf= nullptr;

// msaa only: a multisampled surface cannot be copied into a texture, but it can be RESOLVED
// into a plain render target of the same size and format, and that can.
static IDirect3DSurface9* s_resolveSurf = nullptr;

static int       s_w = 0, s_h = 0, s_cocW = 0, s_cocH = 0;
static D3DFORMAT s_fmt = D3DFMT_UNKNOWN;
static D3DMULTISAMPLE_TYPE s_msaa = D3DMULTISAMPLE_NONE;

// ---- the focus, traced on the main thread ---------------------------------------------
// Smoothed, so auto focus does not snap the whole frame every time the crosshair crosses an
// edge. Written by Dof_UpdateFocus (the DoPostScreenSpaceEffects hook), read by the draw.
static volatile float s_focus = 0.f;
static void*  s_engineTrace   = nullptr;
static double s_lastFocusTime = 0.0;

void Dof_Destroy()
{
    if (s_resolveSurf) { s_resolveSurf->Release(); s_resolveSurf = nullptr; }
    if (s_cocTmpSurf)  { s_cocTmpSurf->Release();  s_cocTmpSurf  = nullptr; }
    if (s_cocTmpTex)   { s_cocTmpTex->Release();   s_cocTmpTex   = nullptr; }
    if (s_cocSurf)     { s_cocSurf->Release();     s_cocSurf     = nullptr; }
    if (s_cocTex)      { s_cocTex->Release();      s_cocTex      = nullptr; }
    if (s_blurSurf)    { s_blurSurf->Release();    s_blurSurf    = nullptr; }
    if (s_blurTex)     { s_blurTex->Release();     s_blurTex     = nullptr; }
    if (s_colSurf)     { s_colSurf->Release();     s_colSurf     = nullptr; }
    if (s_colTex)      { s_colTex->Release();      s_colTex      = nullptr; }
    if (s_stageSurf)   { s_stageSurf->Release();   s_stageSurf   = nullptr; }
    if (s_stageTex)    { s_stageTex->Release();    s_stageTex    = nullptr; }
    for (auto& ps : s_ps) { if (ps) { ps->Release(); ps = nullptr; } }
    s_w = s_h = s_cocW = s_cocH = 0;
    s_fmt  = D3DFMT_UNKNOWN;
    s_msaa = D3DMULTISAMPLE_NONE;
}

static IDirect3DPixelShader9* DofShaderFor(IDirect3DDevice9* dev, DofShader which)
{
    if (s_ps[which]) return s_ps[which];
    if (s_shaderFailed) return nullptr;

    const BYTE* bytes = nullptr;
    size_t size = 0;
    switch (which) {
        case DOF_PREPARE: bytes = k_fxDofPrepareBytes; size = sizeof(k_fxDofPrepareBytes); break;
        case DOF_GATHER:  bytes = k_fxDofGatherBytes;  size = sizeof(k_fxDofGatherBytes);  break;
        case DOF_COC:     bytes = k_fxDofCocBytes;     size = sizeof(k_fxDofCocBytes);     break;
        case DOF_NEAR:    bytes = k_fxDofNearBytes;    size = sizeof(k_fxDofNearBytes);    break;
        default:          bytes = k_fxDofSmoothBytes;  size = sizeof(k_fxDofSmoothBytes);  break;
    }

    std::vector<DWORD> code((size + 3) / 4);
    memcpy(code.data(), bytes, size);

    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps[which])) || !s_ps[which]) {
        s_ps[which] = nullptr;
        s_shaderFailed = true;
        DebugLog("dof: pixel shader %d refused by the card -- depth of field is off", (int)which);
        return nullptr;
    }
    DebugLog("dof: shader %d built", (int)which);
    return s_ps[which];
}

static bool DofBuild(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    const int width = (int)desc.Width, height = (int)desc.Height;

    if (s_colTex && s_w == width && s_h == height && s_fmt == desc.Format && s_msaa == desc.MultiSampleType)
        return true;

    Dof_Destroy();

    // The stage keeps the render target's own format so StretchRect accepts it. The colour
    // texture is forced to A8R8G8B8 because the back buffer is usually X8R8G8B8 -- no alpha
    // channel to pack the circle of confusion into, which is the whole point of prepare.
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                  D3DPOOL_DEFAULT, &s_stageTex, nullptr)) ||
        FAILED(s_stageTex->GetSurfaceLevel(0, &s_stageSurf)))
    { DebugLog("dof: stage target creation failed"); Dof_Destroy(); return false; }

    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_colTex, nullptr)) ||
        FAILED(s_colTex->GetSurfaceLevel(0, &s_colSurf)))
    { DebugLog("dof: colour target creation failed"); Dof_Destroy(); return false; }

    // Where the gather lands when anything follows it. Same A8R8G8B8 because the circle of
    // confusion has to survive the hop -- every pass downstream runs off that alpha.
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_blurTex, nullptr)) ||
        FAILED(s_blurTex->GetSurfaceLevel(0, &s_blurSurf)))
    { DebugLog("dof: blur target creation failed"); Dof_Destroy(); return false; }

    // The near coc field, half res twice over: it is a blur of a blur radius, so resolution
    // buys nothing, and fp16 keeps the falloff smooth enough not to band the bleed. Losing
    // these two costs the bleed, never the pass.
    const UINT cocW = (desc.Width  / 2u) < 1u ? 1u : desc.Width  / 2u;
    const UINT cocH = (desc.Height / 2u) < 1u ? 1u : desc.Height / 2u;

    for (const D3DFORMAT fmt : { D3DFMT_A16B16G16R16F, D3DFMT_A8R8G8B8 })
    {
        if (SUCCEEDED(dev->CreateTexture(cocW, cocH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_cocTex, nullptr)) &&
            SUCCEEDED(s_cocTex->GetSurfaceLevel(0, &s_cocSurf)) &&
            SUCCEEDED(dev->CreateTexture(cocW, cocH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_cocTmpTex, nullptr)) &&
            SUCCEEDED(s_cocTmpTex->GetSurfaceLevel(0, &s_cocTmpSurf)))
            break;

        if (s_cocTmpSurf) { s_cocTmpSurf->Release(); s_cocTmpSurf = nullptr; }
        if (s_cocTmpTex)  { s_cocTmpTex->Release();  s_cocTmpTex  = nullptr; }
        if (s_cocSurf)    { s_cocSurf->Release();    s_cocSurf    = nullptr; }
        if (s_cocTex)     { s_cocTex->Release();     s_cocTex     = nullptr; }
    }
    if (!s_cocSurf || !s_cocTmpSurf)
        DebugLog("dof: near plane coc target unavailable -- no near bleed");

    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE &&
        FAILED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE,
                                       0, FALSE, &s_resolveSurf, nullptr)))
    { DebugLog("dof: msaa resolve target creation failed"); Dof_Destroy(); return false; }

    s_w = width; s_h = height; s_cocW = (int)cocW; s_cocH = (int)cocH;
    s_fmt = desc.Format; s_msaa = desc.MultiSampleType;
    DebugLog("dof: built %dx%d msaa %d coc %dx%d", width, height, (int)desc.MultiSampleType, s_cocW, s_cocH);
    return true;
}

// ---- the focus trace ------------------------------------------------------------------
// ITraceFilter is two virtuals and no destructor: slot 0 ShouldHitEntity, slot 1
// GetTraceType. Same hand-built vtable movement.cpp uses, minus the team rules -- what the
// eye focuses on is what is in front of it, friendly or not.
struct DofFocusFilter
{
    void** vfptr;
    void*  pSkip;

    static bool __fastcall sShouldHit(DofFocusFilter* self, void* pEnt, int)
    {
        return pEnt && pEnt != self->pSkip;
    }
    // TRACE_EVERYTHING: hitboxes included, so a player's head in front of a wall is what
    // the focus lands on -- the eye does the same.
    static int __fastcall sTraceType(DofFocusFilter*) { return 0; }

    void Setup(void* skip)
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
        pSkip = skip;
    }
};

// "see through glass" on: CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER |
// CONTENTS_HITBOX -- window, grate and debris left out so a pane of glass cannot steal the
// focus from what is behind it. Off (botox default): MASK_SHOT_HULL | CONTENTS_HITBOX, i.e.
// the same plus CONTENTS_WINDOW 0x2, CONTENTS_GRATE 0x8, CONTENTS_DEBRIS 0x4000000
// (public/bspflags.h). botox depth_of_field.cpp:399.
inline constexpr unsigned k_dofFocusMask = 0x1 | 0x4000 | 0x2000000 | 0x40000000;
inline constexpr unsigned k_dofFocusMaskGlass = k_dofFocusMask | 0x2 | 0x8 | 0x4000000;

static void DofTraceLine(const Vec3& start, const Vec3& end, void* filter, GameTrace_t* tr, unsigned mask)
{
    tr->fraction = 1.f;
    tr->endpos   = end;
    if (!s_engineTrace) return;

    Ray_mv ray;
    ray.Init(start, end);
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)s_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(s_engineTrace, ray, mask, filter, tr);
    } __except(EXCEPTION_EXECUTE_HANDLER) { tr->fraction = 1.f; tr->endpos = end; }
}

static void DofAngleVectors(const Vec3& ang, Vec3& fwd, Vec3& right, Vec3& up)
{
    const float k = 3.14159265f / 180.f;
    const float sp = sinf(ang.x * k), cp = cosf(ang.x * k);
    const float sy = sinf(ang.y * k), cy = cosf(ang.y * k);
    const float sr = sinf(ang.z * k), cr = cosf(ang.z * k);

    fwd   = Vec3(cp * cy, cp * sy, -sp);
    right = Vec3(-1.f * sr * sp * cy + -1.f * cr * -sy,
                 -1.f * sr * sp * sy + -1.f * cr *  cy,
                 -1.f * sr * cp);
    up    = Vec3(cr * sp * cy + -sr * -sy,
                 cr * sp * sy + -sr *  cy,
                 cr * cp);
}

// Main thread, from the game's own post-processing slot: the view setup is alive here and
// the entity list belongs to this thread. The draw only ever sees the number that comes out.
void Dof_UpdateFocus(const Vec3& origin, const Vec3& angles, float fov, float aspect)
{
    if (!g_dofEnable) return;

    float target = g_dofDistance;

    if (g_dofAutoFocus)
    {
        if (!s_engineTrace) s_engineTrace = GetInterface("engine.dll", "EngineTraceClient003");

        Vec3 fwd, right, up;
        DofAngleVectors(angles, fwd, right, up);

        void* local = nullptr;
        const int idx = GetLocalPlayerIndex();
        if (idx > 0) local = GetClientEntity(idx);

        // focus point x/y (botox depth_of_field.cpp:374): the ray through that point of the
        // screen. The setup's fov is horizontal, so the vertical half angle comes off the aspect.
        Vec3 dir = fwd;
        const float px = (g_dofFocusX < 0.f) ? 0.f : (g_dofFocusX > 1.f ? 1.f : g_dofFocusX);
        const float py = (g_dofFocusY < 0.f) ? 0.f : (g_dofFocusY > 1.f ? 1.f : g_dofFocusY);
        if (px != 0.5f || py != 0.5f) {
            const float f    = (fov < 1.f) ? 1.f : (fov > 170.f ? 170.f : fov);
            const float tanX = tanf(f * 0.5f * 3.14159265f / 180.f);
            const float tanY = tanX / ((aspect > 0.01f) ? aspect : 0.01f);
            dir = fwd + right * ((px - 0.5f) * 2.f * tanX) + up * ((0.5f - py) * 2.f * tanY);
            const float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 1e-4f) dir = dir * (1.f / len);
        }

        DofFocusFilter filter; filter.Setup(local);
        GameTrace_t tr{};
        DofTraceLine(origin, origin + dir * 8192.f, &filter, &tr,
                     g_dofSeeThrough ? k_dofFocusMask : k_dofFocusMaskGlass);

        // What the blur compares against is view-space DEPTH, not distance: project the hit
        // onto the forward axis, or an off-centre hit would read further away than it is.
        const Vec3  hit  = tr.endpos - origin;
        const float dist = hit.x * fwd.x + hit.y * fwd.y + hit.z * fwd.z;
        const float lim  = (g_dofFocusLimit < 64.f) ? 64.f : (g_dofFocusLimit > 8192.f ? 8192.f : g_dofFocusLimit);
        target = (dist < 16.f) ? 16.f : (dist > lim ? lim : dist);
    }

    if (s_focus <= 0.f) s_focus = target;

    // Exponential approach on real elapsed time, so the pull rate is the same at 60 and at
    // 400 fps. A hard snap reads as the whole frame breathing every time the crosshair
    // crosses an edge.
    LARGE_INTEGER freq{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const double t  = (freq.QuadPart > 0) ? (double)now.QuadPart / (double)freq.QuadPart : 0.0;
    double dt = (s_lastFocusTime > 0.0) ? (t - s_lastFocusTime) : 0.0;
    s_lastFocusTime = t;
    if (dt < 0.0 || dt > 0.25) dt = 0.25;

    const float speed = (g_dofFocusSpeed < 0.1f) ? 0.1f : (g_dofFocusSpeed > 40.f ? 40.f : g_dofFocusSpeed);
    const float rate  = (dt > 0.0) ? 1.f - expf(-speed * (float)dt) : 1.f;

    s_focus += (target - s_focus) * rate;
}

// ---- borrowed state --------------------------------------------------------------------
struct DofSaved
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
    DWORD rs[13];
    DWORD samp[3][6];
    float psc[6][4];   // c0..c5, every register this chain writes
};

static const D3DRENDERSTATETYPE k_dofRS[13] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE,
    // point size only because the RESZ depth resolve writes its trigger code into it
    D3DRS_POINTSIZE, D3DRS_SEPARATEALPHABLENDENABLE,
};
static const D3DSAMPLERSTATETYPE k_dofSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void DofSave(IDirect3DDevice9* dev, DofSaved& s)
{
    if (FAILED(dev->GetDepthStencilSurface(&s.depthStencil))) s.depthStencil = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 13; ++i) dev->GetRenderState(k_dofRS[i], &s.rs[i]);
    for (int u = 0; u < 3; ++u) {
        dev->GetTexture(u, &s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_dofSamp[i], &s.samp[u][i]);
    }
    dev->GetPixelShaderConstantF(0, s.psc[0], 6);
}

static void DofRestore(IDirect3DDevice9* dev, DofSaved& s)
{
    dev->SetPixelShaderConstantF(0, s.psc[0], 6);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    if (s.fvf) dev->SetFVF(s.fvf);
    dev->SetVertexDeclaration(s.decl);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);
    for (int u = 0; u < 3; ++u) {
        dev->SetTexture(u, s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_dofSamp[i], s.samp[u][i]);
    }
    for (int i = 0; i < 13; ++i) dev->SetRenderState(k_dofRS[i], s.rs[i]);
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

void Dof_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
              const FxViewSetup& view, bool viewmodelPass)
{
    if (!dev || !target || !depth || s_shaderFailed) return;
    if (!ScreenFx_QuadReady(dev)) return;
    if (!DofBuild(dev, desc)) return;

    IDirect3DPixelShader9* prepare = DofShaderFor(dev, DOF_PREPARE);
    IDirect3DPixelShader9* gather  = DofShaderFor(dev, DOF_GATHER);
    if (!prepare || !gather) return;

    // THE GUN IS A FOREGROUND OBJECT AND NOTHING ELSE. Its whole pass is the near plane, so
    // the bleed chain is not optional there -- without it the blur stops dead at the gun's
    // silhouette and the edge comes out sharper than anything around it.
    // botox debug views: "show depth" replaces the gather's output with linear depth (no near,
    // no smooth -- data, not a picture), "show near bleed" the near pass's with the coc field.
    const bool debugDepth = g_dofDebugDepth && !viewmodelPass;
    const bool debugNear  = g_dofDebugNear && g_dofNearBlur && !debugDepth && !viewmodelPass;

    const bool wantNear   = (g_dofNearBlur || viewmodelPass) && !debugDepth && s_cocSurf && s_cocTmpSurf;
    const bool wantSmooth = g_dofSmooth && !debugDepth && !debugNear;

    IDirect3DPixelShader9* cocShader    = wantNear   ? DofShaderFor(dev, DOF_COC)    : nullptr;
    IDirect3DPixelShader9* nearShader   = wantNear   ? DofShaderFor(dev, DOF_NEAR)   : nullptr;
    IDirect3DPixelShader9* smoothShader = wantSmooth ? DofShaderFor(dev, DOF_SMOOTH) : nullptr;
    const bool nearOn = cocShader && nearShader;

    // Full frame sized, or we are sampling something that was never the scene's depth.
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("dof: depth target size does not match the frame"); }
        return;
    }

    // msaa on: resolve the multisampled frame into the plain surface first, then copy that
    HRESULT blit = dev->StretchRect(target, nullptr, s_resolveSurf ? s_resolveSurf : s_stageSurf,
                                    nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(blit) && s_resolveSurf)
        blit = dev->StretchRect(s_resolveSurf, nullptr, s_stageSurf, nullptr, D3DTEXF_NONE);
    if (FAILED(blit)) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("dof: frame blit failed 0x%08lx", (unsigned long)blit); }
        return;
    }

    DofSaved st{};
    DofSave(dev, st);

    const float width = (float)s_w, height = (float)s_h;
    const D3DVIEWPORT9 fullVp{ 0, 0, (DWORD)s_w, (DWORD)s_h, 0.f, 1.f };
    const D3DVIEWPORT9 cocVp { 0, 0, (DWORD)s_cocW, (DWORD)s_cocH, 0.f, 1.f };

    dev->SetDepthStencilSurface(nullptr);
    dev->SetViewport(&fullVp);
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

    // sampler 0 = the frame (linear), 1 = depth (INTZ, POINT only -- it is not filterable),
    // 2 = the half-res coc field, read at full res, so linear or the bleed comes back in
    // 2x2 blocks
    for (DWORD u = 0; u < 3; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        const DWORD filt = (u == 1) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, filt);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, filt);
    }
    dev->SetTexture(1, depth);

    // THE PARTICLE GUARD's other half. The frame as it stood before one translucent thing
    // drew: prepare compares the two and hands a pixel that changed a circle of confusion of
    // zero, because whatever painted it wrote no depth and the blur under it belongs to the
    // geometry behind. Null when the engine's boundary never came past (see screenfx.cpp),
    // and the guard then switches itself off rather than sampling a stale frame.
    //
    // NOT IN THE VIEWMODEL PASS: the gun is drawn long after that copy is taken, so every
    // pixel of it differs and the guard would hold the one thing this pass exists to blur.
    IDirect3DTexture9* preTex = viewmodelPass ? nullptr : ScreenFx_PreTranslucentTexture();
    const float guard = preTex ? ((g_fxParticleGuard < 0.f) ? 0.f
                                : (g_fxParticleGuard > 1.f ? 1.f : g_fxParticleGuard)) : 0.f;
    dev->SetTexture(2, preTex);

    const float focus     = s_focus;
    const float ramp      = (g_dofRamp < 1.f) ? 1.f : g_dofRamp;
    const float halfBand  = g_dofRange * 0.5f;
    const float maxBlur   = (g_dofMaxBlur < 0.f) ? 0.f : g_dofMaxBlur;

    // THE NEAR RAMP IS ITS OWN. Everything in front of the sharp band has to fit between the
    // eye and focus minus half the band -- with the focus at 400 that is 250 units, and
    // ramping over the far side's 600 means the closest possible pixel still only reaches a
    // third of the blur. Capped at the room available, so the near side always reaches full
    // blur at the eye.
    const float nearRamp = std::max(std::min(ramp, focus - halfBand), 1.f);

    // edge softness 1 = two surfaces both behind the focal plane mix freely, which is what
    // distance actually looks like. coldtires dropped the slider and kept this value.
    const float edgeSoftness = 1.f;

    // c2.x is the raw depth the gun is drawn above. The VIEWMODEL PASS always needs the real
    // 0.1, whatever the switch says -- there the cutoff is what SELECTS the gun rather than
    // what excuses it, and the switch being off is the only reason this pass exists at all.
    const float vmCutoff = (viewmodelPass || g_dofViewmodelSharp) ? 0.1f : 0.f;

    const float block[4][4] = {
        { view.zNear, view.zFar, focus, 1.f / ramp },
        { halfBand, maxBlur, 1.f / width, 1.f / height },
        { vmCutoff,
          (viewmodelPass || nearOn) ? 1.f : (g_dofNearBlur ? 1.f : 0.f),
          viewmodelPass ? 1.f : 0.f, guard },
        { g_dofBokehBoost, g_dofBokehThreshold, edgeSoftness, 1.f / nearRamp },
    };
    dev->SetPixelShaderConstantF(0, block[0], 4);

    // c5.x: whether the near plane belongs to the pass after the gather.
    // c5.y: taps per pixel of radius, the gather's and the near pass's density.
    // c5.zw: the gun's own z range -- it is drawn through a projection of its own, so a raw
    //        depth out of its slice unprojected with the world's would put it on the lens.
    const float density = (g_dofTapDensity < 0.5f) ? 0.5f : (g_dofTapDensity > 2.5f ? 2.5f : g_dofTapDensity);
    const float c5[4] = { nearOn ? 1.f : 0.f, density, view.zNearVm, view.zFarVm };
    dev->SetPixelShaderConstantF(5, c5, 1);

    const DWORD colourOnly = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;

    // PASS ONE: the frame plus its circle of confusion, into our own A8R8G8B8 target.
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
    dev->SetRenderTarget(0, s_colSurf);
    dev->SetTexture(0, s_stageTex);
    dev->SetPixelShader(prepare);
    ScreenFx_DrawQuad(dev, s_w, s_h);

    // THE NEAR COC FIELD, two half-res draws, while the prepared frame is still the newest
    // thing in the colour target: everything after this reads the field, nothing rewrites it.
    if (nearOn) {
        const float cocH[4] = { 1.f, 0.f, 1.f, 0.f };   // z marks the pass that unpacks the prepared alpha
        const float cocV[4] = { 0.f, 1.f, 0.f, 0.f };

        dev->SetPixelShader(cocShader);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
        dev->SetViewport(&cocVp);
        dev->SetRenderTarget(0, s_cocTmpSurf);
        dev->SetTexture(0, s_colTex);
        dev->SetPixelShaderConstantF(4, cocH, 1);
        ScreenFx_DrawQuad(dev, s_cocW, s_cocH);

        dev->SetRenderTarget(0, s_cocSurf);
        dev->SetViewport(&cocVp);
        dev->SetTexture(0, s_cocTmpTex);
        dev->SetPixelShaderConstantF(4, cocV, 1);
        ScreenFx_DrawQuad(dev, s_cocW, s_cocH);
    }

    // THE GATHER, reading only the prepared texture. With the bleed on it is the FAR plane's
    // pass alone and lands in the blur target for the near pass to sit on top of.
    const bool gatherToFrame = !nearOn && !smoothShader;

    // c4, read by the gather and the near pass only (coc / smooth rewrite it for their own
    // draws, and neither runs between here and the near pass): the tap ceiling off "quality"
    // (botox: 16 << tier), show depth + its white point, show near bleed.
    {
        const int   q    = (g_dofQuality < 0) ? 0 : (g_dofQuality > 3 ? 3 : g_dofQuality);
        const float white = (g_dofDebugWhite < 1.f) ? 1.f : g_dofDebugWhite;
        const float c4[4] = { (float)(16 << q), debugDepth ? 1.f : 0.f, white, debugNear ? 1.f : 0.f };
        dev->SetPixelShaderConstantF(4, c4, 1);
    }

    dev->SetRenderTarget(0, gatherToFrame ? target : s_blurSurf);
    dev->SetViewport(&fullVp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, gatherToFrame ? colourOnly : 0x0f);
    dev->SetTexture(0, s_colTex);
    dev->SetPixelShader(gather);
    ScreenFx_DrawQuad(dev, s_w, s_h);

    // Where the smoothing picks up from, and the target it may scribble on. The near pass
    // flips them: it reads the far result out of the blur target, so its own output has to
    // land somewhere else and the blur target becomes the free one.
    IDirect3DTexture9* smoothSrc     = s_blurTex;
    IDirect3DSurface9* smoothScratch = s_colSurf;
    IDirect3DTexture9* smoothScratchTex = s_colTex;

    if (nearOn) {
        const bool nearToFrame = !smoothShader;

        // Textures BEFORE the target: the colour surface it is about to draw into is still
        // the sampler 0 binding from the gather, and d3d9 will not have a texture read and
        // written at once.
        dev->SetTexture(0, s_blurTex);
        dev->SetTexture(2, s_cocTex);
        dev->SetRenderTarget(0, nearToFrame ? target : s_colSurf);
        dev->SetViewport(&fullVp);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, nearToFrame ? colourOnly : 0x0f);
        dev->SetPixelShader(nearShader);
        ScreenFx_DrawQuad(dev, s_w, s_h);

        smoothSrc        = s_colTex;
        smoothScratch    = s_blurSurf;
        smoothScratchTex = s_blurTex;
    }

    // THE LAST TWO PASSES: the separable gaussian, horizontal into whichever target is free
    // by now and vertical onto the frame itself.
    if (smoothShader) {
        // ADoF's bleed factor: how much of THIS pixel's own blur a neighbour has to beat
        // before it counts. The smoothening amount HAS to follow the blur radius -- the taps
        // are two pixels apart, so the reach is 2 * coc * this, and a fixed value means the
        // smoothing IS the blur whenever the max blur slider is small.
        const float bleed = 1.f - edgeSoftness;
        const float amount = (maxBlur * 0.5f > 4.f) ? 4.f : maxBlur * 0.5f;

        const float horizontal[4] = { 1.f, 0.f, bleed, amount };
        const float vertical[4]   = { 0.f, 1.f, bleed, amount };

        dev->SetPixelShader(smoothShader);
        // sampler before target again, for the same reason: the two swap places
        dev->SetTexture(0, smoothSrc);
        dev->SetRenderTarget(0, smoothScratch);
        dev->SetViewport(&fullVp);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
        dev->SetPixelShaderConstantF(4, horizontal, 1);
        ScreenFx_DrawQuad(dev, s_w, s_h);

        dev->SetTexture(0, smoothScratchTex);
        dev->SetRenderTarget(0, target);
        dev->SetViewport(&fullVp);
        // the frame's alpha is the engine's dest-alpha depth again -- colour only from here
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, colourOnly);
        dev->SetPixelShaderConstantF(4, vertical, 1);
        ScreenFx_DrawQuad(dev, s_w, s_h);
    }

    DofRestore(dev, st);

    // One line per pass, and the two are different pictures: the first is the world, the
    // second is the gun on top of it.
    static bool s_logged[2] = { false, false };
    const int which = viewmodelPass ? 1 : 0;
    if (!s_logged[which]) {
        s_logged[which] = true;
        DebugLog("^FX: dof drew %dx%d %s focus=%.1f zn=%.2f zf=%.0f vm=%.2f..%.0f blur=%.1f "
                 "near=%d smooth=%d guard=%.2f",
                 s_w, s_h, viewmodelPass ? "VIEWMODEL" : "world", focus, view.zNear, view.zFar,
                 view.zNearVm, view.zFarVm, maxBlur, (int)nearOn,
                 (int)(smoothShader != nullptr), guard);
    }
}
