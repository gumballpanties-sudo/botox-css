#include "menu_internal.h"
#include "../misc/minesweeper.h"   // fpsoverlay minesweeper window (botox menu.cpp:1020)
#include "../visuals/esp_preview.h"   // g_espPreview, EspPreview::DrawBody/kWidth/kHeight


// ==== tooltips: OFF ==============================================================
// Every menu tooltip goes through here instead of ImGui::SetTooltip. Body is empty, so
// nothing draws and no tooltip window is ever created -- which also keeps the tooltip
// out of the draw-data split in render.cpp (a tooltip is its own root window and would
// otherwise have to be classified). Text is kept at every call site: to bring tooltips
// back, uncomment the one line below.
void MenuTooltip(const char* fmt, ...)
{
    (void)fmt;
    // va_list a; va_start(a, fmt); ImGui::SetTooltipV(fmt, a); va_end(a);
}

// ==== botox imgui fork widgets (dependencies/imgui/imgui_widgets.cpp), ported 1:1 =========
// Declared in menu.h so unit menus and aim.cpp port botox tab code verbatim. Fonts:
// BoldFont() = font_name_verdana_bd_11, g_fontIcon13 = font_name_icon_13.

// botox key_names, 256 entries, one per vk
static const char* const kKeyNames[256] = {
    "[-]",   "[m1]",  "[m2]",  "[cn]",  "[m3]",  "[m4]",  "[m5]",  "[-]",   "[bac]", "[tab]", "[-]",   "[-]",   "[clr]", "[ret]", "[-]",   "[-]",
    "[shi]", "[ctl]", "[men]", "[pau]", "[cap]", "[kan]", "[-]",   "[jun]", "[fin]", "[kan]", "[-]",   "[esc]", "[con]", "[nco]", "[acc]", "[mad]",
    "[spa]", "[pgu]", "[pgd]", "[end]", "[hom]", "[lef]", "[up]",  "[rig]", "[dow]", "[sel]", "[pri]", "[exe]", "[pri]", "[ins]", "[del]", "[hel]",
    "[0]",   "[1]",   "[2]",   "[3]",   "[4]",   "[5]",   "[6]",   "[7]",   "[8]",   "[9]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[-]",   "[a]",   "[b]",   "[c]",   "[d]",   "[e]",   "[f]",   "[g]",   "[h]",   "[i]",   "[j]",   "[k]",   "[l]",   "[m]",   "[n]",   "[o]",
    "[p]",   "[q]",   "[r]",   "[s]",   "[t]",   "[u]",   "[v]",   "[w]",   "[x]",   "[y]",   "[z]",   "[win]", "[win]", "[app]", "[-]",   "[sle]",
    "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[mul]", "[add]", "[sep]", "[min]", "[dec]", "[div]",
    "[f1]",  "[f2]",  "[f3]",  "[f4]",  "[f5]",  "[f6]",  "[f7]",  "[f8]",  "[f9]",  "[f10]", "[f11]", "[f12]", "[f13]", "[f14]", "[f15]", "[f16]",
    "[f17]", "[f18]", "[f19]", "[f20]", "[f21]", "[f22]", "[f23]", "[f24]", "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[num]", "[scr]", "[equ]", "[mas]", "[toy]", "[oya]", "[oya]", "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[shi]", "[shi]", "[ctr]", "[ctr]", "[alt]", "[alt]", "[bbk]", "[bfw]", "[brf]", "[bst]", "[bse]", "[bfa]", "[bho]", "[mut]", "[vo-]", "[vo+]",
    "[nxt]", "[prv]", "[stp]", "[ply]", "[mai]", "[sel]", "[ap1]", "[ap2]", "[-]",   "[-]",   "[;]",   "[=]",   "[,]",   "[dsh]", "[.]",   "[/]",
    "[`]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[[]",   "[\\]",  "[]]",   "[']",   "[oe8]",
    "[-]",   "[oem]", "[<>]",  "[oem]", "[oem]", "[ime]", "[oem]", "[pkt]", "[-]",   "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[oem]",
    "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[att]", "[crs]", "[exs]", "[eof]", "[ply]", "[zoo]", "[-]",   "[pa1]", "[clr]", "[-]"
};

ImU32 BlendU32(const ImVec4& a, const ImVec4& b, float t)
{
    return ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

// botox popup chrome (OptionPopup / save_popup): 20px band, centred bold title, outline, accent line
static void PopupChrome(const char* title, const char* title_end, ImGuiID anim_id)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    const float rnd = ImGui::GetStyle().WindowRounding - 2.f;
    ImGui::PushClipRect(p, ImVec2(p.x + sz.x, p.y + 20.f), false);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + 20.f), kBandBg(), rnd, ImDrawFlags_RoundCornersTop);
    ImGui::PopClipRect();
    ImGui::PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), false);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), kTabBorder(), rnd);
    auto anim = ImAnimationHelper(anim_id, ImGui::GetIO().DeltaTime);
    anim.Update(ImGui::IsMouseHoveringRect(p, ImVec2(p.x + sz.x, p.y + sz.y)) ? 3.f : -2.f, 1.f, 0.5f, 1.f);
    ImGui::PopClipRect();
    ImFont* f = BoldFont();
    const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, title, title_end);
    dl->AddText(f, f->FontSize, ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (20.f - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text, anim.AnimationData->second), title, title_end);
    RenderFadedGradientLine(dl, ImVec2(p.x, p.y + 20.f), ImVec2(sz.x, 1.f), ImColor(Accent()));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.f);
}

// Right-edge lane of the row just drawn: a cog and a keybind on one row stack leftward
// (first caller gets the right edge) instead of drawing on top of each other.
float RowLaneX(float w)
{
    static ImGuiID s_win = 0;
    static float s_y = -1.f, s_used = 0.f;
    static int s_frame = -1;
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float y = win->DC.CursorPosPrevLine.y;
    if (s_win != win->ID || s_y != y || s_frame != ImGui::GetFrameCount())
    {
        s_win = win->ID; s_y = y; s_used = 0.f; s_frame = ImGui::GetFrameCount();
    }
    const float x = ImGui::GetRowFrameRight() - s_used - w;
    s_used += w + 6.f;
    return x;
}

