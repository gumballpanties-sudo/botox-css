#include "boot_screen.h"
#include "../screen/blur_rect.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <utility>
#include <atomic>

// botox csgo-sdk hacks/misc/boot_screen.cpp at e14be34 (dna particles flying out of the
// centre, "injecting <step>" + 3 pulsing dots, bar + label centred as one block, step times
// remembered in boot_times.txt, the rest of the overlay fading in after it).
// TF2 differences:
//   - the splash can only start on the first hooked Present, by which point modules ..
//     config are already done (MainThread), so those stamp at once; "hooks" waits for
//     PollThread (it installs the client/engine hooks after its own opening sleep), "fonts"
//     is the atlas render.cpp builds synchronously before the first NewFrame.
//   - eject splash: the request (g_ejectRequest) fades it in over the live overlay; under the
//     full cover MainThread sets g_eject and the world restores, then h_Present draws the splash
//     alone (RenderEjectFrame) while it fades the game back in.
//   - boot_screen_active() (botox holds screen passes under the cover) not ported: no pass
//     here stalls on a shader build.
namespace
{
constexpr const char* k_steps[] = { "modules", "interfaces", "netvars", "convars", "input", "config", "hooks", "render", "fonts", "effects" };
constexpr int k_count   = (int)(sizeof(k_steps) / sizeof(k_steps[0]));
constexpr int k_hooks   = 6;
constexpr int k_fonts   = k_count - 2;
constexpr int k_effects = k_count - 1;
constexpr float kPi = 3.14159265358979f;
constexpr const char* k_times = "C:\\botox-css\\boot_times.txt";

// last inject's step times (ms), bar weights. defaults until one inject has finished
float     g_expected[k_count] = { 50.f, 30.f, 60.f, 20.f, 10.f, 300.f, 400.f, 150.f, 2500.f, 600.f };
long long g_start = 0, g_stamp[k_count] = {};
int       g_done  = 0;
// MainThread waits on these, the render thread writes them
std::atomic<bool>      g_finished{ false }, g_ejecting{ false }, g_ejectCover{ false }, g_ejectDone{ false };
std::atomic<long long> g_lastDraw{ 0 };

float g_reveal   = 0.f;   // rest of the overlay, 0 -> 1 once the splash is gone
int   g_splashVtx = -1;

long long NowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Step()
{
    if (g_done >= k_count) return;
    g_stamp[g_done++] = NowUs();
}

void LoadTimes()
{
    FILE* f = nullptr;
    if (fopen_s(&f, k_times, "r") || !f) return;
    float loaded[k_count] = {};
    bool ok = true;
    for (float& ms : loaded) ok = ok && fscanf_s(f, "%f", &ms) == 1 && ms >= 0.f && ms < 120000.f;
    fclose(f);
    if (ok) std::copy(loaded, loaded + k_count, g_expected);
}

void SaveTimes()
{
    CreateDirectoryA(CONFIG_DIR, nullptr);
    FILE* f = nullptr;
    if (fopen_s(&f, k_times, "w") || !f) return;
    long long prev = g_start;
    for (int i = 0; i < k_count; ++i)
    {
        const float actual = (float)(g_stamp[i] - prev) / 1000.f;
        prev = g_stamp[i];
        fprintf(f, "%.1f\n", (g_expected[i] + actual) * 0.5f);
    }
    fclose(f);
}

// done steps count whole, the running one creeps to 90% of its expected time
float Progress(long long now)
{
    float total = 0.f, filled = 0.f;
    for (int i = 0; i < k_count; ++i)
    {
        const float w = (std::max)(g_expected[i], 1.f);
        total += w;
        if (i < g_done) filled += w;
        else if (i == g_done)
        {
            const float spent = (float)(now - (i ? g_stamp[i - 1] : g_start)) / 1000.f;
            filled += w * (std::min)(spent / w, 0.9f);
        }
    }
    return filled / total;
}

bool PollThreadDone()
{
    return g_hPollThread && WaitForSingleObject(g_hPollThread, 0) == WAIT_OBJECT_0;
}

// dna menu particles (random size + alpha, sine twinkle), flying out of the center
struct Particle { ImVec2 dir; float dist, speed, size, alpha, phase, rate; };
Particle g_particles[75];
bool     g_particlesInit = false;

float Frand() { return (float)rand() / (float)RAND_MAX; }

void Spawn(Particle& p, float dist)
{
    const float a = Frand() * 2.f * kPi;
    p.dir   = ImVec2(std::cos(a), std::sin(a));
    p.dist  = dist;
    p.speed = 60.f + Frand() * 160.f;
    p.size  = 2.5f + Frand() * 2.5f;
    p.alpha = 0.4f + Frand() * 0.5f;
    p.phase = (float)(rand() % 1000) * 0.01f;
    p.rate  = 0.6f + (float)(rand() % 40) / 100.f;
}

// MainThread: returns at flag, or at once when no frames come (minimised), 1.5s cap
void WaitFor(const std::atomic<bool>& flag)
{
    for (int i = 0; i < 150 && !flag; ++i)
    {
        if (NowUs() - g_lastDraw > 250'000) return;
        Sleep(10);
    }
}
}

void BootScreen_EjectBegin()
{
    g_ejecting = true;
    WaitFor(g_ejectCover);
}

void BootScreen_EjectEnd()
{
    g_ejectDone = true;
    WaitFor(g_finished);
}

bool BootScreen_EjectDrawing() { return g_ejecting && !g_finished; }

void RenderBootScreen()
{
    static float ramp = 0.f, shown = 0.f, clock_ = 0.f, hold = 0.f, warm = 0.f;
    static int   calm = 0;
    static bool  started = false, saved = false, ejecting = false;

    // every frame, finished too: eject's wait reads it as "frames are coming"
    const long long now = NowUs();
    g_lastDraw = now;

    if (g_ejecting && !ejecting)
    {
        ejecting = true;
        shown = clock_ = hold = 0.f;
        g_finished = false;
        DebugLog("BOOT: eject splash");
    }

    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 1.f / 30.f);   // a hitch frame never skips the fade
    // rest of the overlay: in after the inject splash is gone, out while the eject splash comes in
    if (ejecting)        g_reveal = (std::max)(g_reveal - dt / 0.35f, 0.f);
    else if (g_finished) g_reveal = (std::min)(g_reveal + dt / 0.4f, 1.f);
    g_splashVtx = -1;
    if (g_finished) return;

