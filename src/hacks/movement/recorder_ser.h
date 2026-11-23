#pragma once
// RECORDER, the pure half. Engine free on purpose: tests/recorder_test.cpp covers it, and
// recorder.cpp calls it instead of repeating it so the test covers the code that really runs.
// One frame = one tick's sent cmd + the position/velocity the hull had BEFORE that cmd ran
// (botox frame_t). Position drives the start check, the approach and the path line; playback
// itself is buttons/move/view only, never a teleport. NOT botox: the live half (RecFrame::live).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <algorithm>

inline constexpr int kRecFlOnGround = 1 << 0;   // m_fFlags FL_ONGROUND
inline constexpr int kRecFlDucking  = 1 << 1;   // m_fFlags FL_DUCKING (duck finished: ground move cropped 1/3)

struct RecFrame
{
    float pitch = 0.f, yaw = 0.f;        // view angles this tick
    float forward = 0.f, side = 0.f, up = 0.f;
    int   buttons = 0;
    short mouseDx = 0, mouseDy = 0;
    int   weaponDef = 0;                  // m_iItemDefinitionIndex of the held weapon
    int   weaponSlot = -1;                // its m_hMyWeapons index, -1 = unknown
    bool  hasVel = false;
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float velX = 0.f, velY = 0.f, velZ = 0.f;
    int   flags = -1;                     // botox-tf2's field (m_fFlags before the cmd). CSS never fills it: one file layout for both
    // REC4, live replay. raw = the player's own cmd before any movement feature, keys = feature binds down
    // that cmd (vk bitmap). live = a feature was armed or rewrote the cmd: playback lays raw and the same
    // features run again off the real hull. A pin (edge surf, texture bug, air stuck ...) is exact float:
    // the recorded presses only fit the recorded position, 0.005u off the start = miss
    bool  live = false;
    float rawPitch = 0.f, rawYaw = 0.f, rawForward = 0.f, rawSide = 0.f, rawUp = 0.f;
    int   rawButtons = 0;
    uint64_t keys[4] = {};
};

// IN_JUMP DUCK FORWARD BACK MOVELEFT MOVERIGHT: the buttons movement features write. Attack etc. always
// replay from the sent cmd
inline constexpr int kRecMoveMask = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 9) | (1 << 10);

// sent cmd vs raw: did the movement pipeline change it (view steer counts)
inline bool RecCmdChanged(const RecFrame& f)
{
    return f.rawPitch != f.pitch || f.rawYaw != f.yaw || f.rawForward != f.forward || f.rawSide != f.side
        || f.rawUp != f.up || ((f.rawButtons ^ f.buttons) & kRecMoveMask) != 0;
}

inline void RecKeySet(uint64_t keys[4], int vk) { if (vk > 0 && vk < 256) keys[vk >> 6] |= 1ull << (vk & 63); }
inline bool RecKeyDown(const uint64_t keys[4], int vk) { return vk > 0 && vk < 256 && ((keys[vk >> 6] >> (vk & 63)) & 1) != 0; }

// a feature bind asked while the recorder may own the cmd. state -1 = no playback (ask the keyboard),
// 0 = approach / raw frame (nothing was armed there: all off), 1 = live frame (bind mode 1 hold / 2 toggle =
// the recorded key, any other mode = always on). Returns -1 or the answer
inline int RecBindAnswer(int state, const uint64_t keys[4], int vk, int mode)
{
    if (state <= 0) return state < 0 ? -1 : 0;
    return (mode == 1 || mode == 2) ? (RecKeyDown(keys, vk) ? 1 : 0) : 1;
}

struct RecClipData
{
    std::string map;
    int tickrate = 67;                    // frames are per tick: other rate = wrong speed
    std::vector<RecFrame> frames;
};

inline constexpr uint32_t kRecMagicV2 = 0x32434552u;    // "REC2": no flags
inline constexpr uint32_t kRecMagicV3 = 0x33434552u;    // "REC3": + flags per frame
inline constexpr uint32_t kRecMagic   = 0x34434552u;    // "REC4": + live, raw input, feature keys
inline constexpr int      kRecMaxTick = 10 * 60 * 1000; // ~10 real minutes @ 1000 tps upper bound
inline constexpr size_t   kRecFrameBytesV2 = 4 * 5 + 4 + 2 * 2 + 4 + 4 + 1 + 4 * 6;
inline constexpr size_t   kRecFrameBytesV3 = kRecFrameBytesV2 + 4;
inline constexpr size_t   kRecFrameBytes   = kRecFrameBytesV3 + 1 + 4 * 5 + 4 + 8 * 4;

// Fixed layout, explicit field writes (not a struct memcpy) so padding/compiler ABI never
// matters. Header: magic, tickrate, map length + bytes, frame count. Always writes REC4.
inline bool RecSerialize(const RecClipData& clip, std::vector<uint8_t>& out)
{
    if (clip.frames.size() > (size_t)kRecMaxTick || clip.map.size() > 255) return false;
    out.clear();
    out.reserve(16 + clip.map.size() + clip.frames.size() * kRecFrameBytes);

    const auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (i * 8))); };
    const auto putF  = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(u); };
    const auto putI  = [&](int v)   { put32((uint32_t)v); };
    const auto putS  = [&](short v) { out.push_back((uint8_t)(v & 0xFF)); out.push_back((uint8_t)((v >> 8) & 0xFF)); };

    put32(kRecMagic);
    putI(clip.tickrate);
    put32((uint32_t)clip.map.size());
    out.insert(out.end(), clip.map.begin(), clip.map.end());
    put32((uint32_t)clip.frames.size());
    for (const RecFrame& f : clip.frames)
    {
        putF(f.pitch); putF(f.yaw);
        putF(f.forward); putF(f.side); putF(f.up);
        putI(f.buttons);
        putS(f.mouseDx); putS(f.mouseDy);
        putI(f.weaponDef); putI(f.weaponSlot);
        out.push_back(f.hasVel ? 1 : 0);
        putF(f.posX); putF(f.posY); putF(f.posZ);
        putF(f.velX); putF(f.velY); putF(f.velZ);
        putI(f.flags);
        out.push_back(f.live ? 1 : 0);
        putF(f.rawPitch); putF(f.rawYaw);
        putF(f.rawForward); putF(f.rawSide); putF(f.rawUp);
        putI(f.rawButtons);
        for (const uint64_t k : f.keys) { put32((uint32_t)k); put32((uint32_t)(k >> 32)); }
    }
    return true;
}