void ImGui::OptionPopup(const char* str_id, const std::function<void()>& function, const ImVec2& window_size)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (IsSearchHidden()) return;   // botox search_skip(str_id, attached): the cog follows its row
    const char* title_end = FindRenderedTextEnd(str_id);
    {
        ImFont* icon = g_fontIcon13 ? g_fontIcon13 : GetFont();
        const char* cog = "\xef\x80\x93";   // U+F013
        const ImVec2 cs = icon->CalcTextSizeA(icon->FontSize, FLT_MAX, 0.f, cog);
        // pinned to the row's right edge, on the row just drawn (cursor is already one row below)
        const float cog_x = RowLaneX(15.f);
        const float cog_y = GetWindowPos().y + GetCursorPosY() - window->Scroll.y - 20.f;
        const bool hovered = IsMouseHoveringRect(ImVec2(cog_x, cog_y), ImVec2(cog_x + 15.f, cog_y - 1.f + cs.y), false);
        window->DrawList->AddText(icon, icon->FontSize, ImVec2(cog_x, cog_y),
                                  hovered ? IM_COL32_WHITE : IM_COL32(190, 190, 190, 255), cog);
        if (hovered && (IsMouseClicked(ImGuiMouseButton_Right) || IsMouseClicked(ImGuiMouseButton_Left)))
            OpenPopup(str_id);
    }
    SetNextWindowSize(window_size);
    if (BeginPopup(str_id))
    {
        PopupChrome(str_id, title_end, ImHashStr(str_id));
        function();
        EndPopup();
    }
}

// botox Keybind: "[key]" on the row above, right edge, 50% text -> accent while listening.
// Left click = listen (esc clears, click away cancels), right click = style popup.
// mode: 0 always on, 1 hold, 2 toggle (same order as botox EKeyStyles).
bool ImGui::Keybind(const char* label, int* key, int* mode, bool has_style)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    if (IsSearchHidden()) return false;   // botox search_skip(label, attached): the key follows its row
    static ImGuiID s_listening = 0;
    static bool s_clean = false;   // every key seen up since listening began

    const ImGuiID id = window->GetID(label);
    if (*key < 0 || *key > 255) *key = 0;
    const bool listening = (s_listening == id);
    const char* disp = listening ? "[...]" : (*key ? kKeyNames[*key] : "[none]");

    SameLine(0.f, 0.f);
    const ImVec2 ts = CalcTextSize(disp);
    const ImVec2 ls = CalcTextSize(kKeyNames[*key]);   // botox label_size: popup anchor, read before input
    window->DC.CursorPos.x = RowLaneX(ts.x);
    const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + ts.x, window->DC.CursorPos.y + ts.y));
    ItemSize(ts, 0.f);
    if (!ItemAdd(bb, id)) return false;

    bool changed = false;
    const bool hovered = ItemHoverable(bb, id);
    if (hovered && IsMouseClicked(0)) { if (!listening) { s_listening = id; s_clean = false; *key = 0; } }
    else if (IsMouseClicked(0)) { if (listening) s_listening = 0; }
    else if (hovered && IsMouseClicked(1) && has_style) OpenPopup(label);

    if (s_listening == id)
    {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *key = 0; s_listening = 0; }
        else
        {
            bool anyDown = false;
            int pressed = 0;
            for (int vk = 0x01; vk <= 0xFE && !pressed; ++vk)
            {
                if (vk == VK_ESCAPE || vk == 0x07 || vk == 0x0A || vk == 0x0B || vk == 0xE5 || vk == 0xE7) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { anyDown = true; if (s_clean) pressed = vk; }
            }
            if (!anyDown) s_clean = true;   // the click that started listening must be released first
            if (pressed) { *key = pressed; s_listening = 0; changed = true; }
        }
    }

    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 8.f));
    SetNextWindowSize(ImVec2(61.f, 65.f), ImGuiCond_Always);
    // botox frame_bb.Max = cursor - label size: popup opens up-left of the key text
    SetNextWindowPos(ImVec2(bb.Min.x - ls.x + 5.f, bb.Min.y - ls.y), ImGuiCond_Always);
    if (BeginPopup(label, ImGuiWindowFlags_NoMove))
    {
        ImGuiWindow* pw = GetCurrentWindow();
        pw->DrawList->AddRect(pw->Pos, ImVec2(pw->Pos.x + pw->Size.x, pw->Pos.y + pw->Size.y), kTabBorder(), GetStyle().WindowRounding);
        if (Selectable("always on", *mode == 0, ImGuiSelectableFlags_DontClosePopups)) *mode = 0;
        if (Selectable("hold",      *mode == 1, ImGuiSelectableFlags_DontClosePopups)) *mode = 1;
        if (Selectable("toggle",    *mode == 2, ImGuiSelectableFlags_DontClosePopups)) *mode = 2;
        EndPopup();
    }
    PopStyleVar();

    auto anim = ImAnimationHelper(id + ImHashStr("text-animation"), GetIO().DeltaTime);
    anim.Update(2.f, (s_listening == id) ? 2.f : -2.f);
    ImVec4 tc = GetStyleColorVec4(ImGuiCol_Text); tc.w *= 0.5f;
    ImVec4 ac = GetStyleColorVec4(ImGuiCol_Accent); ac.w = 1.f;
    window->DrawList->AddText(bb.Min, BlendU32(tc, ac, anim.AnimationData->second), disp);
    return changed;
}

// botox MultiCombo on a bitmask (bit i = item i): preview = picked items joined ", ", word-wrapped
// to the frame with "...", "none" when empty; rows toggle without closing.
bool ImGui::MultiCombo(const char* label, int* mask, const char* const items[], int count)
{
    if (GetCurrentWindow()->SkipItems) return false;
    std::string buf;
    for (int i = 0; i < count; ++i)
        if (*mask & (1 << i)) { if (!buf.empty()) buf += ", "; buf += items[i]; }
    if (buf.empty()) buf = "none";
    else
    {
        ImGuiContext& g = *GImGui;
        const float w = GetRowFrameWidth();
        const char* wrap = g.Font->CalcWordWrapPositionA(GetCurrentWindow()->FontWindowScale, buf.data(), buf.data() + buf.size(),
                                                        w - g.Style.FramePadding.x * 2.f);
        const size_t n = (size_t)(wrap - buf.data());
        if (n > 0 && n < buf.size()) { buf.resize(n); buf += "..."; }
    }
    bool changed = false;
    if (BeginCombo(label, buf.c_str(), 0))
    {
        for (int i = 0; i < count; ++i)
            if (Selectable(items[i], (*mask & (1 << i)) != 0, ImGuiSelectableFlags_DontClosePopups)) { *mask ^= (1 << i); changed = true; }
        EndCombo();
    }
    return changed;
}

