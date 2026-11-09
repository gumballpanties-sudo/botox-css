#include "indicators.h"
#include "migoo.h"
#include "miwarp.h"
#include "points.h"                 // g_trickFired / g_esFired: the particle burst counts
#include "../movement/movement.h"   // MvInd_t: the movement thread's per-cmd snapshot
#include "../../hooks/crashlog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

// botox hacks/indicators/indicators.cpp, the movement half, 1:1. Port differences (architecture,
// not game-specific -- true on CS:S same as TF2), all data-source:
//   * botox reads the local player on the game thread each paint; here every value comes from the
//     movement snapshot (MV_GetIndicators, published per cmd) and draws in the imgui frame build,
//     so g_render.m_draw_data queue + callbacks are direct draws into the background list
//   * real_time = ImGui::GetTime(), screen = io.DisplaySize, device = g_d3dDev
//   * a fake wall jump (fakemove.inl) is a takeoff too: its speed becomes the pre speed

namespace
{
    struct col_t { float r = 0.f, g = 0.f, b = 0.f, a = 1.f; };

    col_t col_of(const float* c) { return { c[0], c[1], c[2], c[3] }; }

    // botox c_color::get_u32( alpha_multiplier )
    ImU32 u32(const col_t& c, const float am = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * am)); }
    ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

    float rido_lerp(const float current, const float target, const float smooth, const float dt)
    {
        return current + (target - current) * (1.f - std::exp(-smooth * 0.96f * dt));
    }

    ImDrawList* draw_list() { return ImGui::GetBackgroundDrawList(); }
    float screen_w() { return ImGui::GetIO().DisplaySize.x; }
    float screen_h() { return ImGui::GetIO().DisplaySize.y; }
    float real_time() { return (float)ImGui::GetTime(); }

    // label + drop shadow colour. shadow keeps the label's alpha so the fade stays in sync
    struct indicator_colors_t {
        col_t m_text;
        col_t m_shadow;
        col_t m_outline;
    };

    struct shadow_opts_t {
        bool  m_on;
        float m_blur;
        col_t m_color         = { 0.f, 0.f, 0.f, 1.f };
        bool  m_outline       = false;
        col_t m_outline_color = { 0.f, 0.f, 0.f, 1.f };
        int   m_outline_px    = 1;
        float m_off_x         = 1.f;
        float m_off_y         = 1.f;
    };

    col_t shadow_at(const col_t& tint, const float a) { return { tint.r, tint.g, tint.b, a * tint.a }; }
    ImU32 shadow_u32(const col_t& tint, const float a) { return u32(shadow_at(tint, a)); }

    // outline / outline color / outline px / shadow x / shadow y, same shape on every indicator
    shadow_opts_t shadow_opts_of(const bool on, const float blur, const float* color, const bool outline, const float* outline_color,
                                 const int outline_px, const int shadow_x, const int shadow_y)
    {
        return { on, blur, col_of(color), outline, col_of(outline_color), std::clamp(outline_px, 1, 5),
                 (float)shadow_x, (float)shadow_y };
    }

    indicator_colors_t key_colors_plain(const col_t& shadow_tint, const col_t& outline_tint)
    {
        const col_t base = col_of(g_kiColor);
        return { base, shadow_at(shadow_tint, base.a), shadow_at(outline_tint, base.a) };
    }

    indicator_colors_t key_colors_at(const float t, const bool shadow_detect, const col_t& shadow_tint, const bool predicted,
                                     const col_t& outline_tint)
    {
        const col_t base = col_of(g_kiColor);
        const col_t hit  = col_of(predicted ? g_kiColorPredict : g_kiColorSuccess);

        const auto blend = [&](const col_t& from, const col_t& to, const float alpha) {
            return col_t{ std::lerp(from.r, to.r, t), std::lerp(from.g, to.g, t), std::lerp(from.b, to.b, t), alpha };
        };

        if (shadow_detect)
            return { base, blend(shadow_tint, hit, base.a * shadow_tint.a), blend(outline_tint, hit, base.a * outline_tint.a) };

        const col_t text = blend(base, hit, std::lerp(base.a, hit.a, t));
        return { text, shadow_at(shadow_tint, text.a), shadow_at(outline_tint, text.a) };
    }

    struct key_pending_t {
        int         m_id;
        std::string m_text;
        bool        m_detect;
        bool        m_success;
        bool        m_active;
        bool        m_predicted = false;
    };
}

struct mito_ent_t {
    char    m_text[56];
    ImU32   m_col;
    ImU32   m_shadow;
    ImU32   m_outline;
    float   m_anim;
    float   m_raw;
    ImVec2  m_size;
    ImVec2  m_pos;
    float   m_rgb[3];
    ImVec2  m_piv;
    float   m_sx;
    float   m_sy;
    ImFont* m_font;
    float   m_fs;
};

// one frame of tags: laid out, split, drawn
struct mito_frame_t {
    ImFont*    m_font   = nullptr;
    float      m_fs     = 0.f;
    bool       m_axis_x = true;
    float      m_blur   = 0.f;
    bool       m_squash = false;
    ImVec2     m_off    = ImVec2(1.f, 1.f);
    int        m_ring_px = 1;
    int        m_n      = 0;
    mito_ent_t m_ents[LBL_COUNT]{};
    bool       m_in[LBL_COUNT]{};
    int        m_parent[LBL_COUNT]{};
};

struct frame_tex_t {
    IDirect3DTexture9* m_tex = nullptr;
    IDirect3DDevice9*  m_dev = nullptr;
    int  m_dim = 0;
    bool m_dyn = false;
    int  m_w = 0, m_h = 0;
    std::uint64_t m_key = 0;
};

static frame_tex_t s_frame_tex[64];
static int s_frame_tex_next = 0, s_frame_tex_frame = -1;

static void frame_tex_release(frame_tex_t& t)
{
    if (t.m_tex)
        t.m_tex->Release();
    t = {};
}

void Indicators_ReleaseTextures()
{
    for (frame_tex_t& t : s_frame_tex)
        frame_tex_release(t);
}

static frame_tex_t* frame_tex_next()
{
    if (ImGui::GetFrameCount() != s_frame_tex_frame) {
        s_frame_tex_frame = ImGui::GetFrameCount();
        s_frame_tex_next  = 0;
    }
    return s_frame_tex_next < (int)std::size(s_frame_tex) ? &s_frame_tex[s_frame_tex_next] : nullptr;
}

// next slot if it still holds `key` from last frame: no blit / blur / upload. key 0 never matches
static const frame_tex_t* frame_tex_cached(const std::uint64_t key)
{
    frame_tex_t* const t = frame_tex_next();
    if (!t || !key || t->m_key != key || !t->m_tex || t->m_dev != g_d3dDev)
        return nullptr;
    ++s_frame_tex_next;
    return t;
}

// this frame's next texture holding px (w x h A8R8G8B8), drawn with uv ( w, h ) / m_dim, remembered as
// `key` (0 = never reused). null = pool dry or d3d refused it: stage + hr say which (mitosis logs them)
static const frame_tex_t* frame_tex_upload(const unsigned int* px, const int w, const int h, const std::uint64_t key, const char*& stage,
                                           HRESULT& hr)
{
    stage = "pool dry";
    hr    = E_OUTOFMEMORY;
    if (!frame_tex_next())
        return nullptr;

    IDirect3DDevice9* const device = g_d3dDev;
    stage = "no device";
    hr    = E_POINTER;
    if (!device)
        return nullptr;

    int dim = 32;
    while (dim < w || dim < h)
        dim <<= 1;

    stage = "too big";
    hr    = E_INVALIDARG;
    if (dim > 1024)
        return nullptr;

    frame_tex_t& t = s_frame_tex[s_frame_tex_next];
    if (t.m_dev != device || t.m_dim < dim) {
        frame_tex_release(t);

        stage = "CreateTexture";
        hr    = device->CreateTexture(dim, dim, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.m_tex, nullptr);
        if (FAILED(hr) || !t.m_tex) {
            t.m_tex = nullptr;
            t.m_dyn = true;
            hr      = device->CreateTexture(dim, dim, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.m_tex, nullptr);
        }
        if (FAILED(hr) || !t.m_tex) {
            frame_tex_release(t);
            return nullptr;
        }
        t.m_dev = device;
        t.m_dim = dim;
    }

    // WHOLE level, never a sub-rect: DISCARD with a rect is D3DERR_INVALIDCALL on the DYNAMIC one
    stage   = "LockRect";
    t.m_key = 0;
    D3DLOCKED_RECT lr{};
    hr = t.m_tex->LockRect(0, &lr, nullptr, t.m_dyn ? D3DLOCK_DISCARD : 0);
    if (FAILED(hr))
        return nullptr;

    // DISCARD leaves the rest undefined: clear the one texel column and row just past the box, the
    // only ones its edge can ever reach
    for (int y = 0; y < h; ++y) {
        unsigned int* row = (unsigned int*)((unsigned char*)lr.pBits + (size_t)lr.Pitch * y);
        std::memcpy(row, px + (size_t)w * y, (size_t)w * 4);
        if (w < t.m_dim)
            row[w] = 0;
    }
    if (h < t.m_dim)
        std::memset((unsigned char*)lr.pBits + (size_t)lr.Pitch * h, 0, (size_t)(w < t.m_dim ? w + 1 : w) * 4);
    t.m_tex->UnlockRect(0);

    t.m_w   = w;
    t.m_h   = h;
    t.m_key = key;
    ++s_frame_tex_next;
    return &t;
}

static std::vector<unsigned char> s_soft_cov;
static std::vector<float>         s_soft_a, s_soft_b;
static std::vector<unsigned int>  s_soft_px;

static int soft_radii(const float sigma, int r[3])
{
    const float ideal = std::sqrt(4.f * sigma * sigma + 1.f);
    int wl = (int)std::floor(ideal);
    if (wl % 2 == 0)
        --wl;

    const int m = (int)std::round((12.f * sigma * sigma - 3.f * wl * wl - 12.f * wl - 9.f) / (-4.f * wl - 4.f));
    for (int i = 0; i < 3; ++i)
        r[i] = ((i < m ? wl : wl + 2) - 1) / 2;

    return r[0] + r[1] + r[2];
}

static void box_pass(const float* src, float* dst, const int count, const int n, const int step, const int line_step, const int r)
{
    const float inv = 1.f / (float)(2 * r + 1);

    for (int l = 0; l < count; ++l) {
        const float* s = src + (size_t)l * line_step;
        float*       d = dst + (size_t)l * line_step;

        float acc = 0.f;
        for (int i = 0; i < r && i < n; ++i)
            acc += s[i * step];

        for (int i = 0; i < n; ++i) {
            if (i + r < n)
                acc += s[(i + r) * step];
            d[i * step] = acc * inv;
            if (i - r >= 0)
                acc -= s[(i - r) * step];
        }
    }
}

static void soft_image(ImDrawList* dl, const frame_tex_t& tex, const ImVec2 org, const ImU32 color)
{
    const float d = (float)tex.m_dim;
    dl->AddImage((ImTextureID)tex.m_tex, org, ImVec2(org.x + tex.m_w, org.y + tex.m_h), ImVec2(0.f, 0.f),
                 ImVec2(tex.m_w / d, tex.m_h / d), color);
}

static bool soft_draw(ImDrawList* dl, const int bw, const int bh, const ImVec2 org, const ImU32 color, const int r[3], const std::uint64_t key)
{
    const size_t n = (size_t)bw * bh;
    s_soft_b.resize(n);

    float *a = s_soft_a.data(), *b = s_soft_b.data();
    for (int i = 0; i < 3; ++i) {
        box_pass(a, b, bh, bw, 1, bw, r[i]);
        std::swap(a, b);
    }
    for (int i = 0; i < 3; ++i) {
        box_pass(a, b, bw, bh, bw, 1, r[i]);
        std::swap(a, b);
    }

    s_soft_px.resize(n);
    for (size_t i = 0; i < n; ++i)
        s_soft_px[i] = ((unsigned int)(std::clamp(a[i], 0.f, 1.f) * 255.f + 0.5f) << 24) | 0x00FFFFFFu;

    const char* stage = nullptr;
    HRESULT     hr    = S_OK;
    const frame_tex_t* const tex = frame_tex_upload(s_soft_px.data(), bw, bh, key, stage, hr);
    if (!tex)
        return false;

    soft_image(dl, *tex, org, color);
    return true;
}

static bool soft_text(ImDrawList* dl, ImFont* font, const float fs, const ImVec2 pos, const char* text, const ImU32 color, const float blur)
{
    int r[3];
    const int pad = soft_radii(blur * 0.5f, r);
    if (pad <= 0 || !font->ContainerAtlas || !font->ContainerAtlas->TexPixelsAlpha8)
        return false;

    if (!(color & IM_COL32_A_MASK))
        return true;

    const ImVec2 size   = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
    const int    margin = pad + 1 + (int)std::ceil(fs * 0.125f);
    const int x0 = (int)std::floor(pos.x) - margin, y0 = (int)std::floor(pos.y) - margin;
    const int bw = (int)std::ceil(pos.x + size.x) + margin - x0;
    const int bh = (int)std::ceil(pos.y + size.y) + margin - y0;
    if (bw > 1024 || bh > 1024)
        return false;

    std::uint64_t key = 0xCBF29CE484222325ull;
    const auto mix = [&](const void* data, const size_t bytes) {
        for (size_t i = 0; i < bytes; ++i)
            key = (key ^ ((const unsigned char*)data)[i]) * 0x100000001B3ull;
    };
    const float frac[2]   = { pos.x - std::floor(pos.x), pos.y - std::floor(pos.y) };
    const void* atlas[2]  = { font->ContainerAtlas, font->ContainerAtlas->TexID };
    mix(text, std::strlen(text) + 1);
    mix(atlas, sizeof(atlas));
    mix(&fs, sizeof(fs));
    mix(&size.x, sizeof(size.x));
    mix(&blur, sizeof(blur));
    mix(frac, sizeof(frac));

    const ImVec2 org((float)x0, (float)y0);
    if (const frame_tex_t* const hit = frame_tex_cached(key)) {
        soft_image(dl, *hit, org, color);
        return true;
    }

    const size_t n = (size_t)bw * bh;
    s_soft_cov.assign(n, 0);
    if (!mig::BlitLabel(s_soft_cov.data(), bw, bh, org, font, fs, text, pos))
        return false;

    s_soft_a.resize(n);
    for (size_t i = 0; i < n; ++i)
        s_soft_a[i] = s_soft_cov[i] / 255.f;

    return soft_draw(dl, bw, bh, org, color, r, key);
}

