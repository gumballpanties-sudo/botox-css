// ============================================================================
// WHOLE-MAP PIXEL SURF SCAN -- every brush in the map, one seam at a time
// ----------------------------------------------------------------------------
// The crosshair finder (psassist.inl) answers "is there a surf on THIS wall, between these two
// heights". This answers "where are they, all of them", and draws each one as a line along the
// whole length of the seam instead of a dot.
//
// It does NOT scan a grid. A pixel surf is a solver artifact on the TOP EDGE OF A BRUSH'S WALL
// FACE (psassist.inl's own note: no trace can screen for one, only the sim can -- but the sim
// has to be pointed somewhere), and the collision BSP still has the mapper's brushes as
// authored. So:
//   1. IEngineTrace::GetBrushesInAABB over the whole world, GetBrushInfo for each brush's planes
//      (fakemove.inl FM_Brushes already does exactly this for the wall-jump seam gate).
//   2. ps_map.h turns each brush into its wall faces' top edges -- a horizontal segment with real
//      ends, which is the length that gets drawn. Rows of brushes merge back into one seam.
//   3. every seam is sampled along its length, and each sample is handed to the SAME column
//      scanner the crosshair finder uses (PfScanChunk, one cell), so probes, prunes, strict mode
//      and the prop gates are one implementation, not two.
// Sequential by construction: one budgeted slice per command, cursor in statics, cancellable,
// and the lines appear seam by seam while it runs.
//
// TEXTUREBUGS COME OUT OF THE SAME WALK (PmTbProbe). A texturebug is a horizontal texture
// boundary on a flush wall, which IS a brush top edge with the wall carrying on above it -- the
// candidate this file already has. Two line traces per sample settle it, against the ~24 sims a
// pixel surf costs, and each one becomes a red ring the route calculator can place a TEXTUREBUG
// marker on. Before this they existed only on a column you aimed at by hand.
//
// WHAT IT REFUSES, and both were in the report ("finds a lot of surfs inside walls or out of
// bounds"): a spot whose leaf is CONTENTS_SOLID (the face is buried inside another brush) or
// PointOutsideWorld (the outward faces of the shell that seals the map). A swept trace cannot
// answer either -- it reports where a sweep stopped, never whether it began somewhere real --
// so both are asked of the BSP directly, ahead of the sims, at PmSpotUsable. The scan also runs
// the strict gates whatever the checkbox says (psassist.inl): thousands of lines on geometry you
// never looked at have nothing to be weighed against.
//
// Not found here, by construction: displacement walls and prop surfaces (neither is a brush),
// brush entities (func_brush & co are not in the world BSP GetBrushesInAABB walks), and faces
// past |nz| 0.7 (k_psMapMaxNz -- floors, the finder's own bound).
//
// THE 3D SKYBOX IS SKIPPED. It is real brushwork in the same BSP, a 1/16 scale copy of the
// scenery, and every wall in it has a top edge: lines out in the void past the map. The engine
// keeps it in its own AREA (m_skybox3d.area, 255 = map has none), so one leaf->area lookup per
// seam drops it before a single sim is spent (PmBuildSeams).

// ---- state (movement thread only, except the two atomics the menu button sets) --------------
static std::vector<PsMapSeam> s_pmSeams;    // candidates for this map, built once per scan
static size_t s_pmSeam = 0;                 // cursor: the seam being verified
static int    s_pmSample = 0;               // sample inside that seam
static int    s_pmSamples = 0;
static int    s_pmRunLo = -1, s_pmRunHi = -1;   // confirmed run of samples inside this seam
static std::atomic<bool> s_pmBusy{ false };     // the menu row reads it on its own thread
static float  s_pmStride = 32.f;            // a hull is 49 wide -- a sample every 32u covers it
// per seam. 64 was a CAP ON COVERAGE, not on cost: a 4000u wall (every outer wall of every
// payload map) got 64 samples 62u apart, wider than the 49u hull, so whole stretches of it were
// never asked and the surfs on them were never found -- the "doesn't find all surfs" half of the
// report. 512 x 32u is 16000u of seam, longer than a map is wide, so the stride now holds
// everywhere and the cap is back to being the runaway guard it was meant to be. The cost is
// bounded by the per-tick budget either way; a longer scan is the price of a complete one
static constexpr int k_pmMaxSamples = 512;
static constexpr size_t k_pmMaxTbPts = 2048;   // rings drawn per scan; see PmTbProbe
static std::atomic<bool> s_pmReq{ false };  // menu -> movement thread: start
static std::atomic<bool> s_pmStop{ false }; // menu -> movement thread: cancel
static std::atomic<int>  s_pmDoneSeams{ 0 }, s_pmTotalSeams{ 0 };

