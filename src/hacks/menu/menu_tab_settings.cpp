#include "menu_internal.h"

// ==== skins =========================================================================
// skinchanger removed entirely (11-03 user: "THERE ARE NO SKINS IN CSS, REMOVE THE
// SKINCHANGER ENTIRELY"). Tab_Skins / inventory tab / skin share page deleted; code deletion
// (skins.cpp, skinshare.cpp) is unit SKINRM. See report.

// botox tab_fonts: one page per face, "<face> font" settings | "font list". botox pages
// indicators / esp map to TF2 (fonts.h); its chud hud / default hud pages rewrite CS:GO's
// panorama fonts.conf and have no TF2 equivalent. Applies live: render.cpp rebuilds the atlas
// (indicators) and aim/helpers.cpp recreates the vgui face (esp) once the pick settles.
const char* kFontsSub[3] = { "indicators", "esp", "net graph" };   // botox case 4 = net graph (unit hud)
int s_fontsSub = 0;
static const char* const kFontFlagNames[FF_COUNT] = {
    "no hinting", "no autohint", "force autohint", "light hinting", "monohinting", "bold", "oblique", "monochrome" };

static void FontPage(const char* name, char* face, size_t faceSize, int* size, int* flags, void (*reset)(),
                     const std::vector<std::string>& list, ImGuiTextFilter& filter)
{
    menu_columns_begin();

    if (menu_group_begin(name))
    {
        ImGui::SliderInt("size", size, 0, 50, "%d");
        ImGui::MultiCombo("font flags", flags, kFontFlagNames, FF_COUNT);
        if (ImGui::Button("reset to default", ImVec2(-1.f, 15.f))) reset();
        if (ImGui::Button("reload fonts", ImVec2(-1.f, 15.f))) Fonts_RequestReload();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("font list"))
    {
        filter.Draw("search for a font");
        if (ImGui::BeginListBox("##font list", ImVec2(0.f, 300.f)))
        {
            for (const auto& n : list)
                if (filter.PassFilter(n.c_str()) && ImGui::Selectable(n.c_str(), _stricmp(face, n.c_str()) == 0))
                    strncpy_s(face, faceSize, n.c_str(), _TRUNCATE);
            ImGui::EndListBox();
        }
    }
    menu_group_end();

    menu_columns_end();
}

void Tab_Fonts()
{
    if (s_fontsSub == 1)
    {
        static ImGuiTextFilter s_espFilter;
        FontPage("esp font", g_fontEspName, sizeof(g_fontEspName), &g_fontEspSize, &g_fontEspFlags,
                 Fonts_ResetEsp, Fonts_Families(), s_espFilter);
    }
    else if (s_fontsSub == 2)   // unit hud: custom net graph face (hud_extra.cpp rebuilds the atlas)
    {
        static ImGuiTextFilter s_ngFilter;
        FontPage("net graph font", g_hudNgFontName, sizeof(g_hudNgFontName), &g_hudNgFontSize, &g_hudNgFontFlags,
                 HudFonts_ResetNetGraph, Fonts_FileNames(), s_ngFilter);
    }
    else
    {
        static ImGuiTextFilter s_indFilter;
        FontPage("indicator font", g_fontIndName, sizeof(g_fontIndName), &g_fontIndSize, &g_fontIndFlags,
                 Fonts_ResetIndicator, Fonts_FileNames(), s_indFilter);
    }
    // UNIT_MENU_SLOT fonts
}

// botox config_panel(visuals, ..., list_rows 12): file name, list, then create / save / load /
// remove / refresh / open folder, with yes/no confirmations. visuals = the second panel: only
// the visuals-tab keys (visual_keys.h), files in C:\botox-css\visuals. Names shown without ".cfg".
static bool CfgNameGetter(void* data, int idx, const char** out)
{
    *out = (*static_cast<std::vector<std::string>*>(data))[idx].c_str();
    return true;
}

