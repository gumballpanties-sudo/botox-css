#pragma once
#include "../../globals/globals.h"

// botox injection splash, ported from csgo-sdk hacks/misc/boot_screen.cpp. Draws itself
// from the first hooked frame until the inject steps finish, then is gone for the session.
void RenderBootScreen();

// botox boot_screen_fade_others: between ImGui::Render and the draw, scales every vertex
// alpha except the splash's own by the reveal ramp -- the overlay fades in after the splash.
void BootScreen_FadeOthers(ImDrawData* dd);

// botox boot_screen_eject_begin / _end. MainThread, blocking <= 1.5s each, back at once when
// no frame is drawing (minimised, imgui never up): begin = splash fades in and covers, end =
// teardown work done, "ejected", fades the game back in.
void BootScreen_EjectBegin();
void BootScreen_EjectEnd();
// h_Present after g_eject: true while the splash still has frames to draw (render.cpp
// RenderEjectFrame draws it alone, every other overlay is already down).
bool BootScreen_EjectDrawing();
