#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// Unit E5: which GIF frame is showing at `elapsedMs` into playback, given each frame's
// delay in ms and infinite looping. Split out of gif_tex.h/.cpp so it is testable without
// GDI+ or d3d9 -- same shape as mp_time.h/mp_art.h for the media player.
// Gate test: tests/giftex_test.cpp.
namespace n_gif {

// A GIF timed at exactly 0 (NETSCAPE loop block, no delay) would otherwise divide by zero
// or spin forever; one tick keeps it moving without a visible stall.
inline uint32_t FrameDelayMs(uint32_t d) { return d ? d : 1; }

inline size_t FrameIndexAt(const std::vector<uint32_t>& delaysMs, double elapsedMs)
{
    if (delaysMs.empty()) return 0;

    uint64_t total = 0;
    for (uint32_t d : delaysMs) total += FrameDelayMs(d);
    if (total == 0) return 0;

    double t = elapsedMs < 0.0 ? 0.0 : elapsedMs;
    const uint64_t tMod = (uint64_t)t % total;   // loop wrap

    uint64_t acc = 0;
    for (size_t i = 0; i < delaysMs.size(); ++i)
    {
        acc += FrameDelayMs(delaysMs[i]);
        if (tMod < acc) return i;
    }
    return delaysMs.size() - 1;   // rounding leftover -- last frame, never past the end
}

} // namespace n_gif
