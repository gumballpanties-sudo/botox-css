#include "watermark_styles.h"
#include "watermark.h"            // g_wmBox
#include "../misc/unitE_logic.h"
#include "../movement/prediction.h"   // g_globalVars (interval_per_tick), MV_GetIndicators (local speed)
#include "avatar_cache.h"
#include "avatar_pure.h"   // n_avatar::FriendIdToSteam64
#include "gif_tex.h"
#include "wm_pure.h"
#include "../screen/blur_rect.h"
#include "../../dependencies/data/airflow_data.h"   // airflow watermark logo icon
#include "../../dependencies/imgui/imgui_internal.h"   // ImDrawListSharedData (evolve shadow corners), IM_PI
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

int  Aim_ChokedCommands();                    // aim/wm_aim.cpp: CClientState::chokedcommands
bool Aim_NetServer(char* out, size_t size);   // aim/wm_aim.cpp: INetChannelInfo::GetAddress

// ==== botox misc.cpp watermark_clock / watermark_label / watermark_brand / watermark_info_parts ====
// One frame's engine state, read once at the top of Watermark_Draw (render thread).
struct WmFrame
{
    bool  inGame   = false;
    float interval = 0.f;   // gpGlobals->interval_per_tick, 0 = unknown
    int   speed    = 0;     // local 2d speed (movement thread snapshot), 0 when stale
};
static WmFrame s_f;

static std::string Clock(const char* fmt = "%H:%M")
{
    time_t raw = time(nullptr); struct tm lt; localtime_s(&lt, &raw);
    char buf[32]; strftime(buf, sizeof(buf), fmt, &lt);
    return buf;
}
static int AvgFps() { return (int)(g_fps.load() + 0.5f); }   // botox g_utilities.get_average_fps
static std::string Brand() { return g_wmLabel[0] ? g_wmLabel : "botox"; }
static std::string User() { return g_wmUserName; }
static bool TickLive() { return s_f.inGame && s_f.interval > 0.f; }
static int Tickrate(bool round = true) { return TickLive() ? (int)(1.f / s_f.interval + (round ? 0.5f : 0.f)) : 0; }
// botox reads the net channel's own latency per style; this DLL keeps one ping (visuals.cpp
// UpdateNetStats), every style shows that one.
static int Ping() { return g_conn.load() ? g_ping.load() : 0; }

static std::string Label()
{
    switch (g_wmLabelType) {
    case 0:  return Clock();
    case 1:  return std::to_string(AvgFps()) + " fps";
    case 2:  return User();
    default: return {};
    }
}

static std::vector<std::string> InfoParts()
{
    std::vector<std::string> parts;
    if (g_wmUser) parts.push_back(" | " + User());
    if (g_wmTickrate && TickLive()) parts.push_back(" | " + std::to_string(Tickrate()) + " tick");   // in game only
    if (g_wmShowFPS) parts.push_back(" | " + std::to_string(AvgFps()) + " fps");
    if (g_wmShowTime) parts.push_back(" | " + Clock());
    return parts;
}

// botox legendware: net_channel->get_address(), "loopback" -> "Local server" (aim/wm_aim.cpp, SDK classes)
static bool NetServer(char* out, size_t n) { return Aim_NetServer(out, n); }

// botox g_trick_time window (points_on_trick stamps it, here Points_OnTrick -> g_wmTrickMs)
static bool TrickWithin(float seconds)
{
    const unsigned long long at = g_wmTrickMs.load();
    return at && (float)(GetTickCount64() - at) < seconds * 1000.f;
}

static ImU32 PackColor(const float c[4], float alphaMul = 1.f)
{
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), (int)(c[3] * alphaMul * 255.f));
}
// botox accent_u32(alpha): the menu accent (ImGuiCol_Accent) at a fixed alpha
static ImU32 AccentA(int a)
{
    const ImVec4 v = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
    return IM_COL32((int)(v.x * 255.f), (int)(v.y * 255.f), (int)(v.z * 255.f), a);
}
static ImU32 Accent() { return AccentA(255); }
static ImU32 AccentF(float a) { const ImVec4 v = ImGui::GetStyleColorVec4(ImGuiCol_Accent); return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x, v.y, v.z, a)); }
static float TextW(ImFont* f, float px, const std::string& s) { return f->CalcTextSizeA(px, FLT_MAX, 0.f, s.c_str()).x; }
static float ImguiW(ImFont* f, const std::string& s) { return floorf(TextW(f, f->FontSize, s) + 0.99999f); }   // botox imgui_text_width
// botox vgui_text_width / vgui_shadow_text: floored width, floored pos, shadow = black at the colour's alpha
static float VguiW(ImFont* f, const std::string& s) { return floorf(TextW(f, f->FontSize, s)); }
static void VguiShadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 color, const char* s)
{
    const ImVec2 at(floorf(pos.x), floorf(pos.y));
    dl->AddText(f, f->FontSize, ImVec2(at.x + 1.f, at.y + 1.f), color & IM_COL32_A_MASK, s);
    dl->AddText(f, f->FontSize, at, color, s);
}
static void SetBox(float x0, float y0, float x1, float y1) { g_wmBox = ImVec4(x0, y0, x1, y1); }
static ImFont* Or(ImFont* f) { return f ? f : ImGui::GetFont(); }

using n_wm::Utf8Truncate;   // wm_pure.h, botox spectator_list.cpp utf8_truncate

// style 0: default -- misc.cpp draw_watermark() body. verdana bd 11 brand (white) + verdana 11
// info parts in the menu accent, (25) box + (50) outline, fixed top-right 8px in.
static void DrawDefault(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* bold = Or(g_menuBold), * slim = Or(g_menuReg);

    struct Seg { std::string text; ImU32 color; ImFont* font; };
    std::vector<Seg> segs;
    segs.push_back({ Brand(), IM_COL32(255, 255, 255, 255), bold });
    for (auto& part : InfoParts()) segs.push_back({ std::move(part), Accent(), slim });

    float contentW = 0.f, contentH = 0.f;
    for (auto& s : segs) {
        const ImVec2 sz = s.font->CalcTextSizeA(s.font->FontSize, FLT_MAX, 0.f, s.text.c_str());
        contentW += sz.x; contentH = (std::max)(contentH, sz.y);
    }

    constexpr float padX = 14.f, height = 25.f, minW = 95.f;
    const float rounding = ImGui::GetStyle().WindowRounding;
    const float width = segs.size() > 1 ? (std::max)(contentW + padX * 2.f, minW) : contentW + padX * 2.f;
    const float x = io.DisplaySize.x - width - 8.f, y = 8.f;
    SetBox(x, y, x + width, y + height);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(25, 25, 25, 255), rounding);
    dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(x + width - 1.f, y + height - 1.f), IM_COL32(50, 50, 50, 255), rounding);

    float tx = x + (width - contentW) * 0.5f;
    const float ty = y + (height - contentH) * 0.5f;
    for (auto& s : segs) {
        dl->AddText(s.font, s.font->FontSize, ImVec2(tx, ty), s.color, s.text.c_str());
        tx += s.font->CalcTextSizeA(s.font->FontSize, FLT_MAX, 0.f, s.text.c_str()).x;
    }
}

// style 1: kamibebra -- misc.cpp draw_watermark_kamibebra(). rubik 13 (or rubik medium 13 = "bold
// font"), custom text, typing / deleting animation with holds, user / time / fps flags, width eased
// at 12/s unless "mikudere type" pins it to 95, custom rounding, outline (picked or rgb cycle).
static ImU32 s_kamiRgbLast = IM_COL32(255, 255, 255, 255);   // botox kami_rgb_last, the media frame reuses it
static ImU32 KamiRgb(float dt)
{
    static float phase = 0.f;
    float speed = g_wmKamiRgbSpeed;
    if (speed <= 0.f) speed = 0.5f;
    phase += dt * speed * 0.2f;
    if (phase > 1.f) phase -= floorf(phase);

    const float h = phase * 6.f;
    const int i = (int)h;
    const float f = h - (float)i, q = 1.f - f;
    constexpr float s = 0.6f, v = 0.9f;
    float r = 0.f, g = 0.f, b = 0.f;
    switch (i % 6) {
    case 0: r = v; g = v * (1.f - s * (1.f - f)); b = v * (1.f - s); break;
    case 1: r = v * (1.f - s * q); g = v; b = v * (1.f - s); break;
    case 2: r = v * (1.f - s); g = v; b = v * (1.f - s * (1.f - f)); break;
    case 3: r = v * (1.f - s); g = v * (1.f - s * q); b = v; break;
    case 4: r = v * (1.f - s * (1.f - f)); g = v * (1.f - s); b = v; break;
    case 5: r = v; g = v * (1.f - s); b = v * (1.f - s * q); break;
    }
    return s_kamiRgbLast = IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), 255);
}
static float KamiRounding() { return g_wmKamiRounding ? g_wmKamiRoundingValue : 4.f; }
static void DrawKamibebra(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_wmKamiBold ? g_fontKamibebraBold13 : g_fontKamibebra13);
    const float dt = io.DeltaTime;

    const bool custom = g_wmKamiCustomText;
    const std::string mainText = custom ? std::string(g_wmKamiText) : Brand();

    static std::string lastMain;
    static size_t typingIndex = 0;
    static float typingTimer = 0.f, pauseTimer = 0.f;
    static bool deleting = false, inPause = false, pauseAfterTyping = false;

    if (mainText != lastMain) { typingIndex = 0; deleting = false; typingTimer = 0.f; lastMain = mainText; }

    std::string display = mainText;
    if (g_wmKamiTyping) {
        if (inPause) {
            pauseTimer += dt;
            if (pauseTimer >= (pauseAfterTyping ? g_wmKamiTypingHold : g_wmKamiDeletingHold)) {
                inPause = false; pauseTimer = 0.f; deleting = pauseAfterTyping;
            }
        } else {
            typingTimer += dt;
            const int cps = deleting ? g_wmKamiDeletingSpeed : g_wmKamiTypingSpeed;
            if (typingTimer >= 1.f / (float)(std::max)(cps, 1)) {
                typingTimer = 0.f;
                if (!deleting && typingIndex < mainText.length()) ++typingIndex;
                else if (deleting && typingIndex > 0) --typingIndex;
                else { inPause = true; pauseTimer = 0.f; pauseAfterTyping = !deleting; }
            }
        }
        display = mainText.substr(0, (std::min)(typingIndex, mainText.length()));
    } else { typingIndex = 0; deleting = false; }

    const int flags = g_wmKamiShowFlags;
    const bool showUser = (flags & 1) != 0, showTime = (flags & 2) != 0, showFps = (flags & 4) != 0;

    static int cachedFps = 0;
    static float fpsTimer = 0.f;
    if (showFps) {
        fpsTimer += dt;
        if (fpsTimer >= 0.5f) { cachedFps = dt > 0.f ? (int)(1.f / dt) : 0; fpsTimer = 0.f; }
    }

    const std::string timeText = showTime ? Clock() : std::string{};
    const std::string fpsText = std::to_string(cachedFps);
    const std::string user = User();

    std::string full = display;
    if (showUser) full += " | " + user;
    if (showTime) full += " | " + timeText;
    if (showFps)  full += " | " + fpsText + " fps";

    const float fs = font->FontSize;
    const ImVec2 fullSize = font->CalcTextSizeA(fs, FLT_MAX, 0.f, full.c_str());
    constexpr float minWidth = 95.f, height = 28.f, padX = 14.f;

    float width = minWidth;
    if (!g_wmKamiMikudere && (custom || (flags & 7) != 0)) {
        const float target = (std::max)(fullSize.x + padX * 2.f, minWidth);
        static float current = target;
        current += (target - current) * 12.f * dt;
        width = current;
    }

    const float x = io.DisplaySize.x - width - 8.f, y = 8.f;
    SetBox(x, y, x + width, y + height);

    const float rounding = KamiRounding();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(16, 16, 16, 255), rounding);

    const float* stroke = g_wmKamiOutlineColor;
    if (g_wmKamiOutline) {
        const ImU32 outline = g_wmKamiRgb ? KamiRgb(dt) : PackColor(stroke);
        dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), outline, rounding, ImDrawFlags_RoundCornersAll, 1.f);
    }

    // "text color like stroke" takes the picked colour, never the rgb one (donor)
    const ImU32 main = g_wmKamiTextLikeStroke
        ? IM_COL32((int)(stroke[0] * 255.f), (int)(stroke[1] * 255.f), (int)(stroke[2] * 255.f), 255) : IM_COL32(255, 255, 255, 255);
    constexpr ImU32 info = IM_COL32(146, 144, 145, 255);

    float tx = x + (width - fullSize.x) * 0.5f;
    const float ty = y + (height - fullSize.y) * 0.5f - 1.f;
    const auto put = [&](const std::string& s, ImU32 c) { dl->AddText(font, fs, ImVec2(tx, ty), c, s.c_str()); tx += TextW(font, fs, s); };

    put(display, main);
    if (showUser) { put(" | ", info); put(user, info); }
    if (showTime) { put(" | ", info); put(timeText, info); }
    if (showFps)  { put(" | ", info); put(fpsText, main); put(" fps", info); }
}

