#pragma once
// dist_calc.h -- nearest-edge finder, ported from botox csgo-sdk hacks/movement/dist_calc.cpp
// (dc_probe/dc_edge_dist/movement_nearest_edge). Engine-free and templated on the trace, the same
// shape fm_slide.h / buildplace.h use, so tests/dist_calc_test.cpp runs it against a box world.
//
// Was MISSING from the TF2 port entirely (no trace of dist_calc.cpp or its feature anywhere in
// botox-tf2/botox-css). CS has no reason it wouldn't apply -- it is pure floor-edge geometry, no
// TF2 or CS:GO specific mechanic involved -- so this is a straight 1:1 port of the algorithm.
//
// 11-03: added the lj/jb verdict -- MaxBlock() (ported from botox's n_route::max_block,
// route_engine.cpp ~line 2858) and Verdict()/VerdictLine(), wired to routecalc.inl's RcMaxBlock
// (the thin live-cvar wrapper routecalc.inl owns). MaxBlock() itself stays pure/engine-free (every
// number it needs is a parameter, same shape as routesim.h's rs::Tick taking rs::Params by value)
// so tests/dist_calc_test.cpp can run it with no engine, same as the edge finder above.
//
// The two-click HUD tool's key/aim handling and the opposite-normal "square" clamp on point B
// (dist_calc.cpp's snap lambda) are wired in movement.cpp (DistCalc()/DistCalc_Render()) -- they
// need a live aim trace and session state (which click this is), so they don't belong in this
// engine-free header.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "routesim.h"

