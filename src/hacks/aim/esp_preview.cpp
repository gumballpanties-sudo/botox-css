// botox csgo hacks/visuals/players/esp_preview.cpp, CS:S port (from botox-tf2, same engine branch). See visuals/esp_preview.h.
//
// Departures from csgo (Source 2013 has no CMergedMDL): the model is Aletherium ModelPreview's fake entity, a zeroed
// block run through the C_BaseFlex ctor and SetModelByIndex. Two hooks keep it alive out of a map (main menu):
// CModelInfoClient::GetModel hands our model_t* to SetModelByIndex / the draw, R_LightcacheGetDynamic spoofs a
// neutral light state. Both only act on the game thread, out of a map, inside our own calls. Aletherium's
// UpdateVisibility hook (keeps the ent out of the client leaf system) is a private vtable here instead:
// slot 91 = UpdateVisibility (SetModelByIndex calls [vt+0x2d8] after OnNewModel [vt+0x2f0]).
// Its lightcache hook wrote the light state through the 3rd arg (stats); the real args are
// (origin, LightingState_t&, stats, flags, bDebug) (engine call at 0xec346, tf2 0xed546), fixed here.
#include "sdk.h"
#include "esp.h"
#include "backtrack.h"
#include "../visuals/esp_preview.h"
#include "../visuals/chams_api.h"
#include "../esp/esp_queue.h"
#include "../esp/esp_math.h"
#include "../menu/cfg/u_esp.h"   // g_espBacktrackTrail
#include "../../dependencies/imgui/imgui.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
extern void* g_matSystem;               // IMaterialSystem (thread.cpp)
extern void* g_modelRender;             // IVModelRender

bool g_espPreviewDrawing = false;

namespace
{
	// ---- CS:S x64, disasm-checked 11-09 (same sigs / slots as TF2: client / engine / materialsystem) ----
	constexpr const char* kModel = "models/player/t_phoenix.mdl";
	constexpr const char* kSequence = "Idle_lower";    // full body (49/49 bones weighted, not a delta), looping
	constexpr int   kSequenceIndex = 1;                 // virtual list: ragdoll (local) 0, cs_player_shared.mdl Idle_lower 1 (cstrike_pak vpk)
	constexpr float kHullMins[3] = { -16.f, -16.f, 0.f }, kHullMaxs[3] = { 16.f, 16.f, 72.f };   // VEC_HULL
	constexpr float kRunSpeed = 221.f;                  // ak47 carrier max speed (not read off the encrypted script): ghosts trail like a target running sideways
	constexpr int   kHeadHitbox = 12;                   // set 0 "cstrike": 12 = ValveBiped.Bip01_Head1 (0 is the pelvis)
	constexpr float kDefaultTick = 1.f / 66.f;
	constexpr float kFov = 40.f;
	constexpr int   kEntSize = 0x400 * 10;              // Aletherium: C_BaseFlex fits with room
	constexpr int   kVtSlots = 256;                     // C_BaseFlex primary vtable: 220 slots (css 219), copy past it
	constexpr int   kSlotUpdateVisibility = 91;
	constexpr int   kMenuIndex = 1;                     // out of a map the ent sits on index 1, the GetModel hook answers it
	constexpr int   kStudioRender = 0x1;

	// IMatRenderContext / IMaterialSystem / IStudioRender / IVModelRender slots (disasm, tf2 == css)
	constexpr int kRcRelease = 1, kRcBindCubemap = 5, kRcClearBuffers = 12, kRcAmbientCube = 16, kRcClearColor4ub = 73,
	              kRcSetToneMapScale = 153, kRcGetToneMapScale = 154, kRcLightingOrigin = 157;
	constexpr int kMatFindTexture = 79, kMatGetRenderContext = 98;
	constexpr int kStudioAmbientColors = 17, kStudioLocalLights = 18;   // CStudioRenderContext: 17 = Vector4D* (16 = Vector*, w forced 1)
	constexpr int kMdlForcedMaterialOverride = 1, kMdlSuppressEngineLighting = 22;

	template <typename R, typename... A>
	R VCall(void* p, int idx, A... a) { return reinterpret_cast<R(__fastcall*)(void*, A...)>((*reinterpret_cast<void***>(p))[idx])(p, a...); }