// surfs marked so far. Read from the list itself, never a counter kept beside it: the list is
// also cleared on a map change (PsLoadPointsForMap), which is code that cannot see this file
static int PmLineCount()
{
    std::lock_guard<std::mutex> l(g_psLock);
    return (int)(s_psMapLines.size() / 2);
}

// diag, all per scan
static int    s_pmBrushes = 0, s_pmCells = 0, s_pmNoWall = 0, s_pmHits = 0;
static int    s_pmVoid = 0, s_pmTb = 0;     // samples off the map / in a brush, and seams found
static double s_pmMs = 0.0;
static unsigned long long s_pmStartMs = 0;

static int PmTbCount()
{
    std::lock_guard<std::mutex> l(g_psLock);
    return (int)s_psMapTbPts.size();
}

static void PmAbort()
{
    if (!s_pmBusy) return;
    DebugLog("PMAP: scan cancelled at seam %d/%d (%d line(s) + %d texturebug(s) kept)",
             (int)s_pmSeam, (int)s_pmSeams.size(), PmLineCount(), PmTbCount());
    s_pmBusy = false;
    s_pmSeams.clear();
    s_pmSeams.shrink_to_fit();
    s_pmSeam = 0; s_pmSample = 0; s_pmSamples = 0;
    s_pmRunLo = s_pmRunHi = -1;
    // done/total are left alone on purpose: they describe the scan that just stopped, and the
    // menu row keeps printing them next to the lines it left on the world
    // a job of ours may be half way through the shared scanner
    if (s_pfQuiet) { PfCancelScan(); s_pfQuiet = false; s_pfBandHalf = 16.f; s_pfBandStep = 4.f; }
}

// ---- candidates: every brush in the map, once ------------------------------------------------
// GetBrushesInAABB over the whole world, in tiles (PmBuildSeams). The engine walks its own BSP
// for this, so it is one hitch on the button press (logged as build=), not something to spread
// over ticks: the sims after it are 99% of the work and those ARE spread.
static int s_pmSky = 0;                     // seams dropped as 3D skybox, per scan

// ---- the 3D skybox: its own BSP area, never a place you stand --------------------------------
// m_skybox3d.area is the area the server put the sky_camera in (255 = the map has no 3D sky).
// Asked at three points of the seam, the two spots a hull riding it occupies: just off the face
// at the edge, and PmSpotUsable's hip spot. Any of them in the sky area = the whole seam is
// skybox brushwork (the area is sealed, a seam cannot straddle it). A spot buried in a brush
// sits in a solid leaf (area 0) and is not mistaken for sky.
// Gate armed only when the player's own spot answers with a real area that is NOT the sky's:
// standing in the skybox (noclip) or a query that cannot answer leaves every seam in.
static int PmSkyArea(void* pLocal)
{
    if (g_offSkyArea <= 0) return -1;
    const int sky = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(pLocal) + g_offSkyArea);
    if (sky < 0 || sky >= 255) return -1;
    const int own = MV_PointArea(EntV3(pLocal, g_offOrigin) + Vec3(0.f, 0.f, 40.f));
    if (own <= 0 || own == sky)   // area 0 = solid / outside, never where you stand
    {
        DebugLog("PMAP: skybox gate OFF -- sky area %d, your area %d", sky, own);
        return -1;
    }
    return sky;
}

