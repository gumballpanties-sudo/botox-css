#pragma once
#include "../../globals/globals.h"

// Discord Rich Presence over the raw local IPC pipe (\\.\pipe\discord-ipc-N), documented
// client<->Discord protocol -- no SDK dependency, no game hook, pure Windows named-pipe IPC.
// botox defaults: its app id + imgur image URL; g_discordAppId / g_discordImageKey override
// when non-empty. Payload = botox's ("in menu" / "playing on <map>"); g_discordState/Details unused.
void DiscordRpc_Start();      // load: spawns the IPC thread, idles until g_discordEnable
void DiscordRpc_Shutdown();   // eject: closes the pipe, joins