static bool soft_picture(ImDrawList* dl, const unsigned int* px, const int w, const int h, const ImVec2 at, const ImU32 color, const float blur)
{
    int r[3];
    const int border = soft_radii(blur * 0.5f, r) + 1;
    if (border <= 1)
        return false;

    const int bw = w + 2 * border, bh = h + 2 * border;
    s_soft_a.assign((size_t)bw * bh, 0.f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            s_soft_a[(size_t)(y + border) * bw + x + border] = (px[(size_t)y * w + x] >> 24) / 255.f;

    return soft_draw(dl, bw, bh, ImVec2(at.x - border, at.y - border), color, r, 0);
}

// every offset inside a px-radius disc (round corners), centre skipped. px 1 = 8 neighbours
template <class F>
static void ring_each(const int px, F&& f)
{
    for (int dy = -px; dy <= px; ++dy)
        for (int dx = -px; dx <= px; ++dx)
            if ((dx || dy) && dx * dx + dy * dy <= px * px + px)
                f((float)dx, (float)dy);
}

// one label. blur 0 = hard 1px shadow; blur > 0 = the soft copy (a blurred texture). fs = draw size
// (font size x the indicator's scale)
static void push_label(ImFont* font, const float fs, const ImVec2& pos, const std::string& text, const ImU32 color, const ImU32 shadow_color,
                       const shadow_opts_t& shadow, ImU32 outline_color = 0u)
{
    if (!font)
        return;
    ImDrawList* const dl = draw_list();

    // no lit colour from the caller = plain outline colour, faded with the label
    if (shadow.m_outline && !outline_color)
        outline_color = shadow_u32(shadow.m_outline_color, (float)((color >> IM_COL32_A_SHIFT) & 0xFFu) / 255.f);

    const bool  ring = shadow.m_outline && (outline_color & IM_COL32_A_MASK);
    const bool  drop = shadow.m_on;
    const float size = fs > 0.f ? fs : font->FontSize;

    dl->PushTextureID(font->ContainerAtlas->TexID);
    if (!ring && (!drop || (shadow.m_blur <= 0.f && shadow.m_off_x == 1.f && shadow.m_off_y == 1.f))) {
        // botox render.text, text_flag_dropshadow = one copy at +1,+1
        if (drop)
            dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y + 1.f), shadow_color, text.c_str());
        dl->AddText(font, size, pos, color, text.c_str());
        dl->PopTextureID();
        return;
    }
    dl->PopTextureID();

    // order: shadow, ring, label
    const float  blur = drop ? shadow.m_blur : 0.f;
    const ImVec2 off(shadow.m_off_x, shadow.m_off_y);
    const int    ring_px = ring ? shadow.m_outline_px : 0;

    const ImVec2 sp(std::floor(pos.x) + off.x, std::floor(pos.y) + off.y);
    const bool   soft = drop && blur > 0.f && soft_text(dl, font, size, sp, text.c_str(), shadow_color, blur);

    dl->PushTextureID(font->ContainerAtlas->TexID);
    if (drop && !soft)
        dl->AddText(font, size, ImVec2(pos.x + off.x, pos.y + off.y), shadow_color, text.c_str());
    ring_each(ring_px, [&](const float dx, const float dy) {
        dl->AddText(font, size, ImVec2(pos.x + dx, pos.y + dy), outline_color, text.c_str());
    });
    dl->AddText(font, size, pos, color, text.c_str());
    dl->PopTextureID();
}

constexpr float k_kami_ghost_life = 0.5f;

static float kami_rate(float dt)
{
    if (dt <= 0.f || dt > 0.1f)
        dt = 0.016f;
    return 1.f - std::exp(-12.f * dt);
}

static void kami_ghost_draw(ImFont* font, const float fs, const std::string& text, const ImVec2& at, const float age, const float rise)
{
    const float t = std::clamp(age / k_kami_ghost_life, 0.f, 1.f);
    push_label(font, fs, ImVec2(at.x, at.y - rise * t), text, u32(col_of(g_indKamiShadowColor), 1.f - t), 0u, { false, 0.f });
}

static bool draw_mitosis(ImDrawList* dl, ImFont* font, const float fs, const mig::Tag* tags, const int n, const bool axis_x, const ImU32 tint,
                         const ImU32 shadow_tint, const float blur, const ImVec2 off)
{
    static mig::Work s_wk;
    static unsigned  s_noted = 0;
    const auto first = [&](const unsigned bit) {
        const bool f = !(s_noted & bit);
        s_noted |= bit;
        return f;
    };

    ImFontAtlas* atlas = font ? font->ContainerAtlas : nullptr;
    if (!atlas || !atlas->TexPixelsAlpha8) {
        if (first(1))
            CrashLog_Note("mitosis: text fallback -- no atlas CPU pixels (atlas %p)", (void*)atlas);
        return false;
    }

    int ox = 0, oy = 0, w = 0, h = 0;
    if (!mig::Render(tags, n, font, fs, axis_x, s_wk, ox, oy, w, h)) {
        if (first(2))
            CrashLog_Note("mitosis: text fallback -- Render refused (%d tags, box %dx%d, fs %.1f)", n, w, h, fs);
        return false;
    }

    const char* stage = nullptr;
    HRESULT     hr    = S_OK;
    const frame_tex_t* const tex = frame_tex_upload(s_wk.px.data(), w, h, 0, stage, hr);
    if (!tex) {
        if (first(4))
            CrashLog_Note("mitosis: text fallback -- texture %dx%d %s (hr 0x%08lX)", w, h, stage, (unsigned long)hr);
        return false;
    }

    if (first(16))
        CrashLog_Note("mitosis: drawing (%d tags, box %dx%d at %d,%d, tex %dx%d %s)", n, w, h, ox, oy, tex->m_dim, tex->m_dim,
                      tex->m_dyn ? "dynamic" : "managed");

    const ImTextureID id = (ImTextureID)tex->m_tex;
    const ImVec2 p0((float)ox, (float)oy), p1((float)(ox + w), (float)(oy + h));
    const ImVec2 uv1((float)w / tex->m_dim, (float)h / tex->m_dim);

    if (shadow_tint & IM_COL32_A_MASK) {
        const ImVec2 s0(p0.x + off.x, p0.y + off.y);
        if (!(blur > 0.f && soft_picture(dl, s_wk.px.data(), w, h, s0, shadow_tint, blur)))
            dl->AddImage(id, s0, ImVec2(p1.x + off.x, p1.y + off.y), ImVec2(0.f, 0.f), uv1, shadow_tint);
    }
    dl->AddImage(id, p0, p1, ImVec2(0.f, 0.f), uv1, tint);
    return true;
}

static void scale_verts(ImDrawList* dl, const int vtx0, const ImVec2 piv, const float sx, const float sy)
{
    ImDrawVert* v   = dl->VtxBuffer.Data + vtx0;
    ImDrawVert* end = dl->VtxBuffer.Data + dl->VtxBuffer.Size;
    for (; v < end; ++v) {
        v->pos.x = piv.x + (v->pos.x - piv.x) * sx;
        v->pos.y = piv.y + (v->pos.y - piv.y) * sy;
    }
}

// picture for split tags, text pass for the rest. shadow + label share one warp; SubPixelText
// stops gliding tags stair-stepping
static void draw_mito_frame(ImDrawList* dl, const mito_frame_t& f)
{
    ImFont* const font = f.m_font;
    if (!font)
        return;

    const float fs = f.m_fs;
    const int   n  = f.m_n;

    mig::Tag tags[LBL_COUNT];
    int      map[LBL_COUNT]{};
    int      nt      = 0;
    unsigned tint_a  = 0;
    unsigned shade_a = 0;

    for (int i = 0; i < n; ++i)
        if (f.m_in[i])
            map[i] = nt++;

    for (int i = 0; i < n; ++i) {
        if (!f.m_in[i])
            continue;

        const mito_ent_t& e = f.m_ents[i];
        mig::Tag&         t = tags[map[i]];
        t.text   = e.m_text;
        t.pos    = e.m_pos;
        t.size   = e.m_size;
        t.raw    = e.m_raw;
        t.col[0] = e.m_rgb[0];
        t.col[1] = e.m_rgb[1];
        t.col[2] = e.m_rgb[2];
        t.parent = f.m_parent[i] >= 0 ? map[f.m_parent[i]] : f.m_parent[i];
        t.font   = e.m_font;
        t.fs     = e.m_fs;

        tint_a  = std::max(tint_a, (e.m_col >> IM_COL32_A_SHIFT) & 0xFFu);
        shade_a = std::max(shade_a, (e.m_shadow >> IM_COL32_A_SHIFT) & 0xFFu);
    }

    bool imaged[LBL_COUNT]{};
    if (nt && !f.m_squash &&
        draw_mitosis(dl, font, fs, tags, nt, f.m_axis_x, IM_COL32(255, 255, 255, tint_a), IM_COL32(0, 0, 0, shade_a), f.m_blur, f.m_off))
        for (int i = 0; i < n; ++i)
            imaged[i] = f.m_in[i];

    for (int i = 0; i < n; ++i) {
        const mito_ent_t& e = f.m_ents[i];
        if (imaged[i] || e.m_raw <= 0.001f)
            continue;

        ImFont* const ef  = e.m_font;
        const float   efs = e.m_fs;

        const int    vs   = dl->VtxBuffer.Size;
        const ImVec2 sp   = ImVec2(e.m_pos.x + f.m_off.x, e.m_pos.y + f.m_off.y);
        const bool   hard = (e.m_shadow & IM_COL32_A_MASK) && !(f.m_blur > 0.f && soft_text(dl, ef, efs, sp, e.m_text, e.m_shadow, f.m_blur));

        dl->PushTextureID(ef->ContainerAtlas->TexID);
        const int v0 = dl->VtxBuffer.Size;
        if (hard)
            dl->AddText(ef, efs, sp, e.m_shadow, e.m_text);
        if (e.m_outline & IM_COL32_A_MASK)
            ring_each(f.m_ring_px, [&](const float dx, const float dy) {
                dl->AddText(ef, efs, ImVec2(e.m_pos.x + dx, e.m_pos.y + dy), e.m_outline, e.m_text);
            });
        dl->AddText(ef, efs, e.m_pos, e.m_col, e.m_text);
        mig::SubPixelText(dl, v0, e.m_pos, ef);
        if (e.m_sx != 1.f || e.m_sy != 1.f)
            scale_verts(dl, vs, e.m_piv, e.m_sx, e.m_sy);
        dl->PopTextureID();
    }
}

constexpr float k_squash_stretch = 0.35f;
constexpr float k_squash_recoil  = 0.12f;

static void mito_squash(mito_frame_t& f)
{
    constexpr float pi = 3.14159265f;
    const bool ax = f.m_axis_x;

    ImVec2 ctr[LBL_COUNT];
    for (int i = 0; i < f.m_n; ++i)
        ctr[i] = ImVec2(f.m_ents[i].m_pos.x + f.m_ents[i].m_size.x * 0.5f, f.m_ents[i].m_pos.y + f.m_ents[i].m_fs * 0.5f);

    for (int i = 0; i < f.m_n; ++i) {
        const int par = f.m_parent[i];
        if (par == -2)
            continue;

        mito_ent_t& e = f.m_ents[i];
        const float t = mig::Sat(e.m_raw);
        const float g = std::max(mig::EaseOut(t), 0.f);
        const float s = 1.f + k_squash_stretch * std::sin(pi * t);
        ImVec2      c = ctr[i];

        if (par >= 0) {
            const float k = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
            c = ImVec2(ctr[par].x + (ctr[i].x - ctr[par].x) * k, ctr[par].y + (ctr[i].y - ctr[par].y) * k);
            e.m_pos.x += c.x - ctr[i].x;
            e.m_pos.y += c.y - ctr[i].y;

            mito_ent_t& p = f.m_ents[par];
            if (f.m_parent[par] == -2) {
                const float r    = 1.f + k_squash_recoil * std::sin(pi * t);
                const float pc   = ax ? ctr[par].x : ctr[par].y;
                const float oc   = ax ? ctr[i].x : ctr[i].y;
                const float half = ax ? p.m_size.x * 0.5f : p.m_fs * 0.5f;
                const float piv  = pc - (oc >= pc ? half : -half);
                p.m_piv = ax ? ImVec2(piv, ctr[par].y) : ImVec2(ctr[par].x, piv);
                p.m_sx  = ax ? r : 1.f / r;
                p.m_sy  = ax ? 1.f / r : r;
            }
        }

        e.m_piv = c;
        e.m_sx  = ax ? g * s : g / s;
        e.m_sy  = ax ? g / s : g * s;
    }
}