inline bool RecDeserialize(const std::vector<uint8_t>& in, RecClipData& out)
{
    out = RecClipData{};
    size_t p = 0;
    const auto have  = [&](size_t n) { return in.size() - p >= n; };
    const auto get32 = [&]() -> uint32_t
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= (uint32_t)in[p + i] << (i * 8);
        p += 4;
        return v;
    };
    const auto getF = [&]() -> float { uint32_t u = get32(); float v; std::memcpy(&v, &u, 4); return v; };
    const auto getI = [&]() -> int   { return (int)get32(); };
    const auto getS = [&]() -> short { short v = (short)(in[p] | (in[p + 1] << 8)); p += 2; return v; };

    if (!have(12)) return false;
    const uint32_t magic = get32();
    if (magic != kRecMagic && magic != kRecMagicV3 && magic != kRecMagicV2) return false;
    const bool v4 = magic == kRecMagic, v3 = v4 || magic == kRecMagicV3;
    const size_t frameBytes = v4 ? kRecFrameBytes : v3 ? kRecFrameBytesV3 : kRecFrameBytesV2;
    out.tickrate = getI();
    const uint32_t mapLen = get32();
    if (mapLen > 255 || !have(mapLen + 4)) return false;
    out.map.assign((const char*)in.data() + p, mapLen);
    p += mapLen;
    const uint32_t count = get32();
    if (count > (uint32_t)kRecMaxTick || !have((size_t)count * frameBytes)) return false;

    out.frames.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        RecFrame f{};
        f.pitch = getF(); f.yaw = getF();
        f.forward = getF(); f.side = getF(); f.up = getF();
        f.buttons = getI();
        f.mouseDx = getS(); f.mouseDy = getS();
        f.weaponDef = getI(); f.weaponSlot = getI();
        f.hasVel = in[p++] != 0;
        f.posX = getF(); f.posY = getF(); f.posZ = getF();
        f.velX = getF(); f.velY = getF(); f.velZ = getF();
        if (v3) f.flags = getI();
        if (v4)   // older clip: live stays false = raw replay, as before
        {
            f.live = in[p++] != 0;
            f.rawPitch = getF(); f.rawYaw = getF();
            f.rawForward = getF(); f.rawSide = getF(); f.rawUp = getF();
            f.rawButtons = getI();
            for (uint64_t& k : f.keys) { const uint64_t lo = get32(); k = lo | ((uint64_t)get32() << 32); }
        }
        out.frames.push_back(f);
    }
    return true;
}

// ---- playback math (botox movement_recorder.cpp, 1:1) ------------------------------------

inline float RecWrap180(float a)
{
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

inline float RecSmoothstep(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// botox yaw_offset. mode 0 original, 1 left (+90), 2 right (-90), 3 backwards, 4 spinning.
// Smooth end: the offset eases to 0 (spin decelerates to a stop) over the last smoothEndTicks.
inline float RecYawOffset(int mode, size_t frameCount, size_t idx, float smoothEndTicks, float spinPerTick)
{
    if (mode <= 0 || frameCount == 0) return 0.f;
    const float n = (float)(frameCount - 1);
    const float m = std::clamp(smoothEndTicks, 0.f, n);
    const float i = std::min((float)idx, n);

    if (mode == 4)
    {
        const float e = n - m;
        if (m < 1.f || i <= e) return spinPerTick * i;
        const float d = i - e;
        return spinPerTick * (e + d - d * d / (2.f * m));
    }
    const float base = mode == 1 ? 90.f : mode == 2 ? -90.f : 180.f;
    return m < 1.f ? base : base * RecSmoothstep((n - i) / m);
}

// botox clip_route: playback walks onto frame 0 and stands, so a clip has to start where you
// stood or the replay drifts. First still frame (2D speed and |vz| under 5), or size() if none.
inline size_t RecStillStart(const std::vector<RecFrame>& frames)
{
    for (size_t i = 0; i + 1 < frames.size(); ++i)
    {
        const RecFrame& f = frames[i];
        if (f.hasVel && std::sqrt(f.velX * f.velX + f.velY * f.velY) < 5.f && std::fabs(f.velZ) < 5.f)
            return i;
    }
    return frames.size();
}

// botox approach step: move toward (tx,ty) from (cx,cy) in the frame of view yaw (deg),
// gain 20 per unit of distance (the engine clamps to max speed).
inline void RecApproachMove(float cx, float cy, float tx, float ty, float yawDeg, float& fwd, float& side)
{
    const float dx = cx - tx, dy = cy - ty;
    const float r = yawDeg * 3.14159265358979323846f / 180.f;
    const float c = std::cos(r), s = std::sin(r);
    fwd  = -(dx * c + dy * s) * 20.f;
    side = (dy * c - dx * s) * 20.f;
}
