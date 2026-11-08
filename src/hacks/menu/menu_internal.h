#pragma once
// shared by menu.cpp and the menu_tab_*.cpp bodies (botox menu_internal.h layout)
#include "menu.h"
#include "../aim/aim.h"
#include "config.h"
#include "../hud/indicators.h"
#include "../network/netregion.h"
#include "../hud/scoreboard.h"
#include "../screen/colorcorrect.h"
#include "../hud/crosshair.h"   // Crosshair_BlendCombo
#include "../hud/hud_extra.h"   // unit hud: fonts tab net graph page reset
#include "../visuals/lagres.h"
#include "../movement/movement.h"   // RC_* marker access for the route calculator's marker list
#include "../movement/points_style.h"   // movement > points page: styles + preview
#include "../../dependencies/imgui/imgui_internal.h"
#include "menu_chrome.h"  // the panels, the group box flow and every metric they use

#include <cmath>   // isfinite -- SliderF refuses a NaN/inf value as a range bound
#include <cctype>  // tolower -- the searchable warpaint/particle combos
#include <algorithm>
#include <ctime>     // BuildDateText: localtime_s off the PE timestamp
#include <cstdint>
#include <string>
#include "unitA_menu.h"
#include "unitB_menu.h"
#include "unitC_menu.h"
#include "unitD_menu.h"
#include "unitD3_menu.h"
#include "../visuals/world_fx3_math.h"   // unit world: remove hud elements list (k_hudElems)
#include "unitE_menu.h"
#include "../botoxnet/botoxnet.h"        // "botox network" group
#include "../web/websurface.h"           // "web on surface" group
#include "../screen/resolution_spoof.h"  // "resolution spoofer" popup
#include "fonts.h"
#include <functional>
#include <shellapi.h>   // ShellExecuteA -- settings "open folder" (WIN32_LEAN_AND_MEAN drops it)
#include "../misc/game_extras.h"   // unit game: chat translator / bot names / cvar list / image url box
#include "../misc/names.h"         // custom names: cycle lists + misc > player list (aliases)
#include "../misc/game_pure.h"     // n_game::SplitCvars -- cvars "add" splits on ';'
#include "../screen/blur_rect.h"   // BlurRectFade -- settings "background blur"

void MenuTooltip(const char* fmt, ...);
ImU32 BlendU32(const ImVec4& a, const ImVec4& b, float t);
float RowLaneX(float w);
void BindKeyNoMode(const char* id, int* key);
bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt = "%.3f");
bool SliderI(const char* label, int* v, int lo, int hi, const char* fmt = "%d");
void PopupLive(ImGuiID window_id);
void SavePopup(const char* str_id, bool& open, const ImVec2& size, const std::function<void()>& fn);
void MenuClientCmd(const char* cmd);
bool IconButton(const char* id, const int icon, const float height);
void SafeCheckbox(const char* label, bool* v);
void ConvenienceButtons();

// tab roots + their page state (sidebar flyout in menu.cpp)
extern const char* kVisualsSub[3]; extern int s_visualsSub; void Tab_VisualsRoot();
extern const char* kMoveSub[5];    extern int s_moveSub;    void Tab_MovementRoot();
extern const char* kMiscSub[3];    extern int s_miscSub;    void Tab_MiscRoot();
extern const char* kFontsSub[3];   extern int s_fontsSub;   void Tab_Fonts();
void Tab_Settings();
void Tab_Lua();   // menu_tab_lua.cpp -- settings page "lua" (menu.cpp kSettingsSub)
int MenuStyleCount();             // menu_tab_settings.cpp theme rows
void MenuStyleApplyAt(int i);
