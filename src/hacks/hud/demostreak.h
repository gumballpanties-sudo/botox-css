#pragma once
// Killstreak carry-over across demos. Same bytes in dih and fpsoverlay (src/hacks/hud/demostreak.h)
// -- edit one, copy to the other. Tested by tests/demostreak_test.cpp.
//
// A frag video runs one streak through several demos, so loading a demo must not end it. A load
// creates the recorded player, which fires localplayer_changeteam / localplayer_changeclass --
// shared Source game events, not TF2-specific (CS:S's own shared/achievementmgr.cpp:326-327
// listens for both by the same names), and dih also sees the old session drop ("left server");
// all three used to reset the count. Inside a demo only the recorded player's own death ends a
// streak now.
//
// The catch is a rewind: the engine rewinds by loading the SAME file again and skipping up to the
// tick, so every kill before that tick fires a second time. The streak the demo was loaded with is
// kept aside, and a reload of the same demo (same map + length, playback tick gone backwards) puts
// it back before the replayed kills count again.
#include <unordered_map>
#include <cstring>
#include <cstdio>

namespace ds {

using Streaks = std::unordered_map<int, int>;   // config key -> kills, skins.cpp StreakByKey()

struct Demo {
    bool    active   = false;  // the streak belongs to a demo; cleared by any streak work in live play
    char    level[128] = {};   // which demo: its map name ...
    int     length   = 0;      // ... and its total ticks
    int     lastTick = 0;      // playback tick at the last Sync
    Streaks loaded;            // the streak this demo was loaded with
};

enum class Load { Live, Same, NewDemo, Rewind };

// Call before every change to `streak`. playing / level / length / tick are IVEngineClient's
// IsPlayingDemo / GetLevelName / GetDemoPlaybackTotalTicks / GetDemoPlaybackTick. Edits `streak`
// in place on a new demo (out of live play) and on a rewind.
inline Load Sync(Demo& d, bool playing, const char* level, int length, int tick, Streaks& streak)
{
    if (!playing) { d.active = false; return Load::Live; }
    if (!level) level = "";
    if (!d.active || d.length != length || std::strcmp(d.level, level) != 0) {
        // Out of another demo the streak carries on. Out of live play (or the first demo since
        // injecting) it starts from nothing, as a server join does.
        if (!d.active) streak.clear();
        d.active = true;
        std::snprintf(d.level, sizeof(d.level), "%s", level);
        d.length   = length;
        d.lastTick = tick;
        d.loaded   = streak;
        return Load::NewDemo;
    }
    if (tick < d.lastTick) {
        d.lastTick = tick;
        streak     = d.loaded;
        return Load::Rewind;
    }
    d.lastTick = tick;
    return Load::Same;
}

// Does a reset end the streak? In live play every reset does; in a demo only a death.
inline bool ResetEnds(Load l, bool death) { return l == Load::Live || death; }

} // namespace ds