// style 2: clarity -- misc.cpp draw_watermark_clarity(). verdana bold 11, brand + info parts,
// white with a 1px black drop shadow, top-right (w - 6 - tw, 5).
static void DrawClarity(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_menuBold);
    std::string text = Brand();
    for (const auto& part : InfoParts()) text += part;

    const float fs = font->FontSize;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text.c_str());
    const ImVec2 pos(io.DisplaySize.x - 6.f - ts.x, 5.f);
    SetBox(pos.x, pos.y, pos.x + ts.x, pos.y + ts.y);

    dl->AddText(font, fs, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(font, fs, pos, IM_COL32(255, 255, 255, 255), text.c_str());
}

// style 3: clarity v2 -- misc.cpp draw_watermark_clarity_v2(). inter semibold 14, three rings,
// icon glyph bled into the left edge, brand(accent) | user | fps slot "fps" with hairline gaps.
// fps = mean frame time over 0.4s windows, slot width eased at 15/s, faux-bold double draw.
static void DrawClarityV2(ImDrawList* dl, ImGuiIO& io)
{
    if (!g_wmEnable) return;   // donor gates itself (draw_watermark dispatches 3 before the m_watermark check)

    ImFont* font = Or(g_fontClarityInter);
    ImFont* iconFont = g_fontClarityIcon ? g_fontClarityIcon : font;

    const auto textSize = [](ImFont* f, const std::string& text) {
        const ImVec2 sz = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, text.c_str());
        return std::pair{ (int)sz.x, (int)sz.y };
    };

    static int fps = 0, frames = 0;
    static float frameSum = 0.f, lastUpdate = 0.f, slotWidth = 0.f;
    frameSum += io.DeltaTime; ++frames;
    if (const float now = (float)ImGui::GetTime(); now - 0.4f >= lastUpdate || now < lastUpdate) {
        fps = frameSum > 0.f ? (int)(1.f / (frameSum / (float)frames)) : 0;
        frames = 0; frameSum = 0.f; lastUpdate = now;
    }

    const ImU32 accent = Accent();
    const std::string fpsText = std::to_string(fps);

    const float slotTarget = (float)((int)fpsText.size() * textSize(font, "9").first + 3);
    if (slotWidth != slotTarget) slotWidth += (slotTarget - slotWidth) * std::clamp(15.f * io.DeltaTime, 0.f, 1.f);

    struct Seg { std::string text; ImU32 color; int width; };   // width -1 = measure, text "" = gap
    const Seg segs[] = {
        { Brand(), accent, -1 },
        { {}, 0, 12 },
        { User(), IM_COL32(255, 255, 255, 150), -1 },
        { {}, 0, 12 },
        { fpsText, IM_COL32(255, 255, 255, 255), (int)slotWidth },
        { "fps", IM_COL32(255, 255, 255, 150), -1 },
    };
    const auto segW = [&](const Seg& s) { return s.width == -1 ? textSize(font, s.text).first : s.width; };

    int textWidth = 0;
    for (const auto& s : segs) textWidth += segW(s);

    constexpr int fontH = 10;
    const int w = textWidth + 10 * 2, h = fontH + 7 * 2;
    const int x = (int)io.DisplaySize.x - 7 - w, y = 7;

    const auto rect = [&](int grow) {
        return std::pair{ ImVec2((float)(x - grow), (float)(y - grow)), ImVec2((float)(x + w + grow), (float)(y + h + grow)) };
    };

    const auto [fillMin, fillMax] = rect(3);
    dl->AddRectFilled(fillMin, fillMax, IM_COL32(25, 25, 25, 255), 5.f);
    SetBox(fillMin.x, fillMin.y, fillMax.x, fillMax.y);

    {
        const auto [iconW, iconH] = textSize(iconFont, "A");
        dl->PushClipRect(fillMin, fillMax, true);
        dl->AddText(iconFont, iconFont->FontSize, ImVec2((float)(x + 15 - iconW / 2), (float)(y + h / 2 - 1 - iconH / 2)),
            (accent & ~IM_COL32_A_MASK) | (35u << IM_COL32_A_SHIFT), "A");
        dl->PopClipRect();
    }

    const ImU32 rings[3] = { IM_COL32(0, 0, 0, 255), IM_COL32(50, 50, 50, 255), IM_COL32(0, 0, 0, 255) };
    for (int grow = 0; grow < 3; ++grow) {
        const auto [rMin, rMax] = rect(grow);
        dl->AddRect(rMin, rMax, rings[grow], 5.f);
    }

    int cursor = x + 10;
    const int mid = y + h / 2;
    for (const auto& s : segs) {
        if (!s.text.empty()) {
            const float ty = (float)(mid - textSize(font, s.text).second / 2);
            const float a = (float)((s.color >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
            const ImU32 passA = (ImU32)((1.f - sqrtf(1.f - a)) * 255.f + 0.5f);
            const ImU32 color = (s.color & ~IM_COL32_A_MASK) | (passA << IM_COL32_A_SHIFT);
            for (const float dx : { 0.f, 0.5f })
                dl->AddText(font, font->FontSize, ImVec2((float)cursor + dx, ty), color, s.text.c_str());
        } else {
            constexpr int half = fontH / 2;
            dl->AddLine(ImVec2((float)(cursor + 6), (float)(mid - half)), ImVec2((float)(cursor + 6), (float)(mid + half)), IM_COL32(50, 50, 50, 255));
        }
        cursor += segW(s);
    }
}

// style 4: delusional -- misc.cpp draw_watermark_delusional(). tahoma 12 mono, "brand | label",
// vertical accent-fade outline (full alpha top -> 0 bottom) + (20) fill, shadowed white text.
static void DrawDelusional(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontTahoma12);
    const std::string label = Label();
    const std::string main = label.empty() ? Brand() : Brand() + " | " + label;

    const ImVec2 ts = font->CalcTextSizeA(12.f, FLT_MAX, 0.f, main.c_str());
    const ImVec2 pad(7.f, 7.f), margin(4.f, 3.f);
    const float w = io.DisplaySize.x;

    const ImVec2 bmin(w - ts.x - pad.x - margin.x * 2.f - 1.f, pad.y - 1.f);
    const ImVec2 bmax(w - pad.x + 1.f, ts.y + pad.y + margin.y * 2.f + 1.f);
    SetBox(bmin.x, bmin.y, bmax.x, bmax.y);

    dl->AddRectFilledMultiColor(bmin, bmax, Accent(), Accent(), AccentA(0), AccentA(0));
    dl->AddRectFilled(ImVec2(w - ts.x - pad.x - margin.x * 2.f, pad.y), ImVec2(w - pad.x, ts.y + pad.y + margin.y * 2.f), IM_COL32(20, 20, 20, 255));

    const ImVec2 tp(w - ts.x - pad.x - margin.x, pad.y + margin.y);
    dl->AddText(font, 12.f, ImVec2(tp.x + 1.f, tp.y), IM_COL32(0, 0, 0, 255), main.c_str());
    dl->AddText(font, 12.f, tp, IM_COL32(255, 255, 255, 255), main.c_str());
}

// style 5: interwebz -- misc.cpp draw_watermark_interwebz(). calibri 16 mono, uppercase brand +
// game tag + label, 8-way black outline, white, centred on (250, 36) of botox's 640x480 stretch
// block. CS:S has no such canvas (render.cpp draws in io.DisplaySize px), so the point is scaled
// (n_unitE::ScaleVirtual640x480). "CS:GO" -> "CS:S": the one thing the game forces.
static void DrawInterwebz(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontInterwebzCalibri);
    const float fs = font->FontSize;

    std::string brand = Brand();
    for (char& c : brand) c = (char)toupper((unsigned char)c);
    const std::string label = Label();
    const std::string line = brand + " CS:S" + (label.empty() ? "" : " | " + label);

    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, line.c_str());
    float vx, vy;
    n_unitE::ScaleVirtual640x480(io.DisplaySize.x, io.DisplaySize.y, 250.f, 36.f, vx, vy);
    const ImVec2 pos(vx - floorf(ts.x * 0.5f), vy - roundf(font->Ascent));
    SetBox(pos.x, pos.y, pos.x + ts.x, pos.y + ts.y);

    constexpr ImVec2 off[8] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
    for (auto& o : off) dl->AddText(font, fs, ImVec2(pos.x + o.x, pos.y + o.y), IM_COL32(0, 0, 0, 255), line.c_str());
    dl->AddText(font, fs, pos, IM_COL32(255, 255, 255, 255), line.c_str());
}