// botox bool_combo: same preview/word-wrap shape as MultiCombo above, but over named bool*
// pointers instead of a bitmask + item array -- botox's web settings "playback"/"controls"
// rows are the only callers (menu_tab_visuals.cpp:2275-2282).
void bool_combo(const char* label, std::initializer_list<std::pair<const char*, bool*>> items)
{
    if (ImGui::GetCurrentWindow()->SkipItems) return;
    std::string buf;
    for (auto& it : items)
        if (*it.second) { if (!buf.empty()) buf += ", "; buf += it.first; }
    if (buf.empty()) buf = "none";
    else
    {
        ImGuiContext& g = *GImGui;
        const float w = ImGui::GetRowFrameWidth();
        const char* wrap = g.Font->CalcWordWrapPositionA(ImGui::GetCurrentWindow()->FontWindowScale, buf.data(), buf.data() + buf.size(),
                                                        w - g.Style.FramePadding.x * 2.f);
        const size_t n = (size_t)(wrap - buf.data());
        if (n > 0 && n < buf.size()) { buf.resize(n); buf += "..."; }
    }
    if (ImGui::BeginCombo(label, buf.c_str(), 0))
    {
        for (auto& it : items)
            if (ImGui::Selectable(it.first, *it.second, ImGuiSelectableFlags_DontClosePopups))
                *it.second = !*it.second;
        ImGui::EndCombo();
    }
}

// botox ColorEdit4(label, col, flags, swatch_slot): ONE row -- label at the row's left edge,
// swatch pinned to the row's right edge (slot N = N swatches further left), click = botox picker
// popup. The body is the imgui fork's ColorEdit4 (botox port); this name stays for the call sites.
bool ImGui::ColorRow(const char* label, float col[4], ImGuiColorEditFlags flags, int swatch_slot)
{
    return ColorEdit4(label, col, flags, swatch_slot);
}

void MenuSectionHeader(const char* label) { SectionHeader(label); }

void MenuBindKey(const char* id, int* key, int* mode) { ImGui::Keybind(id, key, mode, true); }

// key capture only, no style popup (edge-press actions: set/delete point)
void BindKeyNoMode(const char* id, int* key)
{
    int unused = 1;
    ImGui::Keybind(id, key, &unused, false);
}

// ==== sliders =======================================================================
// A slider's min/max bound the DRAG only, never the value. Ctrl+click (or right-click)
// any slider to type a value -- negative fog start, a fog end past 10000 -- imgui keeps
// it as typed (no ImGuiSliderFlags_AlwaysClamp anywhere in the menu) and config.cpp
// round-trips it through a plain %.4f / atof, so it saves and reloads unchanged.
//
// What was missing is the DRAW: a value outside [lo,hi] pinned the thumb at an end, and
// the very next drag snapped it back into range with no way to get out again. These two
// widen the drawn range to hold the current value, so an out-of-range value shows where
// it actually sits and stays draggable. The normal range is untouched while the value is
// inside it -- widening happens only for a value that is ALREADY out, so no drag can
// reach a region the slider didn't already offer.
//
// Non-finite is refused (NaN/inf would make the range itself garbage) -- the value is
// still whatever was typed, only the bar falls back to its declared range.
bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt)
{
    float dlo = lo, dhi = hi;
    if (isfinite(*v))
    {
        if (*v < dlo) dlo = *v;
        if (*v > dhi) dhi = *v;
    }
    return ImGui::SliderFloat(label, v, dlo, dhi, fmt);
}

bool SliderI(const char* label, int* v, int lo, int hi, const char* fmt)
{
    const int dlo = *v < lo ? *v : lo;
    const int dhi = *v > hi ? *v : hi;
    return ImGui::SliderInt(label, v, dlo, dhi, fmt);
}


// ==== misc / fonts / settings helpers ===============================================

// botox menu.h popup_fade_t: confirm popups fade on their own. live = drawn this frame,
// ghost = last live frame's geometry, drawn while it fades out after the popup closed.
// Stepped once per built frame by Menu_SplitPopups (render.cpp SplitDrawData).
struct PopupFade { ImGuiID id; float alpha; bool live; ImDrawList* ghost; };
static ImVector<PopupFade> s_popups;

// botox popup_live
void PopupLive(ImGuiID window_id)
{
    for (PopupFade& p : s_popups)
        if (p.id == window_id) { p.live = true; return; }
    s_popups.push_back({ window_id, 0.f, true, nullptr });
}

// botox menu.cpp save_popup: opens once per request, a click away drops the request (never
// reopens at the mouse), body under the same 20px band OptionPopup draws.
void SavePopup(const char* str_id, bool& open, const ImVec2& size, const std::function<void()>& fn)
{
    if (!open) return;
    const ImGuiID shown = ImGui::GetID(str_id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    if (!st->GetBool(shown)) { ImGui::OpenPopup(str_id); st->SetBool(shown, true); }
    ImGui::SetNextWindowSize(size);
    if (!ImGui::BeginPopup(str_id)) { open = false; st->SetBool(shown, false); return; }
    PopupLive(ImGui::GetCurrentWindow()->ID);
    PopupChrome(str_id, nullptr, ImHashStr(str_id));
    fn();
    if (!open) { ImGui::CloseCurrentPopup(); st->SetBool(shown, false); }
    ImGui::EndPopup();
}

// IVEngineClient::ClientCmd_Unrestricted, slot 106 -- the call skins.cpp / practice.inl /
// screenfx.cpp already make. botox's convenience buttons call client_cmd_unrestricted the
// same way, straight from the click.
void MenuClientCmd(const char* cmd)
{
    DebugLog("menu: convenience button '%s'", cmd);
    if (!g_engine) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engine);
        if (reinterpret_cast<uintptr_t>(vt[106]) > 0x10000)
            ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) { DebugLog("menu: ClientCmd_Unrestricted faulted"); }
}

// botox icon_button: a 16px button with its glyph drawn on top (0 down, 1 up, 2 x)
bool IconButton(const char* id, const int icon, const float height)
{
    const bool pressed = ImGui::Button(id, ImVec2(16.f, height));
    if (ImGui::IsSearchHidden()) return false;   // filtered out: no glyph on the last shown row
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const float cx = ImFloor((mn.x + mx.x) * 0.5f) + 0.5f, cy = ImFloor((mn.y + mx.y) * 0.5f) + 0.5f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (icon == 2)
    {
        // close-small svg: 24 box, arms +-4.32 from center, stroke 2, square ends
        const float s = height / 24.f, arm = 4.32f * s;
        dl->AddLine(ImVec2(cx - arm, cy - arm), ImVec2(cx + arm, cy + arm), col, 2.f * s);
        dl->AddLine(ImVec2(cx + arm, cy - arm), ImVec2(cx - arm, cy + arm), col, 2.f * s);
    }
    else
    {
        const float tip = icon == 0 ? 2.f : -2.f;
        dl->AddLine(ImVec2(cx - 3.f, cy - tip), ImVec2(cx, cy + tip), col);
        dl->AddLine(ImVec2(cx, cy + tip), ImVec2(cx + 3.f, cy - tip), col);
    }
    return pressed;
}

