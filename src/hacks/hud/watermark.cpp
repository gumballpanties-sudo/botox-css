#include "watermark.h"
#include "watermark_styles.h"
#include "gif_tex.h"
#include "wm_pure.h"   // n_wm::GifPlace
#include "../../render/dpi.h"   // botox stretch block per panel (dpi scale panels)
#include "../../dependencies/imgui/imgui_internal.h"   // GImGui->HoveredWindow
#include "../../dependencies/data/kamibebra_gif_data.h"   // gif type original / shit (botox spectator_gifs)
#include <algorithm>
#include <cmath>
#include <cstring>

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

// botox spectator_list.cpp screen_images: the visuals > images list, any count (render thread)
std::vector<n_screen_image::image_t>& ScreenImages()
{
    static std::vector<n_screen_image::image_t> s_list;
    return s_list;
}

void ScreenImages_SaveCfg(FILE* f)
{
    const auto& list = ScreenImages();
    fprintf(f, "screenImages=%d\n", (int)list.size());
    for (const auto& image : list)
        fprintf(f, "screenImage=%s\n", n_screen_image::to_line(image).c_str());
}

bool ScreenImages_LoadCfg(const char* key, const char* val)
{
    if (strcmp(key, "screenImages") == 0) { ScreenImages().clear(); return true; }   // the count line opens the block
    if (strcmp(key, "screenImage") != 0) return false;
    n_screen_image::image_t image;
    if (n_screen_image::from_line(val, image)) ScreenImages().push_back(std::move(image));
    else DebugLog("config: screenImage line did not parse, dropped");
    return true;
}

// botox spectator_list.cpp draw_screen_images(): visuals > images (fpsoverlay image overlay).
// Free spot or parked beside the watermark, both dragged with the menu open (topmost wins).
static void RenderScreenImages()
{
    static int grabbed = -1;
    static ImVec2 grabOffset;
    if (!g_showMenu || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) grabbed = -1;
    if (!g_sfxImagesEnable) return;

    // menu window over the image takes the click
    const bool click = g_showMenu && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !GImGui->HoveredWindow;
    const ImVec2 m = Dpi_ScreenMouse();
    const float dpi = Dpi_PanelScale();
    const ImVec4 box = g_wmBox;
    const float screenW = ImGui::GetIO().DisplaySize.x;
    ImDrawList* const dl = ImGui::GetBackgroundDrawList();

    const ImDrawCallback pointFilter = [](const ImDrawList*, const ImDrawCmd*) {
        g_d3dDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        g_d3dDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    };

    auto& images = ScreenImages();
    for (int i = 0; i < (int)images.size(); ++i)
    {
        n_screen_image::image_t& img = images[i];
        if (!img.show) continue;
        GifTex* gif = GifTex_GetFile(img.path.c_str());
        float gw = 0.f, gh = 0.f;
        if (!GifTex_Size(gif, gw, gh)) continue;
        const ImTextureID frame = GifTex_Frame(gif, ImGui::GetTime());
        if (!frame) continue;

        const float scale = std::max(img.scale, 0.01f);
        const float w = gw * scale, h = gh * scale;
        const ImVec2 size(w, h);
        const bool anchored = img.anchor == 1 && box.z > box.x && box.w > box.y;
        const bool left = (box.x + box.z) * 0.5f > screenW * 0.5f;
        const float midY = (box.y + box.w - h) * 0.5f;

        ImVec2 at = anchored ? ImVec2(left ? box.x - img.offset_x - w : box.z + img.offset_x, midY + img.offset_y) : ImVec2(img.x, img.y);
        const ImVec2 shown = Dpi_PanelPos(at, size);
        if (click && m.x >= shown.x && m.x <= shown.x + w * dpi && m.y >= shown.y && m.y <= shown.y + h * dpi)
        {
            grabbed = i;
            grabOffset = ImVec2(m.x - shown.x, m.y - shown.y);
        }

        // drag: free = x / y, next to watermark = the offsets that put it there
        if (grabbed == i)
        {
            at = Dpi_PanelLayoutPos(ImVec2(m.x - grabOffset.x, m.y - grabOffset.y), size);
            if (anchored)
            {
                img.offset_x = left ? box.x - w - at.x : at.x - box.z;
                img.offset_y = at.y - midY;
            }
            else
            {
                img.x = at.x;
                img.y = at.y;
            }
        }

        const float rad = img.rotation * 3.14159265f / 180.f;
        const float c = cosf(rad), s = sinf(rad);
        const ImVec2 centre(at.x + w * 0.5f, at.y + h * 0.5f);
        const auto corner = [&](float dx, float dy) { return ImVec2(centre.x + dx * c - dy * s, centre.y + dx * s + dy * c); };
        const int alpha = (int)(std::clamp(img.alpha, 0.f, 100.f) * 2.55f);

        // own block per image: dpi scales each about its own centre, matches the Dpi_PanelPos hit test
        DpiPanelScope block(dl);
        if (img.pixel_art) dl->AddCallback(pointFilter, nullptr);
        dl->AddImageQuad(frame,
            corner(-w * 0.5f, -h * 0.5f), corner(w * 0.5f, -h * 0.5f), corner(w * 0.5f, h * 0.5f), corner(-w * 0.5f, h * 0.5f),
            ImVec2(0.f, 0.f), ImVec2(1.f, 0.f), ImVec2(1.f, 1.f), ImVec2(0.f, 1.f), IM_COL32(255, 255, 255, alpha));
        if (img.pixel_art) dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
}

void RenderWatermark()
{
    g_wmBox = { };
    // botox: every watermark style and the gif are their own stretch block
    { DpiPanelScope block(ImGui::GetBackgroundDrawList()); Watermark_Draw(); }
    { DpiPanelScope block(ImGui::GetBackgroundDrawList()); RenderWatermarkGif(); }
    RenderScreenImages();
}
