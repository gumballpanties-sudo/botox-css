#pragma once
// unit mvtb vars (11-03 botox-csgo tabs round): `inline bool g_x = false;`, defaults = botox csgo variables.h. Primitive types only.
//
// texturebug.inl / ladder.inl / fireman.inl / airstuck.inl already own the bulk of their vars in
// src/dllmain.cpp + src/globals/globals.h (added by an earlier round, before this round's "new
// vars go in u_mvtb" rule) -- those stay there (orchestrator files, not re-declared here). This
// file holds only the vars THIS round adds.


// ---- texturebug.cpp: types allowed / auto crouch / wallstrafe, botox's real knobs (CSS's own
// catch logic already sim-decides stance itself -- see dllmain.cpp:582 comment -- so these are
// wired as real, persisted settings but NOT yet threaded through every duck/speed decision site
// in texturebug.inl's ~3000-line state machine; see MVTB report "NOT FULLY WIRED") ----
// (botox m_texture_bug_types/_auto_duck/_wallstrafe, variables.h:1245-1247)
inline int  g_tbTypesMask = 3;      // bit0 ducking, bit1 standing (both default true, botox's own "never both off")
inline bool g_tbAutoDuck = true;    // m_texture_bug_auto_duck
inline bool g_tbWallstrafeOpt = false; // m_texture_bug_wallstrafe (named *Opt: g_tbWallstrafe would collide
                                        // with nothing today, but keeps this clearly the botox-settings copy)

// ---- texturebug.cpp: detection sound, botox's real combo (CSS never had one) ----
// (botox m_texture_bug_sound/_custom/_volume, variables.h:1248-1250). Same stock-CSS sound
// mapping / EbPlaySound helper edgebug.inl (MVEB) already wired for its own detection sound --
// reused here, not reimplemented (edgebug.inl is included before texturebug.inl in movement.cpp).
inline int   g_tbSound = 0;            // 0 none / 1 arena switch / 2 button / 3 money / 4 beep / 5 custom
inline char  g_tbSoundCustom[128] = ""; // mode 5: sound/ relative path
inline float g_tbSoundVolume = 1.f;    // 0-2 (stored, no engine volume path yet -- same known gap EbPlaySound ships with)

// ---- airstuck.inl: cfg rows for vars already defined in dllmain.cpp/globals.h (orchestrator
// asked these be listed here since config.cpp can't take new rows from this unit this wave --
// see u_mvtb.inc). No new globals, just the cfg wiring.
