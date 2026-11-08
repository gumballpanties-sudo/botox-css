#pragma once
#include <cmath>
#include <cstdint>
#include <utility>
#include "mc_world_assets.h"   // n_mc_sky::k_sun, k_clouds (botox mc_sky_assets.h, 1:1)

// Pure math for the minecraft sky/clouds pass (mc_world.cpp). No engine types, no globals --
// testable standalone (tests/mcworld_test.cpp). Ported 1:1 from botox
// hacks/visuals/edicts/mc_sky.h + mc_clouds_math.h.
namespace n_mc_world
{
    struct Vec3f { float x = 0.f, y = 0.f, z = 0.f; };

    // Source's own AngleVectors (mathlib_base.cpp:919), QAngle(pitch,yaw,roll). The test builds
    // a view-projection off it to check CornerRays against a known camera.
    inline void AngleVectors(float pitch, float yaw, float roll, Vec3f& fwd, Vec3f& right, Vec3f& up)
    {
        const float d2r = 3.14159265358979323846f / 180.f;
        const float sp = sinf(pitch * d2r), cp = cosf(pitch * d2r);
        const float sy = sinf(yaw   * d2r), cy = cosf(yaw   * d2r);
        const float sr = sinf(roll  * d2r), cr = cosf(roll  * d2r);

        fwd.x = cp * cy;        fwd.y = cp * sy;        fwd.z = -sp;
        right.x = -sr * sp * cy + cr * sy;
        right.y = -sr * sp * sy - cr * cy;
        right.z = -sr * cp;
        up.x = cr * sp * cy + sr * sy;
        up.y = cr * sp * sy - sr * cy;
        up.z = cr * cp;
    }

    // ---- botox n_mc_sky (mc_sky.h) -- the colours the baked skybox was rendered with ----
    // ARGB.lerp -> Mth.lerpInt: a + floor( t * ( b - a ) )
    inline int LerpInt(float t, int a, int b) { return a + (int)std::floor(t * (float)(b - a)); }

    inline void SkyColor(float out[3]) { out[0] = 0x78 / 255.f; out[1] = 0xa7 / 255.f; out[2] = 0xff / 255.f; }

    // fog_color #c0d8ff pulled toward sky_color by 1 - clampedLerp( min( 512 / 16, 12 ) / 32, .25, 1 ) ^ .25 ( igr )
    inline void FogColor(float out[3])
    {
        const float t = 1.f - (float)std::pow(0.25f + (12.f / 32.f) * 0.75f, 0.25);
        out[0] = LerpInt(t, 0xc0, 0x78) / 255.f;
        out[1] = LerpInt(t, 0xd8, 0xa7) / 255.f;
        out[2] = LerpInt(t, 0xff, 0xff) / 255.f;
    }

