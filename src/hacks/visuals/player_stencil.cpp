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

	// Two independent target sets, same shaders: 0 = live (screen size), 1 = esp preview (its own
	// small target). Keeping them apart means neither rebuilds the other's D3DPOOL_DEFAULT
	// textures every single frame just because the two sizes differ (Build() below would otherwise
	// see a size mismatch and call ReleaseTargets()+recreate on every draw, live and preview alike).
	enum { kLive = 0, kPreview = 1, kInstances = 2 };
	struct Targets {
		IDirect3DSurface9* msaaSurf = nullptr, *maskSurf = nullptr, *distSurf = nullptr, *blurSurf = nullptr;
		IDirect3DTexture9* maskTex = nullptr, *distTex = nullptr, *blurTex = nullptr;
		UINT w = 0, h = 0;
		D3DFORMAT fmt = D3DFMT_UNKNOWN;
		D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE;
		DWORD msq = 0;
	};
	Targets s_t[kInstances];
	IDirect3DDevice9* s_dev = nullptr;   // the device every resource above belongs to (both instances)

	template <size_t N>
	std::vector<DWORD> Code(const BYTE (&bytes)[N])   // BYTE arrays carry no alignment: copy into DWORDs
	{
		std::vector<DWORD> c((N + 3) / 4);
		memcpy(c.data(), bytes, N);
		return c;
	}

	void ReleaseTargets(int inst)
	{
		Targets& t = s_t[inst];
		for (IDirect3DSurface9** s : { &t.msaaSurf, &t.maskSurf, &t.distSurf, &t.blurSurf }) if (*s) { (*s)->Release(); *s = nullptr; }
		for (IDirect3DTexture9** tex : { &t.maskTex, &t.distTex, &t.blurTex }) if (*tex) { (*tex)->Release(); *tex = nullptr; }
		t.w = t.h = 0; t.fmt = D3DFMT_UNKNOWN; t.ms = D3DMULTISAMPLE_NONE; t.msq = 0;
	}
	void ReleaseAllTargets() { for (int i = 0; i < kInstances; ++i) ReleaseTargets(i); }

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

	bool Build(IDirect3DDevice9* dev, const D3DSURFACE_DESC& d, int inst)
	{
		Targets& t = s_t[inst];
		if (t.maskTex && t.w == d.Width && t.h == d.Height && t.fmt == d.Format && t.ms == d.MultiSampleType && t.msq == d.MultiSampleQuality)
			return true;
		ReleaseTargets(inst);
		auto make = [&](IDirect3DTexture9*& tex, IDirect3DSurface9*& s, D3DFORMAT f) {
			return SUCCEEDED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &tex, nullptr)) && SUCCEEDED(tex->GetSurfaceLevel(0, &s));
		};
		bool ok = make(t.maskTex, t.maskSurf, d.Format) && make(t.distTex, t.distSurf, d.Format)
			&& (make(t.blurTex, t.blurSurf, D3DFMT_A16B16G16R16F) || make(t.blurTex, t.blurSurf, d.Format));
		if (ok && d.MultiSampleType != D3DMULTISAMPLE_NONE)
			ok = SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, d.MultiSampleType, d.MultiSampleQuality, FALSE, &t.msaaSurf, nullptr));
		if (!ok)
		{
			DebugLog("player stencil: target creation failed (inst %d, %ux%u fmt %d ms %d)", inst, d.Width, d.Height, (int)d.Format, (int)d.MultiSampleType);
			ReleaseTargets(inst);
			return false;
		}
		t.w = d.Width; t.h = d.Height; t.fmt = d.Format; t.ms = d.MultiSampleType; t.msq = d.MultiSampleQuality;
		DebugLog("player stencil: targets (inst %d) %ux%u (msaa %d)", inst, t.w, t.h, (int)t.ms);
		return true;
	}

	bool Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* depthStencil, const PlayerStencilSettings& st, int inst)
	{
		Targets& tg = s_t[inst];   // "tg": the function already uses `t` as a local "top" coordinate below
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
		constexpr UINT kConstants = 80;   // c0..c13 settings, c15 wave anchor, c16..c47 player boxes, c48..c79 phases
		float oldK[kConstants][4]{};

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
		dev->GetPixelShaderConstantF(0, oldK[0], kConstants);
		dev->GetTexture(0, &oldTex);
		for (size_t i = 0; i < std::size(kStates); ++i) { dev->GetRenderState(kStates[i], &oldStates[i]); dev->SetRenderState(kStates[i], kValues[i]); }
		for (size_t i = 0; i < std::size(kSamplers); ++i) { dev->GetSamplerState(0, kSamplers[i], &oldSamplers[i]); dev->SetSamplerState(0, kSamplers[i], kSamplerValues[i]); }

		const float W = float(tg.w), H = float(tg.h);
		const D3DVIEWPORT9 fullVp{ 0, 0, tg.w, tg.h, 0.f, 1.f };
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
		dev->SetPixelShaderConstantF(12, st.outlineWave, 1);
		dev->SetPixelShaderConstantF(13, st.outlineWaveColor, 1);
		dev->SetPixelShaderConstantF(15, st.waveAnchor, 1);
		// player boxes: ndc -> this target's uv through the view rect, like the capsules
		if (const int nBoxes = std::min((int)st.waveAnchor[1], PlayerStencilSettings::kMaxBoxes); nBoxes > 0)
		{
			const float hw = 0.5f * float(oldVp.Width), hh = 0.5f * float(oldVp.Height);
			float uvBoxes[PlayerStencilSettings::kMaxBoxes][4]{};
			for (int i = 0; i < nBoxes; i++)
			{
				const float* b = st.boxes[i];
				uvBoxes[i][0] = (vl + (b[0] + 1.f) * hw) / W;
				uvBoxes[i][1] = (vt + (1.f - b[3]) * hh) / H;
				uvBoxes[i][2] = (vl + (b[2] + 1.f) * hw) / W;
				uvBoxes[i][3] = (vt + (1.f - b[1]) * hh) / H;
			}
			dev->SetPixelShaderConstantF(16, uvBoxes[0], nBoxes);
			dev->SetPixelShaderConstantF(48, st.boxPhase[0], nBoxes);
		}

		// ---- mask: red where 2 <= stencil < 128 (visible), green where stencil == 1 (occluded) ----
		dev->SetRenderTarget(0, tg.msaaSurf ? tg.msaaSurf : tg.maskSurf);
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

		const bool masked = !tg.msaaSurf || SUCCEEDED(dev->StretchRect(tg.msaaSurf, nullptr, tg.maskSurf, nullptr, D3DTEXF_NONE));
		dev->SetDepthStencilSurface(nullptr);

		if (masked)
		{
			// ponytail: full-target offscreen passes, empty pixels still run every tap; scissor to the
			// player boxes if it ever shows in frame time (botox's own note)
			dev->SetTexture(0, tg.maskTex);
			if (st.outline)
			{
				dev->SetRenderTarget(0, tg.distSurf);
				dev->SetPixelShader(s_ps[PS_ROWS]);
				rect(0.f, 0.f, W, H);
			}

			if (st.inner && !(st.innerBones && st.nCapsules == 0))
			{
				dev->SetPixelShaderConstantF(7, st.innerReach, 1);
				dev->SetRenderTarget(0, tg.blurSurf);
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
				dev->SetTexture(0, tg.blurTex);
				rect(vl, vt, vr, vb);

				dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				dev->SetTexture(0, tg.maskTex);
				dev->SetPixelShaderConstantF(7, st.gradient, 1);
			}

			if (st.glow)
			{
				dev->SetRenderTarget(0, tg.blurSurf);
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
				dev->SetTexture(0, tg.blurTex);
				rect(vl, vt, vr, vb);
				dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			}
			if (st.outline)
			{
				dev->SetPixelShader(s_ps[PS_COLUMNS]);
				dev->SetTexture(0, tg.distTex);
				rect(vl, vt, vr, vb);
			}
		}

		dev->SetRenderTarget(0, target);
		dev->SetDepthStencilSurface(depthStencil);
		dev->SetViewport(&oldVp);
		dev->SetScissorRect(&oldScissor);
		dev->SetPixelShaderConstantF(0, oldK[0], kConstants);
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

	// botox execute(): render thread (queued) or the main thread under mat_queue_mode 0. inst picks
	// which of the two target sets (live / preview) this queued call draws into; Execute reads
	// whatever is bound on dev at the time it actually runs (see PlayerStencil_Submit's ordering note).
	void Execute(const PlayerStencilSettings& st, int inst)
	{
		IDirect3DDevice9* dev = g_d3dDev;
		bool drawn = false;
		if (dev && dev != s_dev)   // new device (or first run): everything we hold is someone else's
		{
			ReleaseAllTargets();
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
				&& dd.MultiSampleQuality == d.MultiSampleQuality && dd.Width >= d.Width && dd.Height >= d.Height && Build(dev, d, inst))
				drawn = Draw(dev, target, ds, st, inst);
			if (ds) ds->Release();
			target->Release();
		}
		// CSS-specific: live-only. s_glowLive feeds PlayerStencil_GlowLive(), which the LIVE
		// Glow_Render reads to decide whether the engine's own halo backs off (botox glow_enabled()
		// exclusivity, no TF2 analog -- TF2 dropped the legacy/full-glow split entirely). A preview
		// draw must never flip this, or opening the esp preview while the live full-res glow is on
		// would wrongly re-arm the live screen's material-system halo.
		if (st.glow && inst == kLive)
			s_glowLive.store(drawn, std::memory_order_relaxed);
		static bool s_loggedLive = false, s_loggedPreview = false;
		bool& logged = inst == kPreview ? s_loggedPreview : s_loggedLive;
		if (!logged && drawn)
		{
			logged = true;
			DebugLog("player stencil: first pass drawn (inst %d, outline %d glow %d inner %d bones %d, %d capsules)",
				inst, st.outline, st.glow, st.inner, st.innerBones, st.nCapsules);
		}
	}

	// ---- fake CFunctors (screenfx.cpp "the call queue"): one per ring slot, so a queued frame
	// keeps its own settings while the main thread fills the next. One shared ring for both
	// instances (each slot remembers which instance it was submitted for) -- plenty: live submits
	// at most once a frame and preview at most 60 times a second, never both mid-flight per slot ----
	struct StencilFunctor { void** vfptr; int slot; };
	PlayerStencilSettings s_ring[4];
	int s_ringInstance[4] = {};
	unsigned s_next = 0;
	int   __fastcall Fn_AddRef(void*)           { return 1; }
	int   __fastcall Fn_Release(void*)          { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned) { return p; }
	void  __fastcall Fn_Call(void* p)
	{
		MiscHookGuard _g;
		const int slot = static_cast<StencilFunctor*>(p)->slot;
		Execute(s_ring[slot], s_ringInstance[slot]);
	}
	void* s_vt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Call };
	StencilFunctor s_fn[4] = { { s_vt, 0 }, { s_vt, 1 }, { s_vt, 2 }, { s_vt, 3 } };
}

