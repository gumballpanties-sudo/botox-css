#pragma once
#include <math.h>

// The maths behind frame blending (hacks/screen/frameblend.cpp), with nothing in it that needs a
// device, a game or a thread -- so tests/fbmath_test.cpp can hold it to its claims directly.
//
// Two claims run through the whole feature, and both are exact identities rather than
// approximations, which is what makes them worth testing:
//
//   1. a point that did not move must reproject onto ITSELF. The velocity buffer is the
//      difference between where a pixel is and where the same world point was on screen last
//      frame -- so a wall must come back zero however far the camera flew, or the whole frame
//      smears while nothing moved.
//   2. a point riding on a player must reproject onto where HE was. Same identity, with his own
//      step taken back out of the fourth column.
namespace fbm
{
    // AngleVectors (mathlib). The view basis is these three in the order the shader works in:
    // right, up, forward.
    inline void AngleVectors(const float ang[3], float fwd[3], float right[3], float up[3])
    {
        const float k = 3.14159265f / 180.f;
        const float sp = sinf(ang[0] * k), cp = cosf(ang[0] * k);
        const float sy = sinf(ang[1] * k), cy = cosf(ang[1] * k);
        const float sr = sinf(ang[2] * k), cr = cosf(ang[2] * k);

        fwd[0]   = cp * cy;                 fwd[1]   = cp * sy;                 fwd[2]   = -sp;
        right[0] = -sr * sp * cy + cr * sy; right[1] = -sr * sp * sy - cr * cy; right[2] = -sr * cp;
        up[0]    = cr * sp * cy + sr * sy;  up[1]    = cr * sp * sy - sr * cy;  up[2]    = cr * cp;
    }

    // rows: right, up, forward. p = basis * (world - eye) is then the view-space point the
    // shader's view_pos() rebuilds out of the depth.
    inline void Basis(const float ang[3], float basis[3][3])
    {
        float fwd[3], right[3], up[3];
        AngleVectors(ang, fwd, right, up);

        for (int a = 0; a < 3; ++a) {
            basis[0][a] = right[a];
            basis[1][a] = up[a];
            basis[2][a] = fwd[a];
        }
    }

