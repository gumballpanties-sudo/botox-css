#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// botox globals/render m_draw_data, TF2 shape. botox queues its esp as draw objects on the game
// thread (paint traverse), swaps the finished set under a lock and replays it into ImGui's
// background list on the render thread (draw_cached_data). Same here: esp.cpp (aim/ hooks.cpp
// IEngineVGui::Paint, game thread) records screen-space commands, render.cpp RenderEspQueue()
// replays them inside BuildImGuiFrame -- where the ImGui context, the esp face and the avatar
// textures are valid. POD only: this header is shared by the aim TU (no dih globals) and render.cpp.
//
// Text is anchored, not pre-measured: the game thread cannot touch the font atlas (a rebuild frees
// it on the render thread), so a text command carries an align factor and the replay subtracts
// width * ax. Line height is all the layout needs on the game thread: g_espQFontH.

enum EspCmdType : uint8_t
{
    ESPC_LINE,       // x0,y0 -> x1,y1  col  thick
    ESPC_RECT,       // botox impl_t::rect: min x0,y0 max x1,y1, col, col2 = outline, round, thick, othick, flags
    ESPC_GRAD,       // vertical gradient rect: col top, col2 bottom, round
    ESPC_CORNER,     // corner_rect_draw_object_t: min/max, ax/ay arms, round, col, thick
    ESPC_POLY,       // convex fill col, optional closed outline col2 / thick (flags & ESPF_OUTLINE)
    ESPC_POLYLINE,   // open polyline col thick; ESPF_GRADIENT = col top -> col2 bottom over y0..y1
    ESPC_CIRCLE,     // centre x0,y0 radius x1, col, thick
    ESPC_TEXT,       // top-left x0,y0 after x -= w*ax, y -= h*ay; col, col2 shadow (ESPF_SHADOW)
    ESPC_AVATAR,     // steam avatar sid: min x0,y0 max x1,y1, round, alpha in col; ESPF_LEFT_OF_TEXT
    ESPC_WPNICON,    // CS:S weapon icon glyph (aim/esp_weapon_icons.h): min x0,y0 max x1,y1, col,
                     // p0 = CSWeaponID. Replay resolves p0 -> texture via EspWeaponIcon_Get, same
                     // lazy-texture shape as ESPC_AVATAR/Avatar_Get, just render-thread-local
                     // (not a Steam async fetch) since the font file is read straight off disk.
};

enum : uint8_t
{
    ESPF_FILLED     = 1 << 0,
    ESPF_OUT_INNER  = 1 << 1,   // e_rect_flags rect_flag_inner_outline
    ESPF_OUT_OUTER  = 1 << 2,   // e_rect_flags rect_flag_outer_outline
    ESPF_OUTLINE    = 1 << 3,
    ESPF_GRADIENT   = 1 << 4,
    ESPF_SHADOW     = 1 << 5,
    ESPF_LEFT_OF_TEXT = 1 << 6, // avatar: x0 is an offset from the last text's left edge (botox name avatar)
};

struct EspCmd
{
    uint8_t  type = 0, flags = 0;
    float    x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t col = 0, col2 = 0;
    float    thick = 1.f, round = 0.f, othick = 1.f, ax = 0.f, ay = 0.f;
    int      p0 = 0, pn = 0;      // point pool [p0, p0+pn) as x,y pairs; text: char pool offset
    uint64_t sid = 0;
};

struct EspFrame
{
    std::vector<EspCmd> cmds;
    std::vector<float>  pts;
    std::vector<char>   text;
    uint64_t            stampMs = 0;   // GetTickCount64 at commit; a stale frame is not drawn
    uint32_t            seq = 0;       // which Commit this is (0 = never written)
    float               srcW = 0, srcH = 0;   // engine screen size the coords were projected for
    void Clear() { cmds.clear(); pts.clear(); text.clear(); }
};

// botox draws with the esp font's line height; render.cpp publishes it after every atlas build.
inline std::atomic<float> g_espQFontH{ 11.f };

