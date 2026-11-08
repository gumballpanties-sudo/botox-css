#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

// Unit C2 cross-thread ESP extra: sound pings (CSoundEmitterSystem_EmitSound hook, visuals.cpp),
// pushed from the GAME thread;
// esp.cpp's Draw() runs off the render-thread IEngineVGui::Paint hook -- same cross-thread shape
// flipworld.cpp's SRWLOCK core already solves in this tree. A spinlock is used here instead so
// this header stays <atomic>/<chrono> only (no <windows.h>), since it is included from both
// dih-native TUs and the aim/ typed SDK world (which esp.h's own banner comment says must not
// see dih's globals.h / windows types). Critical sections are a few float writes -- bounded,
// so a spin is fine.
//
// The render thread NEVER reads live entities here: the push side snapshots position into this
// fixed array, and Draw() reads only that copy. Player esp itself goes through esp_queue.h.

struct EspFxSoundPing { float x, y, z, bornMs; bool used; };

inline constexpr int kEspFxMaxSoundPings = 16;

inline std::atomic_flag  g_espFxLock = ATOMIC_FLAG_INIT;
inline EspFxSoundPing    g_espFxSoundPings[kEspFxMaxSoundPings] = {};
inline int               g_espFxSoundNext  = 0;

struct EspFxLockGuard
{
    EspFxLockGuard()  { while (g_espFxLock.test_and_set(std::memory_order_acquire)) {} }
    ~EspFxLockGuard() { g_espFxLock.clear(std::memory_order_release); }
};

inline float EspFx_NowMs()
{
    using namespace std::chrono;
    return (float)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline void EspFx_PushSoundPing(float x, float y, float z)
{
    EspFxLockGuard _g;
    g_espFxSoundPings[g_espFxSoundNext] = { x, y, z, EspFx_NowMs(), true };
    g_espFxSoundNext = (g_espFxSoundNext + 1) % kEspFxMaxSoundPings;
}

// Draw-side: copies out everything still alive (age < lifetimeMs) under the lock. The render
// thread never walks the shared arrays directly.
inline int EspFx_CopySoundPings(EspFxSoundPing* out, int maxOut, float nowMs, float lifetimeMs)
{
    int n = 0;
    EspFxLockGuard _g;
    for (int i = 0; i < kEspFxMaxSoundPings && n < maxOut; ++i)
        if (g_espFxSoundPings[i].used && (nowMs - g_espFxSoundPings[i].bornMs) < lifetimeMs)
            out[n++] = g_espFxSoundPings[i];
    return n;
}