    // visual/sun_angle at tick 1000: 360 * cubic_bezier( .362, .241, .638, .759 ) over ( tick - 6000 ) / 24000
    inline float SunAngle()
    {
        const auto bz = [](double a, double b, double t) {
            return 3.0 * (1.0 - t) * (1.0 - t) * t * a + 3.0 * (1.0 - t) * t * t * b + t * t * t;
        };
        const double p = (1000.0 - 6000.0 + 24000.0) / 24000.0;
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 60; i++)
            (bz(0.362, 0.638, (lo + hi) * 0.5) < p ? lo : hi) = (lo + hi) * 0.5;
        return (float)(bz(0.241, 0.759, (lo + hi) * 0.5) * 360.0 * 3.14159265358979 / 180.0);
    }

    inline bool CloudCell(int x, int z)
    {
        const int cx = ((x % 256) + 256) % 256, cz = ((z % 256) + 256) % 256;
        return (n_mc_sky::k_clouds[cz * 32 + cx / 8] >> (cx % 8)) & 1;
    }

    // a = cell has cloud, row y = cell z (botox cells_argb)
    inline void CellsArgb(std::uint32_t* out, int pitchTexels)
    {
        for (int z = 0; z < 256; z++)
            for (int x = 0; x < 256; x++)
                out[z * pitchTexels + x] = CloudCell(x, z) ? 0xffffffffu : 0x00ffffffu;
    }

    // ---- botox n_mc_clouds (mc_clouds_math.h) ----------------------------------------
    // tick 1000 at cell 35, 14: sun clear of clouds, ~30% cover overhead
    inline constexpr float  k_referenceOffset[2] = { 35.f * 12.f + 6.f, 14.f * 12.f + 3.96f };
    inline constexpr double k_unitsPerBlock = 40.0;   // tf2 player 72 units = mc player 1.8 blocks
    inline constexpr double k_wrap          = 256.0 * 12.0;

    // view origin ( source units ) + game time -> camera in cloud texture blocks
    inline void CloudOffset(double curtime, float originX, float originY, float out[2])
    {
        const double ticks = std::fmod((curtime > 0.0 ? curtime : 0.0) * 20.0, 256.0 * 400.0);
        const double x = k_referenceOffset[0] - 30.0 + originX / k_unitsPerBlock + ticks * 0.03;
        const double z = k_referenceOffset[1] - originY / k_unitsPerBlock;
        const auto wrap = [](double v) {
            const double m = std::fmod(v, k_wrap);
            return static_cast<float>(m < 0.0 ? m + k_wrap : m);
        };
        out[0] = wrap(x);
        out[1] = wrap(z);
    }

    // ps c30.w = GAMMA_LIGHT_SCALE ( hdr tonemap ). the sky faces get it too, so the pass matches them
    inline float GammaScale(float w) { return std::isfinite(w) && w > 0.01f && w < 64.f ? w : 1.f; }

    inline bool Invert(const double m[4][4], double out[4][4])
    {
        double a[4][8]{};
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                a[r][c]     = m[r][c];
                a[r][c + 4] = r == c ? 1.0 : 0.0;
            }

        for (int c = 0; c < 4; c++) {
            int pivot = c;
            for (int r = c + 1; r < 4; r++)
                if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
            if (!(std::fabs(a[pivot][c]) > 1e-12)) return false;

            std::swap(a[c], a[pivot]);
            const double inv = 1.0 / a[c][c];
            for (double& v : a[c]) v *= inv;

            for (int r = 0; r < 4; r++) {
                if (r == c) continue;
                const double f = a[r][c];
                for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
            }
        }

        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) out[r][c] = a[r][c + 4];
        return true;
    }

    // rows = vs c8..c11 ( cViewProj, common_vs_fxc.h:83, clip = rows * world ). World ray through the
    // four clip-space corners of the shared screenfx quad, strip order tl tr bl br = uv (0,0) (1,0)
    // (0,1) (1,1). A ray at a uv in between is the bilinear mix of these (the unprojected plane is
    // affine in ndc). Eye = the point clip maps to w 0. botox corner_rays, minus its draw_up pixel
    // offsets: our quad already carries the half-texel shift.
    inline bool CornerRays(const float rows[4][4], float out[4][3])
    {
        double m[4][4], inv[4][4];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) m[r][c] = rows[r][c];
        if (!Invert(m, inv)) return false;

        const auto unproject = [&](double x, double y, double z, double w, double p[3]) {
            double h[4];
            for (int r = 0; r < 4; r++)
                h[r] = inv[r][0] * x + inv[r][1] * y + inv[r][2] * z + inv[r][3] * w;
            if (!(std::fabs(h[3]) > 1e-12)) return false;
            for (int i = 0; i < 3; i++) p[i] = h[i] / h[3];
            return true;
        };

        double eye[3];
        if (!unproject(0.0, 0.0, 1.0, 0.0, eye)) return false;

        for (int i = 0; i < 4; i++) {
            const double nx = (i & 1) ? 1.0 : -1.0, ny = (i & 2) ? -1.0 : 1.0;
            double p[3];
            if (!unproject(nx, ny, 0.5, 1.0, p)) return false;

            double len = 0.0;
            for (int k = 0; k < 3; k++) {
                const double d = p[k] - eye[k];
                out[i][k] = static_cast<float>(d);
                len += d * d;
            }
            if (!std::isfinite(len) || !(len > 0.0)) return false;
        }
        return true;
    }
}
