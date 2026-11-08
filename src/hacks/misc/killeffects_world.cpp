#include "killeffects.h"
#include "../../render/render.h"       // W2S_Project
#include "../movement/movement.h"     // Vec3, Ray_mv, GameTrace_t, kTrace_TraceRay
#include "../movement/prediction.h"   // EntV3
#include "unitE_logic.h"  // n_unitE::McXpValue/McXpIcon
#include "mc_xp_orb.h"    // botox hacks/misc/mc_xp_orb.h, verbatim (experience_orb.png)
#include <vector>
#include <random>
#include <mutex>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <climits>
#include <algorithm>

// botox hacks/misc/kill_effects.cpp custom-particle styles (soul / smoke / balls / mc), spawn +
// step + alpha + shapes 1:1, drawn as ImGui billboards at W2S_Project(pos) with the radius
// projected from world units. botox draws them as world quads through its own shaders (disc,
// glow rings, 4 point star, fbm smoke); the same shapes are built from imgui primitives here.
// Through walls (no depth test) and floor-clamped instead of hull traces: the step runs on the
// render thread, where IEngineTrace is not ours to call.
//
// model colors: botox capture_send / kill_effects_capture -- one back buffer read answers every
// waiting particle with the pixel under its body point (visible only: the eye -> point trace runs
// at spawn, on the game thread), unanswered after 0.7 s = keeps the picked colour.

namespace
{
    constexpr size_t kKfxCap = 4096;   // botox k_kfx_cap
    constexpr float kMcBlock = 40.f;   // 1 minecraft block = 40u (1.8 block player = 72u)
    constexpr int kWant = -2;          // botox k_kfx_want: waiting for its model colour, hidden

    enum : int { kSoul = 5, kSmoke = 3, kBalls = 6, kMcCrit = 8, kMcXp = 9 };

    struct KfxParticle
    {
        int   style = 0;
        int   soulStyle = 0;       // botox m_style for souls: 0 energy 1 heavy 2 light electrical 3 core
        Vec3  pos{}, vel{}, prev{};
        float age = 0.f, life = 1.f, size = 1.f, seed = 0.f;
        float floorZ = -FLT_MAX;   // balls / xp: clamp instead of a hull trace
        bool  resting = false;
        float color[4] = { 1.f, 1.f, 1.f, 1.f };
        // minecraft: ticks at 20 hz, vel = u / tick
        float tickAcc = 0.f;
        float tint[3] = { 1.f, 1.f, 1.f };
        int   tick = 0, lifetime = 0, icon = 0;
        // model colors: sample = body point, capture kWant = queued, -1 = answered / none
        Vec3  sample{};
        int   capture = -1;
    };

    std::mutex g_kfxMtx;           // FireEvent thread spawns, the render thread steps + draws
    std::vector<KfxParticle> g_kfx;
    std::mt19937 g_rng(0x6b667821u);

    float Rand01() { return std::uniform_real_distribution<float>(0.f, 1.f)(g_rng); }
    float RandRange(float lo, float hi) { return lo + (hi - lo) * Rand01(); }

    void Push(const KfxParticle& p) { if (g_kfx.size() < kKfxCap) g_kfx.push_back(p); }

    // botox particle_color: picked colour, or c_color::from_hsb( random, 0.75, 1, alpha )
    void ParticleColor(float out[4])
    {
        if (g_dpMulticolor)
        {
            const float h = Rand01() * 6.f, s = 0.75f;
            const int i = (int)h % 6;
            const float f = h - floorf(h), p = 1.f - s, q = 1.f - s * f, t = 1.f - s * (1.f - f);
            const float rgb[6][3] = { { 1, t, p }, { q, 1, p }, { p, 1, t }, { p, q, 1 }, { t, p, 1 }, { 1, p, q } };
            out[0] = rgb[i][0]; out[1] = rgb[i][1]; out[2] = rgb[i][2];
        }
        else { out[0] = g_dpColor[0]; out[1] = g_dpColor[1]; out[2] = g_dpColor[2]; }
        out[3] = g_dpColor[3];
    }

