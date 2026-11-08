#include "screenfx.h"
#include "fbbones.h"
#include "fbmath.h"            // the reprojection, the bone conversion, the shutter's power
#include "../visuals/visuals.h" // CachedCVar -- cl_wpn_sway_interp / scale for the sway model
#include "../visuals/viewmodel_fps.h" // g_vmFps.Stepped -- low fps viewmodel holds the gun's blur too
#include "../movement/prediction.h"   // g_globalVars (framecount, main thread)
#include "shaders/fx_fb_velocity.h"    // k_fxFbVelocityBytes
#include "shaders/fx_fb_tile_h.h"      // k_fxFbTileHBytes
#include "shaders/fx_fb_tile_v.h"      // k_fxFbTileVBytes
#include "shaders/fx_fb_neighbour.h"   // k_fxFbNeighbourBytes
#include "shaders/fx_fb_gather.h"      // k_fxFbGatherBytes
#include "shaders/fx_fb_trail.h"       // k_fxFbTrailBytes -- player / animation trail history

#include <atomic>
#include <math.h>
#include <string.h>
#include <vector>

// FRAME BLENDING -- the motion blur the engine never shipped.
// Ported from coldtires (hacks/visuals/screen/true_motion_blur.cpp).
//
// WHAT THE GAME HAS, and it is not this: mat_motion_blur_* is screen space with ONE blur
// vector for the whole frame, worked out from how far the camera turned. It cannot know what a
// pixel is, so a running player and the wall behind him smear the same way and walking forward
// does nothing but a radial fudge.
//
// We have a readable z buffer (screenfx.cpp), so the velocity the engine gave up on is two
// matrices away: unproject the depth, put the point back through LAST frame's camera, and the
// difference is where that pixel slid to. Over that runs McGuire et al. 2012 "A Reconstruction
// Filter for Plausible Motion Blur" with Guertin et al. 2014's feature-aware weighting:
//
//   1. velocity    one frame buffer: streak length in pixels, view depth, unit direction
//   2. tiles       the longest streak in every KxK block, then the longest in each block's 5x5
//                  neighbourhood -- a moving thing has to blur PAST ITS OWN EDGE, and a still
//                  pixel has no idea anything is coming. The tile is how it finds out.
//   3. gather      samples along both the tile's direction and its own, jittered, weighing
//                  every tap by depth, spread and direction
//
// THE SAMPLE COUNT IS THE STREAK, not a slider: one tap per pixel of streak, capped. A 2px
// streak taking 32 taps is 32 reads of the same texel; a 90px streak taking 8 is eight separate
// ghosts, which is what a fixed count gives at any speed worth blurring.
//
// THE EXPOSURE IS A SHUTTER TIME, not a per-frame amount: the one-frame displacement is divided
// by the frametime, so the picture is the same at 60 fps and at 400.
//
// ---- IT RUNS TWICE A FRAME, and the two halves see different frames ----------------------
//   world run   the game's own post slot. The scene is finished and DrawViewModels has not run
//               (viewrender.cpp:2160), so the frame holds the world and no gun: the camera
//               reprojection plus the players' own boxes, and the HUD is not in it either.
//   gun run     IVRenderView::ViewDrawFade, after the gun is drawn and before the HUD. The only
//               window where the viewmodel is in the frame and the HUD is not. The world half
//               is already finished and is left alone; the gun's pixels get their velocity from
//               the BONES (fbbones.h), so a reload, a swing or a deploy smears and your own
//               walking and turning do not.
//
// Splitting it that way is also what makes the fence cheap: in the world run the gun is not in
// the frame at all, and in the gun run nothing else is moving.

// ---- the tuning that is not a slider ---------------------------------------------------
// How far past his own hull a player is allowed to reach. The box the shader tests is the
// collision hull, which is what a player OCCUPIES and not what he looks like -- an arm out or a
// rocket launcher held across the chest is drawn well outside it. The trade is one way round: a
// few units too much catches the strip of wall a man stands against, a few too little leaves
// his own weapon sharp on a blurred body, and the second is the one the eye picks out.
static constexpr float k_playerMargin = 12.f;

// the floor stays nearly tight: a man's feet are on it and it is the one surface always inside
// his box, so widening downwards drags the ground he stands on along with him
static constexpr float k_playerFloor = 2.f;

// no player moves this far in a frame. A teleport, a respawn or a slot changing hands would
// otherwise draw a streak the length of the map through whoever inherited it.
static constexpr float k_playerTeleport = 256.f;

// TF2's standing hull (VEC_HULL_MIN/MAX). A crouched player is shorter, never wider, so the box
// is at worst a little tall -- which the margin already is on every other side.
static constexpr float k_hullMins[3] = { -24.f, -24.f, 0.f };
static constexpr float k_hullMaxs[3] = {  24.f,  24.f, 82.f };

static constexpr int k_maxPlayers    = 10;   // five constant registers each
static constexpr int k_playerSlots   = 65;   // by entity index
static constexpr int k_fbRegisters   = 130;  // c0..c129: ps_3_0 allows 224

// bones live at c66, four registers each (see fx_fb_velocity.hlsl)
static constexpr int k_boneBase   = 66;
static constexpr int k_playerBase = 16;

// ---- the frame the draw is handed -------------------------------------------------------
// Everything the passes need off a thread they will not be running on. The view setup is gone
// by then and the next frame's camera is already in the globals, so the whole reprojection is
// worked out on the main thread and arrives here as numbers.
struct FbFrame
{
    bool  valid = false;      // false = the history is a lie this frame (first frame, teleport, stall)
    int   frame = 0;

    float fov     = 90.f;
    float prevFov = 90.f;

    float fovVm   = 54.f;     // the gun's own projection, which is not the world's
    float zNearVm = 1.f;
    float zFarVm  = 1000.f;

    // rows of prev_view^T * cur_view, the eye's own movement in .w
    float reproj[3][4] = {};

    // the camera's axes as columns with the eye in .w: what puts a view-space point back into
    // the world, which is the only way to ask which player's box it is inside
    float viewToWorld[3][4] = {};

    // the camera itself, for the bone conversion on the draw side
    float basis[3][3] = {};
    float origin[3]   = {};

    // how many frames of travel the shutter stays open for: shutter / frametime
    float exposure = 0.f;

    // the same for the gun, uncapped by the world's amount -- its transforms are RUN for the
    // shutter rather than stretched, so the arc it draws is the arc the animation really took
    float gunExposure = 0.f;

    // botox extras: how much history survives (player trail = the world run), and the sway
    // shift every bone proxy gets in view space
    float trailBlend    = 0.f;
    float swayShift[3]  = {};

