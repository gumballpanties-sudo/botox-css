#pragma once
// Pure helpers of spectator_list.cpp (botox spectator_list.cpp), no engine / imgui deps so
// tests/specpure_test.cpp can run them: utf8 cut, imgur link -> raw file, url cache name,
// eyes-style row layout.
#include <string>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace n_spec {

// botox utf8_truncate: first maxChars codepoints, whole sequences only
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

// botox url_image_direct: imgur page / .gifv / no extension links -> the raw file on i.imgur.com
inline std::string UrlDirect(std::string url)
{
    for (const char* page : { "://imgur.com/", "://www.imgur.com/", "://m.imgur.com/" }) {
        const size_t at = url.find(page);
        if (at != std::string::npos) { url.replace(at, strlen(page), "://i.imgur.com/"); break; }
    }
    if (url.find("://i.imgur.com/") == std::string::npos) return url;
    const size_t q = url.find_first_of("?#");
    if (q != std::string::npos) url.erase(q);
    if (url.size() > 5 && url.compare(url.size() - 5, 5, ".gifv") == 0) url.pop_back();
    const size_t dot = url.find_last_of('.');
    if (dot == std::string::npos || dot < url.find_last_of('/')) url += ".gif";
    return url;
}

// fnv-1a, the downloaded file's name under C:\botox-css\images
inline uint32_t UrlHash(const std::string& url)
{
    uint32_t h = 0x811C9DC5u;
    for (const char c : url) h = (h ^ (uint8_t)c) * 0x01000193u;
    return h;
}

// botox eye_row_layout: one row = avatar circle + gap + eye icon, padded for the stroke halo
struct EyeLayout { int face = 0, pad = 0, width = 0, height = 0, pitch = 0; float radius = 0.f, icon = 0.f, stroke = 0.f, gap = 0.f; };

inline EyeLayout MakeEyeLayout(int face)
{
    constexpr float k_eyeStroke = 1.5f;
    EyeLayout l;
    l.face   = face;
    l.radius = face * 0.5f;
    l.icon   = face * 0.8f;
    l.stroke = k_eyeStroke * l.icon / 24.f;
    l.gap    = face * 0.09f;
    l.pad    = (int)std::ceil(l.stroke * 0.5f + 2.f);
    l.width  = (int)std::ceil(l.pad * 2.f + face + l.gap + l.icon);
    l.height = face + l.pad * 2;
    l.pitch  = (int)std::floor(face * 1.25f + 0.5f);
    return l;
}

} // namespace n_spec
