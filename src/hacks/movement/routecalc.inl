// ============================================================================
//  ROUTE CALCULATOR -- every combination that completes the route, from TF2's own
//  movement code.
//
//  Place markers in order (ground, pixelsurf, edgebug, headbang, texturebug, pixeljump), press calculate, read every
//  press sequence that completes them: "walk off -> jumpbug -> pixelsurf (stand) -> longjump".
//  Moves are only the replicable ones, each one of dih's own binds: jump, minijump, longjump,
//  jumpbug, and off the start walk off / crouch walk off. You run the approach; this only answers
//  the vertical half, which is the half that decides everything at a pixel.
//
//  THE PHYSICS IS routesim.h, NOT THIS FILE. A z-only port of CTFGameMovement, same float/double
//  order as the engine, run analytically -- no Pred_Simulate per leg, so a whole DAG of routes is
//  searched and every combination is counted. ..16 flew presses one at a time through the
//  engine with zero horizontal speed and ranked them by a recorder; that could never see a
//  catch, never chain one leg's landing height into the next, and never list more than a guess.
//  Why the port can be trusted:
//    * tests/routesim_test.cpp replays the hops recorded on hightower (/16 ledgers): the
//      bhop launch ladder (-255.26 / -254.56 jump, -255.25 / -254.53 longjump) to 5e-5, longjump
//      caught from -254.53 7 of 7, missed from lower 0 of 13, jump 0 of 37 -- all exact.
//    * every solve flies jump / longjump / minijump / jump + jumpbug through Pred_Simulate beside
//      the port from where you stand (RcEngineCheck) and logs the worst |dz|. If the engine and the
//      port ever disagree the panel says so on its first line.
//  What it cannot know: horizontal. It assumes you are over a floor when you land on it, beside a
//  ledge while below its top, and at a bhop speed under 1.2x maxspeed, where
//  PreventBunnyJumping leaves a standing hop's vz alone (there is no 'hop speed' knob any more).
//
//  Read in C:\dih\debug.log: `RC: solve`, `RC: engine check`, `RC: marker`, `RC: layers`,
//  `RC: combo`.
//
//  WHAT YOU AIM AT NAMES THE TYPE -- there is no 'place as' combo:
//    * a grey pixel finder pin (or an assist target off one) under the crosshair = a PIXELSURF
//      marker, a red seam ring = a TEXTUREBUG marker. Those two exist nowhere else: no ring, no
//      seam marker, ever, and the pixel calculator's marker obeys the same rule (grey only).
//    * no ring in the window -> the FACE names the type, no combo and no bind: a floor face is a
//      ground marker, a ceiling face a headbang, a wall face is REFUSED with the normal in the
//      message instead of stored at a height you never aimed at. An edgebug is a ground marker
//      switched to edgebug in its own row in the menu (it is the same ledge, read differently).
//    * picking a point is an ANGLE to the ring drawn for it (rs::AimTanTo), with no "not past the
//      surface the ray hit" clause: a lip in front of the wall no longer eats the snap.
//
//
//    * the PIXEL CALCULATOR is its own feature (g_pixelCalc) with its own keys and its own
//      marker/panel toggles -- turning either feature off takes its points off the screen. One
//      press answers the pixel off a single jump AND with 1, 2, 3 hops on your own flat in front
//      of it (land and press again, or jumpbug off it): k_pcMaxHops, one solve per hop count.
//    * TEXTUREBUG markers (rs::PT_TEXTUREBUG): place one on a pixel finder seam (the red rings)
//      and the solve answers what the seam does to the route. Physics in routesim.h.
//    * the marker ray: a ring of rays around the crosshair so a handrail is hittable, a
//      MASK_SOLID retry when a player clip brush you are standing in makes the ray start solid,
//      and own-team buildings solid to it by network class instead of by collision group.
//    * a grounded start is read as STANDING even while you crouch -- see RcSolveBegin.
//    * the engine-check disagreement line no longer prints on the panel (still logged).
// ============================================================================

#include <string>
#include "routesim.h"
#include "rc_edge.h"     // rce::NearestEdge: an edgebug marker's z is its ledge's lip, not your click
#include "../visuals/lagres.h"      // LagRes_RenderOrigin: a player's drawn origin
#include "dist_calc.h"   // dc::MaxBlock: the distance calculator's lj/jb reach test (RcMaxBlock below)

// ---- markers --------------------------------------------------------------------------------
struct RcPoint
{
    Vec3 pos;
    int  type = rs::PT_GROUND;
    unsigned moves = rs::kMovesMarker; // ground: presses after landing on it + may it be jumpbugged
    bool enabled = true;
    bool snapped = false;             // came off a finder/assist point: its z is an engine pin
    bool stand = true, duck = true;   // arrival stances allowed (surf/edgebug/headbang/headbounce)
    bool player = false;              // placed on a player's head
    // headbounce only (rs::PT_HEADBOUNCE): true = flat ceiling (plays as headbang), false = seam
    // window. Classified at placement (RcAddPoint, RcHeadbounceCeiling), botox pf_hb_is_ceiling's
    // own job -- never a menu row, botox has none either
    bool hbCeiling = false;
    // pixeljump only (rs::PT_PIXELJUMP): true = the lip is a flush prop top, catches rs::kSkinUp
    // higher than the pin (botox pj_skin). Classified at placement (RcPixelJumpSkin)
    bool pjSkin = false;
    // EDGEBUG ROWS TAKE THE LIP'S Z (RcEdgeSnapPass, rc_edge.h). baseZ is the z you placed it at,
    // put back the moment the row is switched off edgebug. edgeTried stops a failed scan from
    // re-running its ~600 traces every command
    float baseZ = 0.f;
    bool  edgeZ = false, edgeTried = false;
    // the entity it sits on, as its full HANDLE (the serial catches a reused index),
    // this session only -- never saved. RC_RefreshLive keeps pos on its top:
    // xy = origin + entOff, z = hull top + 1/32. 0 = not pinned (or the pin ended)
    unsigned ent = 0;
    Vec3     entOff;
};

static std::mutex           g_rcLock;         // markers + the published answer (render thread reads)
static std::vector<RcPoint> s_rcPoints;
static char                 s_rcMap[128] = {};
static unsigned             s_rcGen = 0;      // bumped on every edit -- render/menu see changes

static constexpr int k_rcMaxMarkers   = 32;
static constexpr int k_rcSettleTicks  = 12;

// the published answer -- the panel, the console and the log all print this one block
static std::vector<std::string> s_rcLines;     // under g_rcLock
// popup row kind (botox popup_row_kind_t: combo/head/note/error) rides as an optional first byte
// of a row or of s_rcMessage. render.cpp RenderRouteOverlay reads + strips it (same 1/2/3 values);
// the log and console strip it. Untagged = combo row; an untagged message = note
#define RC_HEAD "\x01"
#define RC_NOTE "\x02"
#define RC_ERR  "\x03"
static const char* RcRowText(const std::string& s)
{
    return s.c_str() + ((!s.empty() && (unsigned char)s[0] >= 1 && (unsigned char)s[0] <= 3) ? 1 : 0);
}
// the drawn path of the first printed combo: the world spots the legs run between, and the port's
// feet height after every cmd of each leg (rs::Solver::TraceBest). Both under g_rcLock
static std::vector<Vec3> s_rcAnchors;                    // anchors[i] -> anchors[i+1] is leg i
static std::vector<Vec3> s_rcPath;                       // the polyline the overlay draws
static std::string s_rcMessage;                // why there is no answer (empty = none)
static float s_rcProgress = 0.f;
static bool  s_rcBusy = false;

// PIXEL CALCULATOR: one pixelsurf marker apart from the route, solved from where you stand
// (the route solver with a one-point route). Session only, dropped on map change
static RcPoint s_pcPoint;                      // under g_rcLock
static bool    s_pcHave = false;
static bool    s_rcPixelSolve = false;         // the running / last solve is the pixel one

// ---- PIXEL CALCULATOR: HOPS ON THE FLAT YOU STAND ON ----------------------------------------
// One press off the floor is not the only way to a pixel. Landing back on your own flat and
// pressing again is a different arc every time -- a ducked landing launches from a ducked hull, a
// jumpbug leaves the floor 20-22u up instead of touching it -- so pixels no single jump can reach
// are reachable two or three presses in. The solver answers a ROUTE, so pass k simply puts k
// ground markers at the floor you are standing on in front of the pixel: pass 0 is the single
// jump, pass 1 is "press, land on your flat, press again", and so on. One pass per solve, run back
// to back off one key press, each pass's lines stacked under the ones before it.
static constexpr int k_pcMaxHops = 3;
static int  s_pcHops = 0;                    // ground markers in front of the pixel this pass
static int  s_pcNextHops = -1;               // pass to start next (-1 = none queued)
static bool s_pcHopStart = false;            // the start was grounded: hop passes mean something
static bool s_pcPathSet = false;             // the drawn arc belongs to the first pass that answered
static bool s_pcAnyCombo = false;            // any pass found anything (else the last one explains)
static std::vector<std::string> s_pcStack;   // lines of the finished passes (under g_rcLock)

// "pixel calculator from a building top" (ref's building calculator, PcBuilding/k_pcBld/
// s_pcBld/s_pcNextBld/PcNextBuilding) removed: no buildings in CSS (TF2 teleporter/dispenser/
// sentry only).

// ---- markers are session only -------------------------------------------------------
// No marker file: a new map, or a new game, starts empty. Marker z is measured off THIS session's
// world (pixel finder / snapped hits), so a reloaded one was a stale number pretending to be exact
static void RcResetForMap(const char* map)
{
    s_pcNextHops = -1;      // a queued hop pass belongs to the map it was pressed on
    std::lock_guard<std::mutex> l(g_rcLock);
    s_rcPoints.clear();
    s_pcHave = false;
    s_pcStack.clear();
    s_rcLines.clear(); s_rcPath.clear();
    s_rcMessage.clear();
    s_rcGen++;
    snprintf(s_rcMap, sizeof(s_rcMap), "%s", map ? map : "");
}

// ---- the engine clock, advanced one tick at a time ------------------------------------------
// DuckOverrides reads curtime against m_flDuckTimer, so a frozen clock would strip every duck.
// Pred_Simulate walks curtime itself; this owns tickcount and re-bases m_nTickBase on a hull that
// was teleported (see [[reference-pred-frozen-clock]]).
static int   s_rcTickBase = 0;
static float s_rcSaveCurtime = 0.f, s_rcSaveFrametime = 0.f;
static int   s_rcSaveTick = 0;

static void RcClockGrab()
{
    if (!g_globalVars) return;
    s_rcSaveTick      = g_globalVars->tickcount;
    s_rcSaveCurtime   = g_globalVars->curtime;
    s_rcSaveFrametime = g_globalVars->frametime;
}
static void RcClockRelease()
{
    if (!g_globalVars) return;
    g_globalVars->tickcount = s_rcSaveTick;
    g_globalVars->curtime   = s_rcSaveCurtime;
    g_globalVars->frametime = s_rcSaveFrametime;
}
static void RcSimTick(void* pLocal, UserCmd_t* pCmd)
{
    if (g_offTickBase > 0)
        EntSetI(pLocal, g_offTickBase, s_rcTickBase);
    if (g_globalVars)
    {
        g_globalVars->tickcount = s_rcTickBase;
        g_globalVars->curtime   = TickInterval() * float(s_rcTickBase);
        g_globalVars->frametime = TickInterval();
    }
    Pred_Simulate(pLocal, pCmd);
    s_rcTickBase++;
}

// ---- put the hull on a spot and let the engine settle it ------------------------------------
// FL_ONGROUND comes back only if m_hGroundEntity changed too ([[reference-ground-flag-teleport]]),
// so both are cleared.
struct RcStand { bool ground = false; float z = 0.f; };

// THE ORIGIN OF A HULL STANDING STILL IS NOT THE FLOOR. CSS WalkMove returns at spd < 1 before
// StayOnGround, so a landing CategorizePosition grounded inside its 2u trace keeps that hover,
// 0..2u over the floor, for as long as you do not move. Log 11-08: stand -63.969, floor -64.969 ->
// every jump 1.0u off, every jumpbug out of its window. The floor = where the hull swept 2u down
// lands (TryPlayerMove's own endpos). Fallback the origin: a player under you (filter skips them)
static float RcFloorUnder(void* pLocal, const Vec3& o)
{
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(o, Vec3(o.x, o.y, o.z - 2.f), EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
              MASK_PLAYERSOLID, &filter, &tr);
    if (tr.startsolid || tr.allsolid || tr.fraction >= 1.f || tr.plane.normal.z < 0.7f) return o.z;
    return tr.endpos.z;
}

