#pragma once
#include <atomic>
#include <cstdint>

// Unit hud (11-01): what the botox hud group draws, read on the GAME thread by aim/hud_aim.cpp
// (IEngineVGui::Paint, where the aim SDK's typed interfaces live) and drawn on whichever thread
// builds the ImGui frame (hud_extra.cpp, crosshair.cpp). BuildImGuiFrame can run on
// the render thread under multicore, which must not walk entities, so the game thread copies the
// numbers here and the render side only reads this. POD only: shared by the aim TU (no globals.h).

struct HudSnap
{
    uint64_t stampMs = 0;          // 0 / stale (>250 ms) = draw nothing
    float    srcW = 0, srcH = 0;   // engine screen px the pixel values below were taken in
    bool     alive = false;

    // aimbot fov circle: radius in engine px, 0 = nothing to draw
    float fovRadius = 0.f;

    // custom net graph (botox CNetGraphPanel::GetFrameData + DrawTextFields inputs)
    int   netGraph = 0, netGraphPos = 1, netGraphHeight = 64;
    float fps = 0.f, frameVarMs = 0.f, latency = 0.f, loss = 0.f, choke = 0.f;
    float inData = 0.f, outData = 0.f, inPackets = 0.f, outPackets = 0.f;
    int   inBytes = 0, outBytes = 0;
    float svTime = 0.f, svDev = 0.f;
    float updateRate = 0.f, cmdRate = 0.f, tickInterval = 0.f, interp = 0.f;
    char  serverType[16] = {};

    // "fade under smoke" (SCREEN, 11-04): nearest active smoke cloud, read on the render
    // thread by ssao.cpp. 0 radius = none active. World-space, not screen-space, like the
    // other fields above it is per-tick on the game thread (hud_aim.cpp CollectSmoke).
    float smokeCenter[3] = { 0.f, 0.f, 0.f };
    float smokeRadius = 0.f;
};

inline std::atomic_flag g_hudSnapLock = ATOMIC_FLAG_INIT;
inline HudSnap g_hudSnap;   // under g_hudSnapLock

struct HudSnapGuard
{
    HudSnapGuard()  { while (g_hudSnapLock.test_and_set(std::memory_order_acquire)) {} }
    ~HudSnapGuard() { g_hudSnapLock.clear(std::memory_order_release); }
};

// render side: a copy, or false when the game thread has not written one in 250 ms
inline bool HudSnap_Read(HudSnap& out, uint64_t nowMs)
{
    {
        HudSnapGuard g;
        out = g_hudSnap;
    }
    return out.stampMs && nowMs - out.stampMs < 250;
}
