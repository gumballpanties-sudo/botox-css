// Player chams + player glow (10-31 rework).
//
// Structure: botox CS:GO hacks/chams/chams.h (material table, ignorez "xqz" variant per slot,
// layer lists drawn in order through the original DrawModelExecute) + ref
// Features/Visuals/{Materials,Chams} (materials from KeyValues through
// IMaterialSystem::CreateMaterial, colour through IVRenderView modulation/blend + $envmaptint/
// $phongtint, override + colour state saved before and restored after every model).
//
// Lives in the aim tree because it needs the aim SDK (IMaterial, KeyValues, CCSPlayer);
// dih's DrawModelExecute hook (visuals.cpp) reaches it through chams_api.h.
//
// Material refcount (the "botox_glow/glow has bad reference count 0" console spam): the old
// code FindMaterial'd its materials without IncrementReferenceCount and then DecRef'd them on
// every level change. Here every material we hold gets exactly one IncrementReferenceCount
// when created/found and exactly one DecrementReferenceCount when released.
#include "sdk.h"
#include "css.h"   // CCSPlayer / CWeaponCSBase
#include "../visuals/chams_api.h"
#include "../visuals/chams_layers.h"
#include "../visuals/lsd_chams.h"   // LsdChams_Begin/End: the "lsd" layer
#include "../visuals/player_stencil.h"   // botox player stencil / internal glow / full-res glow (d3d9 pass)
#include "../visuals/esp_preview.h"  // EspPreview::Drawing/GhostStream: the preview chams branch below
#include "../menu/cfg/u_esp.h"            // g_esx* stencil + inner glow vars (primitives only)
#include "../menu/cfg/u_btchams.h"   // g_chamsBacktrack*
#include "backtrack.h"
#include "../esp/esp_fx.h"   // EspFx_NowMs: one clock for the sound stamp (game thread) and the draw
#include <cmath>
#include <cstring>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
bool Aim_Active();                     // aim.cpp: false = I::/S:: never initialised, chams stays off

extern void* g_matSystem;     // IMaterialSystem (dih, thread.cpp)
extern void* g_modelRender;   // IVModelRender  (dih, thread.cpp)

// ---- settings (dllmain.cpp, saved by config.cpp) ----
extern bool  g_chamsEnable;              // botox "enable enemy chams"
extern bool  g_chamsTeam;                // teammates (TF2 extra, same visible/occluded lists)
extern bool  g_chamsLocalEnable;         // botox "enable local chams"
extern bool  g_chamsRagdolls;
extern float g_chamsSoundDuration;
extern bool  g_chamsSoundFade;
extern float g_chamsSoundMinVolume;
extern float g_chamsSoundMaxDistance;
extern float g_chamsTwoToneThreshold;    // botox m_chams_two_tone_threshold: camera light, cos of facing angle
extern float g_chamsTwoToneEdge;         // botox m_chams_two_tone_edge: cut softness
extern char  g_chamsVisibleCfg[ChamsLayers::kCfgLen];
extern char  g_chamsOccludedCfg[ChamsLayers::kCfgLen];
extern char  g_chamsSoundCfg[ChamsLayers::kCfgLen];
extern char  g_chamsTeamVisibleCfg[ChamsLayers::kCfgLen];
extern char  g_chamsTeamOccludedCfg[ChamsLayers::kCfgLen];
extern char  g_chamsWeaponCfg[ChamsLayers::kCfgLen];
// TF2's uber/cloak visible-list swap (Medic uber, Spy cloak) removed: no analog in CS:S --
// CBaseEntity::GetEffectiveInvisibilityLevel()/IsInvulnerable() don't exist for a CCSPlayer,
// and CTFPlayer_GetEffectiveInvisibilityLevel is confirmed MISSING in CSS (docs/port/sig_check.md).

extern bool  g_espEnable;                // botox m_players: stencil / inner / full-res glow need it
bool Esp_On();                           // g_espEnable + esp keybind (esp_vars.h); preview keeps plain g_espEnable
extern bool  g_glowEnable;               // botox "player glow"
extern float g_glowVisColor[4];
extern float g_glowInvisColor[4];
extern bool  g_glowLegacy;
extern int   g_glowLegacyStyle;          // 0 default, 1 rim, 2 outline, 3 pulse
extern bool  g_glowNormalBlend;
extern float g_glowThickness;
extern bool  g_glowGradient;
extern float g_glowVisOuterColor[4];
extern float g_glowInvisOuterColor[4];
extern float g_glowGradientInner;
extern bool  g_glowWave;
extern float g_glowWaveColor[4];
extern float g_glowWaveSpeed;
extern float g_glowWaveSize;
extern float g_glowWaveStrength;
extern bool  g_glowWavePlayer;           // wave rides each player's body, not the screen
extern float g_glowWaveAngle;            // deg, turns the wave direction
extern bool  g_glowPulse;
extern float g_glowPulseSpeed;
extern float g_glowPulseMin;

namespace
{
	// ---- vtable slots, each verified against ref x64 TF2 ----
	// IMaterialSystem: SDK/Definitions/Interfaces/IMaterialSystem.h:425-426, CreateMaterial is the
	// slot right before FindMaterial; FindMaterial 71 is flipmath.h matFindMaterial (live in this tree).
	constexpr int kMatSysCreateMaterial = 70;
	constexpr int kMatSysFindMaterial   = 71;
	// IVModelRender: SDK/Definitions/Interfaces/IVModelRender.h: DrawModel 0, ForcedMaterialOverride 1,
	// DrawModelExecute 19 (dih's hook slot), GetMaterialOverride 25.
	constexpr int kMdlForcedMaterialOverride = 1;
	constexpr int kMdlGetMaterialOverride    = 25;
	// ModelRenderInfo_t (public/engine/ivmodelrender.h, x64): flags +0x40 (entity_index +0x44 is what
	// visuals.cpp already reads). Depth-only passes must never get chams (botox studio_depth_pass_flags).
	constexpr int kInfoFlags = 0x40;
	constexpr int kDepthPassFlags = 0x08000000 /*STUDIO_SSAODEPTHTEXTURE*/ | 0x40000000 /*STUDIO_SHADOWDEPTHTEXTURE*/;

	// IMaterialVar, ref SDK/Definitions/Main/IMaterialVar.h declaration order through the
	// SetVecValue overload group (MSVC lays an overload group out as a block, so the group is
	// declared exactly as the engine header has it). ref Materials.cpp:271 calls the 3-float
	// SetVecValue on TF2 x64 through this same declaration. aim/defs.h only forward-declares it.
	class ChamsMatVar
	{
	public:
		virtual void* GetTextureValue() = 0;
		virtual const char* GetName() const = 0;
		virtual unsigned short GetNameAsSymbol() const = 0;
		virtual void SetFloatValue(float val) = 0;
		virtual void SetIntValue(int val) = 0;
		virtual void SetStringValue(char const* val) = 0;
		virtual char const* GetStringValue() const = 0;
		virtual void SetFourCCValue(unsigned long type, void* pData) = 0;
		virtual void GetFourCCValue(unsigned long* type, void** ppData) = 0;
		virtual void SetVecValue(float const* val, int numcomps) = 0;
		virtual void SetVecValue(float x, float y) = 0;
		virtual void SetVecValue(float x, float y, float z) = 0;
		virtual void SetVecValue(float x, float y, float z, float w) = 0;
	};

	template <typename R, typename... A>
	R VCallAt(void* p, int idx, A... a)
	{
		return reinterpret_cast<R(__fastcall*)(void*, A...)>((*reinterpret_cast<void***>(p))[idx])(p, a...);
	}

	void ForceMaterial(IMaterial* pMat, int iType = 0 /*OVERRIDE_NORMAL*/)
	{
		if (o_ForcedMaterialOverride) o_ForcedMaterialOverride(g_modelRender, pMat, iType);   // past our own hook
		else VCallAt<void>(g_modelRender, kMdlForcedMaterialOverride, pMat, iType);
	}

	// ---- material table (botox impl_t::m_materials[index][ignorez]) ----
	// No IMaterialVar* kept here: a material reload rebuilds the var array (the IncRef keeps the
	// IMaterial, not its vars), and a held var then crashes in SetVecValue (botox-tf2 crash.log 11-09
	// DrawLayers / GlowPasses_SEH, garbage vtable). FindVar at use, like visuals.cpp does.
	struct Mat
	{
		IMaterial*   pMat = nullptr;
	};
	Mat  s_mats[ChamsLayers::Count][2];
	int  s_buildIndex = 1;                 // next material to build (Original = 0 needs none)
	int  s_lastBuildFrame = -1;
	bool s_built = false;
	bool s_loggedBuilt = false;

	ChamsMatVar* FindVar(IMaterial* p, const char* name)
	{
		bool bFound = false;
		auto pVar = p->FindVar(name, &bFound, false);
		return bFound ? reinterpret_cast<ChamsMatVar*>(pVar) : nullptr;
	}

	// ref CMaterials::Create + botox from_keyvalues: KeyValues -> CreateMaterial -> one IncRef.
	// KeyValues sigs (defs.cpp) checked against the installed engine.dll 10-31: both unique, and
	// LoadFromBuffer(char*) calls the CUtlBuffer overload that references "#include"/"#base".
	IMaterial* CreateFromVMT(const char* name, const char* vmt)
	{
		auto kv = new KeyValues(name);
		if (!kv->LoadFromBuffer(name, vmt))
		{
			DebugLog("chams: KeyValues::LoadFromBuffer failed for %s", name);
			delete kv;
			return nullptr;
		}
		// CreateMaterial takes ownership of kv (the material keeps it), botox relies on it.
		auto pMat = VCallAt<IMaterial*>(g_matSystem, kMatSysCreateMaterial, name, kv);
		if (!pMat || pMat->IsErrorMaterial())
		{
			DebugLog("chams: CreateMaterial failed for %s (%p)", name, (void*)pMat);
			return nullptr;
		}
		pMat->IncrementReferenceCount();
		return pMat;
	}

	// botox make_variants: the depth/fog flags are appended per variant, never inside `body`
	// (FindKey returns the first match -- a duplicate would win).
	void MakeVariants(int index, const char* name, const char* shader, const char* body)
	{
		static const char* const kFlags[2] = {
			"\t\"$ignorez\" \"0\"\n\t\"$znearer\" \"0\"\n\t\"$nofog\" \"0\"\n",
			"\t\"$ignorez\" \"1\"\n\t\"$znearer\" \"1\"\n\t\"$nofog\" \"1\"\n" };
		for (int v = 0; v < 2; ++v)
		{
			char sName[64], sVMT[1024];
			sprintf_s(sName, "botox_chams_%s%s", name, v ? "_xqz" : "");
			sprintf_s(sVMT, "\"%s\"\n{\n%s\t\"$model\" \"1\"\n%s}\n", shader, body, kFlags[v]);
			s_mats[index][v].pMat = CreateFromVMT(sName, sVMT);
		}
	}

