// botox hacks/visuals/lsd_chams.cpp + model_shader.h (commit 95ef94a), TF2 port.
//
// The "lsd" chams layer draws a flat white UnlitGeneric carrier (aim/chams.cpp). While armed, the
// DrawIndexedPrimitive detour below swaps the carrier's shaders for the skinned-position vs +
// "70s Melt" ps (shaders/fx_lsd_*.hlsl, fxc at build time: no d3dx9 at runtime).
//
// TF2 departures from botox:
//   - shader constants: TF2's studio vs layout is CS:GO's (sdk2013 common_vs_fxc.h:83 cViewProj c8,
//     :169 cModel[53] c58; compressed normals UBYTE4, imaterial.h:258), so the vs is botox's verbatim
//   - arm/disarm ride the material system call queue (screenfx.cpp ScreenFx_QueueFunctor, the same
//     seam the screen passes use) instead of botox's n_render_queue; each arm is its own fake
//     functor from a ring, so two lsd draws in one frame never share one alpha
//   - DIP (IDirect3DDevice9 slot 82, d3d9.h: DrawPrimitive 81, DrawIndexedPrimitive 82, anchored on
//     EndScene 42 / Present 17 that thread.cpp hooks) is hooked lazily on the first lsd draw, so a
//     cfg without lsd never pays a detour on every draw call
#include "lsd_chams.h"
#include "chams_layers.h"
#include "../../globals/globals.h"
#include "../movement/prediction.h"          // g_globalVars -- sheen's sweep clock
#include "../screen/screenfx.h"              // ScreenFx_QueueFunctor
#include "../screen/shaders/fx_lsd_vs0.h"    // k_fxLsdVs0Bytes (float verts)
#include "../screen/shaders/fx_lsd_vs1.h"    // k_fxLsdVs1Bytes (COMPRESSED_VERTS)
#include "../screen/shaders/fx_lsd_ps.h"     // k_fxLsdPsBytes
#include "../screen/shaders/fx_twotone_vs0.h" // k_fxTwoToneVs0Bytes (float verts)
#include "../screen/shaders/fx_twotone_vs1.h" // k_fxTwoToneVs1Bytes (float verts + tangent stream)
#include "../screen/shaders/fx_twotone_vs2.h" // k_fxTwoToneVs2Bytes (COMPRESSED_VERTS)
#include "../screen/shaders/fx_twotone_ps0.h" // k_fxTwoTonePs0Bytes (no normal map)
#include "../screen/shaders/fx_twotone_ps1.h" // k_fxTwoTonePs1Bytes (normal map s1)
#include "../screen/shaders/fx_twotone_ps2.h" // k_fxTwoTonePs2Bytes (normal map s3)
#include "../screen/shaders/fx_sheen_vs0.h"   // k_fxSheenVs0Bytes (float verts)
#include "../screen/shaders/fx_sheen_vs1.h"   // k_fxSheenVs1Bytes (COMPRESSED_VERTS)
#include "../screen/shaders/fx_sheen_ps.h"    // k_fxSheenPsBytes
#include "../screen/shaders/fx_cham_vs.h"     // k_fxChamVs0 / 1 (float / compressed verts)
#include "../screen/shaders/fx_cham_ps.h"     // k_fxChamPs[mode]
#include "chams_api.h"       // o_ForcedMaterialOverride, Chams_GetSheenCarrierMaterial (NEEDS FROM OTHERS)
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <bcrypt.h>
#include <winhttp.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp")

void DebugLog(const char* fmt, ...);
extern void* g_modelRender;   // IVModelRender (dih, thread.cpp) -- same extern chams.cpp uses
extern void* g_modelInfo;     // IVModelInfoClient (thread.cpp) -- visuals.cpp/movement.cpp already VCall it raw

namespace
{
	using DIP_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	DIP_t o_DIP = nullptr;
	std::atomic<bool> s_hookTried{ false };

	std::atomic<bool> s_armed{ false };
	float s_params[2] = {};                 // render thread only: time, alpha

	IDirect3DVertexShader9* s_vs[2] = {};
	IDirect3DPixelShader9*  s_ps = nullptr;
	bool s_gpuFailed = false;

	// ---- botox model_shader.h ----
	using Defs = std::vector<std::pair<int, int>>;   // kind (0 f, 1 i, 2 b), register
	Defs s_vsDefs, s_psDefs;

	// def literals can leak into the hw register file under the engine's state cache: save + restore each
	void CollectDefs(const DWORD* code, Defs& out)
	{
		for (size_t i = 1; code[i] != 0x0000FFFF;)
		{
			const DWORD token = code[i], op = token & 0xFFFF;
			if (op == D3DSIO_COMMENT) { i += 1 + ((token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT); continue; }
			if (op == D3DSIO_DEF || op == D3DSIO_DEFI || op == D3DSIO_DEFB)
				out.emplace_back(op == D3DSIO_DEF ? 0 : op == D3DSIO_DEFI ? 1 : 2, int(code[i + 1] & D3DSP_REGNUM_MASK));
			i += 1 + ((token & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT);
		}
	}

	// -1 = not a studio decl we can read, 0 = float normals, 1 = UBYTE4 compressed normals
	int VertexCompression(IDirect3DDevice9* dev)
	{
		static IDirect3DVertexDeclaration9* last = nullptr;
		static int result = -1;
		IDirect3DVertexDeclaration9* decl = nullptr;
		if (FAILED(dev->GetVertexDeclaration(&decl)) || !decl)
			return -1;
		if (decl != last)
		{
			D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1] = {};
			UINT n = 0;
			result = -1;
			if (SUCCEEDED(decl->GetDeclaration(nullptr, &n)) && n <= MAXD3DDECLLENGTH + 1 && SUCCEEDED(decl->GetDeclaration(el, &n)))
				for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++)
					if (el[i].Usage == D3DDECLUSAGE_NORMAL && el[i].UsageIndex == 0)
						result = el[i].Type == D3DDECLTYPE_UBYTE4 ? 1 : el[i].Type == D3DDECLTYPE_FLOAT3 ? 0 : -1;
			last = decl;
		}
		decl->Release();
		return result;
	}

	struct Saved { int kind, reg; float v[4]; };
	void SaveConsts(IDirect3DDevice9* d, bool vs, const Defs& defs, std::vector<Saved>& out)
	{
		for (const auto& [kind, reg] : defs)
		{
			Saved s{ kind, reg, {} };
			if (kind == 0)      vs ? d->GetVertexShaderConstantF(reg, s.v, 1) : d->GetPixelShaderConstantF(reg, s.v, 1);
			else if (kind == 1) vs ? d->GetVertexShaderConstantI(reg, (int*)s.v, 1) : d->GetPixelShaderConstantI(reg, (int*)s.v, 1);
			else                vs ? d->GetVertexShaderConstantB(reg, (BOOL*)s.v, 1) : d->GetPixelShaderConstantB(reg, (BOOL*)s.v, 1);
			out.push_back(s);
		}
	}
	void RestoreConsts(IDirect3DDevice9* d, bool vs, const std::vector<Saved>& in)
	{
		for (const Saved& s : in)
		{
			if (s.kind == 0)      vs ? d->SetVertexShaderConstantF(s.reg, s.v, 1) : d->SetPixelShaderConstantF(s.reg, s.v, 1);
			else if (s.kind == 1) vs ? d->SetVertexShaderConstantI(s.reg, (const int*)s.v, 1) : d->SetPixelShaderConstantI(s.reg, (const int*)s.v, 1);
			else                  vs ? d->SetVertexShaderConstantB(s.reg, (const BOOL*)s.v, 1) : d->SetPixelShaderConstantB(s.reg, (const BOOL*)s.v, 1);
		}
	}

	template <size_t N>
	std::vector<DWORD> Code(const BYTE (&bytes)[N])   // BYTE arrays carry no alignment: copy into DWORDs
	{
		std::vector<DWORD> c((N + 3) / 4);
		memcpy(c.data(), bytes, N);
		return c;
	}

