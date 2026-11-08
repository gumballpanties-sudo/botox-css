#pragma once
#include "../../globals/globals.h"
#include <atomic>

// botox hacks/misc/points.cpp 1:1: a "N pts" counter beside the watermark that adds up the
// movement tricks you land (edgebug 125, held trick 125 then +25 per 0.35 s while you ride it,
// the rest 50), springs on every new trick and slides away 3 s after the last one.
// Fed from detections.cpp (CreateMove thread, cmd START state = a trick PERFORMED), drawn from
// the imgui frame build -- the total crosses threads through atomics only.
enum PtsTrick : int
{
    PTS_EDGE_BUG, PTS_FIREMAN, PTS_JUMP_BUG, PTS_HEAD_CEILING,
    PTS_PIXEL_SURF, PTS_TEXTURE_BUG, PTS_AIR_STUCK, PTS_WALL_CLIMB,
    PTS_PIXEL_SURF_RIDE, PTS_TEXTURE_BUG_RIDE, PTS_AIR_STUCK_RIDE, PTS_WALL_CLIMB_RIDE
};
void Points_OnTrick(int trick);   // any thread

// botox g_trick_fired / g_es_fired: PERFORMED counts (cmd start state), the keybind indicator
// particles burst on a change. Bumped in Points_OnTrick before any gate, so points off still counts.
inline std::atomic<int> g_trickFired[PTS_PIXEL_SURF_RIDE] = {};
inline std::atomic<int> g_esFired{ 0 };
void RenderPoints();              // imgui frame build, after RenderWatermark (anchors beside its box)

// botox font_name_points_montserrat_italic_18 ("big font"), built in render.cpp BuildFontAtlas
inline ImFont* g_fontPoints18 = nullptr;
