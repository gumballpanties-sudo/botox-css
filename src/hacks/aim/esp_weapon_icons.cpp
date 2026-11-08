#include "esp_weapon_icons.h"
#include <unordered_map>
#include <vector>
#include <cstdio>

// Own stb_truetype instance: STBTT_STATIC in imgui_draw.cpp makes its copy internal-linkage, so
// this TU needs its own (zero collision risk -- both are `static`, see imgui_draw.cpp:139).
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../dependencies/imgui/imstb_truetype.h"

// weaponId (CSWeaponID) -> glyph char on the "CSweapons" scheme block / Cstrike.ttf. Decrypted
// 11-04 from the live scripts/weapon_*.ctx (see esp_weapon_icons.h banner); index order matches
// aim/css.h's CSWeaponID enum 1:1. 0 = no icon (WEAPON_NONE, WEAPON_SHIELDGUN/KEVLAR/
// ASSAULTSUIT/NVG -- no weapon_*.ctx for the last 3, they are equipment flags, not entities).
static const char kGlyph[] = {
    0,     // WEAPON_NONE
    'Y',   // WEAPON_P228
    'C',   // WEAPON_GLOCK
    'N',   // WEAPON_SCOUT
    'H',   // WEAPON_HEGRENADE
    ']',   // WEAPON_XM1014
    '\\',  // WEAPON_C4
    'L',   // WEAPON_MAC10
    'E',   // WEAPON_AUG
    'P',   // WEAPON_SMOKEGRENADE
    'S',   // WEAPON_ELITE
    'U',   // WEAPON_FIVESEVEN
    'Q',   // WEAPON_UMP45
    'O',   // WEAPON_SG550
    'V',   // WEAPON_GALIL
    'T',   // WEAPON_FAMAS
    'A',   // WEAPON_USP
    'R',   // WEAPON_AWP
    'X',   // WEAPON_MP5NAVY
    'Z',   // WEAPON_M249
    'K',   // WEAPON_M3
    'W',   // WEAPON_M4A1
    'D',   // WEAPON_TMP
    'I',   // WEAPON_G3SG1
    'G',   // WEAPON_FLASHBANG
    'F',   // WEAPON_DEAGLE
    '[',   // WEAPON_SG552
    'B',   // WEAPON_AK47
    'J',   // WEAPON_KNIFE
    'M',   // WEAPON_P90
    0,     // WEAPON_SHIELDGUN (never shipped)
    0,     // WEAPON_KEVLAR (equipment flag)
    0,     // WEAPON_ASSAULTSUIT (equipment flag)
    0,     // WEAPON_NVG (equipment flag)
};
static constexpr int kGlyphCount = sizeof(kGlyph) / sizeof(kGlyph[0]);

// Hardcoded like render.cpp's own verdana/tahoma paths (render.cpp:1568-1583) -- a different
// Steam library path means a missing-file no-op below, not a crash.
static const char* kFontPath = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Source\\cstrike\\resource\\Cstrike.ttf";

struct IconEntry
{
    IDirect3DTexture9* tex = nullptr;
    float aspect = 1.f;   // width / height
};
static std::unordered_map<int, IconEntry> s_cache;   // render thread only
static std::vector<unsigned char>* s_fontData = nullptr;
static bool s_fontTried = false, s_fontOk = false;
static stbtt_fontinfo s_font{};

static bool EnsureFont()
{
    if (s_fontTried) return s_fontOk;
    s_fontTried = true;

    FILE* f = nullptr;
    if (fopen_s(&f, kFontPath, "rb") != 0 || !f)
    {
        DebugLog("esp weapon icon: Cstrike.ttf not found at '%s'", kFontPath);
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }

    s_fontData = new std::vector<unsigned char>((size_t)size);
    const size_t read = fread(s_fontData->data(), 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) { delete s_fontData; s_fontData = nullptr; return false; }

    if (!stbtt_InitFont(&s_font, s_fontData->data(), 0))
    {
        DebugLog("esp weapon icon: stbtt_InitFont failed on '%s'", kFontPath);
        delete s_fontData; s_fontData = nullptr;
        return false;
    }
    s_fontOk = true;
    return true;
}

ImTextureID EspWeaponIcon_Get(int weaponId, float* outAspect)
{
    if (weaponId <= 0 || weaponId >= kGlyphCount || !kGlyph[weaponId] || !g_d3dDev)
        return (ImTextureID)0;

    auto it = s_cache.find(weaponId);
    if (it != s_cache.end())
    {
        if (outAspect) *outAspect = it->second.aspect;
        return it->second.tex ? (ImTextureID)it->second.tex : (ImTextureID)0;
    }

    IconEntry entry;   // cache the attempt either way -- a missing font fails once, not every frame
    s_cache[weaponId] = entry;

    if (!EnsureFont())
        return (ImTextureID)0;

    // 48px render size: big enough to stay crisp at the esp.cpp draw sizes (8-12px on screen),
    // small enough that 28 cached glyphs are a trivial amount of VRAM.
    constexpr float kPx = 48.f;
    const float scale = stbtt_ScaleForPixelHeight(&s_font, kPx);
    int w, h, xoff, yoff;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&s_font, 0.f, scale, (int)kGlyph[weaponId], &w, &h, &xoff, &yoff);
    if (!bitmap || w <= 0 || h <= 0)
    {
        if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
        return (ImTextureID)0;
    }

    std::vector<unsigned char> rgba((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++)
    {
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255;   // tinted by the caller's vertex color
        rgba[i * 4 + 3] = bitmap[i];
    }
    stbtt_FreeBitmap(bitmap, nullptr);

    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = g_d3dDev->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                               D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr) || !tex)
    {
        DebugLog("esp weapon icon: CreateTexture %dx%d failed hr=0x%08X", w, h, (unsigned)hr);
        return (ImTextureID)0;
    }

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
    {
        for (int y = 0; y < h; y++)
            memcpy((unsigned char*)lr.pBits + (size_t)y * lr.Pitch, rgba.data() + (size_t)y * w * 4, (size_t)w * 4);
        tex->UnlockRect(0);
    }

    IconEntry& cached = s_cache[weaponId];
    cached.tex = tex;
    cached.aspect = (float)w / (float)h;
    if (outAspect) *outAspect = cached.aspect;
    return (ImTextureID)tex;
}

void EspWeaponIcon_ReleaseAll()
{
    for (auto& kv : s_cache)
        if (kv.second.tex) { kv.second.tex->Release(); kv.second.tex = nullptr; }
    s_cache.clear();
}
