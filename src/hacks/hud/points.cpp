#include "points.h"
#include "watermark.h"   // g_wmBox -- botox sits the counter beside the watermark box
#include "../../render/dpi.h"   // botox stretch block (dpi scale panels)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

// botox hacks/misc/points.cpp, 1:1. Only change: botox's real_time clock is the steady clock here
// (detections run on the CreateMove thread, the engine's globals are not ours to read there).
namespace
{
    // written by Points_OnTrick (CreateMove), read + zeroed by RenderPoints (render thread)
    std::atomic<int>      s_total{ 0 };
    std::atomic<unsigned> s_events{ 0 };

    float Now()
    {
        using clock = std::chrono::steady_clock;
        static const clock::time_point start = clock::now();
        return std::chrono::duration<float>(clock::now() - start).count();
    }

    // botox points_font: big font, else the watermark style's own face, else verdana bold 11
    std::pair<ImFont*, float> PointsFont()
    {
        ImFont* font = nullptr;
        float size = 0.f;

        if (g_ptsBigFont) { font = g_fontPoints18; size = 21.f; }
        else if (g_wmEnable)
        {
            switch (g_wmStyle)
            {
            case 1:  font = g_fontKamibebraBold13; break;
            case 3:  font = g_fontClarityInter; break;
            case 4:
            case 7:  font = g_fontTahoma12; break;
            case 5:  font = g_fontInterwebzCalibri; break;
            case 6:  font = g_fontHavocTahoma13; break;
            case 8:  font = g_fontAirflow14; break;
            case 9:  font = g_fontEvolveBold16; break;
            case 10: font = g_fontLegendwareVerdana12; break;
            case 11: font = g_fontInteriumDroid24; size = 16.f; break;
            case 12: font = g_fontSkebobNunito14; break;
            case 13: font = g_fontCumidereTahoma16; break;
            case 14: font = g_fontIllusoryTahoma13; break;
            case 15: font = g_fontLumiRubik16; break;
            case 16: font = g_fontTahomaBd12; break;
            case 17: font = g_fontDnaMontserrat15; break;
            case 18: font = g_fontDnaPtRootBold15; break;
            case 19: font = g_fontCucumberTahoma14; break;
            case 20: font = g_fontHowewewareMinecraft14; break;
            case 21: font = g_fontGamesenseSfBold15; size = 13.f; break;
            case 22: font = g_fontArialBd16; break;
            default: break;
            }
        }

        if (!font) { font = g_menuBold ? g_menuBold : ImGui::GetFont(); size = 0.f; }
        return { font, size > 0.f ? size : font ? font->FontSize : 0.f };
    }
}

void Points_OnTrick(int trick)
{
    g_wmTrickMs.store(GetTickCount64());   // unit wm: botox points_on_trick g_trick_time (dna / lumi watermark glow)
    if (trick >= 0 && trick < PTS_PIXEL_SURF_RIDE) g_trickFired[trick].fetch_add(1);
    if (!g_ptsEnable || g_showMenu || trick < 0 || trick > PTS_WALL_CLIMB_RIDE) return;

    constexpr int kHeldFirst = PTS_PIXEL_SURF, kRideFirst = PTS_PIXEL_SURF_RIDE;
    static_assert(PTS_WALL_CLIMB_RIDE - kRideFirst == kRideFirst - 1 - kHeldFirst, "held / ride blocks out of step");
    static float nextRideBonus[kRideFirst - kHeldFirst] = {};
    const float now = Now();

    int amount = 0;
    if (trick >= kRideFirst)
    {
        float& next = nextRideBonus[trick - kRideFirst];
        if (now < next) return;
        amount = 25;
        next = now + 0.35f;
    }
    else if (trick >= kHeldFirst)
    {
        amount = 125;
        nextRideBonus[trick - kHeldFirst] = now + 0.35f;
    }
    else
        amount = trick == PTS_EDGE_BUG ? 125 : trick == PTS_FIREMAN ? 275 : 50;

    s_total.fetch_add(amount, std::memory_order_relaxed);
    s_events.fetch_add(1u, std::memory_order_release);
}