// botox mitosis_queue: parents + warp + squash, then (here: straight away) the draw
static void mitosis_queue(mito_frame_t& f)
{
    const int n = f.m_n;
    if (n <= 0)
        return;

    const float fs   = f.m_fs;
    mito_ent_t* ents = f.m_ents;
    const auto  centre = [&](const int i) { return ImVec2(ents[i].m_pos.x + ents[i].m_size.x * 0.5f, ents[i].m_pos.y + ents[i].m_fs * 0.5f); };

    float raw[LBL_COUNT];
    for (int i = 0; i < n; ++i) {
        raw[i]    = ents[i].m_raw;
        f.m_in[i] = false;
    }

    for (int i = 0; i < n; ++i) {
        f.m_parent[i] = -2;
        if (raw[i] >= 0.995f)
            continue;

        const int par = mig::PickParent(raw, n, i);
        f.m_parent[i] = par;

        if (par == -1) {
            ents[i].m_piv = centre(i);
            ents[i].m_sx = ents[i].m_sy = mig::PopAt(raw[i]);
            f.m_in[i] = raw[i] > 0.001f;
            continue;
        }

        f.m_in[i] = f.m_in[par] = true;

        for (const int k : { i, par }) {
            const ImVec2 c = centre(k), o = centre(k == i ? par : i);
            mito_ent_t&  e = ents[k];

            if (f.m_axis_x) {
                const miw::Warp w = miw::SplitWarp(c.x, e.m_size.x * 0.5f, o.x, raw[i]);
                e.m_piv = ImVec2(w.piv, c.y);
                e.m_sx  = w.sa;
                e.m_sy  = w.sp;
            } else {
                const miw::Warp w = miw::SplitWarp(c.y, e.m_fs * 0.5f, o.y, raw[i]);
                e.m_piv = ImVec2(c.x, w.piv);
                e.m_sy  = w.sa;
                e.m_sx  = w.sp;
            }
        }
    }

    if (f.m_squash)
        mito_squash(f);

    static bool s_was_moving[2]{};
    bool moving = false;
    for (int i = 0; i < n; ++i)
        moving |= f.m_parent[i] != -2;
    if (moving && !s_was_moving[f.m_axis_x])
        for (int i = 0; i < n; ++i)
            if (f.m_parent[i] != -2)
                DebugLog("[mito] %s start n=%d i=%d '%s' par=%d raw=%.2f pos=%.1f,%.1f size=%.1fx%.1f fs=%.1f", f.m_axis_x ? "row" : "stack", n, i,
                         ents[i].m_text, f.m_parent[i], raw[i], ents[i].m_pos.x, ents[i].m_pos.y, ents[i].m_size.x, ents[i].m_size.y, fs);
    s_was_moving[f.m_axis_x] = moving;

    draw_mito_frame(draw_list(), f);
}

static void mito_add_ent(mito_frame_t& f, const char* text, const ImU32 col, const ImU32 shadow, const float r, const float g, const float b,
                         const float raw, const float anim, ImFont* const font = nullptr, const float fs = 0.f, const ImU32 outline = 0u)
{
    if (f.m_n >= LBL_COUNT)
        return;

    mito_ent_t& e = f.m_ents[f.m_n++];
    std::snprintf(e.m_text, sizeof(e.m_text), "%s", text);
    e.m_col     = col;
    e.m_shadow  = shadow;
    e.m_outline = outline;
    e.m_raw     = raw;
    e.m_anim    = anim;
    e.m_font    = font ? font : f.m_font;
    e.m_fs      = font ? fs : f.m_fs;
    e.m_size    = e.m_font->CalcTextSizeA(e.m_fs, FLT_MAX, 0.f, e.m_text);
    e.m_rgb[0]  = r;
    e.m_rgb[1]  = g;
    e.m_rgb[2]  = b;
    e.m_piv     = ImVec2(0.f, 0.f);
    e.m_sx = e.m_sy = 1.f;
}

static void mito_layout_row(mito_frame_t& f, const float spacing_raw, const float y_raw)
{
    const auto  even    = [](const float v) { return 2.f * std::ceil(v * 0.5f); };
    const float spacing = even(spacing_raw);

    float total = 0.f;
    for (int i = 0; i < f.m_n; ++i) {
        if (i)
            total += spacing * f.m_ents[i].m_anim;
        total += even(f.m_ents[i].m_size.x) * f.m_ents[i].m_anim;
    }

    const float y = std::floor(y_raw);
    float       x = std::floor(screen_w() * 0.5f) - total * 0.5f;

    for (int i = 0; i < f.m_n; ++i) {
        mito_ent_t& e = f.m_ents[i];
        if (i)
            x += spacing * e.m_anim;

        const float w    = even(e.m_size.x);
        const float slot = w * e.m_anim;
        e.m_pos = ImVec2(x + (slot - w) * 0.5f + std::floor((w - e.m_size.x) * 0.5f), y);
        x += slot;
    }
}

// ==== detection particles ======================================================================
struct key_particle_t {
    float  m_x, m_y, m_vx, m_vy;
    float  m_life, m_max_life;
    float  m_radius;
    ImVec4 m_color;
};

constexpr float  k_part_gap = 0.25f;
constexpr size_t k_part_max = 600;

static std::vector<key_particle_t> s_key_particles;
static bool   s_part_was[LBL_COUNT]{};
static bool   s_part_want[LBL_COUNT]{};
static float  s_part_want_at[LBL_COUNT]{};
static float  s_part_last[LBL_COUNT]{};
static bool   s_part_seen[LBL_COUNT]{};
static ImVec2 s_part_at[LBL_COUNT]{};

static float part_rand(const float lo, const float hi)
{
    static std::mt19937 gen(std::random_device{}());
    return std::uniform_real_distribution<float>(lo, hi)(gen);
}

static void part_mark(const int id, const float x, const float y, const ImVec2& size)
{
    s_part_seen[id] = true;
    s_part_at[id]   = ImVec2(x + size.x * 0.5f, y + size.y * 0.5f);
}

// the label's PERFORMED count: detections' fires off cmd STARTs (Points_OnTrick), the same ones chat
// uses, so a plan / sim lighting the label never bursts. -1 = no physical detect, the label's
// success edge bursts
static int part_fired(const int id)
{
    const auto trick = [](const int t) { return g_trickFired[t].load(); };
    switch (id) {
    case LBL_EB:   return trick(PTS_EDGE_BUG);
    case LBL_PS:
    case LBL_AST:  return trick(PTS_PIXEL_SURF);
    case LBL_JB:   return trick(PTS_JUMP_BUG);
    case LBL_TB:   return trick(PTS_TEXTURE_BUG);
    case LBL_AIR:  return trick(PTS_AIR_STUCK);
    case LBL_WC:   return trick(PTS_WALL_CLIMB);
    case LBL_FR:   return trick(PTS_FIREMAN);
    case LBL_ES:   return g_esFired.load();
    default:       return -1;
    }
}
static int s_part_fired[LBL_COUNT]{};

static void part_track(const std::vector<key_pending_t>& pending, const float real)
{
    const float cooldown = std::clamp(g_kiParticleCooldown, 0.f, 5.f);

    // every label, listed or not: a fire while hidden never bursts later
    bool fired[LBL_COUNT]{};
    for (int id = 0; id < LBL_COUNT; ++id) {
        const int n      = part_fired(id);
        fired[id]        = n >= 0 && n != s_part_fired[id];
        s_part_fired[id] = n;
    }

    bool listed[LBL_COUNT]{};
    for (const auto& p : pending) {
        const int id = p.m_id;
        listed[id] = true;

        const bool phys = s_part_fired[id] >= 0;
        const bool hit  = p.m_detect && p.m_active && (phys ? fired[id] : p.m_success);
        if (hit && (phys || !s_part_was[id]) && (real - s_part_last[id] >= cooldown || real < s_part_last[id])) {
            s_part_want[id]    = true;
            s_part_want_at[id] = real;
            s_part_last[id]    = real;
        }
        s_part_was[id] = hit;
    }

    for (int id = 0; id < LBL_COUNT; ++id)
        if (!listed[id])
            s_part_was[id] = s_part_want[id] = false;
}

static void part_burst(const ImVec2& at)
{
    const int    amount = std::clamp(g_kiParticleAmount, 1, 60);
    const float  size   = g_kiParticleSize;
    const float  speed  = g_kiParticleSpeed;
    const bool   random = g_kiParticleRandom;
    const float  life   = std::clamp(g_kiParticleLife, 0.05f, 10.f);
    const ImVec4 picked(g_kiParticleColor[0], g_kiParticleColor[1], g_kiParticleColor[2], g_kiParticleColor[3]);

    const size_t over = s_key_particles.size() + (size_t)amount;
    if (over > k_part_max)
        s_key_particles.erase(s_key_particles.begin(), s_key_particles.begin() + (std::ptrdiff_t)(over - k_part_max));

    for (int i = 0; i < amount; ++i) {
        key_particle_t p;
        const float ang = part_rand(0.f, 6.2831853f);
        const float mag = speed * part_rand(0.45f, 1.f);
        p.m_x  = at.x;
        p.m_y  = at.y;
        p.m_vx = std::cos(ang) * mag;
        p.m_vy = std::sin(ang) * mag;
        p.m_life = p.m_max_life = life * part_rand(0.7f, 1.f);
        p.m_radius = std::max(1.f, size * part_rand(0.7f, 1.3f));

        if (random) {
            p.m_color = ImVec4(0.f, 0.f, 0.f, 1.f);
            ImGui::ColorConvertHSVtoRGB(part_rand(0.f, 1.f), part_rand(0.6f, 0.9f), 1.f, p.m_color.x, p.m_color.y, p.m_color.z);
        } else
            p.m_color = picked;

        s_key_particles.push_back(p);
    }
}

static void key_particles()
{
    if (!g_kiEnable || !g_kiParticles) {
        s_key_particles.clear();
        std::fill(std::begin(s_part_was), std::end(s_part_was), false);
        std::fill(std::begin(s_part_want), std::end(s_part_want), false);
        std::fill(std::begin(s_part_seen), std::end(s_part_seen), false);
        // fires while off never burst once it's back on
        for (int id = 0; id < LBL_COUNT; ++id)
            s_part_fired[id] = part_fired(id);
        return;
    }

    const float real = real_time();
    for (int id = 0; id < LBL_COUNT; ++id) {
        if (!s_part_want[id])
            continue;

        const float age = real - s_part_want_at[id];
        if (s_part_seen[id] && age >= 0.f && age <= k_part_gap)
            part_burst(s_part_at[id]);
        if (s_part_seen[id] || age < 0.f || age > k_part_gap)
            s_part_want[id] = false;
    }
    std::fill(std::begin(s_part_seen), std::end(s_part_seen), false);

    const float dt      = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    const float gravity = g_kiParticleGravity;

    ImDrawList* const dl = draw_list();
    size_t live = 0;
    for (auto& p : s_key_particles) {
        p.m_life -= dt;
        if (p.m_life <= 0.f)
            continue;

        p.m_vy += gravity * dt;
        p.m_x += p.m_vx * dt;
        p.m_y += p.m_vy * dt;

        ImVec4 c = p.m_color;
        c.w *= std::clamp(p.m_life / p.m_max_life, 0.f, 1.f);
        dl->AddCircleFilled(ImVec2(p.m_x, p.m_y), p.m_radius, ImGui::ColorConvertFloat4ToU32(c), 0);
        s_key_particles[live++] = p;
    }
    s_key_particles.resize(live);
}

// ==== smooth style =============================================================================
struct smooth_tag_t {
    int         m_id = 0;
    std::string m_text = {};
    ImVec2      m_size = {};
    bool        m_has_detect = false;
    bool        m_predicted  = false;

    float m_opacity = 0.f, m_target_opacity = 1.f;
    float m_x = 0.f, m_target_x = 0.f, m_spawn_x = 0.f;
    float m_y = 0.f, m_target_y = 0.f;
    float m_spawn_y  = 0.f;
    bool  m_removing = false;
    bool  m_fresh    = false;

    float m_detect = 0.f, m_target_detect = 0.f;
    float m_appear_until = 0.f, m_detect_hold_until = 0.f;
};

static std::vector<smooth_tag_t> s_smooth_tags;
static float s_smooth_clock = 0.f;

constexpr float k_smooth_opacity_in = 22.f, k_smooth_opacity_out = 23.5f, k_smooth_position = 9.5f, k_smooth_appear_delay = 0.f;
constexpr float k_smooth_spawn_base = 900.f, k_smooth_spawn_step = 25.f;
constexpr float k_smooth_spawn_drop = 60.f;

static float smooth_spawn_x(const size_t active)
{
    const float W     = screen_w();
    const float scale = W > 0.f ? W / 1920.f : 1.f;
    return W - (k_smooth_spawn_base - k_smooth_spawn_step * (float)(active > 1 ? active - 2 : 0)) * scale;
}

static void smooth_tag_step(smooth_tag_t& t, const bool company, const float now, const float dt, const bool axis_y = false)
{
    float position_smooth = k_smooth_position;
    if (t.m_removing && company) {
        const float to_go          = axis_y ? t.m_target_y - t.m_y : t.m_target_x - t.m_x;
        const float total_distance = std::max(1.f, std::fabs(to_go) + 220.f);
        const float remaining      = std::clamp(to_go / total_distance, 0.f, 1.f);
        position_smooth *= 0.7f + 1.5f * remaining;
    }

    const float opacity_target = (!t.m_removing && now < t.m_appear_until) ? 0.f : t.m_target_opacity;
    t.m_opacity = rido_lerp(t.m_opacity, opacity_target, t.m_removing ? k_smooth_opacity_out : k_smooth_opacity_in, dt);
    t.m_x       = rido_lerp(t.m_x, t.m_target_x, axis_y ? k_smooth_position : position_smooth, dt);
    t.m_y       = rido_lerp(t.m_y, t.m_target_y, axis_y ? position_smooth : k_smooth_position, dt);

    if (std::fabs(t.m_target_x - t.m_x) <= 0.75f)
        t.m_x = t.m_target_x;
    if (std::fabs(t.m_target_y - t.m_y) <= 0.75f)
        t.m_y = t.m_target_y;
    if (std::fabs(t.m_target_opacity - t.m_opacity) <= 0.015f)
        t.m_opacity = t.m_target_opacity;
}

