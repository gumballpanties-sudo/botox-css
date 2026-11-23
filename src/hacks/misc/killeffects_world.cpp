#include "killeffects.h"
#include "../../render/render.h"       // W2S_Project
#include "../movement/movement.h"     // Vec3, Ray_mv, GameTrace_t, kTrace_TraceRay
#include "../movement/prediction.h"   // EntV3
#include "unitE_logic.h"  // n_unitE::McXpValue/McXpIcon
#include "mc_xp_orb.h"    // botox hacks/misc/mc_xp_orb.h, verbatim (experience_orb.png)
#include "particle_shapes.h"  // botox hacks/misc/particle_shapes.h, verbatim (soul shapes, clipart precipitation)
#include "../screen/screenfx.h"                      // ScreenFx_QueueFunctor -- clipart drawn at the post slot
#include "../../dependencies/imgui/imgui_impl_dx9.h"  // ImGui_ImplDX9_RenderDrawData for that list
#include <cstring>
#include <vector>
#include <random>
#include <mutex>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <climits>
#include <algorithm>
#include <utility>

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
    // botox k_explode_*, tools/soul_explode_check.py parses these
    constexpr float kExplodeSpeed[2]  = { 140.f, 300.f };
    constexpr float kExplodeSettle[2] = { 0.25f, 0.6f };
    constexpr float kExplodeDrag      = 6.f;
    constexpr float kExplodeFallDrag  = 1.2f;
    constexpr float kExplodeGravity   = 420.f;

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
    bool TraceLeaf(void* trace, const Ray_mv& ray, unsigned mask, void* filter, GameTrace_t* tr)
    {
        using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
        __try { ((TraceRay_t)(*(void***)trace)[kTrace_TraceRay])(trace, ray, mask, filter, tr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // game thread only (spawn side)
    bool KfxTrace(const Vec3& from, const Vec3& to, unsigned mask, void* skip, GameTrace_t* tr)
    {
        static void* trace = GetInterface("engine.dll", "EngineTraceClient003");
        if (!trace) return false;
        static void* vt[2] = { (void*)&KfxFilter::ShouldHit, (void*)&KfxFilter::TraceType };
        KfxFilter filter{ vt, skip };
        Ray_mv ray;
        ray.Init(from, to);
        return TraceLeaf(trace, ray, mask, &filter, tr);
    }

    bool Visible(const Vec3& at)
    {
        if (!g_eyeOk) return true;   // no trace = sample anyway, the pixel decides
        GameTrace_t tr{};
        if (!KfxTrace(g_eye, at, 0x1 | 0x4000, g_eyeSkip, &tr)) return true;
        return !tr.startsolid && tr.fraction >= 0.99f;
    }

    // botox spawn_soul explode floor: MASK_SOLID_BRUSHONLY 4096 down, start solid / no trace = feet
    float FloorBelow(const Vec3& at, float feet)
    {
        GameTrace_t tr{};
        if (!KfxTrace(at, at - Vec3(0.f, 0.f, 4096.f), 0x1 | 0x2 | 0x8 | 0x4000, nullptr, &tr) || tr.startsolid) return feet;
        return tr.endpos.z;
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
    const int   style  = std::clamp(g_dpSoulStyle, 0, 4);
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
            if (g_kfx.size() >= kKfxCap) return;
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
            case 4:
            {
                const float z = RandRange(-0.35f, 1.f), r = sqrtf(1.f - z * z);
                p.vel = Vec3(cosf(angle) * r, sinf(angle) * r, z) * RandRange(kExplodeSpeed[0], kExplodeSpeed[1]);
                // floor trace point: StepExplode xy drag must stay constant + exact, burst ends at v_xy / drag
                p.floorZ = FloorBelow(p.pos + Vec3(p.vel.x, p.vel.y, 0.f) * (1.f / kExplodeDrag), feet.z);
                break;
            }
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

    // botox step_explode
    void StepExplode(KfxParticle& p, float dt)
    {
        const float s    = std::clamp((p.age - kExplodeSettle[0]) / (kExplodeSettle[1] - kExplodeSettle[0]), 0.f, 1.f);
        const float ramp = s * s * (3.f - 2.f * s);

        const auto integrate = [dt](float& pos, float& vel, float drag, float gravity) {
            const float decay = expf(-drag * dt), terminal = gravity / drag;
            pos += (vel + terminal) * (1.f - decay) / drag - terminal * dt;
            vel = (vel + terminal) * decay - terminal;
        };
        integrate(p.pos.x, p.vel.x, kExplodeDrag, 0.f);
        integrate(p.pos.y, p.vel.y, kExplodeDrag, 0.f);
        integrate(p.pos.z, p.vel.z, kExplodeDrag + (kExplodeFallDrag - kExplodeDrag) * ramp, kExplodeGravity * ramp);

        const float rest = p.floorZ + p.size;
        if (p.pos.z <= rest)
        {
            p.pos.z = rest;
            p.vel.z = std::max(p.vel.z, 0.f);
        }
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

    // unit circles for the glow fan, built once (render thread only)
    struct GlowCircle { int segs; float c[24], s[24]; };
    const GlowCircle* GlowCircles()
    {
        static const struct Table {
            GlowCircle t[4];
            Table() {
                const int segs[4] = { 8, 12, 16, 24 };
                for (int k = 0; k < 4; ++k) {
                    t[k].segs = segs[k];
                    for (int i = 0; i < segs[k]; ++i) {
                        const float a = 6.2831853f * (float)i / (float)segs[k];
                        t[k].c[i] = cosf(a);
                        t[k].s[i] = sinf(a);
                    }
                }
            }
        } table;
        return table.t;
    }

    // botox kfx glow: rings 0.4 / 1 / 1.7 / 2.8 radii at alpha 1 / 0.5 / 0.16 / 0 = the shader's piecewise
    // linear falloff. One gouraud fan (center + 4 rings): one layer, 1 + 4 * segs verts instead of 4 stacked
    // aa discs (144 verts, 4x overdraw). The outer ring is alpha 0, so the polygon edge never shows.
    void Glow(ImDrawList* dl, ImVec2 s, float r, const float c[4], float alpha)
    {
        constexpr float kRad[4] = { 0.4f, 1.f, 1.7f, 2.8f };
        constexpr float kA[4]   = { 1.f, 0.5f, 0.16f, 0.f };
        const float outer = r * 2.8f;
        const GlowCircle& u = GlowCircles()[outer < 8.f ? 0 : outer < 30.f ? 1 : outer < 80.f ? 2 : 3];
        const int n = u.segs;

        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        dl->PrimReserve(n * 3 + n * 18, 1 + n * 4);
        const unsigned int base = dl->_VtxCurrentIdx;   // after PrimReserve: a vtx offset rollover resets it
        dl->PrimWriteVtx(s, uv, Col(c, alpha));
        for (int k = 0; k < 4; ++k)
        {
            const ImU32 col = Col(c, alpha * kA[k]);
            const float rr = r * kRad[k];
            for (int i = 0; i < n; ++i) dl->PrimWriteVtx(ImVec2(s.x + u.c[i] * rr, s.y + u.s[i] * rr), uv, col);
        }
        // ring k vertex i = base + 1 + k * n + i
        for (int i = 0; i < n; ++i)
        {
            const unsigned int j = (unsigned int)((i + 1) % n);
            dl->PrimWriteIdx((ImDrawIdx)base);
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + 1 + j));
        }
        for (int k = 0; k < 3; ++k)
            for (int i = 0; i < n; ++i)
            {
                const unsigned int a0 = base + 1 + k * n + i, a1 = base + 1 + k * n + (unsigned int)((i + 1) % n);
                const unsigned int b0 = a0 + n, b1 = a1 + n;
                const unsigned int idx[6] = { a0, b0, b1, a0, b1, a1 };
                for (const unsigned int v : idx) dl->PrimWriteIdx((ImDrawIdx)v);
            }
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
        else if (p.style == kSoul && p.soulStyle == 4) StepExplode(p, dt);
        else StepSoul(p, dt);

        const float t = p.age / p.life;
        float alpha = ball ? std::clamp((1.f - t) / 0.25f, 0.f, 1.f) : std::min(p.age / 0.1f, 1.f) * powf(1.f - t, 1.5f);
        if (p.style == kSoul && (p.soulStyle == 1 || p.soulStyle == 2)) alpha *= RandRange(0.55f, 1.f);
        else if (p.style == kSoul && p.soulStyle == 4) alpha = std::clamp((1.f - t) / 0.3f, 0.f, 1.f);
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

// ==== precipitation clipart (botox hacks/misc/precip_clipart.cpp, 11-22) ========================
// botox draws soul shape billboards in its z tested kfx pass. Here: imgui at W2S like the particles
// above, so a flake behind a wall is hidden by an eye trace instead (round robin on the main
// thread, faded over kClipSeeFade). Sim = botox 1:1: sky column spawn, path + landing traced once,
// area sheared along the wind, sway settles before landing.
namespace
{
    constexpr float kClipTop        = 512.f;    // botox k_top
    constexpr float kClipSky        = 16384.f;  // k_sky
    constexpr float kClipDepth      = 2048.f;   // k_depth
    constexpr float kClipFadeIn     = 0.4f;     // k_fade_in
    constexpr float kClipSettle     = 0.5f;     // k_settle
    constexpr float kClipSeeFade    = 0.15f;    // ours: eye trace flip fade
    constexpr size_t kClipLosPerTick = 96;      // ours: eye traces per main thread frame
    constexpr float kClipTau        = 6.2831853f;

    struct ClipFlake
    {
        Vec3  from{}, vel{};
        float fall = -1.f;   // s to landing, < 0 = falls past kClipDepth
        float life = 0.f, age = 0.f, shown = 0.f;
        float size = 1.f, spin = 0.f, spinRate = 1.f, phase = 0.f, hue = 0.f;
        float vis = 1.f;     // render thread fade toward see
        bool  see = true;    // main thread eye trace
        // 3d rotation: tumble axis + start plane (unit, u0 . v0 = 0), landing surface normal
        Vec3  axis{ 0.f, 0.f, 1.f }, u0{ 1.f, 0.f, 0.f }, v0{ 0.f, 1.f, 0.f }, normal{ 0.f, 0.f, 1.f };
    };

    // main thread spawns / culls / traces, render thread ages + draws and never resizes, so an
    // index taken under the lock still names the same flake after the traces run unlocked
    std::mutex g_clipMtx;
    std::vector<ClipFlake> g_clip;
    Vec3   g_clipEye{};
    size_t g_clipCursor = 0;
    // main thread only
    std::mt19937 g_clipRng(0x5eedu);
    int  g_clipWarm  = 0;      // spawn attempts that start mid life (first fill, refill after leaving the area)
    bool g_clipFresh = true;
    int  g_clipFails = 0;      // failed spawns in a row: indoors every attempt dies on the roof, back off to a trickle

    float ClipRand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_clipRng); }

    bool ClipOn() { return g_weatherEnable && g_weatherMode == k_weatherClipart && !g_eject; }

    // TRACE_WORLD_ONLY for the sky column; TRACE_EVERYTHING refusing every entity for landing and
    // sight -- static props still block, the engine never hands them to a non FILTER_PROPS filter
    struct ClipFilter
    {
        void* vfptr;
        static bool __fastcall ShouldHit(ClipFilter*, void*, int) { return false; }
        static int __fastcall World(ClipFilter*) { return 1; }
        static int __fastcall All(ClipFilter*) { return 0; }
    };

    bool ClipTrace(const Vec3& from, const Vec3& to, unsigned mask, bool worldOnly, GameTrace_t* tr)
    {
        static void* trace = GetInterface("engine.dll", "EngineTraceClient003");
        if (!trace) return false;
        static void* vtWorld[2] = { (void*)&ClipFilter::ShouldHit, (void*)&ClipFilter::World };
        static void* vtAll[2]   = { (void*)&ClipFilter::ShouldHit, (void*)&ClipFilter::All };
        ClipFilter filter{ worldOnly ? vtWorld : vtAll };
        Ray_mv ray;
        ray.Init(from, to);
        return TraceLeaf(trace, ray, mask, &filter, tr);
    }

    struct ClipCfg { Vec3 wind; float speed, radius, size, sway, lift, groundTime; int target; bool planar; };

    ClipCfg ClipReadCfg()
    {
        ClipCfg c;
        const float yaw = g_clipWindYaw * (kClipTau / 360.f);
        c.wind       = Vec3(cosf(yaw) * g_clipWind, sinf(yaw) * g_clipWind, 0.f);
        c.speed      = std::clamp(g_clipSpeed, 1.f, 2000.f);
        c.radius     = std::clamp(g_clipRadius, 100.f, 4000.f);
        c.size       = std::clamp(g_clipSize, 0.1f, 32.f);
        c.sway       = g_clipSway;
        c.lift       = std::clamp(g_clipShape, 0, n_particle_shape::k_last) ? n_particle_shape::k_scale : 1.f;
        c.groundTime = std::max(g_clipGroundTime, 0.f);
        c.target     = std::clamp(g_clipDensity, 10, 3000);
        c.planar     = g_clip3d;
        return c;
    }

    // botox area_centre: upwind above the eye, downwind below
    Vec3 ClipCentre(const Vec3& eye, const ClipCfg& c, float z)
    {
        const float lead = (z - eye.z) / c.speed;
        return Vec3(eye.x - c.wind.x * lead, eye.y - c.wind.y * lead, z);
    }

    float ClipDist2(const Vec3& eye, const ClipCfg& c, const Vec3& at)
    {
        const Vec3 centre = ClipCentre(eye, c, at.z);
        return (at.x - centre.x) * (at.x - centre.x) + (at.y - centre.y) * (at.y - centre.y);
    }

    // botox paint position: path at t, sway fading out before the landing ( settle 1 falling .. 0 landed ), glyph bottom
    // on the floor; 3d rotation lies flat 0.3 u off the surface and tumbles clear of it
    Vec3 ClipAt(const ClipFlake& f, const ClipCfg& c, bool& landed, float* settleOut = nullptr)
    {
        landed = f.fall >= 0.f && f.age >= f.fall;
        const float t      = landed ? f.fall : f.age;
        const float settle = f.fall >= 0.f ? std::clamp((f.fall - t) / kClipSettle, 0.f, 1.f) : 1.f;
        const float wobble = c.sway * settle;
        const float s      = f.size * c.size;
        if (settleOut) *settleOut = settle;
        const Vec3 path(f.from.x + f.vel.x * t + sinf(t * 1.7f + f.phase) * wobble,
                        f.from.y + f.vel.y * t + cosf(t * 1.1f + f.phase * 1.7f) * wobble,
                        f.from.z + f.vel.z * t);
        return c.planar ? path + f.normal * (0.3f + s * c.lift * settle) : path + Vec3(0.f, 0.f, s * c.lift);
    }

    Vec3 ClipRandUnit()
    {
        const float z = ClipRand(-1.f, 1.f), a = ClipRand(0.f, kClipTau), r = sqrtf(1.f - z * z);
        return Vec3(r * cosf(a), r * sinf(a), z);
    }

    // CONTENTS_SOLID | CONTENTS_MOVEABLE, same as Visible() above
    bool ClipSees(const Vec3& eye, const Vec3& at)
    {
        GameTrace_t tr{};
        if (!ClipTrace(eye, at, 0x1 | 0x4000, false, &tr)) return true;
        return !tr.startsolid && tr.fraction >= 0.99f;
    }

    // botox spawn
    bool ClipSpawn(ClipFlake& f, const Vec3& eye, const ClipCfg& c, bool warm)
    {
        const float angle = ClipRand(0.f, kClipTau), dist = c.radius * sqrtf(ClipRand(0.f, 1.f));
        const Vec3 top = ClipCentre(eye, c, eye.z + kClipTop);
        const Vec3 base(top.x + cosf(angle) * dist, top.y + sinf(angle) * dist, eye.z);

        // MASK_SOLID_BRUSHONLY up, must end on SURF_SKY (0x4)
        GameTrace_t up{};
        if (!ClipTrace(base, base + Vec3(0.f, 0.f, kClipSky), 0x1 | 0x2 | 0x8 | 0x4000, true, &up) ||
            up.startsolid || up.fraction >= 1.f || !(up.surface.flags & 0x4))
            return false;

        f.from = Vec3(base.x, base.y, std::min(up.endpos.z - 4.f, eye.z + kClipTop));
        f.vel  = Vec3(c.wind.x, c.wind.y, -c.speed * ClipRand(0.8f, 1.2f));

        // MASK_SOLID | CONTENTS_WATER
        const float span = (f.from.z - (eye.z - kClipDepth)) / -f.vel.z;
        GameTrace_t down{};
        const bool hit = ClipTrace(f.from, f.from + f.vel * span, 0x1 | 0x2 | 0x8 | 0x4000 | 0x2000000 | 0x20, false, &down) &&
                         down.fraction < 1.f && !down.startsolid;
        f.fall     = hit ? span * down.fraction : -1.f;
        f.life     = hit ? f.fall + c.groundTime : span;
        f.size     = ClipRand(0.75f, 1.25f);
        f.spin     = ClipRand(0.f, 1.f);
        f.spinRate = ClipRand(0.5f, 1.5f) * (ClipRand(0.f, 1.f) < 0.5f ? -1.f : 1.f);
        f.phase    = ClipRand(0.f, kClipTau);
        f.hue      = ClipRand(0.f, 1.f);
        f.normal   = hit ? down.plane.normal : Vec3(0.f, 0.f, 1.f);
        f.axis     = ClipRandUnit();
        f.u0       = ClipRandUnit();
        for (int tries = 0; tries < 8; ++tries)
        {
            const Vec3 r = ClipRandUnit();
            const float d = f.u0.x * r.x + f.u0.y * r.y + f.u0.z * r.z;
            f.v0 = r - f.u0 * d;
            if (f.v0.x * f.v0.x + f.v0.y * f.v0.y + f.v0.z * f.v0.z > 0.01f) break;
        }
        n_particle_shape::unit3(&f.v0.x);
        f.age      = warm ? ClipRand(0.f, f.life) : 0.f;
        f.shown    = 0.f;
        bool landed;
        f.see = ClipSees(eye, ClipAt(f, c, landed));
        f.vis = f.see ? 1.f : 0.f;
        return true;
    }

    // W2S_Project's matrix in one pass per flake, no pixel snap (smooth sub pixel fall). Pixels per world unit
    // = |row0.xyz| * W / 2 / w: a symmetric frustum's row 0 is the view's right axis times the x scale, so
    // this is the billboard radius botox draws, for 1 projection instead of PixelRadius' 4
    struct ClipView { const float (*m)[4]; float hw, hh, px; };

    bool ClipViewInit(ClipView& v, const float (*m)[4])
    {
        v.m = m;
        if (!v.m) return false;
        const ImGuiIO& io = ImGui::GetIO();
        v.hw = io.DisplaySize.x * 0.5f;
        v.hh = io.DisplaySize.y * 0.5f;
        v.px = sqrtf(v.m[0][0] * v.m[0][0] + v.m[0][1] * v.m[0][1] + v.m[0][2] * v.m[0][2]) * v.hw;
        return true;
    }

    bool ClipProject(const ClipView& v, const Vec3& p, ImVec2& s, float& pxPerUnit)
    {
        const float (*m)[4] = v.m;
        const float w = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];   // view depth, world units
        if (w < 7.f) return false;   // engine near plane (view.h 7): botox's z pass clips these, here they'd fill the screen
        const float inv = 1.f / w;
        s.x = v.hw + (m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3]) * inv * v.hw;
        s.y = v.hh - (m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3]) * inv * v.hh;
        pxPerUnit = v.px * inv;
        return true;
    }

    // c_color::from_hsb at brightness 1, alpha untouched
    void ClipHsb(float hue, float sat, float out[4])
    {
        const float h = (hue - floorf(hue)) * 6.f;
        const int i = std::min((int)h, 5);
        const float f = h - (float)i, p = 1.f - sat, q = 1.f - sat * f, t = 1.f - sat * (1.f - f);
        const float rgb[6][3] = { { 1, t, p }, { q, 1, p }, { p, 1, t }, { p, q, 1 }, { t, p, 1 }, { 1, p, q } };
        out[0] = rgb[i][0]; out[1] = rgb[i][1]; out[2] = rgb[i][2];
    }
}

