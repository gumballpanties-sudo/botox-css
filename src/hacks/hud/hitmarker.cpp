#include "hitmarker.h"
#include <algorithm>
#include <cmath>

// botox hitmarker.cpp RenderHitmarker 1:1: 4 lines at 45 deg growing out of screen centre over
// 0.1s / speed, then fading out while sliding 5px outward until 0.5s / speed. Outline = the same
// lines underneath, thickness + 2 * outline thickness.
static float s_markerT = 0.f;   // seconds left to draw, counts down in RenderHitMarker

void Hitmarker_OnHurt(int damage, bool killedTarget)
{
    (void)killedTarget;
    if (damage <= 0 || !g_hmEnable) return;
    s_markerT = 0.5f / std::max(g_hmSpeed, 0.01f);
    // no hit sound: CS:S has its own (default hit sounds in game sounds)
}

void RenderHitMarker()
{
    if (!g_hmEnable || s_markerT <= 0.f) return;

    const float speed     = std::max(g_hmSpeed, 0.01f);
    const float duration  = 0.5f / speed;
    const float extension = 0.1f / speed;
    const float full      = g_hmSize;
    const float thickness = g_hmThickness;
    const float outlineW  = thickness + g_hmOutlineThickness * 2.f;

    ImGuiIO& io = ImGui::GetIO();
    const float elapsed = duration - s_markerT;
    float len, alpha = 1.f, offset = 0.f;
    if (elapsed < extension) len = extension > 0.f ? full * elapsed / extension : full;
    else
    {
        len = full;
        const float fade = (elapsed - extension) / (duration - extension);
        alpha  = 1.f - fade;
        offset = fade * 5.f;
    }

    const ImU32 col  = ImColor(g_hmColor[0], g_hmColor[1], g_hmColor[2], g_hmColor[3] * alpha);
    const ImU32 ocol = ImColor(g_hmOutlineColor[0], g_hmOutlineColor[1], g_hmOutlineColor[2], g_hmOutlineColor[3] * alpha);
    const float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
    const float d = 0.70710678f;   // cos 45 == sin 45
    const ImVec2 lines[4][2] = {
        { ImVec2(cx + offset * d, cy + offset * d), ImVec2(cx + (offset + len) * d, cy + (offset + len) * d) },
        { ImVec2(cx - offset * d, cy - offset * d), ImVec2(cx - (offset + len) * d, cy - (offset + len) * d) },
        { ImVec2(cx + offset * d, cy - offset * d), ImVec2(cx + (offset + len) * d, cy - (offset + len) * d) },
        { ImVec2(cx - offset * d, cy + offset * d), ImVec2(cx - (offset + len) * d, cy + (offset + len) * d) },
    };

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (g_hmOutline)
        for (const auto& l : lines) dl->AddLine(l[0], l[1], ocol, outlineW);
    for (const auto& l : lines) dl->AddLine(l[0], l[1], col, thickness);

    s_markerT -= io.DeltaTime;
}
