#pragma once
#include <cstdint>

// Timeline math for the media player, on its own so the gate test
// (tests/mediaplayer_test.cpp) can have it without winrt.
//
// THE BUG THIS EXISTS FOR: smc's Position is a SNAPSHOT dated LastUpdatedTime, not a live
// counter. A player pushes one timeline update per seek / play / pause / track change and
// expects the reader to extrapolate from there -- Chrome and Firefox go minutes between
// pushes on a video. Reading Position once a second and dating it NOW is what pinned the bar:
// every poll yanked it back to however stale the last push was, so it crawled, stalled, or sat
// still on a video whose length it did appear to know.
//
// Same function runs on both sides. The poll thread calls it with the winrt fields and the age
// of the push; the render thread calls it again with the already-resolved track and the age of
// the poll, which is what keeps the bar sliding between the 1s polls.
namespace mp {

struct Track {
    long long curMs, totalMs;   // totalMs <= 0 -> no length known, draw no fill
};

// A push older than this is not a stale position, it is a player that reported a bogus
// timestamp (or a machine that changed clock). Extrapolating it would slam the bar to full.
static constexpr long long kMaxAgeMs = 6LL * 60 * 60 * 1000;

// All arguments in ms. ageMs = wall clock since the position was sampled. rate = playback
// speed (youtube 2x, a podcast at 1.5x); anything outside (0, 16] is a player lying.
inline Track TrackAt(long long startMs, long long endMs,
                     long long minSeekMs, long long maxSeekMs,
                     long long posMs, long long ageMs, double rate, bool playing)
{
    // EndTime is the length for everything that fills it. Players that only fill the seek
    // window (some casters, some browsers on a live-ish stream) have the length in there
    // instead, and without this fallback the bar has no total at all and never moves.
    long long total = endMs - startMs;
    if (total <= 0) total = maxSeekMs - minSeekMs;
    if (total <= 0) return { 0, 0 };

    long long cur = posMs - startMs;
    if (playing && ageMs > 0 && ageMs < kMaxAgeMs) {
        if (!(rate > 0.0) || rate > 16.0) rate = 1.0;
        cur += (long long)((double)ageMs * rate);
    }
    if (cur < 0) cur = 0;
    if (cur > total) cur = total;   // a timeline left over from the previous track runs long
    return { cur, total };
}

inline float ProgressOf(const Track& t)
{
    if (t.totalMs <= 0) return 0.f;
    const float f = (float)((double)t.curMs / (double)t.totalMs);
    return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}

} // namespace mp