// style 7: onetap -- misc.cpp draw_watermark_onetap(). tahoma 12 mono, fixed 280x20 translucent
// pink bar, "brand | label", in game + " | rate: N | choke: N". botox's " | shots: N" is
// m_iShotsFired -- CS:S's CCSPlayer carries this netvar for real (cs_weapon_parse.cpp:282
// context), unlike TF2, but not wired here: needs a verified offset from the SDK unit
// (aim/css.h). See report NEEDS FROM OTHERS.
static void DrawOnetap(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontTahoma12);
    std::string text = Brand();
    if (const std::string label = Label(); !label.empty()) text += " | " + label;
    if (s_f.inGame && GetLocalPlayerIndex() > 0)
        text += " | rate: " + std::to_string(Tickrate()) + " | choke: " + std::to_string(Aim_ChokedCommands());

    const float w = io.DisplaySize.x;
    SetBox(w - 280.f, 10.f, w - 10.f, 30.f);
    dl->AddRectFilled(ImVec2(w - 280.f, 10.f), ImVec2(w - 10.f, 30.f), IM_COL32(185, 90, 105, 160));
    dl->AddText(font, font->FontSize, ImVec2(w - 275.f, 14.f), IM_COL32(255, 255, 255, 150), text.c_str());
}

// below: styles 6,8-21 -- donor bodies live in botox's spectator_list.cpp, not misc.cpp.

// style 6: havoc-1337 -- spectator_list.cpp draw_watermark_havoc(). tahoma 13 mono, nested grey
// shells, crossing diagonals after the brand: accent = "blue color", accent2 = menu accent.
static void HavocBox(ImDrawList* dl, ImVec2 pos, ImVec2 size, float alpha)
{
    auto shell = [&](float inset, int shade) {
        dl->AddRectFilled(ImVec2(pos.x + inset, pos.y + inset), ImVec2(pos.x + size.x - inset, pos.y + size.y - inset),
            IM_COL32(shade, shade, shade, (int)(255.f * alpha)), 5.f);
    };
    shell(-1.f, 10); shell(0.f, 55); shell(1.f, 10); shell(2.f, 20);
}
static void HavocLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color)
{
    dl->AddLine(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x + 0.5f, b.y + 0.5f), color);
}
static ImU32 HavocAccent() { return PackColor(g_wmHavocColor); }
static ImU32 HavocAccent2() { return Accent(); }
static void DrawHavoc(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontHavocTahoma13);
    const std::string name = Brand() + " ", label = Label();
    const float nameW = VguiW(font, name);

    const ImVec2 size(nameW + VguiW(font, label) + 37.f, 20.f);
    const ImVec2 pos(io.DisplaySize.x - size.x - 20.f, 15.f);
    SetBox(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

    HavocBox(dl, pos, size, 1.f);

    const ImU32 accent = HavocAccent(), accent2 = HavocAccent2();
    for (int i = 0; i < 3; ++i) {
        const float x = pos.x + nameW + 8.f + (float)i;
        HavocLine(dl, ImVec2(x + 19.f, pos.y + 2.f), ImVec2(x + 4.f, pos.y + 17.f), accent2);
        HavocLine(dl, ImVec2(x + 12.f, pos.y + 2.f), ImVec2(x - 3.f, pos.y + 17.f), accent);
    }

    VguiShadowText(dl, font, ImVec2(pos.x + 6.f, pos.y + 3.f), accent, name.c_str());
    VguiShadowText(dl, font, ImVec2(pos.x + nameW + 33.f, pos.y + 3.f), accent2, label.c_str());
}

// style 9: ev0lve -- spectator_list.cpp draw_watermark_evolve(). segoe ui 16 (brand bold), soft
// drop shadow at "alpha override" * 0.25, dark header with an accent underline, label in (220).
static void EvolveShadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float radius, float alpha)
{
    const ImU32 dark = IM_COL32(0, 0, 0, (int)(255.f * alpha)), clear = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilled(min, max, dark);
    dl->AddRectFilledMultiColor(ImVec2(min.x - radius, min.y), ImVec2(min.x, max.y), clear, dark, dark, clear);
    dl->AddRectFilledMultiColor(ImVec2(max.x, min.y), ImVec2(max.x + radius, max.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(min.x, min.y - radius), ImVec2(max.x, min.y), clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(ImVec2(min.x, max.y), ImVec2(max.x, max.y + radius), dark, dark, clear, clear);

    // donor: rounded shadow corners, 8 fading triangles each
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    const auto corner = [&](ImVec2 center, float from) {
        constexpr int segments = 8;
        dl->PrimReserve(segments * 3, segments * 3);
        for (int i = 0; i < segments; ++i) {
            const float a0 = from + IM_PI * 0.5f * (float)i / segments, a1 = from + IM_PI * 0.5f * (float)(i + 1) / segments;
            dl->PrimVtx(center, uv, dark);
            dl->PrimVtx(ImVec2(center.x + cosf(a0) * radius, center.y + sinf(a0) * radius), uv, clear);
            dl->PrimVtx(ImVec2(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius), uv, clear);
        }
    };
    corner(min, IM_PI); corner(ImVec2(max.x, min.y), IM_PI * 1.5f); corner(max, 0.f); corner(ImVec2(min.x, max.y), IM_PI * 0.5f);
}
static void EvolveHeader(ImDrawList* dl, ImVec2 min, float width, float alpha)
{
    dl->AddRectFilled(min, ImVec2(min.x + width, min.y + 24.f), IM_COL32(29, 29, 29, (int)(255.f * alpha)), 4.f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(min.x, min.y + 24.f), ImVec2(min.x + width, min.y + 25.f), AccentF(alpha));
}
static float EvolveMaxAlpha() { return std::clamp(g_wmEvolveAlpha / 100.f, 0.f, 1.f); }
static void DrawEvolve(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontEvolve16);
    ImFont* bold = g_fontEvolveBold16 ? g_fontEvolveBold16 : font;

    const std::string brand = Brand(), label = Label();
    const std::string rest = label.empty() ? std::string{} : " | " + label;

    const float brandW = ImguiW(bold, brand);
    const float width = floorf(brandW + ImguiW(font, rest) + 20.f);

    const ImVec2 min(io.DisplaySize.x - width - 10.f, 10.f), max(min.x + width, min.y + 25.f);
    SetBox(min.x, min.y, max.x, max.y);

    EvolveShadow(dl, min, max, 12.f, EvolveMaxAlpha() * 0.25f);
    EvolveHeader(dl, min, width, 1.f);

    const float ty = floorf(min.y + (24.f - bold->FontSize) * 0.5f);
    dl->AddText(bold, bold->FontSize, ImVec2(min.x + 10.f, ty), Accent(), brand.c_str());
    dl->AddText(font, font->FontSize, ImVec2(min.x + 10.f + brandW, ty), IM_COL32(220, 220, 220, 255), rest.c_str());
}

// style 10: legendware -- spectator_list.cpp draw_watermark_legendware(). verdana 12 mono, accent
// gradient hairlines top+bottom, (10,150) fill, vgui-shadowed text at 220. "brand | user | HH:MM:SS",
// in game "brand | user | server | N ms | N tick | HH:MM:SS".
static void LegendwareGradient(ImDrawList* dl, ImVec2 pos, ImVec2 size, const int rgb[3], int left, int right)
{
    auto col = [&](int a) { return IM_COL32(rgb[0], rgb[1], rgb[2], a); };
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilledMultiColor(pos, end, col(left), col(0), col(0), col(left));
    dl->AddRectFilledMultiColor(pos, end, col(0), col(right), col(right), col(0));
}
static void AccentRgb(int rgb[3])
{
    const ImVec4 v = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
    rgb[0] = (int)(v.x * 255.f); rgb[1] = (int)(v.y * 255.f); rgb[2] = (int)(v.z * 255.f);
}
static void DrawLegendware(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontLegendwareVerdana12);
    const std::string clock = Clock("%H:%M:%S"), brand = Brand(), user = User();

    std::string text = brand + " | " + user + " | " + clock;
    char server[128];
    if (s_f.inGame && NetServer(server, sizeof(server)))
        text = brand + " | " + user + " | " + server + " | " + std::to_string(Ping()) + " ms | " + std::to_string(Tickrate(false)) + " tick | " + clock;

    const int boxW = (int)VguiW(font, text) + 10;
    const int half = boxW / 2;
    const float x = (float)((int)io.DisplaySize.x - 10 - boxW);
    SetBox(x, 10.f, x + (float)boxW, 30.f);

    int rgb[3]; AccentRgb(rgb);
    for (float lineY : { 10.f, 29.f }) {
        LegendwareGradient(dl, ImVec2(x, lineY), ImVec2((float)half, 1.f), rgb, 170, 240);
        LegendwareGradient(dl, ImVec2(x + (float)half, lineY), ImVec2((float)half, 1.f), rgb, 240, 170);
    }

    dl->AddRectFilled(ImVec2(x, 11.f), ImVec2(x + (float)boxW, 29.f), IM_COL32(10, 10, 10, 150));
    VguiShadowText(dl, font, ImVec2(x + 5.f, 13.f), IM_COL32(255, 255, 255, 220), text.c_str());
}

// style 11: interium -- spectator_list.cpp draw_watermark_interium(). droid sans bold 24 drawn at 16,
// 4-corner hue cycle border, "background color" fill, shadowed white "brand | label", top-left.
static float s_interiumHues[4] = { 0.f, 0.25f, 0.5f, 0.75f };
static ImU32 InteriumHsv(float hue, int alpha)
{
    float r = 0.f, g = 0.f, b = 0.f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.f, 0.7f, r, g, b);
    return IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), alpha);
}
static void DrawInterium(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontInteriumDroid24);
    const float step = io.DeltaTime * 0.25f;   // donor: gpGlobals->frametime * 0.25
    for (float& hue : s_interiumHues) { hue += step; if (hue > 1.f) hue -= 1.f; }
    const float* h = s_interiumHues;

    const std::string label = Label();
    const std::string text = label.empty() ? Brand() : Brand() + " | " + label;

    const ImVec2 ts = font->CalcTextSizeA(16.f, FLT_MAX, 0.f, text.c_str());
    constexpr float x = 5.f, y = 5.f;
    const float right = x + ts.x + 10.f, bottom = y + ts.y + 8.f;
    SetBox(x - 1.f, y - 1.f, right + 1.f, bottom + 1.f);

    const auto trunc = [](float v) { return (float)(int)v; };
    dl->AddRectFilledMultiColor(ImVec2(x - 1.f, y - 1.f), ImVec2(trunc(right + 1.f), trunc(bottom + 1.f)),
        InteriumHsv(h[0], 100), InteriumHsv(h[1], 100), InteriumHsv(h[2], 100), InteriumHsv(h[3], 100));
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(trunc(right), trunc(bottom)),
        InteriumHsv(h[0], 255), InteriumHsv(h[1], 255), InteriumHsv(h[2], 255), InteriumHsv(h[3], 255));
    dl->AddRectFilled(ImVec2(x + 1.f, y + 1.f), ImVec2(trunc(right - 1.f), trunc(bottom - 1.f)), PackColor(g_wmInteriumBg));

    dl->AddText(font, 16.f, ImVec2(11.f, 10.f), IM_COL32(0, 0, 0, 255), text.c_str());
    dl->AddText(font, 16.f, ImVec2(10.f, 9.f), IM_COL32(255, 255, 255, 255), text.c_str());
}