static void keybinds_smooth(const std::vector<key_pending_t>& pending, ImFont* font, const float fs, const float dt, const float base,
                            const bool horizontal, const bool shadow_detect, const shadow_opts_t& shadow)
{
    constexpr float k_detect_smooth = 20.f, k_detect_hold = 0.25f;

    s_smooth_clock += dt;
    const float now = s_smooth_clock;
    const float W   = screen_w();
    const float H   = screen_h();

    const auto find = [&](const int id) -> int {
        for (size_t i = 0; i < s_smooth_tags.size(); ++i)
            if (s_smooth_tags[i].m_id == id)
                return (int)i;
        return -1;
    };

    const auto live_count = [&]() {
        size_t count = 0;
        for (const auto& t : s_smooth_tags)
            if (!t.m_removing)
                ++count;
        return count;
    };

    size_t final_active = live_count();
    for (const auto& p : pending) {
        if (!p.m_active)
            continue;
        const int i = find(p.m_id);
        if (i < 0 || s_smooth_tags[i].m_removing)
            ++final_active;
    }
    const float batch_spawn = smooth_spawn_x(final_active);

    const float drop = k_smooth_spawn_drop * (H > 0.f ? H / 1080.f : 1.f);

    const int  origin      = g_kiSmoothOrigin;
    const bool from_bottom = origin == SMOOTH_ORIGIN_BOTTOM || (origin != SMOOTH_ORIGIN_RIGHT && !horizontal);

    const auto start_removing = [&](smooth_tag_t& t) {
        t.m_removing       = true;
        t.m_target_opacity = 0.f;
        if (!from_bottom)
            t.m_target_x = t.m_spawn_x != 0.f ? t.m_spawn_x : smooth_spawn_x(live_count());
        else
            t.m_target_y = t.m_spawn_y != 0.f ? t.m_spawn_y : t.m_y + drop;
    };

    bool listed[LBL_COUNT]{};
    for (const auto& p : pending) {
        listed[p.m_id] = true;
        int i = find(p.m_id);

        if (p.m_active) {
            if (i < 0) {
                smooth_tag_t t{};
                t.m_id                = p.m_id;
                t.m_opacity           = 0.f;
                t.m_target_opacity    = 1.f;
                t.m_appear_until      = now + k_smooth_appear_delay;
                t.m_detect_hold_until = p.m_success ? now + k_detect_hold : 0.f;
                t.m_target_detect     = p.m_success ? 1.f : 0.f;
                t.m_x = t.m_target_x = t.m_spawn_x = batch_spawn;
                t.m_fresh = true;
                s_smooth_tags.push_back(std::move(t));
                i = (int)s_smooth_tags.size() - 1;
            } else {
                smooth_tag_t& t = s_smooth_tags[i];

                if (t.m_removing) {
                    t.m_removing       = false;
                    t.m_opacity        = 0.f;
                    t.m_target_opacity = 1.f;
                    t.m_appear_until   = now + k_smooth_appear_delay;
                    t.m_spawn_x        = batch_spawn;
                    t.m_x              = batch_spawn;
                    t.m_fresh          = true;
                }

                if (p.m_success)
                    t.m_detect_hold_until = now + k_detect_hold;
                t.m_target_detect = (p.m_success || t.m_detect_hold_until > now) ? 1.f : 0.f;
            }

            s_smooth_tags[i].m_text       = p.m_text;
            s_smooth_tags[i].m_has_detect = p.m_detect;
            s_smooth_tags[i].m_predicted  = p.m_predicted;
        } else if (i >= 0 && !s_smooth_tags[i].m_removing)
            start_removing(s_smooth_tags[i]);
    }

    for (auto& t : s_smooth_tags)
        if (!listed[t.m_id] && !t.m_removing)
            start_removing(t);

    for (auto& t : s_smooth_tags)
        t.m_size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, t.m_text.c_str());

    const float gap     = (float)g_kiSpacing;
    const float y0      = H - base;
    float       spawn_y = y0 + drop;
    if (horizontal) {
        const float spacing = fs * 0.2f + gap;
        float total = -spacing;
        for (const auto& t : s_smooth_tags)
            if (!t.m_removing)
                total += t.m_size.x + spacing;

        float x = W * 0.5f - total * 0.5f;
        for (auto& t : s_smooth_tags) {
            if (t.m_removing)
                continue;
            t.m_target_y = y0;
            t.m_target_x = x;
            x += t.m_size.x + spacing;
        }
    } else {
        float y = y0;
        for (auto& t : s_smooth_tags) {
            if (t.m_removing)
                continue;
            t.m_target_x = (W - t.m_size.x) * 0.5f;
            t.m_target_y = y;
            y += t.m_size.y + gap;
        }
        spawn_y = y + drop;
    }

    for (auto& t : s_smooth_tags) {
        if (t.m_fresh) {
            if (!from_bottom)
                t.m_y = t.m_target_y;
            else {
                t.m_x = t.m_target_x;
                t.m_y = t.m_spawn_y = spawn_y;
            }
            t.m_fresh = false;
        }
    }

    size_t visible = 0;
    for (const auto& t : s_smooth_tags)
        if (t.m_opacity > 0.01f || !t.m_removing)
            ++visible;

    for (auto it = s_smooth_tags.begin(); it != s_smooth_tags.end();) {
        smooth_tag_t& t = *it;

        smooth_tag_step(t, visible > 1, now, dt, from_bottom);
        t.m_detect = rido_lerp(t.m_detect, t.m_target_detect, k_detect_smooth, dt);

        if (t.m_opacity > 0.01f) {
            const indicator_colors_t colors =
                t.m_has_detect ? key_colors_at(t.m_detect, shadow_detect, shadow.m_color, t.m_predicted, shadow.m_outline_color)
                               : key_colors_plain(shadow.m_color, shadow.m_outline_color);

            push_label(font, fs, ImVec2(std::round(t.m_x + 0.2f), std::round(t.m_y)), t.m_text, u32(colors.m_text, t.m_opacity),
                       u32(colors.m_shadow, t.m_opacity), shadow, u32(colors.m_outline, t.m_opacity));
            if (!t.m_removing)
                part_mark(t.m_id, t.m_x, t.m_y, t.m_size);
        }

        if (t.m_removing && t.m_opacity < 0.02f)
            it = s_smooth_tags.erase(it);
        else
            ++it;
    }
}

// ==== kamibebra style ==========================================================================
static void kamibebra_text(ImFont* font, const float fs, const float x, const float y, const std::string& text, const ImVec4& color)
{
    push_label(font, fs, ImVec2(std::round(x), std::round(y)), text, u32(color), 0u, { false, 0.f });
}

// own 2 layer drop shadow while the shadow option is off; on = the shared shadow / outline options
static void kamibebra_shadowed(ImFont* font, const float fs, const float s, const float x, const float y, const std::string& text, const ImVec4& color,
                               const shadow_opts_t& shadow, const float shadow_alpha)
{
    if (!shadow.m_on) {
        const float a = (200.f / 255.f) * color.w;
        kamibebra_text(font, fs, x + std::round(s), y + std::round(s), text, ImVec4(0.f, 0.f, 0.f, a));
        kamibebra_text(font, fs, x + std::round(2.f * s), y + std::round(2.f * s), text, ImVec4(0.f, 0.f, 0.f, a * 0.25f));
    }
    push_label(font, fs, ImVec2(std::round(x), std::round(y)), text, u32(color), shadow_u32(shadow.m_color, shadow_alpha), shadow);
}

struct kamibebra_tag_t {
    int         m_id   = 0;
    std::string m_text = {};
    ImVec2      m_size = {};
    float m_opacity = 0.f, m_target_opacity = 1.f;
    float m_x = 0.f, m_target_x = 0.f, m_spawn_x = 0.f;
    float m_y = 0.f, m_target_y = 0.f;
    bool  m_removing = false, m_detect = false, m_predicted = false;
    float m_detect_t = 0.f, m_target_detect = 0.f;
    float m_appear_until = 0.f, m_detect_hold_until = 0.f;
};

static std::vector<kamibebra_tag_t> s_kamibebra_tags;
static float s_kamibebra_clock = 0.f;

static void keybinds_kamibebra(const std::vector<key_pending_t>& pending, ImFont* font, const float fs, const float scale, const float dt, const float base,
                               const bool horizontal, const shadow_opts_t& shadow, const bool shadow_detect)
{
    constexpr float k_opacity = 10.f, k_position = 9.5f, k_detect = 4.5f, k_detect_hold = 0.25f, k_detect_max = 0.75f, k_appear = 0.09f,
                    k_remove_opacity = 2.35f, k_spacing = 4.f;

    const float s = std::clamp(scale, kIndScaleMin, kIndScaleMax);

    s_kamibebra_clock += dt;
    const float now = s_kamibebra_clock;
    const float W   = screen_w();
    const float gap = (float)g_kiSpacing;
    const float y0  = screen_h() - base;

    const auto find = [&](const int id) -> kamibebra_tag_t* {
        for (auto& t : s_kamibebra_tags)
            if (t.m_id == id)
                return &t;
        return nullptr;
    };

    const auto live_count = [&]() {
        size_t count = 0;
        for (const auto& t : s_kamibebra_tags)
            if (!t.m_removing)
                ++count;
        return count;
    };

    const auto recalc = [&]() {
        if (!horizontal) {
            float y = y0;
            for (auto& t : s_kamibebra_tags) {
                if (t.m_removing)
                    continue;
                t.m_target_x = std::round((W - t.m_size.x) * 0.5f);
                t.m_target_y = y;
                y += t.m_size.y + gap;
            }
            return;
        }

        const float spacing = std::round(k_spacing * s) + gap;
        float total = -spacing;
        for (const auto& t : s_kamibebra_tags)
            if (!t.m_removing)
                total += t.m_size.x + spacing;

        float x = W * 0.5f - total * 0.5f;
        for (auto& t : s_kamibebra_tags) {
            if (t.m_removing)
                continue;
            t.m_target_x = x;
            t.m_target_y = y0;
            x += t.m_size.x + spacing;
        }
    };

    const auto start_removing = [&](kamibebra_tag_t& t) {
        t.m_removing       = true;
        t.m_target_opacity = 0.f;
        recalc();
        t.m_target_x = t.m_spawn_x != 0.f ? t.m_spawn_x : smooth_spawn_x(live_count());
    };

    size_t final_active = live_count();
    for (const auto& p : pending) {
        if (!p.m_active)
            continue;
        const kamibebra_tag_t* const t = find(p.m_id);
        if (!t || t->m_removing)
            ++final_active;
    }
    const float batch_spawn = smooth_spawn_x(final_active);

    bool listed[LBL_COUNT]{};
    for (const auto& p : pending) {
        listed[p.m_id]       = true;
        kamibebra_tag_t* tag = find(p.m_id);

        if (p.m_active) {
            if (!tag) {
                kamibebra_tag_t t{};
                t.m_id                = p.m_id;
                t.m_text              = p.m_text;
                t.m_size              = font->CalcTextSizeA(fs, FLT_MAX, 0.f, t.m_text.c_str());
                t.m_appear_until      = now + k_appear;
                t.m_detect_hold_until = p.m_success ? now + k_detect_hold : 0.f;
                t.m_target_detect     = p.m_success ? 1.f : 0.f;
                t.m_predicted         = p.m_predicted;
                t.m_x = t.m_target_x = t.m_spawn_x = batch_spawn;
                s_kamibebra_tags.push_back(std::move(t));
                recalc();
                s_kamibebra_tags.back().m_x = batch_spawn;
                s_kamibebra_tags.back().m_y = s_kamibebra_tags.back().m_target_y;
            } else {
                if (tag->m_removing) {
                    tag->m_removing       = false;
                    tag->m_opacity        = 0.f;
                    tag->m_target_opacity = 1.f;
                    tag->m_appear_until   = now + k_appear;
                    tag->m_spawn_x        = batch_spawn;
                    recalc();
                    tag->m_x = batch_spawn;
                    tag->m_y = tag->m_target_y;
                }
                if (p.m_success)
                    tag->m_detect_hold_until = now + k_detect_hold;
                tag->m_detect = p.m_success || tag->m_detect_hold_until > now;
                if (p.m_success)
                    tag->m_predicted = p.m_predicted;
                tag->m_target_detect = tag->m_detect ? 1.f : 0.f;
                tag->m_text          = p.m_text;
            }
        } else if (tag && !tag->m_removing)
            start_removing(*tag);
    }

    for (auto& t : s_kamibebra_tags)
        if (!listed[t.m_id] && !t.m_removing)
            start_removing(t);

    if (!s_kamibebra_tags.empty()) {
        for (auto& t : s_kamibebra_tags)
            t.m_size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, t.m_text.c_str());
        recalc();
    }

    size_t visible = 0;
    for (const auto& t : s_kamibebra_tags)
        if (t.m_opacity > 0.01f || !t.m_removing)
            ++visible;

    const ImVec4 acc(g_indKamibebraAccent[0], g_indKamibebraAccent[1], g_indKamibebraAccent[2], 1.f);

    for (auto it = s_kamibebra_tags.begin(); it != s_kamibebra_tags.end();) {
        kamibebra_tag_t& t = *it;

        float position_smooth = k_position;
        if (t.m_removing && visible > 1) {
            const float total     = std::max(std::round(s), std::fabs(t.m_target_x - t.m_x) + 220.f * s);
            const float remaining = std::clamp((t.m_target_x - t.m_x) / total, 0.f, 1.f);
            position_smooth *= 0.7f + 1.5f * remaining;
        }

        const float opacity_target = (!t.m_removing && now < t.m_appear_until) ? 0.f : t.m_target_opacity;
        t.m_opacity = rido_lerp(t.m_opacity, opacity_target, k_opacity * (t.m_removing ? k_remove_opacity : 1.f), dt);
        t.m_x       = rido_lerp(t.m_x, t.m_target_x, position_smooth, dt);
        t.m_y       = rido_lerp(t.m_y, t.m_target_y, position_smooth, dt);
        if (std::fabs(t.m_target_x - t.m_x) <= std::round(0.75f * s))
            t.m_x = t.m_target_x;
        if (std::fabs(t.m_target_y - t.m_y) <= std::round(0.75f * s))
            t.m_y = t.m_target_y;
        if (std::fabs(t.m_target_opacity - t.m_opacity) <= 0.015f)
            t.m_opacity = t.m_target_opacity;
        t.m_detect_t = rido_lerp(t.m_detect_t, t.m_target_detect, k_detect, dt);

        const float tx = std::round(t.m_x + 0.2f * s), ty = std::round(t.m_y);
        if (t.m_opacity > 0.01f) {
            const float d = std::min(t.m_detect_t, k_detect_max);
            if (!shadow.m_on) {
                ImVec4 shade(acc.x * d, acc.y * d, acc.z * d, (t.m_detect ? 0.85f : 200.f / 255.f) * t.m_opacity);
                kamibebra_text(font, fs, tx + std::round(s), ty + std::round(s), t.m_text, shade);
                shade.w *= d > 0.01f ? 0.45f : 0.25f;
                kamibebra_text(font, fs, tx + std::round(2.f * s), ty + std::round(2.f * s), t.m_text, shade);
            }

            if (d > 0.01f) {
                const float a = d * t.m_opacity;
                const ImVec4 accent_glow(acc.x, acc.y, acc.z, a * 0.2f), white_glow(1.f, 1.f, 1.f, a * 0.3f);
                static constexpr float k_dirs[4][2] = { { -1.f, 0.f }, { 1.f, 0.f }, { 0.f, -1.f }, { 0.f, 1.f } };
                for (const auto& dir : k_dirs) {
                    kamibebra_text(font, fs, tx + dir[0] * 0.5f * s, ty + dir[1] * 0.5f * s, t.m_text, accent_glow);
                    kamibebra_text(font, fs, tx + dir[0] * 0.25f * s, ty + dir[1] * 0.25f * s, t.m_text, white_glow);
                }
            }
        }

        const indicator_colors_t colors =
            key_colors_at(std::min(t.m_detect_t, 1.f), shadow_detect, shadow.m_color, t.m_predicted, shadow.m_outline_color);
        push_label(font, fs, ImVec2(tx, ty), t.m_text, u32(colors.m_text, t.m_opacity), u32(colors.m_shadow, t.m_opacity), shadow,
                   u32(colors.m_outline, t.m_opacity));
        if (!t.m_removing)
            part_mark(t.m_id, tx, ty, t.m_size);

        if (t.m_removing && t.m_opacity < 0.02f)
            it = s_kamibebra_tags.erase(it);
        else
            ++it;
    }
}