static void ConfigPanel(const bool visuals)
{
    struct PanelState {
        std::vector<std::string> names;
        bool listed = false, save = false, load = false, remove = false;
        int  selected = 0;
        char file[64] = "";
    };
    static PanelState s_panels[2];
    PanelState& p = s_panels[visuals];

    auto refresh = [&p, visuals]() {
        p.names = ListConfigs(visuals);
        for (auto& n : p.names) if (n.size() > 4) n.resize(n.size() - 4);
    };
    if (!p.listed) { refresh(); p.listed = true; }

    ImGui::InputText(visuals ? "visuals file name" : "config file name", p.file, sizeof(p.file));

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 3.f));
    ImGui::ListBox(visuals ? "##visuals config list" : "##config list", &p.selected, CfgNameGetter, &p.names,
                   (int)p.names.size(), 12);
    ImGui::PopStyleVar();

    if (p.selected < 0 || p.selected >= (int)p.names.size()) p.selected = 0;
    const std::string sel = p.names.empty() ? "" : p.names[p.selected];

    if (ImGui::Button("create", ImVec2(-1.f, 15.f)) && p.file[0]) { SaveConfigNamed(p.file, visuals); refresh(); }
    if (ImGui::Button("save", ImVec2(-1.f, 15.f)))   p.save = true;
    if (ImGui::Button("load", ImVec2(-1.f, 15.f)))   p.load = true;
    if (ImGui::Button("remove", ImVec2(-1.f, 15.f))) p.remove = true;
    if (ImGui::Button("refresh", ImVec2(-1.f, 15.f))) refresh();
    if (ImGui::Button("open folder", ImVec2(-1.f, 15.f)))
    {
        // first press before anything was saved: the folder must exist to open
        CreateDirectoryA(CONFIG_DIR, nullptr);
        CreateDirectoryA(ConfigFolder(visuals), nullptr);
        ShellExecuteA(nullptr, "open", ConfigFolder(visuals), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // nothing listed: a save/load/remove on "" would hit nothing
    if (p.names.empty()) p.save = p.load = p.remove = false;

    SavePopup("save confirmation", p.save, ImVec2(220.f, -1.f), [&]() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f))) { SaveConfigNamed(sel.c_str(), visuals); p.save = false; }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) p.save = false;
    });
    SavePopup("load confirmation", p.load, ImVec2(220.f, -1.f), [&]() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f))) { LoadConfigNamed(sel.c_str(), visuals); p.load = false; }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) p.load = false;
    });
    SavePopup("remove confirmation", p.remove, ImVec2(220.f, -1.f), [&]() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f)))
        {
            DeleteConfigNamed(sel.c_str(), visuals);
            refresh();
            p.selected = 0;
            p.remove = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) p.remove = false;
    });
}

// ==== settings > menu: theme dropdown ============================================
// Look only -- colours, borders, rounding, fonts. Never layout, never animation, never accent.
struct MenuStyleRow
{
    const char* name;
    ImU32 window, band, child, frame, outline, windowOutline, popup, text, textDim;
    float windowRounding, frameRounding, popupRounding;
    ImFont* (*regFont)();    // nullptr = leave the chrome font alone (default style)
    ImFont* (*boldFont)();
    bool gamesense = false;  // skeet frame + gradient bar over the window (menu.cpp GamesenseFrame), layout untouched
};

