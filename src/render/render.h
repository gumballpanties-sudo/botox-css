#pragma once
#include "../globals/globals.h"

// -- SetCursorPos hook ---------------------------------------------------------
using SetCursorPos_t = BOOL(WINAPI*)(int, int);
extern SetCursorPos_t o_SetCursorPos;
BOOL WINAPI h_SetCursorPos(int x, int y);

// -- DX9 hooks --------------------------------------------------------------------
using Present_t  = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t    = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using EndScene_t = HRESULT(__stdcall*)(IDirect3DDevice9*);
extern Present_t  o_Present;
extern Reset_t    o_Reset;
extern EndScene_t o_EndScene;
HRESULT __stdcall h_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND win, const RGNDATA* dirty);
HRESULT __stdcall h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp);
HRESULT __stdcall h_EndScene(IDirect3DDevice9* dev);   // slot 42 -- fallback frame build

// -- vgui hook: the crosshair's home ------------------------------------------------
// vgui::IPanel::PaintTraverse, VGUI_Panel009 from vgui2.dll, SLOT 41.
// Counted off public/vgui/IPanel.h with the destructor slot IBaseInterface contributes
// (tier1/interface.h: virtual ~IBaseInterface()) -- Init=1 .. PerformApplySchemeSettings=40,
// PaintTraverse=41. GetName is the same count = 36.
// VPANEL is `uintp` (vgui/VGUI.h:54) = pointer-sized, so uintptr_t on x64. x64 has one
// calling convention -- `this` is just the first argument and there is no edx thunk to add.
using VPANEL_t = uintptr_t;
using PaintTraverse_t = void(*)(void* panels, VPANEL_t vpanel, bool forceRepaint, bool allowForce);
extern PaintTraverse_t o_PaintTraverse;
void h_PaintTraverse(void* panels, VPANEL_t vpanel, bool forceRepaint, bool allowForce);

void* VGui_GetPanelInterface();   // VGUI_Panel009, or null if vgui2.dll has not loaded yet
void  VGui_ResetPanelCache();     // device Reset / level change: FocusOverlayPanel is remade
DWORD Render_PresentTid();        // the render thread, 0 until the first Present

// -- Performance / latency ---------------------------------------------------------
void Perf_Tick();          // applies g_hiTimerRes on change (100ms tick)
void Perf_Shutdown();      // eject: timeEndPeriod

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool GetDX9VTable(void** vt, int n);
void SetupImGuiStyle();
// The menu's own ground, as one call: vertical 20 -> 10 grey gradient plus a 1px
// separator-grey outline, no rounding. menu.cpp builds the window itself the same way
// (GradFillV + four filled strips); this is that skin for the in-world panels
// (watermark, media player) so they read as part of the same menu.
void UiPanel(ImDrawList* dl, const ImVec2& a, const ImVec2& b);
void RenderPsPointWindow();                // drawn by whichever context owns the menu

// -- shared world->screen projector ------------------------------------------------
// Same per-frame matrix the ps overlay already snapshots (PsW2SMatrix: render-thread safe,
// engine slot 36). One accessor so a second unit (killeffects) stops re-deriving this math.
struct Vec3;
bool W2S_Project(const Vec3& v, ImVec2& out);
const float(*Render_W2SMatrix())[4];   // engine world-to-screen 4x4 of the frame being built, null = none
