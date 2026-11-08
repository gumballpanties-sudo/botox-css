#pragma once
#include "../../globals/globals.h"

// Unit D menu widgets. menu.cpp's tab bodies are being rebuilt elsewhere, so this unit
// never touches them -- the function below is a free-standing drop-in a later pass wires
// into the visuals/screen sub-tab with one call. Plain ImGui widgets only (no FeatureCheckbox/
// SliderF/GroupToggle -- those are menu.cpp file-local helpers this unit does not own).
void UnitD_Menu_visuals_screen();

// Unit D2: dlight, sun angle, world/prop texture tint, weather particle colour. Same
// drop-in contract as UnitD_Menu_visuals_screen -- a later pass wires it into the
// visuals/world sub-tab with one call.
void UnitD_Menu_visuals_world();
