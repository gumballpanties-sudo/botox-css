#pragma once
// Whole-map pixel surf CANDIDATES, engine-free (gate test: tests/psmap_test.cpp).
//
// The finder's own rule: no trace can screen for a pixel surf, only the movement sim can
// (psassist.inl). This header does not try. It answers the cheaper question -- WHERE IS THERE
// EVEN A SEAM TO ASK ABOUT -- so the sim is spent on a few thousand real spots instead of a
// grid over the whole map.
//
// A pixel surf rides a brush TOP at the hull's feet with the wall carrying on above it
// (tests/fmseam_test.cpp says the same thing from the other side: "a brush TOP at the hull top
// is a pixel surf, not a head surf"). The collision BSP keeps the mapper's brushes as authored,
// so every such spot is the TOP EDGE OF A BRUSH'S WALL FACE, and that edge is a horizontal
// segment whose ends are the face's own ends -- the whole length of the surf, exactly.
//
// Planes are IEngineTrace::GetBrushInfo's: Vector4D (nx, ny, nz, dist), normal out of the
// brush, solid where dot(n, p) < dist. FmPlane (fm_seam.h) is that layout.
#include "fm_seam.h"
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

// a candidate: one wall face's top edge, as a horizontal segment on the wall surface
struct PsMapSeam
{
    float nx = 0.f, ny = 0.f;   // face normal, unit xy -- points out of the brush, at the player
    float z = 0.f;              // the edge height = the brush's flat top
    float ax = 0.f, ay = 0.f;   // both ends, in the wall plane at z
    float bx = 0.f, by = 0.f;
    float Len() const { const float dx = bx - ax, dy = by - ay; return std::sqrt(dx * dx + dy * dy); }
};

// A ledge you can stand on is a face that is near vertical. The finder anchors on anything up to
// |nz| 0.7, but that is a face the CURSOR picked; here nothing is aiming, so a 45 degree slab
// would hand the sim a sloped "edge" per brush in the map. 0.2 = ~11 degrees off vertical.
inline constexpr float k_psMapMaxNz  = 0.2f;
inline constexpr float k_psMapMinLen = 12.f;    // shorter than a hull is half of: nothing rides it
inline constexpr float k_psMapMaxLen = 8192.f;  // sanity, not a real limit

// brush's flat top, i.e. the plane a hull could rest a sliver of itself on. Several
// (the compiler's axial bevels duplicate it) -> the lowest, which is the real top.
// No flat top = a wedge or a ramp: its "edges" are sloped and no hull stands on them.
inline bool PsMap_BrushTop(const FmPlane* p, int count, float& zTop)
{
    bool found = false;
    for (int i = 0; i < count; ++i)
    {
        if (p[i].nz < 0.999f) continue;
        const float z = p[i].d / p[i].nz;
        if (!found || z < zTop) { zTop = z; found = true; }
    }
    return found;
}

// The top edge of plane `face`, clipped by every other plane of the brush AT that height.
// Everything is 2D: at a fixed z a plane is a half-line constraint, so the edge is a 1D clip
// (a*s <= c per plane) along the face's own tangent. Exact for a brush with a flat top, which
// PsMap_BrushTop already required, and that is every world brush a wall is built from.
inline bool PsMap_FaceSeam(const FmPlane* p, int count, int face, float zTop, PsMapSeam& out)
{
    if (face < 0 || face >= count) return false;
    const FmPlane& w = p[face];
    if (std::fabs(w.nz) > k_psMapMaxNz) return false;
    const float len2 = std::sqrt(w.nx * w.nx + w.ny * w.ny);
    if (len2 < 0.1f) return false;

    const float nx = w.nx / len2, ny = w.ny / len2;
    const float d2 = (w.d - w.nz * zTop) / len2;   // nx*x + ny*y = d2, in the z = zTop slice
    const float px = nx * d2, py = ny * d2;        // the point of the line nearest the origin
    const float tx = ny, ty = -nx;                 // wall tangent (the finder's own convention)

    float smin = -k_psMapMaxLen, smax = k_psMapMaxLen;
    for (int i = 0; i < count; ++i)
    {
        if (i == face) continue;
        const float a = p[i].nx * tx + p[i].ny * ty;
        const float c = p[i].d - p[i].nz * zTop - (p[i].nx * px + p[i].ny * py);
        if (a > 1e-4f)       { const float s = c / a; if (s < smax) smax = s; }
        else if (a < -1e-4f) { const float s = c / a; if (s > smin) smin = s; }
        else if (c < -1e-3f) return false;   // parallel and outside: this face is not here at all
        if (smax - smin < k_psMapMinLen) return false;
    }
    if (smax - smin < k_psMapMinLen || smax - smin > k_psMapMaxLen) return false;

    out.nx = nx; out.ny = ny; out.z = zTop;
    out.ax = px + tx * smin; out.ay = py + ty * smin;
    out.bx = px + tx * smax; out.by = py + ty * smax;
    return true;
}

// every candidate of one brush (its wall faces' top edges). Appends, never clears.
inline void PsMap_BrushSeams(const FmPlane* p, int count, std::vector<PsMapSeam>& out)
{
    float zTop = 0.f;
    if (!PsMap_BrushTop(p, count, zTop)) return;
    PsMapSeam s;
    for (int i = 0; i < count; ++i)
        if (PsMap_FaceSeam(p, count, i, zTop, s))
            out.push_back(s);
}

