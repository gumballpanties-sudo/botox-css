#include "dpi.h"
#include "dpi_math.h"
#include "../globals/globals.h"
#include "../hacks/screen/aspect_stretch_math.h"
#include "../hacks/screen/blur_rect.h"   // BlurRect_ScaleJob: a panel's blur rect grows with it
#include "../dependencies/imgui/imgui_internal.h"
#include "../dependencies/imgui/freetype/imgui_freetype.h"
#include <d3d9.h>
#include <algorithm>
#include <cfloat>
#include <cstring>

// botox globals/render/render.cpp dpi layer, ported 1:1 (see dpi.h for the model). The imgui
// fork calls in through three pointers instead of botox's externs, so the test targets that link
// the fork without this file still build (NULL = no dpi layer):
extern const ImFont* (*GBotoxDpiTwin)(const ImFont*, const ImDrawList*, const char*, const char*);   // imgui_draw.cpp
extern float (*GBotoxDpiWindowScale)(const ImGuiWindow*);                                            // imgui.cpp Begin
extern void (*GBotoxDpiMouse)(ImVec2*);                                                              // imgui.cpp NewFrame

static float  s_scale = 1.f, s_panelScale = 1.f;
static ImVec2 s_screen(0.f, 0.f);
static ImVec2 s_mouse(-FLT_MAX, -FLT_MAX);   // the real mouse this frame, screen px
static int    s_open[2] = {};                // open panel blocks on the background (0) / foreground (1) list

float  Dpi_Scale()       { return s_scale; }
float  Dpi_PanelScale()  { return s_panelScale; }
ImVec2 Dpi_ScreenMouse() { return s_mouse; }

// ==== windows ===================================================================================
// botox is_stretched_window: the hud windows. With "dpi scale panels" off they stay 1:1 like the
// panels, slid so they keep their share of the free room; every other window grows by the scale.
static bool StretchedWindow(const ImGuiWindow* w)
{
    const char* n = w->Name;
    return strstr(n, "botox-spectators") || !strcmp(n, "botox-route-calc-keys") || !strcmp(n, "##mr_clipper_box") ||
           !strcmp(n, "botox-rear-mirror");
}

static bool WindowUnscaled(const ImGuiWindow* root)
{
    return root && s_scale != 1.f && s_panelScale == 1.f && StretchedWindow(root);
}

static ImVec2 UnscaledOffset(const ImGuiWindow* root)
{
    return ImVec2(DpiUnscaledShown(root->Pos.x, root->Size.x, s_screen.x, s_scale) - root->Pos.x,
                  DpiUnscaledShown(root->Pos.y, root->Size.y, s_screen.y, s_scale) - root->Pos.y);
}

static ImRect WindowDrawnRect(const ImGuiWindow* w, ImRect r)
{
    if (WindowUnscaled(w->RootWindow)) { r.Translate(UnscaledOffset(w->RootWindow)); return r; }
    return ImRect(r.Min.x * s_scale, r.Min.y * s_scale, r.Max.x * s_scale, r.Max.y * s_scale);
}

static float WindowScale(const ImGuiWindow* root) { return WindowUnscaled(root) ? 1.f : s_scale; }

// botox dpi_logical_mouse. imgui 1.89 queues mouse events, so this runs from inside NewFrame right
// after the queue is applied (botox's older backend wrote io.MousePos before NewFrame).
static void MouseHook(ImVec2* pos)
{
    s_mouse = *pos;
    if (s_scale == 1.f || !ImGui::IsMousePosValid(pos)) return;
    ImGuiContext& g = *GImGui;
    const ImVec2 m = *pos;

    if (g.MovingWindow && WindowUnscaled(g.MovingWindow->RootWindow))
    {
        const ImVec2 size = g.MovingWindow->RootWindow->Size, k = g.ActiveIdClickOffset;
        *pos = ImVec2(DpiUnscaledLayout(m.x - k.x, size.x, s_screen.x, s_scale) + k.x,
                      DpiUnscaledLayout(m.y - k.y, size.y, s_screen.y, s_scale) + k.y);
        return;
    }

    const ImGuiWindow* owner = g.MovingWindow ? g.MovingWindow : g.ActiveId ? g.ActiveIdWindow : nullptr;
    for (int i = g.Windows.Size - 1; !owner && i >= 0; --i)
    {
        const ImGuiWindow* w = g.Windows[i];
        if (!w->Active || w->Hidden || (w->Flags & ImGuiWindowFlags_NoMouseInputs)) continue;
        if (WindowDrawnRect(w, w->OuterRectClipped).Contains(m)) owner = w;
    }

    if (owner && WindowUnscaled(owner->RootWindow))
    {
        const ImVec2 o = UnscaledOffset(owner->RootWindow);
        *pos = ImVec2(m.x - o.x, m.y - o.y);
        return;
    }
    *pos = ImVec2(m.x / s_scale, m.y / s_scale);
}