	// Shader chams carriers (11-11, Atlas-TF2 style): the look is fx_cham_ps[index], swapped in by the DIP detour
	// (lsd_chams.cpp DrawCham), so every slot is the same plain vertexlit white -- a shader that exists at every
	// graphics setting and brings normals + the ambient cube. The old envmap / phong / bump materials changed
	// look (or went flat white) with mat_phong / mat_specular / shader detail. Damascus keeps its texture (s0).
	void BuildOne(int index)
	{
		using namespace ChamsLayers;
		if (index == Lsd)   // botox case 11 carrier: flat white, the DIP detour swaps in the lsd shaders
			MakeVariants(Lsd, "lsd", "UnlitGeneric", "\t\"$basetexture\" \"vgui/white\"\n");
		else if (IsShader(index))
			MakeVariants(index, kNames[index], "VertexLitGeneric",
				index == Damascus ? "\t\"$basetexture\" \"de_piranesi/marblefloor13\"\n"
				: index == Wireframe || index == Network ? "\t\"$basetexture\" \"vgui/white\"\n\t\"$wireframe\" \"1\"\n"
				: "\t\"$basetexture\" \"vgui/white\"\n");
	}

	// botox create(): one material per frame on the render path (never off-thread), so the first
	// enable costs no visible hitch.
	void BuildStep()
	{
		if (s_built || !g_matSystem) return;
		const int frame = I::GlobalVars ? I::GlobalVars->framecount : s_lastBuildFrame + 1;
		if (frame == s_lastBuildFrame) return;
		s_lastBuildFrame = frame;

		BuildOne(s_buildIndex);
		if (++s_buildIndex >= ChamsLayers::Count)
		{
			s_built = true;
			if (!s_loggedBuilt)
			{
				int n = 0;
				for (auto& v : s_mats) for (auto& m : v) n += m.pMat != nullptr;
				DebugLog("chams: materials built (%d/%d variants)", n, 2 * (ChamsLayers::Count - 2));   // original + two tone have none
				s_loggedBuilt = true;
			}
		}
	}

	IMaterial* GetMat(int index, bool ignorez)
	{
		if (index <= ChamsLayers::Original || index >= ChamsLayers::Count) return nullptr;
		BuildStep();
		return s_mats[index][ignorez ? 1 : 0].pMat;
	}

	// ---- colour (ref CMaterials::SetColor + botox apply_color) ----
	void SetModulation(const float c[4], float alphaScale)
	{
		I::RenderView->SetColorModulation(c);
		I::RenderView->SetBlend(c[3] * alphaScale);
	}

	// one model draw through the original DrawModelExecute, optionally on other bones
	struct DrawCtx
	{
		ChamsDrawFn fn; void* rcx; const void* pState; const void* pInfo; const void* pBones;
		IMaterial* pOrigMat; int iOrigType;   // the override the engine had up (uber material), "original" keeps it
		void Draw(const void* pOtherBones = nullptr) const { fn(rcx, pState, pInfo, pOtherBones ? pOtherBones : pBones); }
	};

	// botox draw_layers: each slot is one full model draw under its material + colour.
	// Returns true when at least one draw happened.
	bool DrawLayers(const ChamsLayers::List& l, bool ignorez, float alphaScale, const DrawCtx& ctx, const void* pBones = nullptr)
	{
		bool bDrew = false;
		for (int i = 0; i < l.used; ++i)
		{
			const int index = l.slots[i];
			const float* c = l.colors[i];
			if (index == ChamsLayers::Original)
			{
				SetModulation(c, alphaScale);
				ForceMaterial(ctx.pOrigMat, ctx.iOrigType);
				if (c[3] * alphaScale > 0.f) { ctx.Draw(pBones); bDrew = true; }
				continue;
			}
			if (index == ChamsLayers::TwoTone)
			{
				const float* d = l.darks[i];
				const float light[4] = { c[0], c[1], c[2], c[3] * alphaScale };
				const float dark[4]  = { d[0], d[1], d[2], d[3] * alphaScale };
				const float alpha = light[3] > dark[3] ? light[3] : dark[3];
				if (alpha <= 0.f) continue;
				// own materials keep the model's normal maps + lighting constants bound; blend < 1 = engine draws translucent,
				// any colour under 255 counts (DIP blends per pixel with the picked alphas, blend value itself unused)
				const bool bTranslucent = light[3] < 1.f || dark[3] < 1.f;
				static const float kWhite[4] = { 1.f, 1.f, 1.f, 1.f };
				ForceMaterial(nullptr);
				SetModulation(kWhite, bTranslucent ? (alpha < 0.99f ? alpha : 0.99f) : 1.f);
				TwoToneChams_Begin(light, dark, g_chamsTwoToneThreshold, g_chamsTwoToneEdge, ignorez);
				ctx.Draw(pBones);
				TwoToneChams_End();
				bDrew = true;
				continue;
			}
			IMaterial* pMat = GetMat(index, ignorez);
			if (!pMat) continue;
			SetModulation(c, alphaScale);
			ForceMaterial(pMat);
			const float flTime = I::GlobalVars ? I::GlobalVars->realtime : 0.f;
			if (index == ChamsLayers::Lsd)
			{
				LsdChams_Begin(flTime, c[3] * alphaScale);
				ctx.Draw(pBones);
				LsdChams_End();
			}
			else
			{
				const float color[4] = { c[0], c[1], c[2], c[3] * alphaScale };
				ShaderChams_Begin(index, color, flTime, ignorez);
				ctx.Draw(pBones);
				ShaderChams_End();
			}
			bDrew = true;
		}
		ForceMaterial(nullptr);
		return bDrew;
	}

	// ---- config lists: cfg string (config.cpp STR) -> parsed list, re-parsed only on change ----
	struct CfgList
	{
		const char* pCfg;
		char seen[ChamsLayers::kCfgLen] = { '\x01' };   // never equal to any real cfg on first sync
		ChamsLayers::List list = {};
		const ChamsLayers::List& Get()
		{
			if (strcmp(seen, pCfg) != 0)
			{
				ChamsLayers::Parse(pCfg, list);
				strcpy_s(seen, pCfg);
			}
			return list;
		}
	};
	CfgList s_visible{ g_chamsVisibleCfg }, s_occluded{ g_chamsOccludedCfg }, s_backtrack{ g_chamsBacktrackCfg },
	        s_sound{ g_chamsSoundCfg }, s_teamVisible{ g_chamsTeamVisibleCfg }, s_teamOccluded{ g_chamsTeamOccludedCfg },
	        s_weapon{ g_chamsWeaponCfg };

	// ---- backtrack ghosts: botox draw_model_execute.cpp:240-340 draw_backtrack_records ----
	float s_btFade[65] = {};   // botox ImAnimationHelper per model + entity: 0..1, 2 * dt * (+-2)

	bool HasSlot(const ChamsLayers::List& l, int index)
	{
		for (int i = 0; i < l.used; ++i)
			if (l.slots[i] == index) return true;
		return false;
	}

	void DrawBacktrack(CCSPlayer* pPlayer, int entIdx, const ChamsLayers::List& l, const DrawCtx& ctx)
	{
		if (entIdx <= 0 || entIdx > 64)
			return;

		float& flFade = s_btFade[entIdx];
		auto Animate = [&](bool bIn) { flFade = std::clamp(flFade + 2.f * I::GlobalVars->absoluteframetime * (bIn ? 2.f : -2.f), 0.f, 1.f); return flFade; };
		auto DrawRecord = [&](const TickRecord* r, float flAlpha) { if (r && flAlpha > 0.f) DrawLayers(l, g_chamsBacktrackXqz, flAlpha, ctx, r->m_aBones); };

		// records are network pose, the model is interpolated behind it: newer ones would draw in front
		const float flModel = F::Backtrack.ModelTime();
		const Vec3 vLive = pPlayer->GetAbsOrigin();

		switch (g_chamsBacktrackType)
		{
		case 0:
			if (auto pOldest = F::Backtrack.OldestRecord(pPlayer); pOldest && pOldest->m_flSimTime <= flModel)
				DrawRecord(pOldest, Animate(pOldest->m_vOrigin.DistToSqr(vLive) > 9.f));
			break;
		case 1:
		{
			auto pOldest = F::Backtrack.OldestRecord(pPlayer);
			auto pList = F::Backtrack.GetRecords(entIdx);
			if (!pOldest || !pList)
				break;

			const float flAlpha = Animate(pOldest->m_vOrigin.DistToSqr(vLive) > 9.f);

			static const TickRecord* aRanked[CBacktrack::kMaxRecords];
			int nRanked = 0;
			for (int i = 0; i < F::Backtrack.Count(); i++)
				if (pList[i].m_flSimTime <= flModel && F::Backtrack.Usable(pPlayer, pList[i].m_flSimTime))
					aRanked[nRanked++] = &pList[i];
			std::sort(aRanked, aRanked + nRanked, [](const TickRecord* a, const TickRecord* b) { return a->m_flSimTime > b->m_flSimTime; });

			// ghost cap thins, never shortens: newest + oldest always, rest evenly spaced, oldest drawn first
			const int iMax = std::clamp(g_chamsBacktrackMaxGhosts, 2, CBacktrack::kMaxRecords);
			const int iDrawn = nRanked < iMax ? nRanked : iMax;
			for (int pick = iDrawn - 1; pick >= 0; --pick)
			{
				const int i = iDrawn > 1 ? int(float(pick) * float(nRanked - 1) / float(iDrawn - 1) + 0.5f) : 0;
				const float flTick = g_chamsBacktrackGradient && nRanked > 1 ? 1.f - float(i) / float(nRanked - 1) : 1.f;
				DrawRecord(aRanked[i], flAlpha * flTick);
			}
			break;
		}
		case 2:
			// botox draws g_ctx.m_record on every enemy model (other models on the target's bones);
			// here only on the player that owns it. Fades in while attack is held.
			if (F::Backtrack.m_pTarget && F::Backtrack.Owns(entIdx, F::Backtrack.m_pTarget) && G::LastUserCmd)
				DrawRecord(F::Backtrack.m_pTarget, Animate(G::LastUserCmd->buttons & IN_ATTACK));
			break;
		}
	}

	// ---- sound chams stamps (botox sound_esp m_last_sound / m_origin) ----
	float s_soundMs[65] = {};
	float s_soundOrigin[65][3] = {};

	float SoundAlphaFor(int entIdx, const Vec3& vLocal)
	{
		if (entIdx <= 0 || entIdx > 64 || s_soundMs[entIdx] <= 0.f) return 0.f;
		if (g_chamsSoundMaxDistance > 0.f)
		{
			const float* o = s_soundOrigin[entIdx];
			const Vec3 vSound(o[0], o[1], o[2]);
			if ((vSound - vLocal).Length() > g_chamsSoundMaxDistance) return 0.f;
		}
		return ChamsLayers::SoundAlpha((EspFx_NowMs() - s_soundMs[entIdx]) / 1000.f, g_chamsSoundDuration, g_chamsSoundFade);
	}

