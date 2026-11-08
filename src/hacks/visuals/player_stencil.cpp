// botox hacks/visuals/screen/player_stencil.cpp, TF2 port. See player_stencil.h for the wiring.
//
// Pass list (botox draw(), unchanged): mask (r = visible, g = occluded, from the engine stencil),
// MSAA resolve, then off the mask: outline = row distance + column distance (exact euclidean px),
// inner glow = vertical-gaussian of a horizontal-gaussian (or one gaussian capsule per hitbox),
// glow = the same blur shaped by soft()/gradient/wave. Composited onto the bound target.
//
// TF2 departures:
//   - shaders precompiled by fxc (shaders/fx_stencil.hlsl, fx_stencil_build.sh): no d3dx9
//   - ps_3_0 never pairs with fixed function, so quads go through screenfx.cpp's passthrough vs
//     (ScreenFx_BindQuad) and the capsules through vs_capsule, positions in clip space
//   - saves what DrawPrimitiveUP nulls (stream 0 + indices: ssao.cpp AoSaved explains why)
//   - capsules from TF2 hitboxes: those are boxes, so aim/chams.cpp turns each into a segment
//     down its long axis with the mean of the two short half-extents as the radius
#include "player_stencil.h"
#include "../../globals/globals.h"
#include "../screen/screenfx.h"
#include "../screen/shaders/fx_stencil_fill.h"
#include "../screen/shaders/fx_stencil_rows.h"
#include "../screen/shaders/fx_stencil_blur.h"
#include "../screen/shaders/fx_stencil_glow.h"
#include "../screen/shaders/fx_stencil_inner.h"
#include "../screen/shaders/fx_stencil_capsule.h"
#include "../screen/shaders/fx_stencil_bone.h"
#include "../screen/shaders/fx_stencil_columns.h"
#include "../screen/shaders/fx_stencil_vs.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

void DebugLog(const char* fmt, ...);

namespace
{
	enum { PS_FILL, PS_ROWS, PS_BLUR, PS_GLOW, PS_INNER, PS_CAPSULE, PS_BONE, PS_COLUMNS, PS_COUNT };
	IDirect3DPixelShader9*       s_ps[PS_COUNT] = {};
	IDirect3DVertexShader9*      s_capsuleVs = nullptr;
	IDirect3DVertexDeclaration9* s_capsuleDecl = nullptr;
	bool                         s_failed = false;
	std::atomic<bool>            s_glowLive{ false };

	IDirect3DSurface9* s_msaaSurf = nullptr, *s_maskSurf = nullptr, *s_distSurf = nullptr, *s_blurSurf = nullptr;
	IDirect3DTexture9* s_maskTex = nullptr, *s_distTex = nullptr, *s_blurTex = nullptr;
	UINT s_w = 0, s_h = 0;
	D3DFORMAT s_fmt = D3DFMT_UNKNOWN;
	D3DMULTISAMPLE_TYPE s_ms = D3DMULTISAMPLE_NONE;
	DWORD s_msq = 0;
	IDirect3DDevice9* s_dev = nullptr;   // the device every resource above belongs to

	template <size_t N>
	std::vector<DWORD> Code(const BYTE (&bytes)[N])   // BYTE arrays carry no alignment: copy into DWORDs
	{
		std::vector<DWORD> c((N + 3) / 4);
		memcpy(c.data(), bytes, N);
		return c;
	}

	void ReleaseTargets()
	{
		for (IDirect3DSurface9** s : { &s_msaaSurf, &s_maskSurf, &s_distSurf, &s_blurSurf }) if (*s) { (*s)->Release(); *s = nullptr; }
		for (IDirect3DTexture9** t : { &s_maskTex, &s_distTex, &s_blurTex }) if (*t) { (*t)->Release(); *t = nullptr; }
		s_w = s_h = 0; s_fmt = D3DFMT_UNKNOWN; s_ms = D3DMULTISAMPLE_NONE; s_msq = 0;
	}

