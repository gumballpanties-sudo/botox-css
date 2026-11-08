#include "mc_world.h"
#include "mc_world_math.h"
#include "../screen/shaders/fx_mcsky.h"     // k_fxMcSkyBytes -- compiled from fx_mcsky.hlsl by fxc
#include "../screen/screenfx.h"     // shared quad vs/decl, ScreenFx_QueueFunctor
#include "../../render/render.h"       // Render_PresentTid
#include "../movement/prediction.h"   // g_globalVars -- curtime, main thread only
#include <vector>

// Minecraft sky/clouds. See mc_world_math.h for the pure math, fx_mcsky.hlsl for the pixel side.
//
// WHERE IT DRAWS. botox queues its cloud pass right behind IVRenderView::DrawWorldLists for a
// view with DRAWWORLDLISTS_DRAW_SKYBOX; here the same point is h_R_DrawSkyBox, which the engine
// only calls from that branch. The quad sits at the depth clear value (z test LESSEQUAL), so it
// lands on sky pixels only whichever side of the opaque world the engine puts its sky, and
// everything drawn after it -- 3D skybox, world, particles, gun, hud -- composites on top.
// The old version painted from the post slot off the resolved depth: sky pixels behind
// particles, smoke and the 3D skybox all read "far" there and were painted over, and with the
// post slot not wired for it, it fell back to EndScene where the depth reads far everywhere and
// the whole screen turned to sky.

// ---- written on the main thread, read on the render thread ---------------------------
// torn read = one frame of a slightly wrong drift, never a crash
static volatile float s_offX = 0.f, s_offZ = 0.f;

void MCWorld_UpdateView(const float origin[3])
{
    if (!g_mcWorldEnable) return;
    float off[2];
    const double curtime = g_globalVars ? (double)g_globalVars->curtime : 0.0;
    n_mc_world::CloudOffset(curtime, origin[0], origin[1], off);
    s_offX = off[0];
    s_offZ = off[1];
}

// ---- render thread ---------------------------------------------------------------------
static IDirect3DPixelShader9* s_ps    = nullptr;
static IDirect3DTexture9*     s_cells = nullptr;
static IDirect3DTexture9*     s_sun   = nullptr;
static bool  s_failed = false;
static float s_sky[3], s_fog[3], s_sunSin = 0.f, s_sunCos = 1.f;
static bool  s_constsReady = false;

void MCWorld_Destroy()
{
    if (s_cells) { s_cells->Release(); s_cells = nullptr; }
    if (s_sun)   { s_sun->Release();   s_sun   = nullptr; }
    if (s_ps)    { s_ps->Release();    s_ps    = nullptr; }
}

// MANAGED first; a d3d9ex device refuses it, then DEFAULT + DYNAMIC (released before Reset by
// ScreenFx_Destroy). Same fallback as botox build_cells.
static IDirect3DTexture9* McTexture(IDirect3DDevice9* dev, UINT size, void (*fill)(uint32_t*, int))
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(size, size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) &&
        FAILED(dev->CreateTexture(size, size, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr{};
    if (!tex || FAILED(tex->LockRect(0, &lr, nullptr, 0))) { if (tex) tex->Release(); return nullptr; }
    fill(static_cast<uint32_t*>(lr.pBits), lr.Pitch / 4);
    tex->UnlockRect(0);
    return tex;
}

static void FillCells(uint32_t* p, int pitch) { n_mc_world::CellsArgb(p, pitch); }
static void FillSun(uint32_t* p, int pitch)
{
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) p[y * pitch + x] = n_mc_sky::k_sun[y * 32 + x];
}

static bool McEnsure(IDirect3DDevice9* dev)
{
    if (s_ps && s_cells && s_sun) return true;
    if (s_failed) return false;

    if (!s_constsReady) {
        n_mc_world::SkyColor(s_sky);
        n_mc_world::FogColor(s_fog);
        const float a = n_mc_world::SunAngle();
        s_sunSin = sinf(a);
        s_sunCos = cosf(a);
        s_constsReady = true;
    }

    if (!s_ps) {
        std::vector<DWORD> code((sizeof(k_fxMcSkyBytes) + 3) / 4);
        memcpy(code.data(), k_fxMcSkyBytes, sizeof(k_fxMcSkyBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_ps)) || !s_ps) {
            s_ps = nullptr;
            s_failed = true;
            DebugLog("mc_world: the card refused the sky shader -- feature is off");
            return false;
        }
        DebugLog("mc_world: shader built");
    }
    if (!s_cells) s_cells = McTexture(dev, 256, FillCells);
    if (!s_sun)   s_sun   = McTexture(dev, 32, FillSun);
    if (!s_cells || !s_sun) {
        DebugLog("mc_world: cloud/sun texture creation failed -- feature is off");
        MCWorld_Destroy();
        s_failed = true;
        return false;
    }
    return true;
}