// ==== mitosis / squash style ===================================================================
static unsigned long long s_mito_fire[LBL_COUNT]{};
static float s_mito_show[LBL_COUNT]{};
static float s_mito_hot[LBL_COUNT]{};
static float s_mito_slot[LBL_COUNT]{};

static void mito_reset_keybinds()
{
    for (int i = 0; i < LBL_COUNT; ++i)
        s_mito_show[i] = s_mito_hot[i] = s_mito_slot[i] = 0.f;
}

static void keybinds_mitosis(const std::vector<key_pending_t>& pending, ImFont* font, const float fs, const float dt, const float base,
                             const bool horizontal, const bool shadow_detect, const shadow_opts_t& shadow, const bool squash)
{
    constexpr unsigned long long k_flash_ms = 180;
    constexpr float k_show_speed = 4.f;
    constexpr float k_hot_speed  = 26.f;

    const unsigned long long now = GetTickCount64();

    bool listed[LBL_COUNT]{};
    for (const auto& p : pending)
        listed[p.m_id] = true;
    for (int id = 0; id < LBL_COUNT; ++id)
        if (!listed[id])
            s_mito_show[id] = s_mito_hot[id] = s_mito_slot[id] = 0.f;

    mito_frame_t f{};
    f.m_font    = font;
    f.m_fs      = fs;
    f.m_axis_x  = horizontal;
    f.m_blur    = shadow.m_blur;
    f.m_squash  = squash;
    f.m_off     = ImVec2(shadow.m_off_x, shadow.m_off_y);
    f.m_ring_px = shadow.m_outline_px;

    int ent_id[LBL_COUNT]{};

    for (const auto& p : pending) {
        const int slot = p.m_id;

        if (p.m_success)
            s_mito_fire[slot] = now;
        const bool flash = s_mito_fire[slot] != 0 && now - s_mito_fire[slot] < k_flash_ms;

        const float width = font->CalcTextSizeA(fs, FLT_MAX, 0.f, p.m_text.c_str()).x;
        s_mito_show[slot] = mig::FadeStep(s_mito_show[slot], p.m_active, dt, mig::SplitSpeed(k_show_speed, width, fs));
        s_mito_hot[slot]  = mig::Sat(s_mito_hot[slot] + dt * k_hot_speed * (flash ? 1.f : -1.f));
        s_mito_slot[slot] = mig::Follow(s_mito_slot[slot], mig::EaseOut(s_mito_show[slot]), dt, mig::kSlotRate);

        if (s_mito_show[slot] <= 0.001f && s_mito_slot[slot] <= 0.002f) {
            s_mito_slot[slot] = 0.f;
            continue;
        }

        const indicator_colors_t colors =
            p.m_detect ? key_colors_at(s_mito_hot[slot], shadow_detect, shadow.m_color, p.m_predicted, shadow.m_outline_color)
                       : key_colors_plain(shadow.m_color, shadow.m_outline_color);

        if (f.m_n < LBL_COUNT)
            ent_id[f.m_n] = slot;
        mito_add_ent(f, p.m_text.c_str(), u32(colors.m_text), shadow.m_on ? u32(colors.m_shadow) : 0u, colors.m_text.r, colors.m_text.g,
                     colors.m_text.b, s_mito_show[slot], s_mito_slot[slot], nullptr, 0.f, shadow.m_outline ? u32(colors.m_outline) : 0u);
    }

    if (f.m_n <= 0)
        return;

    const float gap = (float)g_kiSpacing;
    if (horizontal)
        mito_layout_row(f, f.m_fs * 0.2f + gap, screen_h() - base);
    else {
        float y = std::floor(screen_h() - base);
        for (int i = 0; i < f.m_n; ++i) {
            mito_ent_t& e = f.m_ents[i];
            e.m_pos = ImVec2(std::floor((screen_w() - e.m_size.x) * 0.5f), y);
            y += (e.m_size.y + gap) * e.m_anim;
        }
    }

    for (int i = 0; i < f.m_n; ++i)
        part_mark(ent_id[i], f.m_ents[i].m_pos.x, f.m_ents[i].m_pos.y, f.m_ents[i].m_size);

    mitosis_queue(f);
}

// ==== keybind indicators =======================================================================
struct kami_ghost_t {
    std::string m_text;
    ImVec2      m_at;
    float       m_born;
};

static std::vector<kami_ghost_t> s_kami_key_ghosts;
static bool   s_kami_key_was[LBL_COUNT]{};
static ImVec2 s_kami_key_at[LBL_COUNT]{};

static float s_static_key_alpha[LBL_COUNT]{};

static void keybind_indicators(const MvInd_t& s)
{
    std::vector<key_pending_t> pending;

    const auto shown = [](const int toggle) { return (g_kiShow & (1 << toggle)) != 0; };

    const shadow_opts_t shadow = shadow_opts_of(g_kiShadow, g_kiShadowBlur, g_kiShadowColor, g_kiOutline, g_kiOutlineColor, g_kiOutlinePx,
                                                g_kiShadowX, g_kiShadowY);

    const bool shadow_detect = shadow.m_on && g_kiShadowDetect;

    const auto custom_of = [](const int id) { return g_kiCustomLabels && g_kiLabels[id][0]; };

    const auto push = [&](const int id, const bool detect, const bool success, const bool active, const bool predicted) {
        pending.push_back({ id, custom_of(id) ? std::string(g_kiLabels[id]) : std::string(kKiLabels[id][1]), detect, success, active, predicted });
    };

    // lit reason latched per label: the fade back to base keeps the colour it was lit in
    static bool s_lit_predicted[LBL_COUNT]{};

    const auto add     = [&](const int id, const bool active) { push(id, false, false, active, false); };
    const auto add_hit = [&](const int id, const bool success, const bool active, const bool predicted = false) {
        if (success)
            s_lit_predicted[id] = predicted;
        push(id, true, success, active, s_lit_predicted[id]);
    };

    // active = the bind (s.xOn is On(): enabled AND key), success = the same trick state the
    // detections read. botox order, which is the draw order
    if (g_edgeBug && shown(KI_EB))
        add_hit(LBL_EB, s.ebHit, s.ebOn, s.ebPred);
    // botox ps: success = m_predicted_succesful (never set) || pixel_surf_now, predicted = !now
    if (g_pixelSurf && shown(KI_PS))
        add_hit(LBL_PS, s.psHit, s.psOn, !s.psHit);
    if (g_edgeJump && shown(KI_EJ))
        add(LBL_EJ, s.ejOn);
    if (g_longJump && shown(KI_LJ))
        add(LBL_LJ, s.ljOn);
    if (g_delayHop && shown(KI_DH))
        add(LBL_DH, s.dhOn);
    if (g_miniJump && shown(KI_MJ))
        add(LBL_MJ, s.mjOn);
    if (g_jumpbug && shown(KI_JB))
        add_hit(LBL_JB, s.jbHit, s.jbOn);
    if (g_textureBug && shown(KI_TB))
        add_hit(LBL_TB, s.tbHit, s.tbOn);
    if (g_airStuck && shown(KI_AS))
        add_hit(LBL_AIR, s.asHit, s.asOn);
    if (g_wallClimb && shown(KI_WC))
        add_hit(LBL_WC, s.wcHit, s.wcOn);
    // botox: add_hit(label_fr, is_ladder, key) -- lit on our grab / ride / fling tick
    if (g_fireMan && shown(KI_FR))
        add_hit(LBL_FR, s.frHit, s.frOn);
    if (g_autoStrafe && shown(KI_ST))
        add(LBL_AS, s.strafeOn);
    if (g_psAssist && shown(KI_PSA))
        add_hit(LBL_AST, s.astHit, s.astOn);
    if (g_pixelHop && shown(KI_PH))
        add_hit(LBL_PH, s.phHit, s.phOn);
    if (g_autoCrouch && shown(KI_AC))
        add(LBL_AC, s.acOn);
    if (g_autoBounce && shown(KI_AB))
        add(LBL_AB, s.abOn);
    if (g_edgeSkip && shown(KI_ES))
        add_hit(LBL_ES, s.esHit, s.esOn, true);
    if (g_hsw && shown(KI_HSW)) {
        add(LBL_HSW, s.hswOn);
        if (!custom_of(LBL_HSW))
            pending.back().m_text += g_hswSide == 0 ? "r" : "l";   // TF2 side 0 = right (botox 0 = left, "l")
    }
    float       fs         = 0.f;
    ImFont*     font       = IndicatorFont(g_kiScale, fs);
    const float dt         = ImGui::GetIO().DeltaTime;
    const float base       = (float)g_kiPosition;
    const bool  horizontal = g_kiHorizontal;
    const int   style      = g_kiStyle;
    const float W          = screen_w();
    const float H          = screen_h();

    if (!font)
        return;

    if (g_kiParticles)
        part_track(pending, real_time());

    if (style != IND_STYLE_SMOOTH)
        s_smooth_tags.clear();
    const bool mito = style == IND_STYLE_MITOSIS || style == IND_STYLE_SQUASH;
    if (!mito)
        mito_reset_keybinds();
    const bool kami = style == IND_STYLE_KAMIDERE;
    if (!kami) {
        s_kami_key_ghosts.clear();
        std::fill(std::begin(s_kami_key_was), std::end(s_kami_key_was), false);
    }

    if (style != IND_STYLE_KAMIBEBRA)
        s_kamibebra_tags.clear();

    if (style == IND_STYLE_SMOOTH) {
        keybinds_smooth(pending, font, fs, dt, base, horizontal, shadow_detect, shadow);
        return;
    }

    if (style == IND_STYLE_KAMIBEBRA) {
        keybinds_kamibebra(pending, font, fs, g_kiScale, dt, base, horizontal, shadow, shadow_detect);
        return;
    }

    if (mito) {
        keybinds_mitosis(pending, font, fs, dt, base, horizontal, shadow_detect, shadow, style == IND_STYLE_SQUASH);
        return;
    }

    const col_t shadow_tint = kami ? col_of(g_indKamiShadowColor) : shadow.m_color;
    const float real        = real_time();

    if (kami) {
        bool listed[LBL_COUNT]{};
        for (const auto& p : pending) {
            listed[p.m_id] = true;
            if (s_kami_key_was[p.m_id] && !p.m_active)
                s_kami_key_ghosts.push_back({ p.m_text, s_kami_key_at[p.m_id], real });
            s_kami_key_was[p.m_id] = p.m_active;
        }
        for (int id = 0; id < LBL_COUNT; ++id)
            if (!listed[id])
                s_kami_key_was[id] = false;
    }

    std::erase_if(s_kami_key_ghosts, [&](const kami_ghost_t& g) {
        const float age = real - g.m_born;
        if (age < 0.f || age >= k_kami_ghost_life)
            return true;
        kami_ghost_draw(font, fs, g.m_text, g.m_at, age, 25.f * g_kiScale);
        return false;
    });

    const auto kami_track = [&](const key_pending_t& p, const ImVec2& pos) {
        if (kami)
            s_kami_key_at[p.m_id] = ImVec2(pos.x + 1.f, pos.y + 1.f);
    };

    const bool animate = style != IND_STYLE_STATIC && !kami;

    const float static_fade = style == IND_STYLE_STATIC ? g_kiStaticFade : 0.f;
    if (static_fade > 0.f) {
        bool listed[LBL_COUNT]{};
        for (const auto& p : pending)
            listed[p.m_id] = true;
        for (int id = 0; id < LBL_COUNT; ++id)
            if (!listed[id])
                s_static_key_alpha[id] = 0.f;
    } else
        std::fill(std::begin(s_static_key_alpha), std::end(s_static_key_alpha), 0.f);

    const auto slot_of = [&](const float v) { return static_fade > 0.f ? 1.f : v; };

    const auto show_of = [&](const key_pending_t& p) -> float {
        if (!animate) {
            if (static_fade <= 0.f)
                return p.m_active ? 1.f : 0.f;

            float&      a    = s_static_key_alpha[p.m_id];
            const float step = dt / static_fade;
            a = p.m_active ? std::min(a + step, 1.f) : std::max(a - step, 0.f);
            return a;
        }

        ImAnimationHelper anim = ImAnimationHelper(ImHashStr(kKiLabels[p.m_id][1]), dt);
        anim.Update(3.f, p.m_active ? 3.f : -3.f);
        return anim.AnimationData->second;
    };

    const auto colors_of = [&](const key_pending_t& p) -> indicator_colors_t {
        if (!p.m_detect)
            return key_colors_plain(shadow_tint, shadow.m_outline_color);

        if (!animate)
            return key_colors_at(p.m_success ? 1.f : 0.f, shadow_detect, shadow_tint, p.m_predicted, shadow.m_outline_color);

        constexpr float fade_speed = 10.f;

        ImAnimationHelper fade = ImAnimationHelper(ImHashStr(kKiLabels[p.m_id][1]) + 0x9E3779B9u, dt);
        return key_colors_at(fade.Update(fade_speed, p.m_success ? 1.f : -1.f), shadow_detect, shadow_tint, p.m_predicted, shadow.m_outline_color);
    };

    if (horizontal) {
        struct render_entry_t {
            const key_pending_t* m_p;
            col_t  m_color;
            col_t  m_shadow;
            col_t  m_outline;
            float  m_anim;
            float  m_slot;
            ImVec2 m_size;
        };

        std::vector<render_entry_t> entries;

        for (const auto& p : pending) {
            const indicator_colors_t colors = colors_of(p);

            const float v = show_of(p);
            if (v <= 0.f)
                continue;

            entries.push_back({ &p, colors.m_text, colors.m_shadow, colors.m_outline, v, slot_of(v), font->CalcTextSizeA(fs, FLT_MAX, 0.f, p.m_text.c_str()) });
        }

        const float spacing = fs * 0.2f + (float)g_kiSpacing;

        float total      = 0.f;
        bool  first_pass = true;
        for (const auto& e : entries) {
            if (!first_pass)
                total += spacing * e.m_slot;
            total += e.m_size.x * e.m_slot;
            first_pass = false;
        }

        const float y = H - base;
        float       x = (W - total) / 2.f;

        bool first_draw = true;
        for (const auto& e : entries) {
            if (!first_draw)
                x += spacing * e.m_slot;

            const float slot_width  = e.m_size.x * e.m_slot;
            const float slot_center = x + slot_width / 2.f;

            const ImVec2 pos(slot_center - e.m_size.x / 2.f, y);
            push_label(font, fs, pos, e.m_p->m_text, u32(e.m_color, e.m_anim), u32(e.m_shadow, e.m_anim), shadow, u32(e.m_outline, e.m_anim));
            kami_track(*e.m_p, pos);
            part_mark(e.m_p->m_id, pos.x, pos.y, e.m_size);

            x += slot_width;
            first_draw = false;
        }
        return;
    }

    const float gap    = (float)g_kiSpacing;
    float       offset = 0.f;
    for (const auto& p : pending) {
        const indicator_colors_t colors = colors_of(p);

        const float v = show_of(p);
        if (v <= 0.f)
            continue;

        const ImVec2 text_size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, p.m_text.c_str());

        const ImVec2 pos((W - text_size.x) / 2.f, H - offset - base);
        push_label(font, fs, pos, p.m_text, u32(colors.m_text, v), u32(colors.m_shadow, v), shadow, u32(colors.m_outline, v));
        kami_track(p, pos);
        part_mark(p.m_id, pos.x, pos.y, text_size);

        offset -= (text_size.y + gap) * slot_of(v);
    }
}