    // prev_view^T * cur_view, with prev_view^T * (eye_now - eye_then) in the fourth column.
    // Multiplied out against a view point of this frame it gives that point's view position in
    // the LAST frame -- see the header comment.
    inline void Reprojection(const float prevBasis[3][3], const float basis[3][3],
                             const float step[3], float rows[3][4])
    {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                rows[r][c] = prevBasis[r][0] * basis[c][0] +
                             prevBasis[r][1] * basis[c][1] +
                             prevBasis[r][2] * basis[c][2];

            rows[r][3] = prevBasis[r][0] * step[0] + prevBasis[r][1] * step[1] + prevBasis[r][2] * step[2];
        }
    }

    inline void Apply(const float rows[3][4], const float p[3], float out[3])
    {
        for (int r = 0; r < 3; ++r)
            out[r] = rows[r][0] * p[0] + rows[r][1] * p[1] + rows[r][2] * p[2] + rows[r][3];
    }

    // the camera's axes as columns with the eye in .w: view space back to world space, which is
    // the only way to ask which player's box a pixel is standing in
    inline void ViewToWorld(const float basis[3][3], const float origin[3], float rows[3][4])
    {
        for (int r = 0; r < 3; ++r) {
            rows[r][0] = basis[0][r];
            rows[r][1] = basis[1][r];
            rows[r][2] = basis[2][r];
            rows[r][3] = origin[r];
        }
    }

    // the shader's own two lines, kept here so a test can hold the cpu and the gpu to one story
    inline void ViewPos(float u, float v, float z, float tanX, float tanY, float out[3])
    {
        out[0] = (u * 2.f - 1.f) * tanX * z;
        out[1] = (1.f - v * 2.f) * tanY * z;
        out[2] = z;
    }

    inline void ProjectUv(const float p[3], float tanX, float tanY, float out[2])
    {
        out[0] = (p[0] / (p[2] * tanX)) * 0.5f + 0.5f;
        out[1] = 0.5f - (p[1] / (p[2] * tanY)) * 0.5f;
    }

    // ==== THE SHUTTER, RAISED TO A POWER ==================================================
    // Every transform here measures ONE frame: where a point was on the frame before this one.
    // A shutter is a length of TIME, so the streak wants that transform over shutter/frametime
    // frames -- and scaling the displacement by that number is only right for a straight line.
    // An animation is not one: a swing is an arc, and a linearly stretched arc leaves the
    // model's own path and points the streak the wrong way. So the transform itself goes to the
    // power instead: the same screw motion, run for longer.
    //
    // A rigid transform A(x) = Rx + t is a rotation about an axis through a fixed point plus a
    // slide along that axis. Find both and every power is free: turn by s times the angle, slide
    // s times as far. The fixed point comes out of (I - R)p = t with the axis added back in to
    // make the singular direction solvable.
    //
    // AND THE FIXED POINT IS ONLY ASKED FOR WHEN IT EXISTS. As the angle goes to zero the centre
    // of the turn runs off to infinity -- a bone that turned a fiftieth of a degree puts its
    // fixed point thousands of units away, and the line at the bottom then takes the difference
    // of two huge numbers to land on a translation of a hundredth of a unit. In float that
    // difference is mostly rounding, and rounding that changes every frame on a gun held still
    // is the jitter. Under a degree the arc and its chord are the same to a part in ten thousand.
    inline void PowerTransform(float rot[3][3], float translation[3], float exponent)
    {
        if (fabsf(exponent - 1.f) < 0.01f) return;

        const float trace = rot[0][0] + rot[1][1] + rot[2][2];

        float cosAngle = (trace - 1.f) * 0.5f;
        if (cosAngle < -1.f) cosAngle = -1.f;
        if (cosAngle >  1.f) cosAngle =  1.f;

        const float angle = acosf(cosAngle);

        float axis[3] = { rot[2][1] - rot[1][2], rot[0][2] - rot[2][0], rot[1][0] - rot[0][1] };

        const float sine = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]) * 0.5f;

        // no turn at all, or a half turn in one frame (which is not an animation, it is a
        // switch): the rotation is left alone and the slide is all there is
        if (sine < 1e-6f || angle < 1e-6f) {
            for (int a = 0; a < 3; ++a) translation[a] *= exponent;
            return;
        }

        for (int a = 0; a < 3; ++a) axis[a] /= sine * 2.f;

        // rodrigues at s times the angle, about the same axis. Exact however small the angle is.
        const float turned = angle * exponent;
        const float co = cosf(turned), si = sinf(turned);

        float scaled[3][3] = {};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                scaled[r][c] = ((r == c) ? co : 0.f) + (1.f - co) * axis[r] * axis[c];

        scaled[0][1] -= si * axis[2];
        scaled[0][2] += si * axis[1];
        scaled[1][0] += si * axis[2];
        scaled[1][2] -= si * axis[0];
        scaled[2][0] -= si * axis[1];
        scaled[2][1] += si * axis[0];

        // the slide along the axis, which (I - R) can never account for: the part it maps to zero
        const float along = axis[0] * translation[0] + axis[1] * translation[1] + axis[2] * translation[2];

        const float across[3] = { translation[0] - axis[0] * along,
                                  translation[1] - axis[1] * along,
                                  translation[2] - axis[2] * along };

        bool  linear = angle < 0.02f;
        float fixed[3] = {};

        if (!linear) {
            // (I - R) + a a^T: singular along the axis without the outer product, invertible with
            float solve[3][3] = {};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    solve[r][c] = ((r == c) ? 1.f : 0.f) - rot[r][c] + axis[r] * axis[c];

            const float det = solve[0][0] * (solve[1][1] * solve[2][2] - solve[1][2] * solve[2][1]) -
                              solve[0][1] * (solve[1][0] * solve[2][2] - solve[1][2] * solve[2][0]) +
                              solve[0][2] * (solve[1][0] * solve[2][1] - solve[1][1] * solve[2][0]);

            if (fabsf(det) < 1e-8f) {
                linear = true;
            } else {
                const float inv = 1.f / det;

                // cramer, three columns: the fixed point the whole motion turns around
                for (int col = 0; col < 3; ++col) {
                    float minor[3][3] = {};
                    for (int r = 0; r < 3; ++r)
                        for (int in = 0; in < 3; ++in)
                            minor[r][in] = (in == col) ? across[r] : solve[r][in];

                    fixed[col] = (minor[0][0] * (minor[1][1] * minor[2][2] - minor[1][2] * minor[2][1]) -
                                  minor[0][1] * (minor[1][0] * minor[2][2] - minor[1][2] * minor[2][0]) +
                                  minor[0][2] * (minor[1][0] * minor[2][1] - minor[1][1] * minor[2][0])) * inv;
                }

                // a centre this far out is the degenerate case the angle test catches, rounder
                if (fixed[0] * fixed[0] + fixed[1] * fixed[1] + fixed[2] * fixed[2] > 4096.f * 4096.f)
                    linear = true;
            }
        }

        // x -> R^s (x - p) + p + s (a . t) a, or the straight scaling when there is no usable p
        for (int r = 0; r < 3; ++r) {
            translation[r] = linear
                ? translation[r] * exponent
                : fixed[r] + axis[r] * along * exponent -
                  (scaled[r][0] * fixed[0] + scaled[r][1] * fixed[1] + scaled[r][2] * fixed[2]);

            for (int c = 0; c < 3; ++c) rot[r][c] = scaled[r][c];
        }
    }

    // the same on a 3x4 laid out as rows, which is how the bones are already built
    inline void PowerRows(float rows[3][4], float exponent)
    {
        float rot[3][3] = {};
        float translation[3] = {};

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) rot[r][c] = rows[r][c];
            translation[r] = rows[r][3];
        }

        PowerTransform(rot, translation, exponent);

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) rows[r][c] = rot[r][c];
            rows[r][3] = translation[r];
        }
    }

    // ---- a bone, into what the velocity pass reads ---------------------------------------
    // The proxy arrives as a world-to-world transform for the points attached to it, and the
    // shader wants view space now -> view space then:
    //
    //   w  = eye + basis^T p     this frame's view point, back in the world
    //   w' = A (w - c) + c_then  where that point was, under this bone
    //   p' = basis (w' - eye)    and back into view space
    //
    // THROUGH THIS FRAME'S CAMERA AT BOTH ENDS: the ride is already out of the proxy, so both
    // ends live in the gun as it is held right now and the only thing left is the animation.
    //
    // AND NO WORLD COORDINATE GOES INTO THE MULTIPLY. A x + a is the same as A (x - c) + c_then,
    // so the eye goes in relative to the bone and the two sixteen thousands cancel BEFORE the
    // multiply instead of after it -- after it, the rounding is hundredths of a unit five units
    // from the lens, which is a still gun wearing pixels of streak.
    inline void BoneRows(const float basis[3][3], const float origin[3],
                         const float rot[3][3], const float center[3], const float centerThen[3],
                         float rows[3][4])
    {
        float worldToView[3][3] = {};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                worldToView[r][c] = rot[r][0] * basis[c][0] + rot[r][1] * basis[c][1] + rot[r][2] * basis[c][2];

        float shift[3] = {};
        for (int a = 0; a < 3; ++a)
            shift[a] = rot[a][0] * (origin[0] - center[0]) +
                       rot[a][1] * (origin[1] - center[1]) +
                       rot[a][2] * (origin[2] - center[2]) + centerThen[a] - origin[a];

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                rows[r][c] = basis[r][0] * worldToView[0][c] +
                             basis[r][1] * worldToView[1][c] +
                             basis[r][2] * worldToView[2][c];

            rows[r][3] = basis[r][0] * shift[0] + basis[r][1] * shift[1] + basis[r][2] * shift[2];
        }
    }

    // ---- viewmodel sway (botox true_motion_blur.cpp sway_sample / sway_at / sway_velocity) ---
    // The gun lagging behind a turn, as cl_wpn_sway_interp / cl_wpn_sway_scale model it
    // (tf_viewmodel.cpp CTFViewModel::CalcViewModelLag): the view angles a short time ago,
    // turned into how far the forward vector slid in the gun's own frame. The ride is divided
    // out of every bone proxy, so without this a turn never smears the gun at all.
    inline float NormAngle(float a)
    {
        a = fmodf(a + 180.f, 360.f);
        if (a < 0.f) a += 360.f;
        return a - 180.f;
    }

    struct SwayHistory
    {
        static constexpr int N = 128;   // > (interp + window) at 400 fps
        float time[N]      = {};
        float angles[N][3] = {};
        int   head = 0, filled = 0;

        void Clear() { head = filled = 0; }

        void Sample(const float ang[3], float t)
        {
            time[head] = t;
            for (int a = 0; a < 3; ++a) angles[head][a] = ang[a];
            head = (head + 1) % N;
            if (filled < N) ++filled;
        }

        // the angles at time t, interpolated between the two samples around it
        bool AnglesAt(float t, float out[3]) const
        {
            if (filled < 2) return false;
            const int newest = (head - 1 + N) % N;
            if (t >= time[newest]) { for (int a = 0; a < 3; ++a) out[a] = angles[newest][a]; return true; }

            int newer = newest;
            for (int step = 1; step < filled; ++step) {
                const int older = (newest - step + N) % N;
                if (time[older] > t) { newer = older; continue; }
                const float span = time[newer] - time[older];
                float f = (span > 1e-6f) ? (t - time[older]) / span : 0.f;
                f = (f < 0.f) ? 0.f : (f > 1.f ? 1.f : f);
                for (int a = 0; a < 3; ++a)
                    out[a] = angles[older][a] + NormAngle(angles[newer][a] - angles[older][a]) * f;
                return true;
            }
            return false;
        }
    };

    // the gun's offset at time t in VIEW axes (right, up, forward), botox sway_at
    inline bool SwayAt(const SwayHistory& h, float t, float interp, float scale, float out[3])
    {
        out[0] = out[1] = out[2] = 0.f;
        if (interp <= 0.f) return false;

        float held[3], lagged[3];
        if (!h.AnglesAt(t, held) || !h.AnglesAt(t - interp, lagged)) return false;

        const float diff[3] = { -NormAngle(lagged[0] - held[0]), -NormAngle(lagged[1] - held[1]),
                                -NormAngle(lagged[2] - held[2]) };
        float lf[3], r[3], u[3];
        AngleVectors(diff, lf, r, u);

        const float slide[3] = { (1.f - lf[0]) * scale, -lf[1] * scale, -lf[2] * scale };

        float f[3];
        AngleVectors(held, f, r, u);
        float fade = fabsf(u[2]) - 0.02f;
        fade = (fade < 0.f) ? 0.f : (fade > 1.f ? 1.f : fade);

        out[0] = -slide[1] * fade;   // TF2 has no cl_righthand: never flipped
        out[1] =  slide[2] * fade;
        out[2] =  slide[0] * fade;
        return true;
    }

    // units per second, over botox's 30 ms window
    inline bool SwayVelocity(const SwayHistory& h, float t, float interp, float scale, float out[3])
    {
        out[0] = out[1] = out[2] = 0.f;
        constexpr float window = 0.03f;
        float now[3], before[3];
        if (!SwayAt(h, t, interp, scale, now) || !SwayAt(h, t - window, interp, scale, before)) return false;
        for (int a = 0; a < 3; ++a) out[a] = (now[a] - before[a]) / window;
        return true;
    }

    // what goes onto every bone proxy's translation: back along the sway for the shutter,
    // divided by the viewmodel amount the shader multiplies in, clamped (never dropped -- a
    // dropped shift changes nothing on the proxy list but pops the streak) at 4 units.
    inline void SwayShift(const float velocity[3], float shutterSec, float sway, float vmAmount, float out[3])
    {
        const float gain = sway / ((vmAmount > 0.05f) ? vmAmount : 0.05f);
        float len = 0.f;
        for (int a = 0; a < 3; ++a) { out[a] = -velocity[a] * shutterSec * gain; len += out[a] * out[a]; }
        len = sqrtf(len);
        if (len > 4.f) for (int a = 0; a < 3; ++a) out[a] *= 4.f / len;
    }

    // player trail / animation trail: how much of last frame's history survives this one.
    // A time constant of shutter x amount, so it reads the same at any fps; capped at 0.9 or the
    // history never lets go (botox true_motion_blur.cpp:1052).
    inline float TrailBlend(float elapsedSec, float shutterSec, float amount)
    {
        const float trail = shutterSec * ((amount > 0.f) ? amount : 0.f);
        if (trail <= 0.f || elapsedSec <= 0.f) return 0.f;
        const float b = expf(-elapsedSec / trail);
        return (b > 0.9f) ? 0.9f : b;
    }
} // namespace fbm