static const MenuStyleRow k_menuStyles[] = {
    { "default", IM_COL32(10, 10, 10, 255), IM_COL32(25, 25, 25, 255), IM_COL32(15, 15, 15, 255), IM_COL32(25, 25, 25, 255),
      IM_COL32(50, 50, 50, 100), IM_COL32(50, 50, 50, 255), IM_COL32(20, 20, 20, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(128, 128, 128, 255), 5.f, 2.f, 3.f, nullptr, nullptr },
    { "kamibebra", IM_COL32(13, 13, 13, 255), IM_COL32(16, 16, 16, 255), IM_COL32(13, 13, 13, 255), IM_COL32(16, 16, 16, 255),
      IM_COL32(33, 33, 33, 255), IM_COL32(11, 11, 11, 255), IM_COL32(13, 13, 13, 255), IM_COL32(200, 200, 200, 255),
      IM_COL32(150, 150, 150, 255), 6.f, 4.f, 6.f, []() -> ImFont* { return g_fontKamibebra13; }, []() -> ImFont* { return g_fontKamibebraBold13; } },
    { "clarity v2", IM_COL32(23, 23, 23, 255), IM_COL32(28, 28, 28, 255), IM_COL32(23, 23, 23, 255), IM_COL32(28, 28, 28, 255),
      IM_COL32(55, 55, 55, 255), IM_COL32(7, 7, 7, 255), IM_COL32(23, 23, 23, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(100, 100, 100, 255), 5.f, 5.f, 5.f, []() -> ImFont* { return g_fontClarityInter; }, []() -> ImFont* { return g_fontInterBold15; } },
    { "lobotomy v1", IM_COL32(15, 15, 15, 255), IM_COL32(13, 13, 13, 255), IM_COL32(15, 15, 15, 255), IM_COL32(13, 13, 13, 255),
      IM_COL32(30, 30, 30, 255), IM_COL32(30, 30, 30, 255), IM_COL32(15, 15, 15, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(200, 200, 200, 255), 0.f, 0.f, 0.f, []() -> ImFont* { return g_fontMenuTahoma9; }, []() -> ImFont* { return g_fontMenuTahomaBd9; } },
    { "lobotomy v2", IM_COL32(7, 7, 7, 255), IM_COL32(10, 10, 10, 255), IM_COL32(10, 10, 10, 255), IM_COL32(25, 25, 25, 255),
      IM_COL32(25, 25, 25, 255), IM_COL32(25, 25, 25, 255), IM_COL32(7, 7, 7, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(180, 180, 180, 255), 6.f, 2.f, 4.f, []() -> ImFont* { return g_fontLobotomy13; }, []() -> ImFont* { return g_fontLobotomyBold13; } },
    { "dna", IM_COL32(9, 9, 9, 240), IM_COL32(9, 9, 9, 240), IM_COL32(20, 20, 20, 153), IM_COL32(13, 13, 13, 255),
      IM_COL32(26, 26, 26, 178), IM_COL32(255, 255, 255, 8), IM_COL32(20, 20, 20, 255), IM_COL32(242, 242, 242, 255),
      IM_COL32(77, 77, 77, 217), 4.f, 2.f, 4.f, []() -> ImFont* { return g_fontDnaMontserrat15; }, []() -> ImFont* { return g_fontDnaPtRootBold15; } },
    // csgo menu.cpp "gamesense" row; null fonts = verdana pair
    { "gamesense", IM_COL32(20, 20, 20, 255), IM_COL32(12, 12, 12, 255), IM_COL32(23, 23, 23, 255), IM_COL32(35, 35, 35, 255),
      IM_COL32(10, 10, 10, 255), IM_COL32(10, 10, 10, 255), IM_COL32(23, 23, 23, 255), IM_COL32(205, 205, 205, 255),
      IM_COL32(120, 120, 120, 255), 0.f, 0.f, 0.f, nullptr, nullptr, true },
};
static const int k_menuStyleCount = (int)(sizeof(k_menuStyles) / sizeof(k_menuStyles[0]));

static void MenuStyleApply(const MenuStyleRow& s)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg]         = ImGui::ColorConvertU32ToFloat4(s.window);
    style.Colors[ImGuiCol_ChildBg]          = ImGui::ColorConvertU32ToFloat4(s.child);
    style.Colors[ImGuiCol_PopupBg]          = ImGui::ColorConvertU32ToFloat4(s.popup);
    style.Colors[ImGuiCol_Text]             = ImGui::ColorConvertU32ToFloat4(s.text);
    style.Colors[ImGuiCol_TextDisabled]     = ImGui::ColorConvertU32ToFloat4(s.textDim);
    style.Colors[ImGuiCol_MenuBand]         = ImGui::ColorConvertU32ToFloat4(s.band);
    style.Colors[ImGuiCol_MenuFrame]        = ImGui::ColorConvertU32ToFloat4(s.frame);
    style.Colors[ImGuiCol_MenuOutline]      = ImGui::ColorConvertU32ToFloat4(s.outline);
    style.Colors[ImGuiCol_MenuWindowOutline]= ImGui::ColorConvertU32ToFloat4(s.windowOutline);
    style.WindowRounding = s.windowRounding;
    style.FrameRounding  = s.frameRounding;
    style.PopupRounding  = s.popupRounding;
    // null = font not loaded (system calibri missing): keep the verdana pair, chrome calls CalcTextSize on it
    if (ImFont* f = s.regFont ? s.regFont() : nullptr)   g_menuReg  = f;
    if (ImFont* f = s.boldFont ? s.boldFont() : nullptr) g_menuBold = f;
}

int MenuStyleCount() { return k_menuStyleCount; }
void MenuStyleApplyAt(int i) { MenuStyleApply(k_menuStyles[i]); }
bool MenuStyleGamesense(int i) { return k_menuStyles[i].gamesense; }

// botox tab_settings: col1 "config" + "system", col2 "visuals config" + "menu". Left out, no
// TF2 counterpart: "botox network" (no TF2 server).
void Tab_Settings()
{
    // botox menu_internal.h color_picker_no_alpha_flags
    constexpr ImGuiColorEditFlags kPickerNoAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
        ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;

    menu_columns_begin();

    if (menu_group_begin("config"))
        ConfigPanel(false);
    menu_group_end();

    if (menu_group_begin("system"))
    {
        // install-time diags ran before this was ever ticked, so their lines went nowhere.
        // Replay the ones that were kept the moment the log opens.
        if (ImGui::Checkbox("debug log", &g_debugLogEnable) && g_debugLogEnable)
        {
            DebugLog_FlushEarly();
            Scoreboard_LogInstallOnce();
            LagRes_LogInstallOnce();
        }
        ImGui::Checkbox("safe mode", &g_safeMode);
    }
    menu_group_end();

    // botox DOES have this (tab_settings "botox network" group, safe_checkbox + "botox network
    // settings" popup: share esp / share skins / share web screen). 1:1 here except "share
    // skins" -- dropped, no skinchanger in CSS (user rule, see SKINRM's report). The 11-03
    // comment calling this "no csgo counterpart" was wrong; fixed.
    if (menu_group_begin("botox network"))
    {
        SafeCheckbox("botox network", &g_botox_net_enable);
        if (g_botox_net_enable && !g_safeMode)
            ImGui::OptionPopup("botox network settings", []() {
                ImGui::Checkbox("share esp##botoxnet", &g_botox_net_esp);
                ImGui::Checkbox("share web screen##botoxnet", &g_botox_net_web);
                ImGui::Text("botox users here: %d", g_botox_net.m_users.load());
            }, ImVec2(220.f, -1.f));
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("visuals config"))
        ConfigPanel(true);
    menu_group_end();

    if (menu_group_begin("menu"))
    {
        if (g_menuStyle < 0 || g_menuStyle >= k_menuStyleCount) g_menuStyle = 0;
        const auto styleName = [](void*, int index, const char** out) { *out = k_menuStyles[index].name; return true; };
        ImGui::Combo("theme", &g_menuStyle, styleName, nullptr, k_menuStyleCount);

        ImGui::ColorRow("accent color##accent color", g_accentColor, kPickerNoAlpha);

        // botox menu_tab_settings: edits a copy, commits while not held -- the whole menu
        // rescales under the cursor otherwise (render/dpi.cpp)
        {
            static float s_dpiEdit = 100.f;
            static bool s_dpiActive = false;
            if (!s_dpiActive) s_dpiEdit = g_dpiScale;
            ImGui::SliderFloat("dpi scale", &s_dpiEdit, 25.f, 200.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            s_dpiActive = ImGui::IsItemActive();
            if (!s_dpiActive) g_dpiScale = s_dpiEdit;

            ImGui::Checkbox("dpi scale panels", &g_dpiScalePanels);
        }

        ImGui::Checkbox("background blur", &g_menuBlur);
        if (g_menuBlur)
            ImGui::OptionPopup("background blur settings##menu blur settings", []() {
                ImGui::SliderFloat("max blur##menu blur", &g_menuBlurAmount, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("darken##menu blur", &g_menuBlurDarken, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            }, ImVec2(220.f, -1.f));

        // botox 95ef94a: menu key lives here; none / lmb / rmb fall back to insert (RenderMenu)
        ImGui::Label("menu key");
        int menuKey = g_menuKey;
        int dummyMode = 1;
        ImGui::Keybind("menu key##menu key", &menuKey, &dummyMode, false);
        g_menuKey = menuKey;
    }
    menu_group_end();

    menu_columns_end();
    // UNIT_MENU_SLOT settings
}