// ==== keystrokes ===============================================================================
// IN_* bits -- fixed Source engine constants (in_buttons.h), same on every branch, same
// values movement.cpp defines file-local
static constexpr int IN_JUMP = 1 << 1, IN_DUCK = 1 << 2, IN_FORWARD = 1 << 3, IN_BACK = 1 << 4, IN_MOVELEFT = 1 << 9, IN_MOVERIGHT = 1 << 10;

struct key_press_cell_t {
    const char* m_text;
    int         m_button;
};

static constexpr key_press_cell_t k_key_press_grid[2][2][3] = {
    { { { "A", IN_MOVELEFT }, { "W", IN_FORWARD }, { "D", IN_MOVERIGHT } }, { { "C", IN_DUCK }, { "S", IN_BACK }, { "J", IN_JUMP } } },
    { { { "C", IN_DUCK }, { "W", IN_FORWARD }, { "J", IN_JUMP } }, { { "A", IN_MOVELEFT }, { "S", IN_BACK }, { "D", IN_MOVERIGHT } } },
};

static float s_key_press_anim[3][3]{};

static void key_press(const MvInd_t& s)
{
    float   fs   = 0.f;
    ImFont* font = IndicatorFont(g_kpScale, fs);
    if (!font)
        return;

    const int   buttons  = s.buttons;
    const short mouse_dx = s.mouseDx;

    const auto&  grid      = k_key_press_grid[std::clamp(g_kpLayout, 0, 1)];
    const int    released  = g_kpReleased;
    const bool   duck_jump = g_kpDuckJump;
    const ImVec4 on_color(g_kpColor[0], g_kpColor[1], g_kpColor[2], g_kpColor[3]);
    const ImVec4 off_color(g_kpColorReleased[0], g_kpColorReleased[1], g_kpColorReleased[2], g_kpColorReleased[3]);
    static const float k_black[4] = { 0.f, 0.f, 0.f, 1.f };
    const shadow_opts_t shadow = shadow_opts_of(g_kpShadow, g_kpShadowBlur, k_black, g_kpOutline, g_kpOutlineColor, g_kpOutlinePx,
                                                g_kpShadowX, g_kpShadowY);

    const float fade = g_kpFade;
    const float step = fade > 0.f ? ImGui::GetIO().DeltaTime / fade : 1.f;

    const auto width_of = [&](const char* text) { return font->CalcTextSizeA(fs, FLT_MAX, 0.f, text).x; };

    // fixed pitch off the widest glyph: cells never move, whatever is pressed
    float cell_w = std::max({ width_of("_"), width_of("<"), width_of(">") });
    for (const auto& row : grid)
        for (const auto& cell : row)
            cell_w = std::max(cell_w, width_of(cell.m_text));

    const float pitch_x = cell_w + (float)g_kpGapX;
    const float pitch_y = fs + (float)g_kpGapY;
    const float cx      = screen_w() / 2.f + (float)g_kpOffsetX;
    const float top     = screen_h() - (float)g_kpPosition;

    const auto draw_cell = [&](const int row, const int col, const char* text, const bool down) {
        float& a = s_key_press_anim[row][col];
        a = std::clamp(a + (down ? step : -step), 0.f, 1.f);

        const float x = cx + (float)(col - 1) * pitch_x;
        const float y = std::round(top + (float)row * pitch_y);

        const auto put = [&](const char* t, const ImVec4& c) {
            if (c.w <= 0.f)
                return;
            push_label(font, fs, ImVec2(std::round(x - width_of(t) * 0.5f), y), t, u32(c), u32(ImVec4(0.f, 0.f, 0.f, c.w)), shadow);
        };
        const auto faded = [](ImVec4 c, const float k) {
            c.w *= k;
            return c;
        };

        if (released == 0) {
            put("_", faded(off_color, 1.f - a));
            put(text, faded(on_color, a));
        } else if (released == 1)
            put(text, ImVec4(std::lerp(off_color.x, on_color.x, a), std::lerp(off_color.y, on_color.y, a), std::lerp(off_color.z, on_color.z, a),
                             std::lerp(off_color.w, on_color.w, a)));
        else
            put(text, faded(on_color, a));
    };

    for (int row = 0; row < 2; row++)
        for (int col = 0; col < 3; col++) {
            const key_press_cell_t& cell = grid[row][col];
            if (!duck_jump && (cell.m_button & (IN_DUCK | IN_JUMP))) {
                s_key_press_anim[row][col] = 0.f;
                continue;
            }
            draw_cell(row, col, cell.m_text, (buttons & cell.m_button) != 0);
        }

    if (g_kpMouse) {
        draw_cell(2, 0, "<", mouse_dx < 0);
        draw_cell(2, 2, ">", mouse_dx > 0);
    }
}

// ==== velocity (botox speed_pair) ==============================================================
struct speed_smooth_t {
    smooth_tag_t m_main, m_pre;
    bool  m_has_main = false, m_has_pre = false;
    float m_clock = 0.f, m_last_time = 0.f;
};

struct speed_ghost_t {
    std::string m_main, m_pre;
    float m_born;
    float m_x, m_pre_x;
};

struct speed_kami_t {
    bool  m_init = false;
    float m_last_time = 0.f;
    float m_x = 0.f, m_pre_x = 0.f;
    float m_main_w = 0.f, m_pre_w = 0.f, m_pre_a = 0.f;
    std::vector<speed_ghost_t> m_ghosts;
};

struct speed_kamibebra_t {
    bool        m_init = false;
    float       m_last_time = 0.f;
    float       m_x = 0.f;
    bool        m_has_pre = false, m_pre_removing = false;
    std::string m_pre = {};
    float       m_pre_opacity = 0.f, m_pre_x = 0.f, m_pre_target_x = 0.f;
    ImVec4      m_pre_color = ImVec4(1.f, 1.f, 1.f, 0.f);
};

struct speed_pair_state_t {
    speed_smooth_t m_smooth;
    float m_show = 0.f;
    float m_slot = 0.f;
    speed_kami_t m_kami;
    speed_kamibebra_t m_kamibebra;
};

static speed_pair_state_t s_velocity_pair;

static void kami_touch(speed_kami_t& k)
{
    const float real = real_time();
    if (real - k.m_last_time > 0.25f || real < k.m_last_time)
        k = {};
    k.m_last_time = real;
}

struct speed_pair_opts_t {
    int   m_pre_layout    = PRE_LAYOUT_RIGHT;
    float m_pre_scale     = 1.f;
    int   m_pre_align     = PRE_ALIGN_BOTTOM;
    bool  m_took_off      = false;
    bool  m_kami_glide    = false;
    int   m_smooth_origin = SMOOTH_ORIGIN_AUTO;
};

