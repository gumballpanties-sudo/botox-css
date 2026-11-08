#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// texturebug_pure.h -- pure wallstrafe math, ported 1:1 from botox csgo-sdk
// hacks/movement/texturebug_wallstrafe.h (n_tb::wallstrafe) + texturebug.cpp's own
// tb_wallstrafe_yaw (top of file, lines 55-73). No engine reads, no globals -- gate-tested by
// tests/texturebug_test.cpp. CSS-adapted: the 30.f caps below are CGameMovement::GetAirSpeedCap()
// (gamemovement.h:104, hardcoded 30 in this engine branch, same number botox's cap assumed) --
// not a CS:GO-specific constant, so it ports unchanged. Caller passes CSS's own airaccelerate/
// friction/dt the same way airstuck_pure.h's GridStep does for the same cvar.
namespace tbw
{
    constexpr float k_pi = 3.14159265358979323846f;
    constexpr float k_speed_tolerance = 0.05f;

    // botox n_tb::wallstrafe::retains_speed: did this wallstrafe candidate keep (not lose) speed
    inline bool RetainsSpeed(float before, float after)
    {
        return std::isfinite(before) && std::isfinite(after) && after + k_speed_tolerance >= before;
    }

    struct Wish_t { float into = 0.f, along = 0.f, gain = 0.f; };

    // botox n_tb::wallstrafe::candidates: best along-wall wish + the two neighbours either side of
    // it (0.5 deg step), ranked by acceleration gained along the wall. Engine prediction is the
    // one that validates these against real collisions -- this just ranks the shortlist.
    inline std::array<Wish_t, 3> Candidates(float intoVelocity, float alongVelocity, float wishSpeed, float acceleration)
    {
        std::array<Wish_t, 3> result{};
        if (!std::isfinite(intoVelocity) || !std::isfinite(alongVelocity) || !std::isfinite(wishSpeed) ||
            !std::isfinite(acceleration) || wishSpeed <= 0.f || acceleration <= 0.f)
            return result;
        constexpr float radians = 0.017453292519943295f;
        const auto at = [&](float degrees) {
            Wish_t w{ std::cos(degrees * radians), std::sin(degrees * radians), 0.f };
            const float add = (std::min)(wishSpeed, 30.f) - intoVelocity * w.into - alongVelocity * w.along;
            w.gain = (std::min)(acceleration * wishSpeed, (std::max)(0.f, add)) * w.along;
            return w;
        };
        float angle = 0.f;
        for (int i = 0; i <= 360; ++i)
        {
            const float a = static_cast<float>(i) * 0.25f;
            const Wish_t w = at(a);
            if (w.gain > result[0].gain) { angle = a; result[0] = w; }
        }
        if (result[0].gain <= 0.f) return result;
        result[1] = at((std::max)(0.f, angle - 0.5f));
        result[2] = at((std::min)(90.f, angle + 0.5f));
        return result;
    }

    // botox n_tb::wallstrafe::aligned_wish: add along-wall acceleration while keeping the normal_delta
    // closing speed exactly -- the precision-correction press, with speed gained along the wall as a
    // free side effect where the budget allows it.
    inline Wish_t AlignedWish(float intoVelocity, float alongVelocity, float normalDelta, float wishSpeed, float acceleration)
    {
        Wish_t best{};
        if (!std::isfinite(intoVelocity) || !std::isfinite(alongVelocity) || !std::isfinite(normalDelta) ||
            !std::isfinite(wishSpeed) || !std::isfinite(acceleration) || wishSpeed <= 0.f || acceleration <= 0.f ||
            std::fabs(normalDelta) < 1e-5f)
            return best;
        const double sign = normalDelta < 0.f ? -1.0 : 1.0;
        constexpr double radians = 0.017453292519943295;
        for (int i = 1; i < 360; ++i)
        {
            const double angle = i * 0.25 * radians;
            const double normal = sign * std::cos(angle), along = std::sin(angle);
            const double add = normalDelta / normal;
            const double dot = intoVelocity * normal + alongVelocity * along;
            if (add + dot > (std::min)(static_cast<double>(wishSpeed), 30.0)) continue;
            const double speed = (std::max)(add / acceleration, add + dot);
            const double gain = add * along;
            if (speed > wishSpeed || gain <= best.gain) continue;
            best = { static_cast<float>(normal * speed), static_cast<float>(along * speed), static_cast<float>(gain) };
        }
        return best;
    }

    // botox n_tb::wallstrafe::direction: which along-wall sign to chase (input wins if it's a real
    // press, else keep going whichever way velocity is already moving)
    inline float Direction(float inputAlong, float velocityAlong)
    {
        const float desired = std::fabs(inputAlong) > 1.f ? inputAlong : velocityAlong;
        return desired < 0.f ? -1.f : 1.f;
    }

    // botox tb_wallstrafe_yaw (texturebug.cpp:55-73): the along-wall strafe yaw that keeps the
    // most speed under the air-wish cap (CSS: 30.f, same as botox's cap -- see file header).
    // Returns false (ok) when velocity is too slow to have a meaningful tangent.
    inline bool WallstrafeYaw(float nx, float ny, float vx, float vy, float cap, float& outYawDeg)
    {
        if (std::sqrt(vx * vx + vy * vy) < 1.f) return false;
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl <= 0.f) return false;
        const float unx = nx / nl, uny = ny / nl;
        float tx = -uny, ty = unx;
        if (tx * vx + ty * vy < 0.f) { tx = -tx; ty = -ty; }
        const float s = tx * vx + ty * vy;
        float c = 1.f, sn = 0.f;
        if (s >= cap * 0.5f)
        {
            c  = std::clamp(cap / (s * 2.f), -1.f, 1.f);
            sn = std::sqrt(1.f - c * c);
        }
        outYawDeg = std::atan2(c * ty - sn * uny, c * tx - sn * unx) * (180.f / k_pi);
        return true;
    }
}
