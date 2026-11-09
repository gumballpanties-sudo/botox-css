// esp preview: the d3d texture behind the preview render target, for ImGui::AddImage. csgo read it off
// CTexture+0x50 -> Texture_t+0xC; the x64 chain is unverified, so no offsets: a functor queued while the
// preview target is bound (aim/esp_preview.cpp, inside Push3DView) asks the device for its render target
// and takes that surface's container. Same call queue as player_stencil.cpp.
#include "esp_preview.h"
#include "../screen/screenfx.h"
#include <atomic>

namespace
{
	std::atomic<IDirect3DTexture9*> s_tex{ nullptr };

	void Capture()
	{
		IDirect3DDevice9* dev = g_d3dDev;
		IDirect3DSurface9* surf = nullptr;
		if (!dev || FAILED(dev->GetRenderTarget(0, &surf)) || !surf)
			return;
		IDirect3DTexture9* tex = nullptr;
		D3DSURFACE_DESC d{};
		// the size check: anything else bound (backbuffer, a failed push) is never shown
		if (SUCCEEDED(surf->GetDesc(&d)) && d.Width == UINT(EspPreview::kWidth * EspPreview::kRtScale)
			&& d.Height == UINT(EspPreview::kHeight * EspPreview::kRtScale)
			&& SUCCEEDED(surf->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&tex))) && tex)
		{
			s_tex = tex;   // the engine owns it: no ref kept, dropped on device reset
			tex->Release();
		}
		surf->Release();
	}

	struct CaptureFunctor { void** vfptr; };
	int   __fastcall Fn_AddRef(void*)           { return 1; }
	int   __fastcall Fn_Release(void*)          { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned) { return p; }
	void  __fastcall Fn_Call(void*)             { MiscHookGuard _g; Capture(); }
	void* s_vt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Call };
	CaptureFunctor s_fn = { s_vt };
}

void EspPreview::QueueTextureCapture()
{
	if (!g_d3dDev) return;
	ScreenFx_FlushRenderContext();
	if (!ScreenFx_QueueFunctor(&s_fn))
		Fn_Call(&s_fn);   // mat_queue_mode 0: this thread issues the d3d
}

void* EspPreview::Texture() { return s_tex.load(); }

void EspPreview::DropTexture() { s_tex = nullptr; }