	// ---- player glow colour helpers (botox "player glow" popup) ----
	//   accent wave = colour pulled toward the wave colour by a travelling sine (size = spatial scale)
	//   edge pulse  = alpha breathing between pulse min % and 100 %
	// The glow itself is the ref stencil + render-target pass at the end of this file.
	void GlowColor(const float* base, float out[4], float flPhase)
	{
		for (int k = 0; k < 4; ++k) out[k] = base[k];
		if (!g_glowWave) return;
		const float t = EspFx_NowMs() / 1000.f;
		const float w = (0.5f + 0.5f * sinf(6.2831853f * (t * g_glowWaveSpeed + flPhase))) * std::clamp(g_glowWaveStrength, 0.f, 1.f);
		for (int k = 0; k < 3; ++k) out[k] += (g_glowWaveColor[k] - out[k]) * w;
	}

	float GlowPulse()
	{
		const bool bForced = g_glowLegacy && g_glowLegacyStyle == 3;
		if (!g_glowPulse && !bForced) return 1.f;
		const float flSpeed = g_glowPulse ? g_glowPulseSpeed : 1.f;
		const float flMin = g_glowPulse ? std::clamp(g_glowPulseMin / 100.f, 0.f, 1.f) : 0.f;
		const float s = 0.5f + 0.5f * sinf(6.2831853f * EspFx_NowMs() / 1000.f * flSpeed);
		return flMin + (1.f - flMin) * s;
	}

	// ---- entity classification ----
	// CS:S player models carry no attached cosmetic-wearable layer (no hats; botox CS:GO has
	// none either) -- only the held weapon's world/view model draws as its own entity.
	// Owner must also be the move parent (attached): a thrown grenade is owned by its thrower
	// too, but it flies free, so a model-path-only test would wrongly give it the owner's chams.
	CCSPlayer* OwnerPlayer(CBaseEntity* pEnt, const char* modelName)
	{
		const bool bWeaponModel = modelName && (strstr(modelName, "/weapons/") || strstr(modelName, "/c_models/"));
		if (!bWeaponModel)
			return nullptr;
		// c_baseentity.cpp: RecvPropInt(RECVINFO_NAME(m_hNetworkMoveParent, moveparent)) -- same handle encoding as m_hOwnerEntity
		static const int nMoveParentOff = U::NetVars.GetNetVar("CBaseEntity", "moveparent");
		if (nMoveParentOff <= 0)
			return nullptr;
		const unsigned uMoveParent = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(pEnt) + nMoveParentOff);
		const unsigned uOwner = *reinterpret_cast<unsigned*>(&pEnt->m_hOwnerEntity());
		if (uMoveParent != uOwner)
			return nullptr;
		auto pOwner = I::ClientEntityList->GetClientEntityFromHandle(pEnt->m_hOwnerEntity());
		return pOwner && pOwner->As<CBaseEntity>()->IsPlayer() ? pOwner->As<CCSPlayer>() : nullptr;
	}

	// C_CSRagdoll::m_hPlayer (css.h) -- the player who died into this ragdoll.
	CCSPlayer* RagdollOwner(CBaseEntity* pEnt)
	{
		auto pRagdoll = pEnt->As<CCSRagdoll>();
		if (!pRagdoll) return nullptr;
		auto pOwner = I::ClientEntityList->GetClientEntityFromHandle(pRagdoll->m_hPlayer());
		return pOwner && pOwner->As<CBaseEntity>()->IsPlayer() ? pOwner->As<CCSPlayer>() : nullptr;
	}

	bool s_loggedDraw = false, s_loggedAttach = false;
}

// botox weapon_sheen.cpp carrier_material(): additive black pass the sheen DIP layer
// (lsd_chams.cpp DrawSheen) swaps its own shaders into. Lazy, released with the chams set.
static IMaterial* s_sheenCarrier = nullptr;

void* Chams_GetSheenCarrierMaterial()
{
	if (!s_sheenCarrier && g_matSystem)
		s_sheenCarrier = CreateFromVMT("botox_sheen_carrier",
			"\"UnlitGeneric\"\n{\n"
			"\t\"$basetexture\" \"vgui/white\"\n\t\"$color\" \"[0 0 0]\"\n\t\"$color2\" \"[0 0 0]\"\n"
			"\t\"$additive\" \"1\"\n\t\"$model\" \"1\"\n\t\"$nofog\" \"1\"\n}\n");
	return s_sheenCarrier;
}

void Chams_ReleaseMaterials()
{
	if (s_sheenCarrier)
	{
		s_sheenCarrier->DecrementReferenceCount();
		s_sheenCarrier->DeleteIfUnreferenced();
		s_sheenCarrier = nullptr;
	}
	// ref CMaterials::Remove: one DecRef per IncRef, then let the engine drop it so the next
	// map's CreateMaterial with the same name is not a duplicate.
	for (int i = 0; i < ChamsLayers::Count; ++i)
	{
		for (int v = 0; v < 2; ++v)
		{
			Mat& m = s_mats[i][v];
			if (m.pMat)
			{
				m.pMat->DecrementReferenceCount();
				m.pMat->DeleteIfUnreferenced();
			}
			m = Mat{};
		}
	}
	s_built = false;
	s_buildIndex = 1;
	s_lastBuildFrame = -1;
	for (auto& t : s_soundMs) t = 0.f;
}

// esp.cpp's sound gate + sound arrows (botox g_sound_esp m_last_sound / m_origin): false = never heard
bool Chams_SoundStamp(int entindex, float& ms, float* origin)
{
	if (entindex <= 0 || entindex > 64 || s_soundMs[entindex] <= 0.f)
		return false;
	ms = s_soundMs[entindex];
	origin[0] = s_soundOrigin[entindex][0]; origin[1] = s_soundOrigin[entindex][1]; origin[2] = s_soundOrigin[entindex][2];
	return true;
}

void Chams_OnSound(int entindex, float volume, const float* origin)
{
	if (entindex <= 0 || entindex > 64 || !origin || volume < g_chamsSoundMinVolume)
		return;
	s_soundOrigin[entindex][0] = origin[0];
	s_soundOrigin[entindex][1] = origin[1];
	s_soundOrigin[entindex][2] = origin[2];
	s_soundMs[entindex] = EspFx_NowMs();
}

bool Chams_BacktrackActive()
{
	return g_chamsEnable && s_backtrack.Get().used;
}

bool Chams_AnyActive()
{
	if (EspPreview::Drawing()) return true;   // the preview model always takes the chams branch: it stamps the stencil
	if (g_glowEnable) return true;
	if (g_chamsEnable && (s_visible.Get().used || s_occluded.Get().used || s_backtrack.Get().used || s_sound.Get().used)) return true;
	if (g_chamsTeam && (s_teamVisible.Get().used || s_teamOccluded.Get().used)) return true;
	if (g_chamsLocalEnable && s_weapon.Get().used) return true;
	return false;
}