    // ---- model colors: visibility at spawn (game thread) -------------------------------------
    struct KfxFilter
    {
        void* vfptr; void* skip;
        static bool __fastcall ShouldHit(KfxFilter* self, void* ent, int) { return ent && ent != self->skip; }
        static int __fastcall TraceType(KfxFilter*) { return 0; }
    };

    bool g_eyeOk = false;
    Vec3 g_eye{};
    void* g_eyeSkip = nullptr;

    // botox melt_eye: the local eye, for the k_melt_see trace (CONTENTS_SOLID | CONTENTS_MOVEABLE)
    void ResolveEye()
    {
        g_eyeOk = false;
        const int local = GetLocalPlayerIndex();
        void* p = local > 0 ? GetClientEntity(local) : nullptr;
        if (!p || g_offOrigin <= 0) return;
        __try
        {
            g_eye = EntV3(p, g_offOrigin) + (g_offViewOffset > 0 ? EntV3(p, g_offViewOffset) : Vec3(0.f, 0.f, 68.f));
            g_eyeSkip = p;
            g_eyeOk = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // SEH leaf, no unwindable locals (C2712)
    bool TraceLeaf(void* trace, const Ray_mv& ray, void* filter, GameTrace_t* tr)
    {
        using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
        __try { ((TraceRay_t)(*(void***)trace)[kTrace_TraceRay])(trace, ray, 0x1 | 0x4000, filter, tr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Visible(const Vec3& at)
    {
        static void* trace = GetInterface("engine.dll", "EngineTraceClient003");
        if (!trace || !g_eyeOk) return true;   // no trace = sample anyway, the pixel decides
        static void* vt[2] = { (void*)&KfxFilter::ShouldHit, (void*)&KfxFilter::TraceType };
        KfxFilter filter{ vt, g_eyeSkip };
        Ray_mv ray;
        ray.Init(g_eye, at);
        GameTrace_t tr{};
        if (!TraceLeaf(trace, ray, &filter, &tr)) return true;
        return !tr.startsolid && tr.fraction >= 0.99f;
    }

    // botox want_body_color; points share ~8 positions: one trace per point, cached by the caller
    void WantBodyColor(KfxParticle& p, const Vec3& at, bool visible)
    {
        p.sample  = at;
        p.capture = visible ? kWant : -1;
    }

    // botox body_points no-bones fallback: origin + 8 points 8..72u up (this dll has no hitbox resolver)
    void BodyPoints(const Vec3& feet, Vec3 (&points)[8], bool (&visible)[8], bool trace)
    {
        if (trace) ResolveEye();
        for (int i = 0; i < 8; ++i)
        {
            points[i]  = feet + Vec3(0.f, 0.f, 8.f + 8.f * (float)i);
            visible[i] = trace && Visible(points[i]);
        }
    }

    // botox mc_tick ( CritParticle, 1.21.11 ), one 20 hz tick
    void McTick(KfxParticle& c)
    {
        c.prev = c.pos;
        if (c.tick++ >= c.lifetime) { c.age = c.life; return; }
        c.vel.z -= 0.04f * 0.5f * kMcBlock;
        c.pos = c.pos + c.vel;
        c.vel = c.vel * 0.7f;
        c.tint[1] *= 0.96f;
        c.tint[2] *= 0.9f;
    }
}

// ==== spawn: soul (botox spawn_soul, 1:1) =======================================================
void KfxPush_Soul(const Vec3& feet)
{
    const int   style  = std::clamp(g_dpSoulStyle, 0, 3);
    const int   amount = std::clamp(g_dpAmount, 1, 40);
    const float size   = std::clamp(g_dpSize, 0.25f, 4.f);
    const float life   = std::clamp(g_dpLifetime, 0.5f, 10.f);
    const bool  body   = g_dpModelColors;

    Vec3 points[8]; bool visible[8];
    BodyPoints(feet, points, visible, body);
    const Vec3 core = points[0] + Vec3(0.f, 0.f, 40.f);

    std::lock_guard<std::mutex> lock(g_kfxMtx);
    for (int pt = 0; pt < 8; ++pt)
    {
        const Vec3& point = points[pt];
        for (int i = 0; i < amount; ++i)
        {
            KfxParticle p{};
            p.pos = point + Vec3(RandRange(-5.f, 5.f), RandRange(-5.f, 5.f), RandRange(-5.f, 5.f));
            p.style = kSoul;
            p.soulStyle = style;

            const float angle = RandRange(0.f, 6.2831853f);
            switch (style)
            {
            case 0: { const float s = RandRange(8.f, 30.f);
                      p.vel = Vec3(cosf(angle) * s, sinf(angle) * s, RandRange(4.f, 15.f)); break; }
            case 1: p.vel = Vec3(RandRange(-2.5f, 2.5f), RandRange(-2.5f, 2.5f), RandRange(12.f, 45.f)); break;
            case 2: { const float s = RandRange(15.f, 50.f);
                      p.vel = Vec3(cosf(angle) * s, sinf(angle) * s, RandRange(-10.f, 10.f)); break; }
            default:
            {
                Vec3 toCore = core - p.pos;
                const float len = toCore.Length();
                if (len > 0.1f) toCore = toCore * (1.f / len);
                p.vel = toCore * RandRange(20.f, 40.f);
                break;
            }
            }

            p.size = RandRange(1.f, 2.f) * size;
            p.life = RandRange(0.7f, 1.f) * life;
            p.seed = Rand01();
            ParticleColor(p.color);
            if (body) WantBodyColor(p, point, visible[pt]);
            Push(p);
        }
    }
}

// ==== spawn: smoke puff (botox spawn_smoke, its no-bones path: amount * 8 puffs, radius 6) ======
void KfxPush_Smoke(const Vec3& feet)
{
    const int   amount = std::clamp(g_dpAmount, 1, 40);
    const float size   = std::clamp(g_dpSize, 0.25f, 3.f);
    const float life   = std::clamp(g_dpLifetime, 0.5f, 10.f);

    std::lock_guard<std::mutex> lock(g_kfxMtx);
    for (int i = 0; i < amount * 8; ++i)
    {
        const float z = RandRange(-1.f, 1.f), angle = RandRange(0.f, 6.2831853f);
        const float r = sqrtf(std::max(0.f, 1.f - z * z));
        const Vec3 dir(cosf(angle) * r, sinf(angle) * r, z);

        KfxParticle puff{};
        puff.style = kSmoke;
        puff.pos   = feet + Vec3(0.f, 0.f, RandRange(6.f, 66.f)) + dir * RandRange(0.f, 6.f);
        puff.vel   = dir * RandRange(2.f, 9.f) + Vec3(0.f, 0.f, RandRange(2.f, 8.f));
        puff.size  = 6.f * RandRange(0.75f, 1.15f) * size;
        puff.life  = RandRange(0.6f, 1.f) * life;
        puff.seed  = Rand01();
        const float shade = (float)(int)RandRange(150.f, 205.f) / 255.f;
        puff.color[0] = puff.color[1] = puff.color[2] = shade; puff.color[3] = 1.f;
        Push(puff);
    }
}

// ==== spawn: bouncy balls (botox spawn_balls; floor-clamped at the victim's feet) ===============
void KfxPush_Balls(const Vec3& feet)
{
    const int   amount = std::clamp(g_dpAmount, 5, 100);
    const float size   = std::clamp(g_dpSize, 0.5f, 4.f);
    const float life   = std::clamp(g_dpLifetime, 1.f, 8.f);
    const Vec3  origin = feet + Vec3(0.f, 0.f, 40.f);
    const bool  body   = g_dpModelColors;   // each ball pops out of a random body part and takes its colour

    Vec3 points[8]; bool visible[8];
    BodyPoints(feet, points, visible, body);

    std::lock_guard<std::mutex> lock(g_kfxMtx);
    for (int i = 0; i < amount; ++i)
    {
        KfxParticle ball{};
        if (!body)
            ball.pos = origin + Vec3(RandRange(-8.f, 8.f), RandRange(-8.f, 8.f), RandRange(-4.f, 6.f));
        else
        {
            const int pt = std::uniform_int_distribution<int>(0, 7)(g_rng);
            WantBodyColor(ball, points[pt], visible[pt]);
            ball.pos = points[pt] + Vec3(RandRange(-1.5f, 1.5f), RandRange(-1.5f, 1.5f), RandRange(-1.5f, 1.5f));
        }
        const float angle = RandRange(0.f, 6.2831853f);
        const float speed = RandRange(80.f, 260.f);
        ball.vel    = Vec3(cosf(angle) * speed, sinf(angle) * speed, RandRange(180.f, 420.f));
        ball.style  = kBalls;
        ball.size   = RandRange(1.6f, 3.2f) * size;
        ball.life   = RandRange(0.8f, 1.2f) * life;
        ball.floorZ = feet.z;
        ParticleColor(ball.color);
        Push(ball);
    }
}

// ==== spawn: minecraft crit x's + xp orbs (botox spawn_mc / spawn_mc_xp, 1:1, fixed OBB) ======
void KfxPush_Mc(const Vec3& feet, const Vec3& obbMins, const Vec3& obbMaxs)
{
    const float size = std::clamp(g_dpSize, 0.25f, 3.f);
    const int   mode = std::clamp(g_dpMcMode, 0, 2);
    const float xpLife = std::clamp(g_dpMcXpTime, 1.f, 60.f);

    std::lock_guard<std::mutex> lock(g_kfxMtx);
    if (mode != 0)
    {
        const int count = std::uniform_int_distribution<int>(4, 10)(g_rng);
        for (int i = 0; i < count; ++i)
        {
            KfxParticle orb{};
            orb.style = kMcXp;
            orb.life  = xpLife;
            orb.pos   = orb.prev = feet;
            orb.vel   = Vec3((Rand01() * 0.2f - 0.1f) * 2.f, (Rand01() * 0.2f - 0.1f) * 2.f, Rand01() * 0.2f * 2.f) * kMcBlock;
            orb.size  = size;
            orb.floorZ = feet.z;
            orb.icon  = n_unitE::McXpIcon(n_unitE::McXpValue(std::uniform_int_distribution<int>(1, 40)(g_rng)));
            Push(orb);
        }
    }
    if (mode == 2) return;

    const float width  = obbMaxs.x - obbMins.x;
    const float height = obbMaxs.z - obbMins.z;

    for (int burst = 0; burst < 3; ++burst)
        for (int i = 0; i < 16; ++i)
        {
            const float d = RandRange(-1.f, 1.f), e = RandRange(-1.f, 1.f), f = RandRange(-1.f, 1.f);
            if (d * d + e * e + f * f > 1.f) continue;

            KfxParticle crit{};
            crit.style   = kMcCrit;
            crit.life    = 10.f;
            crit.tickAcc = -(float)burst;   // emitter tick not reached yet
            crit.pos     = feet + Vec3(width * d / 4.f, width * f / 4.f, obbMins.z + height * (0.5f + e / 4.f));

            Vec3 base(RandRange(-1.f, 1.f), RandRange(-1.f, 1.f), RandRange(-1.f, 1.f));
            const float len = base.Length();
            base = len > 1e-4f ? base * ((Rand01() + Rand01() + 1.f) * 0.15f * 0.4f / len) : Vec3();
            base.z += 0.1f;
            crit.vel = (base * 0.1f + Vec3(d, f, e + 0.2f) * 0.4f) * kMcBlock;

            const float shade = Rand01() * 0.3f + 0.6f;
            crit.tint[0] = crit.tint[1] = crit.tint[2] = shade;
            crit.size     = 0.1f * (Rand01() * 0.5f + 0.5f) * 2.f * 0.75f * kMcBlock * size;
            crit.lifetime = std::max((int)(6.f / (Rand01() * 0.8f + 0.6f)), 1);
            McTick(crit);   // CritParticle ctor ticks once
            Push(crit);
        }
}

// ==== step + draw, driven by RenderKillEffects (imgui frame build, render thread) ================
namespace
{
    // botox step_smoke / step_soul / step_ball (ball: floor plane instead of the trace)
    void StepSmoke(KfxParticle& p, float dt)
    {
        p.vel = p.vel * std::max(0.f, 1.f - 2.5f * dt);
        p.vel.z += 6.f * dt;
        p.pos = p.pos + p.vel * dt;
    }

    void StepSoul(KfxParticle& p, float dt)
    {
        p.vel.z += (p.soulStyle == 3 ? 6.f : 14.f) * dt;
        p.vel = p.vel * std::max(0.f, 1.f - 0.6f * dt);
        if (p.soulStyle == 1 || p.soulStyle == 2)
        {
            const float jitter = p.soulStyle == 1 ? 1.2f : 0.6f;
            p.pos.x += RandRange(-jitter, jitter);
            p.pos.y += RandRange(-jitter, jitter);
        }
        p.pos = p.pos + p.vel * dt;
    }

    void StepBall(KfxParticle& b, float dt)
    {
        if (b.resting) return;
        b.vel.z -= 640.f * dt;
        Vec3 next = b.pos + b.vel * dt;
        if (next.z <= b.floorZ && b.vel.z < 0.f)
        {
            // botox reflect off a floor normal: tangent * 0.85, normal * -0.62
            next.z = b.floorZ + 0.25f;
            b.vel = Vec3(b.vel.x * 0.85f, b.vel.y * 0.85f, -b.vel.z * 0.62f);
            if (b.vel.Length() < 24.f) { b.vel = Vec3(); b.resting = true; }
        }
        b.pos = next;
    }

    // botox mc_xp_tick ( ExperienceOrb ): gravity 0.03, drag 0.98, ground 0.6 * 0.98, bounce 0.4
    void McXpTick(KfxParticle& o)
    {
        o.prev = o.pos;
        o.vel.z -= 0.03f * kMcBlock;
        const float fall = o.vel.z;
        o.pos = o.pos + o.vel;
        bool ground = false;
        if (o.pos.z <= o.floorZ) { o.pos.z = o.floorZ; ground = fall < 0.f; o.vel.z = 0.f; }
        o.vel = o.vel * (ground ? 0.6f * 0.98f : 0.98f);
        if (ground && fall < -0.03f * kMcBlock) o.vel.z = -fall * 0.4f;
    }

    // world radius -> screen px: the largest of the three axis offsets (one is always ~across the view)
    float PixelRadius(const Vec3& p, const ImVec2& s, float r)
    {
        float best = 0.f;
        ImVec2 o;
        const Vec3 axes[3] = { Vec3(r, 0.f, 0.f), Vec3(0.f, r, 0.f), Vec3(0.f, 0.f, r) };
        for (const Vec3& a : axes)
            if (W2S_Project(p + a, o)) best = std::max(best, sqrtf((o.x - s.x) * (o.x - s.x) + (o.y - s.y) * (o.y - s.y)));
        return std::max(best, 0.75f);
    }

    ImU32 Col(const float c[4], float alpha)
    {
        return IM_COL32((int)(std::clamp(c[0], 0.f, 1.f) * 255.f), (int)(std::clamp(c[1], 0.f, 1.f) * 255.f),
                        (int)(std::clamp(c[2], 0.f, 1.f) * 255.f), (int)(std::clamp(c[3] * alpha, 0.f, 1.f) * 255.f));
    }

    // botox kfx glow: rings 0.4 / 1 / 1.7 / 2.8 radii at alpha 1 / 0.5 / 0.16 / 0, stacked discs
    void Glow(ImDrawList* dl, ImVec2 s, float r, const float c[4], float alpha)
    {
        dl->AddCircleFilled(s, r * 2.8f, Col(c, alpha * 0.07f), 24);
        dl->AddCircleFilled(s, r * 1.7f, Col(c, alpha * 0.1f), 20);
        dl->AddCircleFilled(s, r * 1.0f, Col(c, alpha * 0.4f), 16);
        dl->AddCircleFilled(s, r * 0.4f, Col(c, alpha), 12);
    }

    // botox kfx star: 4 points, tip 1 / notch 0.43, orientation = seed
    void Star(ImDrawList* dl, ImVec2 s, float r, float spin, ImU32 col)
    {
        ImVec2 v[8];
        for (int i = 0; i < 8; ++i)
        {
            const float a = spin * 6.2831853f + (float)i * 0.78539816f;
            const float k = (i & 1) ? 0.431f : 1.f;
            v[i] = ImVec2(s.x + cosf(a) * r * k, s.y - sinf(a) * r * k);
        }
        for (int i = 0; i < 8; ++i) dl->AddTriangleFilled(s, v[i], v[(i + 1) & 7], col);
    }

    // botox kfx_sprite: pixel art spanning -1..1, one square per texel
    template <typename T>
    void Sprite(ImDrawList* dl, ImVec2 s, float r, int texels, T&& texel)
    {
        const float step = 2.f * r / (float)texels;
        for (int y = 0; y < texels; ++y)
            for (int x = 0; x < texels; ++x)
                if (const ImU32 col = texel(x, y))
                {
                    const float x0 = s.x - r + step * x, y0 = s.y - r + step * y;
                    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + step, y0 + step), col);
                }
    }

    // textures/particle/critical_hit.png rgb, 0 = clear (botox k_mc_crit)
    constexpr uint32_t k_mc_crit[8][8] = {
        { 0xffffff, 0, 0, 0, 0, 0, 0xffffff, 0 },
        { 0, 0xffffff, 0xcecece, 0, 0xcecece, 0xffffff, 0, 0 },
        { 0, 0xcecece, 0xffffff, 0xcecece, 0xffffff, 0xcecece, 0, 0 },
        { 0, 0, 0xcecece, 0xffffff, 0xcecece, 0, 0, 0 },
        { 0, 0xcecece, 0xffffff, 0xcecece, 0xffffff, 0xcecece, 0, 0 },
        { 0, 0xffffff, 0xcecece, 0, 0xcecece, 0xffffff, 0, 0 },
        { 0xffffff, 0, 0, 0, 0, 0, 0xffffff, 0 },
        { 0, 0, 0, 0, 0, 0, 0, 0 },
    };

    // botox capture_send + kill_effects_capture + capture_take, in one place: this IS the
    // render thread (frame build), so the back buffer is read right here.
    void CaptureColors()
    {
        struct Want { KfxParticle* p; long x, y; };
        std::vector<Want> wants;
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        for (KfxParticle& p : g_kfx)
        {
            if (p.capture != kWant) continue;
            if (p.age > 0.7f) { p.capture = -1; continue; }   // never answered: picked colour
            if (p.age < 0.05f) continue;                        // the ragdoll's first frame is not drawn yet
            ImVec2 s;
            if (!W2S_Project(p.sample, s) || s.x < 0.f || s.y < 0.f || s.x >= screen.x || s.y >= screen.y) { p.capture = -1; continue; }
            wants.push_back({ &p, (long)s.x, (long)s.y });
        }
        if (wants.empty()) return;
        for (Want& w : wants) w.p->capture = -1;   // answered or not, once

        IDirect3DDevice9* dev = g_d3dDev;
        IDirect3DSurface9 *bb = nullptr, *resolve = nullptr, *crop = nullptr, *sysmem = nullptr;
        IDirect3DTexture9* tex = nullptr;
        D3DSURFACE_DESC desc{};
        if (!dev || FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb || FAILED(bb->GetDesc(&desc)) ||
            (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8) || !desc.Width || !desc.Height)
        {
            if (bb) bb->Release();
            return;
        }

        // imgui px -> back buffer px
        const float sx = (float)desc.Width / std::max(screen.x, 1.f), sy = (float)desc.Height / std::max(screen.y, 1.f);
        long left = LONG_MAX, top = LONG_MAX, right = -1, bottom = -1;
        for (Want& w : wants)
        {
            w.x = std::clamp((long)(w.x * sx), 0l, (long)desc.Width - 1);
            w.y = std::clamp((long)(w.y * sy), 0l, (long)desc.Height - 1);
            left = std::min(left, w.x); top = std::min(top, w.y); right = std::max(right, w.x); bottom = std::max(bottom, w.y);
        }
        const UINT width = (UINT)(right - left + 1), height = (UINT)(bottom - top + 1);
        const RECT rect{ left, top, right + 1, bottom + 1 };
        const bool msaa = desc.MultiSampleType != D3DMULTISAMPLE_NONE;

        bool ok = SUCCEEDED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &tex, nullptr)) &&
                  SUCCEEDED(tex->GetSurfaceLevel(0, &crop)) &&
                  SUCCEEDED(dev->CreateOffscreenPlainSurface(width, height, desc.Format, D3DPOOL_SYSTEMMEM, &sysmem, nullptr));
        // msaa back buffer: full resolve first, sub-rect blits from msaa are not portable
        if (ok && msaa)
            ok = SUCCEEDED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolve, nullptr)) &&
                 SUCCEEDED(dev->StretchRect(bb, nullptr, resolve, nullptr, D3DTEXF_NONE));
        ok = ok && SUCCEEDED(dev->StretchRect(msaa ? resolve : bb, &rect, crop, nullptr, D3DTEXF_NONE));