void RenderPoints()
{
    constexpr float kInactivity = 3.f, kFade = 0.35f, kQuickAnim = 0.25f, kReveal = 0.22f;
    constexpr float kAlphaOut = 0.08f, kAlphaIn = 0.08f, kSlide = 50.f, kGap = 6.f;
    constexpr float kSpring = 200.f, kDamping = 40.f, kImpulse = 6.f;
    constexpr float kBob = 3.75f, kDropIn = 17.f;

    static int shown = 0;
    static unsigned seenEvents = 0;
    static float displayTimer = 0.f, fadeTimer = 0.f, animTimer = 0.f, revealTimer = 0.f, resetTimer = 0.f;
    static bool fading = false, animActive = false, revealing = false;
    static float offset = 0.f, offsetVel = 0.f, bobPhase = 0.f, drop = 0.f, trail = 0.f;

    const auto reset = [&] {
        shown = 0;
        displayTimer = fadeTimer = animTimer = revealTimer = resetTimer = 0.f;
        fading = animActive = revealing = false;
        offset = offsetVel = bobPhase = drop = trail = 0.f;
    };

    if (!g_ptsEnable)
    {
        s_total.store(0, std::memory_order_relaxed);
        reset();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const float dt    = io.DeltaTime > 0.f ? io.DeltaTime : 1.f / 60.f;
    const float speed = std::clamp(g_ptsAppearSpeed, 0.1f, 20.f);

    const int total       = s_total.load(std::memory_order_relaxed);
    const unsigned events = s_events.load(std::memory_order_acquire);
    const bool newEvent   = events != seenEvents;
    seenEvents = events;

    if (newEvent && (total > 0 || shown > 0)) { displayTimer = kInactivity; fading = false; fadeTimer = 0.f; }

    if (total > shown)
    {
        if (shown <= 0)
        {
            revealing = true; revealTimer = 0.f; animActive = false;
            offset = offsetVel = 0.f;
            drop = -kDropIn;
        }
        else
        {
            revealing = false;
            offsetVel += kSlide * kImpulse;
            animActive = true; animTimer = 0.f;
        }
        shown = total;
    }

    if (shown <= 0 && !fading) return;

    if (displayTimer > 0.f) displayTimer = std::max(displayTimer - dt, 0.f);
    else if (!fading) { fading = true; fadeTimer = 0.f; }

    if (animActive && (animTimer += dt * speed) >= kQuickAnim) { animTimer = kQuickAnim; animActive = false; }
    if (revealing && (revealTimer += dt * speed) >= kReveal) { revealTimer = kReveal; revealing = false; }
    if (fading) fadeTimer = std::min(fadeTimer + dt, kFade);

    const auto easeOut = [](float x) { return 1.f - (1.f - x) * (1.f - x); };
    const auto easeIn  = [](float x) { return x * x; };

    float alpha = 1.f, target = 0.f;
    if (fading)
    {
        target = kSlide;
        alpha  = 1.f - easeIn(std::clamp(fadeTimer / kAlphaOut, 0.f, 1.f));
    }
    else if (revealing)
    {
        const float t = easeOut(std::clamp(revealTimer / kReveal, 0.f, 1.f));
        target = kSlide * (1.f - t) * 0.9f;
        alpha  = t;
    }
    else if (animActive)
    {
        if (animTimer < kQuickAnim * 0.5f)
        {
            target = kSlide * 0.95f;
            alpha  = 1.f - easeIn(std::clamp(animTimer / kAlphaOut, 0.f, 1.f));
        }
        else
            alpha = easeIn(std::clamp((animTimer - kQuickAnim * 0.5f) / kAlphaIn, 0.f, 1.f));
    }

    offsetVel += (kSpring * (target - offset) - kDamping * offsetVel) * dt;
    offset += offsetVel * dt;

    drop += (0.f - drop) * std::min(8.5f * dt, 1.f);

    bobPhase = std::fmod(bobPhase + dt * std::clamp(g_ptsBobSpeed, 0.1f, 10.f) * 3.6f, 6.2831853f);
    const float bob = std::sin(bobPhase) * kBob;

    if (fading && fadeTimer >= kFade && alpha <= 0.001f)
    {
        if ((resetTimer += dt) >= 0.2f)
        {
            int expected = total;
            s_total.compare_exchange_strong(expected, 0, std::memory_order_relaxed);
            reset();
        }
        return;
    }
    resetTimer = 0.f;

    const auto [font, fontSize] = PointsFont();
    if (!font || io.DisplaySize.x <= 0.f) return;

    const std::string text = std::to_string(shown) + " pts";
    const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text.c_str());

    const ImVec4 box   = g_wmBox;
    const bool hasBox  = box.z > box.x;
    const int style    = g_wmStyle;
    const bool rightOf = hasBox && (style == 11 || style == 19 || style == 20);
    const bool screen  = !hasBox || style == 5;

    float x = screen ? io.DisplaySize.x - 8.f - size.x : rightOf ? box.z + kGap : box.x - kGap - size.x;
    float y = (hasBox && !screen ? (box.y + box.w) * 0.5f : 20.f) - size.y * 0.5f;

    x += (rightOf ? -offset : offset) + g_ptsOffsetX;
    y += drop + bob + g_ptsOffsetY;
    x = std::floor(x);
    y = std::floor(y);

    const bool bobbing = !animActive && !fading && alpha >= 0.99f;
    trail += ((bobbing ? -bob * 1.15f : 0.f) - trail) * std::clamp(dt * 5.f, 0.f, 1.f);

    const int ar = (int)(g_accentColor[0] * 255.f), ag = (int)(g_accentColor[1] * 255.f), ab = (int)(g_accentColor[2] * 255.f);
    const auto accentA = [&](float a) { return IM_COL32(ar, ag, ab, (int)std::clamp(alpha * a, 0.f, 255.f)); };

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    DpiPanelScope block(dl);   // botox points_draw: one stretch block
    const char* t = text.c_str();
    dl->AddText(font, fontSize, ImVec2(x + 1.8f, y + 1.8f + trail * 1.35f), accentA(90.f), t);
    dl->AddText(font, fontSize, ImVec2(x + 1.25f, y + 1.25f + trail), accentA(135.f), t);
    dl->AddText(font, fontSize, ImVec2(x + 1.f, y + 1.f + trail), accentA(180.f), t);
    dl->AddText(font, fontSize, ImVec2(x + 0.5f, y + 0.5f + trail * 0.55f), accentA(180.f), t);
    dl->AddText(font, fontSize, ImVec2(x, y), IM_COL32(255, 255, 255, (int)std::clamp(alpha * 255.f, 0.f, 255.f)), t);
}