namespace dc
{
struct Vec3Like { float x = 0.f, y = 0.f, z = 0.f; };

enum Probe { kFloor = 0, kDrop, kWall };

// one hit's classification: floor within 2u of at.z, a drop (steep or a ledge), or inside a wall
// (a wall is never an edge). TraceFn(a, b) -> a result with .frac, .end{x,y,z}, .normalZ, .startSolid
template<class V, class TraceFn>
inline Probe DcProbe(TraceFn&& trace, const V& at, float& zOut)
{
    V a{ at.x, at.y, at.z + 4.f };
    V b{ at.x, at.y, at.z - 6.f };
    const auto tr = trace(a, b);
    if (tr.startSolid) return kWall;
    if (tr.frac >= 1.f || tr.normalZ < 0.7f || std::fabs(tr.end.z - at.z) > 2.f) return kDrop;
    zOut = tr.end.z;
    return kFloor;
}

// distance from p, along dir, to the first drop/ledge -- bisected to ~1/128 of `limit`. -1 = none
// found inside limit (a wall, or solid floor all the way out)
template<class V, class TraceFn>
inline float DcEdgeDist(TraceFn&& trace, const V& p, const V& dir, float limit)
{
    float z = p.z, lo = 0.f, hi = -1.f;
    for (float s = 2.f; ; s += 2.f)
    {
        s = (std::min)(s, limit);
        V at{ p.x + dir.x * s, p.y + dir.y * s, p.z + dir.z * s };
        const Probe kind = DcProbe(trace, at, z);
        if (kind == kWall) return -1.f;
        if (kind == kDrop) { hi = s; break; }
        lo = s;
        if (s >= limit) return -1.f;
    }
    for (int i = 0; i < 7; i++)
    {
        const float mid = 0.5f * (lo + hi);
        V at{ p.x + dir.x * mid, p.y + dir.y * mid, p.z + dir.z * mid };
        (DcProbe(trace, at, z) == kFloor ? lo : hi) = mid;
    }
    return lo;
}

// the nearest floor edge within `reach` of p, optionally restricted to the half-plane `facing`
// points into. normalOut (if given) comes back as the direction walked to reach the edge.
template<class V, class TraceFn>
inline V NearestEdge(TraceFn&& trace, const V& p, float reach, const V& facing = {}, V* normalOut = nullptr)
{
    float z = p.z;
    if (DcProbe(trace, p, z) != kFloor) return p;

    const float facingLen2d = std::sqrt(facing.x * facing.x + facing.y * facing.y);
    const bool face = facingLen2d > 0.5f;
    const auto dirOf = [](float yaw) { return V{ std::cos(yaw), std::sin(yaw), 0.f }; };
    constexpr int kDirs = 48;
    constexpr float kStep = 6.2831853f / kDirs;

    float best = -1.f, bestYaw = 0.f, limit = reach;
    for (int i = 0; i < kDirs; i++)
    {
        const float yaw = i * kStep;
        const V d = dirOf(yaw);
        if (face && d.x * facing.x + d.y * facing.y <= 0.f) continue;
        const float dist = DcEdgeDist(trace, p, d, limit);
        if (dist >= 0.f && (best < 0.f || dist < best)) { best = dist; bestYaw = yaw; limit = dist + 2.f; }
    }
    if (best < 0.f) return p;

    const auto distAt = [&](float yaw) {
        const float dist = DcEdgeDist(trace, p, dirOf(yaw), limit);
        return dist < 0.f ? 1e30f : dist;
    };
    float lo = bestYaw - kStep, hi = bestYaw + kStep;
    for (int i = 0; i < 10; i++)
    {
        const float m1 = lo + (hi - lo) / 3.f, m2 = hi - (hi - lo) / 3.f;
        if (distAt(m1) < distAt(m2)) hi = m2; else lo = m1;
    }
    const float yaw = 0.5f * (lo + hi), dist = distAt(yaw);
    if (dist <= best) { best = dist; bestYaw = yaw; }

    const V dir = dirOf(bestYaw);
    V edge{ p.x + dir.x * best, p.y + dir.y * best, p.z + dir.z * best };
    if (DcProbe(trace, edge, z) == kFloor) edge.z = z;
    if (normalOut) *normalOut = dir;
    return edge;
}

// ---- lj/jb verdict: "can a jump cross this gap" --------------------------------------------
// Ported 1:1 from botox csgo-sdk hacks/movement/route_engine.cpp n_route::max_block (~line 2858).
// The vertical half reuses rs::Tick/State/World (routesim.h -- already CSS-verified, no stamina:
// CS:GO's own max_block carried a second, CS:GO-only sim_tick with a stamina/duck-speed model
// CSS's CheckJumpButton/Duck (cs_gamemovement.cpp) never had, so that duplicate isn't re-grown
// here). The horizontal half (perfect air-strafe speed gain) is CS:GO's own trig -- unaffected by
// stamina -- ported as-is; sv_air_max_wishspeed (airCap) doesn't exist in CSS as a cvar
// (CGameMovement::GetAirSpeedCap() returns a hardcoded 30.f, gamemovement.h:104), so the caller's
// CvarFloat fallback IS the real number, same trick pixelsurf.inl already uses for the same cvar.
//
// jumpbug: true = launch from the jumpbug catch height (2u - kDistEps, Categorize's own 2u),
// false = launch off the true floor (a longjump). dz: height of the landing edge over the takeoff
// edge (positive = landing is higher). maxSpeed: m_flMaxspeed (not negative/zero -> -1). Returns
// the longest block (edge-to-edge, +32 for the two hulls' half-widths meeting) that clears dz at
// perfect strafes, or -1 if dz is never reached falling (the gap is too high for any jump style).
inline float MaxBlock(bool jumpbug, float dz, float maxSpeed, float dt, float gravity,
                      float jumpMod, float airAccel, float airCap)
{
    if (maxSpeed <= 0.f || dt <= 0.f) return -1.f;
    const float a   = airAccel * maxSpeed * dt;
    const float dot = a >= airCap ? 0.f : airCap - a;

    rs::Params p; p.dt = dt; p.gravity = gravity; p.jumpMod = jumpMod;
    rs::World w;   // floor=false, ceil=false: pure air flight (csgo-sdk's own solid=false pass)

    const int ticks = (std::max)(1, (int)(4.f / dt));   // 4s cap, csgo-sdk's own air_tick_cap()

    float best = -1.f;
    for (int variant = 0; variant < 3; variant++)
    {
        rs::State s{};
        s.z = jumpbug ? (2.f - rs::kDistEps) : 0.f;
        s.ground = true;
        float vx = 1.1f * maxSpeed, vy = 0.f, x = 0.f;
        for (int t = 0; t < ticks; t++)
        {
            const float z0 = s.z, x0 = x;
            rs::Cmd c; c.jump = (t == 0); c.duck = (variant == 2) || (variant == 1 && t >= 1);
            rs::Tick(s, c, w, p);

            const float v = std::sqrt(vx * vx + vy * vy);
            const float cc = std::clamp(dot / v, -1.f, 1.f), sn = std::sqrt(1.f - cc * cc);
            const float side = vy > 0.f ? -1.f : 1.f;
            const float wx = cc * vx / v - side * sn * vy / v, wy = cc * vy / v + side * sn * vx / v;
            const float add = (std::min)(a, airCap - (vx * wx + vy * wy));
            if (add > 0.f) { vx += add * wx; vy += add * wy; }
            x += vx * dt;

            if (z0 >= dz && s.z < dz && s.vz < 0.f)
            {
                best = (std::max)(best, x0 + (x - x0) * (z0 - dz) / (z0 - s.z) + 32.f);
                break;
            }
        }
    }
    return best;
}

// too high (max<0) / yes (block fits) / no (block doesn't) -- exact, no margin added: "physics
// exact, marginal box = state set" applies here the same as the route calculator's landing set.
inline const char* VerdictWord(float block, float max)
{
    if (max < 0.f) return "too high";
    return block <= max ? "yes" : "no";
}

// one printed line, botox dist_calc.cpp dist_calc_render's own verdict lambda, 1:1 text
inline std::string VerdictLine(float block, float max, const char* name)
{
    char b[96];
    if (max < 0.f) snprintf(b, sizeof(b), "%s: no ( too high )", name);
    else snprintf(b, sizeof(b), "%.1f %s: %s ( max %.1f )", block, name, VerdictWord(block, max), max);
    return b;
}

struct DcResult { float block = 0.f, dz = 0.f, ljMax = -1.f, jbMax = -1.f; };

// block (2D edge-to-edge) + dz (height) between the takeoff (a) and landing (b) edges, and both
// verdicts. V is the same point type NearestEdge returns (just .x/.y/.z, no methods needed).
template<class V>
inline DcResult Verdict(const V& a, const V& b, float maxSpeed, float dt, float gravity,
                        float jumpMod, float airAccel, float airCap)
{
    DcResult r;
    const float dx = b.x - a.x, dy = b.y - a.y;
    r.block = std::sqrt(dx * dx + dy * dy);
    r.dz    = b.z - a.z;
    r.ljMax = MaxBlock(false, r.dz, maxSpeed, dt, gravity, jumpMod, airAccel, airCap);
    r.jbMax = MaxBlock(true,  r.dz, maxSpeed, dt, gravity, jumpMod, airAccel, airCap);
    return r;
}

// the short one-line status text the two-click tool prints on each press (botox dist_calc.cpp's
// own movement_add_window(3, ...) line), ready for the call site to hand to its own HUD text
inline std::string StatusLine(const DcResult& r)
{
    char b[160];
    snprintf(b, sizeof(b), "distance: %.1f block, dz %+.1f | lj %s | jb %s", r.block, r.dz,
              VerdictWord(r.block, r.ljMax), VerdictWord(r.block, r.jbMax));
    return b;
}
} // namespace dc