    if (!started)
    {
        started = true;
        g_start = now;
        LoadTimes();
        while (g_done < k_hooks) Step();   // MainThread finished these before Present was hooked
    }
    clock_ += dt;

    float fraction = 1.f;
    bool  done     = false;
    if (!ejecting)
    {
        if (g_done == k_hooks && PollThreadDone()) Step();
        if (g_done == k_fonts - 1) Step();                    // render: this frame is it
        if (g_done == k_fonts && g_menuBold) Step();          // atlas built before NewFrame
        // warm = 15 frames in a row under 50 ms, 4s cap for slow pcs
        if (g_done == k_effects)
        {
            warm += dt;
            calm = ImGui::GetIO().DeltaTime < 0.05f ? calm + 1 : 0;
            if (calm >= 15 || warm > 4.f) Step();
        }
        // a stuck step must never hide the overlay for good
        while (clock_ > 20.f && g_done < k_count) Step();

        done = g_done >= k_count;
        if (done && !std::exchange(saved, true))
        {
            SaveTimes();
            DebugLog("BOOT: injected in %.0f ms (hooks step %.0f ms)", (float)(now - g_start) / 1000.f,
                     (float)(g_stamp[k_hooks] - g_stamp[k_hooks - 1]) / 1000.f);
        }
        if (!done) fraction = Progress(now);
    }
    else
    {
        // no steps to weigh: creeps toward 90% until MainThread says the restore is done
        done = g_ejectDone;
        if (!done) fraction = 0.9f * (1.f - std::exp(-clock_ / 0.6f));
    }

    if (done) hold += dt;
    const bool show = !done || hold < (ejecting ? 0.25f : 0.35f);
    ramp = show ? (std::min)(ramp + dt / 0.35f, 1.f) : (std::max)(ramp - dt / 0.45f, 0.f);
    if (ejecting && ramp >= 1.f) g_ejectCover = true;
    if (!show && ramp <= 0.f) { g_finished = true; return; }

    const float fade = ramp * ramp * (3.f - 2.f * ramp);
    shown += (fraction - shown) * (std::min)(dt * 12.f, 1.f);