bool Chams_DrawModel(ChamsDrawFn fnOriginal, void* rcx, const void* pState, const void* pInfo, const void* pBones,
                     int entIdx, const char* modelName, int vmKind)
{
	if (!Aim_Active() || !g_modelRender || !g_matSystem || !pInfo || !pBones || !I::RenderView)
		return false;
	if (*reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pInfo) + kInfoFlags) & kDepthPassFlags)
		return false;

	// someone else's override is live (TF2's own glow outline pass, a depth pass): leave it alone.
	// Except TF2's uber material: an ubered player's hats + gun draw under it (tf_item_wearable.cpp /
	// tf_weaponbase.cpp InternalDrawModel), so without this they never got chams. Restore puts it back.
	IMaterial* pOrigMat = nullptr; int iOrigType = 0;
	VCallAt<void>(g_modelRender, kMdlGetMaterialOverride, &pOrigMat, &iOrigType);
	if (pOrigMat)
	{
		const char* sOrig = pOrigMat->GetName();
		if (!sOrig || !strstr(sOrig, "invulnfx"))
			return false;
	}

	// ---- esp preview: EspPreview::Paint() draws a fake CBaseEntity into its own target, same hook,
	// pInfo's entity_index is not real. Unlike the live path below (no stencil -- Glow_Render marks
	// the real scene separately, after the fact), the preview model draws once, so it has to stamp
	// its own stencil ref 2 here for Glow_RenderPreview (chams.cpp "player glow" section) to read
	// back off the preview target's depth-stencil. Menu has no local player: must return before pLocal.
	if (EspPreview::Drawing())
	{
		const DrawCtx ctx{ fnOriginal, rcx, pState, pInfo, pBones, pOrigMat, iOrigType };

		float aOrigColor[3] = { 1.f, 1.f, 1.f };
		I::RenderView->GetColorModulation(aOrigColor);
		const float flOrigBlend = I::RenderView->GetBlend();
		auto Restore = [&]()
		{
			ForceMaterial(pOrigMat, iOrigType);
			I::RenderView->SetColorModulation(aOrigColor);
			I::RenderView->SetBlend(flOrigBlend);
		};

		// IMatRenderContext stencil slots 117 enable / 118 fail / 119 zfail / 120 pass / 121 cmp /
		// 122 ref / 123 testmask / 124 writemask (verified, named kMatSysGetRenderContext etc. in the
		// "player glow" section below -- literal 98 here only because that section's named constants
		// are declared after this function in the file). GetRenderContext hands back a ref-counted
		// context we must Release (1) ourselves -- unlike Glow_Render's own rc, covered separately there.
		void* rc = VCallAt<void*>(g_matSystem, 98 /*GetRenderContext*/);
		auto PreviewStencil = [&](int cmp, int pass)
		{
			if (!rc) return;
			VCallAt<void>(rc, 117, true);
			VCallAt<void>(rc, 121, cmp);
			VCallAt<void>(rc, 122, 2);
			VCallAt<void>(rc, 123, 0xFFu);
			VCallAt<void>(rc, 124, 0xFFu);
			VCallAt<void>(rc, 118, 1 /*keep*/);
			VCallAt<void>(rc, 119, 1 /*keep*/);
			VCallAt<void>(rc, 120, pass);
		};

		PreviewStencil(8 /*always*/, 3 /*replace*/);

		if (!g_chamsEnable)
			ctx.Draw();
		else
		{
			// live order minus backtrack (moved to the end, after the stencil switch below):
			// sound at alpha 1 if it has layers, occluded (ignorez), visible (or a plain draw)
			if (s_sound.Get().used) DrawLayers(s_sound.Get(), true, 1.f, ctx);
			if (s_occluded.Get().used) DrawLayers(s_occluded.Get(), true, 1.f, ctx);

			bool bDrewModel = false;
			if (s_visible.Get().used) bDrewModel = DrawLayers(s_visible.Get(), false, 1.f, ctx);
			if (!bDrewModel) ctx.Draw();

			if (s_backtrack.Get().used)
			{
				// ghosts always behind the model: stencil != 2, never written
				PreviewStencil(6 /*notequal*/, 1 /*keep*/);

				// ModelRenderInfo_t::pModel (+0x20, fbbones.h kMriModel) -> studiohdr_t, bones = the
				// live pose this draw already got (pBones): EspPreview's stream, not a real backtrack
				auto pModel = *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(pInfo) + 0x20);
				auto pHdr = pModel && I::ModelInfoClient ? I::ModelInfoClient->GetStudiomodel(reinterpret_cast<const model_t*>(pModel)) : nullptr;
				const int nBones = pHdr ? std::clamp(pHdr->numbones, 0, (int)MAXSTUDIOBONES) : 0;
				const auto* pSrc = reinterpret_cast<const matrix3x4*>(pBones);
				// one buffer per ghost: a queued studio draw may read its bones after the next ghost is built
				static matrix3x4 s_previewGhost[CBacktrack::kMaxRecords][MAXSTUDIOBONES];
				int nGhost = 0;

				const EspPreview::Stream stream = EspPreview::GhostStream();
				const int records = stream.records;

				auto DrawGhost = [&](int i, float alpha)
				{
					if (alpha <= 0.f || nBones <= 0 || nGhost >= CBacktrack::kMaxRecords) return;
					matrix3x4* ghost = s_previewGhost[nGhost++];
					const float shift = static_cast<float>(i + 1) * stream.step;
					memcpy(ghost, pSrc, sizeof(matrix3x4) * nBones);
					for (int b = 0; b < nBones; ++b) ghost[b][1][3] -= shift;   // world -y = screen right
					DrawLayers(s_backtrack.Get(), g_chamsBacktrackXqz, alpha, ctx, ghost);
				};

				if (records > 0)
				{
					if (g_chamsBacktrackType == 1)
					{
						// botox DrawBacktrack case 1: cap thins, never shortens -- newest + oldest
						// always, rest evenly spaced, oldest drawn first
						const int iMax = std::clamp(g_chamsBacktrackMaxGhosts, 2, records);
						const int iDrawn = records < iMax ? records : iMax;
						for (int pick = iDrawn - 1; pick >= 0; --pick)
						{
							const int i = iDrawn > 1 ? int(float(pick) * float(records - 1) / float(iDrawn - 1) + 0.5f) : 0;
							const float flTick = g_chamsBacktrackGradient && records > 1 ? 1.f - float(i) / float(records - 1) : 1.f;
							DrawGhost(i, flTick);
						}
					}
					else
						DrawGhost(records - 1, 1.f);   // type 0 / 2: oldest record only
				}
			}
		}

		if (rc) { VCallAt<void>(rc, 117, false); VCallAt<void>(rc, 1 /*Release*/); }
		Restore();
		return true;
	}

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	const DrawCtx ctx{ fnOriginal, rcx, pState, pInfo, pBones, pOrigMat, iOrigType };

	float aOrigColor[3] = { 1.f, 1.f, 1.f };
	I::RenderView->GetColorModulation(aOrigColor);
	const float flOrigBlend = I::RenderView->GetBlend();
	auto Restore = [&]()
	{
		ForceMaterial(pOrigMat, iOrigType);
		I::RenderView->SetColorModulation(aOrigColor);
		I::RenderView->SetBlend(flOrigBlend);
	};

	// ---- local: viewmodel (CSS v_ model = gun + hands in one mesh, one material) ----
	if (vmKind)
	{
		if (!g_chamsLocalEnable)
			return false;
		const ChamsLayers::List& l = s_weapon.Get();
		if (!l.used)
			return false;
		DrawLayers(l, false, flOrigBlend, ctx);   // keeps the transparent-viewmodel blend
		Restore();
		return true;
	}

	if (entIdx <= 0)
		return false;
	auto pClient = I::ClientEntityList->GetClientEntity(entIdx);
	if (!pClient)
		return false;
	auto pEnt = pClient->As<CBaseEntity>();

	CCSPlayer* pPlayer = nullptr;
	bool bAttachment = false;
	int iTeam = 0;
	if (pEnt->IsPlayer())
	{
		pPlayer = pEnt->As<CCSPlayer>();
		iTeam = pPlayer->m_iTeamNum();
	}
	else if ((pPlayer = OwnerPlayer(pEnt, modelName)))
	{
		bAttachment = true;
		iTeam = pPlayer->m_iTeamNum();
	}
	else if (g_chamsRagdolls && (pPlayer = RagdollOwner(pEnt)))
	{
		// C_CSRagdoll (DT_CSRagdoll, c_cs_player.cpp:188) networks m_hPlayer, the player who died
		// into it -- RagdollOwner() resolves that EHandle and casts to CCSPlayer. The dead man's
		// OWN team at the moment he died: a late team-swap after death does not retroactively
		// recolour the corpse, same as botox's CTFRagdoll::m_iTeam read.
		bAttachment = true;
		iTeam = pPlayer->m_iTeamNum();
	}
	else
		return false;

	if (pPlayer == pLocal)
		return false;
	const bool bEnemy = iTeam != pLocal->m_iTeamNum();

	// TF2's uber/cloak visible-list swap (Spy cloak, Medic uber) removed, no CS:S analog.
	const bool bChams = bEnemy ? g_chamsEnable : g_chamsTeam;
	const ChamsLayers::List& lVisible  = bEnemy ? s_visible.Get() : s_teamVisible.Get();
	const ChamsLayers::List& lOccluded = bEnemy ? s_occluded.Get() : s_teamOccluded.Get();
	const bool bLivePlayer = pPlayer && !bAttachment;
	const float flSound = bChams && bEnemy && bLivePlayer && s_sound.Get().used ? SoundAlphaFor(entIdx, pLocal->m_vecOrigin()) : 0.f;
	const bool bDrawVisible = bChams && lVisible.used;
	const bool bDrawOccluded = bChams && lOccluded.used;
	const bool bBacktrack = bChams && bEnemy && bLivePlayer && s_backtrack.Get().used;
	// botox: translucent two tone ghosts lay depth, so they draw after the live model (blend over it, not cut it)
	const bool bBacktrackLate = bBacktrack && !g_chamsBacktrackXqz && HasSlot(s_backtrack.Get(), ChamsLayers::TwoTone);

	if (!bDrawVisible && !bDrawOccluded && !bBacktrack && flSound <= 0.f)
		return false;

	// botox order: sound, backtrack, occluded (all ignorez first), then the model itself
	if (flSound > 0.f)  DrawLayers(s_sound.Get(), true, flSound, ctx);
	if (bBacktrack && !bBacktrackLate) DrawBacktrack(pPlayer, entIdx, s_backtrack.Get(), ctx);
	if (bDrawOccluded)  DrawLayers(lOccluded, true, 1.f, ctx);

	bool bDrewModel = false;
	if (bDrawVisible)
		bDrewModel = DrawLayers(lVisible, false, 1.f, ctx);
	if (!bDrewModel)
	{
		Restore();
		ctx.Draw();
	}

	if (bBacktrackLate) DrawBacktrack(pPlayer, entIdx, s_backtrack.Get(), ctx);

	Restore();

	if (bAttachment && !s_loggedAttach)
	{
		s_loggedAttach = true;
		DebugLog("chams: first weapon chams draw (ent %d, %s, owner %d)", entIdx, modelName ? modelName : "?", pPlayer->entindex());
	}
	if (!s_loggedDraw)
	{
		s_loggedDraw = true;
		DebugLog("chams: first chams draw (ent %d, %s, vis %d occ %d)", entIdx, bEnemy ? "enemy" : "team",
			lVisible.used, lOccluded.used);
	}
	return true;
}

// Chams_RenderCloaked (Spy cloak force-draw trick) removed: no cloak/invisibility concept in
// CS:S. Call site removed too (src/hacks/screen/screenfx.cpp, also this unit's).

// ==== player glow ====================================================================
// ref Features/Visuals/Glow (CGlow::SetupBegin/Mid/End + RenderMain) on the slot recipe
// fpsoverlay vmglow.cpp proved in this TF2 build:
//   pass 1  every glowing model drawn invisible (blend 0, dev/glow_color) -> stencil 1 on its pixels
//   pass 2  same models drawn in the glow colour into rb1 (ignorez material: through walls)
//   blur    rb1 -> rb2 (BlurFilterX) -> rb1 (BlurFilterY, $bloomamount = blur)
//   halo    rb1 added onto the screen where stencil == 0: pixel-shifted copies (stencil width),
//           plus one straight copy when blurred
// IMatRenderContext slots (session_facts, screenfx.cpp:555): SetRenderTarget 6, GetRenderTarget 7,
// ClearBuffers 12, ClearColor4ub 73, DrawScreenSpaceRectangle 103, SetStencilEnable 117,
// Fail 118, ZFail 119, Pass 120, CompareFunction 121, ReferenceValue 122, TestMask 123, WriteMask 124.
// PushRenderTargetAndViewport (105) CRASHES this build (MSVC overload group) -> Get/SetRenderTarget;
// never Get/SetViewport (stale queued state). IMaterialSystem: GetBackBufferDimensions 31,
// Begin/EndRenderTargetAllocation 82/83, CreateNamedRenderTargetTextureEx 85. ITexture IncRef 10, DecRef 11.
// Menu mapping (botox popup): stencil 1 + blur 1 by default; "normal blending" makes thickness the
// blur amount (2..30 px -> 0.2..3); legacy "outline" = stencil 2, no blur; wave/pulse as before;
// gradient has no equivalent here. With the players esp on and legacy off, botox's own full-res
// glow (visuals/player_stencil.cpp, gradient / screen wave / normal blending thickness) takes over
// once it has drawn, and this halo is only the fallback (botox glow_effect_spectator). The same
// stencil pass also marks visible pixels 2 for the player stencil / internal glow mask.
namespace
{
	constexpr int kRcSetRT = 6, kRcGetRT = 7, kRcClearBuffers = 12, kRcClearColor4ub = 73, kRcDrawSSRect = 103;
	constexpr int kRcStencilEnable = 117, kRcStencilFail = 118, kRcStencilZFail = 119, kRcStencilPass = 120,
	              kRcStencilCmp = 121, kRcStencilRef = 122, kRcStencilTestMask = 123, kRcStencilWriteMask = 124;
	constexpr int kMatSysBackBufferDims = 31, kMatSysGetBackBufferFormat = 32, kMatSysBeginRTAlloc = 82,
	              kMatSysEndRTAlloc = 83, kMatSysCreateNamedRTEx = 85, kMatSysGetRenderContext = 98;
	constexpr int kTexIncRef = 10, kTexDecRef = 11;
	// SDK/Definitions/Main/ITexture.h declaration order: GetName 0 .. IncrementReferenceCount 10,
	// DecrementReferenceCount 11 (matches kTexIncRef/kTexDecRef above), SetTextureRegenerator 12,
	// Download 13, GetApproximateVidMemBytes 14, IsError 15. No overload groups above it (AddRef/
	// Release are plain non-virtual inline wrappers), so the slot is exact.
	constexpr int kTexIsError = 15;
	// public/materialsystem/imaterialsystem.h StencilComparisonFunction_t / StencilOperation_t
	constexpr int kStencilEqual = 3, kStencilAlways = 8, kOpKeep = 1, kOpReplace = 3;
	constexpr int kStudioRender = 0x1, kStudioNoShadows = 0x80;   // public/model_types.h

