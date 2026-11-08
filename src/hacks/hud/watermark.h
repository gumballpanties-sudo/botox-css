#pragma once
#include "../../globals/globals.h"

void RenderWatermark();

// botox n_misc::g_watermark_box: the rect the current style drew this frame (x0,y0,x1,y1),
// zero while off. The media player anchors under it when it shares the column.
extern ImVec4 g_wmBox;
// botox n_misc::g_watermark_gif_box: the "gif next to watermark" rect this frame, zero while off.
extern ImVec4 g_wmGifBox;