// style 12: skebob -- spectator_list.cpp draw_watermark_skebob(). nunito 14, double dark frame,
// accent brand | user | N fps (1s refresh of a 0.9/0.1 smoothed frame time). "typing effect":
// brand types in at 150ms/char, holds 2s, deletes at 100ms/char, loops.
static void DrawSkebob(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontSkebobNunito14);

    static float frameTime = 1.f;
    static std::string fps = "0";
    static DWORD nextFps = 0;
    frameTime = frameTime * 0.9f + io.DeltaTime * 0.1f;
    const DWORD now = GetTickCount();
    if (now >= nextFps && frameTime > 0.f) { fps = std::to_string((int)(1.f / frameTime)); nextFps = now + 1000; }

    const std::string brand = Brand();
    std::string shown = brand;

    static bool wasOn = false, typing = true;
    static int count = 0;
    static DWORD nextChar = 0, holdUntil = 0;
    if (g_wmSkebobTyping) {
        if (!wasOn) { count = 0; typing = true; nextChar = now; holdUntil = 0; }
        wasOn = true;
        const int length = (int)brand.size();
        if (typing) {
            if (now > nextChar) {
                ++count; nextChar = now + 150;
                if (count > length) { typing = false; holdUntil = now + 2000; count = length; }
            }
            shown = brand.substr(0, (size_t)std::clamp(count, 0, length));
        } else if (now > holdUntil && now > nextChar) {
            --count; nextChar = now + 100;
            if (count < 0) { typing = true; count = 0; nextChar = now; }
            else shown = brand.substr(0, (size_t)(std::min)(count, length));
        }
    } else wasOn = false;

    const std::string user = User();
    const float size = font->FontSize;
    const float wBrand = TextW(font, size, shown), wUser = TextW(font, size, user), wFps = TextW(font, size, fps), wSuffix = TextW(font, size, "fps");

    const float width = wBrand + wUser + wFps + wSuffix + 70.f;
    const float wx = io.DisplaySize.x - width;
    const ImVec2 min(wx + 10.f, 10.f), max(wx + width - 5.f, 40.f);
    SetBox(min.x, min.y, max.x, max.y);

    dl->AddRectFilled(min, max, IM_COL32(40, 40, 40, 150), 5.f);
    dl->AddRect(min, max, IM_COL32(0, 0, 0, 200), 5.f, 0, 1.f);
    dl->AddRectFilled(ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(20, 20, 20, 255), 5.f);
    dl->AddRect(ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(0, 0, 0, 255), 5.f, 0, 1.f);

    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
    dl->AddText(font, size, ImVec2(wx + 20.f, 20.f), Accent(), shown.c_str());
    dl->AddText(font, size, ImVec2(wx + wBrand + 25.f, 20.f), white, "|");
    dl->AddText(font, size, ImVec2(wx + wBrand + 35.f, 20.f), white, user.c_str());
    dl->AddText(font, size, ImVec2(wx + wBrand + wUser + 40.f, 20.f), white, "|");
    dl->AddText(font, size, ImVec2(wx + wBrand + wUser + 50.f, 20.f), white, fps.c_str());
    dl->AddText(font, size, ImVec2(wx + wBrand + wUser + wFps + 55.f, 20.f), white, "fps");
}

// style 13: cumidere -- spectator_list.cpp draw_watermark_cumidere(). tahoma 16 light, (23) box open
// at the bottom (accent top/left/right), brand accent, " | " (27), label (208), fps label: number
// (208) + " fps" (136), smoothed 0.7/0.3 every 0.5s.
static void DrawCumidere(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontCumidereTahoma16);

    static float fps = 0.f, last = 0.f;
    if ((float)ImGui::GetTime() - last >= 0.5f) { fps = fps * 0.7f + 0.3f / (std::max)(io.DeltaTime, 0.0001f); last = (float)ImGui::GetTime(); }

    const int labelType = g_wmLabelType;
    const std::string brand = Brand();
    const std::string number = labelType == 1 ? std::to_string((int)fps) : Label();
    const std::string suffix = labelType == 1 ? " fps" : "";
    const std::string sep = number.empty() ? "" : " | ";

    const float size = font->FontSize;
    const float wB = TextW(font, size, brand), wSep = TextW(font, size, sep), wN = TextW(font, size, number), wFps = TextW(font, size, suffix);

    const float right = io.DisplaySize.x - 10.f;
    const float left = right - (wB + wSep + wN + wFps + 16.f);
    const float bottom = size + 20.f;
    SetBox(left, 10.f, right, bottom);

    const ImU32 accent = Accent();
    dl->AddRectFilled(ImVec2(left, 10.f), ImVec2(right, bottom), IM_COL32(23, 23, 23, 255));
    dl->AddLine(ImVec2(left, 10.f), ImVec2(right, 10.f), accent, 1.f);
    dl->AddLine(ImVec2(left, 10.f), ImVec2(left, bottom), accent, 1.f);
    dl->AddLine(ImVec2(right, 10.f), ImVec2(right, bottom), accent, 1.f);

    float x = left + 8.f;
    dl->AddText(font, size, ImVec2(x, 15.f), accent, brand.c_str()); x += wB;
    dl->AddText(font, size, ImVec2(x, 15.f), IM_COL32(27, 27, 27, 255), sep.c_str()); x += wSep;
    dl->AddText(font, size, ImVec2(x, 15.f), IM_COL32(208, 208, 208, 255), number.c_str()); x += wN;
    dl->AddText(font, size, ImVec2(x, 15.f), IM_COL32(136, 136, 136, 255), suffix.c_str());
}

// style 14: illusory -- spectator_list.cpp draw_watermark_illusory(). tahoma 13 mono+bold, rounded
// (28) box, (56,128) border, accent(230) under white double-drawn "brand | label" (fps 0.7s smoothed).
static void DrawIllusory(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontIllusoryTahoma13);

    static float fps = 0.f, last = 0.f;
    const float now = (float)ImGui::GetTime();
    if (now - last >= 0.7f && io.DeltaTime > 0.f) { fps = fps * 0.7f + (1.f / io.DeltaTime) * 0.3f; last = now; }

    const std::string label = g_wmLabelType == 1 ? std::to_string((int)fps) + " fps" : Label();
    const std::string text = label.empty() ? Brand() : Brand() + " | " + label;
    const ImVec2 size = font->CalcTextSizeA(13.f, FLT_MAX, 0.f, text.c_str());
    const float w = (float)(int)io.DisplaySize.x;
    SetBox(w - size.x - 18.f, 4.f, w - 4.f, size.y + 16.f);

    dl->AddRectFilled(ImVec2(w - size.x - 17.f, 5.f), ImVec2(w - 5.f, size.y + 15.f), IM_COL32(28, 28, 28, 255), 5.5f);
    dl->AddRect(ImVec2(w - size.x - 18.f, 4.f), ImVec2(w - 4.f, size.y + 16.f), IM_COL32(56, 56, 56, 128), 5.5f, 0, 1.f);

    dl->AddText(font, 13.f, ImVec2(w - size.x - 10.f, 10.f), AccentA(230), text.c_str());
    dl->AddText(font, 13.f, ImVec2(w - size.x - 11.f, 10.f), IM_COL32(255, 255, 255, 255), text.c_str());
}