	void*         s_glowRB1 = nullptr, *s_glowRB2 = nullptr;      // ITexture*
	IMaterial*    s_glowMatColor = nullptr, *s_glowMatHalo = nullptr, *s_glowMatBlurX = nullptr, *s_glowMatBlurY = nullptr;
	bool          s_glowBloom = false;      // blury has $bloomamount (found at init; the var itself is looked up per pass)
	int           s_glowW = 0, s_glowH = 0;
	bool          s_glowReady = false;
	volatile bool s_glowRendering = false;
	bool          s_glowLoggedDraw = false;

	// esp preview targets + its own halo/blur materials (chams_api.h Glow_PreviewTarget / Glow_RenderPreview).
	// Made once in Glow_LevelInit, before the s_glowReady early-return: they must survive GlowRelease()
	// (called on every map's live-glow rebuild); a reinject makes them again under the same names, like rb1/rb2.
	void*         s_previewTarget = nullptr, *s_previewRB1 = nullptr, *s_previewRB2 = nullptr;   // ITexture*
	IMaterial*    s_previewMatHalo = nullptr, *s_previewMatBlurX = nullptr, *s_previewMatBlurY = nullptr;
	bool          s_previewBloom = false;

	struct GlowItem { CBaseEntity* pEnt; float c[4]; };
	std::vector<GlowItem> s_glowItems;

	// mat_dxlevel only changes on restart: fixed for the session. Glow_Render already inlines this
	// test; named here too so Glow_LevelInit's preview block (no blur shaders on dx8) and
	// Glow_RenderPreview share the one check instead of re-deriving it.
	bool Glow_Dx8() { return I::EngineClient && I::EngineClient->GetDXSupportLevel() < 90; }

	void GlowBackBuffer(int& w, int& h)
	{
		w = h = 0;
		if (g_matSystem) VCallAt<void>(g_matSystem, kMatSysBackBufferDims, &w, &h);
	}

	void GlowStencil(void* rc, bool bOn, int iCmp, int iRef, unsigned uTest, unsigned uWrite, int iFail, int iZFail, int iPass)
	{
		VCallAt<void>(rc, kRcStencilEnable, bOn);
		VCallAt<void>(rc, kRcStencilCmp, iCmp);
		VCallAt<void>(rc, kRcStencilRef, iRef);
		VCallAt<void>(rc, kRcStencilTestMask, uTest);
		VCallAt<void>(rc, kRcStencilWriteMask, uWrite);
		VCallAt<void>(rc, kRcStencilFail, iFail);
		VCallAt<void>(rc, kRcStencilZFail, iZFail);
		VCallAt<void>(rc, kRcStencilPass, iPass);
	}

	void GlowSSRect(void* rc, IMaterial* pMat, int dx, int dy, int w, int h)
	{
		using F = void(__fastcall*)(void*, void*, int, int, int, int, float, float, float, float, int, int, void*, int, int);
		((F)(*reinterpret_cast<void***>(rc))[kRcDrawSSRect])(rc, pMat, dx, dy, w, h, 0.f, 0.f, float(w - 1), float(h - 1), w, h, nullptr, 1, 1);
	}

	void GlowDrawAll(bool bColour)
	{
		s_glowRendering = true;
		for (auto& it : s_glowItems)
		{
			if (bColour)
			{
				I::RenderView->SetColorModulation(it.c);
				I::RenderView->SetBlend(it.c[3]);
			}
			// (ref CGlow::DrawModel zeroed Spy invisibility here for a full silhouette --
			// no cloak concept in CS:S, so every item just draws.)
			it.pEnt->DrawModel(kStudioRender | kStudioNoShadows);
		}
		s_glowRendering = false;
	}

	// no C++ unwinding inside: __try (C2712)
	// bHalo = the material system glow (pass 2 + blur + halo); pVisMat = also mark the pixels the
	// player is visible on with stencil 2 (z-tested, invisible draw) for player_stencil.cpp's mask.
	// pRB1/pRB2 + the halo/blur materials are parameters (not the module statics) so Glow_RenderPreview
	// can run the exact same passes against the preview target's own RTs/materials.
	__declspec(noinline) void GlowPasses_SEH(void* rc, int w, int h, int iStencil, float flBlur, bool bHalo, IMaterial* pVisMat,
	                                          void* pRB1, void* pRB2, IMaterial* pMatHalo, IMaterial* pMatBlurX, IMaterial* pMatBlurY)
	{
		__try
		{
			float aColor[3] = { 1.f, 1.f, 1.f };
			I::RenderView->GetColorModulation(aColor);
			const float flBlend = I::RenderView->GetBlend();
			IMaterial* pOrigMat = nullptr; int iOrigType = 0;
			VCallAt<void>(g_modelRender, kMdlGetMaterialOverride, &pOrigMat, &iOrigType);
			const float aWhite[3] = { 1.f, 1.f, 1.f };

			// SetupBegin: silhouette -> stencil 1, nothing visible (blend 0)
			ForceMaterial(s_glowMatColor);
			I::RenderView->SetBlend(0.f);
			I::RenderView->SetColorModulation(aWhite);
			GlowStencil(rc, true, kStencilAlways, 1, 0x0, 0xFF, kOpKeep, kOpReplace, kOpReplace);
			GlowDrawAll(false);
			if (pVisMat)
			{
				// botox mask: red = stencil 2..127 (visible), green = 1 (occluded). Depth-tested: only
				// the pixels nothing covers pass, and those become 2
				ForceMaterial(pVisMat);
				GlowStencil(rc, true, kStencilAlways, 2, 0x0, 0xFF, kOpKeep, kOpKeep, kOpReplace);
				GlowDrawAll(false);
				ForceMaterial(s_glowMatColor);
			}
			if (!bHalo)
			{
				VCallAt<void>(rc, kRcStencilEnable, false);
				I::RenderView->SetColorModulation(aColor);
				I::RenderView->SetBlend(flBlend);
				ForceMaterial(pOrigMat, iOrigType);
				return;
			}

			// SetupMid: coloured models into rb1
			VCallAt<void>(rc, kRcStencilEnable, false);
			void* pSavedRT = VCallAt<void*>(rc, kRcGetRT);
			VCallAt<void>(rc, kRcSetRT, pRB1);
			VCallAt<void>(rc, kRcClearColor4ub, (unsigned char)0, (unsigned char)0, (unsigned char)0, (unsigned char)0);
			VCallAt<void>(rc, kRcClearBuffers, true, false, false);
			GlowDrawAll(true);
			VCallAt<void>(rc, kRcSetRT, pSavedRT);

			// SetupEnd
			ChamsMatVar* pBloom = flBlur > 0.f && pMatBlurY ? FindVar(pMatBlurY, "$bloomamount") : nullptr;   // fresh: see Mat
			if (pBloom)
			{
				pBloom->SetFloatValue(flBlur);
				VCallAt<void>(rc, kRcSetRT, pRB2);
				GlowSSRect(rc, pMatBlurX, 0, 0, w, h);
				VCallAt<void>(rc, kRcSetRT, pRB1);
				GlowSSRect(rc, pMatBlurY, 0, 0, w, h);
				VCallAt<void>(rc, kRcSetRT, pSavedRT);
			}

			GlowStencil(rc, true, kStencilEqual, 0, 0xFF, 0x0, kOpKeep, kOpKeep, kOpKeep);
			I::RenderView->SetBlend(1.f);
			I::RenderView->SetColorModulation(aWhite);
			if (iStencil > 0)
			{
				const int iSide = (iStencil + 1) / 2, iCorner = iStencil / 2;
				GlowSSRect(rc, pMatHalo, -iSide, 0, w, h);
				GlowSSRect(rc, pMatHalo, 0, -iSide, w, h);
				GlowSSRect(rc, pMatHalo, iSide, 0, w, h);
				GlowSSRect(rc, pMatHalo, 0, iSide, w, h);
				if (iCorner)
				{
					GlowSSRect(rc, pMatHalo, -iCorner, -iCorner, w, h);
					GlowSSRect(rc, pMatHalo, iCorner, iCorner, w, h);
					GlowSSRect(rc, pMatHalo, iCorner, -iCorner, w, h);
					GlowSSRect(rc, pMatHalo, -iCorner, iCorner, w, h);
				}
			}
			if (flBlur > 0.f)
				GlowSSRect(rc, pMatHalo, 0, 0, w, h);
			VCallAt<void>(rc, kRcStencilEnable, false);

			I::RenderView->SetColorModulation(aColor);
			I::RenderView->SetBlend(flBlend);
			ForceMaterial(pOrigMat, iOrigType);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			s_glowRendering = false;
			DebugLog("glow: exception in the glow pass, glow off until the next map");
			s_glowReady = false;
		}
	}

	void GlowRelease()
	{
		s_glowReady = false;
		auto Drop = [](IMaterial*& p) { if (p) { p->DecrementReferenceCount(); p->DeleteIfUnreferenced(); p = nullptr; } };
		Drop(s_glowMatHalo); Drop(s_glowMatBlurX); Drop(s_glowMatBlurY); Drop(s_glowMatColor);
		s_glowBloom = false;
		if (s_glowRB1) { VCallAt<void>(s_glowRB1, kTexDecRef); s_glowRB1 = nullptr; }
		if (s_glowRB2) { VCallAt<void>(s_glowRB2, kTexDecRef); s_glowRB2 = nullptr; }
	}
}

namespace
{
	void Rgba(float out[4], const float* c) { for (int k = 0; k < 4; k++) out[k] = c[k]; }

	// stencil outline accent wave (botox m_players_stencil_wave): c12 / c13. real time keeps moving in
	// pause; phase kept in 0..1 so float precision never runs out
	void OutlineWaveParams(PlayerStencilSettings& st)
	{
		st.outlineWave[0] = st.outlineWave[1] = st.outlineWave[2] = st.outlineWave[3] = 0.f;
		if (!g_esxStencilWave) return;
		const float now = I::GlobalVars ? I::GlobalVars->realtime : 0.f;
		const float phase = fmodf(now * g_esxStencilWaveSpeed, 1.f);
		st.outlineWave[0] = 1.f;
		st.outlineWave[1] = phase < 0.f ? phase + 1.f : phase;
		st.outlineWave[2] = 1.f / std::clamp(g_esxStencilWaveSize, 0.05f, 4.f);
		st.outlineWave[3] = std::clamp(g_esxStencilWaveStrength, 0.f, 1.f);
		Rgba(st.outlineWaveColor, g_esxStencilWaveColor);
		const float angle = g_esxStencilWaveAngle * 0.017453293f;   // c14.zw
		st.waveAngle[2] = cosf(angle);
		st.waveAngle[3] = sinf(angle);
	}

