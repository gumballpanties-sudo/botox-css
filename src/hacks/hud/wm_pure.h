#pragma once
#include <cstdint>
#include <cstring>
#include <string>

// Unit wm: the pure halves of the watermark port, split out so tests/wmpure_test.cpp can run
// them without ImGui / GDI+ / d3d9. Used by gif_tex.cpp, watermark.cpp, watermark_styles.cpp.
namespace n_wm {

// botox spectator_list.cpp url_image_direct: imgur page / .gifv / extensionless link -> the raw
// file on i.imgur.com. Anything else comes back unchanged.
inline std::string UrlDirect(std::string url)
{
    for (const char* page : { "://imgur.com/", "://www.imgur.com/", "://m.imgur.com/" })
        if (const size_t at = url.find(page); at != std::string::npos) { url.replace(at, strlen(page), "://i.imgur.com/"); break; }

    if (url.find("://i.imgur.com/") == std::string::npos) return url;
    if (const size_t q = url.find_first_of("?#"); q != std::string::npos) url.erase(q);
    if (url.size() >= 5 && url.compare(url.size() - 5, 5, ".gifv") == 0) url.pop_back();
    const size_t dot = url.find_last_of('.');
    if (dot == std::string::npos || dot < url.find_last_of('/')) url += ".gif";
    return url;
}

// botox url_image_path cache name: fnv-1a 32 of the direct url
inline uint32_t Fnv1a(const std::string& s)
{
    uint32_t h = 0x811C9DC5u;
    for (const char c : s) h = (h ^ (uint8_t)c) * 0x01000193u;
    return h;
}

// botox spectator_list.cpp utf8_truncate: first maxChars code points, never splits one
inline std::string Utf8Truncate(const std::string& text, size_t maxChars)
{
    size_t chars = 0, off = 0;
    while (off < text.size()) {
        if (chars >= maxChars) return text.substr(0, off);
        const unsigned char b = (unsigned char)text[off];
        off += b < 0x80 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : 4;
        ++chars;
    }
    return text;
}

// botox draw_watermark_gif placement (before rotation): box = x0,y0,x1,y1 of the watermark,
// aspect = gif w / h, scale % of the box height, side 0 auto (toward screen centre) / 1 left / 2 right.
struct GifRect { float x0, y0, x1, y1; };
inline GifRect GifPlace(float bx0, float by0, float bx1, float by1, float aspect, float scalePct, int side,
                        float gap, float offX, float offY, float screenW)
{
    const float h = (by1 - by0) * scalePct / 100.f;
    const float w = h * aspect;
    const bool left = side == 1 || (side != 2 && (bx0 + bx1) * 0.5f > screenW * 0.5f);
    const float x = (left ? bx0 - gap - w : bx1 + gap) + offX;
    const float y = (by0 + by1 - h) * 0.5f + offY;
    return { x, y, x + w, y + h };
}

} // namespace n_wm