// ==== font twins ================================================================================
// botox dpi twins: main atlas fonts that drew on a dpi scaled list, re-rasterized at size x scale
// (antialiased) in a side atlas. RenderText takes glyph quads from the twin, layout from the base.
// Baked here on the render thread at the frame's safe point (botox: a worker thread). One-off
// cost when the scale or the fonts in use settle, ~tens of ms for a full set.
constexpr int kTwinMax = 16;

struct TwinRequest
{
    float scale = 1.f;
    int count = 0;
    const ImFont* bases[kTwinMax] = {};
    bool operator==(const TwinRequest&) const = default;
};

struct TwinSet
{
    ImFontAtlas* atlas = nullptr;
    float scale = 1.f;
    int count = 0;
    const ImFont* bases[kTwinMax] = {};
    ImFont* fonts[kTwinMax] = {};
};

static TwinSet*    s_twins = nullptr;
static TwinRequest s_wanted, s_seen, s_baked;
static ULONGLONG   s_wantedChangedMs = 0;

static void DropSet(TwinSet* set)
{
    if (!set) return;
    if (set->atlas && set->atlas->TexID) ((IDirect3DTexture9*)set->atlas->TexID)->Release();
    if (set->atlas) IM_DELETE(set->atlas);
    delete set;
}

// botox list_dpi_scaled: true = this list's vertices grow by the scale before they reach the screen
static bool ListScaled(const ImDrawList* list)
{
    ImGuiContext& g = *GImGui;
    const ImGuiViewportP* vp = g.Viewports[0];
    if (list == vp->DrawLists[0]) return s_panelScale != 1.f && s_open[0] > 0;
    if (list == vp->DrawLists[1]) return s_panelScale != 1.f && s_open[1] > 0;

    const ImGuiWindow* w = g.CurrentWindow && g.CurrentWindow->DrawList == list ? g.CurrentWindow : nullptr;
    for (int i = 0; !w && i < g.Windows.Size; ++i)
        if (g.Windows[i]->DrawList == list) w = g.Windows[i];
    return w && !WindowUnscaled(w->RootWindow);
}

// botox botox_dpi_twin
static const ImFont* Twin(const ImFont* font, const ImDrawList* list, const char* text, const char* end)
{
    if (s_scale == 1.f || !font || !GImGui || font->ContainerAtlas != ImGui::GetIO().Fonts || !ListScaled(list))
        return nullptr;

    const ImFont* twin = nullptr;
    if (const TwinSet* set = s_twins; set && set->scale == s_scale && set->atlas->TexID)
        for (int i = 0; i < set->count && !twin; ++i)
            if (set->bases[i] == font) twin = set->fonts[i];

    if (!twin)
    {
        if (s_wanted.scale != s_scale) s_wanted = { s_scale };
        const ImFont** const last = s_wanted.bases + s_wanted.count;
        if (s_wanted.count < kTwinMax && std::find(s_wanted.bases, last, font) == last)
            s_wanted.bases[s_wanted.count++] = font;
        return nullptr;
    }

    // one texture per draw: a glyph only the base has keeps the whole string on the base
    for (const char* s = text; s < end;)
    {
        unsigned int c = (unsigned char)*s;
        s += c < 0x80 ? 1 : ImTextCharFromUtf8(&c, s, end);
        if (c >= 32 && !twin->FindGlyphNoFallback((ImWchar)c) && font->FindGlyphNoFallback((ImWchar)c)) return nullptr;
    }
    return twin;
}

