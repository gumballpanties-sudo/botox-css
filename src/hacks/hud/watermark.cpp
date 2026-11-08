#include "watermark.h"
#include "watermark_styles.h"
#include "gif_tex.h"
#include "wm_pure.h"   // n_wm::GifPlace
#include "../../render/dpi.h"   // botox stretch block per panel (dpi scale panels)
#include "../../dependencies/data/kamibebra_gif_data.h"   // gif type original / shit (botox spectator_gifs)
#include <algorithm>
#include <cmath>

// botox misc.cpp: draw_watermark() then draw_watermark_gif(). Every style (default body included)
// lives in watermark_styles.cpp, ported 1:1 with botox's own knobs.

ImVec4 g_wmBox    = { };
ImVec4 g_wmGifBox = { };

// botox spectator_list.cpp draw_watermark_gif(): the chosen gif beside the box the style drew this
// frame -- auto side = toward screen centre, scale = % of the box height, then rotation about its
// centre, gap / offsets / alpha / horizontal mirror.
static void RenderWatermarkGif()
{
    g_wmGifBox = { };
    const ImVec4 box = g_wmBox;
    if (!g_wmGif || box.z <= box.x || box.w <= box.y) return;

    const int type = std::clamp(g_wmGifType, 0, 2);
    GifTex* gif = type == 2 ? GifTex_GetFile(g_wmGifPath)
                : type == 1 ? GifTex_Get("kamibebra_shit", kamibebra_gif_shit, sizeof(kamibebra_gif_shit))
                            : GifTex_Get("kamibebra_original", kamibebra_gif_original, sizeof(kamibebra_gif_original));
    float gw = 0.f, gh = 0.f;
    if (!GifTex_Size(gif, gw, gh)) return;
    const ImTextureID frame = GifTex_Frame(gif, ImGui::GetTime());
    if (!frame) return;

    const n_wm::GifRect r = n_wm::GifPlace(box.x, box.y, box.z, box.w, gw / gh, g_wmGifScale, g_wmGifSide,
                                           g_wmGifGap, g_wmGifOffsetX, g_wmGifOffsetY, ImGui::GetIO().DisplaySize.x);
    const float x = r.x0, y = r.y0, w = r.x1 - r.x0, h = r.y1 - r.y0;
    g_wmGifBox = ImVec4(r.x0, r.y0, r.x1, r.y1);

    const float rad = g_wmGifRotation * 3.14159265f / 180.f;
    const float c = cosf(rad), s = sinf(rad);
    const ImVec2 centre(x + w * 0.5f, y + h * 0.5f);
    const auto corner = [&](float dx, float dy) { return ImVec2(centre.x + dx * c - dy * s, centre.y + dx * s + dy * c); };

    const float u0 = g_wmGifFlip ? 1.f : 0.f, u1 = 1.f - u0;
    const int alpha = (int)(std::clamp(g_wmGifAlpha, 0.f, 100.f) * 2.55f);

    ImGui::GetBackgroundDrawList()->AddImageQuad(frame,
        corner(-w * 0.5f, -h * 0.5f), corner(w * 0.5f, -h * 0.5f), corner(w * 0.5f, h * 0.5f), corner(-w * 0.5f, h * 0.5f),
        ImVec2(u0, 0.f), ImVec2(u1, 0.f), ImVec2(u1, 1.f), ImVec2(u0, 1.f), IM_COL32(255, 255, 255, alpha));
}

void RenderWatermark()
{
    g_wmBox = { };
    // botox: every watermark style and the gif are their own stretch block
    { DpiPanelScope block(ImGui::GetBackgroundDrawList()); Watermark_Draw(); }
    { DpiPanelScope block(ImGui::GetBackgroundDrawList()); RenderWatermarkGif(); }
}
