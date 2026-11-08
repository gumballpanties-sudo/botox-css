#include "movement_tricks2_pure.h"   // Unit A3: pure decisions (bounce tangent/eb style), gate-tested

// Unit A3 port: small/medium movement tricks from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp (same convention as movement_ex.inl, right after it) --
// shares every helper/static already defined above it in the same TU: On/EntV3/EntGround/
// TraceHull/TraceFilterWorldProps/IN_*/FL_*/MASK_PLAYERSOLID, and s_textureBugHit/s_headSurfHit/
// s_headBounceHit/s_airStuckHit/s_wallClimbHit/s_psAssistHopActive/s_shouldPixelSurf/s_psRidingNow
// (movement.cpp), s_ebSearchMode/s_ebDetected (edgebug.inl, included earlier in this file).
//
// fireman lives in fireman.inl (CSS has ladders; TF2-era note that it was dropped is obsolete).
//
// Ported as OPTIONS of what already works, not as second engines, where the botox source is
// itself a full sim-planner or a second search algorithm:
//   bounce / auto bounce -- one real-time wall-tangent redirect below, not botox's sim-searched
//                  point planner (n_assist::run/programs/budgets) -- no points file this pass
//   pixel finder headbounce/pixeljump -- two new probe/classify branches added to psassist.inl's
//                  existing scanner (own result lists, PF_GetExtraPts; render wiring not done
//                  this pass -- DebugLog confirms detection)
//   edgebug style presets -- a knob bundle on the existing search (edgebug.inl), not botox's
//                  separate delusional/dna search algorithms (each its own engine)
//   pixel surf fix -- a small duck-hold guard below
// Each is named BLOCKED-as-simplified in the header above, not silently claimed as a full port.

// BOUNCE: auto bounce is botox auto_bounce.cpp (autobounce.inl). A second, simplified
// wall-tangent-redirect version used to live here (g_bounceAssist / s_bounceAssistHit, one
// trace, no points file) -- deleted, it was not a 1:1 port.

// ============================================================================
// PS FIX: a real pixel-surf ride (s_psRidingNow, movement.cpp) can be clipped by an inadvertent
// un-duck landing one tick late (a tap, or another feature's own duck edit arriving out of
// order): hold the duck bit for a couple ticks into a ride so the ladder's sub-unit phase
// (routesim.h) isn't disturbed by a bit that flickers.
// ============================================================================
static void PsSurfFixTick(void* pLocal, UserCmd_t* pCmd)
{
    static int s_holdTicks = 0;
    if (!g_psSurfFix || !pLocal) { s_holdTicks = 0; return; }
    if (s_psRidingNow && g_offDucking > 0 && EntB(pLocal, g_offDucking))
        s_holdTicks = 3;
    if (s_holdTicks > 0)
    {
        pCmd->buttons |= IN_DUCK;
        --s_holdTicks;
    }
}

// EB sound: fires on the edgebug ITSELF, not on the search latching a plan. Pre-prediction
// state = result of last cmd, so compare it to the one before: still airborne, was falling
// past the first-tick gravity value, and now sits exactly on it = vz got reset by an edge
// (same test as EbProbe's first-tick branch, on real state). One tick late (15ms) by design.
// Only counts when the EB assist plan was flying (s_ebDetected) in the last few cmds: the raw
// vz test alone also trips on corrections / slopes / unassisted hits.
// Mouse lock lives in edgebug.inl (EdgeBug_MouseInput).
// edge bug detection sound: edgebug.inl EbSoundPre / EbPlaySound (botox sound combo)