static RcStand RcPlaceAt(void* pLocal, UserCmd_t* pCmd, const Vec3& pos)
{
    RcStand out;
    const Vec3 vAt = pos + Vec3(0.f, 0.f, 1.f);
    if (g_fnSetAbsOrigin) g_fnSetAbsOrigin(pLocal, vAt);
    EntSetV3(pLocal, g_offOrigin, vAt);
    EntSetV3(pLocal, g_offVelocity, Vec3());
    EntSetI(pLocal, g_offFlags, EntI(pLocal, g_offFlags) & ~FL_ONGROUND);
    if (g_offGroundEntity > 0) EntSetI(pLocal, g_offGroundEntity, -1);
    s_rcTickBase = g_offTickBase > 0 ? EntI(pLocal, g_offTickBase) : 0;

    const int   iBtn = pCmd->buttons;
    const float fwd = pCmd->forwardmove, side = pCmd->sidemove, up = pCmd->upmove;
    pCmd->buttons &= ~(IN_JUMP | IN_DUCK);
    pCmd->forwardmove = pCmd->sidemove = pCmd->upmove = 0.f;
    for (int i = 0; i < k_rcSettleTicks; i++)
    {
        RcSimTick(pLocal, pCmd);
        out.z = EntV3(pLocal, g_offOrigin).z;
        if (EntI(pLocal, g_offFlags) & FL_ONGROUND) { out.ground = true; break; }
    }
    // the landing tick hovers and a still hull never snaps (RcFloorUnder): z = the floor under it
    if (out.ground) out.z = RcFloorUnder(pLocal, EntV3(pLocal, g_offOrigin));
    pCmd->buttons = iBtn; pCmd->forwardmove = fwd; pCmd->sidemove = side; pCmd->upmove = up;
    return out;
}

// ---- the live hull as a port state -----------------------------------------------------------
// One reader, shared with AutoJumpbug (movement.cpp MV_ReadSimState).
static rs::State RcReadState(void* pLocal, int tickNow) { return MV_ReadSimState(pLocal, tickNow); }