    // low fps viewmodel between steps: the gun is held in view space, so its blur is too --
    // the gun run replays the bone block it built on the last step frame
    bool  vmHold = false;

    int   playerCount = 0;
    float playerBox[k_maxPlayers][2][4] = {};
    float playerRows[k_maxPlayers][3][4] = {};
};

// Two slots and an index: the main thread writes one while the render thread reads the other.
// ponytail: two is enough because a torn read would be one frame of wrong velocity and nothing
// worse -- three slots if a stall ever laps the reader.
static FbFrame          s_slot[2];
static std::atomic<int> s_ready{ -1 };

// ---- main-thread history ----------------------------------------------------------------
static bool   s_havePrev   = false;
static double s_prevTime   = 0.0;
static float  s_prevFov    = 90.f;
static float  s_prevOrigin[3] = {};
static float  s_prevBasis[3][3] = {};

// where every player stood LAST frame, by entity index, and the frame it was written on. A slot
// whose frame is not the one before this one has no history to measure against: he was dormant,
// he just spawned, or he is a new owner of the slot.
static int   s_playerFrame[k_playerSlots] = {};
static float s_playerPose[k_playerSlots][3] = {};

static std::atomic<int> s_frameId{ 0 };

// the view angles over the last half second, for the sway model (main thread only)
static fbm::SwayHistory s_sway;
static double           s_swayBase = 0.0;   // float time loses ms precision past a day of uptime

// ---- device objects ----------------------------------------------------------------------
enum FbShader { FB_VELOCITY = 0, FB_TILE_H, FB_TILE_V, FB_NEIGHBOUR, FB_GATHER, FB_TRAIL, FB_SHADER_MAX };

static IDirect3DPixelShader9* s_ps[FB_SHADER_MAX] = {};

// the frame, copied out of the render target so a shader can read it. Keeps the target's format.
static IDirect3DTexture9* s_stageTex  = nullptr;
static IDirect3DSurface9* s_stageSurf = nullptr;

// msaa only: the plain surface the multisampled frame is resolved into on the way to the stage
static IDirect3DSurface9* s_resolveSurf = nullptr;

// the velocity buffer, frame sized
static IDirect3DTexture9* s_infoTex  = nullptr;
static IDirect3DSurface9* s_infoSurf = nullptr;

// the trails (botox accumulate pass): [0] world run history, [1] gun run history, each a
// ping-pong pair; s_trailCur is where the gather lands when a trail pass follows it
static IDirect3DTexture9* s_histTex[2][2]  = {};
static IDirect3DSurface9* s_histSurf[2][2] = {};
static int                s_histIdx[2]     = {};
static bool               s_histEmpty[2]   = { true, true };
static IDirect3DTexture9* s_trailCurTex    = nullptr;
static IDirect3DSurface9* s_trailCurSurf   = nullptr;

// the tile chain
static IDirect3DTexture9* s_tileRowTex  = nullptr;
static IDirect3DSurface9* s_tileRowSurf = nullptr;
static IDirect3DTexture9* s_tileTex     = nullptr;
static IDirect3DSurface9* s_tileSurf    = nullptr;
static IDirect3DTexture9* s_nbrTex      = nullptr;
static IDirect3DSurface9* s_nbrSurf     = nullptr;

static int  s_w = 0, s_h = 0;
static int  s_tileSize = 0, s_tileW = 0, s_tileH = 0;
static D3DFORMAT s_fmt = D3DFMT_UNKNOWN;
static D3DMULTISAMPLE_TYPE s_msaa = D3DMULTISAMPLE_NONE;
static bool s_shaderFailed = false;

// ==== main thread =========================================================================
void FrameBlend_NewFrame()
{
    s_frameId.fetch_add(1, std::memory_order_relaxed);
}

void FrameBlend_Reset()
{
    // the camera the reprojection is measured against belongs to a frame that no longer exists
    s_havePrev = false;
    s_ready.store(-1, std::memory_order_release);
    FbBones::Flush();
    // and so is every trail: a new map's first frame must not fade in from the old one
    s_histEmpty[0] = s_histEmpty[1] = true;
}

