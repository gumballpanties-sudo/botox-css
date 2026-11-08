#pragma once
// unit mveb vars (11-03 botox-csgo tabs round): `inline bool g_x = false;`, defaults = botox csgo variables.h. Primitive types only.
//
// edgebug.inl / movement_edge.inl already own the bulk of their vars in src/dllmain.cpp +
// src/globals/globals.h (added by an earlier round, before this round's "new vars go in u_mveb"
// rule) -- those are left alone (orchestrator file, not re-declared here). This file holds only
// the vars THIS round adds: botox edgebug settings missing from the CSS port (search effort, duck
// timing/full window, lip refine, path bend, ebanat shapes, search amount) and the edge bug / edge
// skip "detection sound" + visualize knobs botox ties together that the CSS port split apart.

// ---- edgebug.cpp: search passes botox has that the CSS port never wired a knob for ----
// (botox globals/config/variables.h:1207-1216, defaults copied 1:1)
inline int   g_edgeBugSearchAmount = 2;      // m_edgebug_search_amount, 1-4 (enhanced mode's search_count knob, also edge skip's "use eb" depth)
inline float g_edgeBugSearchEffort = 1.f;    // m_edgebug_search_effort, 0.25-3: scales the per-search clock budget
inline bool  g_edgeBugDuckTiming = true;     // m_edgebug_duck_timing: PASS 1 (press/release sweep around contact) on/off
inline bool  g_edgeBugFullWindow = true;     // m_edgebug_full_window: PASS 1b (press/release at every remaining tick), duck timing only
inline bool  g_edgeBugLipRefine = true;      // m_edgebug_lip_refine: PASS 0b (zero-move push bisect onto the lip)
inline int   g_edgeBugRefineSteps = 7;       // m_edgebug_refine_steps, 1-12
inline bool  g_edgeBugPathBend = true;       // m_edgebug_path_bend: PASS 2 (bend sweep)
inline bool  g_edgeBugEbanatShapes = true;   // m_edgebug_ebanat_shapes: PASS 3 (variants 8-15)

// ---- edgebug.cpp: detection sound, botox's real combo (CSS had a TF2-port bool + id instead) ----
// (botox m_edge_bug_sound/_custom/_volume, variables.h:1181-1183). g_edgeBugSound / g_edgeBugSoundId
// (dllmain.cpp) are the old TF2-port pair -- dead once EbSoundPre (edgebug.inl) replaces
// EdgeBugSoundPre (movement_tricks2.inl, NOT this unit's file: see NEEDS FROM OTHERS in report).
inline int  g_edgeBugSoundMode = 0;         // 0 none / 1 arena switch / 2 button / 3 money / 4 beep / 5 custom
inline char g_edgeBugSoundCustom[128] = ""; // mode 5: sound/ relative path (matches u_game.h's char[] cfg convention)
inline float g_edgeBugSoundVolume = 1.f;    // 0-2

// ---- edgebug.cpp: edgebug visualize, the pad's full botox knob set (CSS only had on/off + size) ----
// (botox m_edgebug_visualize_*, variables.h:1232-1240). g_edgeBugViz / g_edgeBugVizSize already
// exist (dllmain.cpp) and keep their names ("pad size" widget); these are the rest.
inline float g_edgeBugVizColor[4] = { 1.f, 1.f, 1.f, 1.f };      // outline color
inline float g_edgeBugVizThickness = 1.5f;                       // visualize thickness
inline int   g_edgeBugVizShape = 0;                               // pad shape: 0 square / 1 circle
inline int   g_edgeBugVizFill = 0;                                 // pad fill: 0 outline / 1 filled / 2 filled+outline
inline float g_edgeBugVizFillAlpha = 0.35f;                        // fill opacity
inline float g_edgeBugVizPadColor[4] = { 1.f, 1.f, 1.f, 1.f };    // pad color (fill)
inline bool  g_edgeBugVizProjected = false;                       // project onto geometry

// ---- edge_skip.cpp: botox's own standalone knobs + "use edgebug settings" borrowing ----
// (botox m_edge_skip_*, variables.h:1168-1180). g_edgeSkip/g_edgeSkipKey/g_edgeSkipMode,
// g_edgeSkipDepth and g_edgeSkipMaxBend (dllmain.cpp) already exist and are reused as the OWN
// knobs (search_amount / max_angle) when g_edgeSkipUseEb is false. g_edgeSkipTicks (dllmain.cpp,
// a TF2-port "search window ticks" invention) is replaced by g_edgeSkipPredictTime below -- dead,
// listed under NEEDS FROM OTHERS (lives in a shared file, not this unit's to delete outright).
inline bool  g_edgeSkipUseEb = true;          // m_edge_skip_use_eb: borrow every edgebug knob below
inline float g_edgeSkipPredictTime = 0.35f;   // m_edge_skip_predict_time, 0.02-2 s (own value; borrowed = g_edgeBugPredictTime)
inline bool  g_edgeSkipMouseLock = false;
inline int   g_edgeSkipMouseLockType = 1;     // 0 smooth / 1 full
inline float g_edgeSkipMouseLockStrength = 100.f;
inline int   g_edgeSkipSoundMode = 0;         // same combo as g_edgeBugSoundMode
inline char  g_edgeSkipSoundCustom[128] = "";
inline float g_edgeSkipSoundVolume = 1.f;
inline bool  g_edgeSkipVisualize = false;