// what an entity index is as a floor: 1 a player, 2 an engineer building, 0 anything else.
// Buildings by network class (IClientEntityList slot 0 GetClientNetworkable, IClientNetworkable
// slot 2 GetClientClass -- the walk skins.cpp FindPlayerResource uses)
// CS has no buildable objects: CObjectSentrygun/Dispenser/Teleporter never exist on a CS entity
// list, so kind 2 is now permanently dead (every "building" branch below is unreachable, left in
// place rather than threaded out of every call site under this pass -- see report, N/A for CS).
static int RcFloorKind(int idx)
{
    if (idx >= 1 && idx <= 64) return GetClientEntity(idx) ? 1 : 0;
    if (idx <= 64 || idx >= 2048 || !g_entityList) return 0;
    __try {
        void* pNet = VCall<void*>(g_entityList, 0, idx);
        if (!pNet) return 0;
        const ClientClass* cc = VCall<ClientClass*>(pNet, 2);
        if (!cc || !cc->m_pNetworkName) return 0;
        const char* n = cc->m_pNetworkName;
        return (!strcmp(n, "CObjectSentrygun") || !strcmp(n, "CObjectDispenser")
             || !strcmp(n, "CObjectTeleporter")) ? 2 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// an entity's full handle (serial << 16 | index, const.h:81); 0xFFFFFFFF none/unreadable
static unsigned RcEntHandleOf(void* pEnt)
{
    if (!pEnt) return 0xFFFFFFFFu;
    __try {
        const unsigned* pH = VCall<unsigned*>(pEnt, 2);        // IHandleEntity::GetRefEHandle
        return pH ? *pH : 0xFFFFFFFFu;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFFu; }
}
// the entity index a trace hit; -1 none/unreadable, 0 the world
static int RcEntIndexOf(void* pEnt)
{
    const unsigned h = RcEntHandleOf(pEnt);
    return h == 0xFFFFFFFFu ? -1 : (int)(h & 0xFFFF);
}
// same, for the entity a trace hit
static int RcFloorKindOf(void* pEnt)
{
    const int idx = RcEntIndexOf(pEnt);
    if (idx <= 0 || GetClientEntity(idx) != pEnt) return 0;
    return RcFloorKind(idx);
}

// ================================================================================================
//  THE MARKER RAY
// ================================================================================================
// YOUR OWN BUILDINGS. TraceFilterWorldProps runs the engine's team rule: an entity on your team
// is not solid unless its collision group is TFCOLLISION_GROUP_OBJECT_SOLIDTOPLAYERMOVEMENT
// (baseobject_shared.cpp:468). That is right for the sim and wrong for a marker -- it reads one
// netvar (m_CollisionGroup) and one enum value to decide, and everything it gets wrong it gets
// wrong silently: a teleporter and anything mid-carry sit in the other group, and an unresolved
// offset drops every friendly building at once. A marker only needs to know "is this a building",
// which the network class answers outright. So ask the base filter first, and give it one way to
// be overruled: own-team buildings stay solid to the ray you place markers with.
struct TraceFilterRcMarker : TraceFilterWorldProps
{
    static bool __fastcall sHit(TraceFilterRcMarker* self, void* pEnt, int mask)
    {
        if (TraceFilterWorldProps::sShouldHit(self, pEnt, mask)) return true;
        if (!pEnt || pEnt == self->pSkip) return false;
        return RcFloorKind(RcEntIndexOf(pEnt)) == 2;   // sentry / dispenser / teleporter, either team
    }
    static int __fastcall sType(TraceFilterRcMarker*) { return 3; }
    void Setup(void* skip)
    {
        TraceFilterWorldProps::Setup(skip);            // pSkip + nSkipTeam, then take the vtable over
        static void* vt[2] = { (void*)&sHit, (void*)&sType };
        vfptr = vt;
    }
};

// MASK_PLAYERSOLID without CONTENTS_PLAYERCLIP -- the retry below
static constexpr unsigned MASK_RC_NOCLIP = MASK_PLAYERSOLID & ~0x10000u;

// One ray, and the clip-brush rule. Maps wrap player clip brushes around railings, stair noses
// and ledges, and they are invisible: stand in one and the ray comes back startsolid, which is
// fraction 0, endpos = YOUR EYE and a zero normal -- the old code read that as a hit and dropped
// a pixelsurf marker on your own face. A clip you are standing inside cannot be the thing you are
// aiming at, so retry without it and let the ray reach the geometry you can see. A clip brush the
// ray runs INTO is left exactly as it was: it is a real floor/wall to the engine and the marker
// belongs on it, at plane + 1/32 like any other surface.
static bool RcRay(void* pLocal, const Vec3& vEye, const Vec3& vDir, GameTrace_t& out)
{
    TraceFilterRcMarker filter; filter.Setup(pLocal);
    TraceLine(vEye, vEye + vDir * 8192.f, MASK_PLAYERSOLID, &filter, &out);
    if (out.startsolid || out.allsolid)
        TraceLine(vEye, vEye + vDir * 8192.f, MASK_RC_NOCLIP, &filter, &out);
    return out.fraction < 1.f && !out.startsolid && !out.allsolid;
}

// THIN GEOMETRY. A single ray through the crosshair centre misses a handrail, a pipe or a fence
// post by a pixel and lands on the wall 400u behind it, which is a marker for a different height.
// A disk of rays a degree out from the centre grabs it (rce::FanOffset, rc_edge.h -- one ring of
// 8 sampled a circle and let rails fall through onto the floor under them): the NEAREST hit wins,
// because the thing in front is the thing you were aiming at. A flat wall answers every ray at the
// same distance (the off-axis ones are fractionally LONGER), so the fan can only take the marker
// off centre when something genuinely nearer is inside it -- and it has to be nearer by 2u to do it.
static constexpr float k_rcFanWin = 2.f;    // units a fan hit must beat the centre by

static bool RcAimWorld(void* pLocal, const Vec3& vEye, const Vec3& vAngles, GameTrace_t& out)
{
    Vec3 fwd, right, up;
    AngleVectors(vAngles, &fwd, &right, &up);
    const bool bCentre = RcRay(pLocal, vEye, fwd, out);
    const float flCentre = bCentre ? out.fraction * 8192.f : 1e9f;

    GameTrace_t nearest{};
    float flNear = 1e9f;
    for (int k = 0; k < rce::k_fanRays; k++)
    {
        float r, u;
        rce::FanOffset(k, r, u);
        Vec3 d = fwd + right * r + up * u;
        d.Normalize();
        GameTrace_t tr{};
        if (!RcRay(pLocal, vEye, d, tr)) continue;
        const float flDist = tr.fraction * 8192.f;
        if (flDist < flNear) { flNear = flDist; nearest = tr; }
    }
    // the margin is measured against the CENTRE only, so ring hits never bid against each other
    if (flNear < flCentre - (bCentre ? k_rcFanWin : 0.f)) out = nearest;
    return out.fraction < 1.f && !out.startsolid && !out.allsolid;
}

// botox rc_clip_top: A CLIP BRUSH YOU CANNOT SEE IS HIT ON ITS SIDE. r_drawclipbrushes only draws
// them, so aiming at "the ledge" puts the ray on the clip's invisible side face first: a wall, and
// the marker was refused. Step 1u inside that face and drop from k_rcClipClimb over the hit: a
// standable player clip top inside that height is the floor you would really land on. 82u = a
// standing hull: a clip taller than that is not a ledge you get onto, so the caller aims through it
static constexpr float k_rcClipClimb = 82.f;
// Same drop for a SOLID face (a handrail's side, a ledge's lip): the fan's nearest hit on a thin
// rail is its side far more often than its 2u top, and that was refused as a wall. A standable top
// within a step of the hit is the floor you meant; a taller face starts the drop inside the solid
// (startsolid) and stays a wall, so a real wall is still refused
static constexpr float k_rcLipClimb = 18.f;   // engine step height
static bool RcClipTop(void* pLocal, const GameTrace_t& tr, Vec3& top,
                      float climb = k_rcClipClimb, bool needClip = true)
{
    Vec3 n(tr.plane.normal.x, tr.plane.normal.y, 0.f);
    const float len = sqrtf(n.x * n.x + n.y * n.y);
    if (len < 0.1f) return false;
    n = n * (1.f / len);
    const Vec3 inside = tr.endpos - n;
    TraceFilterRcMarker filter; filter.Setup(pLocal);
    GameTrace_t down{};
    TraceLine(inside + Vec3(0.f, 0.f, climb), inside, MASK_PLAYERSOLID, &filter, &down);
    if (down.startsolid || down.fraction >= 1.f || down.plane.normal.z <= 0.7f
        || (needClip && !(down.contents & 0x10000)))
        return false;
    top = down.endpos;   // a line trace stops 1/32 over the plane: already the landing height
    return true;
}

// ================================================================================================
//  THE ENGINE CHECK -- the port and Pred_Simulate side by side, every solve
// ================================================================================================
// Every move off the floor you stand on, same buttons each tick into both, worst |dz| logged:
// jump, longjump, minijump, and a jump held into AutoJumpbug back onto the same floor (its duck,
// its unduck + jump if the fire cmd comes up, else the ducked landing).
// Also where the jump impulse is measured: mod_jump_height attributes and the surface jumpFactor
// are inside the engine's first launch tick and nowhere else.
struct RcCheck
{
    bool  ran = false;
    float worst = 0.f;
    const char* worstName = "";
    float floorZ = 0.f;
    std::string line;
};

static RcCheck RcEngineCheck(void* pLocal, UserCmd_t* pCmd, const Vec3& at, rs::Params& params)
{
    RcCheck out;
    const int   nBase  = PredCommandsPredicted();
    const int   iBtn   = pCmd->buttons;
    const float fwd = pCmd->forwardmove, side = pCmd->sidemove, up = pCmd->upmove;
    static const int   kScripts[] = { rs::SC_JUMP, rs::SC_LONG, rs::SC_MINI, rs::SC_JUMP };
    static const bool  kJumpbug[] = { false, false, false, true };
    static const char* kNames[]   = { "jump", "longjump", "minijump", "jump + jumpbug" };
    RcClockGrab();

    char buf[256];
    std::string parts;
    bool measured = false;
    for (int k = 0; k < 4; k++)
    {
        RestoreToFrame(nBase - 1);
        const RcStand st = RcPlaceAt(pLocal, pCmd, at);
        if (!st.ground) { out.line = "no floor under the check spot"; break; }
        out.floorZ = st.z;

        rs::State s = RcReadState(pLocal, s_rcTickBase);
        s.oldDuck = false; s.oldJump = false;          // the settle ticks sent no buttons
        rs::World w; w.floor = true; w.floorEff = st.z; w.still = true;   // fwd/side 0: no StayOnGround
        rs::ScriptMem mem;
        // the checked hull stands still: PreventBunnyJumping never fires on it, so no bhop crop
        rs::Params still = params; still.cropFrac = 1.f;
        float worst = 0.f; int worstTick = -1; int groundMiss = 0;
        for (int t = 0; t < 96; t++)
        {
            rs::Cmd c = rs::ScriptCmd(kScripts[k], t, s, mem, w, still);
            bool band = false; float margin = 0.f;
            if (kJumpbug[k] && rs::JumpbugCmd(s, w, still, c, band, margin)) mem.mjHold = false;
            pCmd->buttons = (iBtn & ~(IN_JUMP | IN_DUCK)) | (c.jump ? IN_JUMP : 0) | (c.duck ? IN_DUCK : 0);
            pCmd->forwardmove = pCmd->sidemove = pCmd->upmove = 0.f;
            RcSimTick(pLocal, pCmd);
            const float zEng  = EntV3(pLocal, g_offOrigin).z;
            const bool  gEng  = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;

            if (!measured && k == 0 && t == 0 && g_offVelocity > 0)
            {
                // standing ADD launch (cs_gamemovement.cpp:680-692): vz_end = (impulse - halfgrav) *
                // staminaRatio - 2 * halfgrav. Undo the stamina the sim already models off the live
                // m_flStamina (s.stamina, read before this tick) and what is left is the surface's
                // jumpFactor -- the only knob routesim.h cannot read for itself
                const float vzEng = EntV3(pLocal, g_offVelocity).z;
                const float half  = params.gravity * 0.5f * params.dt;
                const double ratio = s.stamina > 0.f ? rs::StaminaRatio(s.stamina) : 1.0;
                const float mul = (float)(((double)vzEng + 2.0 * half) / ratio + half);
                const float mod = mul / rs::kJumpMul;  // CS 301.993, NOT TF2's 289 (routesim.h)
                if (mod > 0.2f && mod < 4.f && fabsf(mod - params.jumpMod) > 0.001f)
                {
                    DebugLog("RC: engine check -- launch %.3f u/s (stamina %.0f ms, ratio %.4f), jump factor %.4f (not 1)",
                             mul, s.stamina, ratio, mod);
                    params.jumpMod = still.jumpMod = mod;
                }
                measured = true;
            }

            rs::TickInfo ti;
            rs::Tick(s, c, w, still, &ti);
            const float d = fabsf(zEng - s.z);
            if (d > worst) { worst = d; worstTick = t; }
            if (gEng != s.ground) groundMiss++;
            if (t > 2 && gEng && s.ground) break;
        }
        snprintf(buf, sizeof(buf), "%s%s %.4f%s", parts.empty() ? "" : ", ", kNames[k], worst,
                 groundMiss ? " (ground flag off)" : "");
        parts += buf;
        if (worst > out.worst) { out.worst = worst; out.worstName = kNames[k]; }
        if (worst > 0.01f)
            DebugLog("RC: engine check -- %s off by %.4fu at tick %d, ground flag mismatched on %d tick(s)",
                     kNames[k], worst, worstTick, groundMiss);
        out.ran = true;
    }
    RestoreToFrame(nBase - 1);
    RcClockRelease();
    pCmd->buttons = iBtn; pCmd->forwardmove = fwd; pCmd->sidemove = side; pCmd->upmove = up;
    if (out.ran)
    {
        snprintf(buf, sizeof(buf), "worst |dz| at z %.2f: ", out.floorZ);
        out.line = buf + parts;
    }
    return out;
}

// ================================================================================================
//  max_block -- the distance calculator's own question: longest edge-to-edge block (dz over the
//  takeoff) a longjump / jumpbug clears, perfect strafes. The real port is dc::MaxBlock
//  (dist_calc.h), kept pure/engine-free there so tests/dist_calc_test.cpp can run it with no
//  engine; this is just the live numbers CS:GO's n_route::max_block (route_engine.cpp ~line 2858)
//  read inline (cvars, tick interval), handed in as plain floats the same way routecalc.inl hands
//  rs::Params to the rest of routesim.h.
// ================================================================================================
static float RcMaxBlock(bool jumpbug, float dz, float maxSpeed)
{
    return dc::MaxBlock(jumpbug, dz, maxSpeed, TickInterval(), CvarFloat("sv_gravity", 800.f), 1.f,
                         CvarFloat("sv_airaccelerate", 10.f), CvarFloat("sv_air_max_wishspeed", 30.f));
}

// ================================================================================================
//  THE SOLVE -- runs on a worker thread. RcSolveBegin (game thread) reads every engine/entity/
//  convar value the solve needs into plain data (route/start/params/opt), then hands that snapshot
//  to the worker; from there the worker touches NOTHING but its own job (rs::Solver is a pure z-only
//  port, no traces, no convars -- see routesim.h's header comment). The job is reached by the game
//  thread only through atomics (gen/cancel/done/progress) until done is seen true, which is also
//  the happens-before edge for job->seconds and the solver's own Result -- the worker never writes
//  either again after that store. s_rcJob itself (the shared_ptr slot) is only ever touched by the
//  game thread; the worker has its own copy via the thread's captured shared_ptr.
// ================================================================================================
static constexpr double kRcHardLimitMs = 45000.0;   // worker-side ceiling (off the game thread, so
                                                     // generous -- matches the csgo-sdk route calc's
                                                     // own k_rc_time_limit_ms)
struct RcJob
{
    rs::Solver solver;
    int   gen = 0;
    std::atomic<bool>  cancel{false};
    std::atomic<bool>  done{false};
    std::atomic<float> progress{0.f};
    double seconds = 0.0;
};
static std::shared_ptr<RcJob> s_rcJob;
static int    s_rcSolveGen = 0;              // game thread only; bumped on every solve start/cancel
static std::vector<int>  s_rcMarkerNo;       // solver point index -> marker number
static std::vector<int>  s_rcMarkerType;
static RcCheck s_rcCheck;
static double s_rcMs = 0.0;
static int    s_rcDuckLeft = 0;              // ticks of m_flDuckTimer left when calculate was pressed

static void RcCon(int r, int g, int b, const std::string& text) { ConsoleColorPrint(r, g, b, text.c_str()); }

static void RcSolveCancel(const char* why)
{
    s_rcBusy = false;
    if (s_rcJob) s_rcJob->cancel.store(true, std::memory_order_relaxed);
    s_rcJob.reset();          // the worker thread keeps the job alive through its own shared_ptr
    s_rcSolveGen++;
    s_pcNextHops = -1;
    std::lock_guard<std::mutex> l(g_rcLock);
    s_pcStack.clear();
    if (why && *why) s_rcMessage = why;
    s_rcProgress = 0.f;
}

// bPixel: the pixel calculator -- route = the one pixel marker, start = where you are.
// nHops: pixel calculator only, ground markers at your own floor in front of it (see above)
static void RcSolveBegin(void* pLocal, UserCmd_t* pCmd, bool bPixel = false, int nHops = 0)
{
    s_rcMs = 0.0; s_rcDuckLeft = 0;
    s_pcHops = bPixel ? std::clamp(nHops, 0, k_pcMaxHops) : 0;
    s_pcNextHops = -1;
    const bool bFresh = s_pcHops == 0;   // a fresh press, not the next pass of one
    if (bFresh)
    {
        s_pcPathSet = false; s_pcAnyCombo = false;
        std::lock_guard<std::mutex> l(g_rcLock);
        s_pcStack.clear();
    }
    const bool bHere = bPixel || g_rcStartHere;
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        s_rcPixelSolve = bPixel;       // read by the render thread (which panel owns the answer)
        // a later hop pass keeps what the passes before it already answered, and their arc
        if (bFresh) { s_rcLines.clear(); s_rcPath.clear(); }
        s_rcMessage.clear();
        s_rcProgress = 0.f;
    }

    std::vector<RcPoint> pts;
    std::vector<int> markerNo;
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        if (bPixel)
        {
            if (s_pcHave) { pts.push_back(s_pcPoint); markerNo.push_back(1); }
        }
        else
            for (size_t i = 0; i < s_rcPoints.size(); i++)
            {
                if (!s_rcPoints[i].enabled) continue;
                pts.push_back(s_rcPoints[i]);
                markerNo.push_back((int)i + 1);
            }
    }
    if (bPixel && pts.empty())
    {
        RcSolveCancel("place the pixel marker first (pixel calculator -> place pixel)");
        return;
    }
    const int nNeed = bHere ? 1 : 2;
    if ((int)pts.size() < nNeed)
    {
        RcSolveCancel(bHere ? "add a marker to jump to" : "add a start marker and a marker to jump to");
        return;
    }
    if (!bHere && !rs::FloorLike(pts[0].type))
    {
        RcSolveCancel("marker 1 is the start: it has to be a floor (ground)");
        return;
    }

    // ---- params, read live (MV_SimParams = the engine knobs; these three are dih's own) ----
    rs::Params params = MV_SimParams(pLocal);
    params.pixelSurf   = g_pixelSurf;
    params.jbYieldSurf = g_jbYieldSurf;
    params.psHorizon   = std::clamp(g_pixelSurfPredictTicks, 1, 32);
    // botox reads its own delay hop ticks for both calculators (pixel_calc.cpp in.delay_ticks)
    params.delayTicks  = std::clamp(g_rcDelayTicks, 0, 8);
    // no 'hop speed' knob: every bhop in an answer is read as one under 1.2x class max speed
    // (params.cropFrac stays 1), which is where PreventBunnyJumping leaves a standing hop alone

    // ---- the start ----
    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);
    rs::Start start;
    Vec3 vCheckAt;
    bool bCheck = false;
    size_t firstPoint = 0;
    const bool bLiveGround = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    const Vec3 vLive = EntV3(pLocal, g_offOrigin);
    if (bHere && !bLiveGround)
    {
        // continue the live air path: the next simulated tick starts exactly here
        const int tb = g_offTickBase > 0 ? EntI(pLocal, g_offTickBase) : 0;
        start.air = true;
        start.hasFloor = false;
        start.s = RcReadState(pLocal, tb);
        start.s.oldJump = s_prevCmdJump;
        // dih's feature memory, live: who holds duck, PixelSurf's ride latch and ride detect
        start.s.hold = ((s_rawButtons & IN_DUCK) ? 1 : 0) | (s_psAirDuckHeld ? 2 : 0) | (s_mjHoldDuck ? 4 : 0);
        if (s_shouldPixelSurf && g_globalVars)
            start.s.psAge = std::clamp(g_globalVars->tickcount - (s_pixelSurfTicks - 16) - 1, -32, 16);
        start.s.psRiding = s_psRidingNow;
        start.s.psPrevSurf = s_prevSurfVel;
        // a lip's dot is ON the wall face: a hull settled there starts half in the wall, so a
        // lip is never the check spot
        for (const RcPoint& p : pts)
            if (rs::FloorLike(p.type) && p.type != rs::PT_PIXELJUMP) { vCheckAt = p.pos; bCheck = true; break; }
    }
    else
    {
        vCheckAt = bHere ? vLive : pts[0].pos;
        bCheck = bHere || pts[0].type != rs::PT_PIXELJUMP;
        if (bHere && g_offGroundEntity > 0)
        {
            // standing on someone: only the printed name changes ("jump off player")
            const unsigned h = (unsigned)EntI(pLocal, g_offGroundEntity);
            const int kind = h != 0xFFFFFFFFu ? RcFloorKind((int)(h & 0xFFFF)) : 0;
            start.player = kind == 1;
        }
        if (!bHere)
        {
            start.player = pts[0].player; firstPoint = 1;
        }
        // the starting jump is its own row (botox "starting jump" + m_route_calc_start_styles),
        // wherever it leaves from. The pixel calculator has no start row: every move is its
        if (!bPixel) start.moves = (unsigned)g_rcStartMoves & (rs::kMovesPress | rs::kMovesStart);
        // a lip as the start: hop off it (botox hop_only), nothing to walk off or wait on
        if (!bHere && pts[0].type == rs::PT_PIXELJUMP)
            start.moves &= rs::kMovesPress & ~(1u << rs::MB_DELAY);
    }

    // one Pred_Simulate flight from where you stand. Every hop pass of one pixel solve would
    // measure the same floor off the same spot, so it runs on the first pass and the rest keep it
    if (bFresh)
    {
        s_rcCheck = RcCheck();
        if (bCheck)
            s_rcCheck = RcEngineCheck(pLocal, pCmd, vCheckAt, params);
    }

    if (!start.air)
    {
        // a standing start is the settled floor: the check just measured it
        // where you stand hovers 0..2u over it standing still (RcFloorUnder): floor traced, the
        // hull keeps its live z -- a press with no step first launches from the hover, any step snaps
        float z = bHere ? RcFloorUnder(pLocal, vLive) : pts[0].pos.z;
        if (s_rcCheck.ran && fabsf(s_rcCheck.floorZ - z) < 4.f) z = s_rcCheck.floorZ;
        start.floorEff = z;
        if (bHere && vLive.z - z > 0.01f)
            DebugLog("RC: start hover %.4fu over floor %.4f (standing still, no StayOnGround)", vLive.z - z, z);
        if (bHere)
        {
            // where you stand, as the engine will start the next tick.
            // CROUCHED = STANDING for a grounded start. Holding duck cannot be part of
            // any answer here: every press the calculator offers begins with a jump, RunLeg's
            // Bunnyhop path stands the hull up first (movement.cpp:820) and only then presses, and
            // the 0.3s duck timer a release starts then eats a longjump/minijump duck -- so the
            // same spot answered differently depending on whether you happened to be crouching
            // when you hit the key. It is the same floor either way, so read it as the settled
            // standing hull. A MID-AIR start keeps its real duck state: there the stance is the
            // arc, not a key you can let go of.
            const int tb = g_offTickBase > 0 ? EntI(pLocal, g_offTickBase) : 0;
            start.s = RcReadState(pLocal, tb);
            start.s.z = std::clamp(vLive.z, z, z + 2.f); start.s.vz = 0.f; start.s.ground = true;
            start.s.oldJump = s_prevCmdJump;
            start.s.fDuck = start.s.ducking = start.s.ducked = false;
            start.s.duckTime = 0.f;
            start.s.oldDuck = false;
            start.s.airDucked = 0;
            start.s.hold = 0;
            // THE DUCK TIMER IS NOT A KEY YOU CAN LET GO OF. rc20 wiped it here along
            // with the crouch, and that is where "it says minijump but I just jump twice" comes
            // from: every duck RELEASE sets m_flDuckTimer = curtime + 0.3 (tf_gamemovement.cpp:3295)
            // and for those 20 ticks DuckOverrides (:3194) strips IN_DUCK off every grounded cmd.
            // A minijump IS jump + duck on one grounded cmd, so inside that window dih's MiniJump
            // refuses the duck (movement.cpp:1320) and the press comes out a plain jump -- you jump,
            // nothing mini happens, you press again: two jumps. Crouch-jumping onto anything
            // leaves the timer nearly always still running right after you land on it.
            // The hull is still read as STANDING (rc20's point, and right: a crouch is a key).
            // The timer is kept (RcReadState read it off m_flDuckTimer above), and the panel says
            // how long it has left.
            s_rcDuckLeft = std::max(0, params.duckTimerTicks - (start.s.tick - start.s.releaseTick));
        }
        else
            start.s = rs::RestState(z);
    }

    // ---- the markers, as the port reads them ----
    std::vector<rs::Point> route;
    s_rcMarkerNo.clear(); s_rcMarkerType.clear();
    // the pixel calculator's hops: k ground markers on the floor you are standing on, in front of
    // the pixel. Each one may be LANDED on and pressed off again, or JUMPBUGGED off (the floor is
    // never touched, the hull leaves it 20-22u up) -- both are arcs a single press cannot make.
    // Only off a grounded start: in the air there is no flat of yours to come back to
    s_pcHopStart = bPixel && !start.air && start.hasFloor;
    if (s_pcHopStart)
        for (int h = 0; h < s_pcHops; h++)
        {
            rs::Point g;
            g.type  = rs::PT_GROUND;
            g.z     = start.floorEff;
            g.moves = rs::kMovesMarker;      // press off it, or jumpbug off it
            route.push_back(g);
            s_rcMarkerNo.push_back(0);       // not one of yours: it is the floor under your feet
            s_rcMarkerType.push_back(rs::PT_GROUND);
        }
    for (size_t i = firstPoint; i < pts.size(); i++)
    {
        rs::Point p;
        p.type = pts[i].type;
        p.z = pts[i].pos.z;
        p.moves = pts[i].moves;
        p.allowStand = pts[i].stand;
        p.allowDuck = pts[i].duck;
        p.player = pts[i].player;
        p.hbCeiling = pts[i].hbCeiling;
        p.pjSkin = pts[i].pjSkin;
        route.push_back(p);
        s_rcMarkerNo.push_back(markerNo[i]);
        s_rcMarkerType.push_back(pts[i].type);
    }

    // where the drawn arc hangs: leg i runs from anchor i to anchor i + 1. Anchor 0 is where the
    // route leaves from -- you, or marker 1 when marker 1 is the start
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        if (bFresh) s_rcPath.clear();
        s_rcAnchors.clear();
        s_rcAnchors.push_back(bHere ? vLive : pts[0].pos);
        // a hop marker is your own feet: the arc draws the loop back down to them
        if (s_pcHopStart)
            for (int h = 0; h < s_pcHops; h++) s_rcAnchors.push_back(vLive);
        for (size_t i = firstPoint; i < pts.size(); i++) s_rcAnchors.push_back(pts[i].pos);
    }

    rs::Options opt;
    opt.moves = (unsigned)g_rcMoveMask & rs::kMovesAll;
    opt.topK = std::max(32, std::clamp(g_rcMaxShown, 1, 64));

    // everything the solve needs is plain data now (route/start/params/opt) -- the snapshot the
    // worker gets. Begin() itself is O(1) (one root node), so it stays here on the game thread
    // beside the engine reads above it; Step() is what runs for real, off-thread, below
    auto job = std::make_shared<RcJob>();
    job->gen = ++s_rcSolveGen;
    job->solver.Begin(route, start, params, opt);
    s_rcJob = job;
    s_rcBusy = true;

    DebugLog("RC: solve (dihrc32) -- %d marker(s), start %s z %.3f vz %.1f%s, moves 0x%X start 0x%X, "
             "delay hop %dt, gravity %.0f, tick %.4f, scale %.2f, jump mod %.4f, bhop crop %.4f",
             (int)route.size(), start.air ? "IN AIR" : (bHere ? "where you stand" : "marker 1"),
             start.s.z, start.s.vz, start.s.fDuck ? " ducked" : "", opt.moves, start.moves,
             params.delayTicks, params.gravity, params.dt, params.scale, params.jumpMod, params.cropFrac);
    for (size_t i = 0; i < route.size(); i++)
        DebugLog("RC: marker %d  %-11s z %.4f  moves 0x%X  stand %d duck %d%s%s%s", s_rcMarkerNo[i],
                 rs::PointTypeName(route[i].type), route[i].z, route[i].moves, (int)route[i].allowStand,
                 (int)route[i].allowDuck, route[i].player ? "  (player head)" : "",
                 route[i].type == rs::PT_HEADBOUNCE ? (route[i].hbCeiling ? "  (ceiling)" : "  (seam window)") : "",
                 route[i].type == rs::PT_PIXELJUMP && route[i].pjSkin ? "  (skin)" : "");
    if (s_rcCheck.ran)
        DebugLog("RC: engine check -- %s", s_rcCheck.line.c_str());
    else
        DebugLog("RC: engine check -- not run (%s)", s_rcCheck.line.empty() ? "no floor marker to stand on" : s_rcCheck.line.c_str());

    // the worker: no engine calls reach it (rs::Solver is pure), only this job's own atomics. It
    // never touches s_rcJob -- cancellation drops the game thread's reference, the thread's own
    // capture keeps the job alive until it exits
    // pinned (pinned_thread.h): an eject mid-solve unmaps once the solver returns
    HANDLE hWorker = CreatePinnedThread([job]
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        LARGE_INTEGER freq{}, t0{}, now{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        auto ElapsedMs = [&]() -> double
        {
            QueryPerformanceCounter(&now);
            return double(now.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart);
        };
        bool finished = false;
        try
        {
            while (!finished && !job->cancel.load(std::memory_order_relaxed) && ElapsedMs() < kRcHardLimitMs)
            {
                finished = job->solver.Step(64);
                job->progress.store(job->solver.Progress(), std::memory_order_relaxed);
            }
        }
        // bad_alloc (maxNodes/maxEdges still allow a big-but-finite heap) or anything else Step()
        // could throw must not reach the top of a detached thread -- that is std::terminate(), i.e.
        // takes the whole game down. Report it the same way a wall-clock timeout is reported instead
        catch (...) { finished = false; }
        if (!finished && !job->cancel.load(std::memory_order_relaxed)) job->solver.ForceTimeout();
        job->seconds = ElapsedMs() / 1000.0;
        // a cancelled job never reports done -- RcSolveCancel already dropped the game thread's
        // reference to it, so nothing would read the flag anyway, but DOES means never stale-finish
        if (!job->cancel.load(std::memory_order_relaxed)) job->done.store(true, std::memory_order_release);
    });
    if (hWorker) CloseHandle(hWorker);
}

