#pragma once
#include "../../globals/globals.h"
#include "screen_image_pure.h"
#include <cstdio>

void RenderWatermark();

// botox n_misc::g_watermark_box: the rect the current style drew this frame (x0,y0,x1,y1),
// zero while off. The media player anchors under it when it shares the column.
extern ImVec4 g_wmBox;
// botox n_misc::g_watermark_gif_box: the "gif next to watermark" rect this frame, zero while off.
extern ImVec4 g_wmGifBox;

// visuals > images (botox n_misc::screen_images): any count, render thread. Drawn by RenderWatermark.
std::vector<n_screen_image::image_t>& ScreenImages();
// config.cpp: "screenImages=<count>" (a load clears the list) then one "screenImage=<line>" each.
// Written in full and visuals files alike. Load = true when the key was one of these.
void ScreenImages_SaveCfg(FILE* f);
bool ScreenImages_LoadCfg(const char* key, const char* val);