inline std::atomic_flag g_espQLock = ATOMIC_FLAG_INIT;
inline EspFrame g_espQBuild;    // game thread only
// MULTICORE. With mat_queue_mode on, the main thread paints frame N+1 while the render thread is
// still replaying frame N; drawing the newest esp there puts N+1's boxes on N's world and they
// slide while the view turns. So committed frames are kept in a small ring, render.cpp's call-queue
// functor carries the sequence number committed at that frame's vgui point (OvQueue), and the
// replay picks that exact frame. The main thread runs at most a frame or two ahead: 4 is plenty.
inline constexpr int k_espQRing = 4;
inline EspFrame g_espQRing[k_espQRing];   // under g_espQLock
inline std::atomic<uint32_t> g_espQSeq{ 0 };   // last committed seq

struct EspQLockGuard
{
    EspQLockGuard()  { while (g_espQLock.test_and_set(std::memory_order_acquire)) {} }
    ~EspQLockGuard() { g_espQLock.clear(std::memory_order_release); }
};

// IM_COL32 layout (ABGR), from 0..1 floats and an extra alpha multiplier (botox get_u32(alpha))
inline uint32_t EspCol(const float* c, float alphaMul = 1.f)
{
    auto b = [](float v) { v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v); return (uint32_t)(v * 255.f + 0.5f); };
    return b(c[0]) | (b(c[1]) << 8) | (b(c[2]) << 16) | (b(c[3] * alphaMul) << 24);
}
inline uint32_t EspColRGBA(float r, float g, float b, float a) { const float c[4] = { r, g, b, a }; return EspCol(c); }

namespace EspQ
{
    inline EspCmd& Push(uint8_t type) { g_espQBuild.cmds.emplace_back(); EspCmd& c = g_espQBuild.cmds.back(); c.type = type; return c; }

