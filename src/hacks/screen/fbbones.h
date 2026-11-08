#pragma once
#include <atomic>
#include <math.h>
#include <string.h>

// THE GUN'S ANIMATION, AS A VELOCITY -- the half a depth reprojection can never see.
// Ported from coldtires (hacks/visuals/viewmodel_bones.h).
//
// The screen pass reprojects the depth buffer, which only knows what the CAMERA did, so the
// whole viewmodel comes out of it with one transform: its entity pose. A swing, a reload, an
// inspect moves BONES inside that pose, and against one transform every one of them reads as
// no motion at all.
//
// The fix is what every engine with motion blur does -- PreviousBoneMatrices: keep last frame's
// bone matrices and hand the shader the transform each bone went through. TF2 has no shader
// that can write a velocity target, so the bones go up as CONSTANTS instead and a gun pixel
// blends the few nearest it (fx_fb_velocity.hlsl). That is a real per-pixel velocity, which the
// reconstruction then walks with all of its samples -- no feedback loop, no stack of copies.
//
// WHICH BONES, AND THEY DO NOT CHANGE. A v_model runs to a hundred and fifty bones and the
// shader holds sixteen. Re-picking them per frame rebuilds the field a pixel reads its velocity
// out of underneath it every frame, which on screen is jitter. The pick is made ONCE per model,
// off bone positions alone (farthest point sampling from the root), and never touched again.
//
// AND EACH PART GETS ITS OWN SHARE. TF2 draws the weapon and the arms as separate calls
// (C_BaseViewModel::DrawModel -> CEconEntity::DrawOverriddenViewmodel), and one pick over both
// puts most of the sixteen wherever bones are densest -- usually the hand. The weapon gets ten
// slots of its own and the arms six, so the gun always has bones of its own to be nearest to.
//
// AND THE RIDE COMES OFF HERE, ON THE DRAW THREAD. The gun's rigid motion -- bob, the lag behind
// a turn, the kick, all of it driven by walking and turning -- is divided out against the pose
// THESE VERY BONES were built from, in the same breath as the matrices. Doing it later, against
// a pose read on another thread, pairs frame N's bones with frame N+1's ride and leaves the
// whole ride behind as residual: a gun that smears every time the mouse moves at all.
namespace FbBones
{
    // one bone as the shader sees it
    struct Proxy {
        // where it sits THIS frame, in world space -- the point a pixel measures its distance to
        float center[3]     = { 0.f, 0.f, 0.f };

        // and where it sat LAST frame. It is rot * center + offset and the reader could work it
        // out, but a map is sixteen thousand units wide and that product rounds off tens of
        // thousandths of a unit five units from the lens -- which is pixels of streak on a gun
        // that never moved. Kept, so the reader never multiplies a world coordinate by anything.
        float centerThen[3] = { 0.f, 0.f, 0.f };

        // world now -> world then for points rigidly attached to this bone, WITH THE PART'S RIDE
        // TAKEN BACK OUT. A bone that only rode along comes back the identity exactly.
        float rot[3][3]     = {};
    };

    // sixteen is what fits: four constant registers each, and pass one unrolls all of them
    inline constexpr int kMaxProxies    = 16;
    inline constexpr int kWeaponProxies = 10;
    inline constexpr int kOtherProxies  = kMaxProxies - kWeaponProxies;

    struct Set {
        int   count  = 0;
        int   frame  = -1;   // the frame these were measured on
        int   weapon = 0;    // how many of them are the WEAPON's, the arms being the rest
        Proxy proxy[kMaxProxies] = {};
    };

    // ---- what the draw hook hands us ---------------------------------------------------
    // ModelRenderInfo_t, x64 (public/ivmodelrender.h): origin 0x00, angles 0x0C,
    // pRenderable 0x18, pModel 0x20, pModelToWorld 0x28, flags 0x40, entity_index 0x44.
    // DrawModelState_t opens with studiohdr_t* (ivmodelrender.h:38), and studiohdr_t::numbones
    // is at 0x9C (public/studio.h: id/version/checksum/name[64]/length/6 Vectors/flags).
    inline constexpr size_t kMriOrigin       = 0x00;
    inline constexpr size_t kMriAngles       = 0x0C;
    inline constexpr size_t kMriModel        = 0x20;
    inline constexpr size_t kMriModelToWorld = 0x28;
    inline constexpr size_t kStudioNumBones  = 0x9C;

