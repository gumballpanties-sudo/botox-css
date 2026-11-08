#pragma once
// unit game vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.

// botox m_chat_translator / m_chat_translator_language (index into kChatLanguageNames)
inline bool g_chatTranslator     = false;
inline int  g_chatTranslatorLang = 0;

// botox m_bot_names*. mode 0 original, 1 same name, 2 random string, 3 steam names.
// "random rank + medal" has no TF2 counterpart (scoreboard draws no rank/medal), not ported.
inline bool g_botNames         = false;
inline int  g_botNamesMode     = 3;
inline char g_botNamesSame[32] = "botox";   // TF2 player_info_t name is 32 bytes (botox: 128)
inline int  g_botNamesLength   = 8;
inline bool g_botNamesNoPrefix = true;
inline bool g_botNamesPing     = false;

// custom names, fpsoverlay names.cpp 1:1 (hacks/misc/names.cpp; same cfg keys). Streamer mode
// 0 off, 1 others as class, 2 team / enemy label, 3 one label -- never masks you. Cycle lists
// (names.h NameList) + per-player aliases live in names.cpp's own cfg block. Trigger 0 timer
// (interval s, game time), 1 on your kill; shared by every cycle.
inline int   g_streamerMode              = 0;
inline char  g_streamerTeamLabel[64]     = "teammate";
inline char  g_streamerEnemyLabel[64]    = "enemy";
inline char  g_streamerEveryoneLabel[64] = "player";
inline bool  g_useCustomUsername         = false;
inline char  g_customUsername[64]        = "";
inline bool  g_enableNameCycle           = false;
inline bool  g_enableTeamLabelCycle      = false;
inline bool  g_enableEnemyLabelCycle     = false;
inline bool  g_enableEveryoneLabelCycle  = false;
inline int   g_nameTrigger               = 0;
inline float g_nameInterval              = 5.f;

// botox m_performance_* rows (master = g_perfEnable, dllmain). skip unused bone records: no
// tick records in this build (backtrack removed 11-01), not ported.
inline bool g_perfThreadedBones = true;
inline bool g_perfForcePreload  = true;
inline bool g_perfMulticore     = true;
inline bool g_perfHighPriority  = false;
inline bool g_perfFastCores     = true;
inline bool g_perfNoFocusSleep  = true;

// botox m_menu_blur / _amount / _darken (percent)
inline bool  g_menuBlur       = false;
inline float g_menuBlurAmount = 100.f;
inline float g_menuBlurDarken = 18.f;

// botox m_cvar_list (misc > cvars): one command per row, run top to bottom on map load /
// server join. Slots, not botox's one '\n' string: config.cpp is one key=value per line.
constexpr int kCvarRows = 16;
inline char g_cvarRows[kCvarRows][128] = {};