    inline void Line(float x0, float y0, float x1, float y1, uint32_t col, float thick)
    {
        EspCmd& c = Push(ESPC_LINE); c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.col = col; c.thick = thick;
    }
    // botox rect_draw_object_t, same argument order
    inline void Rect(float x0, float y0, float x1, float y1, uint32_t col, uint32_t outline, bool filled, float round,
                     float thick, uint8_t outlineFlags, float othick = 1.f)
    {
        EspCmd& c = Push(ESPC_RECT); c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.col = col; c.col2 = outline;
        c.flags = (uint8_t)((filled ? ESPF_FILLED : 0) | outlineFlags); c.round = round; c.thick = thick; c.othick = othick;
    }
    inline void Grad(float x0, float y0, float x1, float y1, uint32_t top, uint32_t bottom, float round = 0.f)
    {
        EspCmd& c = Push(ESPC_GRAD); c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.col = top; c.col2 = bottom; c.round = round;
    }
    // botox impl_t::corner_rect: the arms are a third of the width / a fifth of the height
    inline void CornerRect(float x1, float y1, float x2, float y2, uint32_t col, float thick, float rounding)
    {
        const int w = (int)(x2 - x1), h = (int)(y2 - y1), iw = w / 3, ih = h / 5;
        EspCmd& c = Push(ESPC_CORNER);
        c.x0 = x1 + 0.5f; c.y0 = y1 + 0.5f; c.x1 = x2 - 0.5f; c.y1 = y2 + 0.5f;
        c.ax = (float)iw; c.ay = (float)ih;
        const float r = rounding > 0.f ? rounding : 0.f, cap = (float)(iw < ih ? iw : ih);
        c.round = r < cap ? r : cap; c.col = col; c.thick = thick;
    }
    inline int Points(const float* xy, int n)
    {
        const int at = (int)g_espQBuild.pts.size() / 2;
        g_espQBuild.pts.insert(g_espQBuild.pts.end(), xy, xy + n * 2);
        return at;
    }
    inline void Poly(const float* xy, int n, uint32_t col, bool outline = false, uint32_t outlineCol = 0, float thick = 1.f)
    {
        EspCmd& c = Push(ESPC_POLY); c.p0 = Points(xy, n); c.pn = n; c.col = col; c.col2 = outlineCol; c.thick = thick;
        c.flags = outline ? ESPF_OUTLINE : 0;
    }
    inline void Polyline(const float* xy, int n, uint32_t col, float thick)
    {
        if (n < 2) return;
        EspCmd& c = Push(ESPC_POLYLINE); c.p0 = Points(xy, n); c.pn = n; c.col = col; c.thick = thick;
    }
    // botox corner health bar callback: fill vertices re-coloured top -> bottom over [yTop, yBottom]
    inline void PolylineGrad(const float* xy, int n, uint32_t top, uint32_t bottom, float thick, float yTop, float yBottom)
    {
        if (n < 2) return;
        EspCmd& c = Push(ESPC_POLYLINE); c.p0 = Points(xy, n); c.pn = n; c.col = top; c.col2 = bottom; c.thick = thick;
        c.flags = ESPF_GRADIENT; c.y0 = yTop; c.y1 = yBottom;
    }
    inline void Circle(float x, float y, float r, uint32_t col, float thick)
    {
        EspCmd& c = Push(ESPC_CIRCLE); c.x0 = x; c.y0 = y; c.x1 = r; c.col = col; c.thick = thick;
    }
    inline void Text(float x, float y, const char* s, uint32_t col, uint32_t shadow, float ax = 0.f, float ay = 0.f)
    {
        if (!s || !*s) return;
        EspCmd& c = Push(ESPC_TEXT); c.x0 = x; c.y0 = y; c.col = col; c.col2 = shadow; c.ax = ax; c.ay = ay; c.flags = ESPF_SHADOW;
        c.p0 = (int)g_espQBuild.text.size();
        g_espQBuild.text.insert(g_espQBuild.text.end(), s, s + strlen(s) + 1);
    }
    inline void Avatar(float x0, float y0, float x1, float y1, uint64_t sid, float alpha, float round, bool leftOfText = false)
    {
        if (!sid) return;
        EspCmd& c = Push(ESPC_AVATAR); c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.sid = sid; c.round = round;
        c.col = EspColRGBA(1.f, 1.f, 1.f, alpha); c.flags = leftOfText ? ESPF_LEFT_OF_TEXT : 0;
    }
    // CSWeaponID weapon icon glyph, centered in [x0,y0]-[x1,y1] respecting the real glyph aspect
    // (render.cpp resizes the shorter axis down -- see EspWeaponIcon_Get's outAspect). col carries
    // the caller's tint * alpha, same as every other esp colour.
    inline void WeaponIcon(float x0, float y0, float x1, float y1, int weaponId, uint32_t col)
    {
        if (weaponId <= 0) return;
        EspCmd& c = Push(ESPC_WPNICON); c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1; c.p0 = weaponId; c.col = col;
    }

    inline void Begin() { g_espQBuild.Clear(); }
    // botox swap_draw_data, into the next ring slot
    inline void Commit(uint64_t nowMs, float screenW, float screenH)
    {
        const uint32_t seq = g_espQSeq.load(std::memory_order_relaxed) + 1;
        {
            EspQLockGuard _g;
            EspFrame& slot = g_espQRing[seq % k_espQRing];
            std::swap(g_espQBuild.cmds, slot.cmds);
            std::swap(g_espQBuild.pts, slot.pts);
            std::swap(g_espQBuild.text, slot.text);
            slot.stampMs = nowMs;
            slot.seq = seq;
            slot.srcW = screenW; slot.srcH = screenH;
        }
        g_espQSeq.store(seq, std::memory_order_release);
    }

    // render thread: copy frame `want` out (0 or already overwritten = the newest one)
    inline void CopyFrame(uint32_t want, EspFrame& out)
    {
        EspQLockGuard _g;
        const uint32_t last = g_espQSeq.load(std::memory_order_acquire);
        const EspFrame* f = &g_espQRing[last % k_espQRing];
        if (want && g_espQRing[want % k_espQRing].seq == want)
            f = &g_espQRing[want % k_espQRing];
        out.cmds = f->cmds; out.pts = f->pts; out.text = f->text; out.stampMs = f->stampMs; out.seq = f->seq;
        out.srcW = f->srcW; out.srcH = f->srcH;
    }
}