	bool GpuReady(IDirect3DDevice9* dev)
	{
		if (s_ps) return true;
		if (s_gpuFailed) return false;
		const auto vs0 = Code(k_fxLsdVs0Bytes), vs1 = Code(k_fxLsdVs1Bytes), ps = Code(k_fxLsdPsBytes);
		bool ok = SUCCEEDED(dev->CreateVertexShader(vs0.data(), &s_vs[0])) && SUCCEEDED(dev->CreateVertexShader(vs1.data(), &s_vs[1]))
			&& SUCCEEDED(dev->CreatePixelShader(ps.data(), &s_ps));
		if (ok)
		{
			CollectDefs(vs0.data(), s_vsDefs);   // both vs variants share the one def (c0)
			CollectDefs(ps.data(), s_psDefs);
			DebugLog("LSD: shaders ready (%d vs defs, %d ps defs)", int(s_vsDefs.size()), int(s_psDefs.size()));
			return true;
		}
		DebugLog("LSD: gpu FAILED -- lsd layers draw as flat white");
		for (auto*& v : s_vs) if (v) { v->Release(); v = nullptr; }
		if (s_ps) { s_ps->Release(); s_ps = nullptr; }
		s_gpuFailed = true;
		return false;
	}

	HRESULT DrawLsd(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader / unreadable decl = the carrier draws as plain flat chams
		const int compressed = GpuReady(dev) ? VertexCompression(dev) : -1;
		if (compressed < 0)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		D3DVIEWPORT9 vp = {};
		dev->GetViewport(&vp);
		const float k[2][4] = { { s_params[0], s_params[1], 0.f, 0.f },
			{ float(vp.X), float(vp.Y), float(vp.Width ? vp.Width : 1), float(vp.Height ? vp.Height : 1) } };

		DWORD oldSrgb = 0;
		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		float oldPsK[2][4] = {};
		static std::vector<Saved> savedVs, savedPs;   // render thread only
		savedVs.clear(); savedPs.clear();

		dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);
		dev->GetPixelShaderConstantF(20, oldPsK[0], 2);
		SaveConsts(dev, true, s_vsDefs, savedVs);
		SaveConsts(dev, false, s_psDefs, savedPs);

