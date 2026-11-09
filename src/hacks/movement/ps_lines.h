#pragma once
#include "../../globals/globals.h"

// Pixelsurf lines: every surf you ride gets walked with the pixel finder's hold proof and drawn
// along the wall, kept per map (pixelsurf.inl PIXELSURF LINES). Drawn by render.cpp RenderPsLines.
// "full trajectory" (botox's trace-only live re-scan) is gone 11-09: the walk already measures the
// whole ridable stretch both ways
inline bool  g_psLinesEnable = false;
inline float g_psLinesColor[4]     = { 1.f, 1.f, 1.f, 0.6f };

// botox m_pixel_surf_line_style (int, def 0: full lines\0dotted lines\0dots\0) /
// m_pixel_surf_line_thickness (float, def 2.0, slider 0.5-10). Were MISSING entirely --
// the draw loop (render.cpp RenderPsOverlay) hardcoded a 1.5px solid AddLine (PSPORT 11-04).
inline int   g_psLinesStyle     = 0;
inline float g_psLinesThickness = 2.f;
