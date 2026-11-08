#pragma once
// botox points_ui.cpp style half 1:1 (point_style / ring_draw / design_draw / point_ring /
// point_style_preview + helpers): one look per point kind, shared by the world draws in
// render.cpp (assist, pixeljump, finder dots, calculator markers) and the movement > points
// menu preview. botox paints the world through m_draw_data and the preview through a window
// draw list; here both are ImDrawList, so one painter (world = 32 segments, preview = 64).
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <initializer_list>
#include "../../dependencies/imgui/imgui.h"
#include "../menu/cfg/u_points.h"

extern float g_accentColor[4];

enum PointStyleId : int {
    PS_STYLE_PS = 0,
    PS_STYLE_HB,
    PS_STYLE_PJ,
    PS_STYLE_CALC,
    PS_STYLE_PF_PS,
    PS_STYLE_PF_HB,
    PS_STYLE_PF_PJ,
    PS_STYLE_PF_TB,
    PS_STYLE_COUNT,
};
inline constexpr const char* kPointDesignNames[] = { "arrow", "single arrow", "line", "cross", "x", "dot",
                                                     "inner ring", "diamond", "square", "triangle", "chevron", "burst" };
inline constexpr const char* kPointShapeNames[]  = { "circle", "square", "diamond", "hexagon", "octagon", "triangle" };
inline constexpr int kPointDesignCount = (int)(sizeof(kPointDesignNames) / sizeof(kPointDesignNames[0]));
inline constexpr int kPointShapeCount  = (int)(sizeof(kPointShapeNames) / sizeof(kPointShapeNames[0]));

// no color = live accent. gaps != null = outline shape + gaps editable
struct PointStyle {
    const char* name;
    const char* label;
    float* color;
    int*   gaps;
    bool*  design;
    int*   designType;
    int*   shape;
};

inline const PointStyle& PointStyleGet(int style)
{
    static const PointStyle styles[PS_STYLE_COUNT] = {
        { "pixelsurf assist point", "pixelsurf",  nullptr,         &g_pointGaps[0], &g_pointDesign[0], &g_pointDesignType[0], &g_pointShape[0] },
        { "bounce assist point",    "bounce",     nullptr,         &g_pointGaps[1], &g_pointDesign[1], &g_pointDesignType[1], &g_pointShape[1] },
        { "pixeljump assist point", "pixeljump",  nullptr,         &g_pointGaps[2], &g_pointDesign[2], &g_pointDesignType[2], &g_pointShape[2] },
        { "calculator point",       "calc",       nullptr,         &g_pointGaps[3], nullptr,           nullptr,               &g_pointShape[3] },
        { "finder pixelsurf",       "pixelsurf",  g_pointClrPfPs,  nullptr,         nullptr,           nullptr,               nullptr },
        { "finder headbounce",      "headbounce", g_pointClrPfHb,  nullptr,         nullptr,           nullptr,               nullptr },
        { "finder pixeljump",       "pixeljump",  g_pointClrPfPj,  nullptr,         nullptr,           nullptr,               nullptr },
        { "finder texturebug",      "texturebug", g_pointClrPfTb,  nullptr,         nullptr,           nullptr,               nullptr },
    };
    return styles[std::clamp(style, 0, PS_STYLE_COUNT - 1)];
}