		// shadertoy colours are display values: no linear -> srgb on write
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetVertexShader(s_vs[compressed]);
		dev->SetPixelShader(s_ps);
		dev->SetPixelShaderConstantF(20, k[0], 2);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
		dev->SetPixelShaderConstantF(20, oldPsK[0], 2);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		return hr;
	}

	// ---- botox hacks/visuals/two_tone_chams.cpp ----
	std::atomic<bool> s_ttArmed{ false };
	float s_ttLight[4] = {}, s_ttDark[4] = {}, s_ttParams[2] = {};   // render thread only
	bool  s_ttIgnorez = false, s_ttTranslucent = false;

	// vs: plain, plain + tangent stream, compressed. ps: no normal map, normal map in s1, s3
	IDirect3DVertexShader9* s_ttVs[3] = {};
	IDirect3DPixelShader9*  s_ttPs[3] = {};
	Defs s_ttVsDefs, s_ttPsDefs;
	bool s_ttGpuFailed = false;
	// ponytail: keyed by shader pointer, never cleared (botox clears on device lost); a material
	// reload that recreates an engine shader at a freed address could reuse a stale sampler index
	std::unordered_map<IDirect3DPixelShader9*, int> s_bumpSamplers;
	int s_ttLogged = 0;

	bool TwoToneGpuReady(IDirect3DDevice9* dev)
	{
		if (s_ttPs[0]) return true;
		if (s_ttGpuFailed) return false;
		const std::vector<DWORD> vs[3] = { Code(k_fxTwoToneVs0Bytes), Code(k_fxTwoToneVs1Bytes), Code(k_fxTwoToneVs2Bytes) };
		const std::vector<DWORD> ps[3] = { Code(k_fxTwoTonePs0Bytes), Code(k_fxTwoTonePs1Bytes), Code(k_fxTwoTonePs2Bytes) };
		bool ok = true;
		for (int i = 0; i < 3 && ok; i++)
		{
			ok = SUCCEEDED(dev->CreateVertexShader(vs[i].data(), &s_ttVs[i])) && SUCCEEDED(dev->CreatePixelShader(ps[i].data(), &s_ttPs[i]));
			if (ok) { CollectDefs(vs[i].data(), s_ttVsDefs); CollectDefs(ps[i].data(), s_ttPsDefs); }   // defs differ per variant: union
		}
		if (ok)
		{
			DebugLog("TWO: shaders ready (%d vs defs, %d ps defs)", int(s_ttVsDefs.size()), int(s_ttPsDefs.size()));
			return true;
		}
		DebugLog("TWO: gpu FAILED -- two tone layers draw as the model's own materials");
		for (auto*& v : s_ttVs) if (v) { v->Release(); v = nullptr; }
		for (auto*& p : s_ttPs) if (p) { p->Release(); p = nullptr; }
		s_ttVsDefs.clear(); s_ttPsDefs.clear();
		s_ttGpuFailed = true;
		return false;
	}

	bool HasTangentStream(IDirect3DDevice9* dev)
	{
		IDirect3DVertexDeclaration9* decl = nullptr;
		if (FAILED(dev->GetVertexDeclaration(&decl)) || !decl)
			return false;
		D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1] = {};
		UINT n = 0;
		bool result = false;
		if (SUCCEEDED(decl->GetDeclaration(nullptr, &n)) && n <= MAXD3DDECLLENGTH + 1 && SUCCEEDED(decl->GetDeclaration(el, &n)))
			for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++)
				result |= el[i].Usage == D3DDECLUSAGE_TANGENT && el[i].UsageIndex == 0;
		decl->Release();
		return result;
	}

	// botox reads the sampler with D3DXGetShaderConstantTable; no d3dx9 here, so the CTAB comment block
	// is walked by hand (d3dx9shader.h D3DXSHADER_CONSTANTTABLE / D3DXSHADER_CONSTANTINFO layout).
	// Returns the sampler register of `name`, -1 not found, -2 no constant table.
	int CtabSampler(const DWORD* code, size_t dwords, const char* const* names, int nNames)
	{
		for (size_t i = 1; i < dwords && code[i] != 0x0000FFFF;)
		{
			const DWORD token = code[i], op = token & 0xFFFF;
			const size_t len = op == D3DSIO_COMMENT ? (token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT
			                                        : (token & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT;
			if (op == D3DSIO_COMMENT && len >= 8 && i + 1 + len <= dwords && code[i + 1] == MAKEFOURCC('C', 'T', 'A', 'B'))
			{
				const BYTE* base = reinterpret_cast<const BYTE*>(&code[i + 2]);
				const size_t size = (len - 1) * 4;
				const DWORD* hdr = reinterpret_cast<const DWORD*>(base);   // Size, Creator, Version, Constants, ConstantInfo, Flags, Target
				const DWORD count = hdr[3], info = hdr[4];
				if (info > size || count > (size - info) / 20) return -2;
				for (DWORD c = 0; c < count; c++)
				{
					const BYTE* ci = base + info + c * 20;   // DWORD Name, WORD RegisterSet, WORD RegisterIndex, WORD RegisterCount, WORD Reserved, DWORD TypeInfo, DWORD DefaultValue
					const DWORD nameOff = *reinterpret_cast<const DWORD*>(ci);
					const WORD regSet = *reinterpret_cast<const WORD*>(ci + 4), regIdx = *reinterpret_cast<const WORD*>(ci + 6);
					if (nameOff >= size || regSet != 3 /* D3DXRS_SAMPLER */) continue;
					const char* nm = reinterpret_cast<const char*>(base + nameOff);
					const size_t maxLen = size - nameOff;
					for (int k = 0; k < nNames; k++)
						if (strnlen(nm, maxLen) < maxLen && strcmp(nm, names[k]) == 0) return int(regIdx);
				}
				return -1;
			}
			i += 1 + len;
		}
		return -2;
	}

	// normal map register of the engine pixel shader, by name from its constant table (TF2 skin/phong
	// "NormalMapSampler", bumped vertexlit "BumpmapSampler"). -1 none, -2 no table. cached per shader
	int BumpSampler(IDirect3DPixelShader9* shader)
	{
		if (!shader) return -1;
		if (const auto found = s_bumpSamplers.find(shader); found != s_bumpSamplers.end())
			return found->second;
		int result = -2;
		UINT size = 0;
		if (SUCCEEDED(shader->GetFunction(nullptr, &size)) && size)
		{
			std::vector<DWORD> code((size + 3) / 4);
			static const char* const kNames[2] = { "NormalMapSampler", "BumpmapSampler" };
			if (SUCCEEDED(shader->GetFunction(code.data(), &size)))
				result = CtabSampler(code.data(), code.size(), kNames, 2);
		}
		if (s_ttLogged < 32)
		{
			s_ttLogged++;
			DebugLog("TWO: ps %p bump %d (-1 none, -2 no ctab)", (void*)shader, result);
		}
		s_bumpSamplers.emplace(shader, result);
		return result;
	}

	HRESULT DrawTwoTone(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader / not a studio mesh = the model's own material draws as is
		const int compressed = TwoToneGpuReady(dev) ? VertexCompression(dev) : -1;
		if (compressed < 0)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);

		const int bump = BumpSampler(oldPs);
		const int vsIndex = compressed ? 2 : HasTangentStream(dev) ? 1 : 0;
		const int psIndex = bump == 1 ? 1 : bump == 3 ? 2 : 0;

		DWORD oldSrgb = 0, oldZFunc = 0, oldZWrite = 0, oldAlphaTest = 0, oldAlphaRef = 0, oldBlend = 0, oldSrc = 0, oldDest = 0, oldColorWrite = 0;
		dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
		dev->GetRenderState(D3DRS_ZFUNC, &oldZFunc);
		dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
		dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
		dev->GetRenderState(D3DRS_ALPHAREF, &oldAlphaRef);
		dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldBlend);
		dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
		dev->GetRenderState(D3DRS_DESTBLEND, &oldDest);
		dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);

		const float psK[3][4] = { { s_ttLight[0], s_ttLight[1], s_ttLight[2], s_ttLight[3] },
			{ s_ttDark[0], s_ttDark[1], s_ttDark[2], s_ttDark[3] },
			{ s_ttParams[0], s_ttParams[1], oldAlphaTest ? 1.f : 0.f, float(oldAlphaRef & 0xFF) / 255.f } };

		float oldPsK[3][4] = {};
		static std::vector<Saved> savedVs, savedPs;   // render thread only
		savedVs.clear(); savedPs.clear();

		dev->GetPixelShaderConstantF(30, oldPsK[0], 3);
		SaveConsts(dev, true, s_ttVsDefs, savedVs);
		SaveConsts(dev, false, s_ttPsDefs, savedPs);

		// picked colours are display values: no linear -> srgb on write. alpha test = clip in our ps
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

		// prepass lays depth, colour pass then only hits each pixel's nearest surface (equal depth must pass)
		if (s_ttIgnorez)
			dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		else if (s_ttTranslucent && (oldZFunc == D3DCMP_LESS || oldZFunc == D3DCMP_GREATER))
			dev->SetRenderState(D3DRS_ZFUNC, oldZFunc == D3DCMP_LESS ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL);

		dev->SetVertexShader(s_ttVs[vsIndex]);
		dev->SetPixelShader(s_ttPs[psIndex]);
		dev->SetPixelShaderConstantF(30, psK[0], 3);

		// translucent: own blend whatever the material asked (additive, opaque snapshot). not ignorez = depth only
		// prepass first so inner parts (far arm, legs, weapon) never show through. ignorez can't: depth there is the world
		if (s_ttTranslucent)
		{
			if (!s_ttIgnorez)
			{
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
				dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				o_DIP(dev, type, base, minV, nV, start, prims);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldColorWrite);
			}
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
		if (s_ttIgnorez || s_ttTranslucent)
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
		dev->SetRenderState(D3DRS_ZFUNC, oldZFunc);
		dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldBlend);
		dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
		dev->SetRenderState(D3DRS_DESTBLEND, oldDest);
		dev->SetPixelShaderConstantF(30, oldPsK[0], 3);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		return hr;
	}

	// ---- shader chams (fx_cham_*.hlsl, Atlas-TF2 model.hlsl): every material slot but original / lsd / two tone ----
	std::atomic<bool> s_chArmed{ false };
	float s_chColor[4] = {}, s_chTime = 0.f;   // render thread only
	int   s_chMode = 0;
	bool  s_chIgnorez = false;

	IDirect3DVertexShader9* s_chVs[2] = {};
	IDirect3DPixelShader9*  s_chPs[ChamsLayers::Count] = {};
	static_assert(sizeof(k_fxChamPs) / sizeof(k_fxChamPs[0]) == ChamsLayers::Count, "fx_cham_build.sh COUNT != ChamsLayers::Count");
	Defs s_chVsDefs, s_chPsDefs;
	bool s_chReady = false, s_chGpuFailed = false;

	std::vector<DWORD> Code(const BYTE* bytes, unsigned size)
	{
		std::vector<DWORD> c((size + 3) / 4);
		memcpy(c.data(), bytes, size);
		return c;
	}

	bool ChamGpuReady(IDirect3DDevice9* dev)
	{
		if (s_chReady) return true;
		if (s_chGpuFailed) return false;
		const std::vector<DWORD> vs[2] = { Code(k_fxChamVs0), Code(k_fxChamVs1) };
		bool ok = true;
		for (int i = 0; i < 2 && ok; i++)
			if ((ok = SUCCEEDED(dev->CreateVertexShader(vs[i].data(), &s_chVs[i]))))
				CollectDefs(vs[i].data(), s_chVsDefs);
		for (int m = 0; m < ChamsLayers::Count && ok; m++)
		{
			if (!k_fxChamPs[m].bytes) continue;
			const auto ps = Code(k_fxChamPs[m].bytes, k_fxChamPs[m].size);
			if ((ok = SUCCEEDED(dev->CreatePixelShader(ps.data(), &s_chPs[m]))))
				CollectDefs(ps.data(), s_chPsDefs);   // union of every mode's defs
		}
		if (ok)
		{
			DebugLog("CHAM: shaders ready (%d vs defs, %d ps defs)", int(s_chVsDefs.size()), int(s_chPsDefs.size()));
			return s_chReady = true;
		}
		DebugLog("CHAM: gpu FAILED -- shader chams draw as the plain white carrier");
		for (auto*& v : s_chVs) if (v) { v->Release(); v = nullptr; }
		for (auto*& p : s_chPs) if (p) { p->Release(); p = nullptr; }
		s_chVsDefs.clear(); s_chPsDefs.clear();
		s_chGpuFailed = true;
		return false;
	}

	// Atlas model_render.hpp surface_t::additive / blends
	bool ChamAdditive(int m)
	{
		using namespace ChamsLayers;
		return m == Glow || m == Metallic || m == Wet || m == Fresnel || m == Neon || m == Soap || m == Aurora || m == Aquarium || m == Sky;
	}

	HRESULT DrawCham(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader / not a studio mesh = the carrier (plain white vertexlit) draws as is
		const int compressed = ChamGpuReady(dev) ? VertexCompression(dev) : -1;
		IDirect3DPixelShader9* ps = s_chMode > 0 && s_chMode < ChamsLayers::Count ? s_chPs[s_chMode] : nullptr;
		if (compressed < 0 || !ps)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		const bool additive = ChamAdditive(s_chMode);
		const bool blends = additive || s_chColor[3] < 1.f || s_chMode == ChamsLayers::Crystal || s_chMode == ChamsLayers::Network || s_chMode == ChamsLayers::Grid;

		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);

		DWORD oldSrgb = 0, oldZFunc = 0, oldZWrite = 0, oldAlphaTest = 0, oldBlend = 0, oldSrc = 0, oldDest = 0, oldColorWrite = 0, oldTexSrgb = 0;
		dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
		dev->GetRenderState(D3DRS_ZFUNC, &oldZFunc);
		dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
		dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
		dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldBlend);
		dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
		dev->GetRenderState(D3DRS_DESTBLEND, &oldDest);
		dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);
		dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldTexSrgb);

		const float psK[2][4] = { { s_chColor[0], s_chColor[1], s_chColor[2], s_chColor[3] }, { s_chTime, 0.f, 0.f, 0.f } };
		float oldPsK[2][4] = {};
		static std::vector<Saved> savedVs, savedPs;   // render thread only
		savedVs.clear(); savedPs.clear();
		dev->GetPixelShaderConstantF(40, oldPsK[0], 2);
		SaveConsts(dev, true, s_chVsDefs, savedVs);
		SaveConsts(dev, false, s_chPsDefs, savedPs);

		// colours are display values (Atlas: srgb write off), basetexture read raw to match
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
		if (s_chIgnorez)
			dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		else if (blends && (oldZFunc == D3DCMP_LESS || oldZFunc == D3DCMP_GREATER))
			dev->SetRenderState(D3DRS_ZFUNC, oldZFunc == D3DCMP_LESS ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL);

		dev->SetVertexShader(s_chVs[compressed]);
		dev->SetPixelShader(ps);
		dev->SetPixelShaderConstantF(40, psK[0], 2);

		// Atlas draw_depth_first: blended = depth only prepass, then colour on the nearest surface only
		if (blends)
		{
			if (!s_chIgnorez)
			{
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
				dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				o_DIP(dev, type, base, minV, nV, start, prims);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldColorWrite);
			}
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			dev->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
		}
		else
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		if (s_chIgnorez || blends)
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
		dev->SetRenderState(D3DRS_ZFUNC, oldZFunc);
		dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldBlend);
		dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
		dev->SetRenderState(D3DRS_DESTBLEND, oldDest);
		dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldTexSrgb);
		dev->SetPixelShaderConstantF(40, oldPsK[0], 2);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		return hr;
	}

	// weapon sheen's armed flag + draw function are defined below (after two-tone, in their own
	// section); forward-declared here so h_DIP (which dispatches to all three) can see them --
	// the alternative (moving ~460 lines above h_DIP) would bury the sheen section between lsd
	// and two-tone instead of after them.
	std::atomic<bool> s_sheenArmed{ false };
	HRESULT DrawSheen(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims);

	HRESULT __stdcall h_DIP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// every draw in the game lands here: one guard (eject drains it before freeing trampolines,
		// globals.h) and relaxed loads unless armed. Round-4's note that the TF2 killstreak "normal
		// blend sheen" branch that used to sit here (g_sheenNormal, SheenPassModel/DrawSheenNormal,
		// deleted -- CS:S has no killstreak system) meant NO DIP intercept was needed for weapon
		// sheen at all was wrong: botox's real weapon sheen IS a DIP intercept, see the sheen section
		// above. Sheen checked first: it only arms for the one extra viewmodel draw a cham layer
		// draw could in principle overlap (narrowest window of the three).
		MiscHookGuard _g;
		if (s_sheenArmed.load(std::memory_order_relaxed))
			return DrawSheen(dev, type, base, minV, nV, start, prims);
		if (s_ttArmed.load(std::memory_order_relaxed))
			return DrawTwoTone(dev, type, base, minV, nV, start, prims);
		if (s_armed.load(std::memory_order_relaxed))
			return DrawLsd(dev, type, base, minV, nV, start, prims);
		if (s_chArmed.load(std::memory_order_relaxed))
			return DrawCham(dev, type, base, minV, nV, start, prims);
		return o_DIP(dev, type, base, minV, nV, start, prims);
	}

	void EnsureHook()
	{
		// main thread (lsd / two tone begin) and render thread (SheenNormal_Tick) both get here:
		// one winner, or the loser's MH_CreateHook fails and nulls o_DIP under a live hook
		if (!g_d3dDev || s_hookTried.exchange(true)) return;
		void** vt = *reinterpret_cast<void***>(g_d3dDev);
		if (MH_CreateHook(vt[82], (LPVOID)&h_DIP, (LPVOID*)&o_DIP) == MH_OK && MH_EnableHook(vt[82]) == MH_OK)
			DebugLog("LSD: DrawIndexedPrimitive hook installed (slot 82)");
		else
		{
			o_DIP = nullptr;
			DebugLog("LSD: DrawIndexedPrimitive hook FAILED -- lsd layers draw as flat white");
		}
	}

	// ---- fake CFunctors (layout + why: screenfx.cpp "the call queue") ----
	struct LsdFunctor { void** vfptr; float time, alpha; };
	int   __fastcall Fn_AddRef(void*)              { return 1; }
	int   __fastcall Fn_Release(void*)             { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned)    { return p; }
	void  __fastcall Fn_Arm(void* p)
	{
		auto* f = static_cast<LsdFunctor*>(p);
		s_params[0] = f->time;
		s_params[1] = f->alpha;
		s_armed.store(true, std::memory_order_relaxed);
	}
	void  __fastcall Fn_Disarm(void*) { s_armed.store(false, std::memory_order_relaxed); }

	void* s_armVt[4]    = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Arm };
	void* s_disarmVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Disarm };
	// ponytail: ring of 1024 arms, the render thread trails by about one frame; a frame with more
	// lsd layer draws than that would recycle an arm still waiting in the queue (wrong time/alpha)
	LsdFunctor s_arms[1024];
	unsigned   s_armNext = 0;
	LsdFunctor s_disarm = { s_disarmVt, 0.f, 0.f };

	// two tone: same ring scheme, one arm per layer draw
	struct TtFunctor { void** vfptr; float light[4], dark[4], params[2]; bool ignorez; };
	void __fastcall Fn_TtArm(void* p)
	{
		auto* f = static_cast<TtFunctor*>(p);
		memcpy(s_ttLight, f->light, sizeof(s_ttLight));
		memcpy(s_ttDark, f->dark, sizeof(s_ttDark));
		memcpy(s_ttParams, f->params, sizeof(s_ttParams));
		s_ttIgnorez = f->ignorez;
		s_ttTranslucent = f->light[3] < 1.f || f->dark[3] < 1.f;
		s_ttArmed.store(true, std::memory_order_relaxed);
	}
	void __fastcall Fn_TtDisarm(void*) { s_ttArmed.store(false, std::memory_order_relaxed); }

	void* s_ttArmVt[4]    = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_TtArm };
	void* s_ttDisarmVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_TtDisarm };
	TtFunctor s_ttArms[1024];
	unsigned  s_ttArmNext = 0;
	TtFunctor s_ttDisarm = { s_ttDisarmVt };

	// shader chams: same ring scheme
	struct ChFunctor { void** vfptr; float color[4]; float time; int mode; bool ignorez; };
	void __fastcall Fn_ChArm(void* p)
	{
		auto* f = static_cast<ChFunctor*>(p);
		memcpy(s_chColor, f->color, sizeof(s_chColor));
		s_chTime = f->time;
		s_chMode = f->mode;
		s_chIgnorez = f->ignorez;
		s_chArmed.store(true, std::memory_order_relaxed);
	}
	void __fastcall Fn_ChDisarm(void*) { s_chArmed.store(false, std::memory_order_relaxed); }

	void* s_chArmVt[4]    = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_ChArm };
	void* s_chDisarmVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_ChDisarm };
	ChFunctor s_chArms[1024];
	unsigned  s_chArmNext = 0;
	ChFunctor s_chDisarm = { s_chDisarmVt };

	// ==== weapon sheen (botox hacks/visuals/weapon_sheen.cpp + model_shader.h, 1:1) ================
	// Round-5 fix: lsd_chams.h/.cpp's own round-4 note ("botox's real weapon sheen needs no DIP
	// intercept") and world_fx2.cpp's Fx2_SheenTick ($envmaptint poked on the weapon's own material)
	// were both wrong -- read against the real botox csgo-sdk source, weapon_sheen.cpp IS a third
	// DIP-shader layer, same shape as lsd/two-tone above: a forced-material-override additive-black
	// carrier (Chams_GetSheenCarrierMaterial, aim/chams.cpp -- NEEDS FROM OTHERS, this TU can't build
	// a KeyValues: INCLUDE RULE) draws a SECOND pass of the viewmodel weapon while armed, and this
	// DIP detour swaps in the real envmap-cubemap + animated-mask-texture + model-space-UV-sweep
	// shader (fx_sheen_vs/ps.hlsl, ported verbatim from weapon_sheen.cpp's k_vs/k_ps) for that one
	// draw. $envmaptint alone (the old approach) can only pulse the WHOLE gun's ambient reflection
	// uniformly -- it has no way to paint a travelling band, which is the entire visual. Assets
	// (animated mask + 2 cubemaps) are the SAME botox CS:GO asset set (game-independent sheen
	// VTFs, not CS:GO content) fetched from the same URL + sha256-pinned, botox's own load_assets.
	constexpr float kSheenFrameRate = 25.f;
	// bright core u per frame of the sha-pinned animatedsheen0.vtf (box-smoothed column peak, red channel)
	constexpr float kSheenMaskCore[60] = { 0.000f, 0.000f, 0.020f, 0.020f, 0.020f, 0.020f, 0.020f, 0.035f, 0.043f, 0.059f, 0.066f, 0.090f,
	                                        0.105f, 0.121f, 0.145f, 0.168f, 0.191f, 0.215f, 0.238f, 0.270f, 0.301f, 0.332f, 0.355f, 0.395f,
	                                        0.418f, 0.457f, 0.488f, 0.520f, 0.551f, 0.582f, 0.613f, 0.645f, 0.684f, 0.715f, 0.746f, 0.777f,
	                                        0.816f, 0.848f, 0.879f, 0.902f, 0.949f, 0.973f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f,
	                                        0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f, 0.980f };

	enum { kSheenAssetMask, kSheenAssetCube, kSheenAssetCubeHdr, kSheenAssetCount };
	struct SheenAssetDef { const char* name; const char* sha256; };
	constexpr SheenAssetDef kSheenAssets[kSheenAssetCount] = {
		{ "animatedsheen0.vtf",        "2cdd1054c6ce25a8973c34cf00b0e004746eaaa6438d6a2c466f04d54de1a00c" },
		{ "cubemap_sheen001.vtf",      "7bed7b6d9308932b7c5d8e74857836786bb5486c17a289607d5e3ad75afa7a0b" },
		{ "cubemap_sheen001.hdr.vtf",  "4fd062162bcdc7d0564495404ceb4255e9c8295435398c40a7ceaf0f16f84f3d" },
	};
	constexpr wchar_t kSheenHost[] = L"raw.githubusercontent.com";
	constexpr const char* kSheenUrlPath = "/gumballpanties-sudo/botox-assets/main/sheen/";
	constexpr const char* kSheenCacheDir = "C:\\botox\\sheen\\";

	struct SheenVtf
	{
		std::vector<unsigned char> bytes;
		int width = 0, height = 0, mips = 0, frames = 0, faces = 0;
		D3DFORMAT format = D3DFMT_UNKNOWN;
		size_t data = 0;

		size_t LevelSize(int level) const
		{
			const int w = (std::max)(1, width >> level), h = (std::max)(1, height >> level);
			if (format == D3DFMT_DXT1)
				return size_t((std::max)(1, (w + 3) / 4)) * (std::max)(1, (h + 3) / 4) * 8;
			return size_t(w) * h * 8;
		}
		// vtf high res block: smallest mip first, then frame, then face
		const unsigned char* Image(int level, int frame, int face) const
		{
			size_t offset = data;
			for (int l = mips - 1; l > level; l--)
				offset += LevelSize(l) * frames * faces;
			return bytes.data() + offset + (size_t(frame) * faces + face) * LevelSize(level);
		}
	};

	struct SheenPlanes { float u[4]; float v[4]; };
	struct SheenParams { float u[4], v[4], tint[4], band[4]; int frame = 0; };

	std::atomic<int> s_sheenState{ 0 };   // 0 idle, 1 loading, 2 ready, 3 failed
	SheenVtf s_sheenVtf[kSheenAssetCount];
	std::unordered_map<std::string, SheenPlanes> s_sheenPlanes;

	IDirect3DVertexShader9* s_sheenVs[2] = {};
	IDirect3DPixelShader9*  s_sheenPs = nullptr;
	bool s_sheenGpuFailed = false;
	Defs s_sheenVsDefs, s_sheenPsDefs;
	std::vector<IDirect3DTexture9*> s_sheenMasks;
	IDirect3DCubeTexture9* s_sheenCubes[2] = {};   // [0] sdr, [1] hdr

	// s_sheenArmed is forward-declared + defined above, before h_DIP
	SheenParams s_sheenParams{};            // render thread only
	float s_sheenNextStart = 0.f;           // main thread: botox begin()'s throttle

	template <typename T>
	T SheenReadAt(const std::vector<unsigned char>& bytes, size_t offset)
	{
		T v{};
		std::memcpy(&v, bytes.data() + offset, sizeof(v));
		return v;
	}

	bool ParseSheenVtf(SheenVtf& vtf)
	{
		const auto& bytes = vtf.bytes;
		if (bytes.size() < 80 || std::memcmp(bytes.data(), "VTF", 4) != 0 || SheenReadAt<int>(bytes, 4) != 7)
			return false;

		const int minor = SheenReadAt<int>(bytes, 8);
		const int headerSize = SheenReadAt<int>(bytes, 12);
		const int flags = SheenReadAt<int>(bytes, 20);
		const int firstFrame = SheenReadAt<unsigned short>(bytes, 26);
		const int format = SheenReadAt<int>(bytes, 52);

		vtf.width  = SheenReadAt<unsigned short>(bytes, 16);
		vtf.height = SheenReadAt<unsigned short>(bytes, 18);
		vtf.frames = SheenReadAt<unsigned short>(bytes, 24);
		vtf.mips   = bytes[56];
		vtf.format = format == 13 ? D3DFMT_DXT1 : format == 24 ? D3DFMT_A16B16G16R16F : D3DFMT_UNKNOWN;
		// pre 7.5 envmaps carry a 7th spheremap face
		vtf.faces = (flags & 0x4000) ? ((minor < 5 && firstFrame != 0xffff) ? 7 : 6) : 1;

		if (vtf.format == D3DFMT_UNKNOWN || vtf.width <= 0 || vtf.height <= 0 || vtf.frames <= 0 || vtf.mips <= 0 || vtf.mips > 16)
			return false;

		size_t total = 0;
		for (int level = 0; level < vtf.mips; level++)
			total += vtf.LevelSize(level) * vtf.frames * vtf.faces;

		if (headerSize < 80 || total + headerSize > bytes.size())
			return false;

		vtf.data = bytes.size() - total;
		return true;
	}

	std::string SheenSha256Hex(const std::vector<unsigned char>& data)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		unsigned char digest[32] = {};

		const bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
		                BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)) &&
		                BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data.data()), (ULONG)data.size(), 0)) &&
		                BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));

		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		if (!ok) return {};

		char hex[65] = {};
		for (int i = 0; i < 32; i++) sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
		return hex;
	}

	std::vector<unsigned char> SheenReadFile(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) return {};
		return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	}

	// WinHttp GET over HTTPS -- same pattern mediaplayer.cpp's LrclibGet / webview2host.cpp's
	// bandwidth probe already use in this DLL, raw bytes instead of a text body. 8MB cap (these
	// VTFs are a few hundred KB each).
	bool SheenDownload(const wchar_t* host, const std::string& path, std::vector<unsigned char>& out)
	{
		out.clear();
		const std::wstring wpath(path.begin(), path.end());
		HINTERNET session = WinHttpOpen(L"botox", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session) return false;
		WinHttpSetTimeouts(session, 5000, 5000, 15000, 15000);

		HINTERNET conn = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
		                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
		                     : nullptr;
		bool ok = false;
		if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		    WinHttpReceiveResponse(req, nullptr))
		{
			DWORD status = 0, statusSize = sizeof(status);
			if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			                        &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status == 200)
			{
				unsigned char buffer[8192];
				DWORD read = 0;
				while (WinHttpReadData(req, buffer, sizeof(buffer), &read) && read && out.size() < 8u * 1024 * 1024)
					out.insert(out.end(), buffer, buffer + read);
				ok = !out.empty();
			}
		}
		if (req) WinHttpCloseHandle(req);
		if (conn) WinHttpCloseHandle(conn);
		WinHttpCloseHandle(session);
		return ok;
	}

	// background thread (botox load_assets, 1:1): cache-file + sha256 check, download once on a
	// miss/mismatch, parse. Any failure parks the feature off (state 3) instead of crashing.
	void SheenLoadAssets()
	{
		CreateDirectoryA("C:\\botox", nullptr);
		CreateDirectoryA("C:\\botox\\sheen", nullptr);

		for (int i = 0; i < kSheenAssetCount; i++)
		{
			const std::string path = std::string(kSheenCacheDir) + kSheenAssets[i].name;
			SheenVtf& vtf = s_sheenVtf[i];

			for (int attempt = 0; attempt < 2; attempt++)
			{
				vtf.bytes = SheenReadFile(path);
				if (!vtf.bytes.empty() && SheenSha256Hex(vtf.bytes) == kSheenAssets[i].sha256)
					break;

				vtf.bytes.clear();
				if (attempt != 0) break;
				std::vector<unsigned char> downloaded;
				if (!SheenDownload(kSheenHost, std::string(kSheenUrlPath) + kSheenAssets[i].name, downloaded))
					break;
				std::ofstream out(path, std::ios::binary);
				if (out) out.write(reinterpret_cast<const char*>(downloaded.data()), (std::streamsize)downloaded.size());
			}

			if (vtf.bytes.empty() || !ParseSheenVtf(vtf))
			{
				DebugLog("SHEEN: %s missing or bad hash/parse, expected sha256 %s (download from %s%s failed or mismatched)",
				         kSheenAssets[i].name, kSheenAssets[i].sha256, kSheenUrlPath, kSheenAssets[i].name);
				s_sheenState.store(3, std::memory_order_release);
				return;
			}
		}
		DebugLog("SHEEN: assets ready (%d mask frames)", s_sheenVtf[kSheenAssetMask].frames);
		s_sheenState.store(2, std::memory_order_release);
	}

	void EnsureSheenAssetsLoading()
	{
		int idle = 0;
		if (s_sheenState.compare_exchange_strong(idle, 1))
			std::thread(SheenLoadAssets).detach();
	}

	// model-space sweep axis (botox model_planes, weapon_sheen.cpp:308-378): along = the model's
	// longest local axis (muzzle direction), across = 2nd longest. botox measures this off the raw
	// VVD vertex block; this TU has no CS:S VVD accessor, so it uses IVModelInfo::GetModelBounds
	// instead (slot 6, counted off aim/defs.h's IVModelInfo virtual order with its declared
	// `virtual ~IVModelInfo(){}` at slot 0 -- the SAME counting method that already gives
	// GetModel=1/GetModelName=3, both independently confirmed live by visuals.cpp's
	// ModelByIndex/ModelNameByIndex comment). g_modelInfo is globals.h's own extern (VCall raw,
	// same pattern visuals.cpp/movement.cpp already use on it -- no aim include needed).
	// UNVERIFIED css: botox's own model_planes comment is explicit that a model's BOUNDS BOX (as
	// opposed to its raw vertex extents) can sit off-centre for some v_ models -- the sweep may
	// ride high/low/short on a given CS:S weapon until checked in-game. See report.
	bool SheenModelPlanes(const char* modelName, SheenPlanes& out)
	{
		if (!modelName || !g_modelInfo) return false;
		if (const auto it = s_sheenPlanes.find(modelName); it != s_sheenPlanes.end()) { out = it->second; return true; }

		const int modelIndex = VCall<int>(g_modelInfo, 2, modelName);        // IVModelInfo::GetModelIndex
		if (modelIndex <= 0) return false;
		const void* model = VCall<const void*>(g_modelInfo, 1, modelIndex);  // IVModelInfo::GetModel
		if (!model) return false;
		float mins[3] = {}, maxs[3] = {};
		VCall<void>(g_modelInfo, 6, model, mins, maxs);                      // IVModelInfo::GetModelBounds

		int axes[3] = { 0, 1, 2 };
		std::sort(axes, axes + 3, [&](int a, int b) { return maxs[a] - mins[a] > maxs[b] - mins[b]; });
		const int along = axes[0], across = axes[1];
		const float length = maxs[along] - mins[along], height = maxs[across] - mins[across];
		if (length <= 0.01f || height <= 0.01f) return false;

		SheenPlanes planes{};
		// sweep runs toward the far end from the grip (the muzzle / blade tip)
		if (maxs[along] >= -mins[along]) { planes.u[along] = 1.f / length;  planes.u[3] = -mins[along] / length; }
		else                             { planes.u[along] = -1.f / length; planes.u[3] = maxs[along] / length; }
		planes.v[across] = 1.f / height;
		planes.v[3] = -mins[across] / height;

		s_sheenPlanes.emplace(modelName, planes);
		out = planes;
		return true;
	}

	void SheenFillLevel(const SheenVtf& vtf, int level, int frame, int face, const D3DLOCKED_RECT& rect)
	{
		const int w = (std::max)(1, vtf.width >> level), h = (std::max)(1, vtf.height >> level);
		const bool dxt = vtf.format == D3DFMT_DXT1;
		const int rows = dxt ? (std::max)(1, (h + 3) / 4) : h;
		const size_t rowBytes = dxt ? size_t((std::max)(1, (w + 3) / 4)) * 8 : size_t(w) * 8;
		const unsigned char* source = vtf.Image(level, frame, face);
		for (int row = 0; row < rows; row++)
			memcpy(static_cast<unsigned char*>(rect.pBits) + row * rect.Pitch, source + row * rowBytes, rowBytes);
	}

	IDirect3DTexture9* MakeSheenTexture(IDirect3DDevice9* dev, const SheenVtf& vtf, int frame)
	{
		IDirect3DTexture9 *staging = nullptr, *texture = nullptr;
		if (FAILED(dev->CreateTexture(vtf.width, vtf.height, vtf.mips, 0, vtf.format, D3DPOOL_SYSTEMMEM, &staging, nullptr)))
			return nullptr;

		bool ok = true;
		for (int level = 0; level < vtf.mips && ok; level++)
		{
			D3DLOCKED_RECT rect{};
			ok = SUCCEEDED(staging->LockRect(level, &rect, nullptr, 0));
			if (ok) { SheenFillLevel(vtf, level, frame, 0, rect); staging->UnlockRect(level); }
		}

		if (!ok || FAILED(dev->CreateTexture(vtf.width, vtf.height, vtf.mips, 0, vtf.format, D3DPOOL_DEFAULT, &texture, nullptr)) ||
		    FAILED(dev->UpdateTexture(staging, texture)))
		{
			if (texture) texture->Release();
			texture = nullptr;
		}
		staging->Release();
		return texture;
	}

	IDirect3DCubeTexture9* MakeSheenCube(IDirect3DDevice9* dev, const SheenVtf& vtf)
	{
		IDirect3DCubeTexture9 *staging = nullptr, *texture = nullptr;
		if (vtf.faces < 6 || FAILED(dev->CreateCubeTexture(vtf.width, vtf.mips, 0, vtf.format, D3DPOOL_SYSTEMMEM, &staging, nullptr)))
			return nullptr;

		bool ok = true;
		for (int face = 0; face < 6 && ok; face++)
			for (int level = 0; level < vtf.mips && ok; level++)
			{
				D3DLOCKED_RECT rect{};
				ok = SUCCEEDED(staging->LockRect(static_cast<D3DCUBEMAP_FACES>(face), level, &rect, nullptr, 0));
				if (ok) { SheenFillLevel(vtf, level, 0, face, rect); staging->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), level); }
			}

		if (!ok || FAILED(dev->CreateCubeTexture(vtf.width, vtf.mips, 0, vtf.format, D3DPOOL_DEFAULT, &texture, nullptr)) ||
		    FAILED(dev->UpdateTexture(staging, texture)))
		{
			if (texture) texture->Release();
			texture = nullptr;
		}
		staging->Release();
		return texture;
	}

	// render thread: build shaders (precompiled bytecode -- no runtime D3DXCompileShader, same "no
	// d3dx9 at runtime" convention lsd/two-tone above already keep) + textures, once assets are ready.
	bool SheenGpuReady(IDirect3DDevice9* dev)
	{
		if (s_sheenPs) return true;
		if (s_sheenGpuFailed) return false;
		if (s_sheenState.load(std::memory_order_acquire) != 2) return false;

		const auto vs0 = Code(k_fxSheenVs0Bytes), vs1 = Code(k_fxSheenVs1Bytes), ps = Code(k_fxSheenPsBytes);
		bool ok = SUCCEEDED(dev->CreateVertexShader(vs0.data(), &s_sheenVs[0])) &&
		          SUCCEEDED(dev->CreateVertexShader(vs1.data(), &s_sheenVs[1])) &&
		          SUCCEEDED(dev->CreatePixelShader(ps.data(), &s_sheenPs));
		if (ok) { CollectDefs(vs0.data(), s_sheenVsDefs); CollectDefs(ps.data(), s_sheenPsDefs); }

		for (int frame = 0; frame < s_sheenVtf[kSheenAssetMask].frames && ok; frame++)
		{
			s_sheenMasks.push_back(MakeSheenTexture(dev, s_sheenVtf[kSheenAssetMask], frame));
			ok = s_sheenMasks.back() != nullptr;
		}
		if (ok)
		{
			s_sheenCubes[0] = MakeSheenCube(dev, s_sheenVtf[kSheenAssetCube]);
			s_sheenCubes[1] = MakeSheenCube(dev, s_sheenVtf[kSheenAssetCubeHdr]);
			ok = s_sheenCubes[0] && s_sheenCubes[1];
		}

		if (!ok)
		{
			DebugLog("SHEEN: gpu FAILED -- weapon sheen stays off");
			for (auto*& v : s_sheenVs) if (v) { v->Release(); v = nullptr; }
			if (s_sheenPs) { s_sheenPs->Release(); s_sheenPs = nullptr; }
			for (auto* t : s_sheenMasks) if (t) t->Release();
			s_sheenMasks.clear();
			for (auto*& c : s_sheenCubes) if (c) { c->Release(); c = nullptr; }
			s_sheenGpuFailed = true;
			return false;
		}
		DebugLog("SHEEN: shaders + %d mask frames + 2 cubemaps ready", (int)s_sheenMasks.size());
		return true;
	}

	HRESULT DrawSheen(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		const int compressed = SheenGpuReady(dev) ? VertexCompression(dev) : -1;
		const SheenParams& p = s_sheenParams;
		if (compressed < 0 || p.frame <= 0 || p.frame >= (int)s_sheenMasks.size())
			return o_DIP(dev, type, base, minV, nV, start, prims);   // carrier itself is additive black: skipping is safe

		// c30.z = ENV_MAP_SCALE: 16 under integer hdr, 1 without (shaderapidx8.cpp) -- read only,
		// never set (whatever the engine's last draw left resident), same as botox's draw().
		float lightScale[4] = {};
		dev->GetPixelShaderConstantF(30, lightScale, 1);
		const bool hdr = lightScale[2] > 1.5f;

		static constexpr D3DRENDERSTATETYPE states[] = { D3DRS_ALPHABLENDENABLE,   D3DRS_SRCBLEND,         D3DRS_DESTBLEND,      D3DRS_BLENDOP,
		                                                 D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ZWRITEENABLE, D3DRS_COLORWRITEENABLE,
		                                                 D3DRS_SRGBWRITEENABLE, D3DRS_FOGENABLE };
		const DWORD stateValues[] = { TRUE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD, FALSE, FALSE, TRUE,
		                               D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE, TRUE, FALSE };
		static constexpr D3DSAMPLERSTATETYPE samplers[] = { D3DSAMP_ADDRESSU,  D3DSAMP_ADDRESSV,    D3DSAMP_ADDRESSW,      D3DSAMP_MAGFILTER,   D3DSAMP_MINFILTER,
		                                                    D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE, D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL, D3DSAMP_BORDERCOLOR };
		const DWORD srgbRead = hdr ? FALSE : TRUE;
		// mask u: black border, a narrowed band maps the gun ends past the mask edges
		const DWORD samplerValues[2][10] = {
			{ D3DTADDRESS_CLAMP,  D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, srgbRead, 0, 0, 0 },
			{ D3DTADDRESS_BORDER, D3DTADDRESS_WRAP,  D3DTADDRESS_WRAP,  D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, srgbRead, 0, 0, 0 } };

		DWORD oldStates[std::size(states)] = {}, oldSamplers[2][10] = {};
		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		IDirect3DBaseTexture9* oldTextures[2] = {};
		float oldPsConsts[4][4] = {};
		static std::vector<Saved> savedVs, savedPs;
		savedVs.clear(); savedPs.clear();

		for (size_t i = 0; i < std::size(states); i++) dev->GetRenderState(states[i], &oldStates[i]);
		for (DWORD stage = 0; stage < 2; stage++)
		{
			for (size_t i = 0; i < std::size(samplers); i++) dev->GetSamplerState(stage, samplers[i], &oldSamplers[stage][i]);
			dev->GetTexture(stage, &oldTextures[stage]);
		}
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);
		dev->GetPixelShaderConstantF(6, oldPsConsts[0], 4);
		SaveConsts(dev, true, s_sheenVsDefs, savedVs);
		SaveConsts(dev, false, s_sheenPsDefs, savedPs);

		for (size_t i = 0; i < std::size(states); i++) dev->SetRenderState(states[i], stateValues[i]);
		for (DWORD stage = 0; stage < 2; stage++)
			for (size_t i = 0; i < std::size(samplers); i++) dev->SetSamplerState(stage, samplers[i], samplerValues[stage][i]);

		dev->SetTexture(0, s_sheenCubes[hdr ? 1 : 0]);
		dev->SetTexture(1, s_sheenMasks[p.frame]);
		dev->SetVertexShader(s_sheenVs[compressed]);
		dev->SetPixelShader(s_sheenPs);
		dev->SetPixelShaderConstantF(6, p.u, 1);
		dev->SetPixelShaderConstantF(7, p.v, 1);
		dev->SetPixelShaderConstantF(8, p.tint, 1);
		dev->SetPixelShaderConstantF(9, p.band, 1);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		for (size_t i = 0; i < std::size(states); i++) dev->SetRenderState(states[i], oldStates[i]);
		for (DWORD stage = 0; stage < 2; stage++)
			for (size_t i = 0; i < std::size(samplers); i++) dev->SetSamplerState(stage, samplers[i], oldSamplers[stage][i]);
		dev->SetPixelShaderConstantF(6, oldPsConsts[0], 4);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		for (DWORD stage = 0; stage < 2; stage++) dev->SetTexture(stage, oldTextures[stage]);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		if (oldTextures[0]) oldTextures[0]->Release();
		if (oldTextures[1]) oldTextures[1]->Release();
		return hr;
	}

	void ForceSheenMaterial(void* pMat)
	{
		if (o_ForcedMaterialOverride) o_ForcedMaterialOverride(g_modelRender, pMat, 0 /*OVERRIDE_NORMAL*/);
	}

	// own tiny fake-CFunctor vtable (not the shared Fn_AddRef/Release/Dtor below -- those are
	// defined further down this file, after this section, for lsd/two-tone's own functors).
	struct SheenFunctor { void** vfptr; SheenParams params; };
	int   __fastcall Fn_SheenAddRef(void*)           { return 1; }
	int   __fastcall Fn_SheenRelease(void*)          { return 1; }
	void* __fastcall Fn_SheenDtor(void* p, unsigned) { return p; }
	void __fastcall Fn_SheenArm(void* p)
	{
		auto* f = static_cast<SheenFunctor*>(p);
		s_sheenParams = f->params;
		s_sheenArmed.store(true, std::memory_order_relaxed);
	}
	void __fastcall Fn_SheenDisarm(void*) { s_sheenArmed.store(false, std::memory_order_relaxed); }

	void* s_sheenArmVt[4]    = { (void*)&Fn_SheenAddRef, (void*)&Fn_SheenRelease, (void*)&Fn_SheenDtor, (void*)&Fn_SheenArm };
	void* s_sheenDisarmVt[4] = { (void*)&Fn_SheenAddRef, (void*)&Fn_SheenRelease, (void*)&Fn_SheenDtor, (void*)&Fn_SheenDisarm };
	// one weapon sheen draw at a time in practice (the local viewmodel only) -- a tiny ring is plenty
	SheenFunctor s_sheenArms[64];
	unsigned     s_sheenArmNext = 0;
	SheenFunctor s_sheenDisarmFn = { s_sheenDisarmVt, {} };
}

