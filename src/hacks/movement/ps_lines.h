#pragma once
#include "../../globals/globals.h"

// Pixelsurf lines + full trajectory: connects the PS assist's saved targets with a route
// line (distinct from g_psAssistRender's points), and optionally the finder's scan order too.
// Pure data -- the draw loop lives inline in render.cpp's RenderPsOverlay, right after the
// assistPts point loop, so it reuses that function's own W2S/dl/d instead of a second copy.
inline bool  g_psLinesEnable = false;
inline bool  g_psLinesFull   = false;   // also connect the finder's scan-order points
inline float g_psLinesColor[4]     = { 1.f, 1.f, 1.f, 0.6f };
inline float g_psLinesFullColor[4] = { 0.6f, 0.6f, 1.f, 0.4f };

// botox m_pixel_surf_line_style (int, def 0: full lines\0dotted lines\0dots\0) /
// m_pixel_surf_line_thickness (float, def 2.0, slider 0.5-10). Were MISSING entirely --
// the draw loop (render.cpp RenderPsOverlay) hardcoded a 1.5px solid AddLine (PSPORT 11-04).
inline int   g_psLinesStyle     = 0;
inline float g_psLinesThickness = 2.f;
