#pragma once
#include <cstdint>
#include <string>

// Unit E's pure logic, split out so it is testable without the engine/winrt/imgui headers
// every feature file above it pulls in -- same shape as mp_art.h/mp_time.h for the media
// player. Gate test: tests/unitE_test.cpp.
namespace n_unitE {

// CBaseHandle decode, networked-ehandle convention already used in this codebase
// (movement.cpp:89, "ENT_ENTRY_MASK (16 bit)"; source-sdk-2013-master/public/basehandle.h
// confirms GetEntryIndex() == m_Index & 0xFFFF for an already-decoded in-memory handle).
// -1 = invalid handle or out-of-range index.
inline int DecodeEHandleIndex(unsigned int raw)
{
    if (raw == 0xFFFFFFFFu) return -1;
    const int idx = (int)(raw & 0xFFFF);
    return (idx >= 1 && idx <= 2048) ? idx : -1;
}

// Discord IPC JSON payloads are flat strings from user config text -- '"' and '\\' get
// escaped, control bytes dropped (botox discord_rpc.cpp json_escape 1:1).
inline std::string JsonEscape(const char* s)
{
    std::string out;
    for (const char* p = s; p && *p; ++p)
    {
        if (*p == '"' || *p == '\\') out += '\\';
        if ((unsigned char)*p >= 0x20) out += *p;
    }
    return out;
}

// IVEngineClient::GetLevelName gives "maps/ctf_2fort.bsp"; botox shows the short name
// (CS:GO get_level_name_short) -> strip the dir and the extension.
inline std::string LevelNameShort(const char* s)
{
    std::string n = s ? s : "";
    const size_t slash = n.find_last_of("/\\");
    if (slash != std::string::npos) n.erase(0, slash + 1);
    const size_t dot = n.rfind('.');
    if (dot != std::string::npos) n.erase(dot);
    return n;
}

// Linear fade fraction, 1 at spawn counting down to 0 at life -- hit marker/death
// particles/score popup all use the same shape.
inline float FadeFrac(float tRemaining, float life)
{
    if (life <= 0.f) return 0.f;
    float f = tRemaining / life;
    return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}

// botox spawn_mc_xp: XP orb value bucketing (ExperienceOrb 1:1, mc_xp_value/mc_xp_icon in
// kill_effects.cpp). Deterministic int->int, moved here so it is testable without the
// particle pool / ImGui draw list around it.
inline int McXpValue(int xp)
{
    for (int step : { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3 })
        if (xp >= step) return step;
    return 1;
}
inline int McXpIcon(int value)
{
    int icon = 10;
    for (int step : { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3 })
    {
        if (value >= step) return icon;
        icon--;
    }
    return 0;
}

// interwebz watermark: botox draws it at a fixed point in its 640x480 "stretch block"
// virtual canvas (250,36). TF2 has no such canvas (render.cpp draws straight in
// io.DisplaySize px), so the fixed point is scaled proportionally instead -- same
// relative spot on any resolution rather than a hardcoded low-res pixel.
inline void ScaleVirtual640x480(float screenW, float screenH, float vx, float vy, float& outX, float& outY)
{
    outX = vx / 640.f * screenW;
    outY = vy / 480.f * screenH;
}

} // namespace n_unitE