void TwoToneChams_Begin(const float light[4], const float dark[4], float threshold, float edge, bool ignorez)
{
	EnsureHook();
	if (!o_DIP) return;
	TtFunctor& f = s_ttArms[s_ttArmNext++ & 1023];
	f.vfptr = s_ttArmVt;
	memcpy(f.light, light, sizeof(f.light));
	memcpy(f.dark, dark, sizeof(f.dark));
	f.params[0] = threshold;
	f.params[1] = edge;
	f.ignorez = ignorez;
	if (!ScreenFx_QueueFunctor(&f))
		Fn_TtArm(&f);   // mat_queue_mode 0: this thread is the render thread
}

void TwoToneChams_End()
{
	if (!o_DIP) return;
	if (!ScreenFx_QueueFunctor(&s_ttDisarm))
		Fn_TtDisarm(&s_ttDisarm);
}

void LsdChams_Begin(float realTime, float alpha)
{
	EnsureHook();
	if (!o_DIP) return;
	LsdFunctor& f = s_arms[s_armNext++ & 1023];
	f = { s_armVt, realTime, alpha };
	if (!ScreenFx_QueueFunctor(&f))
		Fn_Arm(&f);   // mat_queue_mode 0: this thread is the render thread
}

void LsdChams_End()
{
	if (!o_DIP) return;
	if (!ScreenFx_QueueFunctor(&s_disarm))
		Fn_Disarm(&s_disarm);
}