// botox menu_internal.h safe_checkbox: safe mode greys it out and shows it unticked
void SafeCheckbox(const char* label, bool* v)
{
    bool off = false;
    ImGui::BeginDisabled(g_safeMode);
    ImGui::Checkbox(label, g_safeMode ? &off : v);
    ImGui::EndDisabled();
    if (g_safeMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("safe mode enabled");
}

// botox tab_misc "convenience": one button per saved title/command, an x icon to drop it,
// "no buttons" when empty, a rule, then "add button" opening a "new button" popup. Slots
// instead of botox's one tab/newline string (config.cpp is one key=value per line).
void ConvenienceButtons()
{
    static char s_title[32] = "", s_command[160] = "";
    static bool s_openAdd = false;
    int remove = -1, shown = 0;
    for (int i = 0; i < kConvButtons; ++i)
    {
        if (!g_convTitle[i][0] || !g_convCmd[i][0]) continue;
        ++shown;
        ImGui::PushID(i);
        // frames draw 2 px past bb, 7 px gap = same 3 px seam as cvars rows
        if (ImGui::Button(g_convTitle[i], ImVec2(ImGui::GetContentRegionAvail().x - 7.f - 16.f, 15.f)))
            MenuClientCmd(g_convCmd[i]);
        ImGui::SameLine(0.f, 7.f);
        if (IconButton("##delete", 2, 15.f)) remove = i;
        ImGui::PopID();
    }
    if (!shown) ImGui::TextDisabled("no buttons");

    // line spans frame edges (bb + 2 px)
    const ImVec2 line = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(line.x - 2.f, line.y), ImVec2(line.x + ImGui::GetContentRegionAvail().x + 2.f, line.y),
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0.f, 1.f));

    if (remove >= 0) { g_convTitle[remove][0] = '\0'; g_convCmd[remove][0] = '\0'; }

    if (ImGui::Button("add button", ImVec2(-1.f, 15.f))) s_openAdd = true;
    SavePopup("new button", s_openAdd, ImVec2(220.f, -1.f), []() {
        ImGui::InputText("title", s_title, sizeof(s_title));
        ImGui::InputText("command", s_command, sizeof(s_command));
        auto blank = [](const char* t) { while (*t == ' ') ++t; return *t == '\0'; };
        int slot = -1;
        for (int i = 0; i < kConvButtons && slot < 0; ++i)
            if (!g_convTitle[i][0] || !g_convCmd[i][0]) slot = i;
        ImGui::BeginDisabled(blank(s_title) || blank(s_command) || slot < 0);
        if (ImGui::Button("add button", ImVec2(-1.f, 15.f)))
        {
            strncpy_s(g_convTitle[slot], s_title, _TRUNCATE);
            strncpy_s(g_convCmd[slot], s_command, _TRUNCATE);
            s_title[0] = s_command[0] = '\0';
            s_openAdd = false;
        }
        ImGui::EndDisabled();
        if (slot < 0) ImGui::TextDisabled("all %d slots used", kConvButtons);
    });
}


// ==== tab bar =======================================================================
// botox's own tab/page shape (botox_menu_spec.md section 2): aimbot | visuals (esp,
// world, screen) | movement (main, indicators, calculator, recorder) | misc (main,
// player list) | inventory (= skinchanger) | fonts | settings.
// "inventory" tab removed with the skinchanger (11-03). botox_menu_spec.md section 2: pages
// per tab. aimbot/fonts/settings have none; visuals/movement/misc carry the sidebar's flyout.
enum MenuTab { TAB_AIMBOT, TAB_VISUALS, TAB_MOVEMENT, TAB_MISC, TAB_FONTS, TAB_SETTINGS, TAB_COUNT };
static const char* kTabNames[TAB_COUNT] = { "aimbot", "visuals", "movement", "misc", "fonts", "settings" };
static int s_tab = TAB_AIMBOT;
// botox settings pages { "main", "lua" } (menu.cpp:790)
static const char* kSettingsSub[2] = { "main", "lua" };
static int s_settingsSub = 0;

static const SidebarTab kSidebar[TAB_COUNT] = {
    { kTabNames[TAB_AIMBOT],    nullptr,     0 },
    { kTabNames[TAB_VISUALS],   kVisualsSub, 3 },
    { kTabNames[TAB_MOVEMENT],  kMoveSub,    5 },
    { kTabNames[TAB_MISC],      kMiscSub,    3 },
    { kTabNames[TAB_FONTS],     kFontsSub,   3 },
    { kTabNames[TAB_SETTINGS],  kSettingsSub, 2 },
};

// botox_menu_spec.md section 1, build_date_text() literal (menu.cpp lines 22-45): the
// PE timestamp of this module, via localtime_s, falling back to __DATE__ if that read
// fails. Format "<d> <mon> <yyyy>", lowercase 3-letter month -- NOT the day name (botox
// has no weekday in the title, that was this fork's own embellishment).
static const char* BuildDateText()
{
    static char s_text[20] = { 0 };
    if (s_text[0]) return s_text;

    static const char* kMon[12] = { "jan","feb","mar","apr","may","jun",
                                    "jul","aug","sep","oct","nov","dec" };

    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&BuildDateText), &mod))
    {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<const std::uint8_t*>(mod) + dos->e_lfanew);
            std::tm local = { };
            const std::time_t stamp = nt->FileHeader.TimeDateStamp;
            if (nt->Signature == IMAGE_NT_SIGNATURE && localtime_s(&local, &stamp) == 0)
            {
                _snprintf(s_text, sizeof(s_text), "%d %s %d",
                          local.tm_mday, kMon[local.tm_mon], local.tm_year + 1900);
                return s_text;
            }
        }
    }

    // __DATE__ fallback: "Mmm dd yyyy", space-padded day ("Aug  5 2026")
    const int day  = (__DATE__[4] == ' ' ? 0 : (__DATE__[4] - '0') * 10) + (__DATE__[5] - '0');
    const int year = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
                     (__DATE__[9] - '0') * 10   + (__DATE__[10] - '0');
    int m = 0;
    for (int i = 0; i < 12; ++i)
        if (_strnicmp(__DATE__, kMon[i], 3) == 0) { m = i; break; }
    _snprintf(s_text, sizeof(s_text), "%d %s %d", day, kMon[m], year);
    return s_text;
}

// ==== menu search (botox menu.cpp:843-905 box, :1040-1124 search_page) ==================
static char s_search[64] = { };
static bool s_searchFocus = false;

