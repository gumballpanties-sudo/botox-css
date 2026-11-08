#pragma once
#include <cstddef>
// unit misctab vars (11-03 botox-csgo tabs round): `inline bool g_x = false;`, defaults = botox csgo variables.h. Primitive types only.

// ---- clantag (botox m_clantag*, variables.h:1832 defaults). Sender = engine.dll cl_clanid
// KeyValues("ClanTagChanged") func, server takes any text (game_extras.cpp Clantag_Frame).
// Frames: botox keeps "text\thold\n..."; cfg STR is one line, so '|' splits frames here.
inline bool  g_clantag          = false;
inline char  g_clantagText[25]  = "botox";
inline int   g_clantagAnim      = 0;     // 0 static,1 typewriter,2 scroll,3 frame by frame,4 now playing
inline float g_clantagSpeed     = 1.f;
inline float g_clantagHold      = 2.f;
inline char  g_clantagFrames[1024] = "b|bo|bot|boto|botox";

// ---- hit sound (botox m_hit_sound*) ------------------------------------------------------
// 0 arena switch / 1 button / 2 money / 3 beep / 4 custom. Stock paths = pixelsurf.inl's
// already-verified-present-in-CSS-vpk picks (same family, different slots so all 4 are distinct).
// "play" has no volume arg (Source console cmd): g_hitSoundVolume is stored but not yet
// applied, same known gap pixelsurf.inl / movement_tricks2.inl already flagged (NEEDS FROM OTHERS).
inline bool  g_hitSound       = false;
inline int   g_hitSoundType   = 0;
inline char  g_hitSoundCustom[128] = "";
inline float g_hitSoundVolume = 1.f;

// ---- auto revive (botox m_auto_revive*): "sm_respawn #<userid>" on player_death, needs a
// SourceMod-admin server (same dependency botox's own client_cmd call has on CS:GO). Modes 2/3
// targets = misc > player list "auto revive" column (names.cpp Names_IsReviveTarget). ----------
inline bool g_autoRevive     = false;
inline int  g_autoReviveMode = 0;   // 0 everyone, 1 yourself, 2 only targets, 3 yourself+targets

// ---- auto buy (botox m_auto_buy*), CSS 1.6-era weapon set: no ssg08/mp7/mp9/bizon/nova/
// sawedoff/mag7/m249/negev/tec9/dual-cz/auto-sniper (cs_weapon_parse.cpp has none of them).
// round_start arms it, AutoBuy_Tick gates every try on m_bInBuyZone/m_ArmorValue/m_bHasHelmet/
// m_bHasDefuser (local netvar resolve, misc_overlays.cpp). Only remaining gap: no per-weapon
// "already own it" skip (needs a GetClientClass() vtable slot, unverified -- harmless, the
// server's own buy command is idempotent).
inline bool g_autoBuy          = false;
inline int  g_autoBuyPrimary   = 0;    // 0 none,1 ak47/m4a1,2 awp,3 scout,4 sg552/aug,5 galil/famas,6 mac10/tmp,7 ump45,8 p90,9 m3,10 xm1014
inline int  g_autoBuySecondary = 0;    // 0 none,1 p228,2 deagle,3 elite/fiveseven
inline int  g_autoBuyArmor     = 0;    // 0 none,1 vest,2 vest+helmet
inline int  g_autoBuyGrenades  = 0;    // bit0 he,1 one flash,2 two flashes,3 smoke (no molotov/decoy in CSS)
inline int  g_autoBuyExtras    = 0;    // bit0 defuser (no zeus in CSS)

// ---- performance: botox "skip unused bone records" -- gates backtrack's TickRecord bone
// copy (aim/backtrack.cpp m_aRecords), not wired here: NEEDS FROM OTHERS (AIMTAB owns backtrack.cpp).
inline bool g_perfSkipUnusedRecords = false;

// ---- chat translator libre fallback (botox m_chat_translator_libre_url / _key) ------------
inline char g_chatLibreUrl[128] = "http://localhost:5000";
inline char g_chatLibreKey[128] = "";

// ---- chat images (botox m_chat_images): CS:GO SayText2 lines carry no inline image markup
// in CSS's cs_hud_chat.cpp (plain localized text only) -- see report N/A.

// ---- random rank + medal (botox m_bot_names_profile): CS:GO scoreboard draws a competitive
// rank badge + medal icon per bot; CSS scoreboard (hud/scoreboard.cpp, no Panorama) draws
// neither -- see report N/A.