// botox build_dpi_fonts
static bool BuildSet(TwinSet& set, const TwinRequest& req)
{
    ImFontAtlas* const atlas = set.atlas = IM_NEW(ImFontAtlas)();
    atlas->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
    set.scale = req.scale;
    set.count = req.count;
    const float s = set.scale;
    constexpr unsigned mono = ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;

    for (int i = 0; i < set.count; ++i)
    {
        const ImFont* const base = set.bases[i] = req.bases[i];
        for (int c = 0; c < base->ConfigDataCount; ++c)
        {
            ImFontConfig cfg = base->ConfigData[c];
            if (!cfg.FontData) continue;   // input data cleared: nothing to re-raster from
            cfg.FontDataOwnedByAtlas = false;   // the main atlas still owns the bytes
            cfg.DstFont = nullptr;
            cfg.SizePixels *= s;
            cfg.GlyphOffset = ImVec2(cfg.GlyphOffset.x * s, cfg.GlyphOffset.y * s);
            cfg.GlyphExtraSpacing = ImVec2(cfg.GlyphExtraSpacing.x * s, cfg.GlyphExtraSpacing.y * s);
            cfg.GlyphMinAdvanceX *= s;
            if (cfg.GlyphMaxAdvanceX < FLT_MAX) cfg.GlyphMaxAdvanceX *= s;
            // 1 bit glyphs only fit the size they were hinted at: rescaled = antialiased
            if (cfg.FontBuilderFlags & mono)
                cfg.FontBuilderFlags = (cfg.FontBuilderFlags & ~mono) | ImGuiFreeTypeBuilderFlags_LightHinting;
            ImFont* const f = atlas->AddFont(&cfg);
            if (c == 0) set.fonts[i] = f;
        }
        if (!set.fonts[i]) return false;
    }
    return atlas->Build();
}

// 4096 x 4096: a 30 px font at 200 % alone is ~5.5 m px
constexpr float kTwinAtlasMaxPx = 4096.f * 4096.f;

// botox build_dpi_fonts_worker: over budget = drop the biggest font (it stays 100 %) and bake again
static TwinSet* Bake(TwinRequest req)
{
    for (;;)
    {
        TwinSet* set = new TwinSet{};
        if (!BuildSet(*set, req)) { DropSet(set); return nullptr; }
        if (req.count <= 1 || (float)set->atlas->TexWidth * (float)set->atlas->TexHeight <= kTwinAtlasMaxPx) return set;
        DropSet(set);
        const ImFont** const last = req.bases + req.count;
        const ImFont** const big = std::max_element(req.bases, last, [](const ImFont* a, const ImFont* b) { return a->FontSize < b->FontSize; });
        std::move(big + 1, last, big);
        req.bases[--req.count] = nullptr;
    }
}

// MANAGED A8R8G8B8: survives a device Reset. The backend takes the colour from the vertex.
static IDirect3DTexture9* Upload(IDirect3DDevice9* dev, ImFontAtlas* atlas)
{
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    atlas->GetTexDataAsRGBA32(&px, &w, &h);
    if (!dev || !px || w <= 0 || h <= 0) return nullptr;

    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr)) || !t) return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) { t->Release(); return nullptr; }
    for (int y = 0; y < h; ++y)
    {
        const unsigned char* src = px + (size_t)y * w * 4;
        DWORD* dst = (DWORD*)((unsigned char*)lr.pBits + (size_t)lr.Pitch * y);
        for (int x = 0; x < w; ++x, src += 4)
            dst[x] = D3DCOLOR_ARGB(src[3], src[0], src[1], src[2]);
    }
    t->UnlockRect(0);
    return t;
}

void Dpi_DropFonts()
{
    DropSet(s_twins);
    s_twins = nullptr;
    s_wanted = s_seen = s_baked = {};
}