// style 15: lumi -- spectator_list.cpp draw_watermark_lumi(). rubik 16 mono, (20) box, title (title
// colour) then " / "-joined accent elements (user / tick / time / fps), width eased at 8/s.
// design original = accent stripe divider; new = 16 falling accent rain streaks, 6px gap.
// animated title = types in at 6.67 chars/s, holds 2s, restarts. glow = accent bloom for 1.5s after a
// trick (Points_OnTrick), shadow colour = text shadow tinted by it. offset x / y move the box.
static void LumiGlow(ImDrawList* dl, ImVec2 min, ImVec2 max, float fade)
{
    const float radius = (std::max)(g_wmLumiGlowRadius, 1.f);
    const float opacity = g_wmLumiGlowOpacity;
    const float falloff = -1.25f / (radius * 0.4f * radius);
    for (int i = 0; i < 7; ++i) {
        const float d = (float)(i - 3);
        const int kernel = (int)(expf(2.f * d * d * falloff) * 255.f) & 0xff;
        const float a = (float)kernel * opacity * fade;
        if (a < 0.5f) continue;
        const float grow = radius * 0.33333334f * (float)i;
        dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow), ImVec2(max.x + grow, max.y + grow), AccentA((std::min)((int)a, 255)),
            radius * 0.1f * (float)i + 5.f);
    }
}
struct LumiStreak { float x = 0.f, y = 0.f, speed = 0.f, len = 0.f, alpha = 0.f; };
static uint32_t s_lumiSeed = 0x51a9c37du;
static float LumiRand(float scale, float base) { s_lumiSeed = s_lumiSeed * 0x19660du + 0x3c6ef35fu; return (float)(s_lumiSeed >> 8) * scale + base; }
static void LumiReseed(LumiStreak& s)
{
    s.x = LumiRand(5.960465e-08f, 0.f);
    s.speed = LumiRand(3.874302e-08f, 0.35f);
    s.len = LumiRand(1.31130236e-08f, 0.1f);
    s.alpha = LumiRand(2.2649768e-08f, 0.32f);
}
static void LumiRain(ImDrawList* dl, ImVec2 min, ImVec2 max, float dtIn)
{
    static LumiStreak streaks[16];
    static bool seeded = false;
    if (!seeded) {
        for (auto& s : streaks) {   // donor order: x, y, speed, len, alpha
            s.x = LumiRand(5.960465e-08f, 0.f); s.y = LumiRand(5.960465e-08f, 0.f);
            s.speed = LumiRand(3.874302e-08f, 0.35f); s.len = LumiRand(1.31130236e-08f, 0.1f); s.alpha = LumiRand(2.2649768e-08f, 0.32f);
        }
        seeded = true;
    }

    float dt = dtIn <= 0.f ? 0.016f : dtIn;
    dt = (std::min)(dt, 0.033f);

    const ImVec2 clipMin(min.x + 1.2f, min.y + 1.2f), clipMax(max.x - 1.2f, max.y - 1.2f);
    if (clipMin.x >= clipMax.x || clipMin.y >= clipMax.y) return;

    dl->PushClipRect(clipMin, clipMax, true);
    const float w = max.x - min.x, h = max.y - min.y;
    for (auto& s : streaks) {
        s.y += dt * s.speed * 0.55f;
        if (s.y > 1.15f) { s.y = -s.len; LumiReseed(s); }
        const float x = w * s.x + min.x;
        const float y0 = s.y * h + min.y, y1 = h * s.len + y0;
        if (min.y <= y1 && y0 <= max.y)
            dl->AddLine(ImVec2(x, (std::max)(y0, min.y)), ImVec2(x, (std::min)(y1, max.y)), AccentA((int)(s.alpha * 255.f)), 1.f);
    }
    dl->PopClipRect();
}
static void DrawLumi(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontLumiRubik16);
    const float now = (float)ImGui::GetTime();

    static float smoothFps = 0.f, lastFps = 0.f;
    static int shownFps = 0;
    smoothFps = smoothFps * 0.9f + io.Framerate * 0.100000024f;
    if (now - lastFps >= 0.3f) { lastFps = now; shownFps = (int)(smoothFps + 0.5f); }

    std::vector<std::string> elements;
    if (g_wmUser) elements.push_back(User());
    if (g_wmTickrate && TickLive()) elements.push_back(std::to_string(Tickrate()) + " tick");   // as styles 0 / 2: in game only
    if (g_wmShowTime) elements.push_back(Clock());
    if (g_wmShowFPS) elements.push_back(std::to_string(shownFps) + " fps");

    std::string title = Brand();

    static bool typeWasOn = false, typed = false;
    static float typeStart = 0.f, typeShown = 1.f;
    static std::string typeTitle;
    const bool animated = g_wmLumiAnimTitle;
    if (animated) {
        if (!typeWasOn || title != typeTitle) { typeTitle = title; typeShown = 1.f; typed = false; typeStart = now; }

        size_t characters = 0;   // utf-8 characters, the cut never splits one
        for (const char c : title) characters += ((unsigned char)c & 0xc0) != 0x80;
        const float length = (float)characters;

        if (!typed) {
            float target = (now - typeStart) * 6.6666665f + 1.f;
            if (length <= target) {
                target = length;
                if (length - 0.1f <= typeShown) { typed = true; typeStart = now; }
            }
            const float dt = io.DeltaTime > 0.f ? io.DeltaTime : 0.016f;
            typeShown = (1.f - expf(-12.f * dt)) * (target - typeShown) + typeShown;
        } else {
            typeShown = length;
            if (now - typeStart >= 2.f) { typed = false; typeShown = 1.f; typeStart = now; }
        }
        title = Utf8Truncate(title, (std::min)(characters, (size_t)ceilf((std::max)(typeShown, 0.f))));
    }
    typeWasOn = animated;

    const float size = font->FontSize;
    const float titleW = TextW(font, size, title), sepW = TextW(font, size, " / ");
    const int design = g_wmLumiDesign;
    const float gap = design == 1 ? 6.f : 27.f;

    float content = titleW;
    if (!elements.empty()) {
        content += gap + (float)(elements.size() - 1) * sepW;
        for (const auto& e : elements) content += TextW(font, size, e);
    }

    static float width = 0.f, lastEase = -10.f;
    const float target = content + 12.f;
    width = now - lastEase > 1.f || fabsf(width - target) > 200.f ? target : (target - width) * io.DeltaTime * 8.f + width;
    lastEase = now;

    const float height = size + 12.f;
    const float x = io.DisplaySize.x - width - 10.f + g_wmLumiX;
    const float y = 10.f + g_wmLumiY;
    SetBox(x, y, x + width, y + height);

    const ImU32 accent = Accent();

    static float glow = 0.f;
    const bool glowing = g_wmLumiGlow && TrickWithin(1.5f);
    glow = std::clamp(glow + io.DeltaTime * (glowing ? 4.f : -4.f), 0.f, 1.f);
    if (glow > 0.f) LumiGlow(dl, ImVec2(x, y), ImVec2(x + width, y + height), glow);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(20, 20, 20, 255), 5.f);
    if (design == 1) LumiRain(dl, ImVec2(x, y), ImVec2(x + width, y + height), io.DeltaTime);
    if (!g_wmLumiNoBorder) dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);

    if (design != 1) {
        const float stripeX = x + titleW + 11.f;
        for (int i = 15; i < 18; ++i) {
            const float sx = stripeX + (float)i;
            dl->AddLine(ImVec2(sx, y + 4.f), ImVec2(sx - 12.f, y + height - 5.f), accent, 1.f);
            dl->AddLine(ImVec2(sx - 5.f, y + 4.f), ImVec2(sx - 17.f, y + height - 5.f), accent, 1.f);
        }
    }

    ImU32 shadow = IM_COL32(0, 0, 0, 255);
    if (g_wmLumiShadowColor && glow > 0.f) {
        const ImVec4 a = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
        shadow = IM_COL32((int)(a.x * glow * 255.f + 0.5f), (int)(a.y * glow * 255.f + 0.5f), (int)(a.z * glow * 255.f + 0.5f), 255);
    }

    float tx = (width - content) * 0.5f + x;
    const float ty = floorf((height - size) * 0.5f + y);
    const auto text = [&](float at, ImU32 color, const char* s) {
        dl->AddText(font, size, ImVec2(at + 1.f, ty + 1.f), shadow, s);
        dl->AddText(font, size, ImVec2(at, ty), color, s);
    };

    text(tx, PackColor(g_wmLumiTitleColor), title.c_str());
    tx += titleW + gap;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) { text(tx, accent, " / "); tx += sepW; }
        text(tx, accent, elements[i].c_str());
        tx += TextW(font, size, elements[i]);
    }
}

// style 16: billware -- spectator_list.cpp draw_watermark_billware(). tahoma bold 12 mono, nested
// rounded-corner gradient rings, 10 slow-drifting accent dots, accent under white " brand | label".
static void BillwareMulticolorRounded(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 bg, ImU32 ul, ImU32 ur, ImU32 br, ImU32 bl, float rounding)
{
    const float r = (std::min)(rounding, (std::min)(fabsf(max.x - min.x) * 0.5f - 1.f, fabsf(max.y - min.y) * 0.5f - 1.f));
    if (r <= 0.f) return;
    dl->AddRectFilledMultiColor(min, max, ul, ur, br, bl);
    struct { ImVec2 corner, center; float aMin, aMax; } corners[] = {
        { min, ImVec2(min.x + r, min.y + r), 4.82f, 3.1f },
        { ImVec2(max.x, min.y), ImVec2(max.x - r, min.y + r), 6.34f, 4.62f },
        { max, ImVec2(max.x - r, max.y - r), 7.96f, 6.24f },
        { ImVec2(min.x, max.y), ImVec2(min.x + r, max.y - r), 9.5f, 7.77f },
    };
    for (auto& c : corners) { dl->PathLineTo(c.corner); dl->PathArcTo(c.center, r, c.aMin, c.aMax, 0); dl->PathFillConvex(bg); }
}
static void BillwareBox(ImDrawList* dl, ImVec2 min, ImVec2 max, float alpha)
{
    auto grey = [](float v, float a) { return IM_COL32((int)(v * 255.f), (int)(v * 255.f), (int)(v * 255.f), (int)(a * 255.f)); };
    auto grown = [&](float g) { return std::pair{ ImVec2(min.x - g, min.y - g), ImVec2(max.x + g, max.y + g) }; };

    const ImU32 dark = grey(0.07f, alpha), light = grey(0.14f, alpha);
    const auto [fillMin, fillMax] = grown(2.f);
    BillwareMulticolorRounded(dl, fillMin, fillMax, dark, dark, dark, light, light, 5.5f);
    BillwareMulticolorRounded(dl, fillMin, fillMax, dark, light, light, dark, dark, 5.5f);

    const ImU32 rings[3] = { grey(0.15f, alpha), grey(0.3f, alpha * 0.7f), grey(0.15f, alpha) };
    for (int i = 0; i < 3; ++i) { const auto [a, b] = grown(2.f + (float)i); dl->AddRect(a, b, rings[i], 5.5f, 0, 1.f); }
}
static void DrawBillware(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontTahomaBd12);

    static float fps = 0.f, last = 0.f;
    const float now = (float)ImGui::GetTime();
    if (now - last >= 0.5f && io.DeltaTime > 0.f) { fps = fps * 0.7f + (1.f / io.DeltaTime) * 0.5f; last = now; }   // donor's 0.7 / 0.5 kept

    const std::string label = g_wmLabelType == 1 ? std::to_string((int)fps) + " fps" : Label();
    const std::string text = " " + Brand() + (label.empty() ? "" : " | " + label);
    const ImVec2 size = font->CalcTextSizeA(12.f, FLT_MAX, 0.f, text.c_str());
    const float w = (float)(int)io.DisplaySize.x;
    const ImVec2 min(w - size.x - 13.f, 7.f), max(w - 7.f, size.y + 13.f);
    SetBox(min.x - 4.f, min.y - 4.f, max.x + 4.f, max.y + 4.f);

    BillwareBox(dl, min, max, 1.f);

    const float x0 = min.x - 2.f, x1 = max.x + 2.f, y0 = min.y - 2.f, y1 = max.y + 2.f;
    auto randomIn = [](float lo, float hi) { return (float)(rand() % (std::max)((int)(hi - lo), 1)) + lo; };

    static std::vector<ImVec2> dots;
    if (dots.empty()) for (int i = 0; i < 10; ++i) { const float x = randomIn(x0, x1); dots.emplace_back(x, randomIn(y0, y1)); }

    const ImU32 dotColor = AccentA(102);
    for (auto& dot : dots) {
        dl->AddCircle(dot, 2.5f, dotColor, 0, 1.f);
        dl->AddCircleFilled(dot, 0.5f, dotColor, 0);
        dot.y += 0.07f;
        if (dot.y > y1 - 1.f) { dot.y = randomIn(y0, y1); dot.x = randomIn(x0, x1); }
    }

    dl->AddText(font, 12.f, ImVec2(w - size.x - 12.f, 9.f), Accent(), text.c_str());
    dl->AddText(font, 12.f, ImVec2(w - size.x - 11.f, 10.f), IM_COL32(255, 255, 255, 255), text.c_str());
}

