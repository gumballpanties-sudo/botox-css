#pragma once
// Low fps viewmodel -- botox hacks/visuals/viewmodel_fps.h, 1:1.
//
// Bones held in CAMERA space, refreshed g_vmLowFpsRate times a second, put back on the live camera
// every frame: anim + sway + bob step, gun stays glued to the screen. Main thread only.
//
// botox hooks DrawViewModels for the camera and "inside = viewmodel". TF2 side: camera = the
// CViewSetup OverrideView (slot 16) hands back (same origin/angles DrawViewModels draws with),
// "inside" = h_DrawModelExecute's ViewmodelDrawKind (gun + arms draws). Clock = botox sim_clock:
// absoluteframetime summed, so host_framerate / demo speed step the gun like the recording sees
// it, a missed frame adds 1 s so every gate resets.
//
// Core is pure (no game memory, no windows.h): tests/vmfps_test.cpp. Game reads live in the callers.
#include <algorithm>
#include <cmath>

namespace VmFps
{
    // a bone matrix: matrix3x4_t, row major, translation in [row][3]
    struct Mat34 { float m[3][4]; };

    inline constexpr int kMaxBones = 256;
    inline constexpr int kMaxSlots = 8;

    // studiohdr_t::numbones, x64 (public/studio.h: id/version/checksum/name[64]/length/6 Vectors/flags)
    // -- same literal fbbones.h reads off DrawModelState_t's first pointer.
    inline constexpr size_t kStudioNumBones = 0x9C;

    // mathlib AngleMatrix, rotation part. Only ever used against its own transpose, so the
    // column convention cancels -- orthonormal is all that matters.
    inline void AngleMatrix(const float ang[3], float out[3][3])
    {
        const float k = 3.14159265f / 180.f;
        const float sp = sinf(ang[0] * k), cp = cosf(ang[0] * k);
        const float sy = sinf(ang[1] * k), cy = cosf(ang[1] * k);
        const float sr = sinf(ang[2] * k), cr = cosf(ang[2] * k);
        out[0][0] = cp * cy;                 out[1][0] = cp * sy;                 out[2][0] = -sp;
        out[0][1] = sr * sp * cy - cr * sy;  out[1][1] = sr * sp * sy + cr * cy;  out[2][1] = sr * cp;
        out[0][2] = cr * sp * cy + sr * sy;  out[1][2] = cr * sp * sy - sr * cy;  out[2][2] = cr * cp;
    }

    struct Core
    {
        // Once per frame, before the viewmodel draws (botox: begin()). origin/angles = the camera.
        void Frame(int frameCount, float absFrameTime, float rate, const float origin[3], const float angles[3])
        {
            float r[3][3];
            AngleMatrix(angles, r);
            for (int row = 0; row < 3; ++row) {
                m_cam.m[row][0] = r[row][0]; m_cam.m[row][1] = r[row][1]; m_cam.m[row][2] = r[row][2];
                m_cam.m[row][3] = origin[row];
            }

            if (frameCount == m_frame) return;

            // sim clock: frame gap of 1 = real frame time (clamped), anything else = 1 s
            m_clock += frameCount - m_frame == 1 ? std::clamp(absFrameTime, 0.f, 0.25f) : 1.f;
            m_frame = frameCount;

            const float now  = static_cast<float>(m_clock);
            const float step = 1.f / std::clamp(rate, 15.f, 60.f);
            const float gap  = now - m_last;

            m_step = gap >= step || gap < 0.f;

            // whole steps only: cadence stays even when the game fps isn't a multiple of the rate
            if (m_step)
                m_last = gap < 0.f || gap > step * 4.f ? now : m_last + step * std::floor(gap / step);
        }

        // The bones to draw with (botox: apply()). count already validated 1..kMaxBones.
        const Mat34* Apply(const void* model, const Mat34* bones, int count)
        {
            if (!bones || !model || count <= 0 || count > kMaxBones) return bones;
            m_drawn = m_frame;

            Slot* slot = nullptr;
            for (auto& e : m_slot) if (e.model == model) { slot = &e; break; }

            if (!slot) {
                slot = &m_slot[0];
                for (auto& e : m_slot) if (e.seen < slot->seen) slot = &e;
                slot->model = model;
                slot->seen  = -2;
            }

            Mat34* const out = slot->out[m_frame & 1];

            if (slot->seen == m_frame) return out;

            // new part / swapped skeleton / not drawn last frame (scope, swap): never show an old pose
            if (m_step || slot->count != count || slot->seen != m_frame - 1)
                for (int b = 0; b < count; ++b) ToCamera(bones[b], slot->held[b]);

            slot->count = count;
            slot->seen  = m_frame;

            for (int b = 0; b < count; ++b) FromCamera(slot->held[b], out[b]);
            return out;
        }

        bool Stepped() const { return m_step; }

        // Frame() ran for this game frame. The draw gates on it: no camera this frame (hook not
        // installed, odd view) = live bones, never a pose held from whenever the camera last came.
        bool OnFrame(int frameCount) const { return m_frame == frameCount; }

        // A viewmodel part went through Apply this frame = DrawViewModels is running (or ran).
        // Gates the cloak meter: viewmodel vgui screens draw in that pass, world ones before it.
        bool ViewmodelDrawn() const { return m_drawn == m_frame; }

    private:
        struct Slot {
            const void* model = nullptr;
            int seen  = -2;
            int count = 0;
            Mat34 held[kMaxBones]{};
            // by frame parity: last frame's array may still be in flight on the queued material thread
            Mat34 out[2][kMaxBones]{};
        };

        // camera is rigid: inverse = transpose rotation, minus origin
        void ToCamera(const Mat34& world, Mat34& local) const
        {
            const auto& c = m_cam.m;
            const float away[3] = { world.m[0][3] - c[0][3], world.m[1][3] - c[1][3], world.m[2][3] - c[2][3] };
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col)
                    local.m[row][col] = c[0][row] * world.m[0][col] + c[1][row] * world.m[1][col] + c[2][row] * world.m[2][col];
                local.m[row][3] = c[0][row] * away[0] + c[1][row] * away[1] + c[2][row] * away[2];
            }
        }

        void FromCamera(const Mat34& local, Mat34& world) const
        {
            const auto& c = m_cam.m;
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 4; ++col)
                    world.m[row][col] = c[row][0] * local.m[0][col] + c[row][1] * local.m[1][col] + c[row][2] * local.m[2][col];
                world.m[row][3] += c[row][3];
            }
        }

        Slot   m_slot[kMaxSlots]{};
        Mat34  m_cam{};
        double m_clock = 0.0;
        int    m_frame = -1;
        int    m_drawn = -2;
        float  m_last  = 0.f;
        bool   m_step  = false;
    };
}

inline VmFps::Core g_vmFps{};