// botox update_dpi_fonts. Render thread, before NewFrame: last frame's lists are presented, so a
// texture swapped here is referenced by nothing but popup ghosts (true = caller drops them).
bool Dpi_UpdateFonts(IDirect3DDevice9* dev)
{
    if (s_scale == 1.f)
    {
        const bool had = s_twins != nullptr;
        Dpi_DropFonts();
        return had;
    }

    const ULONGLONG now = GetTickCount64();
    if (!(s_wanted == s_seen)) { s_seen = s_wanted; s_wantedChangedMs = now; }
    if (!s_wanted.count || s_wanted == s_baked || now - s_wantedChangedMs < 250) return false;

    // dropped, not retried a frame: wanted == baked keeps it on the base fonts until the scale
    // or the fonts in use change
    s_baked = s_wanted;
    const ULONGLONG t0 = GetTickCount64();
    TwinSet* set = Bake(s_wanted);
    if (!set) { DebugLog("dpi: font bake failed -- scaled text stays on the 100 %% fonts"); return false; }
    set->atlas->TexID = Upload(dev, set->atlas);
    if (!set->atlas->TexID)
    {
        DebugLog("dpi: font texture %dx%d failed -- scaled text stays on the 100 %% fonts", set->atlas->TexWidth, set->atlas->TexHeight);
        DropSet(set);
        return false;
    }
    DropSet(s_twins);
    s_twins = set;
    DebugLog("dpi: fonts baked: %d at %.0f %% in %llu ms, atlas %dx%d", set->count, set->scale * 100.f,
             GetTickCount64() - t0, set->atlas->TexWidth, set->atlas->TexHeight);
    return true;
}

// ==== frame =====================================================================================
void Dpi_BeginFrame()
{
    GBotoxDpiTwin = &Twin;
    GBotoxDpiWindowScale = &WindowScale;
    GBotoxDpiMouse = &MouseHook;

    ImGuiIO& io = ImGui::GetIO();
    s_screen = io.DisplaySize;
    s_scale = DpiWantedScale(g_dpiScale);
    s_panelScale = g_dpiScalePanels ? s_scale : 1.f;
    s_open[0] = s_open[1] = 0;

    io.DisplaySize = ImVec2(s_screen.x / s_scale, s_screen.y / s_scale);
    io.DisplayFramebufferScale = ImVec2(s_scale, s_scale);
    // curves tessellate in logical px: finer when they draw bigger, so rounded corners stay round
    ImGui::GetStyle().CircleTessellationMaxError = 0.30f / ImMax(s_scale, 1.f);
}

void Dpi_AfterNewFrame()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 logical = io.DisplaySize;
    io.DisplaySize = s_screen;   // hud code reads it as the screen; imgui read it in NewFrame already
    if (s_scale == 1.f) return;
    // the lists imgui reset to the logical viewport; panels draw in screen px
    for (ImDrawList* l : { ImGui::GetBackgroundDrawList(), ImGui::GetForegroundDrawList() })
        l->PushClipRect(ImVec2(0.f, 0.f), ImMax(s_screen, logical), false);
}

void Dpi_BeforeRender() { ImGui::GetIO().MousePos = s_mouse; }

// botox dpi_scale_overlays, the window half (panel blocks scaled at their own End)
void Dpi_ScaleDrawData(ImDrawData* dd)
{
    if (!dd || s_scale == 1.f) return;
    ImGuiContext& g = *GImGui;
    const ImGuiViewportP* vp = g.Viewports[0];

    for (int i = 0; i < dd->CmdListsCount; ++i)
    {
        ImDrawList* const list = dd->CmdLists[i];
        if (list == vp->DrawLists[0] || list == vp->DrawLists[1]) continue;

        const ImGuiWindow* owner = nullptr;
        for (const ImGuiWindow* w : g.Windows)
            if (w->Active && w->DrawList == list) { owner = w; break; }

        if (owner && WindowUnscaled(owner->RootWindow))
        {
            const ImVec2 o = UnscaledOffset(owner->RootWindow);
            for (ImDrawVert& v : list->VtxBuffer) { v.pos.x += o.x; v.pos.y += o.y; }
            for (ImDrawCmd& c : list->CmdBuffer) { c.ClipRect.x += o.x; c.ClipRect.y += o.y; c.ClipRect.z += o.x; c.ClipRect.w += o.y; }
            continue;
        }
        for (ImDrawVert& v : list->VtxBuffer) { v.pos.x *= s_scale; v.pos.y *= s_scale; }
        for (ImDrawCmd& c : list->CmdBuffer) { c.ClipRect.x *= s_scale; c.ClipRect.y *= s_scale; c.ClipRect.z *= s_scale; c.ClipRect.w *= s_scale; }
    }
    dd->DisplaySize = s_screen;
}

// ==== panels ====================================================================================
static int ListSlot(const ImDrawList* list)
{
    const ImGuiViewportP* vp = GImGui->Viewports[0];
    return list == vp->DrawLists[0] ? 0 : list == vp->DrawLists[1] ? 1 : -1;
}

