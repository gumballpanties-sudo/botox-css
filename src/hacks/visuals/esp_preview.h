#pragma once
// botox csgo hacks/visuals/players/esp_preview, TF2 port: visuals > esp "esp preview" box right of the menu.
// A CBaseFlex fake entity (Aletherium ModelPreview, no CMergedMDL in Source 2013) wearing soldier.mdl, drawn
// into its own render target through the live chams passes, the live 2d esp (aim/esp.cpp DrawPlayer) on top.
// Plain types only: menu / render TUs include this.

extern bool g_espPreview;   // dllmain.cpp, config "espPreview"

namespace EspPreview
{
	constexpr float kWidth = 260.f, kHeight = 400.f, kRtScale = 2.f;

	void Paint();      // game thread, aim/hooks.cpp IEngineVGui_Paint (menu + in game)
	void DrawBody();   // render thread, inside the preview ImGui window
	void OnEject();
	bool Drawing();    // true while the preview model draws into its target (aim/chams.cpp preview branch)

	// backtrack ghost stream: ghost i sits (i + 1) * step world units along -y (screen right)
	struct Stream { int records; float step; };
	Stream GhostStream();

	// visuals/esp_preview_tex.cpp: IDirect3DTexture9* of the preview target, read on the render thread
	void QueueTextureCapture();   // game thread, while the preview target is bound
	void* Texture();
	void DropTexture();           // render.cpp h_Reset: the target is remade
}
