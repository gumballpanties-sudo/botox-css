#pragma once
#include "../../globals/globals.h"

// Unit E5: blurs the backbuffer under a panel rect. Call during imgui frame build, same
// call shape as AddImage/AddRectFilled -- the actual blit+blur runs later, when
// ImGui_ImplDX9_RenderDrawData replays the draw list on the render thread (an ImDrawList
// callback). Reuses bloom's blur shader (fx_bloom_blur.h / fx_bloom_blur.hlsl), straight
// replace instead of bloom's additive combine.
//
// rounding is accepted but not applied to the blur edge itself (sharp rect, scissor-less):
// a caller's own rounded panel background draws over the blurred rect next and masks the
// corners. ponytail: true corner-crop would need a stencil pass; add it if a panel's
// corners ever show square blur past the mask.
void BlurRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float strength, float rounding);

// Same blur, crossfaded over the sharp frame at alpha 0..1 (botox screen_blur_fullscreen tint
// alpha): the boot screen and the menu background blur fade in/out instead of popping.
void BlurRectFade(ImDrawList* dl, ImVec2 min, ImVec2 max, float strength, float alpha);

// A blur job's rect moved with the vertices around it (render/dpi.cpp panel scale): if cmd is a
// blur callback, its rect becomes pivot + (rect - pivot) * scale. Any other cmd: untouched.
void BlurRect_ScaleJob(const ImDrawCmd& cmd, ImVec2 pivot, float scale);

void BlurRect_NewFrame();   // clears this frame's job slots -- call once, near Bloom_NewFrame
void BlurRect_Destroy();    // device Reset: DEFAULT-pool scratch targets + shader are gone