DpiPanel Dpi_PanelBegin(ImDrawList* list)
{
    DpiPanel p{ nullptr, 0, 0 };
    const int slot = list ? ListSlot(list) : -1;
    if (s_panelScale == 1.f || slot < 0) return p;
    p.list = list;
    p.v0 = list->VtxBuffer.Size;
    p.c0 = ImMax(list->CmdBuffer.Size - 1, 0);
    ++s_open[slot];
    return p;
}

void Dpi_PanelEnd(DpiPanel& p)
{
    ImDrawList* const list = p.list;
    if (!list) return;
    p.list = nullptr;
    const int slot = ListSlot(list);
    if (slot >= 0 && s_open[slot] > 0) --s_open[slot];

    const float scale = s_panelScale;
    ImVector<ImDrawVert>& vtx = list->VtxBuffer;
    ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
    for (int v = p.v0; v < vtx.Size; ++v) { lo = ImMin(lo, vtx[v].pos); hi = ImMax(hi, vtx[v].pos); }
    if (lo.x > hi.x) return;   // drew nothing

    const ImVec2 pivot(DpiSnapPivot(DpiPivot((lo.x + hi.x) * 0.5f, s_screen.x), scale),
                       DpiSnapPivot(DpiPivot((lo.y + hi.y) * 0.5f, s_screen.y), scale));
    for (int v = p.v0; v < vtx.Size; ++v)
    {
        ImVec2& pos = vtx[v].pos;
        pos = ImVec2(pivot.x + (pos.x - pivot.x) * scale, pivot.y + (pos.y - pivot.y) * scale);
    }
    // grow only: the first command also covers draws from before the block
    for (int c = p.c0; c < list->CmdBuffer.Size; ++c)
    {
        // every callback from c0 on is this panel's: c0 was the list's last command at Begin, and
        // that is never a callback (AddCallback opens a fresh command after its own)
        BlurRect_ScaleJob(list->CmdBuffer[c], pivot, scale);
        ImVec4& clip = list->CmdBuffer[c].ClipRect;
        clip.x = ImMin(clip.x, pivot.x + (clip.x - pivot.x) * scale);
        clip.y = ImMin(clip.y, pivot.y + (clip.y - pivot.y) * scale);
        clip.z = ImMax(clip.z, pivot.x + (clip.z - pivot.x) * scale);
        clip.w = ImMax(clip.w, pivot.y + (clip.w - pivot.y) * scale);
    }
}

ImVec2 Dpi_PanelPos(ImVec2 pos, ImVec2 size)
{
    if (s_panelScale == 1.f) return pos;
    return ImVec2(DpiAxisShown(pos.x, size.x, s_screen.x, s_panelScale), DpiAxisShown(pos.y, size.y, s_screen.y, s_panelScale));
}

ImVec2 Dpi_PanelLayoutPos(ImVec2 shown, ImVec2 size)
{
    if (s_panelScale == 1.f) return shown;
    return ImVec2(DpiAxisLayout(shown.x, size.x, s_screen.x, s_panelScale), DpiAxisLayout(shown.y, size.y, s_screen.y, s_panelScale));
}

// botox panel_mouse: undoes the aspect overlay stretch (it runs last, pivot from the drawn block), then dpi
ImVec2 Dpi_PanelMouse(ImVec2 pos, ImVec2 size)
{
    const ImVec2 shown = Dpi_PanelPos(pos, size);
    const float dpi = s_panelScale;
    ImVec2 m = s_mouse;
    if (g_aspectRatioEnable && g_sfxAspectCheatOverlay && !g_eject)
    {
        const float ratio = g_aspectRatioValue < 0.5f ? 0.5f : (g_aspectRatioValue > 4.f ? 4.f : g_aspectRatioValue);
        const float stretch = AsScale(s_screen.x, s_screen.y, ratio);
        if (std::fabs(stretch - 1.f) >= 0.001f)
        {
            const float pivot = AsPivot(shown.x, shown.x + size.x * dpi, s_screen.x);
            m.x = pivot + (m.x - pivot) / stretch;
        }
    }
    return ImVec2(pos.x + (m.x - shown.x) / dpi, pos.y + (m.y - shown.y) / dpi);
}