	bool EnsureShaders(IDirect3DDevice9* dev)
	{
		if (s_ps[PS_COLUMNS] || s_failed) return !s_failed;
		const std::vector<DWORD> code[PS_COUNT] = { Code(k_fxStencilFillBytes), Code(k_fxStencilRowsBytes), Code(k_fxStencilBlurBytes),
			Code(k_fxStencilGlowBytes), Code(k_fxStencilInnerBytes), Code(k_fxStencilCapsuleBytes), Code(k_fxStencilBoneBytes),
			Code(k_fxStencilColumnsBytes) };
		bool ok = true;
		for (int i = 0; i < PS_COUNT && ok; i++) ok = SUCCEEDED(dev->CreatePixelShader(code[i].data(), &s_ps[i])) && s_ps[i];
		const auto vs = Code(k_fxStencilVsBytes);
		ok = ok && SUCCEEDED(dev->CreateVertexShader(vs.data(), &s_capsuleVs)) && s_capsuleVs;
		const D3DVERTEXELEMENT9 el[] = {
			{ 0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			{ 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
			D3DDECL_END() };
		ok = ok && SUCCEEDED(dev->CreateVertexDeclaration(el, &s_capsuleDecl)) && s_capsuleDecl;
		ok = ok && ScreenFx_QuadReady(dev);
		if (!ok)
		{
			DebugLog("player stencil: shader failed -- outline / inner glow off, glow back to the halo");
			s_failed = true;
		}
		return ok;
	}

	bool Build(IDirect3DDevice9* dev, const D3DSURFACE_DESC& d)
	{
		if (s_maskTex && s_w == d.Width && s_h == d.Height && s_fmt == d.Format && s_ms == d.MultiSampleType && s_msq == d.MultiSampleQuality)
			return true;
		ReleaseTargets();
		auto make = [&](IDirect3DTexture9*& t, IDirect3DSurface9*& s, D3DFORMAT f) {
			return SUCCEEDED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &t, nullptr)) && SUCCEEDED(t->GetSurfaceLevel(0, &s));
		};
		bool ok = make(s_maskTex, s_maskSurf, d.Format) && make(s_distTex, s_distSurf, d.Format)
			&& (make(s_blurTex, s_blurSurf, D3DFMT_A16B16G16R16F) || make(s_blurTex, s_blurSurf, d.Format));
		if (ok && d.MultiSampleType != D3DMULTISAMPLE_NONE)
			ok = SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, d.MultiSampleType, d.MultiSampleQuality, FALSE, &s_msaaSurf, nullptr));
		if (!ok)
		{
			DebugLog("player stencil: target creation failed (%ux%u fmt %d ms %d)", d.Width, d.Height, (int)d.Format, (int)d.MultiSampleType);
			ReleaseTargets();
			return false;
		}
		s_w = d.Width; s_h = d.Height; s_fmt = d.Format; s_ms = d.MultiSampleType; s_msq = d.MultiSampleQuality;
		DebugLog("player stencil: targets %ux%u (msaa %d)", s_w, s_h, (int)s_ms);
		return true;
	}

	bool Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* depthStencil, const PlayerStencilSettings& st)
	{
		IDirect3DPixelShader9* oldPs = nullptr;
		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DVertexDeclaration9* oldDecl = nullptr;
		IDirect3DBaseTexture9* oldTex = nullptr;
		IDirect3DVertexBuffer9* oldStream = nullptr;
		IDirect3DIndexBuffer9* oldIndices = nullptr;
		UINT oldStreamOff = 0, oldStreamStride = 0;
		D3DVIEWPORT9 oldVp{};
		RECT oldScissor{};
		DWORD fvf = 0;
		float oldK[12][4]{};

		// botox k_states / k_state_values
		constexpr D3DRENDERSTATETYPE kStates[] = { D3DRS_STENCILENABLE, D3DRS_TWOSIDEDSTENCILMODE, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
			D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_COLORWRITEENABLE,
			D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND,
			D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
			D3DRS_SEPARATEALPHABLENDENABLE };
		constexpr DWORD kValues[] = { FALSE, FALSE, D3DCMP_LESS, 1, 0xffffffff, 0, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
			0x0f, 0, D3DFILL_SOLID, FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD, FALSE, D3DCULL_NONE,
			FALSE, FALSE, FALSE };
		static_assert(std::size(kStates) == std::size(kValues));
		DWORD oldStates[std::size(kStates)]{};
		constexpr D3DSAMPLERSTATETYPE kSamplers[] = { D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE };
		constexpr DWORD kSamplerValues[] = { D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, 0 };
		DWORD oldSamplers[std::size(kSamplers)]{};

		dev->GetPixelShader(&oldPs);
		dev->GetVertexShader(&oldVs);
		dev->GetVertexDeclaration(&oldDecl);
		dev->GetFVF(&fvf);
		dev->GetStreamSource(0, &oldStream, &oldStreamOff, &oldStreamStride);
		dev->GetIndices(&oldIndices);
		dev->GetViewport(&oldVp);
		dev->GetScissorRect(&oldScissor);
		dev->GetPixelShaderConstantF(0, oldK[0], 12);
		dev->GetTexture(0, &oldTex);
		for (size_t i = 0; i < std::size(kStates); ++i) { dev->GetRenderState(kStates[i], &oldStates[i]); dev->SetRenderState(kStates[i], kValues[i]); }
		for (size_t i = 0; i < std::size(kSamplers); ++i) { dev->GetSamplerState(0, kSamplers[i], &oldSamplers[i]); dev->SetSamplerState(0, kSamplers[i], kSamplerValues[i]); }

		const float W = float(s_w), H = float(s_h);
		const D3DVIEWPORT9 fullVp{ 0, 0, s_w, s_h, 0.f, 1.f };
		// botox draw_rect: pixel rect, uv = px / size, -0.5 = pixel centres on texels
		auto rect = [&](float l, float t, float r, float b) {
			auto cx = [&](float px) { return (px - 0.5f) / W * 2.f - 1.f; };
			auto cy = [&](float py) { return 1.f - (py - 0.5f) / H * 2.f; };
			const FxQuadVert q[4] = { { cx(l), cy(t), 0.f, 1.f, l / W, t / H }, { cx(r), cy(t), 0.f, 1.f, r / W, t / H },
				{ cx(l), cy(b), 0.f, 1.f, l / W, b / H }, { cx(r), cy(b), 0.f, 1.f, r / W, b / H } };
			dev->SetViewport(&fullVp);
			dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(FxQuadVert));
		};

		const float vl = float(oldVp.X), vt = float(oldVp.Y), vr = vl + float(oldVp.Width), vb = vt + float(oldVp.Height);
		const float k0[4] = { 1.f / W, 1.f / H, std::ceil(st.thickness), st.thickness };

		ScreenFx_BindQuad(dev);
		dev->SetPixelShaderConstantF(0, k0, 1);
		dev->SetPixelShaderConstantF(1, st.visible, 1);
		dev->SetPixelShaderConstantF(2, st.invisible, 1);
		dev->SetPixelShaderConstantF(3, st.glowVisible, 1);
		dev->SetPixelShaderConstantF(4, st.glowInvisible, 1);
		dev->SetPixelShaderConstantF(5, st.glowVisibleOuter, 1);
		dev->SetPixelShaderConstantF(6, st.glowInvisibleOuter, 1);
		dev->SetPixelShaderConstantF(7, st.gradient, 1);
		dev->SetPixelShaderConstantF(8, st.wave, 1);
		dev->SetPixelShaderConstantF(9, st.waveColor, 1);
		dev->SetPixelShaderConstantF(10, st.innerVisible, 1);
		dev->SetPixelShaderConstantF(11, st.innerInvisible, 1);

		// ---- mask: red where 2 <= stencil < 128 (visible), green where stencil == 1 (occluded) ----
		dev->SetRenderTarget(0, s_msaaSurf ? s_msaaSurf : s_maskSurf);
		dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
		dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		dev->SetPixelShader(s_ps[PS_FILL]);
		dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
		rect(vl, vt, vr, vb);
		// vgui popups (console) leave stencil 254 behind: zero red where 128 <= stencil
		dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_LESSEQUAL);
		dev->SetRenderState(D3DRS_STENCILREF, 128);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
		dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
		rect(vl, vt, vr, vb);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		dev->SetRenderState(D3DRS_STENCILREF, 1);
		dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_GREEN);
		rect(vl, vt, vr, vb);
		dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);

		const bool masked = !s_msaaSurf || SUCCEEDED(dev->StretchRect(s_msaaSurf, nullptr, s_maskSurf, nullptr, D3DTEXF_NONE));
		dev->SetDepthStencilSurface(nullptr);

		if (masked)
		{
			// ponytail: full-target offscreen passes, empty pixels still run every tap; scissor to the
			// player boxes if it ever shows in frame time (botox's own note)
			dev->SetTexture(0, s_maskTex);
			if (st.outline)
			{
				dev->SetRenderTarget(0, s_distSurf);
				dev->SetPixelShader(s_ps[PS_ROWS]);
				rect(0.f, 0.f, W, H);
			}

			if (st.inner && !(st.innerBones && st.nCapsules == 0))
			{
				dev->SetPixelShaderConstantF(7, st.innerReach, 1);
				dev->SetRenderTarget(0, s_blurSurf);
				if (st.innerBones)
				{
					struct CapVert { float x, y, z, w, s[4], r[4]; };
					const float hw = 0.5f * float(oldVp.Width), hh = 0.5f * float(oldVp.Height);
					static std::vector<CapVert> verts;   // render thread only
					verts.clear();
					for (int i = 0; i < st.nCapsules; i++)
					{
						const auto& c = st.capsules[i];
						const float ax = vl + (c.a[0] + 1.f) * hw, ay = vt + (1.f - c.a[1]) * hh;
						const float bx = vl + (c.b[0] + 1.f) * hw, by = vt + (1.f - c.b[1]) * hh;
						const float sa = std::max(c.sigma[0] * hh, 0.75f), sb = std::max(c.sigma[1] * hh, 0.75f);   // floored: far players don't shimmer
						const float reach = 3.f * std::max(sa, sb);
						const float l = std::max(std::min(ax, bx) - reach, vl), r = std::min(std::max(ax, bx) + reach, vr);
						const float t = std::max(std::min(ay, by) - reach, vt), b = std::min(std::max(ay, by) + reach, vb);
						if (l >= r || t >= b) continue;
						auto corner = [&](float x, float y) {
							return CapVert{ (x - 0.5f) / W * 2.f - 1.f, 1.f - (y - 0.5f) / H * 2.f, 0.f, 1.f, { ax, ay, bx, by }, { sa, sb, 0.f, 0.f } };
						};
						for (const CapVert& v : { corner(l, t), corner(r, t), corner(l, b), corner(r, t), corner(r, b), corner(l, b) })
							verts.push_back(v);
					}
					dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
					if (!verts.empty())
					{
						dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
						dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MAX);   // overlapping capsules union
						dev->SetPixelShader(s_ps[PS_CAPSULE]);
						dev->SetVertexShader(s_capsuleVs);
						dev->SetVertexDeclaration(s_capsuleDecl);
						dev->SetViewport(&fullVp);
						dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(verts.size() / 3), verts.data(), sizeof(CapVert));
						ScreenFx_BindQuad(dev);
						dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
						dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
					}
				}
				else
				{
					dev->SetPixelShader(s_ps[PS_BLUR]);
					rect(0.f, 0.f, W, H);
				}

				dev->SetRenderTarget(0, target);
				dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				dev->SetPixelShader(s_ps[st.innerBones ? PS_BONE : PS_INNER]);
				dev->SetTexture(0, s_blurTex);
				rect(vl, vt, vr, vb);

				dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				dev->SetTexture(0, s_maskTex);
				dev->SetPixelShaderConstantF(7, st.gradient, 1);
			}

			if (st.glow)
			{
				dev->SetRenderTarget(0, s_blurSurf);
				dev->SetPixelShader(s_ps[PS_BLUR]);
				rect(0.f, 0.f, W, H);
			}

			dev->SetRenderTarget(0, target);
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
			if (st.glow)
			{
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				dev->SetPixelShader(s_ps[PS_GLOW]);
				dev->SetTexture(0, s_blurTex);
				rect(vl, vt, vr, vb);
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			}
			if (st.outline)
			{
				dev->SetPixelShader(s_ps[PS_COLUMNS]);
				dev->SetTexture(0, s_distTex);
				rect(vl, vt, vr, vb);
			}
		}

		dev->SetRenderTarget(0, target);
		dev->SetDepthStencilSurface(depthStencil);
		dev->SetViewport(&oldVp);
		dev->SetScissorRect(&oldScissor);
		dev->SetPixelShaderConstantF(0, oldK[0], 12);
		dev->SetPixelShader(oldPs);
		dev->SetVertexShader(oldVs);
		if (fvf) dev->SetFVF(fvf);   // fvf first, decl last (ssao.cpp AoRestore)
		dev->SetVertexDeclaration(oldDecl);
		dev->SetStreamSource(0, oldStream, oldStreamOff, oldStreamStride);
		dev->SetIndices(oldIndices);
		dev->SetTexture(0, oldTex);
		for (size_t i = 0; i < std::size(kSamplers); ++i) dev->SetSamplerState(0, kSamplers[i], oldSamplers[i]);
		for (size_t i = 0; i < std::size(kStates); ++i) dev->SetRenderState(kStates[i], oldStates[i]);
		for (IUnknown* held : { (IUnknown*)oldTex, (IUnknown*)oldDecl, (IUnknown*)oldVs, (IUnknown*)oldPs, (IUnknown*)oldStream, (IUnknown*)oldIndices })
			if (held) held->Release();
		return masked;
	}

	// botox execute(): render thread (queued) or the main thread under mat_queue_mode 0
	void Execute(const PlayerStencilSettings& st)
	{
		IDirect3DDevice9* dev = g_d3dDev;
		bool drawn = false;
		if (dev && dev != s_dev)   // new device (or first run): everything we hold is someone else's
		{
			ReleaseTargets();
			for (auto*& p : s_ps) p = nullptr;
			s_capsuleVs = nullptr; s_capsuleDecl = nullptr; s_failed = false;
			s_dev = dev;
		}
		IDirect3DSurface9* target = nullptr;
		if (dev && EnsureShaders(dev) && SUCCEEDED(dev->GetRenderTarget(0, &target)) && target)
		{
			IDirect3DSurface9* ds = nullptr;
			if (FAILED(dev->GetDepthStencilSurface(&ds))) ds = nullptr;
			D3DSURFACE_DESC d{}, dd{};
			// the stencil test reads the engine's depth-stencil: the mask target must match its samples and fit inside it
			if (ds && SUCCEEDED(target->GetDesc(&d)) && SUCCEEDED(ds->GetDesc(&dd)) && dd.MultiSampleType == d.MultiSampleType
				&& dd.MultiSampleQuality == d.MultiSampleQuality && dd.Width >= d.Width && dd.Height >= d.Height && Build(dev, d))
				drawn = Draw(dev, target, ds, st);
			if (ds) ds->Release();
			target->Release();
		}
		if (st.glow)
			s_glowLive.store(drawn, std::memory_order_relaxed);
		static bool s_logged = false;
		if (!s_logged && drawn)
		{
			s_logged = true;
			DebugLog("player stencil: first pass drawn (outline %d glow %d inner %d bones %d, %d capsules)",
				st.outline, st.glow, st.inner, st.innerBones, st.nCapsules);
		}
	}

	// ---- fake CFunctors (screenfx.cpp "the call queue"): one per ring slot, so a queued frame
	// keeps its own settings while the main thread fills the next ----
	struct StencilFunctor { void** vfptr; int slot; };
	PlayerStencilSettings s_ring[4];
	unsigned s_next = 0;
	int   __fastcall Fn_AddRef(void*)           { return 1; }
	int   __fastcall Fn_Release(void*)          { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned) { return p; }
	void  __fastcall Fn_Call(void* p)           { MiscHookGuard _g; Execute(s_ring[static_cast<StencilFunctor*>(p)->slot]); }
	void* s_vt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Call };
	StencilFunctor s_fn[4] = { { s_vt, 0 }, { s_vt, 1 }, { s_vt, 2 }, { s_vt, 3 } };
}

void PlayerStencil_Submit(const PlayerStencilSettings& s)
{
	if (s_failed || !g_d3dDev) return;
	const int slot = int(s_next++ & 3);
	s_ring[slot] = s;
	// the glow pass's draws are still in the shaderapi's dynamic buffers: push them out first,
	// or the mask reads a stencil the players are not in yet (screenfx.cpp ScreenFx_FlushRenderContext)
	ScreenFx_FlushRenderContext();
	if (!ScreenFx_QueueFunctor(&s_fn[slot]))
		Fn_Call(&s_fn[slot]);   // mat_queue_mode 0: this thread issues the d3d
}

bool PlayerStencil_GlowLive() { return s_glowLive.load(std::memory_order_relaxed); }
bool PlayerStencil_Failed() { return s_failed; }
void PlayerStencil_ReleaseTargets() { ReleaseTargets(); }