static bool PmSeamInSky(const PsMapSeam& s, int sky)
{
    const Vec3 n(s.nx, s.ny, 0.f);
    static const float kT[3] = { 0.15f, 0.5f, 0.85f };
    for (float t : kT)
    {
        const Vec3 at(s.ax + (s.bx - s.ax) * t, s.ay + (s.by - s.ay) * t, s.z);
        if (MV_PointArea(at + n * 4.f + Vec3(0.f, 0.f, 4.f)) == sky) return true;
        if (MV_PointArea(at + n * 26.f + Vec3(0.f, 0.f, 36.f)) == sky) return true;
    }
    return false;
}

// ---- TILED, NOT ONE BOX ----------------------------------------------------------------------
// GetBrushesInAABB walks the tree through CM_BoxLeafnums' 1024 slot node ring (engine cmodel.cpp
// NODELIST_MAX). One whole-world box splits at nearly every node, overflows the ring, and the walk
// quits early without a word: de_nuke handed back 3016 of its 6579 brushes, and a replay of that
// exact walk on the .bsp gives the same 3016. Half the map was never a candidate -- the "doesn't
// find all surfs" report. Of 330 installed csgo/tf2/css maps 64 lose brushes as one box (53 of
// 244 tf2 maps; cp_premuda keeps 64 of 5079); 2048u tiles lose none, worst ring peak 524 of 1024
// (csgo/tools/pf_map_tiles_check.py replays the walk on the .bsp files).
// A brush touching several tiles is kept by the one holding its AABB min corner (the bevels make
// every brush list its axial planes), so it is cut into seams once.
// ponytail: fixed grid, halve k_pmTile if a map ever fails that check
static constexpr float k_pmWorld = 16384.f;  // Source's world limit
static constexpr float k_pmTile  = 2048.f;

static bool PmOwnsBrush(const FmPlane* p, int np, const float lo[3])
{
    for (int k = 0; k < 3; ++k)
    {
        float c = 0.f;
        bool have = false;
        for (int i = 0; i < np; ++i)
        {
            const float n = k == 0 ? p[i].nx : k == 1 ? p[i].ny : p[i].nz;
            if (n < -0.9999f && (!have || -p[i].d > c)) { c = -p[i].d; have = true; }
        }
        if (!have) continue;    // no axial plane on this axis: every tile keeps it, merge dedupes
        if (c < lo[k] && lo[k] > -k_pmWorld) return false;
        if (c >= lo[k] + k_pmTile && lo[k] + k_pmTile < k_pmWorld) return false;
    }
    return true;
}

static void PmBuildSeams(void* pLocal)
{
    s_pmSeams.clear();
    s_pmBrushes = 0;
    s_pmSky = 0;
    for (float x = -k_pmWorld; x < k_pmWorld; x += k_pmTile)
    for (float y = -k_pmWorld; y < k_pmWorld; y += k_pmTile)
    for (float z = -k_pmWorld; z < k_pmWorld; z += k_pmTile)
    {
        if (s_pmSeams.size() > 400000) break;   // runaway guard, never reached on a real map
        const float mn[3] = { x, y, z };
        const float mx[3] = { x + k_pmTile, y + k_pmTile, z + k_pmTile };
        FM_Brushes(mn, mx, [&](const FmPlane* p, int np) {
            if (!PmOwnsBrush(p, np, mn)) return false;
            s_pmBrushes++;
            PsMap_BrushSeams(p, np, s_pmSeams);
            return s_pmSeams.size() > 400000;
        }, 1 << 20);
    }
    PsMap_Merge(s_pmSeams);

    // after the merge: a row of skybox brushes is one lookup set, not eight
    const int sky = PmSkyArea(pLocal);
    if (sky >= 0)
    {
        const size_t before = s_pmSeams.size();
        s_pmSeams.erase(std::remove_if(s_pmSeams.begin(), s_pmSeams.end(),
                        [&](const PsMapSeam& s) { return PmSeamInSky(s, sky); }), s_pmSeams.end());
        s_pmSky = (int)(before - s_pmSeams.size());
    }
}