// styles 17/18: dna / dna clarity -- spectator_list.cpp draw_watermark_dna[_clarity](). montserrat
// 15 / pt root bold 15, corner-shaded ring box, parts brand | user | label (fps label = number +
// " fps"). dna: 60-step accent strip over the text + "detection glow": accent bloom for glow ticks
// after a trick, or while an enemy is spotted (TF2: visible, aim/wm_aim.cpp). dna clarity: faint
// icon glyph bled into the left edge.
static ImU32 DnaGrey(float v, float a = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(v, v, v, a)); }
static void DnaBox(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 ring, ImU32 midRing)
{
    const ImU32 corner = IM_COL32(10, 10, 12, 50), dark = IM_COL32(10, 10, 12, 255), light = DnaGrey(0.14f);
    BillwareMulticolorRounded(dl, min, max, corner, dark, dark, light, light, 5.5f);
    BillwareMulticolorRounded(dl, min, max, corner, light, light, dark, dark, 5.5f);
    dl->AddRect(min, max, ring, 5.5f);
    dl->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), midRing, 5.5f);
    dl->AddRect(ImVec2(min.x - 2.5f, min.y - 2.5f), ImVec2(max.x + 2.5f, max.y + 2.5f), ring, 5.5f);
}
static int DnaFps(float& rate, float& last, float dt)
{
    const float now = (float)ImGui::GetTime();
    const float ft = dt > 0.f ? dt : 0.0001f;
    if (now - last >= 0.5f) { rate = rate * 0.7f + 0.3f / ft; last = now; }
    return (int)rate;
}
struct DnaPart { std::string text; ImU32 color; };
static std::vector<DnaPart> DnaParts(ImU32 brand, ImU32 sep, ImU32 user, ImU32 number, ImU32 unit, int fps)
{
    std::vector<DnaPart> parts{ { Brand(), brand } };
    if (const std::string name = User(); !name.empty()) { parts.push_back({ " | ", sep }); parts.push_back({ name, user }); }
    if (g_wmLabelType == 1) {
        parts.push_back({ " | ", sep }); parts.push_back({ std::to_string(fps), number }); parts.push_back({ " fps", unit });
    } else if (const std::string label = Label(); !label.empty()) {
        parts.push_back({ " | ", sep }); parts.push_back({ label, number });
    }
    return parts;
}
static float DnaPartsW(ImFont* f, const std::vector<DnaPart>& parts)
{
    float w = 0.f;
    for (const auto& p : parts) w += TextW(f, 15.f, p.text);
    return w;
}
static void DrawDna(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontDnaMontserrat15);

    static float rate = 0.f, last = 0.f;
    const ImU32 accent = AccentF(1.f), white = DnaGrey(0.8f), darkgrey = DnaGrey(0.4f);
    const auto parts = DnaParts(accent, darkgrey, darkgrey, white, darkgrey, DnaFps(rate, last, io.DeltaTime));
    const float textW = DnaPartsW(font, parts);
    const float w = (float)(int)io.DisplaySize.x;

    const ImVec2 textPos(w - textW - 10.f, 10.f);
    const ImVec2 min(w - textW - 15.f, 5.f), max(w - 5.f, 30.f);
    SetBox(min.x - 3.f, min.y - 3.f, max.x + 3.f, max.y + 3.f);

    const float interval = s_f.interval > 0.f ? s_f.interval : 0.015f;
    const bool trick = TrickWithin((float)g_wmDnaGlowTicks * interval);
    const bool lit = g_wmDnaGlow && (trick || (g_wmDnaGlowSpotted && s_f.inGame && g_wmEnemySpotted.load()));

    static float glow = 0.f;
    glow = lit ? (std::min)(1.f, glow + 3.f * io.DeltaTime) : (std::max)(0.f, glow - 3.f * io.DeltaTime);

    if (glow > 0.01f) {
        for (int i = 0; i < 10; ++i) {
            const float t = (float)i / 9.f, grow = 7.f * glow * t;
            dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow), ImVec2(max.x + grow, max.y + grow), AccentF(0.75f * glow * (1.f - t)), 5.5f, ImDrawFlags_RoundCornersAll);
        }
        dl->AddRectFilled(ImVec2(min.x - 2.f, min.y - 2.f), ImVec2(max.x + 2.f, max.y + 2.f), AccentF(glow * 0.9f), 5.5f, ImDrawFlags_RoundCornersAll);
    }

    DnaBox(dl, min, max, IM_COL32(10, 10, 12, 200), IM_COL32(4, 4, 5, 50));

    const float gradW = textW * 1.1f, center = textPos.x + textW * 0.5f, top = textPos.y - 6.f;
    for (int i = 0; i < 60; ++i) {
        const float x0 = center - gradW * 0.5f + gradW / 60.f * (float)i, x1 = x0 + gradW / 60.f;
        const float a = (std::max)(0.f, 1.f - fabsf((x0 + x1) * 0.5f - center) / (gradW * 0.5f));
        dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, top + 3.f), AccentF(0.75f * a), 2.f, 0);
    }

    float x = textPos.x;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i == 0) dl->AddText(font, 15.f, ImVec2(x + 1.f, textPos.y + 1.f), white, parts[i].text.c_str());
        dl->AddText(font, 15.f, ImVec2(x, textPos.y), parts[i].color, parts[i].text.c_str());
        x += TextW(font, 15.f, parts[i].text);
    }
}
static void DrawDnaClarity(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontDnaPtRootBold15);
    ImFont* icon = g_fontDnaClarityIcon50 ? g_fontDnaClarityIcon50 : g_fontClarityIcon;

    static float rate = 0.f, last = 0.f;
    const ImU32 accent = AccentF(1.f), white = DnaGrey(0.8f), darkgrey = DnaGrey(0.5f), darkgrey2 = DnaGrey(0.2f);
    const auto parts = DnaParts(accent, darkgrey2, darkgrey, white, darkgrey, DnaFps(rate, last, io.DeltaTime));
    const float textW = DnaPartsW(font, parts);
    const float w = (float)(int)io.DisplaySize.x;

    const ImVec2 textPos(w - textW - 10.f, 10.f);
    const ImVec2 min(w - textW - 15.f, 5.f), max(w - 5.f, 30.f);
    SetBox(min.x - 3.f, min.y - 3.f, max.x + 3.f, max.y + 3.f);

    DnaBox(dl, min, max, IM_COL32(20, 20, 22, 250), IM_COL32(4, 4, 5, 100));
    if (icon) {
        const float iconW = icon->CalcTextSizeA(50.f, FLT_MAX, 0.f, "A").x;
        dl->PushClipRect(min, max, true);
        dl->AddText(icon, 50.f, ImVec2(min.x + 6.f - iconW * 0.3f, textPos.y - 17.f), AccentF(0.15f), "A");
        dl->PopClipRect();
    }

    float x = textPos.x;
    for (const auto& p : parts) {
        dl->AddText(font, 15.f, ImVec2(x, textPos.y), p.color, p.text.c_str());
        x += TextW(font, 15.f, p.text);
    }
}

// style 19: cucumber -- spectator_list.cpp draw_watermark_cucumber(). tahoma 14 draws, verdana 14
// measures the user (donor quirk kept), pill bg, accent brand | user white, accent underline,
// 5/s fade in/out on the watermark toggle (so it runs with the toggle off until faded).
static constexpr ImU32 kCucumberBg = IM_COL32(18, 15, 18, 255);
static float CucumberW(ImFont* f, const std::string& s) { return (float)(int)(TextW(f, 14.f, s) + 0.95f); }
static ImU32 CucumberFade(ImU32 color, float alpha)
{
    return (color & ~IM_COL32_A_MASK) | ((ImU32)((float)(color >> IM_COL32_A_SHIFT) * alpha + 0.5f) << IM_COL32_A_SHIFT);
}
static void DrawCucumber(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontCucumberTahoma14);
    ImFont* measure = g_fontCucumberVerdana14 ? g_fontCucumberVerdana14 : font;

    static float alpha = 0.f;
    const float step = io.DeltaTime * 5.f;
    alpha = std::clamp(g_wmEnable ? alpha + step : alpha - step, 0.f, 1.f);
    if (!g_wmEnable && alpha <= 0.f) return;

    const std::string brand = Brand(), user = User();
    const float userW = CucumberW(measure, user);

    constexpr float x = 10.f, y = 10.f;
    const ImVec2 max(x + 100.f + userW, y + 20.f);
    SetBox(x, y, max.x, max.y);

    dl->AddRectFilled(ImVec2(x, y), max, kCucumberBg, 15.f);

    const ImU32 white = CucumberFade(IM_COL32(255, 255, 255, 255), alpha);
    float tx = x + 16.f;
    dl->AddText(font, 14.f, ImVec2(tx, y + 2.f), CucumberFade(Accent(), alpha), brand.c_str());
    tx += CucumberW(font, brand) + 8.f;
    dl->AddText(font, 14.f, ImVec2(tx, y + 2.f), white, "|");
    tx += CucumberW(font, "|") + 8.f;
    dl->AddText(font, 14.f, ImVec2(tx, y + 2.f), white, user.c_str());

    dl->AddRectFilled(ImVec2(x + 15.f, y + 18.f), ImVec2(x + 85.f + userW, y + 20.f), Accent());
}

// style 20: howeweware -- spectator_list.cpp draw_watermark_howeweware(). minecraft 14, no
// background, rows of grey value + accent unit, top-left: brand / user, then time "pm", ping "ms",
// tick, fps, velocity "vel". imgui 1.70 window metrics: (int)(w + 0.95), padding 8, spacing 8 x 4.
static constexpr ImU32 kHowewewareGrey = IM_COL32(225, 225, 225, 255);
static float HowewewareW(ImFont* f, float size, const std::string& s) { return (float)(int)(TextW(f, size, s) + 0.95f); }
static void DrawHoweweware(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontHowewewareMinecraft14);
    constexpr float size = 14.f, line = size + 4.f, pad = 8.f, pos = 5.f, spacing = 8.f;
    const bool connected = s_f.inGame;

    std::vector<std::pair<std::string, std::string>> lines{ { Brand(), User() } };
    if (g_wmShowTime) lines.emplace_back(Clock("%X"), "pm");
    if (g_wmShowPing && connected) lines.emplace_back(std::to_string(Ping()), "ms");
    if (g_wmTickrate && TickLive()) lines.emplace_back(std::to_string(Tickrate(false)), " tick");
    if (g_wmShowFPS) {   // donor's smoothing: 0.9 * last + 0.1 * frame, last always 0
        float frame = io.DeltaTime * 0.1f;
        if (frame <= 0.f) frame = 1.f;
        lines.emplace_back(std::to_string((int)(1.f / frame) / 10), "fps");
    }
    if (g_wmVelocity && connected) lines.emplace_back(std::to_string(s_f.speed), "vel");

    const ImU32 accent = Accent();
    float right = pos + pad;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float y = pos + pad + line * (float)i;
        float x = pos + pad;
        dl->AddText(font, size, ImVec2(x, y), kHowewewareGrey, lines[i].first.c_str());
        x += HowewewareW(font, size, lines[i].first) + spacing;
        dl->AddText(font, size, ImVec2(x, y), accent, lines[i].second.c_str());
        right = (std::max)(right, x + HowewewareW(font, size, lines[i].second));
    }
    const float bottom = pos + pad + line * (float)(lines.size() - 1) + size;
    SetBox(pos, pos, pos + (std::max)(32.f, right - pos + pad), pos + (std::max)(32.f, bottom - pos + pad));
}

