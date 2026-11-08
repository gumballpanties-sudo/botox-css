#pragma once
// Public face of the aim subsystem (src/hacks/aim). Everything dih's own TUs may call is here.
//
// The subsystem has its own SDK layer, hook table and feature tree, but no load loop of its
// own: dih owns the DLL, so Core is a stub (src/hacks/aim/core.h) and Aim_Init below is the load
// path -- called once from dih's PollThread after client.dll and engine.dll are up.
//
// Nothing here is aimbot logic. It is initialisation order and a hook whitelist.
//
// Kept deliberately free of every aim header so dih's own TUs (thread.cpp, menu.cpp) can
// include it without pulling in SDK.h / Vars.h.

// Brings up signatures, interfaces, the hook whitelist, the event listener, convar unlock and
// the saved config. Safe to call more than once; only the first call does anything.
// Returns true when the aimbot pipeline is live.
bool Aim_Init();

// True once Aim_Init() has succeeded. dih's thread.cpp reads this to decide whether to
// install its OWN CHLClient::CreateMove hook: when the aim half is up, that hook belongs
// to the CHLClient_CreateMove hook in src/hacks/aim/hooks.cpp, which calls dih's
// Movement_Run itself.
bool Aim_Active();

// Print a line to the game chat (\x07RRGGBB color codes). No-op until interfaces resolve.
// Game thread only. `text` is the ChatPrintf format, so it must not contain '%'.
void Aim_ChatPrint(const char* text);

// Disables the whitelisted hooks. Called from dih's eject path before MH_Uninitialize.
void Aim_Unload();

// Aim-var persistence, folded into dih's OWN config file (src/hacks/aim/aim.cpp).
// src/hacks/menu/config.cpp calls these from SaveConfig / LoadConfig, so the aim settings sit in
// C:otox-tf2\default.cfg with everything else and the menu's save/load buttons already cover them.
#include <cstdio>
void Aim_SaveVars(FILE* f);                                  // append our keys
bool Aim_LoadVar(const char* szKey, const char* szValue);    // true = key was ours

// The aimbot tab body, drawn inside dih's own menu window. Lives on this side because it
// reads ConfigVar metadata; dih's menu.cpp only calls it.
void Aim_RenderMenuTab();
