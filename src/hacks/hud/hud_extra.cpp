#include "hud_extra.h"
#include "hud_snap.h"
#include "hud_math.h"
#include "../menu/fonts.h"
#include "../../globals/globals.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================================================
// botox indicators.cpp aimbot_fov_circle + net_graph, drawn into the background list. The
// game-thread inputs (radius, net channel numbers) arrive through hud_snap.h.
// ============================================================================================

// ---- fonts ---------------------------------------------------------------------------------
static ImFont* s_fontNg = nullptr;
static uint32_t s_ngApplied = 0;
static FontSettle s_ngSettle;

static uint32_t NgSig() { return FontSig(g_hudNgFontName, g_hudNgFontSize, g_hudNgFontFlags, 0); }

// render.cpp TestBuildFont: a face FreeType chokes on would take the whole atlas down with it
static bool NgTestBuild(const std::string& path, float px, int flags)
{
    ImFontAtlas test;
    ImFontConfig cfg;
    cfg.FontBuilderFlags = flags;
    ImFont* f = test.AddFontFromFileTTF(path.c_str(), px, &cfg, test.GetGlyphRangesDefault());
    return f && test.Build() && f->FindGlyphNoFallback('0') && f->FindGlyphNoFallback('A');
}

void HudFonts_Build()
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    s_ngApplied = NgSig();

    // botox render_font_page: size 0 / unloadable = the fallback face (legendware lucida 10)
    s_fontNg = nullptr;
    if (g_hudNgFontSize > 0)
    {
        const float px = (float)std::clamp(g_hudNgFontSize, 6, 100);
        const int flags = g_hudNgFontFlags & 0xFF;   // FontFlagBit i == builder flag 1 << i
        const std::string path = Fonts_FindPath(g_hudNgFontName);
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES && NgTestBuild(path, px, flags))
        {
            ImFontConfig c;
            c.FontBuilderFlags = flags;
            s_fontNg = atlas->AddFontFromFileTTF(path.c_str(), px, &c, atlas->GetGlyphRangesDefault());
        }
        else
            DebugLog("hud: net graph font '%s' unusable (%s) -- lucida 10", g_hudNgFontName, path.c_str());
    }
}

bool HudFonts_Due() { return s_ngSettle.Due(NgSig(), s_ngApplied, GetTickCount64()); }

void HudFonts_ResetNetGraph()
{
    strcpy_s(g_hudNgFontName, "lucon");
    g_hudNgFontSize = 14;
    g_hudNgFontFlags = 0;
}

static ImU32 Col(const float c[4], float a = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * a)); }

// ---- aimbot fov circle (botox circle_draw_object_t: segments clamp(r/2, 32, 128)) -----------
static void DrawFovCircle(const HudSnap& s, ImDrawList* dl, const ImVec2& ds)
{
    if (!g_hudFovCircle || s.fovRadius < 1.f || s.srcH <= 0.f) return;
    const float r = s.fovRadius * ds.y / s.srcH;   // engine px -> imgui px
    const int seg = std::clamp((int)(r * 0.5f), 32, 128);
    dl->AddCircle(ImVec2(ds.x * 0.5f, ds.y * 0.5f), r, Col(g_hudFovCircleColor), seg, std::max(g_hudFovCircleThick, 0.5f));
}

// ---- custom net graph (botox n_indicators::impl_t::net_graph, CNetGraphPanel row layout) -----
namespace
{
    enum { NG_FPS, NG_FRAMEVAR, NG_PING, NG_UP, NG_LOSS, NG_CHOKE, NG_CMD, NG_TICK, NG_SV, NG_SVVAR, NG_SERVER };
    struct Piece { const char* gap; std::string text; const float* color; };
    struct Row { std::vector<Piece> left; Piece right{ "", "", nullptr }; };
}