// ---- merge: a wall is authored as a ROW of brushes -------------------------------------------
// Eight brushes side by side make eight collinear edges that butt together, which is one surf
// and would otherwise be eight scans and eight lines. Same plane (0.5u, the same tolerance
// FmSeam_HasFace calls coplanar), same height, touching within `gap` -> one segment.
// Sorted in place; the result is the merged list.
inline void PsMap_Merge(std::vector<PsMapSeam>& v, float gap = 2.f)
{
    auto key = [](const PsMapSeam& s) {
        // plane identity, quantised: normal to 0.01, distance to 0.5u, HEIGHT to 1/64u.
        // The height is the odd one out on purpose. Depth is a tolerance question -- two faces
        // 0.3u apart are the same wall, which is what FmSeam_HasFace's 0.5u means -- but the
        // height is what gets PROBED, and the probe owns the 1u unit the height falls in (TryPin
        // places the hull at floor(z) + 0.0287 and sweeps that unit). At 0.5u two brushes at
        // 511.9 and 512.1 landed in one bucket, merged into one run, and the whole run was then
        // probed at 511.x -- so every sample over the second brush asked the wrong unit and its
        // surf could not be found at all. Off-grid heights a few tenths apart is not an exotic
        // case, it is exactly the vertex-manipulated geometry pixel surfs live on.
        // A real row of brushes shares its top plane bit for bit (nz is exactly 1 on an axial
        // top, so z is the plane distance itself), so 1/64u still merges every row it should.
        const float d = s.nx * s.ax + s.ny * s.ay;
        return std::array<long, 4>{ std::lround(s.nx * 100.f), std::lround(s.ny * 100.f),
                                    std::lround(d * 2.f), std::lround(s.z * 64.f) };
    };
    // the run's own tangent decides the order, so "along the wall" means one thing per plane
    auto proj = [](const PsMapSeam& s, float x, float y) { return s.ny * x - s.nx * y; };

    std::sort(v.begin(), v.end(), [&](const PsMapSeam& a, const PsMapSeam& b) {
        const auto ka = key(a), kb = key(b);
        if (ka != kb) return ka < kb;
        return std::min(proj(a, a.ax, a.ay), proj(a, a.bx, a.by))
             < std::min(proj(b, b.ax, b.ay), proj(b, b.bx, b.by));
    });

    std::vector<PsMapSeam> outv;
    for (const PsMapSeam& s : v)
    {
        if (!outv.empty() && key(outv.back()) == key(s))
        {
            PsMapSeam& m = outv.back();
            const float m0 = proj(m, m.ax, m.ay), m1 = proj(m, m.bx, m.by);
            const float s0 = proj(m, s.ax, s.ay), s1 = proj(m, s.bx, s.by);
            const float mlo = std::min(m0, m1), mhi = std::max(m0, m1);
            const float slo = std::min(s0, s1), shi = std::max(s0, s1);
            if (slo <= mhi + gap && shi >= mlo - gap)   // touching or overlapping: extend
            {
                if (slo < mlo) { m.ax = (s0 < s1) ? s.ax : s.bx; m.ay = (s0 < s1) ? s.ay : s.by; }
                if (shi > mhi) { m.bx = (s0 < s1) ? s.bx : s.ax; m.by = (s0 < s1) ? s.by : s.ay; }
                continue;
            }
        }
        outv.push_back(s);
    }
    v.swap(outv);
}

// ---- where the sim is asked along one seam ----------------------------------------------------
// One probe answers for the hull standing there, and a hull is 49u wide, so a sample every
// `stride` covers the segment. Ends are inset: a sample ON the corner is a hull hanging half off
// the brush, which is a different question and a false no.
inline int PsMap_SampleCount(float len, float stride, int cap)
{
    if (len < k_psMapMinLen) return 0;
    const float usable = len - 2.f * std::min(8.f, len * 0.25f);
    const float step = std::max(stride, 1.f);
    int n = 1 + (int)std::floor(usable / step);
    // THE GAP, NOT THE COUNT, IS WHAT COVERS THE SEAM. floor() alone leaves the last gap wider
    // than the stride whenever the length is not a multiple of it: 184u of usable seam came out
    // as 6 samples 36.8u apart, and a stride is the promise that no unsampled stretch is wider
    // than one. The samples are spread over the WHOLE segment (PsMap_SampleT), so one more of
    // them closes every gap at once, not just the last
    if (n > 1 && usable / float(n - 1) > step) n++;
    return n < 1 ? 1 : (n > cap ? cap : n);
}

// sample i of n, as a fraction of the segment (inset both ends)
inline float PsMap_SampleT(int i, int n, float len)
{
    if (n <= 1) return 0.5f;
    const float inset = std::min(8.f, len * 0.25f) / std::max(len, 1e-3f);
    return inset + (1.f - 2.f * inset) * (float(i) / float(n - 1));
}

// ---- what gets drawn -------------------------------------------------------------------------
// A RUN of consecutive samples that the sim confirmed becomes one segment. Each end is widened
// by half a stride (the sample proved a hull standing there, and a hull is wider than the stride)
// and clamped to the seam's real ends. A gap the sim refused splits the run, so a seam that only
// rides at one end is marked at that end and nowhere else -- the line never claims more surf than
// was proved. `ax/ay -> bx/by` out, at the seam's own height.
inline void PsMap_RunSegment(const PsMapSeam& s, int lo, int hi, int n, float stride,
                             float& ax, float& ay, float& bx, float& by)
{
    const float len = s.Len();
    const float half = (stride * 0.5f) / (len > 1.f ? len : 1.f);
    float t0 = PsMap_SampleT(lo, n, len) - half;
    float t1 = PsMap_SampleT(hi, n, len) + half;
    if (t0 < 0.f) t0 = 0.f;
    if (t1 > 1.f) t1 = 1.f;
    ax = s.ax + (s.bx - s.ax) * t0; ay = s.ay + (s.by - s.ay) * t0;
    bx = s.ax + (s.bx - s.ax) * t1; by = s.ay + (s.by - s.ay) * t1;
}