__declspec(noinline) static bool FbReadOrigin(const void* ent, float out[3])
{
    __try {
        const float* o = reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(ent) + (size_t)g_offOrigin);
        out[0] = o[0]; out[1] = o[1]; out[2] = o[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// THE OTHER PLAYERS. The camera reprojection only knows what the EYE did, so a man running
// across a still screen comes out with a velocity of zero and stays sharp while everything else
// smears. The textbook fix is a velocity target -- every model redrawn with last frame's bones
// -- and this engine has no shader that writes one, so it is done the other way round: the
// depth buffer already says where every pixel is in the WORLD, and a player is a box in it.
//
// It is RIGID, which is the same trade the gun's entity pose was: a man walking, strafing,
// falling or being pushed blurs, and a leg swinging inside a man standing still does not. His
// TURN is deliberately not in it either -- his legs follow the animation state's feet yaw and
// his chest follows his eyes, so there is no one rotation that describes what the mesh did, and
// feeding either in draws a streak through a man standing still flicking his mouse.
static void FbCollectPlayers(FbFrame& f, const float basis[3][3], const float origin[3])
{
    fbm::ViewToWorld(basis, origin, f.viewToWorld);

    if (!g_fbPlayers || g_offOrigin <= 0) return;

    struct Entry {
        float distSq;
        float box[2][4];
        float rows[3][4];
    };

    Entry entries[k_playerSlots] = {};
    int   total = 0;

    const int local = GetLocalPlayerIndex();

    for (int idx = 1; idx < k_playerSlots; ++idx)
    {
        if (idx == local) continue;

        void* ent = GetClientEntity(idx);
        if (!ent) continue;

        float where[3];
        if (!FbReadOrigin(ent, where)) continue;

        // THE POSE IS REMEMBERED WHATEVER HAPPENS BELOW. A frame that cannot measure him -- he
        // was dormant last frame, the camera is not valid, he just spawned -- is still the frame
        // the NEXT one measures against, and skipping the write here is how a player stays sharp
        // forever.
        const bool havePrev = (s_playerFrame[idx] == f.frame - 1);

        const float was[3] = { s_playerPose[idx][0], s_playerPose[idx][1], s_playerPose[idx][2] };

        s_playerFrame[idx]   = f.frame;
        s_playerPose[idx][0] = where[0];
        s_playerPose[idx][1] = where[1];
        s_playerPose[idx][2] = where[2];

        if (!havePrev || !f.valid || total >= k_playerSlots) continue;

        const float travel[3] = { where[0] - was[0], where[1] - was[1], where[2] - was[2] };

        if (travel[0] * travel[0] + travel[1] * travel[1] + travel[2] * travel[2] >
            k_playerTeleport * k_playerTeleport) continue;

        // A dormant player's origin stops updating, so his travel is zero and his rows come out
        // equal to the camera's -- which is exactly what a wall gets. No dormancy test needed.
        Entry& e = entries[total++];

        // the eye's own step, less his: what the camera would have written, minus where he took
        // himself. A player standing still leaves this equal to the camera's reprojection, which
        // is the check that the two agree.
        const float shift[3] = { origin[0] - s_prevOrigin[0] - travel[0],
                                 origin[1] - s_prevOrigin[1] - travel[1],
                                 origin[2] - s_prevOrigin[2] - travel[2] };

        fbm::Reprojection(s_prevBasis, basis, shift, e.rows);

        for (int a = 0; a < 3; ++a) {
            e.box[0][a] = where[a] + k_hullMins[a] + ((a == 2) ? k_playerFloor : -k_playerMargin);
            e.box[1][a] = where[a] + k_hullMaxs[a] + k_playerMargin;
        }

        const float away[3] = { where[0] - origin[0], where[1] - origin[1], where[2] - origin[2] };
        e.distSq = away[0] * away[0] + away[1] * away[1] + away[2] * away[2];
    }

    if (total <= 0) return;

    // nearest first: those are the ones worth the ten slots when a whole server is on screen.
    // Insertion sort -- ten of thirty-two, once a frame.
    for (int i = 1; i < total; ++i) {
        Entry key = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].distSq > key.distSq) { entries[j + 1] = entries[j]; --j; }
        entries[j + 1] = key;
    }

    const int kept = (total < k_maxPlayers) ? total : k_maxPlayers;

    // AND THEY GO UP BACK TO FRONT. The shader cannot break out of an unrolled loop, so the LAST
    // box a pixel is inside is the one it takes -- write the nearest man last and an overlap
    // resolves onto him, which is whose pixels they are.
    for (int slot = 0; slot < kept; ++slot) {
        const Entry& e = entries[kept - 1 - slot];
        memcpy(f.playerBox[slot][0], e.box[0], sizeof(float) * 4);
        memcpy(f.playerBox[slot][1], e.box[1], sizeof(float) * 4);
        for (int r = 0; r < 3; ++r) memcpy(f.playerRows[slot][r], e.rows[r], sizeof(float) * 4);
    }

    f.playerCount = kept;
}

// Main thread, from the game's own post-processing slot: the view setup is alive here and the
// entity list belongs to this thread. The draw half only ever reads the numbers left behind.
void FrameBlend_UpdateView(const float origin[3], const float angles[3],
                           float fov, float fovVm, float zNearVm, float zFarVm)
{
    if (!g_fbEnable) {
        // what is left in the history is the other side of however long the feature was off,
        // and reading across that gap is a teleport
        s_havePrev = false;
        s_ready.store(-1, std::memory_order_release);
        s_sway.Clear();
        return;
    }

    FbFrame f{};

    f.frame   = s_frameId.load(std::memory_order_relaxed);
    f.fov     = fov;
    f.fovVm   = (fovVm > 1.f && fovVm < 179.f) ? fovVm : fov;
    f.zNearVm = (zNearVm > 0.f) ? zNearVm : 1.f;
    f.zFarVm  = (zFarVm > f.zNearVm) ? zFarVm : (f.zNearVm + 1000.f);

    // the view's axes in world space, in the order the shader works in: right, up, forward
    float basis[3][3];
    fbm::Basis(angles, basis);

    // REAL TIME, and off the performance counter: this is a per-FRAME record, and the tick
    // clock hands several frames the same stamp -- a speed divided by that span is either zero
    // or a spike.
    LARGE_INTEGER freq{}, ctr{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ctr);
    const double now = (freq.QuadPart > 0) ? (double)ctr.QuadPart / (double)freq.QuadPart : 0.0;

    const double elapsed = s_havePrev ? (now - s_prevTime) : 0.0;

    // sampled every frame, valid or not: the lag model looks back a tenth of a second
    if (s_sway.filled == 0) s_swayBase = now;
    s_sway.Sample(angles, (float)(now - s_swayBase));

    const float step[3] = { origin[0] - s_prevOrigin[0],
                            origin[1] - s_prevOrigin[1],
                            origin[2] - s_prevOrigin[2] };

    const float travelled = sqrtf(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);

    // a stall, a teleport, or a spectator hopping to another player: the reprojection would be
    // an invention and the whole frame would be rebuilt out of the wrong place. One frame with
    // no blur is the answer, and the camera below is still remembered for the next one.
    f.valid = s_havePrev && elapsed > 0.0 && elapsed < (1.0 / 15.0) && travelled < 128.f;

    // THIS RUNS ON AN INVALID FRAME TOO: it is the frame that WRITES the poses the next one
    // measures against, and it needs the camera as it is here and as it was last time -- so it
    // goes before either is overwritten at the bottom.
    FbCollectPlayers(f, basis, origin);

    if (f.valid)
    {
        f.prevFov = s_prevFov;

        // prev_view^T * cur_view, and prev_view^T * (eye_now - eye_then) in the fourth column
        fbm::Reprojection(s_prevBasis, basis, step, f.reproj);

        // THE SHUTTER, and why it is divided by the frametime: the reprojection measures how far
        // a point moved in ONE frame, and an exposure is a length of TIME. Eight frames of
        // travel is the stop -- past that one frame of colour is being stretched over more
        // screen than it has detail for.
        const float shutter = ((g_fbShutter < 0.f) ? 0.f : (g_fbShutter > 60.f ? 60.f : g_fbShutter)) * 0.001f;
        const float frames  = (float)(shutter / elapsed);
        const float capped  = (frames > 8.f) ? 8.f : frames;

        f.exposure    = ((g_fbAmount < 0.f) ? 0.f : g_fbAmount) * capped;
        f.gunExposure = capped;

        // botox player trail: history time constant off the shutter
        f.trailBlend    = fbm::TrailBlend((float)elapsed, shutter, g_fbObject);

        // botox viewmodel sway. TF2 ships cl_wpn_sway_interp 0 (tf_viewmodel.cpp:65, "timing
        // bug") so its gun never lags; 0 there falls back to TF2's own 0.1 / 5 so the slider
        // has a lag to smear. A value the user set wins.
        if (g_fbViewmodel && g_fbVmSway > 0.f && g_cvar) {
            static CachedCVar ccInterp, ccScale;
            ConVar* ci = ccInterp.Get("cl_wpn_sway_interp");
            ConVar* cs = ccScale.Get("cl_wpn_sway_scale");
            const float interp = (ci && ci->GetFloat() > 0.f) ? ci->GetFloat() : 0.1f;
            const float scale  = (ci && ci->GetFloat() > 0.f && cs) ? cs->GetFloat() : 5.f;
            float vel[3];
            if (fbm::SwayVelocity(s_sway, (float)(now - s_swayBase), interp, scale, vel))
                fbm::SwayShift(vel, shutter, g_fbVmSway, g_fbVmAmount, f.swayShift);
        }
    }

    memcpy(f.basis, basis, sizeof(basis));
    memcpy(f.origin, origin, sizeof(float) * 3);

    // OverrideView (world_fx.cpp) ran g_vmFps.Frame for this frame before the post slot
    f.vmHold = g_vmLowFps && g_globalVars && g_vmFps.OnFrame(g_globalVars->framecount) && !g_vmFps.Stepped();

    const int ready = s_ready.load(std::memory_order_relaxed);
    const int next  = (ready == 0) ? 1 : 0;
    s_slot[next] = f;
    s_ready.store(next, std::memory_order_release);

    s_havePrev   = true;
    s_prevTime   = now;
    s_prevFov    = fov;
    s_prevOrigin[0] = origin[0];
    s_prevOrigin[1] = origin[1];
    s_prevOrigin[2] = origin[2];
    memcpy(s_prevBasis, basis, sizeof(basis));
}