static void McDraw(IDirect3DDevice9* dev)
{
    if (!dev || g_eject || !g_mcWorldEnable) return;
    if (!McEnsure(dev) || !ScreenFx_QuadReady(dev)) return;

    // This view's own camera, exactly as the engine just drew its sky with it: vs c8..c11 =
    // cViewProj (common_vs_fxc.h:83), ps c30.w = gamma light scale (common_ps_fxc.h:49).
    float rows[4][4]{}, light[4]{}, rays[4][3]{};
    D3DVIEWPORT9 vp{};
    if (FAILED(dev->GetVertexShaderConstantF(8, rows[0], 4)) || FAILED(dev->GetViewport(&vp)) ||
        !vp.Width || !vp.Height || !n_mc_world::CornerRays(rows, rays)) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("mc_world: no usable cViewProj at the sky draw -- sky skipped"); }
        return;
    }
    if (FAILED(dev->GetPixelShaderConstantF(30, light, 1))) light[3] = 1.f;

    DWORD zFunc = D3DCMP_LESSEQUAL;
    dev->GetRenderState(D3DRS_ZFUNC, &zFunc);
    const bool reversed = zFunc == D3DCMP_GREATER || zFunc == D3DCMP_GREATEREQUAL;

    static const D3DRENDERSTATETYPE k_rs[] = {
        D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
        D3DRS_CULLMODE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE,
        D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE, D3DRS_FOGENABLE, D3DRS_SCISSORTESTENABLE,
    };
    const DWORD k_rsVal[] = {
        D3DZB_TRUE, FALSE, (DWORD)(reversed ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL), FALSE, FALSE,
        D3DCULL_NONE, FALSE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE, FALSE,
        0, D3DFILL_SOLID, FALSE, FALSE,
    };
    constexpr int kRs = (int)(sizeof(k_rs) / sizeof(k_rs[0]));
    static_assert(sizeof(k_rsVal) / sizeof(k_rsVal[0]) == sizeof(k_rs) / sizeof(k_rs[0]), "state list");
    static const D3DSAMPLERSTATETYPE k_samp[] = {
        D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
    };
    // s0 cells wrap (the grid tiles), s1 sun clamp
    const DWORD k_sampVal[2][6] = {
        { D3DTADDRESS_WRAP,  D3DTADDRESS_WRAP,  D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, 0 },
        { D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, 0 },
    };

    // ---- save. The shaderapi caches every one of these, ps constants included: a constant
    // left changed mid-scene would shade the next world surface with our numbers.
    DWORD oldRs[kRs]{}, oldSamp[2][6]{}, oldFvf = 0;
    float oldC[8][4]{};
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DBaseTexture9* oldTex[2]{};
    IDirect3DVertexBuffer9* oldStream = nullptr;
    IDirect3DIndexBuffer9* oldIdx = nullptr;
    UINT oldOff = 0, oldStride = 0;

    for (int i = 0; i < kRs; ++i) dev->GetRenderState(k_rs[i], &oldRs[i]);
    for (int u = 0; u < 2; ++u) {
        dev->GetTexture(u, &oldTex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_samp[i], &oldSamp[u][i]);
    }
    dev->GetPixelShader(&oldPs);
    dev->GetVertexShader(&oldVs);
    dev->GetVertexDeclaration(&oldDecl);
    dev->GetFVF(&oldFvf);
    dev->GetStreamSource(0, &oldStream, &oldOff, &oldStride);
    dev->GetIndices(&oldIdx);
    dev->GetPixelShaderConstantF(0, oldC[0], 8);

    // ---- draw
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], k_rsVal[i]);
    for (int u = 0; u < 2; ++u)
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_samp[i], k_sampVal[u][i]);

    const float c[8][4] = {
        { s_offX, s_offZ, n_mc_world::GammaScale(light[3]), 0.f },
        { s_sky[0], s_sky[1], s_sky[2], 0.f },
        { s_fog[0], s_fog[1], s_fog[2], 0.f },
        { s_sunSin, s_sunCos, 0.f, 0.f },
        { rays[0][0], rays[0][1], rays[0][2], 0.f },
        { rays[1][0], rays[1][1], rays[1][2], 0.f },
        { rays[2][0], rays[2][1], rays[2][2], 0.f },
        { rays[3][0], rays[3][1], rays[3][2], 0.f },
    };
    dev->SetPixelShaderConstantF(0, c[0], 8);
    dev->SetTexture(0, s_cells);
    dev->SetTexture(1, s_sun);
    dev->SetPixelShader(s_ps);
    ScreenFx_BindQuad(dev);

    // ScreenFx_DrawQuad's corners and half-texel shift, at the depth clear value instead of 0
    const float dx = 1.f / (float)vp.Width, dy = 1.f / (float)vp.Height;
    const float z  = reversed ? 0.f : 1.f;
    const FxQuadVert quad[4] = {
        { -1.f - dx,  1.f + dy, z, 1.f, 0.f, 0.f },
        {  1.f - dx,  1.f + dy, z, 1.f, 1.f, 0.f },
        { -1.f - dx, -1.f + dy, z, 1.f, 0.f, 1.f },
        {  1.f - dx, -1.f + dy, z, 1.f, 1.f, 1.f },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(FxQuadVert));

    // ---- restore
    dev->SetPixelShaderConstantF(0, oldC[0], 8);
    dev->SetPixelShader(oldPs);
    dev->SetVertexShader(oldVs);
    if (oldDecl) dev->SetVertexDeclaration(oldDecl); else dev->SetFVF(oldFvf);
    dev->SetStreamSource(0, oldStream, oldOff, oldStride);
    dev->SetIndices(oldIdx);
    for (int u = 0; u < 2; ++u) {
        dev->SetTexture(u, oldTex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_samp[i], oldSamp[u][i]);
    }
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], oldRs[i]);

    for (IUnknown* held : { (IUnknown*)oldPs, (IUnknown*)oldVs, (IUnknown*)oldDecl, (IUnknown*)oldTex[0],
                            (IUnknown*)oldTex[1], (IUnknown*)oldStream, (IUnknown*)oldIdx })
        if (held) held->Release();

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("mc_world: sky drew %ux%u at the skybox draw (gamma %.2f, %s z)", (unsigned)vp.Width,
                 (unsigned)vp.Height, n_mc_world::GammaScale(light[3]), reversed ? "reversed" : "normal");
    }
}

