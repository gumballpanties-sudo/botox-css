#pragma once
// botox "dpi scale" + "dpi scale panels" (globals/render/render.cpp), 1:1.
//  - imgui lays out in logical px (screen / scale); every window's vertices grow by the scale
//    after Render (Dpi_ScaleDrawData), text from fonts re-rasterized at size x scale ("twins").
//  - the hud panels draw on the background / foreground lists in SCREEN px and stay 1:1, unless
//    "dpi scale panels": then each panel block grows about its own pivot (Dpi_PanelBegin/End).
//  - io.DisplaySize is the SCREEN size outside NewFrame (hud code reads it as the screen), and
//    io.MousePos is logical inside the frame: hud hit tests use Dpi_ScreenMouse / Dpi_PanelMouse.
#include "../dependencies/imgui/imgui.h"

struct IDirect3DDevice9;

// frame order (render.cpp BuildImGuiFrame)
bool  Dpi_UpdateFonts(IDirect3DDevice9* dev);   // before NewFrame. true = twin texture swapped/dropped (drop lists holding it)
void  Dpi_BeginFrame();          // after the backends' NewFrame, before ImGui::NewFrame
void  Dpi_AfterNewFrame();       // right after ImGui::NewFrame
void  Dpi_BeforeRender();        // after ImGui::EndFrame, before ImGui::Render (software cursor lands at the screen mouse)
void  Dpi_ScaleDrawData(ImDrawData* dd);   // after ImGui::Render
void  Dpi_DropFonts();           // main atlas rebuilt / device dropped: twins point at freed fonts or a dead texture

float  Dpi_Scale();              // 1 = off
float  Dpi_PanelScale();         // Dpi_Scale() with "dpi scale panels", else 1
ImVec2 Dpi_ScreenMouse();        // the real mouse, screen px
ImVec2 Dpi_PanelPos(ImVec2 pos, ImVec2 size);          // panel layout pos -> where it shows
ImVec2 Dpi_PanelLayoutPos(ImVec2 shown, ImVec2 size);  // inverse (drags)
ImVec2 Dpi_PanelMouse(ImVec2 pos, ImVec2 size);        // screen mouse in the layout space of the panel at pos/size

// botox begin_stretch_block / end_stretch_block (panel = true): one hud panel's draws on `list`
// (background or foreground). Scaled about its own pivot at End when "dpi scale panels" is on, a
// no-op otherwise. Panels only: crosshair, hitmarker, world markers never. Never nest two on one list.
struct DpiPanel { ImDrawList* list; int v0, c0; };
DpiPanel Dpi_PanelBegin(ImDrawList* list);
void     Dpi_PanelEnd(DpiPanel& p);
struct DpiPanelScope
{
    DpiPanel p;
    explicit DpiPanelScope(ImDrawList* list) : p(Dpi_PanelBegin(list)) {}
    ~DpiPanelScope() { Dpi_PanelEnd(p); }
    DpiPanelScope(const DpiPanelScope&) = delete;
    DpiPanelScope& operator=(const DpiPanelScope&) = delete;
};