// style 21: gamesense -- spectator_list.cpp draw_watermark_gamesense() (chifirium.dll FUN_1008e770).
// sf pro display bold 15 px atlas, every measure + draw at 13. 4-layer frame, optional skeet
// gradient bar, brand split "game"|"sense" (game / sense colours), parts user / fps / ms / tick /
// vel / HH:MM:SS in (255,230) after (210) " | ", offset x / y move it.
static constexpr float kGamesenseSize = 13.f;
static float GamesenseW(ImFont* f, const std::string& s) { return TextW(f, kGamesenseSize, s); }
static void GamesenseFrame(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    dl->AddRectFilled(ImVec2(min.x - 3.f, min.y - 3.f), ImVec2(max.x + 3.f, max.y + 3.f), IM_COL32(36, 36, 36, 255));
    dl->AddRect(ImVec2(min.x - 3.f, min.y - 3.f), ImVec2(max.x + 3.f, max.y + 3.f), IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);
    dl->AddRectFilled(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), IM_COL32(18, 18, 18, 255));
    dl->AddRectFilled(min, max, IM_COL32(12, 12, 12, 245));
    dl->AddRect(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), IM_COL32(62, 62, 62, 255), 0.f, 0, 1.f);
}
static void GamesenseBar(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    constexpr ImU32 blue = IM_COL32(59, 175, 222, 255), pink = IM_COL32(202, 70, 205, 255), yellow = IM_COL32(201, 227, 58, 255);
    const float mid = min.x + (max.x - min.x) * 0.5f;
    dl->AddRectFilledMultiColor(ImVec2(min.x + 2.f, min.y + 2.f), ImVec2(mid, min.y + 4.f), blue, pink, pink, blue);
    dl->AddRectFilledMultiColor(ImVec2(mid, min.y + 2.f), ImVec2(max.x - 2.f, min.y + 4.f), pink, yellow, yellow, pink);
}
void Watermark_GamesenseFrame(ImDrawList* dl, ImVec2 min, ImVec2 max, bool bar)
{
    GamesenseFrame(dl, min, max);
    if (bar) GamesenseBar(dl, min, max);
}
// split at half the codepoints ("gamesense" -> game | sense)
static size_t GamesenseSplit(const std::string& text)
{
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); ++i) if (((unsigned char)text[i] & 0xc0) != 0x80) starts.push_back(i);
    return starts.size() < 2 ? text.size() : starts[starts.size() / 2];
}
static void DrawGamesense(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontGamesenseSfBold15);

    static float fps = 0.f;   // donor fps: 0.1 * (1 / frametime) + 0.9 * last
    fps = 1.f / (std::max)(0.0001f, io.DeltaTime) * 0.1f + fps * 0.9f;

    std::vector<std::string> parts;
    if (g_wmUser) parts.push_back(User());
    if (g_wmShowFPS) parts.push_back(std::to_string((int)(fps + 0.5f)) + " fps");
    if (g_wmShowPing) parts.push_back(std::to_string(Ping()) + "ms");
    if (g_wmTickrate && TickLive()) parts.push_back(std::to_string(Tickrate()) + "tick");
    if (g_wmVelocity) parts.push_back(std::to_string(s_f.inGame ? s_f.speed : 0) + " vel");
    if (g_wmShowTime) parts.push_back(Clock("%H:%M:%S"));

    const std::string brand = Brand();
    const size_t cut = GamesenseSplit(brand);
    const std::string game = brand.substr(0, cut), sense = brand.substr(cut);

    constexpr const char* separator = " | ";
    float width = GamesenseW(font, game) + GamesenseW(font, sense);
    for (const auto& p : parts) width += GamesenseW(font, separator) + GamesenseW(font, p);
    width += 14.f;

    const bool bar = g_wmGsBar;
    const float textH = font->CalcTextSizeA(kGamesenseSize, FLT_MAX, 0.f, "A").y;
    const float height = textH + 10.f + (bar ? 3.f : 0.f);
    const float x = io.DisplaySize.x - width - 15.f + g_wmGsX, y = 15.f + g_wmGsY;
    const ImVec2 min(x, y), max(x + width, y + height);
    SetBox(x - 3.f, y - 3.f, max.x + 3.f, max.y + 3.f);

    GamesenseFrame(dl, min, max);
    if (bar) GamesenseBar(dl, min, max);

    const float textY = y + 3.f + (bar ? 2.f : 0.f);
    float textX = x + 7.f;
    const auto put = [&](const std::string& s, ImU32 c) { dl->AddText(font, kGamesenseSize, ImVec2(textX, textY), c, s.c_str()); textX += GamesenseW(font, s); };

    put(game, PackColor(g_wmGsGameColor));
    put(sense, PackColor(g_wmGsSenseColor));
    for (const auto& p : parts) { put(separator, IM_COL32(210, 210, 210, 255)); put(p, IM_COL32(255, 255, 255, 230)); }
}

// style 22: boxed -- ImGui window "watermark_menu" redrawn on the list.
// Default ImGuiStyle (padding 8), bg = its primary color, menu accent where ref uses its accent.
static constexpr ImU32 kBoxedBg   = IM_COL32(37, 39, 48, 255);   // cfg primary color
static constexpr ImU32 kBoxedDark = IM_COL32(15, 15, 15, 255);
// window rect [min, max): fill, three insets (dark / accent 25% / dark), accent + black outside
static void BoxedFrame(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    const float x = min.x, y = min.y, r = max.x, b = max.y;
    dl->AddRectFilled(min, max, kBoxedBg);
    dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(r - 2.f, b - 2.f), kBoxedDark);
    dl->AddRect(ImVec2(x + 2.f, y + 2.f), ImVec2(r - 3.f, b - 3.f), AccentF(0.25f));
    dl->AddRect(ImVec2(x + 3.f, y + 3.f), ImVec2(r - 4.f, b - 4.f), kBoxedDark);
    dl->AddRect(ImVec2(x - 1.f, y - 1.f), ImVec2(r, b), Accent());
    dl->AddRect(ImVec2(x - 2.f, y - 2.f), ImVec2(r + 1.f, b + 1.f), IM_COL32(0, 0, 0, 255));
}
static void DrawBoxed(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontArialBd16);
    const float px = font->FontSize;
    const auto w = [&](const std::string& s) { return ImguiW(font, s); };   // ImGui::CalcTextSize, ceiled

    // fps: frametime ema 0.1, the shown number refreshed once a second
    static float avg = 0.f, shownAvg = 0.f;
    static double next = 0.0;
    avg = avg > 0.f ? io.DeltaTime * 0.1f + avg * 0.9f : io.DeltaTime;
    if (next < ImGui::GetTime()) { next = ImGui::GetTime() + 1.0; shownAvg = avg; }

    const std::string brand = Brand();
    std::string rest = " / dev", fps;
    if (g_wmShowTime) rest += " / " + Clock("%H:%M:%S");
    if (g_wmShowFPS) { rest += " / "; fps = std::to_string(shownAvg > 0.f ? (int)(1.f / shownAvg) : 0); }

    // Text + SameLine(0, 0) chain, window AlwaysAutoResize inside constraints (50,25)-(300,250)
    const float contentW = w(brand) + w(rest) + w(fps) + (g_wmShowFPS ? w(" fps") : 0.f);
    const float width = std::clamp(contentW + 16.f, 50.f, 300.f), height = std::clamp(px + 16.f, 25.f, 250.f);
    // ref places it with FramePadding.x * 2 (= 8). ref leaves fps number out of measure ->
    // box ran off right edge with show fps; measure full content + clamp outer ring on screen
    const float x = floorf((std::min)(io.DisplaySize.x - (8.f + contentW + io.DisplaySize.x * g_wmBoxedX),
                                      io.DisplaySize.x - width - 1.f));
    const float y = floorf(g_wmBoxedY * io.DisplaySize.y);
    SetBox(x - 2.f, y - 2.f, x + width + 1.f, y + height + 1.f);

    BoxedFrame(dl, ImVec2(x, y), ImVec2(x + width, y + height));

    constexpr ImU32 white = IM_COL32(255, 255, 255, 255), grey = IM_COL32(153, 153, 153, 255);
    float tx = x + 8.f;
    const auto put = [&](const std::string& s, ImU32 c) { dl->AddText(font, px, ImVec2(tx, y + 8.f), c, s.c_str()); tx += w(s); };
    put(brand, white);
    put(rest, grey);
    put(fps, white);
    if (g_wmShowFPS) put(" fps", grey);
}

// style 8: airflow -- spectator_list.cpp draw_watermark_airflow(). sf ui display semibold 14.
// Two rounded pills over a live backbuffer blur: left = airflow logo (accent) + brand, right =
// local avatar + "user | HH:MM ping ms". botox modulates the blur image white (left) / (100) grey
// (right); BlurRect has no tint, so the right pill gets black at 155 alpha over it = blur * 100/255.
struct WmPlayerInfo_t
{
    char     name[32];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[32];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};