// botox search box: tab-sized, sits above the tabs. Draw-list only (no item), keys read off
// the imgui input queue while focused.
// csgo menu.cpp gamesense_frame: 6 px frame outside the window (12 / 60 / 40 x3 / 60), 2 px bar over the band's
// top edge = majorkahook g_color1..3 (37b1da -> cc46cd -> cce335), second row same rgb at half alpha
static void GamesenseFrame(ImDrawList* dl, const ImVec2& pos, const ImVec2& size)
{
    static constexpr int kLayers[6] = { 12, 60, 40, 40, 40, 60 };
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->PushClipRect(ImVec2(pos.x - 6.f, pos.y - 6.f), ImVec2(max.x + 6.f, max.y + 6.f), false);
    for (int i = 0; i < 6; i++)
    {
        const float o = 6.f - (float)i;
        dl->AddRect(ImVec2(pos.x - o, pos.y - o), ImVec2(max.x + o, max.y + o), IM_COL32(kLayers[i], kLayers[i], kLayers[i], 255));
    }
    const float mid = ImFloor(pos.x + size.x / 2.f);
    for (int row = 0; row < 2; row++)
    {
        const int a = row ? 128 : 255;
        const float y = pos.y + 1.f + (float)row;
        const ImU32 c1 = IM_COL32(55, 177, 218, a), c2 = IM_COL32(204, 70, 205, a), c3 = IM_COL32(204, 227, 53, a);
        dl->AddRectFilledMultiColor(ImVec2(pos.x + 1.f, y), ImVec2(mid, y + 1.f), c1, c2, c2, c1);
        dl->AddRectFilledMultiColor(ImVec2(mid, y), ImVec2(max.x - 1.f, y + 1.f), c2, c3, c3, c2);
    }
    dl->PopClipRect();
}

static void SearchBox(float x, float& topY, bool canClick, float dt)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 a(wp.x + x, wp.y + topY - ImGui::GetScrollY());
    const ImVec2 b(a.x + mf::kTabW, a.y + mf::kTabH);
    const bool hovered = ImGui::IsMouseHoveringRect(a, b);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        s_searchFocus = canClick && hovered;

    size_t length = strlen(s_search);
    if (s_searchFocus)
    {
        for (const ImWchar c : ImGui::GetIO().InputQueueCharacters)
            if (c >= 32 && c < 127 && length < sizeof(s_search) - 1)
            {
                s_search[length++] = (char)c;
                s_search[length] = '\0';
            }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && length > 0)
        {
            length = ImGui::GetIO().KeyCtrl ? 0 : length - 1;
            s_search[length] = '\0';
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            length = 0;
            s_search[0] = '\0';
            s_searchFocus = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) s_searchFocus = false;
    }

    auto hov = ImAnimationHelper(ImHashStr("menu search") + ImHashStr("hovered-text-animation"), dt);
    hov.Update(2.f, hovered || s_searchFocus ? 2.f : -2.f, 0.5f);

    const float rnd = ImGui::GetStyle().WindowRounding - 2.f;
    dl->AddRectFilled(a, b, kBandBg(), rnd);
    dl->AddRect(a, b, kTabBorder(), rnd);

    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text, hov.AnimationData->second);
    ImFont* f = BoldFont();
    if (length > 0 || s_searchFocus)
    {
        const ImVec2 fa(a.x + 3.f, a.y + 3.f), fb(b.x - 3.f, b.y - 3.f);
        dl->AddRectFilled(fa, fb, ImGui::GetColorU32(ImGuiCol_ChildBg), ImGui::GetStyle().FrameRounding);
        dl->AddRect(fa, fb, kTabBorder(), ImGui::GetStyle().FrameRounding);
    }

    if (length == 0 && !s_searchFocus)
    {
        const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, "search");
        dl->AddText(f, f->FontSize, ImVec2(ImFloor(a.x + (mf::kTabW - ts.x) * 0.5f), ImFloor(a.y + (mf::kTabH - ts.y) * 0.5f)),
                    textCol, "search");
    }
    else
    {
        const bool caret = s_searchFocus && (int)(ImGui::GetTime() * 2.0) % 2 == 0;
        const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s_search);
        const float tx = ImFloor(ImMin(a.x + 6.f, b.x - 9.f - ts.x));
        const float ty = ImFloor(a.y + (mf::kTabH - f->FontSize) * 0.5f);
        dl->PushClipRect(ImVec2(a.x + 3.f, a.y), ImVec2(b.x - 3.f, b.y), true);
        dl->AddText(f, f->FontSize, ImVec2(tx, ty), textCol, s_search);
        if (caret)
            dl->AddLine(ImVec2(tx + ts.x + 1.f, ty), ImVec2(tx + ts.x + 1.f, ty + f->FontSize), textCol);
        dl->PopClipRect();
    }

    topY += mf::kTabH + mf::kTabGapY;
}

// one page of one tab, drawn through the tab root with its page var swapped
static void DrawTabPage(int tab, int page)
{
    int* sub = tab == TAB_VISUALS ? &s_visualsSub : tab == TAB_MOVEMENT ? &s_moveSub
             : tab == TAB_MISC    ? &s_miscSub    : tab == TAB_FONTS    ? &s_fontsSub
             : tab == TAB_SETTINGS ? &s_settingsSub : nullptr;
    const int saved = sub ? *sub : 0;
    if (sub) *sub = page;
    switch (tab)
    {
    case TAB_AIMBOT:   Aim_RenderMenuTab(); break;
    case TAB_VISUALS:  Tab_VisualsRoot();   break;
    case TAB_MOVEMENT: Tab_MovementRoot();  break;
    case TAB_MISC:     Tab_MiscRoot();      break;
    case TAB_FONTS:    Tab_Fonts();         break;
    case TAB_SETTINGS: if (s_settingsSub == 1) Tab_Lua(); else Tab_Settings(); break;
    }
    if (sub) *sub = saved;
}

