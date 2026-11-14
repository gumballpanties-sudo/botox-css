#include "rear_mirror.h"
#include "../../globals/globals.h"
#include "../movement/prediction.h"   // g_globalVars
#include "../screen/screenfx.h"       // ScreenFx_QueueFunctor / ScreenFx_FlushRenderContext
#include "../../render/dpi.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
	constexpr const char* kTextureName = "botox_rear_mirror";
	constexpr DWORD kDrawnWindowMs = 250;

	// CViewSetup x64: operator= (client 0xdaf70) copies fields to +0x86 then VMatrix +0x88 (0x40). Plain bools, no bitfields,
	// no motion blur mode / csm culler (bloom off also skips DoImageSpaceMotionBlur, viewrender.cpp:2143)
	constexpr int kViewSize = 0xC8;
	constexpr int kVsX = 0x00, kVsUnscaledX = 0x04, kVsY = 0x08, kVsUnscaledY = 0x0C, kVsWidth = 0x10, kVsUnscaledWidth = 0x14,
	              kVsHeight = 0x18, kVsStereoEye = 0x1C, kVsUnscaledHeight = 0x20, kVsFov = 0x38, kVsAngles = 0x4C, kVsSubrect = 0x68, kVsAspect = 0x6C,
	              kVsOffCenter = 0x70, kVsBloom = 0x84;
	constexpr int kClearFlags = 0x1 | 0x2 | 0x20;   // VIEW_CLEAR_COLOR | DEPTH | STENCIL
	constexpr int kWhatToDraw = 0x4;               // RENDERVIEW_SUPPRESSMONITORRENDERING: no hud, no viewmodel, no 2nd monitor pass

	constexpr int kVrRenderView = 6;   // CViewRender::RenderView( view, clear, what ), client 0x1d5b00
	// CViewRender m_rbTakeFreezeFrame[ eye ] +0x600, m_flFreezeFrameUntil +0x604 ( RenderView freeze frame test, client 0x1d5cc5 )
	constexpr int kTakeFreezeFrame = 0x600, kFreezeFrameUntil = 0x604;
	constexpr int kMatGetBackBufferFormat = 32, kMatCreateNamedRTEx = 85, kMatGetRenderContext = 98;
	constexpr int kTexActualWidth = 3, kTexActualHeight = 4, kTexIsError = 15;
	constexpr int kRcRelease = 1, kRcSetRenderTarget = 6, kRcPushRtAndViewport = 108, kRcPopRtAndViewport = 109;   // 105..108 = Push overloads reversed

	using RenderView_t = void(__fastcall*)(void*, const void*, int, int);
	RenderView_t o_RenderView = nullptr;

	std::atomic<bool> s_drawing{ false };
	bool  s_failed  = false;
	int   s_frame   = -1;
	void* s_texture = nullptr;   // ITexture*
	void* s_context = nullptr;   // IMatRenderContext*

	std::atomic<IDirect3DTexture9*> s_shown{ nullptr };
	std::atomic<float> s_u{ 1.f }, s_v{ 1.f };
	std::atomic<int> s_width{ 0 }, s_height{ 0 };
	std::atomic<DWORD> s_drawnTick{ 0 };

	// esp_preview_tex.cpp path, no CTexture offsets: render thread, at the end of the mirror pass, the bound target's container
	std::atomic<unsigned> s_grabW{ 0 }, s_grabH{ 0 };
	void Grab()
	{
		IDirect3DDevice9* dev = g_d3dDev;
		IDirect3DSurface9* surf = nullptr;
		if (!dev || FAILED(dev->GetRenderTarget(0, &surf)) || !surf)
			return;
		IDirect3DTexture9* tex = nullptr;
		D3DSURFACE_DESC d{};
		if (SUCCEEDED(surf->GetDesc(&d)) && d.Width == s_grabW.load() && d.Height == s_grabH.load()
			&& SUCCEEDED(surf->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&tex))) && tex)
		{
			s_shown = tex;   // engine owned, dropped on reset
			tex->Release();
		}
		surf->Release();
	}

	struct GrabFunctor { void** vfptr; };
	int   __fastcall Fn_AddRef(void*)           { return 1; }
	int   __fastcall Fn_Release(void*)          { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned) { return p; }
	void  __fastcall Fn_Call(void*)             { MiscHookGuard _g; Grab(); }
	void* s_grabVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Call };
	GrabFunctor s_grab = { s_grabVt };

	template <typename T> T& At(uint8_t* p, int off) { return *reinterpret_cast<T*>(p + off); }

	// game thread, top of RenderView. true = target pushed and `out` holds the back view: render it, then End()
	bool Begin(const void* viewRender, const void* setup, uint8_t* out)
	{
		if (s_drawing.load(std::memory_order_relaxed) || s_failed || g_eject || !g_rearMirror || !g_globalVars || !viewRender)
			return false;

		// freeze frame pending / shown: the first RenderView would grab ( or draw ) it, the freeze cam would show the mirror
		const auto* const render = static_cast<const uint8_t*>(viewRender);
		const int eye = *reinterpret_cast<const int*>(static_cast<const uint8_t*>(setup) + kVsStereoEye);
		if (eye < 0 || eye > 2 || render[kTakeFreezeFrame + eye] ||
		    *reinterpret_cast<const float*>(render + kFreezeFrameUntil) > g_globalVars->curtime)
			return false;

		// first RenderView of the frame = the main view, overlay / monitor views nest inside it
		const int frame = g_globalVars->framecount;
		if (frame == s_frame)
			return false;
		s_frame = frame;

		// box not drawn lately ( tab, eject, loading ): no second scene
		if (GetTickCount() - s_drawnTick.load(std::memory_order_relaxed) > kDrawnWindowMs)
			return false;

		if (!g_matSystem)
			return false;

		if (!s_texture)
		{
			// no FindTexture first (chams.cpp rule): a miss leaves an error texture under the name. reinject: the texture
			// manager finds the live target by name and re-inits it ( materialsystem 0x53e90 )
			s_texture = VCall<void*>(g_matSystem, kMatCreateNamedRTEx, kTextureName, 1, 1, 4 /* RT_SIZE_FULL_FRAME_BUFFER */,
				VCall<int>(g_matSystem, kMatGetBackBufferFormat),
				1 /* MATERIAL_RT_DEPTH_SEPARATE: msaa main depth never pairs with it */, 0x4u | 0x8u /* clamp s t */, 1u /* HDR */);

			if (!s_texture || VCall<bool>(s_texture, kTexIsError))
			{
				s_texture = nullptr;
				s_failed  = true;
				DebugLog("[mirror] render target failed, rear view mirror off");
				return false;
			}
		}

		const int targetW = VCall<int>(s_texture, kTexActualWidth);
		const int targetH = VCall<int>(s_texture, kTexActualHeight);
		if (targetW < 16 || targetH < 16)
			return false;

		const int w = std::clamp(s_width.load(std::memory_order_relaxed), 16, targetW);
		const int h = std::clamp(s_height.load(std::memory_order_relaxed), 16, targetH);

		std::memcpy(out, setup, kViewSize);

		At<int>(out, kVsX) = At<int>(out, kVsUnscaledX) = At<int>(out, kVsY) = At<int>(out, kVsUnscaledY) = 0;
		At<int>(out, kVsWidth) = At<int>(out, kVsUnscaledWidth) = w;
		At<int>(out, kVsHeight) = At<int>(out, kVsUnscaledHeight) = h;
		At<float>(out, kVsAspect) = static_cast<float>(w) / static_cast<float>(h);
		At<float>(out, kVsFov)    = std::clamp(g_rearMirrorFov, 10.f, 170.f);
		float* const ang = &At<float>(out, kVsAngles);
		ang[1] = std::remainder(ang[1] + 180.f, 360.f);
		ang[0] = ang[2] = 0.f;

		// no off centre / subrect, no bloom + auto exposure ( a second luminance sample a frame would drag the main view's exposure )
		At<bool>(out, kVsSubrect) = At<bool>(out, kVsOffCenter) = At<bool>(out, kVsBloom) = false;

		void* const context = VCall<void*>(g_matSystem, kMatGetRenderContext);
		if (!context)
			return false;

		VCall<void>(context, kRcPushRtAndViewport);
		VCall<void>(context, kRcSetRenderTarget, s_texture);

		s_context = context;
		s_grabW = static_cast<unsigned>(targetW);
		s_grabH = static_cast<unsigned>(targetH);
		s_u.store(static_cast<float>(w) / static_cast<float>(targetW), std::memory_order_relaxed);
		s_v.store(static_cast<float>(h) / static_cast<float>(targetH), std::memory_order_relaxed);
		s_drawing.store(true, std::memory_order_relaxed);
		return true;
	}

	void End()
	{
		s_drawing.store(false, std::memory_order_relaxed);

		if (!s_context)
			return;

		// still bound: RenderView ends on SetRenderTarget( saved ) + Push2DView / PopView
		ScreenFx_FlushRenderContext();
		if (!ScreenFx_QueueFunctor(&s_grab))
			Fn_Call(&s_grab);   // mat_queue_mode 0: this thread issues the d3d

		VCall<void>(s_context, kRcPopRtAndViewport);
		VCall<void>(s_context, kRcRelease);
		s_context = nullptr;
	}

	// before the main view: w2s matrix, current view, glow + post state the main pass leaves stay its own
	void __fastcall h_RenderView(void* rcx, const void* setup, int clearFlags, int whatToDraw)
	{
		MiscHookGuard _g;

		alignas(16) uint8_t mirror[kViewSize];
		if (setup && Begin(rcx, setup, mirror))
		{
			o_RenderView(rcx, mirror, kClearFlags, kWhatToDraw);
			End();
		}

		o_RenderView(rcx, setup, clearFlags, whatToDraw);
	}

	__declspec(noinline) void* ReadSlot(void** slot)
	{
		__try { return slot ? *slot : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
}

void RearMirror_Install(void** viewSlot)
{
	void* const view = ReadSlot(viewSlot);
	if (reinterpret_cast<uintptr_t>(view) < 0x10000)
	{
		DebugLog("[mirror] IViewRender not found -- rear view mirror off");
		return;
	}
	InstallVTableHook(view, kVrRenderView, "mirror: CViewRender::RenderView", (void*)&h_RenderView, (void**)&o_RenderView);
}

bool RearMirror_Drawing() { return s_drawing.load(std::memory_order_relaxed); }

void RearMirror_DropTexture() { s_shown = nullptr; }

void RearMirror_Draw()
{
	if (!g_rearMirror || !g_engine || !IsInGame())
		return;

	const ImVec2 size(static_cast<float>(std::max(g_rearMirrorWidth, 16)), static_cast<float>(std::max(g_rearMirrorHeight, 16)));
	const float rounding = std::clamp(g_rearMirrorRounding, 0.f, std::min(size.x, size.y) * 0.5f);

	ImGui::SetNextWindowPos(ImVec2((ImGui::GetMainViewport()->Size.x - size.x) * 0.5f, 16.f), ImGuiCond_FirstUseEver);   // logical px (render/dpi.cpp)
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(16.f, 16.f));

	int flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
	            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
	if (!g_showMenu)
		flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

	if (ImGui::Begin("botox-rear-mirror", nullptr, flags))
	{
		ImDrawList* const list = ImGui::GetWindowDrawList();
		const ImVec2 pos = ImGui::GetWindowPos();
		const ImVec2 end(pos.x + size.x, pos.y + size.y);

		const float scale = Dpi_PanelScale();
		s_width.store(static_cast<int>(size.x * scale + 0.5f), std::memory_order_relaxed);
		s_height.store(static_cast<int>(size.y * scale + 0.5f), std::memory_order_relaxed);
		s_drawnTick.store(GetTickCount(), std::memory_order_relaxed);

		if (IDirect3DTexture9* const texture = s_shown.load())
		{
			const float u = s_u.load(std::memory_order_relaxed), v = s_v.load(std::memory_order_relaxed);

			// scene alpha = whatever the shaders wrote ( depth to alpha ): opaque colour, vertex alpha only
			list->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
				if (g_d3dDev) g_d3dDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			}, nullptr);
			list->AddImageRounded((ImTextureID)texture, pos, end, ImVec2(g_rearMirrorFlip ? u : 0.f, 0.f), ImVec2(g_rearMirrorFlip ? 0.f : u, v),
			                      IM_COL32_WHITE, rounding);
			list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		}
		else
			list->AddRectFilled(pos, end, IM_COL32(0, 0, 0, 160), rounding);

		list->AddRect(pos, end, IM_COL32(0, 0, 0, 255), rounding);
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}