	// wave anchor boxes (botox player_stencil run()): ndc hull bounds of every glow player, wave phase
	// offset by entindex so they don't flash together. A corner behind the camera = no box for that player
	void StencilBoxes(PlayerStencilSettings& st, bool bGlowAnchor, bool bOutlineAnchor)
	{
		st.waveAnchor[0] = st.waveAnchor[1] = st.waveAnchor[2] = 0.f;
		if (!bGlowAnchor && !bOutlineAnchor) return;
		const VMatrix& m = I::EngineClient->WorldToScreenMatrix();
		int n = 0;
		for (auto& it : s_glowItems)
		{
			if (n >= PlayerStencilSettings::kMaxBoxes) break;
			if (!it.pEnt->IsPlayer()) continue;
			const Vec3 o = it.pEnt->GetAbsOrigin(), mn = it.pEnt->m_vecMins(), mx = it.pEnt->m_vecMaxs();
			float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
			bool ok = true;
			for (int i = 0; i < 8 && ok; i++)
			{
				const Vec3 p = { o.x + ((i & 1) ? mx.x : mn.x), o.y + ((i & 2) ? mx.y : mn.y), o.z + ((i & 4) ? mx.z : mn.z) };
				const float w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
				if (w < 1.f) { ok = false; break; }
				const float x = (m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3]) / w;
				const float y = (m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3]) / w;
				lo[0] = std::min(lo[0], x); lo[1] = std::min(lo[1], y);
				hi[0] = std::max(hi[0], x); hi[1] = std::max(hi[1], y);
			}
			if (!ok) continue;
			float* b = st.boxes[n];
			b[0] = lo[0]; b[1] = lo[1]; b[2] = hi[0]; b[3] = hi[1];
			st.boxPhase[n][0] = (float)it.pEnt->entindex() * 0.17f;
			n++;
		}
		st.waveAnchor[0] = bGlowAnchor ? 1.f : 0.f;
		st.waveAnchor[1] = (float)n;
		st.waveAnchor[2] = bOutlineAnchor ? 1.f : 0.f;
	}

	// preview twin: one box over the given capsules' ends (already ndc), phase 0
	void PreviewBox(PlayerStencilSettings& st, bool bGlowAnchor, bool bOutlineAnchor)
	{
		st.waveAnchor[0] = st.waveAnchor[1] = st.waveAnchor[2] = 0.f;
		if ((!bGlowAnchor && !bOutlineAnchor) || st.nCapsules <= 0) return;
		float* b = st.boxes[0];
		b[0] = b[1] = 1e9f; b[2] = b[3] = -1e9f;
		for (int i = 0; i < st.nCapsules; i++)
			for (const float* p : { st.capsules[i].a, st.capsules[i].b })
			{
				b[0] = std::min(b[0], p[0]); b[1] = std::min(b[1], p[1]);
				b[2] = std::max(b[2], p[0]); b[3] = std::max(b[3], p[1]);
			}
		st.boxPhase[0][0] = 0.f;
		st.waveAnchor[0] = bGlowAnchor ? 1.f : 0.f;
		st.waveAnchor[1] = 1.f;
		st.waveAnchor[2] = bOutlineAnchor ? 1.f : 0.f;
	}

	// botox player_stencil after_glow(): settings off the menu vars, capsules off the hitboxes
	// botox player_stencil glow settings (gradient / additive radius / wave / pulse) off the menu vars.
	// radiusScale: 2 on the esp preview's 2x target, capped at the 32-tap loop like csgo
	void GlowParams(PlayerStencilSettings& st, float radiusScale)
	{
		const bool gradient = g_glowGradient;
		Rgba(st.glowVisible, g_glowVisColor);
		Rgba(st.glowInvisible, g_glowInvisColor);
		Rgba(st.glowVisibleOuter, gradient ? g_glowVisOuterColor : g_glowVisColor);
		Rgba(st.glowInvisibleOuter, gradient ? g_glowInvisOuterColor : g_glowInvisColor);
		const float start = 1.f - std::clamp(g_glowGradientInner, 0.f, 100.f) / 100.f;
		st.gradient[0] = start;
		st.gradient[1] = 1.f / std::max(1.f - start, 1e-3f);
		const bool additive = !g_glowNormalBlend;
		st.gradient[2] = additive ? 15.f /*k_engine_glow_radius*/ : std::clamp(g_glowThickness, 2.f, 30.f);
		st.gradient[3] = additive ? 1.f : 0.f;

		// real time keeps moving in pause; phases kept in 0..1 so float precision never runs out
		const float now = I::GlobalVars ? I::GlobalVars->realtime : 0.f;
		st.wave[0] = st.wave[1] = st.wave[2] = st.wave[3] = 0.f;
		if (g_glowWave)
		{
			const float phase = fmodf(now * g_glowWaveSpeed, 1.f);
			st.wave[0] = 1.f;
			st.wave[1] = phase < 0.f ? phase + 1.f : phase;
			st.wave[2] = 1.f / std::clamp(g_glowWaveSize, 0.05f, 4.f);
			st.wave[3] = std::clamp(g_glowWaveStrength, 0.f, 1.f);
			Rgba(st.waveColor, g_glowWaveColor);
			const float angle = g_glowWaveAngle * 0.017453293f;   // c14.xy
			st.waveAngle[0] = cosf(angle);
			st.waveAngle[1] = sinf(angle);
		}
		if (g_glowPulse)
		{
			const float floor_ = std::clamp(g_glowPulseMin, 0.f, 100.f) / 100.f;
			const float wave = 0.5f + 0.5f * sinf(6.2831853f * fmodf(now * g_glowPulseSpeed, 1.f));
			const float scale = floor_ + (1.f - floor_) * wave;
			for (float* c : { st.glowVisible, st.glowInvisible, st.glowVisibleOuter, st.glowInvisibleOuter })
				c[3] *= scale;
			if (!additive)
				st.gradient[2] = std::max(2.f, st.gradient[2] * (0.6f + 0.4f * scale));
		}
		st.gradient[2] = std::min(st.gradient[2] * radiusScale, 31.f);
	}

	void StencilSubmit(bool bOutline, bool bInner, bool bGlow)
	{
		static PlayerStencilSettings st;   // ~19 KB: not on the stack
		st.outline = bOutline; st.glow = bGlow; st.inner = bInner;
		Rgba(st.innerVisible, g_esxInnerGlowVisColor);
		Rgba(st.innerInvisible, g_esxInnerGlowInvisColor);
		st.innerReach[0] = std::clamp(g_esxInnerGlowIntensity, 0.25f, 4.f);
		st.innerReach[2] = std::clamp(g_esxInnerGlowSize, 2.f, 30.f);
		st.innerBones = bInner && g_esxInnerGlowBones;
		st.nCapsules = 0;
		if (st.innerBones)
		{
			// TF2 hitboxes are boxes: segment down the long axis, radius = mean of the two short half-extents
			const VMatrix& m = I::EngineClient->WorldToScreenMatrix();
			const float focal = sqrtf(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]);
			const float width = std::clamp(g_esxInnerGlowBoneWidth, 10.f, 150.f) / 100.f;
			auto project = [&](const Vec3& p, float ndc[2], float& w) {
				w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
				if (w < 1.f) return false;
				ndc[0] = (m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3]) / w;
				ndc[1] = (m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3]) / w;
				return true;
			};
			matrix3x4 aBones[MAXSTUDIOBONES];
			for (auto& it : s_glowItems)
			{
				if (!it.pEnt->IsPlayer()) continue;   // weapon items ride their owner's capsules
				auto pPlayer = it.pEnt->As<CCSPlayer>();
				const auto pModel = pPlayer->GetModel();
				const auto pHdr = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
				auto pSet = pHdr ? pHdr->pHitboxSet(0) : nullptr;
				if (!pSet || !pPlayer->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
					continue;
				for (int i = 0; i < pSet->numhitboxes && st.nCapsules < PlayerStencilSettings::kMaxCapsules; i++)
				{
					auto pBox = pSet->pHitbox(i);
					if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES) continue;
					const Vec3 c = (pBox->bbmin + pBox->bbmax) * 0.5f, e = (pBox->bbmax - pBox->bbmin) * 0.5f;
					const int ax = e.x >= e.y && e.x >= e.z ? 0 : e.y >= e.z ? 1 : 2;
					const float ext[3] = { e.x, e.y, e.z };
					const float radius = (ext[(ax + 1) % 3] + ext[(ax + 2) % 3]) * 0.5f;
					if (radius <= 0.f) continue;
					Vec3 a = c, b = c;
					(&a.x)[ax] -= std::max(ext[ax] - radius, 0.f);   // capsule ends: the box minus its rounded caps
					(&b.x)[ax] += std::max(ext[ax] - radius, 0.f);
					Vec3 wa, wb;
					Math::VectorTransform(a, aBones[pBox->bone], wa);
					Math::VectorTransform(b, aBones[pBox->bone], wb);
					PlayerStencilCapsule& cap = st.capsules[st.nCapsules];
					float fa = 0.f, fb = 0.f;
					if (!project(wa, cap.a, fa) || !project(wb, cap.b, fb)) continue;
					cap.sigma[0] = radius * width * focal / fa;
					cap.sigma[1] = radius * width * focal / fb;
					st.nCapsules++;
				}
			}
		}

		Rgba(st.visible, g_esxStencilVisColor);
		Rgba(st.invisible, g_esxStencilInvisColor);
		st.thickness = std::clamp(g_esxStencilThickness, 0.5f, 8.f);

		GlowParams(st, 1.f);
		OutlineWaveParams(st);
		StencilBoxes(st, bGlow && g_glowWave && g_glowWavePlayer, g_esxStencilWave && g_esxStencilWavePlayer);
		PlayerStencil_Submit(st);
	}

	// StencilSubmit's preview twin: capsules off the GIVEN bones/hdr (EspPreview's fake model, no
	// CCSPlayer/SetupBones) projected with the GIVEN world-to-projection (EspPreview's own camera,
	// not WorldToScreenMatrix()). Submitted to player_stencil.cpp's second ("preview") target set
	// so it never fights the live, screen-sized one over a rebuild every frame.
	//
	// Glow: the live full-res glow settings (GlowParams), radius x2 for the 2x target.
	// Capsule generation itself IS CSS's live formula (hitbox box -> long-axis segment, radius =
	// mean of the two short half-extents): identical to TF2's, confirmed against StencilSubmit above.
	void StencilSubmitPreview(bool bOutline, bool bInner, bool bGlow, const float (*w2p)[4], const void* pBonesVoid, int nBones, const void* pHdrVoid)
	{
		static PlayerStencilSettings st;
		st.outline = bOutline; st.glow = bGlow; st.inner = bInner;
		Rgba(st.innerVisible, g_esxInnerGlowVisColor);
		Rgba(st.innerInvisible, g_esxInnerGlowInvisColor);
		st.innerReach[0] = std::clamp(g_esxInnerGlowIntensity, 0.25f, 4.f);
		// fx_stencil.hlsl's gauss() loop ([loop] for i<=32) reads c7.z as its own iteration bound:
		// doubled for the 2x target, capped at 31 so it never asks the loop to run past its hard
		// limit. Capsule sigma below is NOT doubled: it is in NDC units and the shader already
		// scales it by the bound viewport's own half-height (ps_capsule has no loop at all).
		st.innerReach[2] = std::min(std::clamp(g_esxInnerGlowSize, 2.f, 30.f) * 2.f, 31.f);
		st.innerBones = bInner && g_esxInnerGlowBones;
		st.nCapsules = 0;

		const auto* pHdr = static_cast<const studiohdr_t*>(pHdrVoid);
		const auto* pBones = static_cast<const matrix3x4*>(pBonesVoid);

		if (st.innerBones && pHdr && pBones && nBones > 0 && w2p)
		{
			const float focal = sqrtf(w2p[1][0] * w2p[1][0] + w2p[1][1] * w2p[1][1] + w2p[1][2] * w2p[1][2]);
			const float width = std::clamp(g_esxInnerGlowBoneWidth, 10.f, 150.f) / 100.f;
			auto project = [&](const Vec3& p, float ndc[2], float& w) {
				w = w2p[3][0] * p.x + w2p[3][1] * p.y + w2p[3][2] * p.z + w2p[3][3];
				if (w < 1.f) return false;
				ndc[0] = (w2p[0][0] * p.x + w2p[0][1] * p.y + w2p[0][2] * p.z + w2p[0][3]) / w;
				ndc[1] = (w2p[1][0] * p.x + w2p[1][1] * p.y + w2p[1][2] * p.z + w2p[1][3]) / w;
				return true;
			};
			auto pSet = pHdr->pHitboxSet(0);
			if (pSet)
			{
				for (int i = 0; i < pSet->numhitboxes && st.nCapsules < PlayerStencilSettings::kMaxCapsules; i++)
				{
					auto pBox = pSet->pHitbox(i);
					if (!pBox || pBox->bone < 0 || pBox->bone >= nBones) continue;
					const Vec3 c = (pBox->bbmin + pBox->bbmax) * 0.5f, e = (pBox->bbmax - pBox->bbmin) * 0.5f;
					const int ax = e.x >= e.y && e.x >= e.z ? 0 : e.y >= e.z ? 1 : 2;
					const float ext[3] = { e.x, e.y, e.z };
					const float radius = (ext[(ax + 1) % 3] + ext[(ax + 2) % 3]) * 0.5f;
					if (radius <= 0.f) continue;
					Vec3 a = c, b = c;
					(&a.x)[ax] -= std::max(ext[ax] - radius, 0.f);
					(&b.x)[ax] += std::max(ext[ax] - radius, 0.f);
					Vec3 wa, wb;
					Math::VectorTransform(a, pBones[pBox->bone], wa);
					Math::VectorTransform(b, pBones[pBox->bone], wb);
					PlayerStencilCapsule& cap = st.capsules[st.nCapsules];
					float fa = 0.f, fb = 0.f;
					if (!project(wa, cap.a, fa) || !project(wb, cap.b, fb)) continue;
					cap.sigma[0] = radius * width * focal / fa;
					cap.sigma[1] = radius * width * focal / fb;
					st.nCapsules++;
				}
			}
		}

		Rgba(st.visible, g_esxStencilVisColor);
		Rgba(st.invisible, g_esxStencilInvisColor);
		// c0.z in ps_rows/ps_columns: a texel-space constant against the MASK texture's own
		// resolution (not NDC), so it needs the same x2 + 31 cap as innerReach above.
		st.thickness = std::min(std::clamp(g_esxStencilThickness, 0.5f, 8.f) * 2.f, 31.f);
		GlowParams(st, 2.f);
		OutlineWaveParams(st);
		PreviewBox(st, bGlow && g_glowWave && g_glowWavePlayer, g_esxStencilWave && g_esxStencilWavePlayer);
		PlayerStencil_Submit(st, true /*preview instance*/);
	}
}