static void speed_pair(speed_pair_state_t& st, const int style, const char* anim_key, const std::string& main_text, const std::string& pre_value,
                       const ImVec4& color, const float shadow_alpha, const float y, const bool show_pre, const shadow_opts_t& shadow,
                       const float scale, const speed_pair_opts_t& opts = {})
{
    float   fs   = 0.f;
    ImFont* font = IndicatorFont(scale, fs);
    if (!font)
        return;

    float       pfs   = fs;
    ImFont*     pfont = font;
    const float pscl  = std::clamp(scale * opts.m_pre_scale, kIndScaleMin, kIndScaleMax);
    if (opts.m_pre_scale != 1.f) {
        float f = 0.f;
        if (ImFont* const pf = IndicatorFont(pscl, f)) {
            pfont = pf;
            pfs   = f;
        }
    }

    const float dt = ImGui::GetIO().DeltaTime;
    const float W  = screen_w();
    const float H  = screen_h();

    if (style != IND_STYLE_SMOOTH)
        st.m_smooth = {};
    const bool mito = style == IND_STYLE_MITOSIS || style == IND_STYLE_SQUASH;
    if (!mito)
        st.m_show = st.m_slot = 0.f;
    if (style != IND_STYLE_KAMIDERE)
        st.m_kami = {};
    if (style != IND_STYLE_KAMIBEBRA)
        st.m_kamibebra = {};

    const auto ink = [](const ImFont* f, const float size) {
        const ImFontGlyph* const g = f->FindGlyph('0');
        const float s = size / f->FontSize;
        return g ? ImVec2(g->Y0 * s, g->Y1 * s) : ImVec2(0.f, size);
    };

    if (style == IND_STYLE_KAMIBEBRA) {
        const float s = std::clamp(scale, kIndScaleMin, kIndScaleMax);

        speed_kamibebra_t& k = st.m_kamibebra;
        const float real = real_time();
        if (real - k.m_last_time > 0.25f || real < k.m_last_time)
            k = {};
        k.m_last_time = real;

        if (opts.m_took_off && show_pre) {
            if (!k.m_has_pre) {
                k.m_pre_opacity = 0.f;
                k.m_pre_color   = ImVec4(1.f, 1.f, 1.f, 0.f);
            } else
                k.m_pre_opacity = 1.f;
            k.m_has_pre      = true;
            k.m_pre_removing = false;
            k.m_pre          = pre_value;
        }
        if (!show_pre && k.m_has_pre)
            k.m_pre_removing = true;

        const bool   stacked   = opts.m_pre_layout == PRE_LAYOUT_ABOVE || opts.m_pre_layout == PRE_LAYOUT_BELOW;
        const float  spacing   = 5.f * s;
        const ImVec2 main_size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, main_text.c_str());
        const ImVec2 pre_size  = k.m_has_pre ? pfont->CalcTextSizeA(pfs, FLT_MAX, 0.f, k.m_pre.c_str()) : ImVec2(0.f, 0.f);

        const float target_x = k.m_has_pre && !k.m_pre_removing && !stacked ? (W - (main_size.x + std::round(spacing) + pre_size.x)) * 0.5f
                                                                            : (W - main_size.x) * 0.5f;
        if (!k.m_init) {
            k.m_x    = target_x;
            k.m_init = true;
        }
        k.m_x = rido_lerp(k.m_x, target_x, 8.5f, dt);

        const ImVec4 main_color = color;
        kamibebra_shadowed(font, fs, s, k.m_x + 0.2f * s, y, main_text, main_color, shadow, shadow_alpha);

        if (k.m_has_pre) {
            k.m_pre_opacity = rido_lerp(k.m_pre_opacity, k.m_pre_removing ? 0.f : 1.f, 16.f * (k.m_pre_removing ? 1.6f : 1.f), dt);
            if (stacked)
                k.m_pre_x = k.m_pre_target_x = std::round((W - pre_size.x) * 0.5f);
            else if (!k.m_pre_removing)
                k.m_pre_x = k.m_pre_target_x = k.m_x + main_size.x + spacing;
            else
                k.m_pre_x = rido_lerp(k.m_pre_x, k.m_pre_target_x, 12.f, dt);

            const ImVec4 target(main_color.x, main_color.y, main_color.z, k.m_pre_opacity * main_color.w);
            k.m_pre_color.x = rido_lerp(k.m_pre_color.x, target.x, 14.f * 0.96f, dt);
            k.m_pre_color.y = rido_lerp(k.m_pre_color.y, target.y, 14.f * 0.96f, dt);
            k.m_pre_color.z = rido_lerp(k.m_pre_color.z, target.z, 14.f * 0.96f, dt);
            k.m_pre_color.w = target.w;

            float pre_y = opts.m_pre_layout == PRE_LAYOUT_ABOVE ? y - pre_size.y : y + main_size.y;
            if (!stacked) {
                const ImVec2 mi = ink(font, fs), pi = ink(pfont, pfs);
                pre_y = std::round(opts.m_pre_align == PRE_ALIGN_TOP ? y + mi.x - pi.x : y + mi.y - pi.y);
            }

            if (k.m_pre_opacity > 0.01f)
                kamibebra_shadowed(pfont, pfs, pscl, k.m_pre_x + 0.2f * s, pre_y, k.m_pre, k.m_pre_color, shadow, shadow_alpha * k.m_pre_opacity);

            if (k.m_pre_removing && k.m_pre_opacity < 0.01f)
                k.m_has_pre = k.m_pre_removing = false;
        }
        return;
    }

    const bool        stacked   = opts.m_pre_layout == PRE_LAYOUT_ABOVE || opts.m_pre_layout == PRE_LAYOUT_BELOW;
    const std::string pre_text  = stacked ? pre_value : " " + pre_value;
    const ImVec2      main_size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, main_text.c_str());
    const ImVec2      pre_size  = pfont->CalcTextSizeA(pfs, FLT_MAX, 0.f, pre_text.c_str());

    float pre_y = opts.m_pre_layout == PRE_LAYOUT_ABOVE ? y - pre_size.y : y + main_size.y;
    if (!stacked) {
        const ImVec2 mi = ink(font, fs), pi = ink(pfont, pfs);
        pre_y = std::round(opts.m_pre_align == PRE_ALIGN_TOP ? y + mi.x - pi.x : y + mi.y - pi.y);
    }
    const float pre_centre_x = std::round((W - pre_size.x) * 0.5f);

    const auto with_a = [](const ImVec4& c, const float a) { return ImVec4(c.x, c.y, c.z, c.w * a); };

    const auto push = [&](const bool pre, const ImVec2& position, const std::string& text, const ImVec4& text_color, const float text_shadow_alpha) {
        push_label(pre ? pfont : font, pre ? pfs : fs, position, text, u32(text_color), shadow_u32(shadow.m_color, text_shadow_alpha), shadow);
    };

    if (style == IND_STYLE_SMOOTH) {
        speed_smooth_t& s = st.m_smooth;
        smooth_tag_t&   m = s.m_main;
        smooth_tag_t&   p = s.m_pre;

        // not drawn a while (dead, toggled off): start clean, never resume a stale half-fade
        const float real = real_time();
        if (real - s.m_last_time > 0.25f || real < s.m_last_time)
            s = {};
        s.m_last_time = real;

        s.m_clock += dt;
        const float now = s.m_clock;

        const float x0 = (W - main_size.x - (show_pre && !stacked ? pre_size.x : 0.f)) * 0.5f;
        m.m_target_x = x0;
        m.m_target_y = y;

        // the number never comes or goes: first frame takes its place outright
        if (!s.m_has_main) {
            m.m_x = x0;
            m.m_y = y;
            m.m_opacity = m.m_target_opacity = 1.f;
            s.m_has_main = true;
        }

        const bool  from_bottom = opts.m_smooth_origin == SMOOTH_ORIGIN_BOTTOM;
        const float drop        = k_smooth_spawn_drop * (H > 0.f ? H / 1080.f : 1.f);

        if (show_pre && (!s.m_has_pre || p.m_removing)) {
            p = {};
            p.m_target_opacity = 1.f;
            p.m_appear_until   = now + k_smooth_appear_delay;
            p.m_spawn_x        = smooth_spawn_x(2);
            p.m_x              = stacked ? p.m_spawn_x : p.m_spawn_x - (m.m_x + main_size.x);
            p.m_y              = pre_y;
            if (from_bottom) {
                p.m_x = stacked ? pre_centre_x : 0.f;
                p.m_y = p.m_spawn_y = pre_y + drop;
            }
            s.m_has_pre = true;
        }

        if (!show_pre && s.m_has_pre && !p.m_removing) {
            p.m_removing       = true;
            p.m_target_opacity = 0.f;
            if (!stacked)
                p.m_x = std::round(m.m_x + 0.2f) + std::round(main_size.x + p.m_x) - 0.2f;
            p.m_target_x = from_bottom ? p.m_x : p.m_spawn_x;
            if (from_bottom)
                p.m_target_y = p.m_spawn_y != 0.f ? p.m_spawn_y : p.m_y + drop;
        }

        if (s.m_has_pre && !p.m_removing) {
            p.m_target_x = stacked ? pre_centre_x : 0.f;
            p.m_text     = pre_text;
        }
        if (!p.m_removing || !from_bottom)
            p.m_target_y = pre_y;

        smooth_tag_step(m, false, now, dt);
        if (s.m_has_pre)
            smooth_tag_step(p, p.m_opacity > 0.01f, now, dt, from_bottom);

        const float main_x = std::round(m.m_x + 0.2f);
        push(false, ImVec2(main_x, std::round(m.m_y)), main_text, color, shadow_alpha);

        if (s.m_has_pre) {
            const float pre_x = p.m_removing || stacked ? std::round(p.m_x + 0.2f) : main_x + std::round(main_size.x + p.m_x);
            if (p.m_opacity > 0.01f)
                push(true, ImVec2(pre_x, std::round(p.m_y)), p.m_text, with_a(color, p.m_opacity), shadow_alpha * p.m_opacity);

            if (p.m_removing && p.m_opacity < 0.02f)
                s.m_has_pre = false;
        }
        return;
    }

    if (mito) {
        st.m_show = mig::FadeStep(st.m_show, show_pre, dt, mig::SplitSpeed(4.f, pre_size.x, fs));
        st.m_slot = mig::Follow(st.m_slot, mig::EaseOut(st.m_show), dt, mig::kSlotRate);

        const bool pre_on = !(st.m_show <= 0.001f && st.m_slot <= 0.002f);
        if (!pre_on)
            st.m_slot = 0.f;

        mito_frame_t f{};
        f.m_font    = font;
        f.m_fs      = fs;
        f.m_axis_x  = !stacked;
        f.m_blur    = shadow.m_blur;
        f.m_squash  = style == IND_STYLE_SQUASH;
        f.m_off     = ImVec2(shadow.m_off_x, shadow.m_off_y);
        f.m_ring_px = shadow.m_outline_px;

        const ImU32 col       = u32(color);
        const ImU32 shade_col = shadow.m_on ? shadow_u32(shadow.m_color, shadow_alpha) : 0u;
        const ImU32 ring_col  = shadow.m_outline ? shadow_u32(shadow.m_outline_color, color.w) : 0u;

        const bool pre_first = opts.m_pre_layout == PRE_LAYOUT_ABOVE;
        if (pre_on && pre_first)
            mito_add_ent(f, pre_value.c_str(), col, shade_col, color.x, color.y, color.z, st.m_show, st.m_slot, pfont, pfs, ring_col);
        mito_add_ent(f, main_text.c_str(), col, shade_col, color.x, color.y, color.z, 1.f, 1.f, nullptr, 0.f, ring_col);
        if (pre_on && !pre_first)
            mito_add_ent(f, pre_value.c_str(), col, shade_col, color.x, color.y, color.z, st.m_show, st.m_slot, pfont, pfs, ring_col);

        if (!stacked) {
            mito_layout_row(f, pfont->CalcTextSizeA(pfs, FLT_MAX, 0.f, " ").x, y);
            if (pre_on)
                f.m_ents[1].m_pos.y = std::floor(pre_y);
        } else {
            for (int i = 0; i < f.m_n; ++i) {
                mito_ent_t& e   = f.m_ents[i];
                const bool  pre = pre_on && i == (pre_first ? 0 : 1);
                const float ey  = !pre ? y : pre_first ? y - e.m_size.y * e.m_anim : y + main_size.y * e.m_anim;
                e.m_pos = ImVec2(std::floor((W - e.m_size.x) * 0.5f), std::floor(ey));
            }
        }
        mitosis_queue(f);
        return;
    }

    if (style == IND_STYLE_KAMIDERE) {
        speed_kami_t& k = st.m_kami;
        kami_touch(k);

        const float real = real_time();
        const float rate = opts.m_kami_glide ? kami_rate(dt) : 1.f;
        const col_t tint = col_of(g_indKamiShadowColor);
        const auto  tint_at = [&](const float a) { return u32(shadow_at(tint, a)); };
        const auto  ease    = [&](float& v, const float to) { v += (to - v) * rate; };

        const bool first = !k.m_init;
        k.m_init = true;
        if (first) {
            k.m_main_w = main_size.x;
            k.m_pre_w  = pre_size.x;
        }
        ease(k.m_pre_a, show_pre ? 1.f : 0.f);
        ease(k.m_main_w, main_size.x);
        ease(k.m_pre_w, pre_size.x);

        const bool  pre_on = k.m_pre_a > 0.01f;
        const float tx     = W * 0.5f - (k.m_main_w + (pre_on && !stacked ? k.m_pre_w : 0.f)) * 0.5f;
        if (first)
            k.m_x = tx;
        ease(k.m_x, tx);

        const float tpx = stacked ? W * 0.5f - k.m_pre_w * 0.5f : k.m_x + k.m_main_w;
        if (first)
            k.m_pre_x = tpx;
        ease(k.m_pre_x, tpx);

        if (opts.m_took_off)
            k.m_ghosts.push_back({ main_text, pre_on ? pre_text : std::string(), real, k.m_x, k.m_pre_x });

        const float rise = 15.f * scale;
        std::erase_if(k.m_ghosts, [&](const speed_ghost_t& g) { return real - g.m_born < 0.f || real - g.m_born >= k_kami_ghost_life; });
        for (speed_ghost_t& g : k.m_ghosts) {
            const float age = real - g.m_born;
            ease(g.m_x, k.m_x);
            ease(g.m_pre_x, k.m_pre_x);
            kami_ghost_draw(font, fs, g.m_main, ImVec2(std::round(g.m_x) + 1.f, y + 1.f), age, rise);
            if (!g.m_pre.empty())
                kami_ghost_draw(pfont, pfs, g.m_pre, ImVec2(std::round(g.m_pre_x) + 1.f, pre_y + 1.f), age, rise);
        }

        push_label(font, fs, ImVec2(std::round(k.m_x), y), main_text, u32(color), tint_at(shadow_alpha), shadow);
        if (pre_on)
            push_label(pfont, pfs, ImVec2(std::round(k.m_pre_x), pre_y), pre_text, u32(with_a(color, k.m_pre_a)), tint_at(shadow_alpha * k.m_pre_a),
                       shadow);
        return;
    }

    float pre_alpha = show_pre ? 1.f : 0.f;
    if (style != IND_STYLE_STATIC) {
        ImAnimationHelper pre_speed_animation = ImAnimationHelper(ImHashStr(anim_key), dt);
        pre_speed_animation.Update(3.f, show_pre ? 3.f : -3.f);
        pre_alpha = pre_speed_animation.AnimationData->second;
    }

    const ImVec4 pre_color = with_a(color, pre_alpha);

    if (stacked) {
        push(false, ImVec2((W - main_size.x) / 2.f, y), main_text, color, shadow_alpha);
        if (pre_alpha > 0.f)
            push(true, ImVec2(pre_centre_x, pre_y), pre_text, pre_color, shadow_alpha * pre_alpha);
        return;
    }

    const float total_x = main_size.x + pre_size.x * pre_alpha;
    const float start_x = (W - total_x) / 2.f;

    push(false, ImVec2(start_x, y), main_text, color, shadow_alpha);

    if (pre_alpha > 0.f)
        push(true, ImVec2(start_x + main_size.x, pre_y), pre_text, pre_color, shadow_alpha * pre_alpha);
}

static struct {
    int   m_last_velocity          = 0;
    int   m_tick_prev_velocity     = 0;
    int   m_take_off_velocity      = 0;
    float m_take_off_time_velocity = 0.f;
    bool  m_last_on_ground_velocity = false;
    int   m_wj_seq                 = 0;
} s_indicator_data;