// Call around a SECOND DrawModel of the local player's viewmodel weapon (vmKind == 2), same
// "extra layer draw" shape Chams_DrawModel already uses for player chams -- NEEDS FROM OTHERS,
// see report for the exact call site:
//   if (WeaponSheen_Begin(modelName)) { fnOriginal(...); WeaponSheen_End(); }
// Begin forces the carrier material itself (ForceSheenMaterial) and arms the DIP swap; the
// caller only needs the one extra draw in between. Returns false (nothing forced, nothing
// armed) whenever the sheen shouldn't draw this frame -- off, ejecting, assets not loaded yet,
// off the sweep's duty cycle (botox's own throttle: one pass every m_weapon_sheen_delay
// seconds), or no carrier material yet (Chams_GetSheenCarrierMaterial, NEEDS FROM OTHERS).
bool WeaponSheen_Begin(const char* modelName)
{
	if (g_eject || !g_wpnSheenEnable) return false;
	EnsureSheenAssetsLoading();
	if (s_sheenState.load(std::memory_order_acquire) != 2) return false;
	if (!g_globalVars) return false;

	// CProxyAnimatedWeaponSheen::OnBind: same frame for every call in one frame, wrap -> wait
	const float curtime = g_globalVars->curtime;
	const float delay = std::clamp(g_wpnSheenDelay, 0.f, 10.f);
	if (s_sheenNextStart > curtime + delay + 1.f) s_sheenNextStart = curtime;
	if (s_sheenNextStart > curtime) return false;

	const float delta = curtime - s_sheenNextStart;
	const float previous = (std::max)(delta - g_globalVars->frametime, 0.f);
	const int frames = s_sheenVtf[kSheenAssetMask].frames;
	if (frames <= 0) return false;
	const int frame = int(kSheenFrameRate * delta) % frames;
	if (int(kSheenFrameRate * previous) % frames > frame)
	{
		s_sheenNextStart = curtime + delay;
		return false;
	}
	if (frame <= 0) return false;

	SheenPlanes planes{};
	if (!SheenModelPlanes(modelName, planes)) return false;

	void* carrier = Chams_GetSheenCarrierMaterial();
	if (!carrier) return false;

	EnsureHook();
	if (!o_DIP) return false;

	SheenParams params{};
	memcpy(params.u, planes.u, sizeof(params.u));
	memcpy(params.v, planes.v, sizeof(params.v));
	params.frame = frame;
	params.band[0] = kSheenMaskCore[(std::min)(frame, 59)];
	params.band[1] = 1.f / std::clamp(g_wpnSheenWidth, 0.1f, 1.f);
	params.band[2] = g_wpnSheenNormalBlend ? 1.f : 0.f;
	params.band[3] = std::clamp(g_wpnSheenEnv, 0.f, 10.f);
	for (int i = 0; i < 4; i++) params.tint[i] = g_wpnSheenColor[i];

	SheenFunctor& f = s_sheenArms[s_sheenArmNext++ & 63];
	f.vfptr = s_sheenArmVt;
	f.params = params;
	if (!ScreenFx_QueueFunctor(&f))
		Fn_SheenArm(&f);   // mat_queue_mode 0: this thread is the render thread

	ForceSheenMaterial(carrier);
	return true;
}