static ImTextureID LocalAvatarTex()
{
    if (!g_engine) return 0;
    const int idx = GetLocalPlayerIndex();
    if (idx < 1) return 0;
    WmPlayerInfo_t pi = {};
    if (!VCall<bool>(g_engine, 8, idx, &pi) || pi.fakeplayer || !pi.friendsID) return 0;
    return Avatar_Get(n_avatar::FriendIdToSteam64(pi.friendsID), false);
}
static void DrawAirflow(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Or(g_fontAirflow14);

    const std::string clock = Clock("%H:%M");
    const std::string prefix = Brand(), ping = std::to_string(Ping()) + "ms", user = User();

    const float textWidth = ImguiW(font, prefix + " " + user + " | " + clock + "  " + ping);
    const float prefixSize = ImguiW(font, prefix) + 16.f;
    const float windowWidth = 75.f + textWidth + 16.f;

    const float x = io.DisplaySize.x - windowWidth - 10.f + 4.f, y = 10.f + 1.f;
    SetBox(x, y, x + windowWidth - 10.f, y + 29.f);

    const ImDrawListFlags oldFlags = dl->Flags;
    dl->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
    const float rightX = x + prefixSize + 30.f;
    const ImVec2 rightMax(rightX + windowWidth - prefixSize - 40.f, y + 29.f);

    BlurRect(dl, ImVec2(x, y), rightMax, 1.f, 3.f);
    dl->AddRectFilled(ImVec2(rightX, y), rightMax, IM_COL32(0, 0, 0, 155), 3.f, ImDrawFlags_RoundCornersRight);

    GifTex* gif = GifTex_Get("airflow_logo", airflow_logo, sizeof(airflow_logo));
    if (const ImTextureID logo = GifTex_Frame(gif, 0.0))
        dl->AddImage(logo, ImVec2(x + 10.f, y + 7.f), ImVec2(x + 26.f, y + 23.f), ImVec2(0, 0), ImVec2(1, 1), Accent());
    dl->AddText(font, 14.f, ImVec2(x + 32.f, y + 6.5f), white, prefix.c_str());

    const ImVec2 avMin(rightX + 7.f, y + 6.f), avMax(rightX + 23.f, y + 22.f);
    if (const ImTextureID avatar = LocalAvatarTex()) dl->AddImageRounded(avatar, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), white, 20.f);
    else dl->AddRectFilled(avMin, avMax, white, 20.f);
    dl->AddText(font, 14.f, ImVec2(rightX + 29.f, y + 6.5f), white, (user + " | " + clock + " " + ping).c_str());

    dl->AddRect(ImVec2(x, y), ImVec2(x + windowWidth - 10.f, y + 29.f), IM_COL32(120, 120, 120, 100), 3.f);
    dl->Flags = oldFlags;
}

// botox misc.cpp draw_watermark(): clarity v2 (3) and cucumber (19) run before the m_watermark
// check (they gate / fade on it themselves), every other style needs it on.
void Watermark_Draw()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f) return;

    s_f.inGame = IsInGame();
    s_f.interval = g_globalVars ? g_globalVars->interval_per_tick : 0.f;
    MvInd_t mv{};
    MV_GetIndicators(mv);
    s_f.speed = mv.ms && GetTickCount64() - mv.ms < 250 ? (int)(mv.speed + 0.5f) : 0;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const int style = g_wmStyle;

    if (style == 3) return DrawClarityV2(dl, io);
    if (style == 19) return DrawCucumber(dl, io);
    if (!g_wmEnable) return;

    switch (style) {
    case 1:  return DrawKamibebra(dl, io);
    case 2:  return DrawClarity(dl, io);
    case 4:  return DrawDelusional(dl, io);
    case 5:  return DrawInterwebz(dl, io);
    case 6:  return DrawHavoc(dl, io);
    case 7:  return DrawOnetap(dl, io);
    case 8:  return DrawAirflow(dl, io);
    case 9:  return DrawEvolve(dl, io);
    case 10: return DrawLegendware(dl, io);
    case 11: return DrawInterium(dl, io);
    case 12: return DrawSkebob(dl, io);
    case 13: return DrawCumidere(dl, io);
    case 14: return DrawIllusory(dl, io);
    case 15: return DrawLumi(dl, io);
    case 16: return DrawBillware(dl, io);
    case 17: return DrawDna(dl, io);
    case 18: return DrawDnaClarity(dl, io);
    case 20: return DrawHoweweware(dl, io);
    case 21: return DrawGamesense(dl, io);
    case 22: return DrawBoxed(dl, io);
    default: return DrawDefault(dl, io);
    }
}

// ==== media player: botox media_player_look / media_player_frame + the donor halves ===========
MediaLook Watermark_MediaLook(int style)
{
    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 accent = Accent();
    switch (style) {
    case 1: {
        const float* s = g_wmKamiOutlineColor;
        const ImU32 main = g_wmKamiTextLikeStroke ? IM_COL32((int)(s[0] * 255.f), (int)(s[1] * 255.f), (int)(s[2] * 255.f), 255) : white;
        return { true, main, IM_COL32(146, 144, 145, 255), 0 };
    }
    case 2:  return { false, white, white, 1 };
    case 3:  return { true, accent, IM_COL32(255, 255, 255, 150), 0 };
    case 4:  return { true, white, white, 1 };
    case 5:  return { false, white, white, 2 };
    case 7:  return { true, IM_COL32(255, 255, 255, 150), IM_COL32(255, 255, 255, 150), 0 };
    // spectator_list.cpp media_player_donor_look
    case 6:  return { true, HavocAccent(), HavocAccent2(), 1 };
    case 8:  return { true, white, IM_COL32(255, 255, 255, 102), 0 };
    case 9:  return { true, accent, IM_COL32(220, 220, 220, 255), 0 };
    case 10: return { true, IM_COL32(255, 255, 255, 220), IM_COL32(255, 255, 255, 220), 1 };
    case 11: return { true, white, white, 1 };
    case 12: return { false, white, IM_COL32(153, 153, 153, 255), 3 };
    case 13: return { false, white, IM_COL32(153, 153, 153, 255), 1 };
    case 14: return { false, white, white, 1 };
    case 15: return { true, white, accent, 0 };
    case 16: return { true, white, accent, 0 };
    case 17: return { true, accent, DnaGrey(0.8f), 0 };
    case 18: return { true, accent, DnaGrey(0.5f), 0 };
    case 19: return { true, accent, white, 0 };
    case 20: return { false, accent, kHowewewareGrey, 0 };
    case 21: return { true, PackColor(g_wmGsSenseColor), IM_COL32(255, 255, 255, 230), 0 };
    // boxed music display: white title, artist white at 0.5, +1 black shadow
    case 22: return { true, white, IM_COL32(255, 255, 255, 127), 1 };
    default: return { true, white, accent, 0 };   // style 0: menu accent
    }
}

// the style's watermark box grown to [min, max]. bare styles (look.box false) pass 0
void Watermark_MediaFrame(ImDrawList* dl, ImVec2 min, ImVec2 max, int style)
{
    switch (style) {
    case 0:
        dl->AddRectFilled(min, max, IM_COL32(25, 25, 25, 255), ImGui::GetStyle().WindowRounding);
        dl->AddRect(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), IM_COL32(50, 50, 50, 255), ImGui::GetStyle().WindowRounding);
        break;
    case 1: {
        const float rounding = KamiRounding();
        dl->AddRectFilled(min, max, IM_COL32(16, 16, 16, 255), rounding);
        if (g_wmKamiOutline)
            dl->AddRect(min, max, g_wmKamiRgb ? s_kamiRgbLast : PackColor(g_wmKamiOutlineColor), rounding, ImDrawFlags_RoundCornersAll, 1.f);
        break;
    }
    case 3: {
        dl->AddRectFilled(min, max, IM_COL32(25, 25, 25, 255), 5.f);
        const ImU32 rings[] = { IM_COL32(0, 0, 0, 255), IM_COL32(50, 50, 50, 255), IM_COL32(0, 0, 0, 255) };
        for (int grow = 0; grow < 3; ++grow) {
            const float inset = (float)(3 - grow);
            dl->AddRect(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset), rings[grow], 5.f);
        }
        break;
    }
    case 4:
        dl->AddRectFilledMultiColor(min, max, Accent(), Accent(), AccentA(0), AccentA(0));
        dl->AddRectFilled(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), IM_COL32(20, 20, 20, 255));
        break;
    case 7: dl->AddRectFilled(min, max, IM_COL32(185, 90, 105, 160)); break;
    // spectator_list.cpp media_player_donor_frame
    case 6: HavocBox(dl, min, ImVec2(max.x - min.x, max.y - min.y), 1.f); break;
    case 8:   // blur modulated (100) grey = blur under black at 155
        BlurRect(dl, min, max, 1.f, 3.f);
        dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 155), 3.f);
        break;
    case 9:
        EvolveShadow(dl, min, max, 12.f, EvolveMaxAlpha() * 0.25f);
        dl->AddRectFilled(min, ImVec2(max.x, max.y - 1.f), IM_COL32(29, 29, 29, 255), 4.f, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(min.x, max.y - 1.f), max, Accent());
        break;
    case 10: {
        int rgb[3]; AccentRgb(rgb);
        const float half = floorf((max.x - min.x) / 2.f);
        for (const float lineY : { min.y, max.y - 1.f }) {
            LegendwareGradient(dl, ImVec2(min.x, lineY), ImVec2(half, 1.f), rgb, 170, 240);
            LegendwareGradient(dl, ImVec2(min.x + half, lineY), ImVec2(max.x - min.x - half, 1.f), rgb, 240, 170);
        }
        dl->AddRectFilled(ImVec2(min.x, min.y + 1.f), ImVec2(max.x, max.y - 1.f), IM_COL32(10, 10, 10, 150));
        break;
    }
    case 11: {
        const float* h = s_interiumHues;   // stepped by the watermark the same frame
        dl->AddRectFilledMultiColor(min, max, InteriumHsv(h[0], 100), InteriumHsv(h[1], 100), InteriumHsv(h[2], 100), InteriumHsv(h[3], 100));
        dl->AddRectFilledMultiColor(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f),
            InteriumHsv(h[0], 255), InteriumHsv(h[1], 255), InteriumHsv(h[2], 255), InteriumHsv(h[3], 255));
        dl->AddRectFilled(ImVec2(min.x + 2.f, min.y + 2.f), ImVec2(max.x - 2.f, max.y - 2.f), PackColor(g_wmInteriumBg));
        break;
    }
    case 15:
        dl->AddRectFilled(min, max, IM_COL32(20, 20, 20, 255), 5.f);
        if (!g_wmLumiNoBorder) dl->AddRect(min, max, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);
        break;
    case 16: BillwareBox(dl, ImVec2(min.x + 4.f, min.y + 4.f), ImVec2(max.x - 4.f, max.y - 4.f), 1.f); break;
    case 17: DnaBox(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(10, 10, 12, 200), IM_COL32(4, 4, 5, 50)); break;
    case 18: DnaBox(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(20, 20, 22, 250), IM_COL32(4, 4, 5, 100)); break;
    case 19:
        dl->AddRectFilled(min, max, kCucumberBg, 15.f);
        dl->AddRectFilled(ImVec2(min.x + 15.f, max.y - 2.f), ImVec2(max.x - 15.f, max.y), Accent());
        break;
    case 21:
        GamesenseFrame(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f));
        if (g_wmGsBar) GamesenseBar(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f));
        break;
    case 22: BoxedFrame(dl, ImVec2(min.x + 2.f, min.y + 2.f), ImVec2(max.x - 1.f, max.y - 1.f)); break;   // outer rings stay in [min, max]
    default: break;
    }
}
