#pragma once
#include "../../globals/globals.h"
#include <string>

// Windows "now playing" overlay (botox globals/media_player + misc.cpp draw_media_player).
// Reads whatever owns the system media transport controls -- Spotify, a browser tab, foobar,
// the Groove/Media Player app -- so there is nothing per-player to configure. Look + fonts
// follow the watermark style (g_wmStyle), see watermark_styles.h Watermark_MediaLook.
void MediaPlayer_Start();                       // load: spawns the winrt poll thread
void MediaPlayer_Shutdown();                    // eject: joins it, drops the cover texture
void MediaPlayer_Tick(IDirect3DDevice9* dev);   // RENDER THREAD ONLY: (re)builds the cover texture
void MediaPlayer_ReleaseTexture();              // device change: the texture is the old device's
void RenderMediaPlayer();                       // imgui frame build, after RenderWatermark (anchors under its box)
bool   MediaPlayer_HasSession();                // anything to control
bool   MediaPlayer_Track(std::string& title, std::string& artist);   // botox m_has_media + title/artist (clantag), any thread
void   MediaPlayer_RequestControl(int cmd);     // 0 play/pause, 1 next, 2 previous -- poll thread only acts on it