inline ImU32 PointColU32(const float* c, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * std::clamp(alpha, 0.f, 1.f)));
}
// style color (or accent) at alpha, opacity NOT applied (botox style_color in RenderPoints)
inline ImU32 PointStyleRaw(int style, float alpha)
{
    const PointStyle& s = PointStyleGet(style);
    return PointColU32(s.color ? s.color : g_accentColor, alpha);
}
// botox point_style_u32: alpha * points opacity
inline ImU32 PointStyleU32(int style, float alpha)
{
    return PointStyleRaw(style, alpha * std::clamp(g_pointsOpacity, 0.f, 1.f));
}
inline int PointStyleGaps(int style)
{
    const PointStyle& s = PointStyleGet(style);
    return s.gaps ? *s.gaps & 15 : 0;
}
inline int PointStyleDesign(int style)
{
    const PointStyle& s = PointStyleGet(style);
    if (!s.design || !*s.design) return -1;
    return std::clamp(*s.designType, 0, kPointDesignCount - 1);
}
inline int PointStyleShape(int style)
{
    const PointStyle& s = PointStyleGet(style);
    return s.gaps ? std::clamp(*s.shape, 0, kPointShapeCount - 1) : 0;
}
inline float PointsThickness(float base) { return base * std::clamp(g_pointsThickness, 0.25f, 5.f); }
inline float PointsSize() { return std::clamp(g_pointsSize, 0.25f, 4.f); }
inline float PointDesignHalf(float radius) { return radius * std::clamp(g_pointsDesignSize, 0.2f, 1.2f); }
inline float PointsPulse(bool on)
{
    if (!on || !g_pointsPulse) return 1.f;
    return 1.f + 0.1f * std::sin((float)ImGui::GetTime() * 2.f * 3.14159265f * 1.5f);
}
inline float PointArrowLengthScale(float ratio) { return 0.15f + 0.85f * ratio; }
inline float PointArrowFade(float ratio)
{
    const float t = std::clamp((ratio - 0.10f) / 0.30f, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline ImU32 PointWithAlpha(ImU32 c, int a)
{
    return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp(a, 0, 255) << IM_COL32_A_SHIFT);
}

// centre design, oriented along dir. order = kPointDesignNames
inline void PointDesignDraw(ImDrawList* dl, int type, ImVec2 centre, float half, float dirX, float dirY, ImU32 color, float t, int segs)
{
    if (type < 0 || half <= 0.f) return;
    const float dlen = std::sqrt(dirX * dirX + dirY * dirY);
    if (dlen < 0.0001f) return;
    const float ax = dirX / dlen, ay = dirY / dlen;
    const float px = -ay, py = ax;
    const float head = (std::min)(std::clamp(half * 0.4f, 2.f, 6.f), half * 0.5f);
    auto pt = [&](float along, float across) { return ImVec2(centre.x + ax * along + px * across, centre.y + ay * along + py * across); };
    auto line = [&](ImVec2 a, ImVec2 b) { dl->AddLine(a, b, color, t); };
    auto loop = [&](std::initializer_list<ImVec2> pts) {
        const ImVec2* prev = pts.begin() + pts.size() - 1;
        for (const ImVec2& q : pts) { line(*prev, q); prev = &q; }
    };
    switch (type) {
    case 0:
        line(pt(-half, 0.f), pt(half, 0.f));
        line(pt(-half, 0.f), pt(-half + head, -head));
        line(pt(-half, 0.f), pt(-half + head, head));
        [[fallthrough]];
    case 1:
        if (type == 1) line(pt(-half, 0.f), pt(half, 0.f));
        line(pt(half, 0.f), pt(half - head, -head));
        line(pt(half, 0.f), pt(half - head, head));
        break;
    case 2:
        line(pt(-half, 0.f), pt(half, 0.f));
        break;
    case 3:
        line(pt(-half, 0.f), pt(half, 0.f));
        line(pt(0.f, -half), pt(0.f, half));
        break;
    case 4: {
        const float d = half * 0.7071f;
        line(pt(-d, -d), pt(d, d));
        line(pt(-d, d), pt(d, -d));
        break;
    }
    case 5:
        dl->AddCircleFilled(centre, (std::max)(half * 0.3f, 1.5f), color, segs);
        break;
    case 6:
        dl->AddCircle(centre, half * 0.5f, color, segs, t);
        break;
    case 7: {
        const float d = half * 0.6f;
        loop({ pt(d, 0.f), pt(0.f, d), pt(-d, 0.f), pt(0.f, -d) });
        break;
    }
    case 8: {
        const float d = half * 0.45f;
        loop({ pt(d, d), pt(-d, d), pt(-d, -d), pt(d, -d) });
        break;
    }
    case 9:
        loop({ pt(half * 0.6f, 0.f), pt(-half * 0.4f, half * 0.5f), pt(-half * 0.4f, -half * 0.5f) });
        break;
    case 10:
        line(pt(half * 0.35f, 0.f), pt(-half * 0.35f, -half * 0.55f));
        line(pt(half * 0.35f, 0.f), pt(-half * 0.35f, half * 0.55f));
        break;
    case 11:
        for (int i = 0; i < 8; ++i) {
            const float a = i * 3.14159265f / 4.f, c = std::cos(a), s = std::sin(a);
            line(pt(c * half * 0.35f, s * half * 0.35f), pt(c * half, s * half));
        }
        break;
    }
}

// outline: shape polygon split into short pieces; gap bits 1 top 2 bottom 4 left 8 right skip
// that quarter. order = kPointShapeNames
inline void PointRingDraw(ImDrawList* dl, int shape, ImVec2 c, float r, ImU32 color, int gaps, float t, int segs)
{
    constexpr float pi = 3.14159265f;
    gaps &= 15;
    if (gaps == 15 || r <= 0.f) return;
    if (shape <= 0 && gaps == 0) { dl->AddCircle(c, r, color, segs, t); return; }
    constexpr int   kSides[] = { 64, 4, 4, 6, 8, 3 };
    constexpr float kStart[] = { 0.f, pi * 0.25f, 0.f, 0.f, pi / 8.f, -pi * 0.5f };
    const int s = std::clamp(shape, 0, 5);
    const int sides = kSides[s];
    const int sub = (std::max)(1, 64 / sides);
    auto vertex = [&](int i) {
        const float a = kStart[s] + i * 2.f * pi / sides;
        return ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    };
    for (int i = 0; i < sides; ++i) {
        const ImVec2 v0 = vertex(i), v1 = vertex(i + 1);
        for (int k = 0; k < sub; ++k) {
            const float f0 = (float)k / sub, f1 = (float)(k + 1) / sub;
            const ImVec2 a(v0.x + (v1.x - v0.x) * f0, v0.y + (v1.y - v0.y) * f0);
            const ImVec2 b(v0.x + (v1.x - v0.x) * f1, v0.y + (v1.y - v0.y) * f1);
            float mid = std::atan2((a.y + b.y) * 0.5f - c.y, (a.x + b.x) * 0.5f - c.x);
            if (mid < 0.f) mid += 2.f * pi;
            const int quarter = mid < pi * 0.25f || mid >= pi * 1.75f ? 8
                              : mid < pi * 0.75f                      ? 2
                              : mid < pi * 1.25f                      ? 4
                                                                      : 1;
            if (!(gaps & quarter)) dl->AddLine(a, b, color, t);
        }
    }
}

// botox point_ring: optional 3 step glow, then the outline
inline void PointRing(ImDrawList* dl, int style, ImVec2 c, float r, ImU32 color, float t, int segs)
{
    const int shape = PointStyleShape(style), gaps = PointStyleGaps(style);
    if (g_pointsGlow) {
        const unsigned a = (color >> IM_COL32_A_SHIFT) & 0xFFu;
        for (int i = 1; i <= 3; ++i) {
            const unsigned ga = a * (4 - i) / 10;
            PointRingDraw(dl, shape, c, r + t * i * 1.5f, (color & ~IM_COL32_A_MASK) | (ga << IM_COL32_A_SHIFT), gaps, t * 1.5f, segs);
        }
    }
    PointRingDraw(dl, shape, c, r, color, gaps, t, segs);
}
// botox point_ring_emit / point_design_emit (world draws)
inline void PointRingEmit(ImDrawList* dl, int style, ImVec2 c, float r, ImU32 color, float thickness)
{
    PointRing(dl, style, c, r, color, PointsThickness(thickness), 32);
}
inline void PointDesignEmit(ImDrawList* dl, int style, ImVec2 c, float half, float dirX, float dirY, ImU32 color)
{
    PointDesignDraw(dl, PointStyleDesign(style), c, half, dirX, dirY, color, PointsThickness(1.f), 32);
}
// botox point_label_emit: verdana 11 white under the point
inline void PointLabelEmit(ImDrawList* dl, ImFont* font, int style, ImVec2 c, float radius, float alpha)
{
    if (!g_pointsLabels || !font) return;
    const char* label = PointStyleGet(style).label;
    const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, label);
    dl->AddText(font, font->FontSize, ImVec2(std::floor(c.x - size.x * 0.5f), c.y + radius + 3.f),
                IM_COL32(255, 255, 255, (int)(255.f * std::clamp(alpha, 0.f, 1.f))), label);
}

// botox point_style_preview: menu preview into the current window's draw list
inline void PointStylePreview(int style, float x, float y, float radius)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(x, y);
    const float op = std::clamp(g_pointsOpacity, 0.f, 1.f);
    const float fill = style == PS_STYLE_CALC ? 1.f : std::clamp(g_pointsFill, 0.f, 1.f);
    dl->AddCircleFilled(c, radius, IM_COL32(0, 0, 0, (int)(255.f * fill * op)), 64);
    const ImU32 color = PointStyleU32(style, 1.f);
    const float t = PointsThickness(2.f);
    PointRing(dl, style, c, radius, color, t, 64);
    const bool up = style != PS_STYLE_PS;
    PointDesignDraw(dl, PointStyleDesign(style), c, PointDesignHalf(radius), up ? 0.f : 1.f, up ? -1.f : 0.f,
                    PointStyleU32(style, 200.f / 255.f), t, 64);
    if (style == PS_STYLE_CALC) {
        const ImVec2 s = ImGui::CalcTextSize("1");
        dl->AddText(ImVec2(std::floor(x - s.x * 0.5f), std::floor(y - s.y * 0.5f)), color, "1");
    }
}