ForcedMaterialOverride_t o_ForcedMaterialOverride = nullptr;

void __fastcall h_ForcedMaterialOverride(void* rcx, void* pMat, int iType)
{
	if (s_glowRendering) return;   // ref IVModelRender_ForcedMaterialOverride hook
	o_ForcedMaterialOverride(rcx, pMat, iType);
}

bool Glow_Rendering() { return s_glowRendering; }

void Glow_LevelUnload() {}

// CViewRender::LevelInit (flipworld.cpp hook): the one safe time to allocate named render targets.
// The RTs and materials live across maps (fpsoverlay vmglow); only a new backbuffer size remakes them.
// esp preview targets + its own halo/blur materials, once: Glow_LevelInit (map load / inject) or the
// preview's own first open (game thread) when neither ran yet -- css only inits glow on a map load.
// Not part of the live glow's lifetime (GlowRelease never drops them).
void Glow_PreviewInit()
{
	if (!g_matSystem) return;
	// the preview glow draws its silhouettes with the live glow colour material: a mid-session inject has
	// none yet (CSS inits glow on a map load only). GlowRelease drops it like its own, Glow_LevelInit remakes it.
	if (!s_glowMatColor)
		s_glowMatColor = CreateFromVMT("botox_glow_color", "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"vgui/white\"\n\t\"$model\" \"1\"\n}\n");
	if (s_previewTarget) return;
	{
		using CreateRT_t = void*(__fastcall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned);
		using Fmt_t      = int(__fastcall*)(void*);
		auto fnCreate = (CreateRT_t)(*reinterpret_cast<void***>(g_matSystem))[kMatSysCreateNamedRTEx];
		const int bbFmt = ((Fmt_t)(*reinterpret_cast<void***>(g_matSystem))[kMatSysGetBackBufferFormat])(g_matSystem);
		constexpr int pw = int(EspPreview::kWidth * EspPreview::kRtScale), ph = int(EspPreview::kHeight * EspPreview::kRtScale);

		auto IsMissing = [](void* tex) { return !tex || VCallAt<bool>(tex, kTexIsError); };
		auto Create = [&](const char* name, void*& out)
		{
			// no FindTexture first: off the main thread (inject PollThread) an unknown name takes the
			// disk load path and faults in materialsystem. Same name again next inject = the live
			// rb1/rb2 way (CreateNamedRenderTargetTextureEx on an existing name just hands it back).
			out = fnCreate(g_matSystem, name, pw, ph, 0 /*RT_SIZE_NO_CHANGE*/, bbFmt, 1 /*MATERIAL_RT_DEPTH_SEPARATE*/, 0xCu /*CLAMPS|CLAMPT*/, 1u /*HDR*/);
		};

		VCallAt<void>(g_matSystem, kMatSysBeginRTAlloc);
		Create("botox_esp_preview", s_previewTarget);
		Create("botox_esp_preview_rb1", s_previewRB1);
		Create("botox_esp_preview_rb2", s_previewRB2);
		VCallAt<void>(g_matSystem, kMatSysEndRTAlloc);

		if (IsMissing(s_previewTarget) || IsMissing(s_previewRB1) || IsMissing(s_previewRB2))
		{
			DebugLog("preview: render target creation FAILED (%p %p %p)", s_previewTarget, s_previewRB1, s_previewRB2);
			s_previewTarget = s_previewRB1 = s_previewRB2 = nullptr;
		}
		else
		{
			VCallAt<void>(s_previewTarget, kTexIncRef);
			VCallAt<void>(s_previewRB1, kTexIncRef);
			VCallAt<void>(s_previewRB2, kTexIncRef);

			s_previewMatHalo = CreateFromVMT("botox_glow_halo_preview", "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"botox_esp_preview_rb1\"\n\t\"$additive\" \"1\"\n}\n");
			if (!Glow_Dx8())
			{
				s_previewMatBlurX = CreateFromVMT("botox_glow_blurx_preview", "\"BlurFilterX\"\n{\n\t\"$basetexture\" \"botox_esp_preview_rb1\"\n}\n");
				s_previewMatBlurY = CreateFromVMT("botox_glow_blury_preview", "\"BlurFilterY\"\n{\n\t\"$basetexture\" \"botox_esp_preview_rb2\"\n}\n");
				s_previewBloom = s_previewMatBlurY && FindVar(s_previewMatBlurY, "$bloomamount");
			}
			DebugLog("preview: render targets %dx%d ok, halo=%p blurx=%p blury=%p bloom=%d",
				pw, ph, (void*)s_previewMatHalo, (void*)s_previewMatBlurX, (void*)s_previewMatBlurY, (int)s_previewBloom);
		}
	}
}

void Glow_LevelInit()
{
	if (!g_matSystem) return;
	Glow_PreviewInit();

	int w, h; GlowBackBuffer(w, h);
	if (w <= 0 || h <= 0) return;
	if (s_glowReady && w == s_glowW && h == s_glowH) return;
	GlowRelease();
	s_glowW = w; s_glowH = h;

	// flipworld.cpp's proven arguments: RT_SIZE_LITERAL 8, IMAGE_FORMAT_RGB888 2, shared depth 0,
	// CLAMPS|CLAMPT|EIGHTBITALPHA 0x200C, CREATERENDERTARGETFLAGS_HDR 1. One Begin/End block for both:
	// 85 outside a block calls End itself, and End is a full device release/reacquire.
	using CreateRT_t = void*(__fastcall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned);
	auto fnCreate = (CreateRT_t)(*reinterpret_cast<void***>(g_matSystem))[kMatSysCreateNamedRTEx];
	VCallAt<void>(g_matSystem, kMatSysBeginRTAlloc);
	s_glowRB1 = fnCreate(g_matSystem, "botox_glow_rb1", w, h, 8, 2, 0, 0x200Cu, 1u);
	s_glowRB2 = fnCreate(g_matSystem, "botox_glow_rb2", w, h, 8, 2, 0, 0x200Cu, 1u);
	VCallAt<void>(g_matSystem, kMatSysEndRTAlloc);
	if (!s_glowRB1 || !s_glowRB2)
	{
		DebugLog("glow: render target creation FAILED (%p %p)", s_glowRB1, s_glowRB2);
		GlowRelease();
		return;
	}
	VCallAt<void>(s_glowRB1, kTexIncRef);
	VCallAt<void>(s_glowRB2, kTexIncRef);

	// ref CGlow::Initialize materials, through CreateFromVMT (KeyValues -> CreateMaterial, one IncRef)
	// TF2's dev/glow_color does not exist in CSS (not in any installed vpk): same thing built here
	s_glowMatColor = CreateFromVMT("botox_glow_color", "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"vgui/white\"\n\t\"$model\" \"1\"\n}\n");
	s_glowMatHalo  = CreateFromVMT("botox_glow_halo",  "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"botox_glow_rb1\"\n\t\"$additive\" \"1\"\n}\n");
	s_glowMatBlurX = CreateFromVMT("botox_glow_blurx", "\"BlurFilterX\"\n{\n\t\"$basetexture\" \"botox_glow_rb1\"\n}\n");
	s_glowMatBlurY = CreateFromVMT("botox_glow_blury", "\"BlurFilterY\"\n{\n\t\"$basetexture\" \"botox_glow_rb2\"\n}\n");
	s_glowBloom = s_glowMatBlurY && FindVar(s_glowMatBlurY, "$bloomamount");

	s_glowReady = s_glowMatColor && s_glowMatHalo && s_glowMatBlurX && s_glowMatBlurY && s_glowBloom;
	DebugLog("glow: level init %dx%d ok=%d rb1=%p rb2=%p color=%p halo=%p blurx=%p blury=%p bloom=%d",
		w, h, (int)s_glowReady, s_glowRB1, s_glowRB2, (void*)s_glowMatColor, (void*)s_glowMatHalo,
		(void*)s_glowMatBlurX, (void*)s_glowMatBlurY, (int)s_glowBloom);
	if (!s_glowReady) GlowRelease();
}