// ---- one sample: is there a pixel surf standing here -----------------------------------------
// The cheap half first. A pixel surf rides a sliver of a brush top WITH THE WALL CARRYING ON
// ABOVE IT; a brush top with open air above is an ordinary ledge you walk onto, and that is most
// of the edges in a map (floors, stairs, crates, rooftops). One ray just above the edge kills
// them for the price of a trace instead of 24 movement sims.
// The catch is a sub-unit thing at the FEET, so the ray goes just over the edge and not at chest
// height: a stub of wall a few units tall above the seam is still a surf, and asking at 24 would
// throw every one of those away.
// THREE HEIGHTS, NOT ONE, AND startsolid IS A NO. Both halves of that were wrong:
//   * 6u was the only height asked, so the very stubs the low ray existed to keep -- a 2u lip
//     over the edge -- came back "no wall" and were never simmed. 2 / 6 / 16 covers a stub, an
//     ordinary course and a tall wall, for two extra line traces against the ~24 sims they gate.
//   * startsolid meant "already inside it: the wall is certainly there", and the ray starts 8u
//     OUT FROM THE FACE. Being inside something out there is not this wall carrying on above the
//     seam, it is a second brush (or a clip) standing in front of the spot, and no hull rides a
//     lip buried in one. That branch is a straight source of the "surfs inside walls" report.
//     Per height, so a lip at the base does not condemn the two heights above it.
// LEANING WALLS (k_psMapMaxNz is the finder's 0.7 now): the ray is centred where the face IS at
// that height (s.lean per unit up), not where it is at the seam, or a wall leaning back 45
// degrees is 16u behind the ray's far end at the top height and an overhang starts the ray
// inside itself. The normal test compares the xy directions: a leaning face's 3D normal has an
// xy part shorter than 1 and never passed dot > 0.9 against the unit seam normal.
static bool PmWallAbove(const PsMapSeam& s, const Vec3& at, TraceFilterWorldProps& filter)
{
    static const float kUp[3] = { 2.f, 6.f, 16.f };
    const Vec3 n(s.nx, s.ny, 0.f);
    for (int i = 0; i < 3; ++i)
    {
        const Vec3 mid = at + Vec3(0.f, 0.f, kUp[i]) - n * (s.lean * kUp[i]);
        GameTrace_t tr{};
        TraceLine(mid + n * 8.f, mid - n * 8.f, MASK_PLAYERSOLID, &filter, &tr);
        if (tr.startsolid || tr.fraction >= 1.f) continue;
        // ...and it has to be THIS wall, not some unrelated face the ray found on the way
        Vec3 hn(tr.plane.normal.x, tr.plane.normal.y, 0.f);
        if (hn.Length2D() < 0.1f) continue;
        hn.Normalize();
        if (hn.x * s.nx + hn.y * s.ny <= 0.9f) continue;
        return true;
    }
    return false;
}

// ---- is this candidate anywhere a player could ever be? --------------------------------------
// GetBrushesInAABB over the whole world hands back every brush in the BSP, and a brush's wall
// faces include the ones facing INTO other brushes and the OUTSIDE faces of the shell that seals
// the map off from the void. Their top edges are perfectly good seams on paper and they are the
// lines drawn through rock and off the edge of the world on the overlay. One leaf lookup at the
// spot a hull would stand -- half a hull's width out from the face, hip height -- answers both:
// CONTENTS_SOLID = buried, PointOutsideWorld = outside any playable area.
static bool PmSpotUsable(const PsMapSeam& s, const Vec3& at)
{
    const Vec3 n(s.nx, s.ny, 0.f);
    return !MV_PointUnusable(at + n * 26.f + Vec3(0.f, 0.f, 36.f));
}