// ---- the call queue ----------------------------------------------------------------------
// Faked CFunctor, same shape as screenfx.cpp's FxFunctor: AddRef, Release, dtor, operator().
// File-static, never freed. Runs on the render thread at the position it was queued from.
struct McFunctor { void** vfptr; };
static int   __fastcall McFn_AddRef (void*)             { return 1; }
static int   __fastcall McFn_Release(void*)             { return 1; }
static void* __fastcall McFn_Dtor   (void* p, unsigned) { return p; }
static void  __fastcall McFn_Call   (void*)
{
    ++g_hookDepth;   // eject drains on it before tearing anything down
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;
    McDraw(g_d3dDev);
}
static void* s_mcFnVt[4] = { (void*)&McFn_AddRef, (void*)&McFn_Release, (void*)&McFn_Dtor, (void*)&McFn_Call };
static McFunctor s_mcFunctor = { s_mcFnVt };

void MCWorld_OnSkyBoxDrawn()
{
    if (!g_mcWorldEnable || g_eject || s_failed) return;
    const DWORD present = Render_PresentTid();
    if (!present) return;

    // The sky faces go out through a dynamic mesh the shaderapi may still be holding (buffered
    // primitives flush on the next engine state change). Unflushed, the last face would reach d3d
    // AFTER our quad and, being $ignorez, paint straight over it. Flush first, either mode.
    if (!ScreenFx_FlushRenderContext()) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("mc_world: render context flush failed -- a sky face may cover the pass"); }
    }

    // Single threaded: this IS the render thread and the sky was just issued, draw now.
    if (GetCurrentThreadId() == present) { McDraw(g_d3dDev); return; }

    // Multicore: the sky draw was recorded, not executed. Queue behind it.
    if (!ScreenFx_QueueFunctor(&s_mcFunctor)) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            DebugLog("mc_world: call queue refused at the sky draw (thread %lu, render thread %lu) -- "
                     "minecraft sky off under multicore, mat_queue_mode 0 brings it back",
                     GetCurrentThreadId(), present);
        }
    }
}