// the wrap USED to live here, at 110 characters. A character count in a proportional
// font is not a width: rows came out wildly different lengths, the panel sized itself to whichever
// one happened to be widest, and the leftovers sat in a ragged half-empty column. The panel wraps
// its own rows now, in pixels, where the font is (render.cpp RenderRouteOverlay)

static std::string RcCount(double n)
{
    char b[48];
    if (n < 1e15) snprintf(b, sizeof(b), "%.0f", n);
    else          snprintf(b, sizeof(b), "%.3g", n);
    return b;
}

static void RcSolveFinish(RcJob& job)
{
    const rs::Result& r = job.solver.Out();
    std::vector<std::string> lines;
    // the "engine and calculator disagree (walls/ceiling near you?)" line is no longer
    // printed on the panel. It fires on any low ceiling over the check spot, which is most of the
    // places worth calculating, and it never told you anything the answer itself did not. The
    // check still RUNS and still logs every disagreement -- `RC: engine check` in C:\dih\debug.log
    const int shown = std::clamp(g_rcMaxShown, 1, 64);
    // a pixel pass past the first gets half the rows: the single jumps are the answer you asked
    // for, the hop chains are what to run when there are none of those
    const int rows = (s_rcPixelSolve && s_pcHops > 0) ? std::max(2, shown / 2) : shown;
    // the next pass of this press: hops 1..max off the real floor.
    // Only off a grounded start -- in the air there is no flat of yours to come back to
    int nNextHops = -1;
    if (s_rcPixelSolve && s_pcHopStart && s_pcHops < k_pcMaxHops) nNextHops = s_pcHops + 1;
    // the last pass of this press: the only one that explains a pixel nothing reached
    const bool bLast = nNextHops < 0;

    // THE PIXEL CALCULATOR'S PANEL IS THE COMBOS, NOTHING ELSE. Every row that is prose about
    // the answer rather than an answer goes to C:\dih\debug.log instead -- the diagnosis is not
    // lost, it is off the screen. The route calculator still prints all of it: its panel is read
    // at a desk, the pixel one is read mid-run.
    auto Explain = [&](const char* s, const char* kind = RC_NOTE)
    {
        if (s_rcPixelSolve) DebugLog("RC: %s", s);
        else                lines.push_back(std::string(kind) + s);
    };

    if (s_rcPixelSolve)
    {
        char b[128];
        // "pixel calculator" is already the panel's own title (render.cpp RenderRouteOverlay) and
        // the pixel's z is on the marker -- the heading said neither of them twice, so it is gone.
        // The hop count is NOT a heading: a combo under it needs those hops run first, so without
        // it the passes of one press blur into one wrong list. Kept, as short as it can be said.
        if (s_pcHops > 0)
        {
            snprintf(b, sizeof(b), RC_HEAD "+%d hop%s first:", s_pcHops, s_pcHops == 1 ? "" : "s");
            lines.push_back(b);
        }
    }
    if (r.total > 0.0)
    {
        for (int i = 0; i < (int)r.combos.size() && i < rows; i++)
            lines.push_back(r.combos[i].text);
        if (!s_rcPixelSolve && r.total > (double)std::min<size_t>(r.combos.size(), (size_t)rows))
            lines.push_back(RC_NOTE "(and +" + RcCount(r.total - (double)std::min<size_t>(r.combos.size(), (size_t)rows)) + " more)");
    }
    // the worker's own ceiling, not a dead marker: Finish() never ran, so there is no deadPoint to
    // name and saying there was one would blame the wrong marker
    else if (r.timedOut)
    {
        char b[160];
        snprintf(b, sizeof(b), "ran out of time (%ds) before any combination finished - remove a "
                                "marker or turn fewer move types on", (int)(kRcHardLimitMs / 1000.0));
        Explain(b, RC_ERR);
    }
    // a pixel pass that found nothing has already said so in its own heading, so only the last
    // pass of a press, and only when no pass of it found anything at all, spends rows on why
    else if (!s_rcPixelSolve || (bLast && !s_pcAnyCombo))
    {
        const int dp = r.deadPoint;
        char b[256];
        if (dp >= 0 && dp < (int)s_rcMarkerNo.size())
        {
            const int type = s_rcMarkerType[dp];
            if (s_rcPixelSolve)
                snprintf(b, sizeof(b), RC_ERR "no combination from where you stand hits the pixel");
            else
                snprintf(b, sizeof(b), RC_ERR "no combination reaches marker %d (%s)", s_rcMarkerNo[dp], rs::PointTypeName(type));
            lines.push_back(b);
            if (type == rs::PT_SURF && r.closest[dp] < 1e8f)
                snprintf(b, sizeof(b), "closest falling tick missed its window by %.3fu", r.closest[dp]);
            else if (r.highest[dp] < 0.f)
                snprintf(b, sizeof(b), "nothing gets %s it: best was %.1fu short",
                         (type == rs::PT_HEADBANG || type == rs::PT_TEXTUREBUG || type == rs::PT_HEADBOUNCE)
                             ? "the head to" : "above",
                         -r.highest[dp]);
            // A TEXTUREBUG HAS NO MISS DISTANCE TO PRINT, so "none arrive the way this marker
            // needs" said nothing at all about the one thing that decides it. The catch is an
            // AIRBORNE tick (rising or falling -- rs::Arrive PT_TEXTUREBUG asks for neither) with
            // the seam STRICTLY between the feet and the head and no stance change on that tick.
            // So the hull got past the seam without a tick ever straddling it, or it ducked or
            // unducked on the tick that would have
            else if (type == rs::PT_TEXTUREBUG)
                snprintf(b, sizeof(b), "the head gets %.1fu over it, but no airborne tick had the seam "
                                       "between your feet and your head (a duck or unduck on that "
                                       "tick cancels it too) - come past it slower",
                         r.highest[dp]);
            // a lip grounds only a hull that started rs::kPjCreepTicks ticks over it: the top of
            // an arc that only grazes it is not enough to creep in
            else if (type == rs::PT_PIXELJUMP)
                snprintf(b, sizeof(b), "arcs get %.1fu over the lip, but not for %d ticks - the hull has to "
                                       "stay over a pixeljump long enough to creep onto it",
                         r.highest[dp], rs::kPjCreepTicks);
            else
                snprintf(b, sizeof(b), "arcs get there (%.1fu over) but none arrive the way this marker needs",
                         r.highest[dp]);
            Explain(b);   // why it missed is a debug.log line for the pixel calculator
        }
        else
            lines.push_back(RC_ERR "no combination completes the route");
    }
    if (r.total > 0.0) s_pcAnyCombo = true;
    if (r.capped)
        Explain("search capped: a marker had too many distinct states, some combinations were not searched");
    // whatever the answer is, say when the clock was against it. Every duck release
    // sets m_flDuckTimer = curtime + 0.3 and DuckOverrides strips IN_DUCK off every grounded cmd
    // until it expires, so a minijump or a longjump pressed inside that window comes out as a
    // plain jump (dih's MiniJump refuses to send the duck at all, movement.cpp:1320). That is the
    // "it told me to minijump and I just jumped twice" case, and it is one line to see it.
    // One copy per press, not one per hop pass: it is the same clock for all of them
    if (s_rcDuckLeft > 0 && bLast)
    {
        char b[176];
        snprintf(b, sizeof(b), "duck timer: %d tick(s) left when you pressed - minijump/longjump off the "
                               "start are refused until it runs out (wait ~0.3s, calculate again)", s_rcDuckLeft);
        Explain(b);
    }

    // ---- the drawn path: fly the first printed combo again and keep every tick's height --------
    std::vector<Vec3> path;
    {
        std::vector<std::vector<float>> legZ;
        std::vector<std::vector<rs::ArcTick>> legArc;
        std::vector<Vec3> anchors;
        { std::lock_guard<std::mutex> l(g_rcLock); anchors = s_rcAnchors; }
        if (r.total > 0.0 && job.solver.TraceBest(legZ, &legArc) && legZ.size() + 1 == anchors.size()
            && legArc.size() == legZ.size())
        {
            path.push_back(anchors[0]);
            for (size_t i = 0; i < legZ.size(); i++)
            {
                const Vec3& a = anchors[i];
                const Vec3& b = anchors[i + 1];
                const size_t n = legZ[i].size();
                // the port answers height per cmd; the horizontal half is the player's, so the
                // ticks are spread along the straight line between the two markers. The SHAPE is
                // the real one -- every launch, duck, ride and drop of the answer above.
                // ONLY THE AIRBORNE TICKS CARRY THE DISTANCE. A leg can open with a
                // dozen grounded ticks (bunnyhop stand-up after a ducked landing, TIME_TO_DUCK
                // before a crouch walk off, a pinned seam ride) that move nobody. Spread over
                // every tick they ate a quarter of the distance standing still, which is what
                // sheared the arcs into straight slides joined by spikes
                size_t nAir = 0;
                float flNetDz = 0.f;
                for (size_t k = 0; k < n; k++)
                {
                    if (legArc[i][k].air) nAir++;
                    flNetDz += legArc[i][k].stanceDz;
                }
                const bool bAllMove = nAir == 0;         // nothing flew: fall back to even spread
                if (bAllMove) nAir = n;
                // THE STANCE TELEPORT IS NOT FLIGHT. Every air duck adds 8.5u to z in one tick
                // (CSS FinishDuck: 62u hull -> 45u around a fixed centre, cs_gamemovement.cpp:826)
                // and every air unduck takes it back, so a minijump's arc had a vertical cliff in it
                // and read as jagged. Take the teleports out as they happen, then put the leg's NET
                // shift back linearly along the leg: the curve is smooth, and it still starts and
                // ends on its markers (a leg that arrives ducked really does end 8.5u above where
                // it would standing).
                float flDz = 0.f;
                size_t moved = 0;
                for (size_t k = 0; k < n; k++)
                {
                    if (bAllMove || legArc[i][k].air) moved++;
                    flDz += legArc[i][k].stanceDz;
                    const float t = nAir ? (float)moved / (float)nAir : 1.f;
                    const float z = legZ[i][k] - flDz + flNetDz * t;
                    path.push_back(Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, z));
                }
                path.push_back(b);   // land exactly on the marker, whatever the tick grid did
            }
        }
    }

    // the pixel calculator's passes stack: each one's block goes under the ones already answered,
    // and the drawn arc belongs to the FIRST pass that had one (the easiest way to the pixel)
    std::vector<std::string> published = lines;
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        if (s_rcPixelSolve)
        {
            published.insert(published.begin(), s_pcStack.begin(), s_pcStack.end());
            s_pcStack = published;
        }
        s_rcLines = published;
        const bool bHavePath = !path.empty();
        if (!s_rcPixelSolve || !s_pcPathSet)
        {
            s_rcPath = std::move(path);
            if (bHavePath) s_pcPathSet = true;
        }
        s_rcProgress = 1.f;
    }
    s_rcBusy = false;
    // queue the next pass of this press (RouteCalc_Tick starts it: it has the live command)
    if (!bLast) s_pcNextHops = nNextHops;

    std::string layers;
    for (size_t i = 0; i < r.layerNodes.size(); i++) layers += " " + std::to_string(r.layerNodes[i]);
    DebugLog("RC: layers (states per marker):%s | %s combination(s), %lld sim ticks, %.1f ms off the "
             "game thread%s%s",
             layers.c_str(), RcCount(r.total).c_str(), r.ticks, s_rcMs, r.capped ? " | CAPPED" : "",
             r.timedOut ? " | TIMED OUT" : "");
    for (size_t i = 0; i < r.combos.size(); i++)
        DebugLog("RC: combo %2d  binds %d  switches %d  cost %2d  margin %s  %s", (int)i + 1,
                 r.combos[i].presses, r.combos[i].switches, r.combos[i].cost,
                 r.combos[i].margin > 1e8f ? "  -   " : (std::to_string(r.combos[i].margin).substr(0, 6)).c_str(),
                 r.combos[i].text.c_str());
    for (const std::string& s : lines) DebugLog("RC: %s", RcRowText(s));

    // one console block per PRESS: a pixel press runs several passes, and they print together
    if (!bLast) return;
    RcCon(180, 180, 180, "\n[ ");
    RcCon(120, 200, 255, "botox");
    RcCon(180, 180, 180, s_rcPixelSolve ? " pixel calculator ]\n" : " route calculator ]\n");
    for (const std::string& s : published)
    {
        const bool bad = !s.empty() && s[0] == RC_ERR[0];
        if (bad) RcCon(220, 120, 120, std::string(RcRowText(s)) + "\n");
        else     RcCon(120, 200, 255, std::string(RcRowText(s)) + "\n");
    }
    if (!s_rcPixelSolve) RcCon(180, 180, 180, RcCount(r.total) + " combination(s)\n\n");
    else                 RcCon(180, 180, 180, "\n");
}