// botox search_page: every searchable page drawn through the imgui row filter. A tab with no
// hits last frame runs in a 1x1 probe (never clipped, so it still counts); a tab with hits gets
// a group. A scrolled-out group skips its items: keep its old state.
static void SearchPage()
{
    struct SearchTab { const char* name; int tab; int pages[3]; int count; };
    // skipped like botox: calculator, recorder, points, player list, cvars, lua = lists + previews, not rows
    static const SearchTab tabs[] = {
        { "aimbot",   0, { 0 },       1 }, { "visuals", 1, { 0, 1, 2 }, 3 }, { "movement", 2, { 0, 1 }, 2 },
        { "misc",     3, { 0 },       1 }, { "fonts",   4, { 0, 1, 2 }, 3 }, { "settings", 5, { 0 },       1 },
    };
    constexpr int kCount = IM_ARRAYSIZE(tabs);
    static bool hits[kCount] = { };

    const auto run = [](const SearchTab& e) {
        ImGui::SetSearchFilter(s_search);
        g_menuFlat = true;
        for (int i = 0; i < e.count; ++i)
        {
            ImGui::PushID(e.pages[i]);
            DrawTabPage(e.tab, e.pages[i]);
            ImGui::PopID();
        }
        g_menuFlat = false;
        const bool found = ImGui::GetSearchHits() > 0;
        ImGui::SetSearchFilter(nullptr);
        return found;
    };

    bool shown[kCount];
    for (int i = 0; i < kCount; ++i) shown[i] = hits[i];

    ImGui::PushID("search");

    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::BeginChild("##search probe", ImVec2(1.f, 1.f), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs);
    // zero-area clip: probe counts rows, draws nothing
    const ImVec2 probe = ImGui::GetWindowPos();
    ImGui::PushClipRect(probe, probe, true);
    for (int i = 0; i < kCount; ++i)
        if (!shown[i]) hits[i] = run(tabs[i]);
    ImGui::PopClipRect();
    ImGui::EndChild();
    ImGui::SetCursorPos(cursor);

    menu_columns_begin();

    bool any = false;
    for (bool h : hits) any |= h;
    if (!any) ImGui::TextDisabled("nothing found");

    for (int column = 0; column < 2; ++column)
    {
        if (column == 1) menu_columns_next();
        for (int i = 0, slot = 0; i < kCount; ++i)
        {
            if (!shown[i] || slot++ % 2 != column) continue;
            if (menu_group_begin(tabs[i].name)) hits[i] = run(tabs[i]);
            menu_group_end();
        }
    }

    menu_columns_end();
    ImGui::PopID();
}

// botox menu.cpp save_popup("eject confirmation", ..., ImVec2(220, -1)): fixed 220 wide,
// 20px #191919 title band with the id centred in verdana bold, accent fade line under it,
// then yes / no as two 15px buttons sharing the row. Goes through SavePopup like botox, so it
// fades with the other confirm popups.
static bool s_ejectOpen = false;
static void EjectPopup()
{
    SavePopup("eject confirmation", s_ejectOpen, ImVec2(220.f, -1.f), []() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f)))
        {
            s_ejectOpen = false;
            g_ejectRequest = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) s_ejectOpen = false;
    });
}

// botox m_anim_progress: render.cpp composites the menu's draw lists as ONE layer at this
// alpha (ImGui_ImplDX9_RenderDrawDataFaded), not style alpha -- that skipped every hand-drawn
// band, title, tab and border, so the chrome popped while the widgets faded.
static float s_animProgress = 0.f;
float Menu_FadeAlpha() { return ImSaturate(s_animProgress); }

static bool OwnsWindow(ImGuiWindow* w, ImGuiWindow* root)
{
    for (int d = 0; w && d < 32; ++d, w = w->ParentWindow)
        if (w == root) return true;
    return false;
}

static bool WindowDrawn(const ImGuiWindow* w) { return w && w->Active && !w->Hidden && w->DrawList; }

// botox fade_draw_lists, popup half: popups fade at 2x the menu speed. Lists owned by a live
// popup leave the menu layer for `out` at menu_fade * popup alpha; closed popups add their ghost.
int Menu_SplitPopups(ImDrawList** lists, int& count, MenuFadedList* out, int max)
{
    ImGuiWindow* root = ImGui::FindWindowByName("botox");
    const float step = (std::min)(ImGui::GetIO().DeltaTime, 1.f / 30.f) / (0.12f * 0.5f);
    const bool shown = s_animProgress > 0.f && WindowDrawn(root);

    for (int i = 0; i < s_popups.Size;)
    {
        PopupFade& p = s_popups[i];
        ImGuiWindow* w = ImGui::FindWindowByID(p.id);
        if (shown && p.live && WindowDrawn(w))
        {
            p.alpha = (std::min)(p.alpha + step, 1.f);
            if (!p.ghost) p.ghost = IM_NEW(ImDrawList)(w->DrawList->_Data);
            p.ghost->CmdBuffer = w->DrawList->CmdBuffer;
            p.ghost->IdxBuffer = w->DrawList->IdxBuffer;
            p.ghost->VtxBuffer = w->DrawList->VtxBuffer;
            p.ghost->Flags     = w->DrawList->Flags;
        }
        else if (!shown || !p.live)
            p.alpha = shown ? p.alpha - step : 0.f;

        // live but hidden = imgui auto-fit frame, hold at 0
        if (p.alpha <= 0.f && !(shown && p.live))
        {
            if (p.ghost) IM_DELETE(p.ghost);
            s_popups.erase(s_popups.Data + i);
            continue;
        }
        ++i;
    }

    int n = 0;
    if (!shown) return 0;   // !shown emptied s_popups above
    const float menu_fade = ImSaturate(s_animProgress);

    ImGuiContext& g = *GImGui;
    int keep = 0;
    for (int i = 0; i < count; ++i)
    {
        ImDrawList* dl = lists[i];
        const PopupFade* owner = nullptr;
        for (ImGuiWindow* w : g.Windows)
        {
            if (w->DrawList != dl) continue;
            for (const PopupFade& p : s_popups)
                if (p.live && OwnsWindow(w, ImGui::FindWindowByID(p.id))) owner = &p;
            break;
        }
        if (owner && n < max) out[n++] = { dl, menu_fade * owner->alpha };
        else lists[keep++] = dl;
    }
    count = keep;

    for (PopupFade& p : s_popups)
    {
        if (!p.live && p.ghost && n < max) out[n++] = { p.ghost, menu_fade * p.alpha };
        p.live = false;
    }
    return n;
}

// ghosts hold the font atlas texture id: drop them with it (reset, rebind, atlas rebuild)
void Menu_DropPopupGhosts()
{
    for (PopupFade& p : s_popups)
        if (p.ghost) { IM_DELETE(p.ghost); p.ghost = nullptr; }
}

// botox 01e09de: blur on the background list, fading at the menu's speed. Crossfaded through
// BlurRectFade's alpha like botox's tint. The blur samples the backbuffer when its callback
// replays, so only what the background list drew BEFORE this call blurs: render.cpp calls it
// after esp + world fx, before the HUD panels (spec list, watermark...), which stay sharp.
// true = it blurred or darkened this frame (render.cpp then keeps the crosshair over it).
bool RenderMenuBlur()
{
    ImGuiIO& io = ImGui::GetIO();
    static float s_blurFade = 0.f;
    const float step = (std::min)(io.DeltaTime, 1.f / 30.f) / 0.12f;   // menu_anim_duration
    const bool want = g_showMenu && g_menuBlur;
    s_blurFade = (g_ejectRequest || g_eject) ? 0.f : ImClamp(s_blurFade + (want ? step : -step), 0.f, 1.f);
    if (s_blurFade <= 0.f) return false;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const float amount = ImClamp(g_menuBlurAmount, 0.f, 100.f) / 100.f;
    const float darken = ImClamp(g_menuBlurDarken, 0.f, 100.f) / 100.f;
    if (amount > 0.f) BlurRectFade(bg, ImVec2(0.f, 0.f), io.DisplaySize, amount, s_blurFade);
    if (darken > 0.f)
        bg->AddRectFilled(ImVec2(0.f, 0.f), io.DisplaySize, IM_COL32(0, 0, 0, (int)(darken * s_blurFade * 255.f + 0.5f)));
    return amount > 0.f || darken > 0.f;
}

