#pragma once
// unit mvps vars (11-03 botox-csgo tabs round): `inline bool g_x = false;`, defaults = botox csgo variables.h. Primitive types only.

// pixel surf detection sound -- botox m_pixel_surf_sound / _sound_custom / _sound_volume
// (variables.h:1088-1090). 0 none / 1 arena switch / 2 button / 3 money / 4 beep / 5 custom.
inline int   g_psSound = 0;
inline char  g_psSoundCustom[128] = {};
inline float g_psSoundVolume = 1.f;

// "pixel jumps over pixelsurfs" -- botox m_auto_align_pixel_jump (variables.h:1164), def true.
// Consumer (auto_align.cpp:535-551 lip-creep ahead of a surf) not ported yet -- CSS AutoAlign()
// (movement.cpp) is a from-scratch MV_DetectWallLines rewrite with no lip-aware detect pass to
// gate; see report NEEDS FROM OTHERS / UNVERIFIED. Var exists so the auto align settings popup
// and cfg round-trip correctly meanwhile.

// pixel finder sub-toggles missing vs botox variables.h:1331-1335 (checked PSPORT 11-04):
// "pixelsurfs" had no standalone var at all (always on while the finder ran) and "map
// texturebugs" reused g_psFinderTb (psassist.inl's column-scan toggle), so toggling the
// scan's texturebug pass also silently flipped the map scan's. Split, botox defaults.
inline bool g_psFinderPixelsurf = true;  // botox m_pixel_finder_pixelsurfs, def true
inline bool g_psFinderMapTb     = false; // botox m_pixel_finder_map_texturebugs, def false

// pixelsurf assist (PSPORT round 2 11-04): botox pixelsurf_assist.cpp's own vars, never ported
// before -- the old psassist.inl design (PsSimHop/JT_*) had no ticks/type/stamina-hop/ground-
// angle-limit/render-height controls at all (g_psAssist/g_psAssistKey/g_psAssistMode/
// g_psSetPointKey/g_psDeletePointKey/g_psAssistSnap/g_psAssistRender/g_psGroundHelp already
// existed as globals.h externs and are reused unchanged). variables.h:1120-1339.
inline int   g_psAssistTicks  = 12;    // botox m_pixel_surf_assist_ticks
inline int   g_psAssistType   = 0;     // botox m_pixel_surf_assist_type: 0 standard, 1 delta strafe
inline bool  g_psAssistBrokeHop = false; // botox m_pixel_surf_assist_brokehop ("stamina hops")
inline float g_psGroundAngleLimit = 2.5f; // botox m_pixel_surf_assist_ground_angle_limit, 0-45
inline float g_psAssistRenderHeight = 0.f; // botox m_pixel_surf_assist_render_height, 0-100