// Draw thread, from the DrawModelExecute detour. `weapon` is the caller's classification of the
// part: the c_ weapon attachment against the v_models / arms stack.
void FrameBlend_CollectBones(const void* pState, const void* pInfo, const void* pBones, bool weapon)
{
    if (!g_fbEnable || !g_fbViewmodel || s_shaderFailed) return;
    FbBones::Collect(pState, pInfo, pBones, weapon, s_frameId.load(std::memory_order_relaxed));
}


// ==== device objects ======================================================================
static void FbReleaseTarget(IDirect3DTexture9*& tex, IDirect3DSurface9*& surf)
{
    if (surf) { surf->Release(); surf = nullptr; }
    if (tex)  { tex->Release();  tex  = nullptr; }
}

void FrameBlend_Destroy()
{
    FbReleaseTarget(s_nbrTex, s_nbrSurf);
    FbReleaseTarget(s_tileTex, s_tileSurf);
    FbReleaseTarget(s_tileRowTex, s_tileRowSurf);
    FbReleaseTarget(s_infoTex, s_infoSurf);
    FbReleaseTarget(s_stageTex, s_stageSurf);
    for (int k = 0; k < 2; ++k) {
        for (int i = 0; i < 2; ++i) FbReleaseTarget(s_histTex[k][i], s_histSurf[k][i]);
        s_histIdx[k] = 0;
        s_histEmpty[k] = true;
    }
    FbReleaseTarget(s_trailCurTex, s_trailCurSurf);

    if (s_resolveSurf) { s_resolveSurf->Release(); s_resolveSurf = nullptr; }

    for (auto& ps : s_ps) { if (ps) { ps->Release(); ps = nullptr; } }

    s_w = s_h = 0;
    s_tileSize = s_tileW = s_tileH = 0;
    s_fmt  = D3DFMT_UNKNOWN;
    s_msaa = D3DMULTISAMPLE_NONE;

    // the camera the reprojection is measured against belongs to a device that is going away
    s_havePrev = false;
}

static IDirect3DPixelShader9* FbShaderFor(IDirect3DDevice9* dev, FbShader which)
{
    if (s_ps[which]) return s_ps[which];
    if (s_shaderFailed) return nullptr;

    const BYTE* bytes = nullptr;
    size_t size = 0;
    switch (which) {
        case FB_VELOCITY:  bytes = k_fxFbVelocityBytes;  size = sizeof(k_fxFbVelocityBytes);  break;
        case FB_TILE_H:    bytes = k_fxFbTileHBytes;     size = sizeof(k_fxFbTileHBytes);     break;
        case FB_TILE_V:    bytes = k_fxFbTileVBytes;     size = sizeof(k_fxFbTileVBytes);     break;
        case FB_NEIGHBOUR: bytes = k_fxFbNeighbourBytes; size = sizeof(k_fxFbNeighbourBytes); break;
        case FB_GATHER:    bytes = k_fxFbGatherBytes;    size = sizeof(k_fxFbGatherBytes);    break;
        default:           bytes = k_fxFbTrailBytes;     size = sizeof(k_fxFbTrailBytes);     break;
    }

    std::vector<DWORD> code((size + 3) / 4);
    memcpy(code.data(), bytes, size);

    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps[which])) || !s_ps[which]) {
        s_ps[which] = nullptr;
        // every pass is needed for a picture, so one failing takes the feature with it
        s_shaderFailed = true;
        DebugLog("frameblend: pixel shader %d refused by the card -- frame blending is off", (int)which);
        return nullptr;
    }
    DebugLog("frameblend: shader %d built", (int)which);
    return s_ps[which];
}

// the velocity and the tiles are signed and run to hundreds of pixels: nothing 8 bit holds it
static bool FbVectorTarget(IDirect3DDevice9* dev, UINT w, UINT h,
                           IDirect3DTexture9*& tex, IDirect3DSurface9*& surf)
{
    const D3DFORMAT fmts[2] = { D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F };

    for (D3DFORMAT f : fmts) {
        if (SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &tex, nullptr)) &&
            SUCCEEDED(tex->GetSurfaceLevel(0, &surf)))
            return true;

        FbReleaseTarget(tex, surf);
    }
    return false;
}