// game thread, every tick: cheap. The solve itself is off on the worker now -- this only reads
// its atomics and, once, hands the finished job back to RcSolveFinish
static void RcSolvePoll()
{
    if (!s_rcJob) return;
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        s_rcProgress = s_rcJob->progress.load(std::memory_order_relaxed);
    }
    if (!s_rcJob->done.load(std::memory_order_acquire)) return;
    std::shared_ptr<RcJob> job = std::move(s_rcJob);
    if (job->gen != s_rcSolveGen)
    {
        // stale: the job was superseded without going through RcSolveCancel. Dropped, not shown
        DebugLog("RC: worker result dropped (gen %d != %d)", job->gen, s_rcSolveGen);
        return;
    }
    s_rcMs = job->seconds * 1000.0;
    RcSolveFinish(*job);
}

// ---- markers: place, delete, clear ------------------------------------------------------------
static bool s_rcAddHeld = false, s_rcDelHeld = false, s_rcClearHeld = false, s_rcSolveHeld = false;

// Where an entity is on screen. Players: the INTERPOLATED origin -- the m_vecOrigin netvar is
// m_vecNetworkOrigin (lagres.cpp:29), ~100ms ahead of the drawn body, so a running target's
// hull was tested where it is not drawn and the crosshair missed it.
static Vec3 RcEntOrigin(void* e, bool bPlayer, bool bResolve)
{
    Vec3 o;
    if (bPlayer && LagRes_RenderOrigin(e, o, bResolve)) return o;
    return EntV3(e, g_offOrigin);
}

// the crosshair ray against every other player's hull: a head is a floor you can bounce off.
// iHit = the player, oHit = the origin its hull was tested at (the pin offset is taken from the
// SAME origin -- resolution may succeed on a later player of the loop); bMate = the nearest
// hull on the ray was a teammate's
static bool RcAimPlayerHead(void* pLocal, const Vec3& vEye, const Vec3& vDir, float flMaxT, Vec3& vHit,
                            int& iHit, Vec3& oHit, bool& bMate)
{
    iHit = 0; bMate = false;
    if (g_offMins <= 0 || g_offMaxs <= 0) return false;
    float best = flMaxT;
    const int myTeam = g_offTeamNum > 0 ? EntI(pLocal, g_offTeamNum) : -1;
    int nResolve = 3;   // lagres resolution attempts this press (LagRes_RenderOrigin's own 16)
    for (int idx = 1; idx <= 64; idx++)
    {
        void* e = GetClientEntity(idx);
        if (!e || e == pLocal || !EntAlive(e)) continue;
        // teammates are not solid to you (CTFGameMovement::PlayerSolidMask only adds the enemy
        // team's contents bit): a head you can stand on is an enemy's. A teammate still takes
        // part in the ray so that aiming at one says so instead of doing nothing
        const bool mate = g_offTeamNum > 0 && EntI(e, g_offTeamNum) == myTeam;
        const Vec3 o = RcEntOrigin(e, true, nResolve-- > 0);
        const Vec3 mn = o + EntV3(e, g_offMins), mx = o + EntV3(e, g_offMaxs);
        if (mx.z - mn.z < 10.f) continue;
        float t0 = 0.f, t1 = best;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; a++)
        {
            const float s = vEye[a], d = vDir[a], lo = mn[a], hi = mx[a];
            if (fabsf(d) < 1e-6f) { if (s < lo || s > hi) miss = true; continue; }
            float ta = (lo - s) / d, tb = (hi - s) / d;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) miss = true;
        }
        if (miss || t0 >= best) continue;
        best = t0; bMate = mate; iHit = idx; oHit = o;
        vHit = vEye + vDir * t0;
        vHit.z = mx.z + rs::kDistEps;     // stood on, the feet stop 1/32 over the head
    }
    return iHit != 0 && !bMate;
}

// Keep entity markers on their entity: players walk and duck.
// Main thread (the vgui paint, once a frame -- same frame as the matrix the overlay draws
// with -- and every tick before the solver copies the route). The pin ENDS, marker left where
// it last was, when the target is gone, its slot went to someone else (handle serial), a
// player dies, or a player's head jumps >250u at once (respawned / teleported while dormant).
// A dormant target's origin is frozen, so its marker is too.
void RC_RefreshLive()
{
    if (!g_routeCalc || g_offOrigin <= 0 || g_offMaxs <= 0 || !IsInGame()) return;
    std::lock_guard<std::mutex> l(g_rcLock);
    for (RcPoint& p : s_rcPoints)
    {
        if (!p.ent) continue;
        const int idx = (int)(p.ent & 0xFFFF);
        void* e = GetClientEntity(idx);
        if (!e || RcEntHandleOf(e) != p.ent
            || (p.player ? !EntAlive(e) : RcFloorKind(idx) != 2)) { p.ent = 0; continue; }
        const Vec3 o = RcEntOrigin(e, p.player, false);
        const Vec3 np(o.x + p.entOff.x, o.y + p.entOff.y, o.z + EntV3(e, g_offMaxs).z + rs::kDistEps);
        if (p.player && np.DistTo(p.pos) > 250.f) { p.ent = 0; continue; }
        p.pos = np;
    }
}