    inline constexpr int kMaxBones = 192;
    inline constexpr int kMaxSlots = 6;

    // a bone matrix: matrix3x4_t, row major, the translation in [row][3]
    struct Mat34 { float m[3][4]; };

    namespace detail
    {
        struct Slot {
            const void* model = nullptr;

            int  frame = -1;
            int  bones = 0;
            bool weapon = false;   // the gun draw, or one of the arms draws

            // WHICH bones this part publishes, chosen once and never touched again
            int picks = 0;
            int pick[kMaxProxies] = {};

            // last frame's pose for this part: the ride taken back out of last frame's bones
            bool  haveRide = false;
            float ride[3][3] = {};
            float rideOrigin[3] = {};

            Mat34 bone[kMaxBones] = {};
        };

        struct State {
            Slot slot[kMaxSlots] = {};

            // how far each bone sits from the nearest one already picked, kept across the pick
            float distance[kMaxBones] = {};

            // this frame's proxies, kept apart so the gun's share is never eaten by an arms draw
            Proxy weaponProxy[kWeaponProxies] = {};
            Proxy otherProxy[kOtherProxies]   = {};

            int weaponCount = 0;
            int otherCount  = 0;
            int frame       = -1;

            // two sets and an index: the draws write one while the screen pass reads the other
            Set published[2] = {};
            std::atomic<int> ready{ -1 };
        };

        inline State& S() { static State s; return s; }

        inline float SquaredBones(const Mat34& a, const Mat34& b)
        {
            const float d[3] = { a.m[0][3] - b.m[0][3], a.m[1][3] - b.m[1][3], a.m[2][3] - b.m[2][3] };
            return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        }

        // FARTHEST POINT SAMPLING, ON POSITIONS ALONE. The root seeds it and every bone after is
        // whichever is furthest from everything already taken, so the proxies spread over the
        // whole part instead of crowding one hand -- a nearest-bone weighting is only as good as
        // the spread it picks from. Runs on the pose it happens to see, and then never again:
        // the SET is what has to be stable, not which pose chose it.
        inline void Choose(Slot& slot, const Mat34* bones, int boneCount)
        {
            State& s = S();

            int quota = slot.weapon ? kWeaponProxies : kOtherProxies;
            if (quota > boneCount) quota = boneCount;

            slot.pick[0] = 0;
            slot.picks   = 1;

            for (int b = 0; b < boneCount; ++b)
                s.distance[b] = SquaredBones(bones[b], bones[0]);

            while (slot.picks < quota) {
                int best = 0;
                for (int b = 1; b < boneCount; ++b)
                    if (s.distance[b] > s.distance[best]) best = b;

                // everything left sits on top of a bone already taken: a duplicate buys nothing
                if (s.distance[best] < 0.25f) break;

                slot.pick[slot.picks++] = best;

                for (int b = 0; b < boneCount; ++b) {
                    const float d = SquaredBones(bones[b], bones[best]);
                    if (d < s.distance[b]) s.distance[b] = d;
                }
            }
        }