static bool FbBuild(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    const int width  = (int)desc.Width;
    const int height = (int)desc.Height;

    if (s_stageTex && s_w == width && s_h == height &&
        s_fmt == desc.Format && s_msaa == desc.MultiSampleType)
        return true;

    // the shaders have no size baked into them and survive a resize; the targets do not
    IDirect3DPixelShader9* kept[FB_SHADER_MAX] = {};
    for (int i = 0; i < FB_SHADER_MAX; ++i) { kept[i] = s_ps[i]; s_ps[i] = nullptr; }

    FrameBlend_Destroy();

    for (int i = 0; i < FB_SHADER_MAX; ++i) s_ps[i] = kept[i];

    // the stage keeps the render target's own format, or StretchRect refuses the copy
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                  desc.Format, D3DPOOL_DEFAULT, &s_stageTex, nullptr)) ||
        FAILED(s_stageTex->GetSurfaceLevel(0, &s_stageSurf)))
    {
        DebugLog("frameblend: stage target creation failed");
        FrameBlend_Destroy();
        return false;
    }

    // THE TILE EDGE, and with it the longest streak the filter can carry: the gather never looks
    // further than the neighbouring tiles, so anything past 2K would be found by nobody. ~54px
    // at 1080p, which is a tenth of the screen height of blur.
    const int tile = (height / 20 < 8) ? 8 : (height / 20);

    const UINT tileW = (UINT)((width  + tile - 1) / tile);
    const UINT tileH = (UINT)((height + tile - 1) / tile);

    if (!FbVectorTarget(dev, desc.Width, desc.Height, s_infoTex, s_infoSurf) ||
        !FbVectorTarget(dev, tileW, desc.Height, s_tileRowTex, s_tileRowSurf) ||
        !FbVectorTarget(dev, tileW, tileH, s_tileTex, s_tileSurf) ||
        !FbVectorTarget(dev, tileW, tileH, s_nbrTex, s_nbrSurf))
    {
        DebugLog("frameblend: no floating point render target -- velocity buffer unavailable");
        FrameBlend_Destroy();
        s_shaderFailed = true;
        return false;
    }

    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE &&
        FAILED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
                                       D3DMULTISAMPLE_NONE, 0, FALSE, &s_resolveSurf, nullptr)))
    {
        DebugLog("frameblend: msaa resolve target creation failed");
        FrameBlend_Destroy();
        return false;
    }

    s_w = width; s_h = height;
    s_tileSize = tile; s_tileW = (int)tileW; s_tileH = (int)tileH;
    s_fmt = desc.Format; s_msaa = desc.MultiSampleType;

    DebugLog("frameblend: built %dx%d msaa %d tile %d (%dx%d)",
             width, height, (int)desc.MultiSampleType, tile, s_tileW, s_tileH);
    return true;
}

// The trail targets, frame sized, plain A8R8G8B8: rgb the picture, alpha the gun mask the
// animation trail fades by. Freed with everything else in FrameBlend_Destroy.
static bool FbEnsureTrail(IDirect3DDevice9* dev, int which)
{
    const auto make = [&](IDirect3DTexture9*& tex, IDirect3DSurface9*& surf) {
        if (tex) return true;
        if (SUCCEEDED(dev->CreateTexture((UINT)s_w, (UINT)s_h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                         D3DPOOL_DEFAULT, &tex, nullptr)) &&
            SUCCEEDED(tex->GetSurfaceLevel(0, &surf)))
            return true;
        FbReleaseTarget(tex, surf);
        return false;
    };

    const bool ok = make(s_histTex[which][0], s_histSurf[which][0]) &&
                    make(s_histTex[which][1], s_histSurf[which][1]) &&
                    make(s_trailCurTex, s_trailCurSurf);
    if (!ok) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("frameblend: trail history targets failed -- no player / animation trail"); }
    }
    return ok;
}

// ---- the state this pass borrows ---------------------------------------------------------
// Saved and put back exactly, same list ssao.cpp keeps plus the vertex shader: d3d9 IGNORES the
// fvf while a vertex shader is bound and the game has one bound here, so a transformed quad
// would go through the world's vertex shader and never land on screen.
struct FbSaved
{
    IDirect3DSurface9*           depthStencil;
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex[3];
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT  streamOffset, streamStride;
    DWORD fvf;
    D3DVIEWPORT9 vp;
    DWORD rs[16];
    DWORD samp[3][6];
    float psc[k_fbRegisters][4];
};

static const D3DRENDERSTATETYPE k_fbRS[16] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_CULLMODE,
    D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE,
    // point size is only in this list because the RESZ depth resolve writes its trigger code
    // into it, and the resolve runs in the same hook a moment before this pass
    D3DRS_POINTSIZE, D3DRS_SEPARATEALPHABLENDENABLE,
};

static const D3DSAMPLERSTATETYPE k_fbSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void FbSave(IDirect3DDevice9* dev, FbSaved& s)
{
    if (FAILED(dev->GetDepthStencilSurface(&s.depthStencil))) s.depthStencil = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->GetRenderState(k_fbRS[i], &s.rs[i]);
    for (int u = 0; u < 3; ++u) {
        dev->GetTexture(u, &s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_fbSamp[i], &s.samp[u][i]);
    }
    dev->GetPixelShaderConstantF(0, s.psc[0], k_fbRegisters);
}

static void FbRestore(IDirect3DDevice9* dev, FbSaved& s)
{
    // CONSTANTS FIRST, then the shader -- the same order the passes use, and for the same
    // reason: the game's shader puts its own literals back into whichever registers it does not
    // read, and it can only do that if it is set last.
    dev->SetPixelShaderConstantF(0, s.psc[0], k_fbRegisters);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);

    // FVF FIRST, DECLARATION LAST, and the fvf only when there was one: SetFVF builds a
    // declaration of its own, and SetFVF(0) with a decl bound leaves none at all.
    if (s.fvf) dev->SetFVF(s.fvf);
    dev->SetVertexDeclaration(s.decl);

    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    for (int u = 0; u < 3; ++u) {
        dev->SetTexture(u, s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_fbSamp[i], s.samp[u][i]);
    }
    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_fbRS[i], s.rs[i]);
    dev->SetViewport(&s.vp);
    dev->SetDepthStencilSurface(s.depthStencil);

    if (s.depthStencil) s.depthStencil->Release();
    if (s.decl)         s.decl->Release();
    for (auto* t : s.tex) if (t) t->Release();
    if (s.vs)      s.vs->Release();
    if (s.ps)      s.ps->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

