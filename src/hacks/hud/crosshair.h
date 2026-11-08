#pragma once
#include "../../globals/globals.h"

inline constexpr float kChUnitToPx = 2.0f;

void RenderCrosshair();

// csgo reticle art + the custom crosshair's blend-mode shaders and frame copy (DEFAULT
// pool) -- drop them on Reset and eject.
void CsgoCrosshair_ReleaseTextures();

// Dropdown of the custom crosshair's blend modes (ch_blend_modes.h), one pick at a time.
// Returns true while the closed combo is hovered, for the caller's tooltip.
bool Crosshair_BlendCombo(const char* id);

// Put the game's "crosshair" convar back to the player's value (eject).
void Crosshair_Restore();

// Menu preview of the custom crosshair (botox custom_crosshair.cpp draw_preview/draw_preview_scene).
// testSpeed < 0 = standing, matching the dynamic-gap convention RenderCustomCrosshair itself uses.
void CustomCrosshair_DrawPreview(ImDrawList* dl, ImVec2 min, ImVec2 max, float testSpeed);
void CustomCrosshair_DrawPreviewScene(ImDrawList* dl, ImVec2 p0, float side);