    ImGuiIO& io = ImGui::GetIO();
    const float w = io.DisplaySize.x, h = io.DisplaySize.y, u = h / 1080.f;
    if (w <= 0.f || h <= 0.f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    g_splashVtx = dl->VtxBuffer.Size;
    const auto color = [fade](float a) { return IM_COL32(255, 255, 255, (int)(a * fade * 255.f + 0.5f)); };

    BlurRectFade(dl, ImVec2(0.f, 0.f), ImVec2(w, h), 1.f, fade);   // botox screen_blur_fullscreen(.., 5 passes)
    dl->AddRectFilled(ImVec2(0.f, 0.f), ImVec2(w, h), IM_COL32(0, 0, 0, (int)(0.18f * fade * 255.f)));

    // content grows 94 -> 100% and rises 10 px with the fade
    const float grow = 0.94f + 0.06f * fade;
    const ImVec2 center(w * 0.5f, h * 0.5f + 10.f * u * (1.f - fade));

    // first frame spreads them along their rays so the stream is already full; off screen = back to the center
    const float reach = std::hypot(w, h) * 0.5f + 4.f * u;
    const ImVec4 accent(g_accentColor[0], g_accentColor[1], g_accentColor[2], 1.f);
    if (!std::exchange(g_particlesInit, true))
    {
        for (Particle& p : g_particles) Spawn(p, Frand() * reach);
        DebugLog("BOOT: particles %d reach=%.0f u=%.2f accent=%.2f %.2f %.2f", (int)(sizeof(g_particles) / sizeof(g_particles[0])),
                 reach, u, accent.x, accent.y, accent.z);
    }
    for (Particle& p : g_particles)
    {
        p.dist += p.speed * u * dt;
        if (p.dist > reach) Spawn(p, 0.f);
        p.phase += dt * p.rate;
        // 40 px fade in off the center so they never pop
        const float a = p.alpha * (0.6f + 0.4f * std::sin(p.phase)) * (std::min)(p.dist / (40.f * u), 1.f) * fade;
        dl->AddCircleFilled(ImVec2(center.x + p.dir.x * p.dist, center.y + p.dir.y * p.dist), p.size * u,
                            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, a)));
    }

    char text[96];
    if (ejecting)
    {
        if (done) snprintf(text, sizeof(text), "ejected");
        else      snprintf(text, sizeof(text), "ejecting   %d%%", (int)(shown * 100.f));
    }
    else if (done) snprintf(text, sizeof(text), "injected");
    else      snprintf(text, sizeof(text), "injecting %s   %d%%", k_steps[(std::min)(g_done, k_count - 1)], (int)(shown * 100.f));

    // menu "botox" label font, native size: monochrome verdana goes soft when scaled
    ImFont* font = g_menuBold ? g_menuBold : ImGui::GetFont();
    const ImVec2 extent = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
    const float dotW = done ? 0.f : font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, ".").x;

    // bar + gap + label centered as one block
    const float barW = 260.f * u * grow, barH = (std::max)(std::round(4.f * u), 2.f), gap = std::round(12.f * u);
    const ImVec2 barMin(std::floor(center.x - barW * 0.5f), std::floor(center.y - (barH + gap + extent.y) * 0.5f));
    const ImVec2 barMax(barMin.x + barW, barMin.y + barH);
    dl->AddRectFilled(barMin, barMax, color(0.15f), barH * 0.5f);
    if (shown > 0.001f)
        dl->AddRectFilled(barMin, ImVec2(barMin.x + barW * (std::min)(shown, 1.f), barMax.y), color(0.95f), barH * 0.5f);

    // dots counted in the width so the label never shifts
    const ImVec2 textPos(std::floor(center.x - (extent.x + 3.f * dotW) * 0.5f), barMax.y + gap);
    dl->AddText(font, font->FontSize, textPos, color(1.f), text);

    // working: 3 dots pulse left to right, 1.2s loop
    for (int i = 0; i < 3 && !done; ++i)
    {
        const float wave = 0.5f + 0.5f * std::cos((clock_ - (float)i * 0.2f) * (2.f * kPi / 1.2f));
        dl->AddText(font, font->FontSize, ImVec2(textPos.x + extent.x + (float)i * dotW, textPos.y), color(0.2f + 0.8f * wave), ".");
    }
}

void BootScreen_FadeOthers(ImDrawData* dd)
{
    if (g_reveal >= 1.f || !dd) return;
    const float eased = g_reveal * g_reveal * (3.f - 2.f * g_reveal);
    const ImU32 scale = (ImU32)(eased * 256.f);
    ImDrawList* const splash = ImGui::GetForegroundDrawList();
    for (int i = 0; i < dd->CmdListsCount; ++i)
    {
        ImDrawList* const list = dd->CmdLists[i];
        const int end = list == splash && g_splashVtx >= 0 ? (std::min)(g_splashVtx, list->VtxBuffer.Size) : list->VtxBuffer.Size;
        for (int v = 0; v < end; ++v)
        {
            ImU32& col = list->VtxBuffer.Data[v].col;
            col = (col & ~IM_COL32_A_MASK) | (((((col >> IM_COL32_A_SHIFT) & 0xFF) * scale) >> 8) << IM_COL32_A_SHIFT);
        }
    }
}
