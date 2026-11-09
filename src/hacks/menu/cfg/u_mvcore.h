#pragma once
// unit mvcore vars (11-03 botox-csgo tabs round): `inline bool g_x = false;`, defaults = botox csgo variables.h. Primitive types only.

// ---- bunny hop: success rate (botox m_bunny_hop_chance, variables.h:1050, def 100) ----
inline int g_bhopChance = 100;   // 0-100%, read by AutoJumpbug/bhop press roll

// ---- edge jump: "on ladders" (botox m_edge_jump_ladder, variables.h:1058, def false) ----
inline bool g_edgeJumpLadder = false;
// long jump's own edge-jump sub-toggle gets the same ladder knob (botox m_long_jump_edge_jump_ladder)
inline bool g_longJumpEdgeLadder = false;

// ---- long jump: adaptive key cancelling, ONE MultiCombo var instead of botox's 3 bools
// (m_long_jump_adaptive_stop vector<bool>[3], variables.h:1062, def all true). bit0=let go,
// bit1=landed, bit2=time passes -- MultiCombo flag order matches botox's vector order.
inline int g_longJumpAdaptiveStopMask = 0x7;

// ---- jump bug settings (botox variables.h:1070-1076) ----
// "jb only": reuses the CSS analytic predictive gate (rs::JumpbugWillHit) that used to be the
// unconditional g_jbPredict bool -- same meaning as botox's m_jump_bug_only (duck only on a
// landing edge the sim says will actually land the bug), default false like botox (def false,
// CSS had it defaulted true/always-on; botox's off state = duck on every edge, no probe).
inline bool g_jumpbugCrouch = false;      // m_jump_bug_crouch: jump while crouched on ground, mid unduck
inline bool g_jumpbugKangaroo = false;    // m_jump_bug_kangaroo: same arm, yields to edgebug + tracks jumpstat fail
inline bool g_jumpbugAssist = false;      // m_jump_bug_assist: steer xy onto the 20-22u unduck band in the air
inline int  g_jumpbugAssistTicks = 4;     // m_jump_bug_assist_ticks, 1-32, def 4

// ---- auto strafe: type + last-direction options (botox variables.h:1112-1114) ----
inline int  g_autoStrafeType = 0;         // 0 standard, 1 WASD, 2 circle (mouse)
inline bool g_autoStrafeLastKeys = false; // type==1: strafe in last pressed direction
inline bool g_autoStrafeLastMouse = false;// type==2: strafe in last mouse direction

// practice window (botox variables.h:1878-1881) -- REPLACES the old TF2-port practice.inl
// console-cheat rows (infinite ammo/health/map time/noclip). The checkpoint/teleport binds
// themselves already existed as g_prSaveKey/g_prTpKey (globals.h, practice.inl PrTeleport());
// this is only the master enable + show-window botox has and that file didn't.
inline bool g_practiceWindow = false;
inline bool g_practiceWindowShow = true;

// fake pov in demos (botox misc/fake_pov.cpp + variables.h:1883-1894). Angle-offset math lives
// in fakepov.inl (movement.cpp pipeline); the engine-side demo angle hook already exists under
// src/hacks/aim/ (G::FakePOVOffset / G::FakePOVActive, hooks.cpp CPrediction_GetLocalViewAngles)
// -- see css_bridge NEEDS FROM OTHERS in the report.
inline bool  g_fakePov = false;
inline int   g_fakePovKey = 0;
inline int   g_fakePovKeyMode = 1;          // bind style for g_fakePovKey (g_fakePovMode below is the angle combo)
inline int   g_fakePovMode = 0;             // 0 right,1 left,2 up,3 bottom,4 backwards,5 spinning
inline float g_fakePovSpinSpeed = 120.f;
inline float g_fakePovSmooth = 6.f;
inline bool  g_fakePovSnapView = false;
inline bool  g_fakePovArrow = false;
inline float g_fakePovArrowColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_fakePovArrowSize = 16.f;
inline float g_fakePovArrowDist = 90.f;

// jumpstats enable = g_jumpstats (globals.h/dllmain.cpp). botox show fails: min distance 220 -> 150
inline bool g_jumpStatsShowFails = false;   // m_jump_stats_show_fails, def false