// ---- post slot draw ---------------------------------------------------------------------------
static void ClipBuild(ImDrawList* dl, const float (*m)[4]);
static void ClipDrawPost(const float (*m)[4]);

// ring of 8 like render.cpp's OvFunctor: the main thread runs at most a frame or two ahead of the
// replay, so a slot's matrix is never rewritten before its call ran. Faked CFunctor, never freed.
struct ClipFunctor { void** vfptr; float m[4][4]; };
static int   __fastcall ClipFn_AddRef (void*)             { return 1; }
static int   __fastcall ClipFn_Release(void*)             { return 1; }
static void* __fastcall ClipFn_Dtor   (void* p, unsigned) { return p; }
static void  __fastcall ClipFn_Call   (void* self)
{
    ++g_hookDepth;   // counter first: eject drains on it before tearing ImGui down
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;
    ClipDrawPost(static_cast<const ClipFunctor*>(self)->m);
}
static void*       g_clipFnVt[4] = { (void*)&ClipFn_AddRef, (void*)&ClipFn_Release, (void*)&ClipFn_Dtor, (void*)&ClipFn_Call };
static ClipFunctor g_clipRing[8];
static unsigned    g_clipNext = 0;   // main thread

// IVEngineClient::WorldToScreenMatrix (slot 36) for this view. SEH leaf, no unwindable locals (C2712)
static bool ClipEngineMatrix(float (&out)[4][4])
{
    __try
    {
        const float* m = VCall<const float*>((void*)g_engine, 36);
        if (!m) return false;
        memcpy(out, m, sizeof(out));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return std::isfinite(out[3][0] + out[3][1] + out[3][2] + out[3][3]) && (out[3][0] != 0.f || out[3][1] != 0.f || out[3][2] != 0.f);
}

void PrecipClipart_ViewTick(const float origin[3], const float angles[3])
{
    const Vec3 eye(origin[0], origin[1], origin[2]);
    const float pitch = angles[0] * (kClipTau / 360.f), yaw = angles[1] * (kClipTau / 360.f);
    const Vec3 fwd(cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch));
    // off, out of game, or a jump no flake survives (map change, spectator switch): start over.
    // g_clipEye is written by this thread only, the unlocked read is safe
    const Vec3 jump  = eye - g_clipEye;
    const bool reset = !ClipOn() || !IsInGame() || jump.x * jump.x + jump.y * jump.y > 8192.f * 8192.f;
    if (reset)
    {
        std::lock_guard<std::mutex> lock(g_clipMtx);
        g_clip.clear();
        g_clipCursor = 0;
        g_clipEye    = eye;
        g_clipWarm   = 0;
        g_clipFresh  = true;
        g_clipFails  = 0;
        if (!ClipOn() || !IsInGame()) return;
    }

    const ClipCfg c = ClipReadCfg();
    static std::vector<std::pair<size_t, Vec3>> s_look;   // main thread only
    static std::vector<char> s_seen;
    static std::vector<ClipFlake> s_new;
    s_look.clear();
    s_seen.clear();
    s_new.clear();

    int attempts = 0;
    {
        std::lock_guard<std::mutex> lock(g_clipMtx);
        g_clipEye = eye;
        if (g_clipFresh) { g_clipWarm = c.target * 3; g_clipFresh = false; }

        size_t live = 0;
        for (size_t i = 0; i < g_clip.size(); ++i)
        {
            const ClipFlake& f = g_clip[i];
            if (f.age >= f.life) continue;
            bool landed;
            if (ClipDist2(eye, c, ClipAt(f, c, landed)) > c.radius * c.radius) { g_clipWarm += 3; continue; }
            g_clip[live++] = f;
        }
        g_clip.resize(live);
        g_clipWarm = std::min(g_clipWarm, c.target * 3);

        // the sight budget goes to flakes in front of the view only: one behind keeps its old answer
        // until it comes round, so a turn re-checks the new view first
        for (size_t scanned = 0; scanned < g_clip.size() && s_look.size() < kClipLosPerTick; ++scanned)
        {
            if (g_clipCursor >= g_clip.size()) g_clipCursor = 0;
            bool landed;
            const Vec3 at  = ClipAt(g_clip[g_clipCursor], c, landed);
            const Vec3 rel = at - eye;
            if (rel.x * fwd.x + rel.y * fwd.y + rel.z * fwd.z > 0.f) s_look.push_back({ g_clipCursor, at });
            ++g_clipCursor;
        }
        // ponytail: fixed attempt cap like botox; 256 misses in a row (indoors, every column hits a roof) = 4 a frame
        attempts = std::min(c.target - (int)g_clip.size(), g_clipWarm > 0 ? 64 : 32);
        if (g_clipFails >= 256) attempts = std::min(attempts, 4);
    }

    // traces unlocked
    for (const auto& l : s_look) s_seen.push_back(ClipSees(eye, l.second) ? 1 : 0);
    for (int i = 0; i < attempts; ++i)
    {
        const bool warm = g_clipWarm > 0;
        if (warm) --g_clipWarm;
        ClipFlake f;
        if (ClipSpawn(f, eye, c, warm)) { s_new.push_back(f); g_clipFails = 0; }
        else g_clipFails = std::min(g_clipFails + 1, 1 << 20);
    }

    {
        std::lock_guard<std::mutex> lock(g_clipMtx);
        for (size_t k = 0; k < s_look.size(); ++k)
            if (s_look[k].first < g_clip.size()) g_clip[s_look[k].first].see = s_seen[k] != 0;
        g_clip.insert(g_clip.end(), s_new.begin(), s_new.end());
    }

    // this frame's draw, at this point of the frame (post slot = before DrawViewModels, so the gun and the hud go
    // over the flakes, like botox's z tested kfx pass). mat_queue_mode 0: this IS the render thread, draw now.
    // Multicore with the queue refused: nothing draws (raw d3d here would land mid playback of the last frame).
    ClipFunctor& fn = g_clipRing[g_clipNext++ & 7];
    fn.vfptr = g_clipFnVt;
    const bool matOk = ClipEngineMatrix(fn.m);
    const bool queued = matOk && ScreenFx_QueueFunctor(&fn);
    static DWORD s_logAt = 0;
    if (GetTickCount() - s_logAt > 5000)
    {
        s_logAt = GetTickCount();
        DebugLog("CLIPART: tick flakes=%zu fails=%d warm=%d matrix=%d queued=%d w-row=%.3f,%.3f,%.3f,%.1f",
                 g_clip.size(), g_clipFails, g_clipWarm, (int)matOk, (int)queued, fn.m[3][0], fn.m[3][1], fn.m[3][2], fn.m[3][3]);
    }
    if (!matOk || queued) return;
    if (GetCurrentThreadId() == Render_PresentTid()) { ClipDrawPost(fn.m); return; }
    static bool s_logged = false;
    if (!s_logged) { s_logged = true; DebugLog("CLIPART: call queue refused off the render thread -- flakes need mat_queue_mode 0"); }
}

