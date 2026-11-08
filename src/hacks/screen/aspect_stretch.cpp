#include "aspect_stretch.h"
#include "aspect_stretch_math.h"
#include "../../globals/globals.h"
#include "resolution_spoof.h"   // aspect accessor RULE: every aspect read goes through ResSpoof::Aspect*
#include "../../dependencies/imgui/imgui.h"
#include "../../dependencies/imgui/imgui_internal.h"
#include <cfloat>
#include <cmath>

// botox render.cpp begin_stretch_block / end_stretch_block / stretch_overlays, folded into one
// wrapper: the block is whatever the wrapped draw call added to the background + foreground
// lists, plus every window it began (botox's named stretched windows).
static void StretchList(ImDrawList* l, int v0, int c0, float pivot, float scale, bool growOnly)
{
    for (int v = v0; v < l->VtxBuffer.Size; ++v)
        l->VtxBuffer[v].pos.x = AsStretchX(l->VtxBuffer[v].pos.x, pivot, scale);
    // clip rects travel with the vertices or widened panels get cut. grow only on the shared
    // lists: their first command also covers draws from before the block.
    for (int c = c0; c < l->CmdBuffer.Size; ++c) {
        ImVec4& clip = l->CmdBuffer[c].ClipRect;
        const float x = AsStretchX(clip.x, pivot, scale), z = AsStretchX(clip.z, pivot, scale);
        clip.x = growOnly ? ImMin(clip.x, x) : x;
        clip.z = growOnly ? ImMax(clip.z, z) : z;
    }
}

void AspectStretch_Run(void (*draw)(), AsKind kind)
{
    // aspect accessor RULE (resolution_spoof.h): never read g_aspectRatioEnable/g_aspectRatioValue/
    // g_sfxAspect* directly -- ResSpoof's accessors fold in stretch-to-fill (which overrides the
    // plain slider with the res's own aspect, and skips the cheat-overlay case while its own
    // crunch upscale is already widening it -- "never twice").
    const bool wanted = ResSpoof::AspectOn() &&
        (kind == AsCrosshair ? (ResSpoof::AspectCheatOverlay() || ResSpoof::AspectCrosshair() || ResSpoof::AspectHud())
                              : ResSpoof::AspectCheatOverlay());
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float wantedRatio = ResSpoof::AspectWanted();
    const float ratio = wantedRatio < 0.5f ? 0.5f : (wantedRatio > 4.f ? 4.f : wantedRatio);
    const float scale = AsScale(screen.x, screen.y, ratio);
    if (!wanted || fabsf(scale - 1.f) < 0.001f) { draw(); return; }

    ImGuiContext& g = *GImGui;
    ImDrawList* lists[2] = { ImGui::GetBackgroundDrawList(), ImGui::GetForegroundDrawList() };
    int v0[2], c0[2];
    for (int i = 0; i < 2; ++i) { v0[i] = lists[i]->VtxBuffer.Size; c0[i] = ImMax(lists[i]->CmdBuffer.Size - 1, 0); }
    const int firstWindow = g.WindowsActiveCount;

    draw();

    ImVector<ImGuiWindow*> windows;
    for (ImGuiWindow* w : g.Windows)
        if (w && w->LastFrameActive == g.FrameCount && w->BeginOrderWithinContext >= firstWindow && w->DrawList)
            windows.push_back(w);

    float lo = FLT_MAX, hi = -FLT_MAX;
    for (int i = 0; i < 2; ++i)
        for (int v = v0[i]; v < lists[i]->VtxBuffer.Size; ++v) {
            lo = ImMin(lo, lists[i]->VtxBuffer[v].pos.x);
            hi = ImMax(hi, lists[i]->VtxBuffer[v].pos.x);
        }
    for (ImGuiWindow* w : windows)
        for (const ImDrawVert& vtx : w->DrawList->VtxBuffer) { lo = ImMin(lo, vtx.pos.x); hi = ImMax(hi, vtx.pos.x); }
    if (lo > hi) return;   // drew nothing

    // crosshair: about the screen centre (botox push_crosshair_rect); panels: their own pivot
    const float pivot = kind == AsCrosshair ? screen.x * 0.5f : AsPivot(lo, hi, screen.x);
    for (int i = 0; i < 2; ++i) StretchList(lists[i], v0[i], c0[i], pivot, scale, true);
    for (ImGuiWindow* w : windows) StretchList(w->DrawList, 0, 0, pivot, scale, false);

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("aspect ratio: stretching overlays x%.3f (%.0fx%.0f -> %.3f)", scale, screen.x, screen.y, ratio);
    }
}
