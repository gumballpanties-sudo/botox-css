#pragma once
// unit movx vars (11-08 missing-features round): `inline bool g_x = false;`, defaults = botox
// csgo-sdk variables.h. Primitive types only. (air freeze removed 11-08 by user.)
//   ps wall strafe  botox movement.cpp:3535 (m_pixel_surf_wall_strafe), variables.h:1082
//   edgebug lob     botox edgebug.cpp lob_post/lob_check/lob_strafe (style 4 "lobotomy v2"),
//                   variables.h:1238-1245, menu_tab_movement.cpp:460,489-501
// wall climb pin catch / spot reach: N/A for CSS -- see report (wallclimb.inl's own header
// comment documents the csgo pin model as deliberately dropped for CSS's ground-based catch).

// ---- pixel surf: botox's own toggle for the wall-strafe gain path pixelsurf.inl::hold() already
// runs unconditionally (CSS WallStrafeYaw, as::WallStrafeYaw). default true = botox's default.
inline bool g_pixelSurfWallStrafe = true;

// ---- edgebug lobotomy v2 (style 4): a whole alternate search+replay path, botox's donor-style
// "lob" search. Settings only appear while that style is selected (menu_tab_movement.cpp).
inline bool  g_edgeBugLobAdvanced   = false;   // search variants 2 -> 4 (adds turn/strafe pair)
inline bool  g_edgeBugLobSilent     = false;   // CorrectMovement re-expresses the probe view, sent view never turns
inline bool  g_edgeBugLobMega       = false;   // "extra advanced search": variants -> 6, strafe_angle forced 179, delta_scale forced 1
inline bool  g_edgeBugLobAutostrafe = false;   // variants 2/3 drive EbLobStrafe instead of a plain view turn
inline float g_edgeBugLobStrafeAngle = 0.f;    // deg, 1-179: "is this turn too big to keep searching" cutoff
inline int   g_edgeBugLobDeltaType  = 0;       // 0 1x / 1 0.8x / 2 0.5x / 3 0.25x -- scales the view delta carried between cmds
inline int   g_edgeBugLobTicks      = 1;       // 0-126: ticks replayed per search variant
inline float g_edgeBugLobLock       = 0.f;     // 0-10: mouse-scale strength while a lob plan is flying
