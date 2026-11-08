#pragma once
#include "../../globals/globals.h"

// botox hacks/misc/spectator_list/spectator_list.cpp (HEAD 63c5358), all 33 styles, g_specStyle =
// botox m_spectators_list_style order: 0 default (list type 1 = draw_spectating_local), 1 delusional,
// 2 interwebz, 3 windows xp, 4 clarity, 5 chillware (bezier link lines), 6 lobotomy v2, 7 kamibebra,
// 8 eyes (procedural eye / twemoji textures), 9 cumhacck, 10 clarity v2, 11 airplane, 12 bhop cheat,
// 13 millionware, 14 aimware, 15 havoc, 16 onetap, 17 airflow, 18 ev0lve, 19 legendware, 20 interium,
// 21 chillware v2 (real ImGui window, position saved), 22 skebob, 23 cumidere, 24 illusory, 25 lumi
// (default / new + mascot / interwebz), 26 billware, 27 inkabanium, 28 dna, 29 cucumber,
// 30 howeweware, 31 gamesense, 32 kamidere.
// Popup vars: src/hacks/menu/cfg/u_spec.h. Netvars: m_iObserverMode / m_hObserverTarget on
// DT_BasePlayer, m_lifeState (netvars.cpp g_offLifeState), resolved lazily.
void RenderSpectatorList();

// device Reset / shutdown: the eye row textures are DEFAULT pool (gif/file images live in gif_tex)
void SpectatorList_ReleaseTextures();

// botox image_path_input: text box (file path or http/imgur link) + "browse" file dialog
void SpecList_PathInput(const char* label, const char* hint, char* buf, size_t size);
