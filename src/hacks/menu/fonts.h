#pragma once
// botox tab_fonts port (menu_tab_settings.cpp tab_fonts + globals/fonts/fonts.cpp).
// Two botox pages map to TF2:
//   indicators -> g_fontMi, the ImGui atlas face every movement indicator draws with
//                 (render.cpp BuildFontAtlas; re-baked per indicator scale, IndicatorFont)
//   esp        -> the vgui surface face the aim-tree esp draws with (aim/helpers.cpp
//                 CFonts::Reload, FONT_INDICATORS). vgui takes a GDI family name, not a file,
//                 so this page lists families instead of font files.
// botox's chud hud / default hud pages edit CS:GO panorama fonts.conf -- no TF2 equivalent.
#include <cstdint>
#include <string>
#include <vector>

// botox e_free_type_font_flags, one bit each, same order as the "font flags" multicombo.
// Bit i == ImGuiFreeTypeBuilderFlags 1 << i, so the mask IS the FontBuilderFlags value.
enum FontFlagBit
{
    FF_NOHINTING, FF_NOAUTOHINT, FF_FORCEAUTOHINT, FF_LIGHTHINTING,
    FF_MONOHINTING, FF_BOLD, FF_OBLIQUE, FF_MONOCHROME, FF_COUNT
};

// Defaults: indicators = botox m_indicator_font_settings ("Verdanab", 29, no flags), esp Verdana 22 bold.
constexpr const char* kFontIndDefName = "verdanab";
constexpr int         kFontIndDefSize = 29;
constexpr int         kFontIndDefFlags = 0;
constexpr const char* kFontEspDefName = "Verdana";
constexpr int         kFontEspDefSize = 22;
constexpr int         kFontEspDefFlags = 1 << FF_BOLD;

extern char g_fontIndName[64];
extern int  g_fontIndSize;    // 0 = default face (botox: size 0 falls back to the stock font)
extern int  g_fontIndFlags;
extern char g_fontEspName[64];
extern int  g_fontEspSize;
extern int  g_fontEspFlags;

const std::vector<std::string>& Fonts_FileNames();   // botox m_font_file_names (file stems)
const std::vector<std::string>& Fonts_Families();    // GDI families (esp page)
std::string Fonts_FindPath(const char* name);        // botox find_path: stem -> full path
std::string Fonts_FamilyPath(const char* family);    // esp page: GDI family -> font file (registry, then find_path)
void Fonts_RequestReload();                          // botox "reload fonts" button
void Fonts_ResetIndicator();
void Fonts_ResetEsp();
uint32_t Fonts_IndSig();     // changes whenever the indicator settings (or a reload) change
uint32_t Fonts_EspSig();
bool Fonts_EspDue();         // vgui side: true once when the esp face must be recreated
void Fonts_EspApplied(uint32_t sig);

// ---- pure, tested by tests/fonts_test.cpp ------------------------------------------------

inline uint32_t FontSig(const char* name, int size, int flags, int reload)
{
    uint32_t h = 2166136261u;   // FNV-1a
    auto mix = [&](uint32_t v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 16777619u; } };
    for (const char* p = name; p && *p; ++p) { h ^= (unsigned char)*p; h *= 16777619u; }
    mix((uint32_t)size); mix((uint32_t)flags); mix((uint32_t)reload);
    return h ? h : 1u;   // 0 is "never applied"
}

// Fires once a signature differs from the applied one and has held still 250 ms (botox
// render.cpp's s_changed debounce): a size drag rebuilds once at the end, not every frame.
struct FontSettle
{
    uint32_t seen = 0;
    uint64_t since = 0;
    bool Due(uint32_t sig, uint32_t applied, uint64_t nowMs)
    {
        if (sig != seen) { seen = sig; since = nowMs; }
        return sig != applied && nowMs - since >= 250;
    }
};

// botox freetype flags -> vgui SetFontGlyphSet. GDI has no hinting knobs: bold = weight,
// oblique = italic, monochrome = no antialias; the hinting bits do nothing on this face.
struct VguiFace { int weight; int flags; };
inline VguiFace FontFlagsToVgui(int mask)
{
    constexpr int kItalic = 0x001, kAntialias = 0x010;   // FONTFLAG_ITALIC / FONTFLAG_ANTIALIAS (aim/defs.h)
    VguiFace f;
    f.weight = (mask & (1 << FF_BOLD)) ? 700 : 400;
    f.flags  = ((mask & (1 << FF_OBLIQUE)) ? kItalic : 0) | ((mask & (1 << FF_MONOCHROME)) ? 0 : kAntialias);
    return f;
}

// Indicator raster px: base size (0 -> default) times the indicator scale, clamped.
inline float FontIndPx(int size, float scale)
{
    const float s = (scale > 0.2f) ? scale : 1.f;
    const int base = (size > 0) ? size : kFontIndDefSize;
    float px = (float)(int)(base * s + 0.5f);
    if (px < 6.f)   px = 6.f;
    if (px > 100.f) px = 100.f;   // 50 (slider max) * 2.5 (scale max) would be 125: atlas guard
    return px;
}