void WeaponSheen_End()
{
	if (!o_DIP) return;
	ForceSheenMaterial(nullptr);
	if (!ScreenFx_QueueFunctor(&s_sheenDisarmFn))
		Fn_SheenDisarm(&s_sheenDisarmFn);
}

// render thread, before Reset / eject: masks + cubes are DEFAULT-pool. CPU vtf assets stay
// (s_sheenState 2), so SheenGpuReady rebuilds on the next sheen draw; defs cleared or it appends twice.
void WeaponSheen_Destroy()
{
	for (auto*& v : s_sheenVs) if (v) { v->Release(); v = nullptr; }
	if (s_sheenPs) { s_sheenPs->Release(); s_sheenPs = nullptr; }
	for (auto* t : s_sheenMasks) if (t) t->Release();
	s_sheenMasks.clear();
	for (auto*& c : s_sheenCubes) if (c) { c->Release(); c = nullptr; }
	s_sheenVsDefs.clear(); s_sheenPsDefs.clear();
}

void ShaderChams_Begin(int mode, const float color[4], float realTime, bool ignorez)
{
	EnsureHook();
	if (!o_DIP) return;
	ChFunctor& f = s_chArms[s_chArmNext++ & 1023];
	f.vfptr = s_chArmVt;
	memcpy(f.color, color, sizeof(f.color));
	f.time = std::fmod(realTime, 1024.f);   // Atlas TIME_WRAP: float precision stays fine
	f.mode = mode;
	f.ignorez = ignorez;
	if (!ScreenFx_QueueFunctor(&f))
		Fn_ChArm(&f);   // mat_queue_mode 0: this thread is the render thread
}

void ShaderChams_End()
{
	if (!o_DIP) return;
	if (!ScreenFx_QueueFunctor(&s_chDisarm))
		Fn_ChDisarm(&s_chDisarm);
}