        // This part's picked bones, last frame's against this frame's, WITH THE RIDE OUT.
        //
        // A bone's raw transform is A = M_then * M_now^-1 (world now -> world then). Writing the
        // part's pose as Ride, a bone that only rode along has A = Ride_then * Ride_now^-1, so
        // the animation on its own, drawn in the pose the gun holds THIS frame, is
        //
        //   A_local = (Ride_now * Ride_then^-1) * A = D * A
        //
        // and a bone that merely rode comes back the identity exactly. D is built from the two
        // poses these very bones were built from, so no other thread and no other frame gets in.
        inline void Emit(const Slot& slot, const Mat34* bones,
                         const float ride[3][3], const float rideOrigin[3])
        {
            State& s = S();

            float d[3][3] = {};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    d[r][c] = ride[r][0] * slot.ride[c][0] + ride[r][1] * slot.ride[c][1] +
                              ride[r][2] * slot.ride[c][2];

            for (int i = 0; i < slot.picks; ++i) {
                const int b = slot.pick[i];

                Proxy* target = nullptr;
                if (slot.weapon) {
                    if (s.weaponCount < kWeaponProxies) target = &s.weaponProxy[s.weaponCount++];
                } else if (s.otherCount < kOtherProxies) {
                    target = &s.otherProxy[s.otherCount++];
                }
                if (!target) return;

                const Mat34& now  = bones[b];
                const Mat34& then = slot.bone[b];

                // R_then * R_now^T: the turn this bone went through, world to world, ride and all
                float rot[3][3] = {};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        rot[r][c] = then.m[r][0] * now.m[c][0] + then.m[r][1] * now.m[c][1] +
                                    then.m[r][2] * now.m[c][2];

                // D * A, and what is left of it is the animation
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        target->rot[r][c] = d[r][0] * rot[0][c] + d[r][1] * rot[1][c] + d[r][2] * rot[2][c];

                // and the centre it moved to, carried through the same D:
                //   c_then' = o_now + D (c_then - o_then)
                // every coordinate in the multiply is measured from the gun's own origin a few
                // units from the lens, so a map sixteen thousand units wide never reaches the
                // rounding -- the same reason the raw centres are kept instead of rebuilt.
                for (int a = 0; a < 3; ++a) {
                    target->center[a] = now.m[a][3];
                    target->centerThen[a] = rideOrigin[a] +
                        d[a][0] * (then.m[0][3] - slot.rideOrigin[0]) +
                        d[a][1] * (then.m[1][3] - slot.rideOrigin[1]) +
                        d[a][2] * (then.m[2][3] - slot.rideOrigin[2]);
                }

                const float step[3] = { target->centerThen[0] - now.m[0][3],
                                        target->centerThen[1] - now.m[1][3],
                                        target->centerThen[2] - now.m[2][3] };

                // A weapon switch, a respawn, a taunt starting: no animation moves a bone this
                // far in one frame and the whole gun would streak off the screen. The proxy is
                // FLATTENED to the identity rather than dropped -- dropping it changes how many
                // slots this part has and where every one after it lands, and a list that
                // changes shape frame to frame is the jitter the fixed pick just removed.
                if (step[0] * step[0] + step[1] * step[1] + step[2] * step[2] > 24.f * 24.f) {
                    memset(target->rot, 0, sizeof(target->rot));
                    for (int a = 0; a < 3; ++a) {
                        target->rot[a][a] = 1.f;
                        target->centerThen[a] = target->center[a];
                    }
                }
            }
        }

        // the weapon's slots first, so a third part that overflows the sixteen never costs the
        // gun one
        inline void Publish(int frame)
        {
            State& s = S();

            const int ready = s.ready.load(std::memory_order_relaxed);
            const int next  = (ready == 0) ? 1 : 0;

            Set& out = s.published[next];

            int count = 0;
            for (int i = 0; i < s.weaponCount && count < kMaxProxies; ++i) out.proxy[count++] = s.weaponProxy[i];
            for (int i = 0; i < s.otherCount  && count < kMaxProxies; ++i) out.proxy[count++] = s.otherProxy[i];

            out.count  = count;
            out.frame  = frame;
            out.weapon = (s.weaponCount < count) ? s.weaponCount : count;

            s.ready.store(next, std::memory_order_release);
        }

        // AngleMatrix (mathlib): columns forward / left / up. Only ever used as a rigid pose
        // against another of itself, so the column convention cancels.
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

        // The reads that touch game memory, kept in their own function: __try cannot share a
        // frame with anything that has a destructor (C2712), which is the whole rest of this.
        __declspec(noinline) inline bool ReadDraw(const void* pState, const void* pInfo,
                                                  int& boneCount, const void*& model,
                                                  float ride[3][3], float rideOrigin[3])
        {
            __try {
                const void* hdr = *reinterpret_cast<const void* const*>(pState);
                if (reinterpret_cast<uintptr_t>(hdr) < 0x10000) return false;

                boneCount = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(hdr) + kStudioNumBones);
                if (boneCount <= 0 || boneCount > kMaxBones) return false;

                const uint8_t* info = reinterpret_cast<const uint8_t*>(pInfo);

                model = *reinterpret_cast<const void* const*>(info + kMriModel);
                if (reinterpret_cast<uintptr_t>(model) < 0x10000) return false;

                // THE POSE THESE BONES WERE BUILT FROM, taken here and nowhere else.
                // pModelToWorld is the matrix the renderer itself is about to use and is
                // preferred whenever the caller filled one; origin/angles is the same thing
                // built by hand, which is what it falls back to.
                const float* m2w = *reinterpret_cast<const float* const*>(info + kMriModelToWorld);

                if (reinterpret_cast<uintptr_t>(m2w) >= 0x10000) {
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 3; ++c) ride[r][c] = m2w[r * 4 + c];
                        rideOrigin[r] = m2w[r * 4 + 3];
                    }
                } else {
                    const float* o = reinterpret_cast<const float*>(info + kMriOrigin);
                    const float* a = reinterpret_cast<const float*>(info + kMriAngles);
                    const float angles[3] = { a[0], a[1], a[2] };
                    AngleMatrix(angles, ride);
                    rideOrigin[0] = o[0]; rideOrigin[1] = o[1]; rideOrigin[2] = o[2];
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }

        __declspec(noinline) inline bool CopyBones(const void* pBones, Mat34* out, int boneCount)
        {
            __try {
                memcpy(out, pBones, sizeof(Mat34) * (size_t)boneCount);
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return true;
        }
    } // namespace detail

    // Called from the DrawModelExecute detour, once per first-person part per frame. `weapon`
    // is the caller's own classification of the draw (TF2: the c_ weapon attachment, drawn at
    // entity_index -1, against the v_models / _arms stack).
    inline void Collect(const void* pState, const void* pInfo, const void* pBones,
                        bool weapon, int frame)
    {
        if (!pState || !pInfo || !pBones) return;

        detail::State& s = detail::S();

        int         boneCount = 0;
        const void* model     = nullptr;
        float       ride[3][3] = {};
        float       rideOrigin[3] = {};

        if (!detail::ReadDraw(pState, pInfo, boneCount, model, ride, rideOrigin)) return;

        // a new frame: last frame's list is published already and these are somebody else's poses
        if (frame != s.frame) {
            s.frame       = frame;
            s.weaponCount = 0;
            s.otherCount  = 0;
        }

        detail::Slot* slot = nullptr;
        for (auto& e : s.slot) {
            if (e.model == model) { slot = &e; break; }
        }

        if (!slot) {
            slot = &s.slot[0];
            for (auto& e : s.slot)
                if (e.frame < slot->frame) slot = &e;

            slot->model    = model;
            slot->frame    = -1;
            slot->bones    = 0;
            slot->picks    = 0;
            slot->haveRide = false;
            slot->weapon   = weapon;
        }

        // the same part coming through twice in one frame (a second pass over the same model):
        // one measurement per frame or the pair is this frame against itself
        if (slot->frame == frame) return;

        static Mat34 s_now[kMaxBones];   // draw thread only, and 36KB is not going on the stack
        if (!detail::CopyBones(pBones, s_now, boneCount)) return;

        // a new skeleton (first sight of this model, or a different lod): pick its proxies once
        if (slot->picks <= 0 || slot->bones != boneCount)
            detail::Choose(*slot, s_now, boneCount);

        if (slot->frame == frame - 1 && slot->bones == boneCount && slot->haveRide)
            detail::Emit(*slot, s_now, ride, rideOrigin);

        memcpy(slot->bone, s_now, sizeof(Mat34) * (size_t)boneCount);

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) slot->ride[r][c] = ride[r][c];
            slot->rideOrigin[r] = rideOrigin[r];
        }

        slot->haveRide = true;
        slot->bones    = boneCount;
        slot->frame    = frame;

        // PUBLISHED AFTER EVERY PART, not at the end of the frame: there is no end-of-frame
        // callback in this hook, and the screen pass reads whatever the last part left -- which
        // is the whole stack, because all of them draw before the gun pass runs.
        if (s.weaponCount > 0 || s.otherCount > 0) detail::Publish(frame);
    }

    // The screen pass' side: a copy, because the draws above run on the material system's
    // thread when the queue is on.
    inline Set Read()
    {
        detail::State& s = detail::S();
        const int ready = s.ready.load(std::memory_order_acquire);
        if (ready < 0) return Set{};
        return s.published[ready];
    }

    // A map change frees every model_t and the heap hands the same addresses back, so the slots
    // have to forget them or one stale pose pairs with a brand new skeleton.
    inline void Flush()
    {
        detail::State& s = detail::S();
        for (auto& e : s.slot) { e.model = nullptr; e.frame = -1; e.picks = 0; e.haveRide = false; }
        s.weaponCount = 0;
        s.otherCount  = 0;
        s.frame       = -1;
        s.ready.store(-1, std::memory_order_release);
    }
} // namespace FbBones
