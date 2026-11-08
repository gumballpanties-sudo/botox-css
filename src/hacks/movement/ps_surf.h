#pragma once
// ---- IS THIS TICK A PIXEL SURF, OR JUST A HULL THE ENGINE REFUSED TO MOVE? ----------------
// The indicator's old answer was `s_shouldPixelSurf || s_psRidingNow`, and both halves lie:
//
//   s_shouldPixelSurf is the HUNT's latch -- a pin the look-ahead PREDICTS up to 32 ticks out,
//   held 16 ticks past it. Falling beside a wall latches it constantly and nothing has to
//   happen; that is "it counts just falling near some walls". The log agrees: stand=13 latches
//   against a single short ride.
//
//   s_psRidingNow is vz pinned at -halfgrav two ticks running. That is the ride's signature,
//   but it is not ONLY the ride's: TryPlayerMove zeroes the whole velocity whenever the hull is
//   trapped (two-plane crease, or a stick driven into a face), and FinishGravity puts back
//   exactly one half step. An air stuck reads it. A texture bug's crease catch reads it
//   (texturebug.inl:22-24). A hull wedged on a crate reads it. Same number, every time.
//
// ---- WHAT DOES NOT WORK: THE SHAPE OF THE WALL -------------------------------------------
// The first fix probed the face at feet-2 and feet+8 on the theory that a surf rests on top of
// an edge while a wedge is jammed against a wall that keeps going. The log killed it outright:
// on a REAL ride (PSURF ride=1 gap=0.00) both rays read 23.97. A pixel surf is a SUB-UNIT seam
// -- the brush above is set back by a fraction of a unit, or by nothing measurable at all -- so
// the wall runs on past the feet exactly like every wedge case does. There is no shape here to
// tell them apart by, and a 0.05u threshold on two float ray distances was never going to be
// the thing that did it.
//
// ---- WHAT DOES: THE HULL IS STILL TRAVELLING ---------------------------------------------
// A surf SLIDES along the face (that ride carried xy=356 with vin=-0.00: all along the wall,
// nothing into it). An air stuck, a texture bug and a crate wedge are nailed in place -- the
// crease zeroes the whole velocity and the hull does not move again until the pin breaks.
// AirStuck already splits the two this way for its own yield (`!origin_frozen`), so this is the
// dll's own discriminator, not a new theory. Distance is measured from where the pin BEGAN, so
// a ride that is braked to a stop on purpose (the park) keeps its travel and keeps the tag lit.
//
// Pure, so tests/pssurf_test.cpp can fly every case without the engine. The caller measures;
// this decides.
namespace ps
{

// how far the hull has to have carried since the pin began before this counts as a ride.
// A wedge measures 0.00 forever (AirStuck calls anything under 0.01u/tick frozen), so this is
// only about how FAST the tag lights: 4u is 2 ticks at 130 u/s, 3 at 90, 8 at 30
inline constexpr float k_travelMin = 4.f;
// how close the hull has to be to the face it is riding. A ride sits flush (the log reads
// gap=0.00 on every ride tick); 4u is slack for a corner, not for a wall across the way
inline constexpr float k_gapMax = 4.f;

// what the caller measured on this tick
struct SurfEvidence
{
    bool  riding    = false;   // vz pinned at -halfgrav 2 ticks running, airborne
    bool  wall      = false;   // flat upright non-displacement BRUSH face within the probe's reach
    float gap       = 999.f;   // hull gap to that face
    float travel    = 0.f;     // 2D distance from where this pin began (0 = nailed in place)
    bool  otherOwns = false;   // air stuck or texture bug landed a pin this tick -- their trick
};

inline bool IsSurf(const SurfEvidence& e)
{
    // no sustained pin = nothing is happening, whatever the hunt predicted
    if (!e.riding || !e.wall || e.otherOwns) return false;
    if (e.gap > k_gapMax) return false;
    // ...and the ride has to have gone somewhere. This is the whole air-stuck / texture-bug split
    return e.travel >= k_travelMin;
}

}   // namespace ps