// ---- the bones, turned into what pass one reads ------------------------------------------
// Every proxy arrives as a world-to-world transform for the points attached to it, and what the
// shader wants is view space now -> view space then:
//
//   w  = eye + basis^T p                 this frame's view point, back in the world
//   w' = A w + a                         where that point was, under this bone
//   p' = basis (w' - eye)                and back into view space
//
// THROUGH THIS FRAME'S CAMERA AT BOTH ENDS. The ride is already out of the proxy (fbbones.h
// takes it off with the pose the bones were built from, on the same thread and the same frame),
// so both ends live in the gun as it is held right now and the only thing left is the
// animation. Last frame's camera cannot appear in a gun streak at all.
static int FbBuildBones(const FbFrame& f, float block[k_fbRegisters][4], float amount)
{
    const FbBones::Set set = FbBones::Read();

    // a set older than the frame before this one is a gun that stopped drawing: holstered, dead,
    // a demo skipping. ONE frame of slack, because a part that misses a draw (or lands the other
    // side of the material queue) would drop the gun's velocity to zero for that frame alone,
    // and a blur that switches off for one frame in twenty is the pop the eye reads as jitter.
    if (set.count <= 0 || set.frame < f.frame - 1) return 0;

    // LOW FPS VIEWMODEL, AFTER THE BLUR: the bones above are the LIVE gun (visuals.cpp collects
    // them before the hold swaps them), so a step frame blurs the real animation. Between steps
    // the gun's pixels sit still in view space and so does this block -- the blurred gun is what
    // gets held, not a stepped gun that streaks once per step. Render thread only.
    static float s_held[FbBones::kMaxProxies * 4][4] = {};
    static int   s_heldCount = 0;
    float (*bones)[4] = block + k_boneBase;
    if (f.vmHold && s_heldCount > 0) {
        memcpy(bones, s_held, sizeof(s_held));
        return s_heldCount;
    }

    // Everything reaching here is supposed to be one frame of ANIMATION, and one frame of
    // animation turns a bone a few degrees: a melee swing is half a turn in a sixth of a second,
    // five degrees a frame at 250fps. Thirty in a frame is not an animation -- it is an anim
    // reset, or two poses that were never a frame apart -- and run for eight shutters it is the
    // screen painted black. Past thirty the proxy is FLATTENED (kept, so the list never changes
    // shape); under it the shutter is capped PER BONE at whatever keeps the arc inside 45
    // degrees. A real animation at a real framerate reaches neither.
    constexpr float k_flatTurn = 0.5236f;   // thirty degrees in one frame
    constexpr float k_arcCap   = 0.7854f;   // and forty five across the whole shutter

    const int count = (set.count < FbBones::kMaxProxies) ? set.count : FbBones::kMaxProxies;

    for (int i = 0; i < count; ++i)
    {
        const FbBones::Proxy& p = set.proxy[i];

        float trace = p.rot[0][0] + p.rot[1][1] + p.rot[2][2];
        float cosT  = (trace - 1.f) * 0.5f;
        if (cosT < -1.f) cosT = -1.f;
        if (cosT >  1.f) cosT =  1.f;

        const bool impossible = acosf(cosT) > k_flatTurn;

        float rot[3][3] = {};
        float centerThen[3] = {};

        if (impossible) {
            for (int a = 0; a < 3; ++a) { rot[a][a] = 1.f; centerThen[a] = p.center[a]; }
        } else {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) rot[r][c] = p.rot[r][c];
                centerThen[r] = p.centerThen[r];
            }
        }

        float rows[3][4] = {};
        fbm::BoneRows(f.basis, f.origin, rot, p.center, centerThen, rows);

        // one frame of animation, run for the length of the shutter -- the arc, not a stretched
        // chord, and never further round than a right angle and a half: past that the arc has
        // left every pixel the frame actually drew and what it paints is an invention
        float spinTrace = rows[0][0] + rows[1][1] + rows[2][2];
        float spinCos   = (spinTrace - 1.f) * 0.5f;
        if (spinCos < -1.f) spinCos = -1.f;
        if (spinCos >  1.f) spinCos =  1.f;

        const float spin = acosf(spinCos);

        float boneExposure = f.gunExposure;
        if (spin > 1e-4f && k_arcCap / spin < boneExposure) boneExposure = k_arcCap / spin;

        fbm::PowerRows(rows, boneExposure);

        // viewmodel sway: the lag the ride division took out, put back by the slider's share
        // (botox true_motion_blur.cpp:1212 -- after the bone, onto its translation)
        for (int r = 0; r < 3; ++r) rows[r][3] += f.swayShift[r];

        // and the bone itself into view space, which is where the pixel measures its distance
        const float away[3] = { p.center[0] - f.origin[0],
                                p.center[1] - f.origin[1],
                                p.center[2] - f.origin[2] };

        const int base = k_boneBase + i * 4;

        for (int a = 0; a < 3; ++a)
            block[base][a] = f.basis[a][0] * away[0] + f.basis[a][1] * away[1] + f.basis[a][2] * away[2];

        for (int r = 0; r < 3; ++r) memcpy(block[base + 1 + r], rows[r], sizeof(float) * 4);
    }

    // the slots nobody filled are parked a mile down the lens, where 1/d^4 makes them worth
    // nothing -- ps_3_0 cannot index a constant register, so pass one walks all sixteen anyway
    for (int i = count; i < FbBones::kMaxProxies; ++i) {
        const int base = k_boneBase + i * 4;
        block[base][0] = block[base][1] = block[base][2] = 1e6f;
    }

    memcpy(s_held, bones, sizeof(s_held));
    s_heldCount = count;

    (void)amount;
    return count;
}