	using Ctor_t = void(__fastcall*)(void*);
	using SetModelByIndex_t = void(__fastcall*)(void*, int);
	using SetAbsAngles_t = void(__fastcall*)(void*, const Vec3&);
	Ctor_t s_ctor = nullptr;
	SetModelByIndex_t s_setModelByIndex = nullptr;
	SetAbsAngles_t s_setAbsAngles = nullptr;

	void* s_studioRender = nullptr;
	CBaseEntity* s_ent = nullptr;
	void* s_vt[kVtSlots + 1] = {};
	const model_t* s_model = nullptr;
	void* s_texture = nullptr;   // ITexture*, aim/chams.cpp Glow_LevelInit
	void* s_cubemap = nullptr;
	int  s_sequence = 0;
	int  s_stage = 0;
	bool s_ready = false, s_failed = false;
	int  s_builtConnected = -1, s_builtIndex = -2;

	// our own calls on the game thread: the GetModel / lightcache hooks act only inside them
	std::atomic<DWORD> s_gameThread{ 0 };
	bool s_inCall = false;
	struct CallScope { CallScope() { s_inCall = true; s_gameThread = GetCurrentThreadId(); } ~CallScope() { s_inCall = false; } };
	bool InOurCall() { return s_inCall && GetCurrentThreadId() == s_gameThread.load(); }

	alignas(16) matrix3x4 s_bones[MAXSTUDIOBONES];
	int s_nBones = 0;

	std::atomic<int>   s_frame{ -1 };
	int                s_seenFrame = -1;
	double             s_lastRender = -1.0;
	std::atomic<float> s_yaw{ 180.f };
	std::atomic<unsigned> s_clear{ 0xFF191919u };

	std::mutex s_mutex;
	EspFrame   s_espBuild, s_espShown;

	void __fastcall NoUpdateVisibility(void*) {}