void* Glow_PreviewTarget() { return s_previewTarget; }

// DoPostScreenSpaceEffects, before the original (ref CClientModeShared_DoPostScreenSpaceEffects):
// the 3D scene is done, viewmodel and HUD are not drawn yet.
void Glow_Render()
{
	// botox player_stencil enabled() / inner_enabled() / glow_wanted(): all need the players esp on
	const bool bEsp = Esp_On();   // halo too: esp bind released = no glow (csgo glow_effect_spectator)
	const bool bLive = bEsp && !PlayerStencil_Failed();
	const bool bOutline = bLive && g_esxStencil, bInner = bLive && g_esxInnerGlow;
	const bool bFullGlow = bLive && g_glowEnable && !g_glowLegacy;
	if (!bEsp || (!g_glowEnable && !bOutline && !bInner))
		return;
	if (!s_glowReady)
	{
		// injected mid-map: the RTs come with the next CViewRender::LevelInit (map change), like flipworld's
		static bool s_logged = false;
		if (!s_logged) { s_logged = true; DebugLog("glow: enabled but no render targets yet -- they are made on the next map load"); }
		return;
	}
	if (!Aim_Active() || !g_matSystem || !g_modelRender
		|| !I::RenderView || !I::ClientEntityList || !I::EngineClient || !I::ModelInfoClient)
		return;
	if (I::EngineClient->GetDXSupportLevel() < 90)   // ref: BlurFilter shaders crash dx8
		return;
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;
	int w, h; GlowBackBuffer(w, h);
	if (w <= 0 || h <= 0 || w != s_glowW || h != s_glowH)   // resized: Glow_LevelInit remakes next map
		return;
	void* rc = VCallAt<void*>(g_matSystem, kMatSysGetRenderContext);
	if (!rc)
		return;

	// ref CGlow::Store: enemy players on screen, colour by line of sight (visible / invisible)
	s_glowItems.clear();
	int aItemOfPlayer[128];
	for (auto& i : aItemOfPlayer) i = -1;
	const float flPulse = GlowPulse();
	const Vec3 vEye = pLocal->GetEyePosition();
	const int nMax = std::min(I::EngineClient->GetMaxClients(), 64);
	for (int i = 1; i <= nMax; ++i)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pPlayer = pClient ? pClient->As<CCSPlayer>() : nullptr;
		if (!pPlayer || pPlayer == pLocal || !pPlayer->IsPlayer() || pPlayer->IsDormant() || !pPlayer->IsAlive() || !pPlayer->ShouldDraw())
			continue;
		if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;   // enemies only (ref CGlow::Store)
		if (!SDK::IsOnScreen(pPlayer, false))
			continue;
		const bool bVisible = SDK::VisPos(pLocal, pPlayer, vEye, pPlayer->GetCenter());
		const Vec3 vOrigin = pPlayer->GetAbsOrigin();
		GlowItem it{ pPlayer };
		const float flCos = cosf(g_glowWaveAngle * 0.017453293f), flSin = sinf(g_glowWaveAngle * 0.017453293f);
		GlowColor(bVisible ? g_glowVisColor : g_glowInvisColor, it.c,
		          g_glowWaveSize * (vOrigin.x * (flCos - flSin) + vOrigin.y * (flSin + flCos)) / 512.f);
		it.c[3] *= flPulse;
		const int idx = pPlayer->entindex();
		if (idx > 0 && idx < 128) aItemOfPlayer[idx] = (int)s_glowItems.size();
		s_glowItems.push_back(it);
	}
	if (s_glowItems.empty())
		return;

	// hats + the held weapon draw as their own entities: same colour as their owner (OwnerPlayer)
	const int nHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= nHighest && i < 2048; ++i)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		if (!pClient) continue;
		auto pEnt = pClient->As<CBaseEntity>();
		if (pEnt->IsDormant() || pEnt->IsPlayer()) continue;
		const char* sModel = nullptr;
		if (auto pModel = pEnt->GetModel()) sModel = I::ModelInfoClient->GetModelName(pModel);
		auto pOwner = OwnerPlayer(pEnt, sModel);
		if (!pOwner) continue;
		const int o = pOwner->entindex();
		if (o <= 0 || o >= 128 || aItemOfPlayer[o] < 0 || !pEnt->ShouldDraw()) continue;
		GlowItem it = s_glowItems[aItemOfPlayer[o]];
		it.pEnt = pEnt;
		s_glowItems.push_back(it);
	}

	int iStencil = 1; float flBlur = 1.f;
	if (g_glowLegacy) { if (g_glowLegacyStyle == 2) { iStencil = 2; flBlur = 0.f; } }
	else if (g_glowNormalBlend) flBlur = std::clamp(g_glowThickness / 10.f, 0.2f, 3.f);

	// botox glow_effect_spectator: the engine halo is the fallback once our full-res glow is live
	const bool bMask = bOutline || bInner || bFullGlow;
	const bool bHalo = g_glowEnable && !(bFullGlow && PlayerStencil_GlowLive());
	GlowPasses_SEH(rc, w, h, iStencil, flBlur, bHalo, bMask ? GetMat(ChamsLayers::Flat, false) : nullptr,
	                s_glowRB1, s_glowRB2, s_glowMatHalo, s_glowMatBlurX, s_glowMatBlurY);
	if (bMask)
		StencilSubmit(bOutline, bInner, bFullGlow);

	if (!s_glowLoggedDraw)
	{
		s_glowLoggedDraw = true;
		DebugLog("glow: first glow pass (%d models, stencil %d, blur %.2f)", (int)s_glowItems.size(), iStencil, flBlur);
	}
}

// Called by EspPreview right after its chams draw, still inside Push3DView on the preview target
// (bound): Glow_Render for one item, the preview entity, g_glowVisColor (it is always "seen").
// pBones/nBones/pHdr/worldToProjection are the preview model's own (EspPreview's camera and bones,
// not WorldToScreenMatrix()/SetupBones -- there is no real player or screen behind this draw).
//
// Re-enters h_DrawModelExecute through pEnt->DrawModel() exactly like the live GlowDrawAll: that
// hook's `if (Glow_Rendering()) { draw plain; return; }` (visuals.cpp:902, before the chams
// branch) already keeps this from re-running the preview chams branch on every glow sub-pass --
// the same guard that keeps the live glow from re-chamsing a player 3x a frame.
//
// Same gates / halo-vs-full-res-glow choice as the live Glow_Render; radii x2 for the 2x target.
void Glow_RenderPreview(void* pEnt, void* pGun, const float (*worldToProjection)[4], const void* pBones, int nBones, const void* pHdr)
{
	if (!pEnt) return;
	// live Glow_Render gates
	const bool bLive = g_espEnable && !PlayerStencil_Failed();
	const bool bOutline = bLive && g_esxStencil, bInner = bLive && g_esxInnerGlow;
	const bool bFullGlow = bLive && g_glowEnable && !g_glowLegacy;
	if (!g_glowEnable && !bOutline && !bInner) return;
	if (!s_glowMatColor || !s_previewMatHalo) return;   // own targets/materials + the shared dev/glow_color; not the live s_glowReady
	if (!g_matSystem || !g_modelRender || !I::RenderView) return;

	void* rc = VCallAt<void*>(g_matSystem, kMatSysGetRenderContext);
	if (!rc) return;

	const int w = int(EspPreview::kWidth * EspPreview::kRtScale), h = int(EspPreview::kHeight * EspPreview::kRtScale);

	// ref CGlow::Store, one item: swap the live item list out for just the preview entity so
	// GlowDrawAll (unmodified) draws only it.
	std::vector<GlowItem> saved;
	saved.swap(s_glowItems);
	GlowItem item{ reinterpret_cast<CBaseEntity*>(pEnt) };
	GlowColor(g_glowVisColor, item.c, 0.f);   // at the origin: wave phase 0
	item.c[3] *= GlowPulse();
	s_glowItems.push_back(item);
	if (pGun)   // live Glow_Render: the held weapon glows in its owner's colour
	{
		item.pEnt = reinterpret_cast<CBaseEntity*>(pGun);
		s_glowItems.push_back(item);
	}

	// same iStencil/flBlur formula as the live pass above (g_glowLegacy / g_glowNormalBlend):
	// pixel-space offsets already computed against w/h = the preview target's own (2x) size.
	int iStencil = 1; float flBlur = 1.f;
	if (g_glowLegacy) { if (g_glowLegacyStyle == 2) { iStencil = 2; flBlur = 0.f; } }
	else if (g_glowNormalBlend) flBlur = std::clamp(g_glowThickness / 10.f, 0.2f, 3.f);

	iStencil = std::min(iStencil * 2, 31);   // 2x target
	const bool bMask = bOutline || bInner || bFullGlow;
	const bool bHalo = g_glowEnable && !(bFullGlow && PlayerStencil_GlowLive());
	GlowPasses_SEH(rc, w, h, iStencil, flBlur, bHalo, bMask ? GetMat(ChamsLayers::Flat, false) : nullptr,
	                s_previewRB1, s_previewRB2, s_previewMatHalo, s_previewMatBlurX, s_previewMatBlurY);

	s_glowItems.swap(saved);

	if (bMask)
		StencilSubmitPreview(bOutline, bInner, bFullGlow, worldToProjection, pBones, nBones, pHdr);

	VCallAt<void>(rc, 1 /*Release*/);
}
