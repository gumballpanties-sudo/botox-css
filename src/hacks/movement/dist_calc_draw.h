#pragma once
// dist_calc_draw.h -- the distance calculator's ImGui half. dist_calc.h stays engine-free
// (dc::DcResult / dc::MaxBlock are plain floats, run by tests/dist_calc_test.cpp with no engine);
// this is the part that actually draws, ported 1:1 from botox csgo-sdk hacks/movement/dist_calc.cpp
// n_movement::impl_t::dist_calc_render.
#include "dist_calc.h"
#include "movement.h"            // Vec3
#include "../../render/render.h" // W2S_Project
#include "../../globals/globals.h" // g_menuReg, g_accentColor
#include "../../dependencies/imgui/imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

// a: takeoff edge (dc::NearestEdge's point A), b: landing edge (point B), r: dc::Verdict(a, b, ...)
// for that same a/b. Call only once the second point is placed (botox's data.count == 2) -- draw
// the lone "A" marker yourself the rest of the time, same split as dist_calc_render's own early
// `if (data.count < 2) return;` after the first marker.
// point A alone (only click placed so far) -- botox dist_calc_render's own early-return path
// ("if (data.count < 2) return" after drawing A). Same marker look as DcRenderVerdict's.
inline void DcRenderPointA(const Vec3& a)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();
    if (!font) return;
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], 1.f));
    const ImU32 white  = IM_COL32(255, 255, 255, 255);
    ImVec2 sa{};
    if (!W2S_Project(a, sa)) return;
    constexpr float radius = 11.f;
    dl->AddCircleFilled(sa, radius, IM_COL32(0, 0, 0, 255), 16);
    dl->AddCircle(sa, radius, accent, 16);
    const ImVec2 lw = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, "A");
    dl->AddText(font, font->FontSize, ImVec2(sa.x - lw.x * 0.5f, sa.y - font->FontSize * 0.5f - 1.f), accent, "A");
    const ImVec2 bw = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, "takeoff");
    dl->AddText(font, font->FontSize, ImVec2(sa.x - bw.x * 0.5f, sa.y - radius - font->FontSize - 3.f), white, "takeoff");
}

inline void DcRenderVerdict(const Vec3& a, const Vec3& b, const dc::DcResult& r)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();
    if (!font) return;
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], 1.f));
    const ImU32 white  = IM_COL32(255, 255, 255, 255);
    const ImU32 grey   = IM_COL32(128, 128, 128, 255);

    ImVec2 sa{}, sb{};
    const bool onA = W2S_Project(a, sa);
    const bool onB = W2S_Project(b, sb);

    if (onA && onB)
    {
        // marching-ants dashed line, clipped to screen: an unclipped end behind the camera is a
        // million-pixel line (dist_calc.cpp's own reason for the clip)
        const ImVec2 view = ImGui::GetIO().DisplaySize;
        const float dx = sb.x - sa.x, dy = sb.y - sa.y;
        const float p[4] = { -dx, dx, -dy, dy }, q[4] = { sa.x, view.x - sa.x, sa.y, view.y - sa.y };
        float t0 = 0.f, t1 = 1.f;
        for (int i = 0; i < 4; i++)
        {
            if (p[i] == 0.f) { if (q[i] < 0.f) t1 = -1.f; }
            else if (p[i] < 0.f) t0 = (std::max)(t0, q[i] / p[i]);
            else t1 = (std::min)(t1, q[i] / p[i]);
        }
        const float l = std::sqrt(dx * dx + dy * dy);
        if (t0 < t1 && l > 0.f)
        {
            const ImVec2 clipA{ sa.x + dx * t0, sa.y + dy * t0 }, clipB{ sa.x + dx * t1, sa.y + dy * t1 };
            dl->AddLine(clipA, clipB, IM_COL32(0, 0, 0, 128), 3.f);
            constexpr float s = 10.f;
            static float offset = 0.f;
            offset = std::fmod(offset + 50.f * ImGui::GetIO().DeltaTime, 2.f * s);
            const float lo = t0 * l, hi = t1 * l;
            for (long long m = (long long)((lo + offset) / s);; m++)
            {
                const float d0 = (std::max)(m * s - offset, lo), d1 = (std::min)((m + 1) * s - offset, hi);
                if (d0 >= hi) break;
                if (d1 > d0)
                    dl->AddLine(ImVec2(sa.x + dx * (d0 / l), sa.y + dy * (d0 / l)),
                                ImVec2(sa.x + dx * (d1 / l), sa.y + dy * (d1 / l)),
                                m % 2 == 0 ? accent : grey, 1.f);
            }
        }
    }

    const auto marker = [&](const ImVec2& p, const char* letter, const char* label)
    {
        constexpr float radius = 11.f;
        dl->AddCircleFilled(p, radius, IM_COL32(0, 0, 0, 255), 16);
        dl->AddCircle(p, radius, accent, 16);
        const ImVec2 lw = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, letter);
        dl->AddText(font, font->FontSize, ImVec2(p.x - lw.x * 0.5f, p.y - font->FontSize * 0.5f - 1.f), accent, letter);
        const ImVec2 bw = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, label);
        dl->AddText(font, font->FontSize, ImVec2(p.x - bw.x * 0.5f, p.y - radius - font->FontSize - 3.f), white, label);
    };
    if (onA) marker(sa, "A", "takeoff");
    if (onB) marker(sb, "B", "landing");

    ImVec2 mid{};
    if (!W2S_Project((a + b) * 0.5f, mid)) return;
    char lines[3][96];
    snprintf(lines[0], sizeof(lines[0]), "%.1f block   dz %+.1f", r.block, r.dz);
    snprintf(lines[1], sizeof(lines[1]), "%s", dc::VerdictLine(r.block, r.ljMax, "lj").c_str());
    snprintf(lines[2], sizeof(lines[2]), "%s", dc::VerdictLine(r.block, r.jbMax, "jb").c_str());
    for (int i = 0; i < 3; i++)
    {
        const ImU32 col = i == 0 ? white : (std::strstr(lines[i], ": yes") ? accent : grey);
        const ImVec2 w = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, lines[i]);
        dl->AddText(font, font->FontSize, ImVec2(mid.x - w.x * 0.5f, mid.y + 8.f + i * (font->FontSize + 2.f)), col, lines[i]);
    }
}
