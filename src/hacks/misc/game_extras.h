#pragma once
#include "../../globals/globals.h"
#include <cstddef>

// botox misc > game / convenience rows that need more than a checkbox:
//   chat translator  (botox chat_extras.cpp)  SayText/SayText2 -> gtx worker -> chat line
//   bot names        (botox bot_names.cpp)    IVEngineClient::GetPlayerInfo, slot 8
//   performance      (botox misc.cpp performance())
//   cvar list        (botox misc.cpp exec_cvars) run on map load / server join
//   image url box    (botox menu_tab_settings.cpp image_path_input) for discord rpc

// PollThread, once: IVEngineClient::GetPlayerInfo hook (bot names).
void GameExtras_Install();

// Engine FrameStageNotify (skins.cpp h_FrameStageNotify), game thread, every stage. Prints
// finished translations, holds bot pings, applies the performance knobs, runs the cvar list
// on join. With g_eject set it puts everything back once.
void GameExtras_Frame(int stage);

// Eject, MainThread, after the hooks are off: restores what no frame restored, aborts the
// http requests and joins every worker thread this file started.
void GameExtras_Shutdown();

// CHLClient::DispatchUserMessage (visuals.cpp h_DispatchUserMessage), game thread, before
// the original reads the buffer. type 3 SayText / 4 SayText2; bf = the engine's bf_read.
void ChatTranslator_OnUserMessage(int type, void* bf);

// botox n_chat_extras::k_language_names, for the "translate to" combo
extern const char* const kChatLanguageNames;

// bot names popup: status line (steam names mode) and the "randomize" button
void BotNames_Status(char* out, size_t size);
void BotNames_Randomize();

// misc > cvars "execute": every row top to bottom (ClientCmd_Unrestricted)
void GameExtras_ExecCvars();

// botox image_path_input: text box + "browse" / "upload from pc" (upload = the file goes to
// freeimage.host and the box gets the url -- discord fetches the image itself).
void ImagePick_Input(const char* label, const char* hint, char* value, size_t size, bool upload);
