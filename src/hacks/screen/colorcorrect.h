#pragma once
#include "../../globals/globals.h"

// Color correction -- fullscreen post-process, ported from fpsoverlay's render.cpp.
//
// WHERE IT RUNS, and why it is not in h_Present like the original was.
// A capture hook (obs, ShadowPlay) owns the Present entry on this build, so anything
// drawn in h_Present lands after the capture copied the backbuffer -- on screen only.
// fpsoverlay graded in h_Present, which for a recording tool is the one place the grade
// must not be. So the pass runs in the game's own UI pass instead:
//   * vgui PaintTraverse, right before the crosshair draws -- world and HUD are already
//     in the backbuffer, our overlay is not, so the overlay stays ungraded.
//   * h_EndScene, first backbuffer scene, when threaded rendering puts the vgui paint on
//     the main thread and dih stands down there (the same fallback the crosshair uses).
// Either way it is in the backbuffer long before Present, so every capture path records it.
//
// ColorCorr_Frame is idempotent per frame: the first caller to reach a backbuffer target
// grades and latches; ColorCorr_NewFrame at the end of h_Present clears the latch.

// CcCurve itself is in types.h. A curve is a mapping, not an offset: identity (the
// diagonal) is neutral. Points stay sorted by x, the first is pinned to x=0 and the last
// to x=1, and the interpolation is monotone cubic (Fritsch-Carlson), so a curve can never
// ring or overshoot past its own control points -- overshoot is what puts halos and
// clipped highlights in a grade.
//
// The one evaluator. The menu graph, the LUT bake and the neutral test all call it, so
// what the curve editor draws is exactly what the GPU applies.
float CcEval(const CcCurve& c, float x);

void  CcResetCurve(CcCurve& c);          // back to the identity diagonal
void  CcResetAll();                      // every slider and every curve
bool  CcCurveIsIdentity(const CcCurve& c);
bool  CcIsNeutral();                     // nothing to do -- the pass is skipped entirely
int   CcAddPoint(CcCurve& c, float x, float y);   // -> new index, or -1 if it will not fit
void  CcRemovePoint(CcCurve& c, int i);           // endpoints and n==2 are refused
void  CcMovePoint(CcCurve& c, int i, float x, float y);  // x is clamped between neighbours

// -- device side ------------------------------------------------------------------
void ColorCorr_Destroy();                        // device Reset, eject
bool ColorCorr_Frame(IDirect3DDevice9* dev);     // true if this call graded the frame
void ColorCorr_NewFrame();                       // end of h_Present: clear the latch
