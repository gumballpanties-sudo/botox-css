#pragma once

// Shared engine FrameStageNotify hook. Used to live in hacks/visuals/skins.cpp (it claimed the
// hook first; MinHook allows only one hook per target); moved out when the CSS skinchanger that
// owned the file was removed entirely (no skins in CSS). Every rider below is unchanged.
bool Fsn_Install();   // engine.dll FrameStageNotify (thread.cpp)