// ---- and the OTHER thing a brush top edge can be: a TEXTUREBUG seam --------------------------
// A texturebug is two coplanar faces the solver clips separately, and on a wall that means a
// horizontal texture boundary -- which IS a brush top edge with the wall carrying on flush above
// it, i.e. exactly the candidate this file already walks. The crosshair pass (psassist.inl
// PfScanSeams) has to hunt for the boundary a unit at a time and then bisect for its height; here
// the plane math already knows it to the float, so two line traces settle it: same plane within
// PfIsSeam's tolerance, different surface name, and an axis-aligned wall for the ulp step to
// exist on at all (TB_UlpStepIntoWall -- a diagonal wall cannot texturebug).
static void PmTbProbe(const PsMapSeam& s, const Vec3& at, TraceFilterWorldProps& filter,
                      const Vec3& vMins, const Vec3& vMaxs)
{
    const Vec3 vN(s.nx, s.ny, 0.f);
    int iAxis = -1;
    TB_UlpStepIntoWall(vN, at, iAxis);
    if (iAxis < 0) return;

    GameTrace_t below{}, above{};
    if (!PfSeamProbe(filter, at, vN, s.z - 1.f, below)) return;
    if (!PfSeamProbe(filter, at, vN, s.z + 1.f, above)) return;
    if (!PfIsSeam(below, above)) return;

    // open air in front of it, or nothing can ride it -- the same thin box the column pass uses
    // (a hull riding a TB is falling past, so its feet are not at the seam)
    Vec3 vFace(above.plane.normal.x, above.plane.normal.y, 0.f);
    if (vFace.Length2D() > 0.1f) vFace.Normalize(); else vFace = vN;
    const float flSup = fabsf(vMaxs.x) * fabsf(vFace.x) + fabsf(vMaxs.y) * fabsf(vFace.y);
    const Vec3 vAt = Vec3(above.endpos.x, above.endpos.y, s.z) + vFace * (flSup + 0.5f);
    GameTrace_t fit{};
    TraceHull(vAt, vAt, Vec3(vMins.x, vMins.y, -4.f), Vec3(vMaxs.x, vMaxs.y, 4.f),
              MASK_PLAYERSOLID, &filter, &fit);
    if (fit.startsolid || fit.allsolid || MV_PointUnusable(vAt)) return;

    // ONE RING PER 64u OF SEAM, AND A CEILING ON THE LOT. A texture boundary runs the whole
    // length of a wall, so a sample every 32u would ring a 4000u wall 125 times -- a wall of red
    // on the overlay and 125 things to aim between. 64u is under a hull's reach either way, so
    // there is always a ring near wherever you end up standing. The cap is a render bound, not a
    // search one: the seams keep being found, they just stop being drawn twice
    const Vec3 vMark(above.endpos.x, above.endpos.y, s.z);
    std::lock_guard<std::mutex> l(g_psLock);
    if (s_psMapTbPts.size() >= k_pmMaxTbPts) return;
    // newest first: the samples walk ALONG a seam, so the ring a duplicate matches is nearly
    // always the one just added and the scan short-circuits on the first compare
    // ponytail: linear, and 2048 is the ceiling that keeps it cheap. A grid hash if a map ever
    // makes this show up in the per-tick budget
    for (size_t i = s_psMapTbPts.size(); i-- > 0; )
    {
        const Vec3& q = s_psMapTbPts[i];
        if (std::fabs(q.z - vMark.z) < 0.5f && (q - vMark).Length2D() < 64.f) return;
    }
    s_psMapTbPts.push_back(vMark);
    s_pmTb++;
}

// hand one cell to the crosshair finder's own scanner and read the answer
static bool PmProbe(void* pLocal, UserCmd_t* pCmd, const PsMapSeam& s, const Vec3& at)
{
    // WHICH HEIGHT TO HAND THE CELL. TryPin places the hull at the DNA catch offset of the unit
    // CONTAINING the height it is given -- floor(z) + 0.0287 -- and its fallback sweeps that unit
    // downward. It spells floor() as trunc(), which is only the same thing for z >= 0, so an
    // integer seam at a NEGATIVE height (half of every TF2 map) would be probed a whole unit low.
    // The column scan never noticed: its next 1u step covers it. A one-cell probe has no next step
    float flProbeZ = at.z;
    if (flProbeZ < 0.f && flProbeZ == floorf(flProbeZ)) flProbeZ += 1.f;

    s_pfNormal  = Vec3(s.nx, s.ny, 0.f);
    s_pfTangent = Vec3(s.ny, -s.nx, 0.f);    // the finder's own convention
    s_pfColumn  = at;
    s_pfLow = s_pfHigh = flProbeZ;           // ONE height: the seam's own
    s_pfSteps = 0;
    s_pfBandHalf = 0.f; s_pfBandStep = 4.f;  // ONE cell: the seam's exact xy is known
    s_pfQuiet = true;
    s_pfBaseButtons = pCmd->buttons;
    s_pfNextI = 0; s_pfNextLat = 0; s_pfHeightHit = false;
    s_pfFound.clear();
    s_pfFallMemo.clear();
    s_pfBusy = true;

    PfScanChunk(pLocal, pCmd);               // 1 cell: the chunk's own clock never trips

    const bool hit = !s_pfFound.empty();
    s_pfFound.clear();
    s_pfBusy = false;
    s_pfQuiet = false;
    s_pfBandHalf = 16.f;
    s_pmCells++;
    return hit;
}