	double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	template <typename F> bool Seh(F&& f) { __try { f(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
}

// ---- hooks (aim.cpp whitelist) ----------------------------------------------------------------

MAKE_HOOK(CModelInfoClient_GetModel,
	U::Memory.FindSignature("engine.dll", "83 FA ? 0F 8C ? ? ? ? 48 8D 0D ? ? ? ? E9 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24"),
	const model_t*, void* rcx, int iIndex)
{
	if (iIndex == kMenuIndex && s_model && InOurCall() && !I::EngineClient->IsConnected())
		return s_model;
	return CALL_ORIGINAL(rcx, iIndex);
}

namespace
{
	struct PreviewLightingState { Vec3 boxcolor[6]; int numlights; void* locallight[4]; };   // LightingState_t x64
}

MAKE_HOOK(R_LightcacheGetDynamic,
	U::Memory.FindSignature("engine.dll", "48 89 5C 24 ? 44 89 4C 24 ? 4C 89 44 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 48 8B 05"),
	void*, const Vec3* pOrigin, PreviewLightingState* pState, void* pStats, unsigned int uFlags, bool bDebug)
{
	if (!InOurCall() || I::EngineClient->IsInGame())
		return CALL_ORIGINAL(pOrigin, pState, pStats, uFlags, bDebug);
	if (pState)
	{
		for (auto& c : pState->boxcolor) c = Vec3(0.5f, 0.5f, 0.5f);
		pState->numlights = 0;
		for (auto& l : pState->locallight) l = nullptr;
	}
	return nullptr;
}

// ---- record stream ----------------------------------------------------------------------------

EspPreview::Stream EspPreview::GhostStream()
{
	const float interval = I::GlobalVars ? I::GlobalVars->interval_per_tick : 0.f;
	const float tick = I::EngineClient && I::EngineClient->IsConnected() && interval > 0.f ? interval : kDefaultTick;
	const float window = F::Backtrack.TimeLimit();
	return { std::clamp(int(window / tick + 0.5f), 2, CBacktrack::kMaxRecords), kRunSpeed * tick };
}

bool EspPreview::Drawing() { return g_espPreviewDrawing; }

namespace
{
	int FindSequence(const studiohdr_t* pHdr)
	{
		if (kSequenceIndex >= 0) return kSequenceIndex;
		for (int i = 0; pHdr && i < pHdr->numlocalseq; i++)
			if (auto pSeq = pHdr->pLocalSeqdesc(i); pSeq && !_stricmp(reinterpret_cast<const char*>(pSeq) + pSeq->szlabelindex, kSequence))
				return i;
		return 0;
	}

	bool Fail(const char* text)
	{
		DebugLog("[preview] %s", text);
		s_failed = true;
		return false;
	}

	// model_t* + the ent's model index for the current state: in game the precache index, out of a map our model on index 1
	bool SetModel()
	{
		const bool connected = I::EngineClient->IsConnected();
		const int index = connected ? I::ModelInfoClient->GetModelIndex(kModel) : -1;
		if (s_builtConnected >= 0 && connected == (s_builtConnected == 1) && (!connected || index == s_builtIndex))   // -1 = never built
			return !connected || index >= 0;
		if (connected && index < 0)
			return false;   // mid level load: keep the last image
		CallScope _c;
		s_model = I::ModelInfoClient->FindOrLoadModel(kModel);
		if (!s_model) return false;
		if (!Seh([&]() { s_setModelByIndex(s_ent, index >= 0 ? index : kMenuIndex); }))
			return Fail("SetModelByIndex faulted, preview off");
		s_builtConnected = connected ? 1 : 0;
		s_builtIndex = index;
		const studiohdr_t* pHdr = I::ModelInfoClient->GetStudiomodel(s_model);
		s_sequence = FindSequence(pHdr);
		return true;
	}

	// one step per paint: sigs, interfaces, targets, model load, ent, done
	bool Init()
	{
		if (s_ready) return true;
		if (s_failed) return false;
		switch (s_stage)
		{
		case 0:
			s_ctor = reinterpret_cast<Ctor_t>(U::Memory.FindSignature("client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 33 ED"));
			s_setModelByIndex = reinterpret_cast<SetModelByIndex_t>(U::Memory.FindSignature("client.dll", "48 89 5C 24 ? 57 48 83 EC ? 66 89 91"));
			break;
		case 1:
			s_setAbsAngles = reinterpret_cast<SetAbsAngles_t>(U::Memory.FindSignature("client.dll", "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 48 8B FA 48 8B D9 E8 ? ? ? ? F3 0F 10 83"));
			if (auto fn = reinterpret_cast<void* (*)(const char*, int*)>(GetProcAddress(GetModuleHandleA("studiorender.dll"), "CreateInterface")))
				s_studioRender = fn("VStudioRender025", nullptr);
			if (!s_ctor || !s_setModelByIndex || !s_setAbsAngles || !s_studioRender || !g_matSystem || !g_modelRender)
				return Fail("signatures / interfaces missing, preview off");
			break;
		case 2:
			if (!Glow_PreviewTarget())
				Glow_PreviewInit();   // injected mid-session: no map load made them yet
			s_texture = Glow_PreviewTarget();
			if (!s_texture)
				return Fail("render target missing, preview off");
			s_cubemap = VCall<void*>(g_matSystem, kMatFindTexture, "editor/cubemap.hdr", "CubeMap textures", false, 0);
			break;
		case 3:
		{
			CallScope _c;   // synchronous vpk read
			s_model = I::ModelInfoClient->FindOrLoadModel(kModel);
			if (!s_model)
				return Fail("t_phoenix.mdl missing, preview off");
			break;
		}
		case 4:
		{
			s_ent = static_cast<CBaseEntity*>(std::calloc(1, kEntSize));
			if (!s_ent)
				return Fail("alloc failed, preview off");
			CallScope _c;
			if (!Seh([&]() { s_ctor(s_ent); }))
				return Fail("C_BaseFlex ctor faulted, preview off");
			// private vtable: UpdateVisibility would put the ent in the client leaf system (drawn in the world)
			void** vt = *reinterpret_cast<void***>(s_ent);
			std::memcpy(s_vt, vt - 1, sizeof(s_vt));
			s_vt[1 + kSlotUpdateVisibility] = reinterpret_cast<void*>(&NoUpdateVisibility);
			*reinterpret_cast<void***>(s_ent) = s_vt + 1;
			break;
		}
		default:
			if (!SetModel())
				return s_failed ? false : (s_ready = false);
			s_ready = true;
			DebugLog("[preview] ready (sequence %d)", s_sequence);
			return true;
		}
		s_stage++;
		return false;
	}

	void SetStencilOff(void* rc) { VCall<void>(rc, 117, false); }

	void RenderEsp(const VMatrix& worldToProjection, const studiohdr_t* pHdr);

	void Render()
	{
		if (!SetModel())
			return;

		const float yaw = s_yaw.load();
		auto pAnim = s_ent->As<CBaseAnimating>();
		static const int nSkin = U::NetVars.GetNetVar("CBaseAnimating", "m_nSkin");
		static const int nAnimTime = U::NetVars.GetNetVar("CBaseEntity", "m_flAnimTime");
		static double s_cycleClock = Now();
		const double now = Now();
		const float dt = std::clamp(float(now - s_cycleClock), 0.f, 0.1f);
		s_cycleClock = now;

		const studiohdr_t* pHdr = I::ModelInfoClient->GetStudiomodel(s_model);
		if (!pHdr) return;

		// camera: fitted off the engine's projection scale, model 68 % of the height, centred, never moves
		const float height = kHullMaxs[2] - kHullMins[2];
		CViewSetup view{};
		const int w = int(EspPreview::kWidth * EspPreview::kRtScale), h = int(EspPreview::kHeight * EspPreview::kRtScale);
		view.width = view.m_nUnscaledWidth = w;
		view.height = view.m_nUnscaledHeight = h;
		view.fov = kFov;
		view.zNear = 7.f;
		view.zFar = 1000.f;
		view.m_flAspectRatio = float(w) / float(h);
		VMatrix worldToView, viewToProjection, worldToProjection, worldToPixels;
		I::RenderView->GetMatricesForView(view, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels);
		const float distance = height * 0.5f / 0.68f * fabsf(viewToProjection.m[1][1]);
		view.origin = Vec3(-distance, 0.f, height * 0.528f);   // csgo 38 of 72
		I::RenderView->GetMatricesForView(view, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels);

		void* rc = VCall<void*>(g_matSystem, kMatGetRenderContext);
		if (!rc) return;

		bool drawn = false;
		{
			CallScope _c;
			drawn = Seh([&]() {
				*reinterpret_cast<int*>(uintptr_t(s_ent) + nSkin) = 0;
				pAnim->m_nSequence() = s_sequence;
				pAnim->m_flCycle() = fmodf(pAnim->m_flCycle() + dt * 0.5f, 1.f);
				*reinterpret_cast<float*>(uintptr_t(s_ent) + nAnimTime) = I::GlobalVars->curtime;
				s_setAbsAngles(s_ent, Vec3(0.f, yaw, 0.f));
				s_ent->InvalidateBoneCache();   // the bone counter only ticks in a map
				s_nBones = std::min(pHdr->numbones, int(MAXSTUDIOBONES));
				if (!s_ent->SetupBones(s_bones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
					s_nBones = 0;
			});

			Frustum frustum;
			if (drawn)
			{
				if (s_cubemap) VCall<void>(rc, kRcBindCubemap, s_cubemap);
				const Vec3 lightOrigin(0.f, 0.f, 40.f);
				VCall<void>(rc, kRcLightingOrigin, &lightOrigin);
				I::RenderView->Push3DView(view, 0x1 | 0x2 | 0x20 /* clear color, depth, stencil */, reinterpret_cast<ITexture*>(s_texture), frustum);

				// in a map the auto exposure scales every shader output (white chams read grey): neutral here
				Vec3 toneMap(1.f, 1.f, 1.f);
				VCall<void>(rc, kRcGetToneMapScale, &toneMap);
				const Vec3 neutral(1.f, 1.f, 1.f);
				VCall<void>(rc, kRcSetToneMapScale, &neutral);

				const unsigned clear = s_clear.load();
				VCall<void>(rc, kRcClearColor4ub, (unsigned char)(clear & 0xFF), (unsigned char)((clear >> 8) & 0xFF), (unsigned char)((clear >> 16) & 0xFF), (unsigned char)255);
				VCall<void>(rc, kRcClearBuffers, true, true, true);

				Vector4D cube[6]; for (auto& c : cube) c = { 0.4f, 0.4f, 0.4f, 1.f };
				VCall<void>(rc, kRcAmbientCube, cube);
				VCall<void>(s_studioRender, kStudioAmbientColors, (const Vector4D*)cube);
				VCall<void>(s_studioRender, kStudioLocalLights, 0, (const void*)nullptr);
				VCall<void>(g_modelRender, kMdlSuppressEngineLighting, true);
				I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
				I::RenderView->SetBlend(1.f);

				// live passes + ghosts: aim/chams.cpp preview branch (DrawModel -> h_DrawModelExecute)
				g_espPreviewDrawing = true;
				drawn = Seh([&]() { s_ent->DrawModel(kStudioRender); });
				// outline / glow / inner glow: the live pass on this target
				if (drawn)
					drawn = Seh([&]() { Glow_RenderPreview(s_ent, worldToProjection.m, s_bones, s_nBones, pHdr); });
				if (drawn)
					EspPreview::QueueTextureCapture();   // the target is bound: its d3d texture for ImGui
				g_espPreviewDrawing = false;

				SetStencilOff(rc);
				VCall<void>(g_modelRender, kMdlSuppressEngineLighting, false);
				VCall<void>(g_modelRender, kMdlForcedMaterialOverride, (void*)nullptr, 0);
				I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
				I::RenderView->SetBlend(1.f);
				VCall<void>(rc, kRcSetToneMapScale, &toneMap);
				I::RenderView->PopView(frustum);
				if (s_cubemap) VCall<void>(rc, kRcBindCubemap, (void*)nullptr);
			}
		}
		VCall<int>(rc, kRcRelease);

		if (!drawn)
		{
			Fail("model draw faulted, preview off");
			s_ready = false;
			s_ent = nullptr;   // leaked on purpose: its state is unknown
			EspPreview::DropTexture();
			return;
		}

		RenderEsp(worldToProjection, pHdr);
	}
}

void EspPreview::Paint()
{
	const int frame = s_frame.load();
	const bool wanted = frame != s_seenFrame;
	s_seenFrame = frame;
	if (!wanted || G::Unload || !g_espPreview || !I::EngineClient || !I::RenderView || !I::ModelInfoClient)
		return;

	// the target keeps the last frame: 60 renders a second whatever the game fps
	const double now = Now();
	if (now >= s_lastRender && now - s_lastRender < 1.0 / 60.0)
		return;
	s_lastRender = now;

	if (Init())
		Render();
}

void EspPreview::OnEject()
{
	// the ent is leaked, never freed: nothing may hold it, but a free under a late engine read is a crash
	EspPreview::DropTexture();
	s_ready = false;
	s_failed = true;
}

// ---- 2d esp -----------------------------------------------------------------------------------

namespace
{
	uint64_t LocalSteamId()
	{
		HMODULE h = GetModuleHandleA("steam_api64.dll");
		auto user = h ? reinterpret_cast<void* (*)()>(GetProcAddress(h, "SteamAPI_SteamUser_v023")) : nullptr;
		auto id = h ? reinterpret_cast<uint64_t(*)(void*)>(GetProcAddress(h, "SteamAPI_ISteamUser_GetSteamID")) : nullptr;
		void* p = user ? user() : nullptr;
		return p && id ? id(p) : 0;
	}

	// same DrawPlayer as the live loop, projected through the preview camera onto the kWidth x kHeight body
	void RenderEsp(const VMatrix& worldToProjection, const studiohdr_t* pHdr)
	{
		s_espBuild.Clear();
		[&]() {
			if (!g_espEnable || !s_nBones)
				return;
			// live gates: distance cut + fade on a target 600 u out
			constexpr float kDistance = 600.f;
			const float a = EspMath::DistanceAlpha(kDistance, g_espMaxDistance, g_espDistanceFade);
			if (a <= 0.f)
				return;

			EspPlayer p;
			p.m_alpha = std::min(a, 1.f);
			p.m_mins = Vec3(kHullMins[0], kHullMins[1], kHullMins[2]);
			p.m_maxs = Vec3(kHullMaxs[0], kHullMaxs[1], kHullMaxs[2]);
			p.m_name = "botox";
			p.m_health = 76;
			p.m_healthFactor = 0.76f;
			p.m_visible = true;
			p.m_distance = kDistance;
			p.m_steamId = g_espAvatar ? LocalSteamId() : 0;   // CS:S bots have no portrait: your own avatar
			p.m_weaponId = WEAPON_AK47;
			p.m_hasAmmo = g_espAmmoBar;
			p.m_ammoFactor = 23.f / 30.f;
			p.m_money = 16000;
			if (g_espFlags)
				for (bool& f : p.m_flags) f = true;   // the flag list picks which ones show
			p.m_studio = pHdr;
			p.m_bones = s_bones;
			auto pSet = pHdr->pHitboxSet(0);
			auto pHead = pSet && pSet->numhitboxes > kHeadHitbox ? pSet->pHitbox(kHeadHitbox) : nullptr;
			if (p.m_steamId && g_espAvatarPosition == 3 && pHead && pHead->bone >= 0 && pHead->bone < s_nBones)
			{
				const Vec3 e = pHead->bbmax - pHead->bbmin;
				p.m_headRadius = std::max(std::max(e.x, e.y), e.z) * 0.5f;
				Math::VectorTransform((pHead->bbmin + pHead->bbmax) * 0.5f, s_bones[pHead->bone], p.m_head);
				p.m_viewRight = Vec3(0.f, -1.f, 0.f);
				p.m_hasHead = true;
			}
			if (g_espBacktrackTrail && pHead && pHead->bone >= 0 && pHead->bone < s_nBones)
			{
				Vec3 head;
				Math::VectorTransform((pHead->bbmin + pHead->bbmax) * 0.5f, s_bones[pHead->bone], head);
				const EspPreview::Stream stream = EspPreview::GhostStream();
				for (int i = 0; i < stream.records; i++)
					p.m_trail.push_back(head - Vec3(0.f, float(i + 1) * stream.step, 0.f));
			}

			// W2S + the esp queue point at the preview for this one call (game thread, same as the live esp)
			const VMatrix savedMatrix = H::Draw.m_mWorldToProjection;
			const int savedW = H::Draw.m_nScreenW, savedH = H::Draw.m_nScreenH;
			H::Draw.m_mWorldToProjection = worldToProjection;
			H::Draw.m_nScreenW = int(EspPreview::kWidth);
			H::Draw.m_nScreenH = int(EspPreview::kHeight);
			std::swap(g_espQBuild.cmds, s_espBuild.cmds);
			std::swap(g_espQBuild.pts, s_espBuild.pts);
			std::swap(g_espQBuild.text, s_espBuild.text);
			F::Esp.DrawPlayer(p);
			std::swap(g_espQBuild.cmds, s_espBuild.cmds);
			std::swap(g_espQBuild.pts, s_espBuild.pts);
			std::swap(g_espQBuild.text, s_espBuild.text);
			H::Draw.m_mWorldToProjection = savedMatrix;
			H::Draw.m_nScreenW = savedW;
			H::Draw.m_nScreenH = savedH;
		}();

		std::lock_guard<std::mutex> lock(s_mutex);
		std::swap(s_espShown, s_espBuild);
	}
}

void EspPreview::DrawBody()
{
	s_frame.store(ImGui::GetFrameCount());

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 end(pos.x + kWidth, pos.y + kHeight);

	ImGui::InvisibleButton("##esp preview body", ImVec2(kWidth, kHeight));
	if (ImGui::IsItemActive())
		s_yaw.store(s_yaw.load() + ImGui::GetIO().MouseDelta.x * 0.5f);

	const ImU32 background = ImGui::GetColorU32(ImGuiCol_ChildBg) | IM_COL32_A_MASK;
	s_clear.store(background);

	// group box look (RenderChildBorder): child bg + menu outline, window rounding - 2
	const float rounding = std::max(ImGui::GetStyle().WindowRounding - 2.f, 0.f);
	dl->AddRectFilled(pos, end, background, rounding);

	if (void* tex = EspPreview::Texture())
		dl->AddImageRounded((ImTextureID)tex, pos, end, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE, rounding);
	else
	{
		const char* text = s_failed ? "preview unavailable" : "loading";
		const ImVec2 size = ImGui::CalcTextSize(text);
		dl->AddText(ImVec2(pos.x + (kWidth - size.x) * 0.5f, pos.y + (kHeight - size.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
	}

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (!s_espShown.cmds.empty())
		{
			// esp is laid out on 0..kWidth: emit unclipped (text culls on the clip rect), then move it onto the body and clip there
			dl->PushClipRect(ImVec2(-100000.f, -100000.f), ImVec2(100000.f, 100000.f), false);
			const int firstVertex = dl->VtxBuffer.Size, firstCommand = dl->CmdBuffer.Size - 1;
			EspQ_ReplayFrame(dl, s_espShown);
			for (int i = firstVertex; i < dl->VtxBuffer.Size; i++)
				dl->VtxBuffer[i].pos.x += pos.x, dl->VtxBuffer[i].pos.y += pos.y;
			for (int i = firstCommand; i < dl->CmdBuffer.Size; i++)
				dl->CmdBuffer[i].ClipRect = ImVec4(pos.x, pos.y, end.x, end.y);
			dl->PopClipRect();
		}
	}

	// last: esp never paints over the edge
	dl->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_MenuOutline), rounding);
}