void PlayerStencil_Submit(const PlayerStencilSettings& s, bool bPreview)
{
	if (s_failed || !g_d3dDev) return;
	const int slot = int(s_next++ & 3);
	s_ring[slot] = s;
	s_ringInstance[slot] = bPreview ? kPreview : kLive;
	// the glow pass's draws are still in the shaderapi's dynamic buffers: push them out first,
	// or the mask reads a stencil the players are not in yet (screenfx.cpp ScreenFx_FlushRenderContext).
	// Both this flush and the functor below go through the SAME ICallQueue as every other main-thread
	// render call (SetRenderTarget, the model draws, DrawScreenSpaceRectangle, ...), replayed strictly
	// in enqueue order: as long as the caller (Glow_Render / Glow_RenderPreview) queues this submit
	// before it ever issues a later SetRenderTarget back off its target, Execute's dev->GetRenderTarget(0)
	// still reads that target when this functor actually runs. For the preview that holds because
	// EspPreview pops its render target/view only after Glow_RenderPreview (and this Submit) returns.
	ScreenFx_FlushRenderContext();
	if (!ScreenFx_QueueFunctor(&s_fn[slot]))
		Fn_Call(&s_fn[slot]);   // mat_queue_mode 0: this thread issues the d3d
}

bool PlayerStencil_GlowLive() { return s_glowLive.load(std::memory_order_relaxed); }
bool PlayerStencil_Failed() { return s_failed; }
void PlayerStencil_ReleaseTargets() { ReleaseAllTargets(); }