// ---- the line for one seam -------------------------------------------------------------------
// A run of confirmed samples becomes one segment, widened by half a stride at each end (the
// sample proved a hull standing there, and the hull is wider than the stride) and clamped to the
// seam's real ends. Unconfirmed gaps split the run, so a seam that only rides at one end is
// marked at that end and nowhere else.
static void PmCloseRun(const PsMapSeam& s)
{
    if (s_pmRunLo < 0) return;
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    PsMap_RunSegment(s, s_pmRunLo, s_pmRunHi, s_pmSamples, s_pmStride, ax, ay, bx, by);
    const Vec3 a(ax, ay, s.z), b(bx, by, s.z);
    {
        std::lock_guard<std::mutex> l(g_psLock);
        s_psMapLines.push_back(a);
        s_psMapLines.push_back(b);
    }
    s_pmRunLo = s_pmRunHi = -1;
}

// ---- the driver: one budgeted slice per command ----------------------------------------------
static void PixelMapScan(void* pLocal, UserCmd_t* pCmd)
{
    if (s_pmStop.exchange(false)) PmAbort();

    if (s_pmReq.exchange(false))
    {
        PmAbort();
        if (s_pfBusy) PfCancelScan();      // a crosshair column is mid-scan: the button wins
        {
            std::lock_guard<std::mutex> l(g_psLock);
            s_psMapLines.clear();
            s_psMapTbPts.clear();
        }
        LARGE_INTEGER f{}, a{}, b{};
        QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        MV_ValidateOutsideWorld(EntV3(pLocal, g_offOrigin) + Vec3(0.f, 0.f, 40.f));
        PmBuildSeams(pLocal);
        QueryPerformanceCounter(&b);
        const double build = double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart);
        s_pmSeam = 0; s_pmSample = 0; s_pmSamples = 0;
        s_pmRunLo = s_pmRunHi = -1;
        s_pmCells = 0; s_pmNoWall = 0; s_pmHits = 0; s_pmVoid = 0; s_pmTb = 0; s_pmMs = 0.0;
        s_pmStartMs = GetTickCount64();
        s_pmDoneSeams = 0;
        s_pmTotalSeams = (int)s_pmSeams.size();
        s_pmBusy = !s_pmSeams.empty();
        DebugLog("PMAP: %s -- %d brush(es) -> %d seam(s) (%d skybox dropped) in %.1f ms, stride %.0f",
                 EngineLevelName(), s_pmBrushes, (int)s_pmSeams.size(), s_pmSky, build, s_pmStride);
        if (!s_pmBusy)
            DebugLog("PMAP: nothing to scan -- no brush wall faces found (GetBrushesInAABB "
                     "returned %d brushes)", s_pmBrushes);
    }

    if (!s_pmBusy) return;
    if (!g_psFinder) { PmAbort(); return; }   // the checkbox is an off switch, not a pause

    static LARGE_INTEGER s_liFreq = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER liStart{}, liNow{};
    QueryPerformanceCounter(&liStart);
    // the same share of the tick the crosshair scan takes, and for the same reason: the game
    // cannot make commands while we hold the movement thread
    const double flTickMs = (double)TickInterval() * 1000.0;
    const double flBudgetMs = std::min((double)std::clamp(g_psFinderBudget, 0.25f, 32.f),
                                       flTickMs > 0.0 ? flTickMs * 0.5 : 7.5);
    auto ElapsedMs = [&]() -> double
    {
        QueryPerformanceCounter(&liNow);
        return double(liNow.QuadPart - liStart.QuadPart) * 1000.0 / double(s_liFreq.QuadPart);
    };

    TraceFilterWorldProps filter; filter.Setup(pLocal);
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    MV_ValidateOutsideWorld(EntV3(pLocal, g_offOrigin) + Vec3(0.f, 0.f, 40.f));

    while (s_pmSeam < s_pmSeams.size())
    {
        const PsMapSeam& s = s_pmSeams[s_pmSeam];
        if (s_pmSamples <= 0)
        {
            s_pmSamples = PsMap_SampleCount(s.Len(), s_pmStride, k_pmMaxSamples);
            s_pmSample = 0;
            s_pmRunLo = s_pmRunHi = -1;
        }

        if (s_pmSample >= s_pmSamples)      // seam done: flush whatever it proved and step on
        {
            PmCloseRun(s);
            s_pmSeam++;
            s_pmSamples = 0;
            s_pmDoneSeams = (int)s_pmSeam;
            if (ElapsedMs() >= flBudgetMs) break;
            continue;
        }

        const float t = PsMap_SampleT(s_pmSample, s_pmSamples, s.Len());
        const Vec3 at(s.ax + (s.bx - s.ax) * t, s.ay + (s.by - s.ay) * t, s.z);
        s_pmSample++;

        bool hit = false;
        if (!PmSpotUsable(s, at))             s_pmVoid++;
        else if (!PmWallAbove(s, at, filter)) s_pmNoWall++;
        else
        {
            // the texturebug question is two traces and is asked of every spot the sim is asked
            // about: the two rides live on the same geometry and a seam is not a surf's rival.
            // Same checkbox the crosshair pass obeys -- a scan run for pixel surfs alone should
            // not come back with the map ringed in red
            // botox m_pixel_finder_map_texturebugs: own var, own default (false) -- this used to
            // reuse g_psFinderTb (the column scan's toggle, def true), so toggling the crosshair
            // finder's texturebug pass silently flipped the whole-map scan's pass too (PSPORT 11-04)
            if (g_psFinderMapTb) PmTbProbe(s, at, filter, vMins, vMaxs);
            hit = PmProbe(pLocal, pCmd, s, at);
        }

        if (hit)
        {
            s_pmHits++;
            if (s_pmRunLo < 0) s_pmRunLo = s_pmSample - 1;
            s_pmRunHi = s_pmSample - 1;
        }
        else if (s_pmRunLo >= 0)
        {
            PmCloseRun(s);                  // the run ended here: the gap is not a surf
        }

        if (ElapsedMs() >= flBudgetMs) break;
    }

    s_pmMs += ElapsedMs();

    if (s_pmSeam >= s_pmSeams.size())
    {
        const double wall = double(GetTickCount64() - s_pmStartMs) / 1000.0;
        DebugLog("PMAP: done -- %d seam(s), %d cell(s) simmed, %d skipped (no wall above), "
                 "%d skipped (in a brush / off the map), %d hit -> %d line(s) + %d texturebug(s) "
                 "| %.0f ms cpu over %.1f s, budget %.1f ms/tick",
                 (int)s_pmSeams.size(), s_pmCells, s_pmNoWall, s_pmVoid, s_pmHits, PmLineCount(),
                 PmTbCount(), s_pmMs, wall, flBudgetMs);
        s_pmBusy = false;
        s_pmSeams.clear();
        s_pmSeams.shrink_to_fit();
        s_pmDoneSeams = s_pmTotalSeams.load();
    }
}

// ---- menu side (main thread) -----------------------------------------------------------------
void PS_MapScanToggle()
{
    if (s_pmBusy) s_pmStop = true;
    else          s_pmReq = true;
}

void PS_MapScanGet(PsMapScanState& out)
{
    out.busy = s_pmBusy;
    const int total = s_pmTotalSeams.load(), done = s_pmDoneSeams.load();
    out.total = total;
    out.done = done;
    out.lines = PmLineCount();
    out.tb = PmTbCount();
    out.progress = total > 0 ? float(done) / float(total) : 0.f;
}