// ---- PICKING A FINDER POINT ---------------------------------------------------------
// THE POINT YOU AIM AT IS THE MARKER TYPE. The finder draws two kinds of ring and they mean
// different things, so the ring under the crosshair already answers "what am I placing":
//   * a grey ring  (s_psFinderPts + the assist targets saved off them) -> PIXELSURF
//   * a red seam   (s_psTbPts / s_psMapTbPts, PfScanSeams + the map walk) -> TEXTUREBUG
//   * no ring at all -> not a seam marker, the surface you aimed at is (ground/edgebug/headbang)
// The rc24 rule this replaces was the other way round: you said the type first and only that set
// was searched. It worked but made the type a separate press per marker. The 2.5 deg window
// (rs::AimTanTo) is what keeps the two sets apart when both are on the same wall -- the ring you
// are actually centred on wins, and rc21's "nearest point of any kind by raw distance" is what
// made a 0.4u-away seam beat the grey ring you aimed at.
//   * a yellow lip  (s_pfLipPts, finder "find pixeljumps") -> PIXELJUMP
//   * an orange pin (s_pfJumpPts, finder "find jump surfs": a pin only a jump reaches) -> PIXELSURF
//     (the solver flies every arc anyway; it only had no way to be aimed at)
//   * an ascent pin (PF_GetExtraPts' headbounce list, g_psFinderHeadbounce) -> HEADBOUNCE, botox's
//     "red headbounce" (botox menu_tab_movement.cpp:174). hbCeiling is never asked for here or in
//     the menu (botox has no such row either): RcAddPoint classifies it itself, the same instant
//     the marker is placed, off a real trace straight up from the pin (RcHeadbounceCeiling, botox
//     pixel_finder.cpp pf_overhead / Point_bounce_ceil)
// bPixel keeps the pixel calculator on what it can answer: surfs and lips, no texturebug seam or
// headbounce ascent pin (botox pixel_calc.cpp never offers those two as a pixel-calc type either).
// Ranking is by how centred the crosshair is on the ring. Gone with rc21's rule: the "not past the
// surface the ray hit (+16u)" clause. It read a lip 200u in front of the wall as the end of the
// ray and refused the point you were looking at (report 4: "it places it nowhere near that
// point"). A ring is a thing you aim AT; what the ray bumps into on the way is not a vote.
// type out = rs::PT_SURF / PT_TEXTUREBUG / PT_PIXELJUMP; why = which ring, for the readout.
// pixelType (pixel calculator only, bPixel true): botox pixel_calc.cpp:114 snaps against ONE
// finder list, the one g_pcType picked (0 pixelsurf, 1 pixeljump) -- not "whichever ring is
// closer" like the route calculator's markers (every ring counts there; the FACE decides for
// everything else). Ignored when !bPixel.
static bool RcPickFinder(const Vec3& vEye, const Vec3& vFwd, bool bPixel, int pixelType,
                         Vec3& pos, int& type, const char*& why, int& nSeen)
{
    // PF_GetExtraPts takes g_psLock itself -- fetched BEFORE this function's own lock below,
    // same mutex, so taking it twice on one thread would deadlock
    std::vector<Vec3> hbJump, hbPts, hbLip;
    if (!bPixel) PF_GetExtraPts(hbJump, hbPts, hbLip);

    std::lock_guard<std::mutex> l(g_psLock);
    float flBest = 1e9f;
    const Vec3* pPick = nullptr;
    auto test = [&](const Vec3& p, int t, const char* w)
    {
        nSeen++;
        const float off = rs::AimTanTo(vEye, vFwd, p);
        if (off < 0.f || off >= flBest) return;
        flBest = off; pPick = &p; type = t; why = w;
    };
    nSeen = 0;
    const bool wantSurf = !bPixel || pixelType == 0;
    const bool wantJump = !bPixel || pixelType == 1;
    if (wantSurf)
    {
        for (const Vec3& p : s_psFinderPts)       test(p, rs::PT_SURF, "green pinned pixel");
        for (const PsAssistPt& p : s_psAssistPts) test(p.pos, rs::PT_SURF, "assist target");
        // only what is on screen: the overlay hides this list with its own checkbox
        if (g_psFinderJumpSurf) for (const Vec3& p : s_pfJumpPts) test(p, rs::PT_SURF, "orange jump surf");
    }
    if (wantJump && g_psFinderPixeljump) for (const Vec3& p : s_pfLipPts) test(p, rs::PT_PIXELJUMP, "yellow pixeljump lip");
    if (!bPixel)
    {
        for (const Vec3& p : s_psTbPts)    test(p, rs::PT_TEXTUREBUG, "cyan seam ring");
        for (const Vec3& p : s_psMapTbPts) test(p, rs::PT_TEXTUREBUG, "cyan seam ring");   // the whole-map walk's seams
        if (g_psFinderHeadbounce) for (const Vec3& p : hbPts) test(p, rs::PT_HEADBOUNCE, "ascent pin");
    }
    if (pPick) pos = *pPick;
    return pPick != nullptr;
}

// ---- HEADBOUNCE: CEILING OR SEAM WINDOW, CLASSIFIED OFF A REAL TRACE ---------------------------
// botox pixel_finder.cpp pf_overhead + Point_bounce_ceil: the finder's own ascent-catch scan
// classifies ceiling vs seam window AT SCAN TIME and remembers it per dot (pf_hb_is_ceiling).
// PSPORT's s_pfHeadbouncePts (psassist.inl TryHeadbounce) stores only the settled ORIGIN, with no
// per-dot classification -- rather than ask for one (and the duck state that found each dot, which
// the list does not carry either), this traces up from that same origin itself, right here, at
// placement: a close solid plane dead over the head IS a flat ceiling (headbang's own clip, botox
// pf_overhead's "only a face the hull touches"); nothing that close is the seam-window face.
// out: ceiling -> the real plane z (DIST_EPSILON off it, same convention headbang's own aim-trace
// placement already uses); seam -> originZ + a fixed standing-hull add (botox's own k_pf_hull is a
// single constant too, not duck-aware -- pixel_finder.cpp pf_hb: "head = got + k_pf_hull").
// UNVERIFIED css: the ray length / closeness tolerance below (botox's own k_pf_over_reach value
// was not read out of the binary) -- generous enough to never misclassify a real seam as a ceiling.
static bool RcHeadbounceCeiling(void* pLocal, const Vec3& originZeroed, bool& ceiling, float& outZ)
{
    ceiling = false;
    outZ = originZeroed.z + rs::StandH(rs::Params());
    if (g_offMins <= 0 || g_offMaxs <= 0) return false;
    const Vec3 mn = EntV3(pLocal, g_offMins), mx = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(originZeroed, originZeroed + Vec3(0.f, 0.f, 8.f), mn, mx, MASK_PLAYERSOLID, &filter, &tr);
    if (tr.startsolid || tr.allsolid || tr.fraction >= 1.f) return false;
    const float headZ = originZeroed.z + mx.z;
    if (tr.endpos.z - headZ > 0.5f) return false;      // not touching: a seam window, not a ceiling
    ceiling = true;
    outZ = tr.endpos.z;                                // the plane, DIST_EPSILON off (the trace's own stop)
    return true;
}

// ---- PIXELJUMP SKIN: a flush prop-top lip, not a real ledge ------------------------------------
// botox route_calc.cpp pj_lip_kind, 1:1 (hull_trace there is the local player's own current hull,
// same as RcHeadbounceCeiling above): walk out from the pin in 8 directions to find the wall face,
// then look for a standable top right at the pin (0, a real ledge) or 2u higher (1, a flush skin --
// the surface continues past the pin, which is where the actual catch settles, routesim.h kSkinUp).
// Neither -> -1, kept as a plain lip (botox's own default when the probe is inconclusive).
static int RcPixelJumpSkin(void* pLocal, const Vec3& at, float pz)
{
    if (g_offMins <= 0 || g_offMaxs <= 0) return -1;
    const Vec3 mn = EntV3(pLocal, g_offMins), mx = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    constexpr float kD = 0.70710678f;
    constexpr float dirs[8][2] = { {1.f,0.f}, {kD,kD}, {0.f,1.f}, {-kD,kD}, {-1.f,0.f}, {-kD,-kD}, {0.f,-1.f}, {kD,-kD} };
    const float top = pz - rs::kDistEps;
    const Vec3 base(at.x, at.y, pz + 1.f);
    for (const auto& d2 : dirs)
    {
        const Vec3 d(d2[0], d2[1], 0.f);
        const Vec3 o = base + d * (16.f * (fabsf(d.x) + fabsf(d.y)) + 8.f);
        GameTrace_t w{};
        TraceHull(o, o - d * 12.f, mn, mx, MASK_PLAYERSOLID, &filter, &w);
        if (w.startsolid || w.fraction >= 1.f || fabsf(w.plane.normal.z) > 0.7f) continue;
        Vec3 n(w.plane.normal.x, w.plane.normal.y, 0.f);
        n.Normalize();
        const float sup = 16.f * (fabsf(n.x) + fabsf(n.y));
        const float gap = (w.endpos - base).Dot(n) - sup;
        if (n.Dot(d) < 0.5f || fabsf(gap) > 0.1f) continue;
        const Vec3 q = base + n * (sup + 0.001f);
        const auto onLip = [&](const GameTrace_t& t)
        {
            return !t.startsolid && t.fraction < 1.f && t.plane.normal.z >= 0.7f && fabsf(t.endpos.z - pz) < 0.05f;
        };
        GameTrace_t t{};
        TraceHull(q, q - Vec3(0.f, 0.f, 2.f), mn, mx, MASK_PLAYERSOLID, &filter, &t);
        if (onLip(t)) return 0;
        const Vec3 s2(q.x, q.y, top + 2.015f);
        TraceHull(s2, s2 - Vec3(0.f, 0.f, 2.f), mn, mx, MASK_PLAYERSOLID, &filter, &t);
        return onLip(t) ? 1 : -1;
    }
    return -1;
}

// pixel calculator: the one pixel marker where you aim (replaces the last one). it is
// a PIXELSURF marker, so it obeys the same rule the route calculator's now does: the pixel finder's
// own points or nothing. A raw wall hit used to be accepted with a "snap it for an exact z" note,
// and that z was whatever height the crosshair happened to be at -- an answer to a question about
// a pixel that is not there. The finder's pin z is the only exact one, so it is the only one.
static void RcPlacePixel(void* pLocal, UserCmd_t* pCmd)
{
    if (g_offViewOffset <= 0) return;
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);

    // botox pixel_calc.cpp:201-202, 1:1: g_pcType picks pixelsurf or pixeljump, not the ring --
    // RcPickFinder's pixelType param restricts the snap to that one finder list
    RcPoint pt;
    const int pixelType = g_pcType != 0 ? 1 : 0;
    pt.type = pixelType == 1 ? rs::PT_PIXELJUMP : rs::PT_SURF;
    int nSeen = 0;
    const char* why = "";
    pt.snapped = RcPickFinder(vEye, vForward, true, pixelType, pt.pos, pt.type, why, nSeen);   // g_psLock, before g_rcLock
    // botox pixel_calc.cpp:207 / route_calc.cpp:1362-1363, 1:1: classified here, before g_rcLock
    // (RcPixelJumpSkin traces the world -- same reason RcAddPoint classifies before its own lock)
    if (pt.snapped && pt.type == rs::PT_PIXELJUMP) pt.pjSkin = RcPixelJumpSkin(pLocal, pt.pos, pt.pos.z) == 1;

    std::lock_guard<std::mutex> l(g_rcLock);
    s_rcPixelSolve = true;      // every message from here belongs to the pixel calculator's panel
    if (!pt.snapped)
    {
        s_rcMessage = nSeen ? RC_ERR "aim at a pixel finder point (a green / orange pixel or a yellow pixeljump) - a pixel is only exact where the finder found one"
                            : RC_ERR "no pixel finder points on this wall - run the pixel finder on it first";
        DebugLog("RC: pixel refused -- nothing aimed at (%d candidate(s))", nSeen);
        return;
    }
    s_pcPoint = pt;
    s_pcHave = true;
    s_rcGen++;
    s_rcLines.clear(); s_rcPath.clear();
    char buf[160];
    if (g_pcAdvanced)
        snprintf(buf, sizeof(buf), "calc: snapped to finder point (z: %.4f)", pt.pos.z);
    else
        snprintf(buf, sizeof(buf), "added %s point at %.2f - press calculate pixel", rs::PointTypeName(pt.type), pt.pos.z);
    s_rcMessage = buf;
    DebugLog("RC: pixel placed at %.2f %.2f %.4f -- %s (%s, %d candidate(s))",
             pt.pos.x, pt.pos.y, pt.pos.z, rs::PointTypeName(pt.type), why, nSeen);
}

// WHAT YOU AIM AT IS THE TYPE -- there is no 'place as' combo any more
//   a grey finder ring / assist target -> pixelsurf
//   a red seam ring                    -> texturebug
//   a floor face                       -> ground
//   a ceiling face                     -> headbang   (a ceiling is only ever a headbang)
//   a wall face                        -> refused, with the normal in the message
// An edgebug is a ground face you slide off, so it is placed as a ground and switched to edgebug
// in the marker's own row in the menu. A player head is a ground too: it IS a floor, and the one
// height that means anything on it is the top.
static bool RcEdgeAt(void* pLocal, const Vec3& at, rce::Edge& out);   // below, with the edgebug snap