        D3DLOCKED_RECT lr{};
        int answered = 0;
        if (ok && SUCCEEDED(dev->GetRenderTargetData(crop, sysmem)) && SUCCEEDED(sysmem->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
        {
            const unsigned char* bits = (const unsigned char*)lr.pBits;
            for (Want& w : wants)
            {
                const uint32_t px = *(const uint32_t*)(bits + (w.y - top) * lr.Pitch + (w.x - left) * 4);
                w.p->color[0] = (float)((px >> 16) & 0xff) / 255.f;
                w.p->color[1] = (float)((px >> 8) & 0xff) / 255.f;
                w.p->color[2] = (float)(px & 0xff) / 255.f;
                ++answered;
            }
            sysmem->UnlockRect();
        }

        if (sysmem) sysmem->Release();
        if (crop) crop->Release();
        if (tex) tex->Release();
        if (resolve) resolve->Release();
        bb->Release();

        static int s_logged = 0;
        if (s_logged < 4) { ++s_logged; DebugLog("KFX: model colors %d/%zu sampled (%ux%u crop)", answered, wants.size(), width, height); }
    }
}

void RenderKillEffects()
{
    std::lock_guard<std::mutex> lock(g_kfxMtx);
    if (!g_dpEnable || !IsInGame()) { g_kfx.clear(); return; }
    if (g_kfx.empty()) return;

    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.05f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool glow = g_dpGlow, stars = g_dpStars;

    CaptureColors();

    for (KfxParticle& p : g_kfx)
    {
        p.age += dt;
        if (p.age >= p.life) continue;

        if (p.style == kMcCrit)
        {
            for (p.tickAcc += dt * 20.f; p.tickAcc >= 1.f && p.age < p.life; p.tickAcc -= 1.f) McTick(p);
            if (p.tickAcc < 0.f || p.age >= p.life) continue;
            const Vec3 at = p.prev + (p.pos - p.prev) * p.tickAcc;
            ImVec2 s;
            if (!W2S_Project(at, s)) continue;
            const float r = PixelRadius(at, s, p.size);
            Sprite(dl, s, r, 8, [&](int x, int y) -> ImU32 {
                const uint32_t rgb = k_mc_crit[y][x];
                if (!rgb) return 0u;
                const auto mix = [&](int shift, int i) { return (int)(((rgb >> shift) & 0xff) * std::clamp(p.tint[i], 0.f, 1.f)); };
                return IM_COL32(mix(16, 0), mix(8, 1), mix(0, 2), 255);
            });
            continue;
        }

        if (p.style == kMcXp)
        {
            for (p.tickAcc += dt * 20.f; p.tickAcc >= 1.f; p.tickAcc -= 1.f) McXpTick(p);
            // ExperienceOrbRenderer: quad 0.3 block, raised 0.1 + 0.075, tint pulses with age, vertex alpha 128
            const float scale = p.size * 0.3f * kMcBlock;
            const Vec3 at = p.prev + (p.pos - p.prev) * p.tickAcc + Vec3(0.f, 0.f, 0.1f * kMcBlock + scale * 0.25f);
            const float phase = p.age * 20.f / 2.f;
            const int red  = (int)((sinf(phase) + 1.f) * 0.5f * 255.f);
            const int blue = (int)((sinf(phase + 4.18879f) + 1.f) * 0.1f * 255.f);
            const float fade = std::clamp((p.life - p.age) / std::min(1.5f, p.life * 0.5f), 0.f, 1.f);
            const int alpha = (int)(fade * fade * (3.f - 2.f * fade) * 128.f);
            ImVec2 s;
            if (!alpha || !W2S_Project(at, s)) continue;
            const float r = PixelRadius(at, s, scale * 0.5f);
            const char* const* orb = k_mc_xp_orb[std::clamp(p.icon, 0, 10)];
            Sprite(dl, s, r, 16, [&](int x, int y) -> ImU32 {
                const char key = orb[y][x];
                if (key == '.') return 0u;
                const uint32_t rgb = k_mc_xp_palette[key - 'a'];
                return IM_COL32(((rgb >> 16) & 0xff) * red / 255, (rgb >> 8) & 0xff, (rgb & 0xff) * blue / 255, alpha);
            });
            continue;
        }

        const bool smoke = p.style == kSmoke, ball = p.style == kBalls;
        if (ball) StepBall(p, dt);
        else if (smoke) StepSmoke(p, dt);
        else StepSoul(p, dt);

        const float t = p.age / p.life;
        float alpha = ball ? std::clamp((1.f - t) / 0.25f, 0.f, 1.f) : std::min(p.age / 0.1f, 1.f) * powf(1.f - t, 1.5f);
        if (p.style == kSoul && (p.soulStyle == 1 || p.soulStyle == 2)) alpha *= RandRange(0.55f, 1.f);
        if (alpha <= 0.01f || p.capture != -1) continue;   // waiting for its model colour = hidden

        ImVec2 s;
        if (!W2S_Project(p.pos, s)) continue;
        const float size = smoke ? p.size * (1.f + 0.7f * t) : p.size;
        const float r = PixelRadius(p.pos, s, size);

        if (smoke)
        {
            // soft puff: botox's fbm shader body thins out to the rim, 0.55 alpha
            dl->AddCircleFilled(s, r, Col(p.color, alpha * 0.55f * 0.45f), 20);
            dl->AddCircleFilled(s, r * 0.7f, Col(p.color, alpha * 0.55f * 0.55f), 16);
            continue;
        }
        if (!ball && stars)
        {
            if (glow) Glow(dl, s, r, p.color, alpha * 0.5f);
            Star(dl, s, r * 1.6f, p.seed, Col(p.color, alpha));
            continue;
        }
        if (glow && !ball) { Glow(dl, s, r, p.color, alpha); continue; }

        dl->AddCircleFilled(s, r, Col(p.color, alpha), 12);
        if (ball)
        {
            // up-left shine, 0.35 radius, +90 per channel
            const float hl[4] = { p.color[0] + 90.f / 255.f, p.color[1] + 90.f / 255.f, p.color[2] + 90.f / 255.f, p.color[3] };
            dl->AddCircleFilled(ImVec2(s.x - r * 0.35f, s.y - r * 0.35f), r * 0.35f, Col(hl, alpha * 0.8f), 8);
        }
    }

    g_kfx.erase(std::remove_if(g_kfx.begin(), g_kfx.end(), [](const KfxParticle& p) { return p.age >= p.life; }), g_kfx.end());
}
