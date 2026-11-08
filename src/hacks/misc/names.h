#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "names_pure.h"

// custom names, fpsoverlay names.cpp + playerlist alias port:
//   aliases        per-player name (steam account id -> name), applies to anyone, you too.
//                  Added from the scoreboard's right-click "Add to Alias List" (scoreboard.cpp)
//                  or misc > player list. Wins over everything below (fpsoverlay order).
//   custom name    your own name, optional cycle list
//   streamer mode  others as class / team label / one label, optional cycle lists
// Two read points, same answer:
// C_PlayerResource::GetPlayerName (client.dll sig, ref's) -- hud, scoreboard, killfeed,
//     target id; instant (the resource's own copy refreshes from GetPlayerInfo once a second)
//   IVEngineClient::GetPlayerInfo   (game_extras.cpp h_GetPlayerInfo) -- chat, esp, this dll's
//     own scoreboard / spectator list

// fpsoverlay NameEntry_t list: count rows used, cycle = its own position
struct NameList
{
    char text[n_names::kMaxNames][64] = {};
    int  count = 0;
    n_names::Cycle cycle;
};
extern NameList g_nameList, g_teamLabelList, g_enemyLabelList, g_everyoneLabelList;

using GetPlayerName_t = const char*(__fastcall*)(void*, int);
extern GetPlayerName_t o_GetPlayerName;
const char* __fastcall h_GetPlayerName(void* self, int index);

// h_GetPlayerInfo, after the bot names patch: TF2 player_info_t (name[32] @0, friendsID @0x48)
// rewritten in place. Also caches the unmasked name per slot for the alias name hint.
void Names_PatchInfo(int index, void* pinfo);

// h_FireEvent, every event: your kill steps the cycles when trigger = on kill.
void Names_OnGameEvent(void* pEvent);

// ---- aliases ----
// account 0 = no steam id (bots / not connected): never stored
bool Names_HasAlias(uint32_t account);
void Names_AddAlias(uint32_t account, int index);   // name hint = slot's real name; no-op if present

// misc > player list page body (menu.cpp Page_MiscPlayerList)
void Names_PlayerListMenu();

// ---- auto revive targets (botox misc > player list "auto revive" column) ----
// account 0 = no steam id (bots / not connected): never a target, Names_SetReviveTarget no-ops.
bool Names_IsReviveTargetAccount(uint32_t account);
bool Names_IsReviveTarget(int index);                    // by player slot -- misc_overlays.cpp
void Names_SetReviveTarget(uint32_t account, bool on);

// ---- aimbot ignore / only aimbot (botox player_list_page's other 2 columns, misc.cpp
// player_list_aim_allowed) -- account 0 = no steam id, never a target, Set* no-ops.
bool Names_IsAimIgnoreAccount(uint32_t account);
bool Names_IsAimIgnore(int index);
void Names_SetAimIgnore(uint32_t account, bool on);
bool Names_IsAimOnlyAccount(uint32_t account);
bool Names_IsAimOnly(int index);
void Names_SetAimOnly(uint32_t account, bool on);
bool Names_AimAllowed(int index);   // the ShouldIgnore()-side gate: ignore wins, else "only" exclusivity

// fpsoverlay MenuEntryList: rows + "+ add" button for one cycle list
void Names_EntryList(const char* id, NameList& list, const char* addLabel);

// config.cpp: lists + aliases, which the flat kEntries table cannot hold
void Names_SaveCfg(FILE* f);
bool Names_LoadCfg(const char* key, const char* val);