static void RcAddPoint(void* pLocal, UserCmd_t* pCmd)
{
    if (g_offViewOffset <= 0) return;
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);

    // ---- a finder ring under the crosshair: that ring's kind IS the marker --------------------
    RcPoint sp;
    int nSeen = 0;
    const char* why = "";
    if (RcPickFinder(vEye, vForward, false, 0, sp.pos, sp.type, why, nSeen))
    {
        sp.snapped = true;
        // botox pf_hb_is_ceiling: classified right here, off the real geometry, not asked for in
        // the menu (see RcHeadbounceCeiling)
        if (sp.type == rs::PT_HEADBOUNCE)
        {
            float outZ = sp.pos.z;
            RcHeadbounceCeiling(pLocal, sp.pos, sp.hbCeiling, outZ);
            sp.pos.z = outZ;
        }
        // botox pixel_calc.cpp:207 / route_calc.cpp:1362-1363: a pixeljump lip is classified the
        // same instant it is placed, route marker or pixel calc point alike
        else if (sp.type == rs::PT_PIXELJUMP)
            sp.pjSkin = RcPixelJumpSkin(pLocal, sp.pos, sp.pos.z) == 1;
        char b[160];
        {
            std::lock_guard<std::mutex> l(g_rcLock);
            s_rcPixelSolve = false;
            if ((int)s_rcPoints.size() >= k_rcMaxMarkers) { s_rcMessage = RC_ERR "marker limit reached"; return; }
            s_rcPoints.push_back(sp);
            s_rcGen++;
            s_rcLines.clear(); s_rcPath.clear();
            if (g_rcAdvanced)
                snprintf(b, sizeof(b), "marker %d placed (%s, %s from the pixel finder, z %.3f)",
                         (int)s_rcPoints.size(), rs::PointTypeName(sp.type), why, sp.pos.z);
            else   // botox's short readout: "added <type> point at <z>"
                snprintf(b, sizeof(b), "added %s point at %.2f", rs::PointTypeName(sp.type), sp.pos.z);
            s_rcMessage = b;
        }
        DebugLog("RC: marker placed at %.2f %.2f %.4f -- %s (finder point, %d candidate(s))",
                 sp.pos.x, sp.pos.y, sp.pos.z, rs::PointTypeName(sp.type), nSeen);
        return;
    }

    // ---- ground / edgebug / headbang: the surface, or a player's head -------------
    GameTrace_t trace{};
    const bool bWorld = RcAimWorld(pLocal, vEye, pCmd->viewangles, trace);

    RcPoint pt;
    Vec3 vHead, oHead;
    int iHead = 0; bool bMate = false;
    const float flWorldT = bWorld ? trace.fraction * 8192.f : 8192.f;
    const bool bHead = RcAimPlayerHead(pLocal, vEye, vForward, flWorldT, vHead, iHead, oHead, bMate);
    // what the crosshair actually hit, for "it does nothing" reports . adds the
    // team + collision group of the entity hit, so "my own building does nothing" is one press to
    // settle: kind 2 = it came back as a building, group 22 = solid to player movement
    const int entTeam = (trace.m_pEnt && g_offTeamNum   > 0) ? EntI(trace.m_pEnt, g_offTeamNum)   : -1;
    const int entGrp  = (trace.m_pEnt && g_offCollGroup > 0) ? EntI(trace.m_pEnt, g_offCollGroup) : -1;
    DebugLog("RC: marker aim -- trace frac %.3f ent %d kind %d team %d group %d nz %.2f contents 0x%X surf '%s' | player %d%s",
             trace.fraction, RcEntIndexOf(trace.m_pEnt), RcFloorKindOf(trace.m_pEnt), entTeam, entGrp,
             trace.plane.normal.z, trace.contents, trace.surface.name ? trace.surface.name : "", iHead,
             bMate ? " (TEAMMATE)" : "");
    // a pure player clip is invisible, so a marker on one looks like a marker in mid air. It is
    // the surface you actually stand on, which is the one the calculator wants -- say so once
    // rather than let it read as a misplaced marker
    bool bClip = bWorld && (trace.contents & 0x10000) != 0 && (trace.contents & 0x1) == 0;
    bool bClipTop = false;   // the hit was a clip's side: the marker went on the clip's top
    bool bClipThrough = false;   // ...too tall a clip: aimed through it at what you see
    bool bLipTop = false;        // the hit was a solid face's side: the marker went on its top
    // a teammate in front is not solid: the world behind him gets the marker, the note says why
    const char* mateNote = bMate ? " - the teammate in front was skipped, teammate heads are not solid" : "";
    if (bHead)
    {
        pt.pos = vHead; pt.type = rs::PT_GROUND; pt.player = true;
        const unsigned h = RcEntHandleOf(GetClientEntity(iHead));
        if (h != 0xFFFFFFFFu) { pt.ent = h; pt.entOff = vHead - oHead; }
    }
    else if (!bWorld)
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        s_rcPixelSolve = false;
        s_rcMessage = bMate ? RC_ERR "that is a teammate - teammate heads are not solid to you, aim at an enemy"
                            : RC_ERR "nothing under the crosshair";
        return;
    }
    else
    {
        // a line trace stops 1/32 off the plane: a floor marker is already the landing height,
        // a ceiling marker already where the head stops. No finder snap on this path at all
        // these types are surfaces, and a nearby pin z is a different marker.
        // THE FACE IS THE TYPE: a floor can only be landed on (ground; switch the row to edgebug
        // in the menu if you mean to slide off it), a ceiling can only be hit with your head.
        // 0.7 is the engine's own floor/ceiling cut (CategorizePosition, normal.z > 0.7 = standable)
        // A player clip's SIDE (botox route_calc add key): its top if a hull could stand there,
        // else the ray again without CONTENTS_PLAYERCLIP, onto the geometry you can see
        if (bClip && fabsf(trace.plane.normal.z) <= 0.7f)
        {
            Vec3 top;
            if (RcClipTop(pLocal, trace, top))
            {
                trace.endpos = top;
                trace.plane.normal = Vec3(0.f, 0.f, 1.f);
                bClipTop = true;
            }
            else
            {
                TraceFilterRcMarker filter; filter.Setup(pLocal);
                GameTrace_t through{};
                TraceLine(vEye, vEye + vForward * 8192.f, MASK_RC_NOCLIP, &filter, &through);
                if (through.fraction < 1.f && !through.startsolid && !through.allsolid)
                {
                    trace = through;
                    bClip = false;
                    bClipThrough = true;
                }
            }
            DebugLog("RC: marker on a clip side -- %s, z %.4f", bClipTop ? "took its top" : bClipThrough
                     ? "aimed through it" : "no top, nothing behind it", trace.endpos.z);
        }
        pt.pos = trace.endpos;
        pt.type = trace.plane.normal.z > 0.7f ? rs::PT_GROUND : rs::PT_HEADBANG;
        // ---- A WALL IS NEITHER ----------------------------------------------------
        // A ground / edgebug is a floor you land on or slide off, a headbang is a ceiling your head
        // hits; a wall face is neither, and the z stored off one is whatever height the crosshair
        // happened to be at -- a marker for a spot that is not the one you meant (the "i want a
        // ground point on a thin ledge" report, where the ray caught the ledge's SIDE). Refuse it
        // and say so, so the fix is to aim at the top instead of to wonder why the answer is wrong.
        // botox "allow selecting invalid pixels": the wall hit is a pixelsurf marker anyway, at the
        // crosshair's z, unverified (no finder pinned it)
        // ...unless it is the SIDE of something you can stand on top of: a handrail, a ledge lip,
        // a prop's edge. The top within a step of the hit is the floor you meant (RcClipTop, solid)
        if (fabsf(trace.plane.normal.z) <= 0.7f && !g_rcAllowInvalid
                 && RcClipTop(pLocal, trace, pt.pos, k_rcLipClimb, false))
        {
            pt.type = rs::PT_GROUND;
            bLipTop = true;
            DebugLog("RC: marker on a face side -- took the top %.2fu up, z %.4f",
                     pt.pos.z - trace.endpos.z, pt.pos.z);
        }
        else if (fabsf(trace.plane.normal.z) <= 0.7f && g_rcAllowInvalid)
            pt.type = rs::PT_SURF;
        else if (fabsf(trace.plane.normal.z) <= 0.7f)
        {
            std::lock_guard<std::mutex> l(g_rcLock);
            s_rcPixelSolve = false;
            char b[256];
            snprintf(b, sizeof(b), RC_ERR "that face is a wall - aim at a floor (ground) or a ceiling "
                                   "(headbang) (normal z %.2f)%s", trace.plane.normal.z,
                     // a wall is where the finder's rings live: the miss is usually an off-ring aim
                     nSeen ? " - for a pixelsurf / texturebug marker, aim at a finder ring itself" : "");
            s_rcMessage = b;
            DebugLog("RC: marker refused -- wall face, normal z %.3f", trace.plane.normal.z);
            return;
        }
    }

    // botox "snap points to nearest edge": a ground marker moves onto the lip of the floor it sits
    // on (xy AND z, botox movement_nearest_edge). An entity marker is pinned to its hull top every
    // frame, so there is no lip on it to keep
    float flSnapDist = -1.f;
    if (g_rcSnapEdge && pt.type == rs::PT_GROUND && !pt.ent)
    {
        rce::Edge e{};
        if (RcEdgeAt(pLocal, pt.pos, e))
        {
            flSnapDist = e.dist;
            pt.pos = Vec3(e.x, e.y, e.z);
        }
    }

    char buf[128];
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        s_rcPixelSolve = false;     // a route marker: the route calculator's panel says so
        if ((int)s_rcPoints.size() >= k_rcMaxMarkers) { s_rcMessage = RC_ERR "marker limit reached"; return; }
        s_rcPoints.push_back(pt);
        s_rcGen++;
        s_rcLines.clear(); s_rcPath.clear();
        if (g_rcAdvanced)
        {
            snprintf(buf, sizeof(buf), "marker %d placed (%s%s, z %.3f)", (int)s_rcPoints.size(),
                     rs::PointTypeName(pt.type),
                     pt.player ? ", player head" : "", pt.pos.z);
            s_rcMessage = buf;
            s_rcMessage += mateNote;
            if (bClipTop && !bHead)   s_rcMessage += " - on top of a player clip (invisible)";
            else if (bClip && !bHead) s_rcMessage += " - on a player clip brush (invisible, but it is what you stand on)";
            if (bClipThrough && !bHead) s_rcMessage += " - aimed through a player clip too tall to stand on";
            if (bLipTop) s_rcMessage += " - hit its side, placed on its top";
            if (flSnapDist >= 0.f)
            {
                snprintf(buf, sizeof(buf), " - snapped to the edge %.1fu away", flSnapDist);
                s_rcMessage += buf;
            }
            if (pt.type == rs::PT_SURF) s_rcMessage += " - NOT a finder dot, unverified";
        }
        else
        {
            snprintf(buf, sizeof(buf), "added %s point at %.2f", rs::PointTypeName(pt.type), pt.pos.z);
            s_rcMessage = buf;
        }
    }
    DebugLog("RC: marker placed at %.2f %.2f %.4f -- %s%s%s", pt.pos.x, pt.pos.y, pt.pos.z,
             rs::PointTypeName(pt.type), pt.snapped ? " (snapped to a finder point)" : "",
             pt.player ? " (player head)" : "");
}

static void RcDeletePoint(void* pLocal, UserCmd_t* pCmd)
{
    if (g_offViewOffset <= 0) return;
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        if (s_rcPoints.empty()) return;
        int iErase = -1;
        {
            // the marker ring the crosshair is on, the same angle rule the finder points use.
            // dropped the trace this used to need: a marker hangs in the air by design
            // (a pixelsurf, a headbang), so "how far past the surface the ray hit" was never a
            // question about markers, and it refused the ones placed off a wall
            float flBest = 1e9f;
            for (int i = 0; i < (int)s_rcPoints.size(); i++)
            {
                const float off = rs::AimTanTo(vEye, vForward, s_rcPoints[i].pos);
                if (off >= 0.f && off < flBest) { flBest = off; iErase = i; }
            }
        }
        if (iErase < 0) iErase = (int)s_rcPoints.size() - 1;   // nothing aimed at: the newest
        s_rcPoints.erase(s_rcPoints.begin() + iErase);
        s_rcGen++;
        s_rcLines.clear(); s_rcPath.clear();
        s_rcMessage.clear();
    }
}

// ---- AN EDGEBUG MARKER'S Z IS ITS LEDGE'S LIP -------------------------------------------------
// Why at all: an edgebug is the height you slide OFF at. A ground marker is the height you land
// at, so where the crosshair hit IS the answer for one and is not for the other -- on a slanted
// brush the couple of units between your click and the lip are the couple of units the bug is.
// Placing a point exactly on a lip through a crosshair is not a thing anybody can do, so the
// marker stops asking: keep the xy, take the z of the nearest edge of the floor it sits on
// (rc_edge.h walks it, this traces for it).
static bool RcEdgeAt(void* pLocal, const Vec3& at, rce::Edge& out)
{
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    auto floor = [&](float x, float y, float zRef, float& z) -> int
    {
        GameTrace_t tr{};
        TraceLine(Vec3(x, y, zRef + rce::k_drop), Vec3(x, y, zRef - 128.f), MASK_PLAYERSOLID, &filter, &tr);
        // the ray starts one step over the last floor, so a wall standing on that floor eats it
        // whole: that is a wall in the way, not the void past a lip, and the walk has to know
        if (tr.startsolid || tr.allsolid) return rce::kBlocked;
        if (!tr.DidHit()) return rce::kVoid;
        // CategorizePosition's own cut (gamemovement.cpp:3864): below it the player is never
        // grounded, so it is not a floor to slide off and counts as the void past the lip
        if (tr.plane.normal.z <= k_ebStandable) return rce::kVoid;
        z = tr.endpos.z;
        return rce::kFloor;
    };
    return rce::NearestEdge(at.x, at.y, at.z, floor, out);
}

