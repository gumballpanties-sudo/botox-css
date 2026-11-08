#pragma once
#include "../../globals/globals.h"

void RenderMenu();
bool RenderMenuBlur();    // background blur: call after the layers it should blur (esp, world fx), before HUD panels. true = drew
float Menu_FadeAlpha();   // botox m_anim_progress, render.cpp fades the menu layer by it

// botox fade_draw_lists popup half: confirm popups (SavePopup) fade on their own, 2x menu speed.
// Once per built frame: moves lists owned by a fading popup out of `lists` into `out`, plus
// ghosts of closed ones. Returns the out count.
struct ImDrawList;
struct MenuFadedList { ImDrawList* list; float alpha; };
int  Menu_SplitPopups(ImDrawList** lists, int& count, MenuFadedList* out, int max);
void Menu_DropPopupGhosts();

// bold label + thin line to the right edge -- the SUB header inside a group box.
// Shared so the ps point window heads its blocks the same way instead of with
// TextDisabled + Separator.
void MenuSectionHeader(const char* label);

// = ImGui::Keybind (botox), for callers that predate it.
void MenuBindKey(const char* id, int* key, int* mode);

#include <functional>
// botox imgui fork widgets (menu.cpp), same names/behaviour as botox dependencies/imgui so
// tab code ports verbatim. ColorRow = botox ColorEdit4(label, col, flags, swatch_slot) (named
// apart because stock ColorEdit4(label, col, flags) would make 3-arg calls ambiguous).
// MultiCombo takes a bitmask (bit i = item i) where botox takes std::vector<bool>.
// Keybind mode: 0 always on, 1 hold, 2 toggle.
// botox menu.cpp menu_columns_begin/next/end + menu_group_begin/end live in menu_chrome.h.
namespace ImGui
{
    void OptionPopup(const char* str_id, const std::function<void()>& function, const ImVec2& window_size);
    bool Keybind(const char* label, int* key, int* mode, bool has_style = true);
    bool MultiCombo(const char* label, int* mask, const char* const items[], int count);
    bool ColorRow(const char* label, float col[4], ImGuiColorEditFlags flags = 0, int swatch_slot = 0);
}

#include <initializer_list>
#include <utility>
// botox bool_combo (hacks/menu/menu.cpp, free function -- not ImGui::, matching botox's own):
// one dropdown of independent checkboxes, used where a row is a group of flags and a checkbox
// PER FLAG would spend more vertical space than botox does. Preview = the ON ones' names
// joined ", ", word-wrapped the same way MultiCombo's preview is. Items don't close the popup
// when toggled, same as MultiCombo's rows.
void bool_combo(const char* label, std::initializer_list<std::pair<const char*, bool*>> items);