// ==== the draw ============================================================================
void FrameBlend_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                     const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                     const FxViewSetup& view, bool gunPass)
{
    if (!dev || !target || !depth || s_shaderFailed || !g_fbEnable) return;
    if (gunPass && !g_fbViewmodel) return;
    if (!ScreenFx_QuadReady(dev)) return;

    const int ready = s_ready.load(std::memory_order_acquire);
    if (ready < 0) return;

    const FbFrame f = s_slot[ready];
    // a frame with no history behind it is a cut: every trail starts over from it
    if (!f.valid) { s_histEmpty[0] = s_histEmpty[1] = true; return; }

    // nothing to smear this frame: the world run wants an exposure, the gun run wants bones.
    // The trail (botox accumulate) runs on its own -- it wants only its history.
    const float amount = (g_fbVmAmount < 0.f) ? 0.f : ((g_fbVmAmount > 64.f) ? 64.f : g_fbVmAmount);
    const int   hk     = gunPass ? 1 : 0;
    const float trail  = gunPass ? 0.f : f.trailBlend;   // gun run has no trail (animation trail removed)
    bool wantTrail = trail > 0.01f;
    bool blur      = gunPass ? (amount > 0.f && g_fbViewmodel) : (f.exposure > 0.f);
    if (!wantTrail) s_histEmpty[hk] = true;
    if (!blur && !wantTrail) return;

    if (!FbBuild(dev, desc)) return;

    IDirect3DPixelShader9* velocity  = FbShaderFor(dev, FB_VELOCITY);
    IDirect3DPixelShader9* tileH     = FbShaderFor(dev, FB_TILE_H);
    IDirect3DPixelShader9* tileV     = FbShaderFor(dev, FB_TILE_V);
    IDirect3DPixelShader9* neighbour = FbShaderFor(dev, FB_NEIGHBOUR);
    IDirect3DPixelShader9* gather    = FbShaderFor(dev, FB_GATHER);
    IDirect3DPixelShader9* trailPs   = wantTrail ? FbShaderFor(dev, FB_TRAIL) : nullptr;
    if (!velocity || !tileH || !tileV || !neighbour || !gather) return;

    // the history pair for this run + the gather's landing spot, built the first time a trail
    // is asked for: five frame-sized targets nobody pays for with the sliders at 0
    if (wantTrail && (!trailPs || !FbEnsureTrail(dev, hk))) wantTrail = false;
    if (!blur && !wantTrail) return;

    // full frame sized, or we are sampling something that was never the scene's depth
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("frameblend: depth target size does not match the frame"); }
        return;
    }

    // ---- the numbers every pass shares ---------------------------------------------------
    const float width  = (float)s_w;
    const float height = (float)s_h;

    // the view setup's fov is HORIZONTAL, so the vertical half angle comes off the aspect ratio
    // -- taken from the frame itself, not from the setup
    const float aspect = (height > 0.f) ? width / height : 1.777778f;

    const float fovClamp = (f.fov < 1.f) ? 1.f : (f.fov > 170.f ? 170.f : f.fov);
    const float tanX     = tanf(fovClamp * 0.5f * 3.14159265f / 180.f);
    const float tanY     = tanX / aspect;

    const float prevClamp = (f.prevFov < 1.f) ? 1.f : (f.prevFov > 170.f ? 170.f : f.prevFov);
    const float prevTanX  = tanf(prevClamp * 0.5f * 3.14159265f / 180.f);
    const float prevTanY  = prevTanX / aspect;

    const float vmClamp = (f.fovVm < 1.f) ? 1.f : (f.fovVm > 170.f ? 170.f : f.fovVm);
    const float vmTanX  = tanf(vmClamp * 0.5f * 3.14159265f / 180.f);
    const float vmTanY  = vmTanX / aspect;

    // the neighbourhood search only reaches two tiles, so a longer streak would be found by nobody
    const float ceilingPx = (float)(s_tileSize * 2);

    // a cap, not a count: the streak picks the samples, and this is where it stops paying for more
    const int taps = (g_fbSamples < 4) ? 4 : ((g_fbSamples > 64) ? 64 : g_fbSamples);

    // AND THE STREAK IS CAPPED TO WHAT THE TAPS CAN FILL. The gather asks for one sample per
    // pixel of streak and then clamps that count, so a streak past the count is walked with the
    // SAME number of samples spread further apart -- and a smear whose taps stand five pixels
    // apart is not a smear, it is that many separate copies of the thing.
    float maxLength = ((g_fbMaxStreak < 0.f) ? 0.f : g_fbMaxStreak) * height;
    const float lengthCeiling = (ceilingPx < (float)taps) ? ceilingPx : (float)taps;
    if (maxLength < 4.f) maxLength = 4.f;
    if (maxLength > lengthCeiling) maxLength = lengthCeiling;

    float block[k_fbRegisters][4] = {};

    block[0][0] = view.zNear;
    block[0][1] = view.zFar;
    block[0][2] = tanX;
    block[0][3] = tanY;

    block[1][0] = prevTanX;
    block[1][1] = prevTanY;
    block[1][2] = 0.1f;             // the raw depth range the engine squeezes the gun into
    block[1][3] = f.exposure;

    block[2][0] = (float)taps;
    block[2][1] = maxLength;
    block[2][2] = (float)s_tileSize;
    // the noise walks with the frame so two frames' taps do not land on top of each other
    block[2][3] = (float)(f.frame & 63);

    for (int r = 0; r < 3; ++r) memcpy(block[3 + r], f.reproj[r], sizeof(float) * 4);

    block[6][0] = width;
    block[6][1] = height;
    block[6][2] = 1.f / ((width  > 1.f) ? width  : 1.f);
    block[6][3] = 1.f / ((height > 1.f) ? height : 1.f);

    block[7][0] = (float)s_tileW;
    block[7][1] = (float)s_tileH;
    block[7][2] = 1.f / ((s_tileW > 0) ? (float)s_tileW : 1.f);
    block[7][3] = 1.f / ((s_tileH > 0) ? (float)s_tileH : 1.f);

    // How sharply the depth test separates a tap from this pixel, as a FRACTION of the distance.
    // 24 puts the whole span inside a 4% depth step, so every crease of a model cuts the streak
    // into pieces with a hard line between them; 12 is the same separation at a silhouette and a
    // ramp across the small steps rather than a cliff.
    block[8][0] = 12.f;

    // THE FENCE, at half the world's near plane: pass one parks every gun pixel under it and no
    // world pixel can ever linearise below it, so one compare tells the two apart.
    block[8][1] = ((view.zNear > 0.01f) ? view.zNear : 0.01f) * 0.5f;
    block[8][2] = gunPass ? 0.f : (float)f.playerCount;
    block[8][3] = gunPass ? 1.f : 0.f;

    for (int r = 0; r < 3; ++r) memcpy(block[9 + r], f.viewToWorld[r], sizeof(float) * 4);

    block[12][0] = (f.zNearVm > 0.01f) ? f.zNearVm : 0.01f;
    block[12][1] = (f.zFarVm > block[12][0] + 1.f) ? f.zFarVm : (block[12][0] + 1.f);
    block[12][2] = vmTanX;
    block[12][3] = vmTanY;

    int boneCount = 0;
    if (gunPass) boneCount = FbBuildBones(f, block, amount);

    block[13][0] = (float)boneCount;
    block[13][1] = amount;

    // THE GUN RUN'S ONE DIAGNOSTIC, once a second. The bones arrive from another thread through
    // another hook, so "the gun is not blurring" has three causes that look identical on screen:
    // no set at all (the DrawModelExecute hook never saw the viewmodel -- wrong classification,
    // or the draw took the glow/depth early-out), a set that keeps arriving more than one frame
    // late (the material queue is running the draws behind us, which reads as chop), or a live
    // set the shutter is simply not moving. age 0 or 1 every line is healthy.
    if (gunPass) {
        static ULONGLONG s_lastReport = 0;
        const ULONGLONG nowMs = GetTickCount64();
        if (nowMs - s_lastReport > 1000) {
            s_lastReport = nowMs;
            const FbBones::Set set = FbBones::Read();
            DebugLog("^FX: frame blending gun -- bones %d (weapon %d) age %d shutter %.2f amount %.2f",
                     set.count, set.weapon, set.count > 0 ? (f.frame - set.frame) : -1,
                     f.gunExposure, amount);
        }
    }

    // the gun run with no skeleton to hang anything on has nothing to smear (the trail still runs)
    if (gunPass && boneCount <= 0) blur = false;
    if (!blur && !wantTrail) return;

    // c14 = blur gamma, 1 / gamma (botox f0e average_bright, 2 = the old c*c / sqrt look),
    //       trail blend (0 on a history that holds nothing yet), -
    {
        const float gamma = (g_fbGamma < 1.f) ? 1.f : (g_fbGamma > 10.f ? 10.f : g_fbGamma);
        block[14][0] = gamma;
        block[14][1] = 1.f / gamma;
        block[14][2] = (wantTrail && !s_histEmpty[hk]) ? trail : 0.f;
    }

    if (!gunPass) {
        for (int i = 0; i < k_maxPlayers; ++i) {
            const int base = k_playerBase + i * 5;

            if (i >= f.playerCount) {
                // parked INSIDE OUT -- mins above maxs, which no point in the world is between
                for (int a = 0; a < 3; ++a) {
                    block[base][a]     =  1e9f;
                    block[base + 1][a] = -1e9f;
                }
                continue;
            }

            memcpy(block[base],     f.playerBox[i][0], sizeof(float) * 4);
            memcpy(block[base + 1], f.playerBox[i][1], sizeof(float) * 4);
            for (int r = 0; r < 3; ++r) memcpy(block[base + 2 + r], f.playerRows[i][r], sizeof(float) * 4);
        }
    }

    // ---- the passes ----------------------------------------------------------------------
    FbSaved st{};
    FbSave(dev, st);

    // msaa on: resolve the multisampled frame into the plain surface first, then copy that
    HRESULT blit = dev->StretchRect(target, nullptr, s_resolveSurf ? s_resolveSurf : s_stageSurf,
                                    nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(blit) && s_resolveSurf)
        blit = dev->StretchRect(s_resolveSurf, nullptr, s_stageSurf, nullptr, D3DTEXF_NONE);

    if (FAILED(blit)) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("frameblend: frame blit failed 0x%08lx", (unsigned long)blit); }
        FbRestore(dev, st);
        return;
    }

    dev->SetDepthStencilSurface(nullptr);
    ScreenFx_BindQuad(dev);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);

    // The colour buffer wants filtering -- the gather's taps land between texels -- but NOTHING
    // ELSE here may be: an INTZ cannot be filtered, and a velocity averaged across a silhouette
    // is a direction that belongs to neither side of it.
    for (DWORD u = 0; u < 3; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    }

    // THE BLOCK GOES UP FOR EVERY DRAW, and it is not a wasted upload: no pass reads every
    // register, and the compiler hands the ones a pass left out to its own literals (def c7 in
    // the velocity pass, def c14/c15 in it too) which the driver writes when the shader is set.
    // Uploading once would leave the passes after it reading somebody else's constants.
    //
    // AND IT GOES UP BEFORE SetPixelShader, never after. A def only ever lands in a register the
    // shader does NOT read as a uniform, so setting the shader last lets its literals win their
    // own slots while ours keep every slot it actually reads.
    const auto pass = [&](IDirect3DSurface9* surface, int w, int h, IDirect3DPixelShader9* shader)
    {
        const D3DVIEWPORT9 vp{ 0, 0, (DWORD)w, (DWORD)h, 0.f, 1.f };
        dev->SetRenderTarget(0, surface);
        dev->SetViewport(&vp);
        dev->SetPixelShaderConstantF(0, block[0], k_fbRegisters);
        dev->SetPixelShader(shader);
        ScreenFx_DrawQuad(dev, w, h);
    };

    const DWORD rgbOnly = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;

    if (blur) {
        // PASS ONE: the velocity buffer, off the depth
        dev->SetTexture(0, depth);
        pass(s_infoSurf, s_w, s_h, velocity);

        // PASSES TWO AND THREE: the longest streak in each tile, one axis at a time
        dev->SetTexture(0, s_infoTex);
        pass(s_tileRowSurf, s_tileW, s_h, tileH);

        dev->SetTexture(0, s_tileRowTex);
        pass(s_tileSurf, s_tileW, s_tileH, tileV);

        // PASS FOUR: and the longest of each tile's neighbours, which is how far a blur may reach out
        dev->SetTexture(0, s_tileTex);
        pass(s_nbrSurf, s_tileW, s_tileH, neighbour);

        // PASS FIVE: the reconstruction, straight onto the frame -- or into the trail's input when
        // a trail follows. Colour filtered, velocity never.
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

        // rgb only: the alpha channel is the engine's and other passes read it
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, wantTrail ? 0x0f : rgbOnly);

        dev->SetTexture(0, s_stageTex);
        dev->SetTexture(1, s_infoTex);
        dev->SetTexture(2, s_nbrTex);
        pass(wantTrail ? s_trailCurSurf : target, s_w, s_h, gather);
    }

    // PASS SIX (botox accumulate): last frame's history blended over this one -- the world run
    // reprojects it through the camera so only what moved on its own leaves a trail, the gun run
    // reads it in place because the gun is bolted to the lens. Drawn twice with the same inputs:
    // into the next history (rgba, alpha = the gun mask) and onto the frame (rgb).
    if (wantTrail) {
        const int next = 1 - s_histIdx[hk];

        dev->SetTexture(0, blur ? s_trailCurTex : s_stageTex);
        dev->SetTexture(1, s_histTex[hk][s_histIdx[hk]]);
        dev->SetTexture(2, depth);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);   // a reprojected read lands between texels
        dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);    // INTZ: never filtered
        dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
        pass(s_histSurf[hk][next], s_w, s_h, trailPs);

        dev->SetRenderState(D3DRS_COLORWRITEENABLE, rgbOnly);
        pass(target, s_w, s_h, trailPs);

        s_histIdx[hk]   = next;
        s_histEmpty[hk] = false;
    }

    FbRestore(dev, st);

    static bool s_logged[2] = { false, false };
    if (!s_logged[gunPass ? 1 : 0]) {
        s_logged[gunPass ? 1 : 0] = true;
        DebugLog("^FX: frame blending %s drew %dx%d tile %d exposure=%.2f gun=%.2f bones=%d "
                 "players=%d taps=%d max=%.0fpx blur=%d trail=%.2f gamma=%.2f sway=%.2f,%.2f,%.2f",
                 gunPass ? "gun" : "world", s_w, s_h, s_tileSize, f.exposure, f.gunExposure,
                 boneCount, f.playerCount, taps, maxLength, (int)blur, wantTrail ? trail : 0.f,
                 block[14][0], f.swayShift[0], f.swayShift[1], f.swayShift[2]);
    }
}