static void DrawNetGraph(const HudSnap& s, ImDrawList* dl, const ImVec2& ds)
{
    if (!g_hudNg || s.netGraph < 1) return;   // botox: shown with net_graph 1

    ImFont* font = s_fontNg ? s_fontNg : g_fontLegendwareLucida10;
    if (!font) return;
    const float fs = font->FontSize;

    const float tickRate = s.tickInterval > 0.f ? std::max(1.f / s.tickInterval, 0.000001f) : 0.000001f;
    const bool warn = g_hudNgWarn;
    const float* normal = g_hudNgColor;

    const float* const levels[4] = { normal, g_hudNgWarn1, g_hudNgWarn2, g_hudNgWarn3 };
    auto variance = [&](float value, float base, float v1, float v2, float v3) -> const float* {
        return warn ? levels[hudm::NgVariance(value, base, v1, v2, v3)] : normal;
    };
    auto rateColor = [&](float rate) -> const float* { return warn ? levels[hudm::NgRateLevel(rate, tickRate)] : normal; };
    const float* interpColor = normal;
    if (warn && s.interp > 0.001f)
    {
        if (s.updateRate > 0.f && s.interp < 2.f / s.updateRate) interpColor = variance(s.interp, 2.f / s.updateRate, 0.f, 0.2f, 0.5f);
        if (s.interp < s.tickInterval) interpColor = g_hudNgWarn3;
    }
    const float* serverColor = s.svTime > 1.f / tickRate + 0.0001f ? variance(s.svTime, 1.f / tickRate, 0.25f, 0.5f, 0.75f) : normal;

    auto shown = [&](int item) { return (g_hudNgItems >> item) & 1; };
    char sz[128];
    auto fmt = [&](const char* f, auto... a) { snprintf(sz, sizeof(sz), f, a...); return std::string(sz); };

    std::vector<Row> rows;
    {
        Row& r = rows.emplace_back();
        if (shown(NG_FPS))      r.left.push_back({ "", fmt("fps: %5i", (int)s.fps), normal });
        if (shown(NG_FRAMEVAR)) r.left.push_back({ "  ", fmt("var: %4.1f ms", s.frameVarMs), normal });
        if (shown(NG_PING))     r.left.push_back({ "  ", fmt("ping: %i ms", (int)(s.latency * 1000.f)), normal });
        if (g_hudNgLerp && !g_hudNgInOut) r.left.push_back({ "  ", fmt("lerp: %4.1fms", s.interp * 1000.f), interpColor });
        if (shown(NG_UP))       r.right = { "", fmt("up:%5.1f/s", s.updateRate), rateColor(s.updateRate) };
    }
    if (g_hudNgInOut)
    {
        Row& in = rows.emplace_back();
        in.left.push_back({ "", fmt("in:  %5i  %5.2fk/s", s.inBytes, s.inData), normal });
        if (g_hudNgLerp) in.left.push_back({ "  ", fmt("lerp: %4.1fms", s.interp * 1000.f), interpColor });
        in.right = { "", fmt("%3.1f/s", s.inPackets), normal };

        Row& out = rows.emplace_back();
        out.left.push_back({ "", fmt("out: %5i  %5.2fk/s", s.outBytes, s.outData), normal });
        out.right = { "", fmt("%3.1f/s", s.outPackets), normal };
    }
    {
        Row& r = rows.emplace_back();
        if (shown(NG_LOSS))  r.left.push_back({ "", fmt("loss: %3i%%", (int)(s.loss * 100.f)), normal });
        if (shown(NG_CHOKE)) r.left.push_back({ "  ", fmt("choke: %2i%%", (int)(s.choke * 100.f)), normal });
        if (shown(NG_CMD))   r.right = { "", fmt("cmd:%5.1f/s", s.cmdRate), rateColor(s.cmdRate) };
    }
    {
        Row& r = rows.emplace_back();
        if (shown(NG_TICK))   r.left.push_back({ "", fmt("tick:%5.1f", tickRate), normal });
        if (shown(NG_SV))     r.left.push_back({ "  ", fmt("sv:%5.1f +-%4.1f ms", s.svTime * 1000.f, s.svDev * 1000.f), serverColor });
        // TF2's GetRemoteFramerate has no start-time deviation (CS:GO's third out param): the
        // frame time deviation is the closest number the engine has
        if (shown(NG_SVVAR))  r.left.push_back({ "   ", fmt("var: %6.3f ms", s.svDev * 1000.f), serverColor });
        if (shown(NG_SERVER)) r.right = { "", s.serverType, normal };
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return r.left.empty() && r.right.text.empty(); }), rows.end());
    if (rows.empty()) return;

    auto textW = [&](const std::string& t) { return font->CalcTextSizeA(fs, FLT_MAX, 0.f, t.c_str()).x; };

    // CNetGraphPanel::OnFontChanged width sample
    float w = textW("fps:  435  var: 4.091 ms  ping: 533 ms lerp 112.3 ms   0/0 offline");
    for (const Row& r : rows)
    {
        std::string line;
        for (const Piece& p : r.left) line += (line.empty() ? "" : p.gap) + p.text;
        w = std::max(w, textW(line) + (r.right.text.empty() ? 0.f : textW("  " + r.right.text)));
    }
    const int sw = (int)ds.x, sh = (int)ds.y;
    w = std::min(w, (float)(sw - 10));

    // CNetGraphPanel::GraphGetXY + ComputeNetgraphHeight, used when x / y = -1
    const int x = g_hudNgX >= 0 ? g_hudNgX : hudm::NgGraphX(s.netGraphPos, sw, (int)w);
    const int y = g_hudNgY >= 0 ? g_hudNgY : hudm::NgGraphY(sh, g_hudNgInOut ? 6 : 4, (int)fs, s.netGraphHeight);

    if (g_hudNgBg)
        dl->AddRectFilled(ImVec2(x - 4.f, y - 2.f), ImVec2(x + w + 4.f, y + fs * rows.size() + 2.f), Col(g_hudNgBgColor));

    const ImU32 outline = Col(g_hudNgOutlineColor);
    auto put = [&](float px, float py, const Piece& p) {
        const ImVec2 at(std::round(px), std::round(py));
        if (g_hudNgTextStyle == 1)
            dl->AddText(font, fs, ImVec2(at.x + 1.f, at.y + 1.f), outline, p.text.c_str());
        else if (g_hudNgTextStyle == 2)
        {
            dl->AddText(font, fs, ImVec2(at.x + 1.f, at.y - 1.f), outline, p.text.c_str());
            dl->AddText(font, fs, ImVec2(at.x - 1.f, at.y + 1.f), outline, p.text.c_str());
        }
        dl->AddText(font, fs, at, Col(p.color), p.text.c_str());
    };

    float rowY = (float)y;
    for (const Row& r : rows)
    {
        float px = (float)x;
        for (size_t i = 0; i < r.left.size(); ++i)
        {
            if (i) px += textW(r.left[i].gap);
            put(px, rowY, r.left[i]);
            px += textW(r.left[i].text);
        }
        if (!r.right.text.empty()) put(x + w - textW(r.right.text) - 1.f, rowY, r.right);
        rowY += fs;
    }
}

void RenderHudExtras()
{
    if (!g_hudFovCircle && !g_hudNg) return;
    HudSnap s;
    if (!HudSnap_Read(s, GetTickCount64())) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    DrawFovCircle(s, dl, ds);
    DrawNetGraph(s, dl, ds);
}