// game thread, once a command. One scan per marker the row is switched to edgebug on, and the
// placed z back the moment it is switched off one
static void RcEdgeSnapPass(void* pLocal)
{
    if (!pLocal || !g_routeCalc || !IsInGame()) return;
    std::vector<std::pair<int, Vec3>> jobs;   // traced outside the lock: the render thread reads these
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        for (int i = 0; i < (int)s_rcPoints.size(); i++)
        {
            RcPoint& p = s_rcPoints[i];
            if (p.type != rs::PT_EDGEBUG)
            {
                if (p.edgeZ) { p.pos.z = p.baseZ; s_rcLines.clear(); s_rcPath.clear(); s_rcGen++; }
                p.edgeZ = p.edgeTried = false;
                continue;
            }
            // an entity marker's z is its hull top, rewritten every frame by RC_RefreshLive:
            // there is no lip to find on it and nothing written here would survive the next frame
            if (!p.edgeZ && !p.edgeTried && !p.ent) jobs.emplace_back(i, p.pos);
        }
    }

    for (const auto& [i, at] : jobs)
    {
        rce::Edge e{};
        const bool bGot = RcEdgeAt(pLocal, at, e);

        std::lock_guard<std::mutex> l(g_rcLock);
        if (i >= (int)s_rcPoints.size()) break;              // deleted while the traces ran
        RcPoint& p = s_rcPoints[i];
        if (p.type != rs::PT_EDGEBUG || p.pos.DistTo(at) > 0.01f) continue;   // edited under us
        p.edgeTried = true;
        char b[192];
        if (!bGot)
        {
            snprintf(b, sizeof(b), "marker %d: no edge within %.0fu - the edgebug keeps the z you placed (%.3f)",
                     i + 1, rce::k_max, p.pos.z);
            DebugLog("RC: edgebug marker %d -- no edge within %.0fu of %.2f %.2f %.4f",
                     i + 1, rce::k_max, at.x, at.y, at.z);
        }
        else
        {
            p.baseZ = p.pos.z;
            p.pos.z = e.z;
            p.edgeZ = true;
            s_rcLines.clear(); s_rcPath.clear(); s_rcGen++;
            snprintf(b, sizeof(b), "marker %d: edgebug z %.3f - the edge %.1fu away (placed z %.3f)",
                     i + 1, e.z, e.dist, p.baseZ);
            DebugLog("RC: edgebug marker %d -- lip at %.2f %.2f %.4f, %.2fu away, z %.4f -> %.4f",
                     i + 1, e.x, e.y, e.z, e.dist, p.baseZ, e.z);
        }
        s_rcPixelSolve = false;
        s_rcMessage = b;
    }
}

// ---- the per-command entry point ---------------------------------------------------------------
static void RouteCalc_Tick(void* pLocal, UserCmd_t* pCmd, bool bPredicted)
{
    if (!pCmd || !pLocal || g_offOrigin <= 0 || g_offFlags <= 0) return;

    // world coordinates: a marker kept across a map change marks a spot that does not exist
    const char* map = EngineLevelName();
    if (map && map[0] && strcmp(map, s_rcMap) != 0)
    {
        if (s_rcBusy) RcSolveCancel("");
        RcResetForMap(map);
    }
    RC_RefreshLive();   // entity markers at their entity before anything reads them
    RcEdgeSnapPass(pLocal);   // ...and edgebug markers on their ledge's lip, not on your click

    // two features now. The route's keys only answer while the route calculator is on,
    // the pixel's only while the pixel calculator is on -- either one alone is a working feature
    const bool bAdd   = g_routeCalc && g_rcAddKey    != 0 && MovementKeyOK(g_rcAddKey, 1);
    const bool bDel   = g_routeCalc && g_rcDeleteKey != 0 && MovementKeyOK(g_rcDeleteKey, 1);
    const bool bClear = g_routeCalc && g_rcClearKey  != 0 && MovementKeyOK(g_rcClearKey, 1);
    const bool bSolve = g_routeCalc && g_rcSolveKey  != 0 && MovementKeyOK(g_rcSolveKey, 1);
    // no 'marker type' bind: a pixelsurf / texturebug marker is named by the finder ring you put
    // it on (RcAddPoint), and the combo only picks between the three surface types

    if (bAdd && !s_rcAddHeld) RcAddPoint(pLocal, pCmd);
    if (bDel && !s_rcDelHeld) RcDeletePoint(pLocal, pCmd);
    if (bClear && !s_rcClearHeld)
    {
        if (s_rcBusy) RcSolveCancel("");
        {
            std::lock_guard<std::mutex> l(g_rcLock);
            s_rcPoints.clear();     // the pixel marker is the pixel calculator's own, not cleared here
            s_rcLines.clear(); s_rcPath.clear();
            s_rcMessage.clear();
            s_rcGen++;
        }
        }
    if (bSolve && !s_rcSolveHeld)
    {
        if (s_rcBusy)
            RcSolveCancel("solve cancelled");
        else if (!bPredicted)
        {
            std::lock_guard<std::mutex> l(g_rcLock);
            s_rcPixelSolve = false;
            s_rcMessage = RC_ERR "prediction is not available - cannot read your state";
        }
        else
            RcSolveBegin(pLocal, pCmd);
    }
    s_rcAddHeld = bAdd; s_rcDelHeld = bDel; s_rcClearHeld = bClear; s_rcSolveHeld = bSolve;

    // ---- pixel calculator: same solver, one pixel marker, always from where you are ----
    static bool s_pcPlaceHeld = false, s_pcSolveHeld = false;
    const bool bPcPlace = g_pixelCalc && g_pcPlaceKey != 0 && MovementKeyOK(g_pcPlaceKey, 1);
    const bool bPcSolve = g_pixelCalc && g_pcSolveKey != 0 && MovementKeyOK(g_pcSolveKey, 1);
    if (bPcPlace && !s_pcPlaceHeld) RcPlacePixel(pLocal, pCmd);
    if (bPcSolve && !s_pcSolveHeld)
    {
        if (s_rcBusy)
            RcSolveCancel("solve cancelled");
        else if (!bPredicted)
        {
            std::lock_guard<std::mutex> l(g_rcLock);
            s_rcPixelSolve = true;
            s_rcMessage = RC_ERR "prediction is not available - cannot read your state";
        }
        else
            RcSolveBegin(pLocal, pCmd, true);
    }
    s_pcPlaceHeld = bPcPlace; s_pcSolveHeld = bPcSolve;

    // the next pass of a pixel press, started here because RcSolveBegin needs the live command.
    if (!s_rcBusy && s_pcNextHops >= 0 && bPredicted)
    {
        const int h = s_pcNextHops;
        s_pcNextHops = -1;
        RcSolveBegin(pLocal, pCmd, true, h);
    }

    // the search is pure math on a copied route: it needs no prediction once it has started
    if (s_rcBusy) RcSolvePoll();
}

// ================================================================================================
//  RENDER + MENU SIDE
// ================================================================================================
void RC_GetDrawData(RcDrawData& out)
{
    std::lock_guard<std::mutex> l(g_rcLock);
    out.pts.clear();
    out.pts.reserve(s_rcPoints.size());
    for (const RcPoint& p : s_rcPoints)
        out.pts.push_back({ p.pos, p.type, p.enabled,
                            rs::LandLike(p.type) ? p.moves != rs::kMovesMarker : (!p.stand || !p.duck),
                            p.player, p.stand, p.duck, p.hbCeiling, p.pjSkin });
    out.hasPixel = s_pcHave;
    out.pixel    = s_pcPoint.pos;
    out.pixelType = s_pcPoint.type;
    out.pixelSolve = s_rcPixelSolve;   // which of the two features owns the answer on screen
    out.path     = s_rcPath;           // the answer's own arc (empty = nothing solved yet)
    out.lines    = s_rcLines;
    out.message  = s_rcMessage;
    out.solving  = s_rcBusy;
    out.progress = s_rcProgress;
    out.gen      = s_rcGen;
}

unsigned RC_PointsGen()
{
    std::lock_guard<std::mutex> l(g_rcLock);
    return s_rcGen;
}

int RC_PointCount()
{
    std::lock_guard<std::mutex> l(g_rcLock);
    return (int)s_rcPoints.size();
}

bool RC_PointGet(int i, RcPointInfo& out)
{
    std::lock_guard<std::mutex> l(g_rcLock);
    if (i < 0 || i >= (int)s_rcPoints.size()) return false;
    const RcPoint& p = s_rcPoints[i];
    out.pos = p.pos; out.type = p.type; out.moves = p.moves; out.enabled = p.enabled;
    out.stand = p.stand; out.duck = p.duck; out.player = p.player; out.snapped = p.snapped;
    out.edgeZ = p.edgeZ; out.hbCeiling = p.hbCeiling; out.pjSkin = p.pjSkin;
    return true;
}

void RC_PointSet(int i, const RcPointInfo& in)
{
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        if (i < 0 || i >= (int)s_rcPoints.size()) return;
        RcPoint& p = s_rcPoints[i];
        int type = (in.type >= 0 && in.type < rs::PT_COUNT) ? in.type : rs::PT_GROUND;
        // the same rule the place key obeys -- a pixelsurf or texturebug marker exists
        // only where the pixel finder pinned one, so the menu combo cannot turn a surface marker
        // into one either. Place it by aiming at the ring instead
        // (botox: unless "allow selecting invalid pixels" is on)
        // a pixeljump too: its z is the lip top the finder traced
        if (rs::FinderOnly(type) && !p.snapped && !g_rcAllowInvalid) type = p.type;
        p.type    = type;
        p.moves   = (in.moves & rs::kMovesMarker) ? (in.moves & rs::kMovesMarker) : rs::kMovesMarker;
        p.enabled = in.enabled;
        p.stand   = in.stand || !in.duck;       // never both off: that marker could not be arrived at
        p.duck    = in.duck;
        p.hbCeiling = in.hbCeiling;
        s_rcLines.clear(); s_rcPath.clear();
        s_rcGen++;
    }
}

// ORDER IS THE ROUTE: marker 1 -> 2 -> 3 is what the solver flies, so re-ordering is editing the
// route itself. The menu's up/down buttons are the only way to fix a marker placed out of turn
// short of clearing the lot and walking it again
void RC_PointMove(int i, int dir)
{
    std::lock_guard<std::mutex> l(g_rcLock);
    const int j = i + dir;
    if (i < 0 || i >= (int)s_rcPoints.size() || j < 0 || j >= (int)s_rcPoints.size()) return;
    std::swap(s_rcPoints[i], s_rcPoints[j]);
    s_rcLines.clear(); s_rcPath.clear();      // the answer was for the old order
    s_rcGen++;
}

void RC_PointDelete(int i)
{
    {
        std::lock_guard<std::mutex> l(g_rcLock);
        if (i < 0 || i >= (int)s_rcPoints.size()) return;
        s_rcPoints.erase(s_rcPoints.begin() + i);
        s_rcLines.clear(); s_rcPath.clear();
        s_rcGen++;
    }
}

// the menu's "clear all points" button (the clear key does the same from RouteCalc_Tick, where it
// can also cancel a running solve; a solve flies a copied route, so one left running only answers
// for markers that are gone, and the next edit drops that answer too)
void RC_PointsClear()
{
    std::lock_guard<std::mutex> l(g_rcLock);
    s_rcPoints.clear();
    s_rcLines.clear(); s_rcPath.clear();
    s_rcMessage.clear();
    s_rcGen++;
}

// botox drop_solutions(): a setting the answer was solved under changed, so the answer is stale
void RC_DropAnswer()
{
    std::lock_guard<std::mutex> l(g_rcLock);
    if (s_rcPixelSolve) return;           // the pixel calculator's answer is not the route's to drop
    s_rcLines.clear(); s_rcPath.clear();
    s_rcGen++;
}

unsigned RC_MarkerMoves() { return rs::kMovesMarker; }
unsigned RC_StartMoves()  { return (rs::kMovesPress & ~(1u << rs::MB_DELAY)) | rs::kMovesStart; }   // no landing to delay off

const char* RC_MoveName(int bit) { return rs::MoveName(bit); }
int         RC_MoveCount()       { return rs::MB_COUNT; }
const char* RC_TypeName(int t)   { return rs::PointTypeName(t); }
int         RC_TypeCount()       { return rs::PT_COUNT; }