void PrecipClipart_Reset()
{
    std::lock_guard<std::mutex> lock(g_clipMtx);
    if (g_clip.empty() && g_clipFresh) return;
    g_clip.clear();
    g_clipCursor = 0;
    g_clipWarm   = 0;
    g_clipFresh  = true;
    g_clipFails  = 0;
}

// render thread: step + build the flakes into dl, projected with this frame's matrix m
static void ClipBuild(ImDrawList* dl, const float (*m)[4])
{
    struct Drawn { Vec3 at; float alpha, size, spin, hue; Vec3 u, v; };   // u / v: 3d rotation plane
    static std::vector<Drawn> s_drawn;   // render thread only
    s_drawn.clear();

    const ClipCfg c = ClipReadCfg();
    Vec3 eye{};
    {
        // step + snapshot under the lock; W2S + geometry after it, so the main thread's tick never waits on imgui
        std::lock_guard<std::mutex> lock(g_clipMtx);
        if (!ClipOn() || !IsInGame() || g_clip.empty()) return;
        const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.05f);
        eye = g_clipEye;
        for (ClipFlake& f : g_clip)
        {
            f.age += dt;
            f.shown += dt;
            if (f.age >= f.life) continue;
            f.vis = std::clamp(f.vis + (f.see ? dt : -dt) / kClipSeeFade, 0.f, 1.f);

            bool landed;
            float settle;
            const Vec3 at    = ClipAt(f, c, landed, &settle);
            const float dist = sqrtf(ClipDist2(eye, c, at));
            float alpha = std::min(f.shown / kClipFadeIn, 1.f) * std::clamp((c.radius - dist) / (c.radius * 0.15f), 0.f, 1.f) * f.vis;
            if (landed) alpha *= 1.f - (f.age - f.fall) / (f.life - f.fall);
            if (alpha <= 0.01f) continue;

            const float t = landed ? f.fall : f.age;
            float spin = f.spin + f.spinRate * g_clipSpin * t;
            Vec3 u(1.f, 0.f, 0.f), v(0.f, 1.f, 0.f);
            if (c.planar)   // spin = tumble speed, botox tumble_frame (particle_shapes.h, same in all 3 games)
                n_particle_shape::tumble_frame(&f.axis.x, &f.u0.x, &f.v0.x, spin * kClipTau,
                                               (f.spin + f.spinRate * g_clipSpin * f.fall) * kClipTau, &f.normal.x, settle, &u.x, &v.x);
            spin -= floorf(spin);
            s_drawn.push_back({ at, alpha, f.size * c.size, spin, f.hue, u, v });
        }
    }

    const int shape      = std::clamp(g_clipShape, 0, n_particle_shape::k_last);
    const int colorMode  = g_clipColorMode, glowMode = g_clipGlow;
    const float sat      = std::clamp(g_clipSaturation, 0.f, 1.f);
    const float hueShift = colorMode == 2 ? g_clipRainbowSpeed * (float)ImGui::GetTime() : 0.f;
    const ImVec2 screen  = ImGui::GetIO().DisplaySize;
    ClipView view;
    if (!ClipViewInit(view, m)) return;

    for (const Drawn& d : s_drawn)
    {
        ImVec2 s;
        float ppu;
        if (!ClipProject(view, d.at, s, ppu)) continue;
        const float r = std::max(d.size * ppu, 0.5f);
        const float reach = r * 2.8f;   // glow rings, the widest part
        if (s.x < -reach || s.y < -reach || s.x > screen.x + reach || s.y > screen.y + reach) continue;

        // multicolor = own hue per glyph, rainbow = shared hue cycling, glyphs spread 0.15 around it
        float col[4] = { g_clipColor[0], g_clipColor[1], g_clipColor[2], g_clipColor[3] };
        if (colorMode) ClipHsb(d.hue * (colorMode == 2 ? 0.15f : 1.f) + hueShift, sat, col);
        const float* glow = glowMode == 1 ? col : glowMode == 2 ? g_clipGlowColor : nullptr;

        // botox kill_effects_shape: glow at half alpha under the glyph; the circle keeps its disc on top
        if (glow) Glow(dl, s, r, glow, d.alpha * 0.5f);
        // a glyph under ~2.5 px reads as a dot whatever its shape: 4 verts instead of 2n + 1 with fringe
        const float tip = shape ? r * n_particle_shape::k_scale : r;
        if (tip < 1.25f)
        {
            const float h = std::max(tip, 0.75f);
            dl->AddRectFilled(ImVec2(s.x - h, s.y - h), ImVec2(s.x + h, s.y + h), Col(col, d.alpha));
        }
        else if (c.planar)
        {
            // glyph on its world plane: outline points projected one by one (circle = 16 point ring), the halo above
            // stays facing the view like botox's
            float unit[n_particle_shape::k_max_points][2];
            int n = 0;
            if (shape) n = n_particle_shape::outline(shape, unit);
            else
            {
                const GlowCircle& ring = GlowCircles()[2];
                for (; n < ring.segs; ++n) { unit[n][0] = ring.c[n]; unit[n][1] = ring.s[n]; }
            }
            const float world = shape ? d.size * n_particle_shape::k_scale : d.size;
            ImVec2 pts[n_particle_shape::k_max_points];
            float ignore;
            bool ok = n >= 3;
            for (int i = 0; i < n && ok; ++i)
                ok = ClipProject(view, d.at + (d.u * unit[i][0] + d.v * unit[i][1]) * world, pts[i], ignore);
            if (ok) n_particle_shape::fill_screen(dl, s, pts, n, Col(col, d.alpha));
        }
        else if (shape) n_particle_shape::fill(dl, s, tip, d.spin, Col(col, d.alpha), shape);
        else dl->AddCircleFilled(s, r, Col(col, d.alpha));   // auto segments by radius
    }
}