void RenderMenu()
{
    ImGuiIO& io = ImGui::GetIO();

    // live accent (settings tab RowColor edits g_accentColor directly; this is the one
    // place it is pushed into the style every widget already reads it through)
    ImGui::GetStyle().Colors[ImGuiCol_Accent] =
        ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], g_accentColor[3]);

    // dpi scale: render/dpi.cpp (logical layout + vertex scale + font twins), nothing here.

    // botox menu.cpp on_end_scene, verbatim (botox_menu_spec.md section 1): fixed
    // 750 x (640 - (menu_band_height - menu_bottom_band_height)) = 750x627, centred once,
    // never resized by the user. m_anim_progress is the open/close fade (section 7); while
    // it has not reached 1 the window eats no input, same as botox's eject-safety gate.
    if (g_ejectRequest || g_eject) s_animProgress = 0.f;   // eject snaps shut, never fades under a release
    else
    {
        const float step = (std::min)(io.DeltaTime, 1.f / 30.f) / 0.12f;   // menu_anim_duration
        s_animProgress = ImClamp(s_animProgress + (g_showMenu ? step : -step), 0.f, 1.f);
    }

    // botox menu_key(): none / lmb / rmb would lock the menu shut or toggle it on every click
    if (!(g_menuKey > VK_RBUTTON && g_menuKey < 256)) g_menuKey = VK_INSERT;

    if (!g_showMenu && s_animProgress <= 0.f) return;

    // menu style: colours/borders/rounding/fonts only. Restore once this window is built --
    // HUD panels elsewhere read GetStyle() (rounding, colours) and must never see the swap.
    struct MenuStyleScope
    {
        ImGuiStyle m_saved     = ImGui::GetStyle();
        ImFont*    m_savedReg  = g_menuReg;
        ImFont*    m_savedBold = g_menuBold;
        MenuStyleScope()
        {
            if (g_menuStyle < 0 || g_menuStyle >= MenuStyleCount()) g_menuStyle = 0;
            MenuStyleApplyAt(g_menuStyle);
            ImGui::PushFont(g_menuReg);
        }
        ~MenuStyleScope()
        {
            ImGui::PopFont();
            ImGui::GetStyle() = m_saved;
            g_menuReg  = m_savedReg;
            g_menuBold = m_savedBold;
        }
    } styleScope;

    // the main viewport, not io.DisplaySize: windows lay out in logical (dpi) px, io.DisplaySize
    // is the screen outside NewFrame (render/dpi.cpp)
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(view.x * 0.5f, view.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(750.f, 640.f - (25.f - 12.f)), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollWithMouse;   // the ##content child scrolls, not the frame
    if (s_animProgress < 0.999f) flags |= ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("botox", NULL, flags))
    {
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 wz = ImGui::GetWindowSize();
    const float rnd = ImGui::GetStyle().WindowRounding;

    // ---- botox on_end_scene order: top band, accent line under the band, bottom band,
    // window border, accent line on bottom band top edge. Lines = RenderFadedGradientLine (bright
    // centre, fading to both ends), full window width.
    // Bands + border each get botox's own PushClipRect(.., false): the window's clip rect is
    // inset WindowPadding.x/2 = 4px per side, which cut the bands and the side border short.
    dl->PushClipRect(wp, ImVec2(wp.x + wz.x, wp.y + mf::kHeaderH), false);
    dl->AddRectFilled(wp, ImVec2(wp.x + wz.x, wp.y + mf::kHeaderH), kBandBg(), rnd, ImDrawFlags_RoundCornersTop);
    dl->PopClipRect();
    RenderFadedGradientLine(dl, ImVec2(wp.x, wp.y + mf::kHeaderH - 1.f), ImVec2(wz.x, 1.f), ImColor(Accent()));

    const float botY = wp.y + wz.y - 12.f;   // menu_bottom_band_height
    dl->PushClipRect(ImVec2(wp.x, botY), ImVec2(wp.x + wz.x, wp.y + wz.y), false);
    dl->AddRectFilled(ImVec2(wp.x, botY), ImVec2(wp.x + wz.x, wp.y + wz.y), kBandBg(), rnd, ImDrawFlags_RoundCornersBottom);
    dl->PopClipRect();
    // border after both bands: bottom fill drawn over it hid the outline
    dl->PushClipRect(wp, ImVec2(wp.x + wz.x, wp.y + wz.y), false);
    dl->AddRect(wp, ImVec2(wp.x + wz.x, wp.y + wz.y), kWindowBorder(), rnd);
    dl->PopClipRect();
    RenderFadedGradientLine(dl, ImVec2(wp.x, botY), ImVec2(wz.x, 1.f), ImColor(Accent()));
    if (MenuStyleGamesense(g_menuStyle)) GamesenseFrame(dl, wp, wz);

    // ---- title: "botox" bold white, then " | <date>" regular accent, same baseline,
    // centred as one unit in the top band.
    {
        ImGui::PushFont(BoldFont());
        char title[8] = "botox";
        const ImVec2 ws = ImGui::CalcTextSize(title);
        ImGui::PopFont();
        ImGui::PushFont(RegFont());
        char date[24];
        _snprintf(date, sizeof(date), " | %s", BuildDateText());
        const ImVec2 ds = ImGui::CalcTextSize(date);
        ImGui::PopFont();

        const float tx = wp.x + (wz.x - ws.x - ds.x) * 0.5f;
        const float ty = wp.y + (mf::kHeaderH - ws.y) * 0.5f;
        ImGui::PushFont(BoldFont());
        dl->AddText(ImVec2(tx, ty), IM_COL32_WHITE, title);
        ImGui::PopFont();
        ImGui::PushFont(RegFont());
        dl->AddText(ImVec2(tx + ws.x, ty), Accent(), date);
        ImGui::PopFont();
    }

    // ---- sidebar: botox's stacked boxed tabs (botox_menu_spec.md section 2) -----------
    // tab_x = window content-left (WindowPadding.x), tab_y = pos.y+25 in LOCAL coords,
    // i.e. 25 + WindowPadding.y once SidebarTab_Draw adds the window origin back on.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float sidebarX = st.WindowPadding.x;
    const float sidebarTop = mf::kHeaderH + st.WindowPadding.y;
    // botox's tab_button is draw-list only; ours is an InvisibleButton, which moves the
    // cursor. Eject is the last one, at the window floor, so without this restore the
    // content below starts under eject -- off the bottom of the window, every tab blank.
    const ImVec2 contentStart = ImGui::GetCursorPos();
    {
        static int s_noPage = -1;
        float tabY = sidebarTop;
        SearchBox(sidebarX, tabY, ImGui::IsWindowHovered(), io.DeltaTime);
        for (int i = 0; i < TAB_COUNT; ++i)
        {
            int& page = (i == (int)TAB_VISUALS)  ? s_visualsSub
                      : (i == (int)TAB_MOVEMENT) ? s_moveSub
                      : (i == (int)TAB_MISC)     ? s_miscSub
                      : (i == (int)TAB_FONTS)    ? s_fontsSub
                      : (i == (int)TAB_SETTINGS) ? s_settingsSub
                      : s_noPage;
            const int oldPage = page;
            if (SidebarTab_Draw(kSidebar[i], i, s_tab == i, page, sidebarX, tabY, io.DeltaTime))
            {
                s_tab = i;
                s_search[0] = '\0';   // botox: picking a tab or page drops the search
            }
            if (page != oldPage) s_search[0] = '\0';
        }

        // eject: same box look, pinned to the sidebar's bottom (spec section 2):
        // tab_y = pos.y + size.y - WindowPadding.y - bottom_band_height - tab_height.
        float ejectY = wz.y - st.WindowPadding.y - 12.f - mf::kTabH;
        int ejectNoPage = -1;
        if (SidebarTab_Draw(SidebarTab{ "eject", nullptr, 0 }, TAB_COUNT, false, ejectNoPage,
                            sidebarX, ejectY, io.DeltaTime))
            s_ejectOpen = true;

        EjectPopup();
    }
    ImGui::SetCursorPos(contentStart);

    // ---- content: botox_menu_spec.md section 2 -- Indent past the sidebar, drop 25px
    // under the title band, PushID per tab so two tabs never share a cached group height.
    // One borderless, bg-less child stands in for botox's two column children: it clips
    // the boxes above the bottom band (avail.y - menu_bottom_band_height, same as
    // menu_columns_begin) and scrolls a tab taller than the window with the wheel.
    ImGui::Indent(mf::kTabW + st.ItemSpacing.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + mf::kHeaderH);
    ImGui::PushID(s_tab);
    ImGui::BeginChild("##content", ImVec2(0.f, ImGui::GetContentRegionAvail().y - 12.f), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

    FlowBegin();
    if (s_search[0]) SearchPage();
    else switch (s_tab)
    {
    // Aimbot / triggerbot / melee. The body lives in src/hacks/aim/aim.cpp because it
    // reads that tree's ConfigVar metadata.
    case TAB_AIMBOT:    Aim_RenderMenuTab(); break;   // unitF groups render inside its columns
    case TAB_VISUALS:   Tab_VisualsRoot();    break;
    case TAB_MOVEMENT:  Tab_MovementRoot();   break;
    case TAB_MISC:      Tab_MiscRoot();       break;
    case TAB_FONTS:     Tab_Fonts();          break;
    case TAB_SETTINGS:  if (s_settingsSub == 1) Tab_Lua(); else Tab_Settings(); break;
    }
    FlowEnd();

    ImGui::EndChild();
    ImGui::PopID();
    ImGui::Unindent(mf::kTabW + st.ItemSpacing.x);

    ImGui::End();

    // own window right of the menu, visuals > esp page only, menu band look: botox csgo-sdk
    // hacks/menu/menu.cpp ~1024-1057 (g_esp_preview.draw_body()). Fades with the menu: render.cpp SplitDrawData.
    float sideX = wp.x + wz.x + 10.f;
    if (s_tab == TAB_VISUALS && s_visualsSub == 0 && !s_search[0] && g_espPreview)
    {
        const float padding = st.WindowPadding.x;
        const ImVec2 size(EspPreview::kWidth + padding * 2.f, mf::kHeaderH + EspPreview::kHeight + padding * 2.f);

        ImGui::SetNextWindowPos(ImVec2(sideX, wp.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);

        // this imgui fork defaults WindowMinSize to 300 wide: the window grew past the band + outline
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.f, 1.f));
        const bool open = ImGui::Begin("botox-esp-preview", nullptr,
                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | (s_animProgress < 0.999f ? ImGuiWindowFlags_NoInputs : 0));
        ImGui::PopStyleVar();
        if (open)
        {
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            const ImVec2 pp = ImGui::GetWindowPos();

            // band, like the root window's top band above (kBandBg/rnd reused from there)
            pdl->PushClipRect(pp, ImVec2(pp.x + size.x, pp.y + mf::kHeaderH), false);
            pdl->AddRectFilled(pp, ImVec2(pp.x + size.x, pp.y + mf::kHeaderH), kBandBg(), rnd, ImDrawFlags_RoundCornersTop);
            pdl->PopClipRect();
            RenderFadedGradientLine(pdl, ImVec2(pp.x, pp.y + mf::kHeaderH - 1.f), ImVec2(size.x, 1.f), ImColor(Accent()));

            ImGui::PushFont(BoldFont());
            const char* title = "esp preview";
            const ImVec2 titleSize = ImGui::CalcTextSize(title);
            pdl->AddText(ImVec2(pp.x + (size.x - titleSize.x) * 0.5f, pp.y + (mf::kHeaderH - titleSize.y) * 0.5f),
                         ImGui::GetColorU32(ImGuiCol_Text), title);
            ImGui::PopFont();

            ImGui::SetCursorPos(ImVec2(padding, mf::kHeaderH + padding));
            EspPreview::DrawBody();

            // window outline last, over everything else in the window
            pdl->PushClipRect(pp, ImVec2(pp.x + size.x, pp.y + size.y), false);
            pdl->AddRect(pp, ImVec2(pp.x + size.x, pp.y + size.y), kWindowBorder(), rnd);
            pdl->PopClipRect();
        }
        ImGui::End();

        sideX += size.x + 10.f;
    }

    // own window, still in the style scope: menu colours + font, fades with the menu (render.cpp SplitDrawData)
    const MsSettings ms = { &g_msOpen, &g_msNight, &g_msDiff, &g_msWidth, &g_msHeight, &g_msMines };
    Minesweeper_Draw(ms, BoldFont(), g_fontMs19, s_animProgress >= 0.999f, ImVec2(sideX, wp.y));
}