static void velocity(const MvInd_t& s, const bool on_ground)
{
    auto& d = s_indicator_data;

    // map change / reconnect rewinds tickcount + curtime: the 5-tick sample would freeze and a
    // stale takeoff timer would keep the pre speed up until the clock caught up
    if (s.tick < d.m_tick_prev_velocity)
        d.m_tick_prev_velocity = 0;
    if (d.m_take_off_time_velocity > s.curtime + 2.f)
        d.m_take_off_time_velocity = 0.f;

    const int velocity = (int)std::round(s.speed);
    bool took_off = d.m_last_on_ground_velocity && !on_ground;
    if (took_off) {
        d.m_take_off_velocity      = velocity;
        d.m_take_off_time_velocity = s.curtime + 2.f;
    }
    // fake wall jump = a jump too: its post-jump speed becomes the pre speed
    if (s.wjSeq != d.m_wj_seq) {
        d.m_wj_seq                 = s.wjSeq;
        d.m_take_off_velocity      = (int)std::round(s.wjSpeed);
        d.m_take_off_time_velocity = s.curtime + 2.f;
        took_off                   = true;
    }

    const bool kamibebra = g_viStyle == IND_STYLE_KAMIBEBRA;
    float alpha = std::min(s.speed / 250.f, 1.f);
    if (kamibebra) {
        alpha = 1.f;
        if (g_viFadeAlpha) {
            if (s.speed <= 0.f)
                alpha = 0.f;
            else if (s.speed < 30.f)
                alpha = std::lerp(0.f, 0.12f, s.speed / 30.f);
            else if (s.speed < 150.f)
                alpha = std::lerp(0.12f, 1.f, std::pow((s.speed - 30.f) / 120.f, 2.8f));
        }
    }
    const float fade = g_viFadeAlpha ? alpha : 1.f;

    // ImColor::Blend (botox imgui.h): per channel, alpha included, multiplier clamped
    ImVec4 blended_color;
    {
        const float k = std::clamp(alpha, 0.f, 1.f);
        const float* a = g_viColor1;
        const float* b = g_viColor2;
        blended_color = ImVec4(a[0] + k * (b[0] - a[0]), a[1] + k * (b[1] - a[1]), a[2] + k * (b[2] - a[2]), a[3] + k * (b[3] - a[3]));
    }

    const float* sc = velocity == d.m_last_velocity ? g_viColor3 : velocity < d.m_last_velocity ? g_viColor4 : g_viColor5;
    const ImVec4 speed_based_color(sc[0], sc[1], sc[2], sc[3] * fade);

    const bool should_draw_take_off = (!on_ground || d.m_take_off_time_velocity > s.curtime) && g_viPreSpeed;

    ImVec4 text_color = g_viCustomColor
                            ? ImVec4(blended_color.x, blended_color.y, blended_color.z, blended_color.w * fade)
                            : speed_based_color;
    if (kamibebra)
        text_color.w = alpha;

    const float shadow_alpha = fade;
    const float y            = screen_h() - (float)g_viPadding;

    char main_text[16], pre_text[24];
    std::snprintf(main_text, sizeof(main_text), "%d", velocity);
    std::snprintf(pre_text, sizeof(pre_text), "(%d)", kamibebra ? std::min(d.m_take_off_velocity, 287) : d.m_take_off_velocity);

    speed_pair(s_velocity_pair, g_viStyle, "velocity_pre_speed", main_text, pre_text, text_color, shadow_alpha, y,
               kamibebra ? g_viPreSpeed && !on_ground : should_draw_take_off,
               shadow_opts_of(g_viShadow, g_viShadowBlur, g_viShadowColor, g_viOutline, g_viOutlineColor, g_viOutlinePx, g_viShadowX, g_viShadowY),
               g_viScale, { g_viPreLayout, g_viPreScale, g_viPreAlign, took_off, false, g_viSmoothOrigin });

    if (d.m_tick_prev_velocity + 5 < s.tick) {
        d.m_last_velocity      = velocity;
        d.m_tick_prev_velocity = s.tick;
    }

    d.m_last_on_ground_velocity = on_ground;
}

// ==== velocity graph ===========================================================================
struct graph_sample_t {
    float m_speed;
    bool  m_take_off;
};

static std::deque<graph_sample_t> s_graph;
static int   s_graph_tick   = 0;
static bool  s_graph_ground = true;
static float s_graph_top    = 300.f;

static speed_pair_state_t s_stamina_pair;
static struct {
    float m_last_stamina          = 0.f;
    int   m_tick_prev_stamina     = 0;
    float m_take_off_stamina      = 0.f;
    float m_take_off_time_stamina = 0.f;
    bool  m_last_on_ground_stamina = false;
} s_stamina_data;

// botox stamina(on_ground), same shape as velocity() above. Value = s.stamina (0 fresh .. 1
// empty, MV_GetIndicators / movement.cpp EntStamina), shown as a 0..100 number like botox's
// get_stamina(). botox only has color1/color2 (no speed-based 3/4/5 set here).
static void stamina(const MvInd_t& s, const bool on_ground)
{
    auto& d = s_stamina_data;

    if (s.tick < d.m_tick_prev_stamina)
        d.m_tick_prev_stamina = 0;
    if (d.m_take_off_time_stamina > s.curtime + 2.f)
        d.m_take_off_time_stamina = 0.f;

    const float stam = s.stamina * 100.f;
    if (d.m_last_on_ground_stamina && !on_ground) {
        d.m_take_off_stamina      = stam;
        d.m_take_off_time_stamina = s.curtime + 2.f;
    }

    const float alpha = std::clamp(s.stamina, 0.f, 1.f);   // 0 fresh .. 1 empty
    const float fade  = g_siFadeAlpha ? alpha : 1.f;

    ImVec4 blended_color;
    {
        const float* a = g_siColor1;
        const float* b = g_siColor2;
        blended_color = ImVec4(a[0] + alpha * (b[0] - a[0]), a[1] + alpha * (b[1] - a[1]),
                                a[2] + alpha * (b[2] - a[2]), a[3] + alpha * (b[3] - a[3]));
    }
    const ImVec4 text_color(blended_color.x, blended_color.y, blended_color.z, blended_color.w * fade);

    const bool should_draw_take_off = (!on_ground || d.m_take_off_time_stamina > s.curtime) && g_siPreSpeed;
    const float shadow_alpha = fade;
    const float y = screen_h() - (float)g_siPadding;

    char main_text[16], pre_text[24];
    std::snprintf(main_text, sizeof(main_text), "%.1f", stam);
    std::snprintf(pre_text, sizeof(pre_text), "(%.1f)", d.m_take_off_stamina);

    speed_pair(s_stamina_pair, g_siStyle, "stamina_pre_speed", main_text, pre_text, text_color, shadow_alpha, y,
               g_siStyle == IND_STYLE_KAMIBEBRA ? g_siPreSpeed && !on_ground : should_draw_take_off,
               shadow_opts_of(g_siShadow, g_siShadowBlur, g_siShadowColor, g_siOutline, g_siOutlineColor, g_siOutlinePx, g_siShadowX, g_siShadowY),
               g_siScale, { g_siPreLayout, g_siPreScale, g_siPreAlign, d.m_last_on_ground_stamina && !on_ground, false, g_siSmoothOrigin });

    if (d.m_tick_prev_stamina + 5 < s.tick) {
        d.m_last_stamina      = stam;
        d.m_tick_prev_stamina = s.tick;
    }
    d.m_last_on_ground_stamina = on_ground;
}

static void velocity_graph(const MvInd_t& s, const bool on_ground, const bool alive)
{
    if (!g_vgEnable || !alive) {
        s_graph.clear();
        return;
    }

    const int   length = std::clamp(g_vgLength, 2, 2048);
    const int   tick   = s.tick;
    const float now    = s.speed;

    if (tick < s_graph_tick || tick - s_graph_tick > length)
        s_graph.clear();

    const int add = s_graph.empty() ? 1 : tick - s_graph_tick;
    for (int i = 0; i < add; ++i)
        s_graph.push_back({ now, i == add - 1 && s_graph_ground && !on_ground });
    if (add > 0) {
        s_graph_tick   = tick;
        s_graph_ground = on_ground;
    }
    while ((int)s_graph.size() > length)
        s_graph.pop_front();

    const float width  = (float)std::max(g_vgWidth, 10);
    const float height = (float)std::max(g_vgHeight, 5);
    const float left   = std::round(screen_w() * 0.5f + g_vgOffsetX - width * 0.5f);
    const float base_y = std::round(screen_h() - (float)g_vgPosition);
    const float step   = width / (float)(length - 1);

    float top = g_vgMax;
    if (top <= 0.f) {
        float peak = 300.f;
        for (const graph_sample_t& g : s_graph)
            peak = std::max(peak, g.m_speed);
        s_graph_top += (peak - s_graph_top) * (1.f - std::exp(-6.f * ImGui::GetIO().DeltaTime));
        top = s_graph_top;
    }

    const bool  fade_sides = g_vgFadeSides;
    const float fade       = g_vgFade;
    const auto  edge_alpha = [&](const float x) {
        if (!fade_sides || fade <= 0.f)
            return 1.f;
        const float t = (x - left) / width;
        return std::clamp(std::min(t, 1.f - t) / fade, 0.f, 1.f);
    };

    struct graph_pt_t {
        ImVec2 m_at;
        float  m_a;
        int    m_speed;
    };
    std::vector<graph_pt_t> pts;
    pts.reserve(s_graph.size());
    const int count = (int)s_graph.size();
    for (int i = 0; i < count; ++i) {
        const float x = left + width - (float)(count - 1 - i) * step;
        const float y = base_y - std::min(s_graph[i].m_speed / top, 1.f) * height;
        pts.push_back({ ImVec2(x, y), edge_alpha(x), (int)std::round(s_graph[i].m_speed) });
    }

    const ImVec4 line(g_vgColor[0], g_vgColor[1], g_vgColor[2], g_vgColor[3]);
    const ImVec4 gain(g_vgColorGain[0], g_vgColorGain[1], g_vgColorGain[2], g_vgColorGain[3]);
    const ImVec4 loss(g_vgColorLoss[0], g_vgColorLoss[1], g_vgColorLoss[2], g_vgColorLoss[3]);
    const ImVec4 fill(g_vgFillColor[0], g_vgFillColor[1], g_vgFillColor[2], g_vgFillColor[3]);
    const bool   gain_colors = g_vgGainColors;
    const bool   fill_on     = g_vgFill;
    const float  thickness   = std::max(g_vgThickness, 0.5f);

    ImDrawList* const dl = draw_list();
    const auto with_a = [](ImVec4 c, const float a) {
        c.w *= a;
        return ImGui::ColorConvertFloat4ToU32(c);
    };

    if (fill_on) {
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        for (size_t i = 1; i < pts.size(); ++i) {
            const graph_pt_t& a = pts[i - 1];
            const graph_pt_t& b = pts[i];
            // reserve first: a 16-bit index overflow moves to a new vtx offset and resets _VtxCurrentIdx
            dl->PrimReserve(6, 4);
            const ImDrawIdx idx = (ImDrawIdx)dl->_VtxCurrentIdx;
            dl->PrimWriteIdx(idx);
            dl->PrimWriteIdx((ImDrawIdx)(idx + 1));
            dl->PrimWriteIdx((ImDrawIdx)(idx + 2));
            dl->PrimWriteIdx(idx);
            dl->PrimWriteIdx((ImDrawIdx)(idx + 2));
            dl->PrimWriteIdx((ImDrawIdx)(idx + 3));
            dl->PrimWriteVtx(a.m_at, uv, with_a(fill, a.m_a));
            dl->PrimWriteVtx(b.m_at, uv, with_a(fill, b.m_a));
            dl->PrimWriteVtx(ImVec2(b.m_at.x, base_y), uv, with_a(fill, 0.f));
            dl->PrimWriteVtx(ImVec2(a.m_at.x, base_y), uv, with_a(fill, 0.f));
        }
    }

    for (size_t i = 1; i < pts.size(); ++i) {
        const graph_pt_t& a = pts[i - 1];
        const graph_pt_t& b = pts[i];
        const ImVec4&     c = !gain_colors || b.m_speed == a.m_speed ? line : b.m_speed > a.m_speed ? gain : loss;
        dl->AddLine(a.m_at, b.m_at, with_a(c, (a.m_a + b.m_a) * 0.5f), thickness);
    }

    if (!g_vgJumpSpeeds)
        return;

    float   fs   = 0.f;
    ImFont* font = IndicatorFont(g_vgTextScale, fs);
    if (!font)
        return;

    const ImVec4        text(g_vgTextColor[0], g_vgTextColor[1], g_vgTextColor[2], g_vgTextColor[3]);
    const shadow_opts_t shadow = { g_vgTextShadow, 0.f };
    for (int i = 0; i < count; ++i) {
        if (!s_graph[i].m_take_off || pts[i].m_a <= 0.f)
            continue;
        char label[16];
        std::snprintf(label, sizeof(label), "%d", pts[i].m_speed);
        const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, label);
        ImVec4 c = text;
        c.w *= pts[i].m_a;
        push_label(font, fs, ImVec2(std::round(pts[i].m_at.x - size.x * 0.5f), std::round(pts[i].m_at.y - size.y - 2.f)), label, u32(c),
                   u32(ImVec4(0.f, 0.f, 0.f, c.w)), shadow);
    }
}

// botox on_paint_traverse (movement half). No live cmd in the last 250 ms = dead / not in game:
// botox's g_ctx.m_local / is_alive gates
void RenderIndicators()
{
    MvInd_t s{};
    MV_GetIndicators(s);
    const bool alive = s.ms != 0 && GetTickCount64() - s.ms <= 250;

    const bool on_ground = s.onGround;

    if (alive && g_viEnable)
        velocity(s, on_ground);

    if (alive && g_siEnable)
        stamina(s, on_ground);

    velocity_graph(s, on_ground, alive);

    if (alive && g_kiEnable)
        keybind_indicators(s);
    key_particles();

    if (alive && g_kpEnable)
        key_press(s);
}