// render thread, at the post slot: own draw list (imgui is between frames here, its shared data and font
// atlas stand), drawn through the dx9 backend like render.cpp's RenderSubset -- it saves and restores a
// full state block, so the engine's state survives. Background list space = real pixels, no dpi scale.
static void ClipDrawPost(const float (*m)[4])
{
    static unsigned s_calls = 0, s_drawnFrames = 0;   // render thread only, CLIPART: diag
    static DWORD s_logAt = 0;
    ++s_calls;
    if (g_eject || !g_imguiInit || !ClipOn()) return;
    static ImDrawList* s_list = nullptr;   // render thread only
    if (!s_list) s_list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    s_list->_ResetForNewFrame();
    s_list->PushTextureID(ImGui::GetIO().Fonts->TexID);
    s_list->PushClipRectFullScreen();
    ClipBuild(s_list, m);

    IDirect3DDevice9* dev = g_d3dDev;
    if (dev && GetTickCount() - s_logAt > 5000)
    {
        s_logAt = GetTickCount();
        DWORD cw = 0, srgb = 0, scissor = 0;
        dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
        dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgb);
        dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
        D3DVIEWPORT9 vp{};
        dev->GetViewport(&vp);
        D3DSURFACE_DESC rt{};
        IDirect3DSurface9* surf = nullptr;
        if (SUCCEEDED(dev->GetRenderTarget(0, &surf)) && surf) { surf->GetDesc(&rt); surf->Release(); }
        DebugLog("CLIPART: post calls=%u drew=%u vtx=%d display=%.0fx%.0f rt=%ux%u fmt=%d ms=%d vp=%lu,%lu %lux%lu colorwrite=0x%lx srgb=%lu scissor=%lu",
                 s_calls, s_drawnFrames, s_list->VtxBuffer.Size, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y,
                 rt.Width, rt.Height, (int)rt.Format, (int)rt.MultiSampleType, vp.X, vp.Y, vp.Width, vp.Height, cw, srgb, scissor);
        s_calls = s_drawnFrames = 0;
    }
    if (s_list->VtxBuffer.Size == 0 || !dev) return;
    ++s_drawnFrames;

    ImDrawList* lists[1] = { s_list };
    ImDrawData dd;
    dd.Valid            = true;
    dd.CmdLists         = lists;
    dd.CmdListsCount    = 1;
    dd.TotalVtxCount    = s_list->VtxBuffer.Size;
    dd.TotalIdxCount    = s_list->IdxBuffer.Size;
    dd.DisplaySize      = ImGui::GetIO().DisplaySize;
    dd.FramebufferScale = ImVec2(1.f, 1.f);

    // the backend sets neither: mid frame the engine can leave colour writes masked or srgb writes on (botox kfx
    // world_state_t sets both too). Its full state block is captured after these, so restore them by hand after
    DWORD oldCw = 0, oldSrgb = 0;
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldCw);
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    ImGui_ImplDX9_RenderDrawData(&dd);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldCw);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
}
