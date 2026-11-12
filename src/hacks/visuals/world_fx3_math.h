#pragma once
// Pure, engine-free halves of world_fx3.cpp (botox bullets.cpp / kill_effects.cpp ragdoll proxy /
// edicts.cpp sun drift). tests/worldfx3_test.cpp drives them.
#include <cmath>
#include <cstring>

namespace wfx3 {

struct V3 { float x, y, z; };

inline float Dist(const V3& a, const V3& b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// botox bullets.cpp sane_position: finite and inside the 32768 world box.
inline bool SanePos(const V3& p)
{
    constexpr float k = 32768.f;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::fabs(p.x) < k && std::fabs(p.y) < k && std::fabs(p.z) < k;
}

// botox on_player_hurt: the local impact nearest the victim's chest (origin + 40), within 128u
// and no older than 0.5s. -1 = none.
struct Impact { V3 pos; float time; };
inline int NearestImpact(const Impact* list, int n, const V3& chest, float now)
{
    int best = -1;
    float bestDist = 128.f;
    for (int i = 0; i < n; ++i) {
        if (now - list[i].time > 0.5f || now < list[i].time) continue;
        const float d = Dist(list[i].pos, chest);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// botox world hit marker alpha: full for 70% of the duration, then a linear fade.
inline float MarkerAlpha(float elapsed, float duration)
{
    if (duration < 0.1f) duration = 0.1f;
    if (elapsed < 0.f || elapsed >= duration) return 0.f;
    const float f = elapsed / duration;
    return f < 0.7f ? 1.f : 1.f - (f - 0.7f) / 0.3f;
}

// botox movement trail rainbow: three sines 120 degrees apart, t = realtime * 1.5. rgb 0..1.
inline void Rainbow(float realtime, float rgb[3])
{
    const float t = realtime * 1.5f;
    rgb[0] = (std::sin(t) + 1.f) * 0.5f;
    rgb[1] = (std::sin(t + 2.0943952f) + 1.f) * 0.5f;
    rgb[2] = (std::sin(t + 4.1887903f) + 1.f) * 0.5f;
}

// botox movement trail stepping: a beam per 4u travelled, a jump > 256u (teleport/respawn) restarts
// the chain without a segment. 0 = wait, 1 = draw last -> origin, 2 = restart at origin.
inline int TrailStep(const V3& last, const V3& origin)
{
    const float d = Dist(last, origin);
    if (d < 4.f) return 0;
    if (d > 256.f || !SanePos(origin)) return 2;
    return 1;
}

// botox ragdoll_force_proxy: mode 1 "away" = horizontal force * 60 * strength, z lifted to at
// least a quarter of it; mode 2 "fly up" = horizontal * 0.1, z = 250000 * strength.
inline void RagdollForce(int mode, float strength, float f[3])
{
    if (mode <= 0) return;
    strength = strength < 0.1f ? 0.1f : (strength > 5.f ? 5.f : strength);
    if (mode == 1) {
        f[0] *= 60.f * strength;
        f[1] *= 60.f * strength;
        const float lift = std::hypot(f[0], f[1]) * 0.25f;
        f[2] = f[2] * 2.f > lift ? f[2] * 2.f : lift;
    } else {
        f[0] *= 0.1f;
        f[1] *= 0.1f;
        f[2] = 250000.f * strength;
    }
}

// CSS vgui panel names for botox's remove hud elements list (index = g_wdNoHudElems slot).
// DECLARE_HUDELEMENT(className) in the panel's own .cpp -- panel name IS the class name
// (CHudElement's own ctor, hud_element_helper.h) -- grepped resources/css-community-master/
// mp/src/game/client/cstrike/*.cpp + game/client/*.cpp (TF2 renamed several of these, e.g.
// "HudPlayerHealth"/"HudWeaponAmmo"; CSS keeps the stock CHud* names).
struct HudElem { const char* label; const char* panels[3]; };
// vgui PANEL names (IPanel::GetName = the BaseClass(NULL, "...") arg), not the CHudElement class names.
inline constexpr HudElem k_hudElems[] = {
    { "health & armor",       { "HudHealth", "HudArmor", nullptr } },          // cs_hud_health.cpp / hud_armor.cpp
    { "ammo",                 { "HudAmmo", nullptr, nullptr } },                // cs_hud_ammo.cpp
    { "weapon selection",     { "HudWeaponSelection", nullptr, nullptr } },     // cs_hud_weaponselection.cpp
    { "killfeed",             { "HudDeathNotice", nullptr, nullptr } },         // hud_deathnotice.cpp
    { "money",                { "HudAccount", nullptr, nullptr } },             // hud_account.cpp
    { "timer & team counter", { "HudRoundTimer", nullptr, nullptr } },          // hud_roundtimer.cpp -- CSS has no koth-style 2nd timer
    { "chat",                 { "HudChat", nullptr, nullptr } },                // cs_hud_chat.cpp
    { "hint text",            { "HudHintDisplay", "HudHintKeyDisplay", nullptr } },   // shared game/client/hud_hintdisplay.cpp, not TF2-only
    { "voice menu",           { "HudMenu", nullptr, nullptr } },                // shared game/client/menu.cpp (radio/voice commands)
    { "vote",                 { "CHudVote", nullptr, nullptr } },                // shared game/client/hud_vote.cpp
    { "spectator panel",      { "specgui", nullptr, nullptr } },                 // engine-built-in, not a CHudElement -- unverified name for CSS, kept as-is
    { "target id",            { "TargetID", nullptr, nullptr } },               // cs_hud_target_id.cpp -- CS's crosshair name/health readout, no CS:GO/TF2 analog kept 1:1
    { "radar",                { "HudRadar", nullptr, nullptr } },               // hud_radar.cpp
};
// Removed with no CSS equivalent found (grepped every DECLARE_HUDELEMENT in game/client +
// game/client/cstrike, see report): TF2's "alerts" (HudAlert, annotation system), "death cam"
// (FreezePanel) and "win panel" (WinPanel) -- none of those class names exist in CSS.
// g_wdNoHudElems (menu/config, not this unit's) is a bitmask keyed to this array's INDEX --
// reordering/resizing it shifts what each saved bit means. One-time migration, flagged in
// report NEEDS FROM OTHERS.
inline constexpr int k_hudElemCount = (int)(sizeof(k_hudElems) / sizeof(k_hudElems[0]));

// ---- world / prop texture (botox misc.cpp world_texture) ----
// botox's preset labels, each mapped to a texture that ships in TF2's own tf2_textures / hl2_textures
// VPKs (every path checked against the VPK directories 11-01). CS:GO-only looks with no TF2 texture
// (ice, sandstone, mudbrick, herringbone, roof tile, asphalt) are left out.
struct TexPreset { const char* label; const char* texture; };
inline constexpr TexPreset k_texPresets[] = {
    { "off", nullptr },
    { "dev", "dev/dev_measuregeneric01b" },
    { "dev orange", "dev/dev_measuregeneric01" },
    { "dev wall", "dev/dev_measurewall01a" },
    { "grid", "dev/graygrid" },
    { "snow", "nature/snowfloor001a" },
    { "snow field", "nature/snow_full001" },
    { "cobblestone", "medieval/cobblestone001" },
    { "grass", "nature/grass_01" },
    { "grass old", "harvest/harvest_grass" },
    { "dirt", "nature/dirtfloor006a" },
    { "mud", "nature/mudfloor001a" },
    { "sand", "nature/sandfloor010a" },
    { "gravel", "nature/gravelfloor001a" },
    { "rock", "nature/rockwall007" },
    { "stone", "nature/stonewall001" },
    { "concrete floor", "concrete/concretefloor008a" },
    { "concrete wall", "concrete/concretewall004a" },
    { "brick", "brick/brickwall001" },
    { "plaster", "plaster/plasterwall003a" },
    { "tile", "tile/floor_tile_001a" },
    { "marble", "stone/marblefloor001b" },
    { "wood floor", "wood/woodfloor005a" },
    { "wood wall", "wood/woodwall009a" },
    { "metal floor", "metal/metalfloor001a" },
    { "metal wall", "metal/metalwall001a" },
    { "metal roof", "metal/metalroof005a" },
    { "carpet", "concrete/floor_carpet001" },
    { "white", "vgui/white" },
    { "custom", nullptr },
};
inline constexpr int k_texPresetCount  = (int)(sizeof(k_texPresets) / sizeof(k_texPresets[0]));
inline constexpr int k_texPresetCustom = k_texPresetCount - 1;
inline constexpr const char* k_texCategories[] = { "ground", "floors", "walls", "wood", "metal", "roofs / ceilings", "other" };
inline constexpr int k_texCategoryCount = 7;

// lowercase, '/' only, "maps/<map>/" cut: the map name must never pick a category (botox world_texture_normalize)
inline void TexNormalize(const char* src, char* out, size_t size)
{
    size_t n = 0;
    for (; src && src[n] && n + 1 < size; ++n) {
        const char c = src[n] == '\\' ? '/' : src[n];
        out[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[n] = '\0';
    if (std::strncmp(out, "maps/", 5) == 0)
        if (const char* rest = std::strchr(out + 5, '/')) std::memmove(out, rest + 1, std::strlen(rest + 1) + 1);
}

inline bool TexAny(const char* hay, const char* const* needles, int n)
{
    for (int i = 0; i < n; ++i) if (std::strstr(hay, needles[i])) return true;
    return false;
}

// botox world_texture_rules, first hit wins: roofs before metal ("metalroof"), floors veto "wall".
// -1 = skipped (decal/glass/sign/water/...), else the k_texCategories index ("other" if no rule hits).
inline int TexClassify(const char* matName, const char* baseTexture)
{
    static const char* const skip[] = { "decal", "overlay", "tools/", "sign", "poster", "glass", "window", "graffiti",
        "number", "marking", "stripe", "water/", "/water", "_water", "liquid", "skybox", "effects/", "vgui/", "sprite",
        "fence", "grate", "lights/", "chainlink", "foliage", "vines" };
    static const char* const roofs[]  = { "ceiling", "roof", "shingle", "awning" };
    static const char* const metal[]  = { "metal", "corrugated", "ibeam", "steel", "rollupdoor", "girder", "vent", "pipe", "rust", "tin_", "aluminum" };
    static const char* const wood[]   = { "wood", "plank", "plywood", "crate", "lumber", "pallet", "bark", "siding" };
    static const char* const gVeto[]  = { "wall", "wll", "sandstone", "brick", "sandbag" };
    static const char* const ground[] = { "grass", "dirt", "mud", "sand", "gravel", "snow", "ice0", "beach", "moss", "ground", "nature/", "soil", "hay", "farm" };
    static const char* const fVeto[]  = { "wall", "wll" };
    static const char* const floors[] = { "floor", "flr", "tile", "asphalt", "road", "sidewalk", "blacktop", "street", "pavement", "cobble",
        "cbble", "carpet", "parking", "stair", "step", "curb", "pebble", "herringbone", "marble", "tarmac", "highway" };
    static const char* const walls[]  = { "wall", "wll", "plaster", "brick", "conc", "cement", "cinder", "stone", "stucco", "adobe", "rock",
        "cliff", "trim", "pillar", "column", "building", "facade", "hieroglyph", "heiroglyph", "door", "base" };
    #define WFX3_N(a) (int)(sizeof(a) / sizeof(a[0]))
    char m[192], t[192];
    TexNormalize(matName, m, sizeof(m));
    TexNormalize(baseTexture, t, sizeof(t));
    if (TexAny(m, skip, WFX3_N(skip)) || TexAny(t, skip, WFX3_N(skip))) return -1;
    auto match = [&](const char* h) -> int {
        if (!h[0]) return -1;
        if (TexAny(h, roofs, WFX3_N(roofs))) return 5;
        if (TexAny(h, metal, WFX3_N(metal))) return 4;
        if (TexAny(h, wood, WFX3_N(wood)))   return 3;
        if (!TexAny(h, gVeto, WFX3_N(gVeto)) && TexAny(h, ground, WFX3_N(ground))) return 0;
        if (!TexAny(h, fVeto, WFX3_N(fVeto)) && TexAny(h, floors, WFX3_N(floors))) return 1;
        if (TexAny(h, walls, WFX3_N(walls))) return 2;
        return -1;
    };
    #undef WFX3_N
    int c = match(m);
    if (c < 0) c = match(t);
    return c < 0 ? k_texCategoryCount - 1 : c;
}

// botox world_decal_material: decal group, $decal flag (MATERIAL_VAR_DECAL 1<<16), decalmodulate shader,
// or a translucent/alpha-tested world material whose name says decal/overlay.
inline bool TexIsDecal(const char* group, bool decalFlag, const char* shader, bool translucentOrAlphaTest, const char* name)
{
    if (group && std::strcmp(group, "Decal textures") == 0) return true;
    if (decalFlag) return true;
    char s[64];
    TexNormalize(shader, s, sizeof(s));
    if (std::strstr(s, "decalmodulate")) return true;
    if (!group || std::strcmp(group, "World textures") != 0 || !translucentOrAlphaTest) return false;
    char n[192];
    TexNormalize(name, n, sizeof(n));
    return std::strstr(n, "decal") || std::strstr(n, "overlay");
}

// -1 = not a listed panel, else the k_hudElems index whose panel this name is.
inline int HudElemFor(const char* name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < k_hudElemCount; ++i)
        for (const char* p : k_hudElems[i].panels)
            if (p && std::strcmp(p, name) == 0) return i;
    return -1;
}

// ---- lighting modulation (UC "Lighting Modulation (Using dLights)" + light glow material tint) ----
// Lamp-looking static prop model. Keywords = the post's; "lighthouse"/"flight"/"lightning"/switches
// are not lamps, players/hostages/guns never are.
inline bool IsLampModel(const char* model)
{
    static const char* const veto[] = { "models/player", "models/characters", "models/weapons", "lighthouse",
        "flight", "highlight", "lightning", "switch" };
    static const char* const lamp[] = { "lamp", "light", "bulb", "lantern", "fixture", "neon" };
    char m[192];
    TexNormalize(model, m, sizeof(m));
    return m[0] && !TexAny(m, veto, (int)(sizeof(veto) / sizeof(veto[0]))) &&
           TexAny(m, lamp, (int)(sizeof(lamp) / sizeof(lamp[0])));
}

// Light bloom / glow / volumetric material (the post's list). Dropped from it: "beam" -- that hit our
// own tracer sprites (laserbeam/physbeam). Vetoed: the post's structural words, players/guns,
// particles and every UI path ("_glow" hud/vgui art would tint the hud).
inline bool IsLightGlowMaterial(const char* name)
{
    static const char* const veto[] = { "wall", "concrete", "brick", "metal", "wood", "floor", "ceiling", "roof",
        "building", "structure", "models/player", "models/weapons", "models/characters", "particle/", "vgui/",
        "hud/", "console/", "debug/", "dev/", "laser", "physbeam" };
    static const char* const glow[] = { "lighteffect", "sprites/glow", "sprites/light", "light_glow", "_glow",
        "spotlight", "volumetric", "lightbeam" };
    char m[192];
    TexNormalize(name, m, sizeof(m));
    return m[0] && !TexAny(m, veto, (int)(sizeof(veto) / sizeof(veto[0]))) &&
           TexAny(m, glow, (int)(sizeof(glow) / sizeof(glow[0])));
}

// The up to maxOut lamps nearest the eye within maxDist, nearest first, as indices into lamps.
// ponytail: O(n * maxOut) insertion, n = lamps seen this map (tens to low hundreds).
inline int PickLamps(const V3* lamps, int n, const V3& eye, float maxDist, int maxOut, int* out)
{
    float d2s[64];
    if (maxOut > 64) maxOut = 64;
    if (maxOut <= 0 || !lamps || !out) return 0;
    int k = 0;
    const float lim = maxDist * maxDist;
    for (int i = 0; i < n; ++i) {
        const float dx = lamps[i].x - eye.x, dy = lamps[i].y - eye.y, dz = lamps[i].z - eye.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (!(d2 <= lim)) continue;
        int j = k < maxOut ? k++ : maxOut;
        if (j == maxOut && d2 >= d2s[maxOut - 1]) continue;
        if (j == maxOut) j = maxOut - 1;
        for (; j > 0 && d2s[j - 1] > d2; --j) { d2s[j] = d2s[j - 1]; out[j] = out[j - 1]; }
        d2s[j] = d2; out[j] = i;
    }
    return k;
}

} // namespace wfx3
