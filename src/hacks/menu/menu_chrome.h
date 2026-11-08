#pragma once
// ==== catalyst chrome ================================================================
// The menu's frame, in the shape catalyst draws it (USELESSSRCCODE\catalyst,
// project/core/menu/menu.cpp): the window, ONE panel inset inside it, a header strip
// with the mark on the left and the tabs on the right, and a content panel of group
// boxes. Metrics come from menu_flow.h, which is engine-free and gate-tested
// (tests/menuflow_test.cpp).
//
// Header and not a block in menu.cpp so the ImGui half can be driven headless:
// tests/menuchrome_test.cpp runs a real ImGui context over these calls and checks the
// boxes that come out (heights converge, columns balance, nothing overlaps or leaves
// the panel) -- none of which is visible from the pure math alone.
//
// Panel greys, all zui::style: window 18, nested 12, group box 14; borders 45 / 40 / 38.
// They are NOT style colours because three different borders would need three enums --
// ImGuiCol_Separator carries the window's 45 (it is also what render.cpp's UiPanel uses)
// and the other two are named here.
#include "../../dependencies/imgui/imgui.h"
#include "../../dependencies/imgui/imgui_internal.h"
#include "menu_flow.h"

#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstdio>

extern ImFont* g_fontBold;   // dllmain.cpp
extern ImFont* g_menuReg;    // dllmain.cpp -- botox chrome font, verdana 11
extern ImFont* g_menuBold;   // dllmain.cpp -- botox chrome font, verdana bold 11

inline const ImU32 kGroupBg     = IM_COL32(14, 14, 14, 255);
inline const ImU32 kGroupBorder = IM_COL32(38, 38, 38, 255);
inline const ImU32 kPanelBorder = IM_COL32(40, 40, 40, 255);
// botox_menu_spec.md section 1/2: #191919 bands + tab boxes, #323232 @ full and @ ~39%.
// Functions, not consts: menu style (settings tab) repaints these live via ImGuiCol_MenuBand/
// MenuWindowOutline/MenuOutline, so the band/border colour has to read the style each draw.
inline ImU32 kBandBg()       { return ImGui::GetColorU32(ImGuiCol_MenuBand); }
inline ImU32 kWindowBorder() { return ImGui::GetColorU32(ImGuiCol_MenuWindowOutline); }
inline ImU32 kTabBorder()    { return ImGui::GetColorU32(ImGuiCol_MenuOutline); }

// The accent, for everything the menu draws by hand. render.cpp now writes zui's
// 160,170,220 into BOTH ImGuiCol_CheckMark and the botox-ported ImGuiCol_Accent slot
// (stage 1 added the slot, stage 2 is what actually feeds it), so the widgets and the
// chrome can never drift apart either way they read it.
inline ImU32 Accent(float alphaMul = 1.f) { return ImGui::GetColorU32(ImGuiCol_Accent, alphaMul); }

inline ImFont* BoldFont() { return g_menuBold ? g_menuBold : (g_fontBold ? g_fontBold : ImGui::GetFont()); }
inline ImFont* RegFont()  { return g_menuReg ? g_menuReg : ImGui::GetFont(); }

// zui's accent rule: transparent -> accent -> solid -> accent -> transparent, 1px tall.
// Three pieces, because a single gradient rect can only fade one way.
inline void AccentRule(ImDrawList* dl, float x, float y, float w, float ratio = mf::kFadeRatio)
{
    const mf::FadeLine l = mf::AccentLine(x, w, ratio);
    if (l.fadeW <= 0.f && l.solidW <= 0.f) return;
    const ImU32 c = Accent();
    const ImU32 t = Accent(0.f);
    if (l.fadeW > 0.f)
    {
        dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + l.fadeW, y + 1.f), t, c, c, t);
        dl->AddRectFilledMultiColor(ImVec2(l.endX, y), ImVec2(l.endX + l.fadeW, y + 1.f), c, t, t, c);
    }
    if (l.solidW > 0.f)
        dl->AddRectFilled(ImVec2(l.solidX, y), ImVec2(l.solidX + l.solidW, y + 1.f), c);
}

// A flat panel with a 1px outline. FOUR FILLED STRIPS, NOT AddRect: at thickness 1
// AddRect takes ImGui's anti-aliased thin-line path, the alpha ramps over +/-1px and a
// 38 border lands on screen as ~32 -- next to a group box border drawn any other way
// the two greys visibly disagree.
inline void PanelRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 fill, ImU32 border)
{
    if (fill) dl->AddRectFilled(a, b, fill);
    dl->AddRectFilled(ImVec2(a.x, a.y),       ImVec2(b.x,       a.y + 1.f), border);
    dl->AddRectFilled(ImVec2(a.x, b.y - 1.f), ImVec2(b.x,       b.y),       border);
    dl->AddRectFilled(ImVec2(a.x, a.y),       ImVec2(a.x + 1.f, b.y),       border);
    dl->AddRectFilled(ImVec2(b.x - 1.f, a.y), ImVec2(b.x,       b.y),       border);
}

// Bold section label + a line to the box's right edge: the SUB header inside a group box
// (the boxes themselves are Section below), and the header the ps point window uses.
// no pre-Dummy: gap above the header = 2x ItemSpacing ~= gap below.
inline void SectionHeader(const char* label)
{
    ImGui::PushFont(BoldFont());
    // TextActive, not literal white: the palette's top end is 235,235,235
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextActive));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (ImGui::IsSearchHidden()) return;   // filtered out: no rule off the last shown row

    {
        const ImVec2 tmax = ImGui::GetItemRectMax();
        const float midY = (ImGui::GetItemRectMin().y + tmax.y) * 0.5f;
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(tmax.x + 6.f, midY),
                                            ImVec2(win->WorkRect.Max.x, midY),
                                            ImGui::GetColorU32(ImGuiCol_Border), 1.f);
    }
    ImGui::Dummy(ImVec2(0.f, 1.f));
}

// ---- the two-column group box flow -------------------------------------------------
// Section("title") opens a group box and closes the previous one, so a tab body stays a
// flat list of calls -- it never has to know which column it is in or how tall it is.
//
// HEIGHTS ARE LAST FRAME'S. A box is an ImGui child and a child needs its size up front,
// so the flow measures the content it just drew, caches it per box id, and uses that
// next frame (catalyst's begin_group_box does exactly this, down to the default). A box
// whose content just grew is clipped for ONE frame. The cache is only written while the
// box was actually drawn: a box scrolled out of view has SkipItems set, submits nothing,
// and would otherwise cache itself down to an empty strip.
struct MenuFlowState
{
    bool  inFlow = false;
    bool  open = false;       // a box (menu_group_begin/end) is currently open
    bool  visible = false;    // ...and it was not culled, so its height may be cached
    int   col = 0;
    float colX[2] = { 0.f, 0.f };
    float colY[2] = { 0.f, 0.f };
    float colW = 0.f;
    ImGuiID id = 0;
    bool  originPending = false;    // the columns start at the FIRST box, not at FlowBegin
    float boxX = 0.f, boxY = 0.f;   // where the open box was placed, for drawn[]
    float boxH = 0.f;               // its RENDERED size this frame (read at open, not re-read
                                     // after menu_group_end -- that already wrote NEXT frame's
                                     // measured value, which is a different number on a frame
                                     // the content changed size)

    std::vector<mf::Rect> drawn;                       // this frame's boxes, for the test
};
inline MenuFlowState g_flow;

// ---- botox_menu_spec.md section 3a, literal: menu_group_begin remembers its OWN last
// frame's height in ImGui's native per-id StateStorage, not a hand-rolled map. Section()
// below reuses this exact storage (ImGui::GetStateStorage()->GetFloat/SetFloat keyed by
// the group's own ImGuiID) so a 2nd, truly bare-bones caller (a future single-column
// page, botox's menu_group_begin/end signature) shares the same cache a Section box
// uses, instead of two height systems disagreeing about the same id.
// botox imgui.cpp BeginChildEx(show_text = true) + RenderChildBorder, 1:1: 20px #191919 band
// above the box with the title centred in verdana bold (hover-fades 0.5 -> 1), accent fade line
// under it, #0F0F0F body, (50,50,50,100) outline round the whole thing. AlwaysUseWindowPadding:
// botox gets its 8px padding from border=true + ChildBorderSize 1; ours runs ChildBorderSize 0.
// `w` 0 = fill the column (botox: groups always sit inside a column child). `fixedH` > 0 = a
// box of that height instead of the cached content height (botox cvars_page: one BeginChild
// sized to the page, show_text = true).
// botox menu.cpp s_menu_flat: search results draw a page's own columns + groups flat, inside
// the result group the page lands in
inline bool g_menuFlat = false;

inline bool menu_group_begin(const char* name, float w = 0.f, float fixedH = 0.f)
{
    if (g_menuFlat) { ImGui::PushID(name); return true; }
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    const float h = fixedH > 0.f ? fixedH : ImGui::GetStateStorage()->GetFloat(ImGui::GetID(name), 40.f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    const bool ret = ImGui::BeginChild(name, ImVec2(w, h), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();

    ImGuiWindow* child = ImGui::GetCurrentWindow();
    const float rnd = ImGui::GetStyle().WindowRounding - 2.f;
    const ImVec2 top(child->Pos.x, child->Pos.y - 20.f);
    const ImVec2 bot(child->Pos.x + child->Size.x, child->Pos.y + child->Size.y);

    ImDrawList* pdl = parent->DrawList;
    pdl->AddRectFilled(top, ImVec2(bot.x, child->Pos.y), kBandBg(), rnd, ImDrawFlags_RoundCornersTop);
    auto anim = ImAnimationHelper(child->ChildId, ImGui::GetIO().DeltaTime);
    anim.Update(ImGui::IsMouseHoveringRect(top, bot, false) ? 3.f : -2.f, 1.f, 0.5f, 1.f);
    const char* end = ImGui::FindRenderedTextEnd(name);
    ImFont* f = BoldFont();
    const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, name, end);
    pdl->AddText(f, f->FontSize, ImVec2(top.x + (child->Size.x - ts.x) * 0.5f, top.y + (20.f - ts.y) * 0.5f),
                 ImGui::GetColorU32(ImGuiCol_Text, anim.AnimationData->second), name, end);
    RenderFadedGradientLine(pdl, ImVec2(top.x, child->Pos.y), ImVec2(child->Size.x, 1.f), ImColor(Accent()));

    ImRect clip(top, bot);
    clip.ClipWithFull(parent->ClipRect);
    child->DrawList->PushClipRect(clip.Min, clip.Max);
    child->DrawList->AddRectFilled(child->Pos, bot, ImGui::GetColorU32(ImGuiCol_ChildBg), rnd, ImDrawFlags_RoundCornersBottom);
    child->DrawList->AddRect(top, bot, kTabBorder(), rnd);
    child->DrawList->PopClipRect();
    return ret;
}
inline void menu_group_end()
{
    if (g_menuFlat) { ImGui::PopID(); return; }
    ImGuiWindow* group = ImGui::GetCurrentWindow();
    if (!group->SkipItems)
        group->ParentWindow->StateStorage.SetFloat(group->ChildId,
            ImMax(group->DC.CursorMaxPos.y - group->DC.CursorStartPos.y + group->WindowPadding.y * 2.f, 1.f));
    ImGui::EndChild();
}
// botox_menu_spec.md section 3a, literal: sequential two-column flow (no masonry --
// the caller picks the column by calling _next). Section()'s own masonry below is the
// one every tab body already calls; these are here for a page that wants botox's exact
// fixed-column shape instead.
inline void menu_columns_begin()
{
    if (g_menuFlat) return;
    const float w = ImFloor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    ImGui::BeginChild("##mc_left", ImVec2(w, ImGui::GetContentRegionAvail().y), false, 0);
}
inline void menu_columns_next()
{
    if (g_menuFlat) return;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##mc_right", ImVec2(0.f, ImGui::GetContentRegionAvail().y), false, 0);
}
inline void menu_columns_end() { if (!g_menuFlat) ImGui::EndChild(); }

// botox_menu_spec.md section 3a: a group box is menu_group_begin/end (headerless
// bordered child, height cached in native StateStorage) -- no hand-drawn notch, no
// title text, the name is only its id. Positioned directly via SetCursorPos at the
// masonry's own colX/colY (not botox's literal "left column"/"right column" children:
// those are fixed-position-once-per-frame windows and re-entering one mid-frame to
// append a later box, as every masonry switch does, lands it BELOW the first use
// instead of beside it -- ImGui child windows re-derive their position from the
// parent's current cursor on every Begin, not just the first one this frame. Direct
// positioning sidesteps that and keeps the exact same masonry this fork already had).
inline void SectionClose()
{
    if (!g_flow.open) return;
    if (g_flow.visible) ImGui::PopItemWidth();          // balance Section()'s push before EndChild
    menu_group_end();                                   // botox's own function, verbatim -- this is
                                                         // what caches NEXT frame's height; g_flow.boxH
                                                         // (read at Section()-open, below) is THIS
                                                         // frame's already-rendered size, a different
                                                         // number on the frame content changed size
    g_flow.drawn.push_back(mf::Rect{ g_flow.boxX, g_flow.boxY + 20.f, g_flow.colW, g_flow.boxH });
    g_flow.colY[g_flow.col] += 20.f + g_flow.boxH + mf::kSecGap;   // title band + body
    g_flow.open = false;
    g_flow.visible = false;
}

// Opens a group box in whichever column is shorter. Must be called inside the content
// child, between FlowBegin and FlowEnd.
inline void Section(const char* title)
{
    if (!g_flow.inFlow) { SectionHeader(title); return; }   // outside a tab body: plain header

    SectionClose();

    // The columns start where the FIRST box is asked for, not where FlowBegin was
    // called: a tab body is free to draw a line of text or a row of buttons above its
    // boxes, and the flow lands underneath it instead of on top of it.
    if (g_flow.originPending)
    {
        const ImVec2 o = ImGui::GetCursorPos();
        g_flow.colX[0] = o.x;
        g_flow.colX[1] = o.x + g_flow.colW + mf::kColGap;
        g_flow.colY[0] = g_flow.colY[1] = o.y;
        g_flow.originPending = false;
    }

    const int col = mf::PickColumn(g_flow.colY[0], g_flow.colY[1]);
    g_flow.col = col;
    g_flow.boxX = g_flow.colX[col];
    g_flow.boxY = g_flow.colY[col];
    ImGui::SetCursorPos(ImVec2(g_flow.boxX, g_flow.boxY));

    g_flow.id = ImGui::GetID(title);
    g_flow.boxH = ImGui::GetStateStorage()->GetFloat(g_flow.id, 40.f);   // this frame's RENDER size
    g_flow.open = true;
    g_flow.visible = menu_group_begin(title, g_flow.colW);   // column width: 0 here = the WHOLE content width,
                                                              // the left box ran under the right column
    if (g_flow.visible)
        ImGui::PushItemWidth(-1.f);
}

inline void FlowBegin()
{
    g_flow.inFlow = true;
    g_flow.originPending = true;
    g_flow.open = false;
    g_flow.colW = mf::ColumnWidth(ImGui::GetContentRegionAvail().x);
    g_flow.drawn.clear();
}

inline void FlowEnd()
{
    SectionClose();
    const bool anyBox = !g_flow.originPending;
    g_flow.inFlow = false;
    g_flow.originPending = false;
    if (!anyBox) return;      // a tab that drew no box at all: leave its cursor alone
    // One item at the bottom of the taller column, so the content keeps scrolling to
    // the end of the flow instead of stopping wherever the last widget happened to land.
    const float bottom = (g_flow.colY[0] > g_flow.colY[1] ? g_flow.colY[0] : g_flow.colY[1]);
    ImGui::SetCursorPos(ImVec2(g_flow.colX[0], bottom));
    ImGui::Dummy(ImVec2(1.f, 1.f));
}

// ---- sidebar: botox's left column of boxed tabs + sub-page flyouts ----------------
// botox_menu_spec.md section 2. One box per top-level tab (100x24, #191919 fill,
// #323232@39% border), hover/selected springs drive label colour + a 1px accent
// underline when selected. A tab with pages gets a small flyout of 17px text rows
// directly under its box, height-clipped by its own open/close spring so it slides
// instead of popping. Stage 2 keeps TF2's existing 6 tabs/names (remap is stage 3);
// this just gives them botox's shape so a tab that later gains real sub-pages (like
// "movement" here, folding its 3 existing sub-tabs in) needs no new plumbing.
struct SidebarTab { const char* name; const char* const* pages; int pageCount; };

// One tab box + its flyout. `openT` is this tab's own open/close spring value [0,1],
// written back by the caller (it owns the array so every tab's spring persists).
// Returns true if this tab was clicked (always), writes into `page` if a page row
// was clicked. `topY` in/out: where the NEXT tab box should start.
inline bool SidebarTab_Draw(const SidebarTab& t, int tabIdx, bool selected, int& page,
                            float x, float& topY, float dt)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float scroll = ImGui::GetScrollY();
    const ImVec2 a(wp.x + x, wp.y + topY - scroll);
    const ImVec2 b(a.x + mf::kTabW, a.y + mf::kTabH);

    ImGui::PushID(tabIdx);   // scopes every id below (tab box + every page row) to THIS tab
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##tabbox", ImVec2(mf::kTabW, mf::kTabH));
    const bool hovered = ImGui::IsItemHovered();
    const unsigned int hid = ImGui::GetID("##tabhover"), sid = ImGui::GetID("##tabsel"), oid = ImGui::GetID("##tabopen");

    // botox tab_button: hover Update(2, +-2, 0.5) and selected Update(2, +-2) = 4/s, label =
    // Blend(white @ hover alpha, accent, lit) on all four channels, underline = full-width
    // faded line at lit alpha.
    auto hov = ImAnimationHelper(hid, dt); hov.Update(2.f, hovered ? 2.f : -2.f, 0.5f);
    auto sel = ImAnimationHelper(sid, dt); const float lit = sel.Update(2.f, selected ? 2.f : -2.f);

    const float rnd = ImGui::GetStyle().WindowRounding - 2.f;
    dl->AddRectFilled(a, b, kBandBg(), rnd);
    dl->AddRect(a, b, kTabBorder(), rnd);
    const ImColor accentC = ImGui::GetColorU32(ImGuiCol_Accent);
    RenderFadedGradientLine(dl, ImVec2(a.x, b.y - 1.f), ImVec2(mf::kTabW, 1.f),
                            ImColor(accentC.Value.x, accentC.Value.y, accentC.Value.z, lit));
    ImFont* bf = BoldFont();
    const ImVec2 ts = bf->CalcTextSizeA(bf->FontSize, FLT_MAX, 0.f, t.name);
    dl->AddText(bf, bf->FontSize, ImVec2(ImFloor(a.x + (mf::kTabW - ts.x) * 0.5f), ImFloor(a.y + (mf::kTabH - ts.y) * 0.5f)),
                ImColor::Blend(ImColor(1.f, 1.f, 1.f, hov.AnimationData->second), accentC, lit), t.name);

    // ---- flyout: only this tab's own pages, only while selected (or closing) -------
    // botox: open Update(3, +-3) = 9/s; rows start 1px under the box, 17 tall, 18 pitch.
    auto opn = ImAnimationHelper(oid, dt);
    const float open = opn.Update(3.f, (selected && t.pageCount > 0) ? 3.f : -3.f);
    float used = mf::kTabH + mf::kTabGapY;
    if (t.pageCount > 0 && open > 0.f)
    {
        const float flyoutH = t.pageCount * mf::kPagePitch * open;
        dl->PushClipRect(ImVec2(a.x, b.y), ImVec2(b.x, b.y + flyoutH), true);
        ImFont* rf = RegFont();
        for (int i = 0; i < t.pageCount; ++i)
        {
            const ImVec2 pa(a.x, b.y + 1.f + i * mf::kPagePitch * open);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(pa);
            const bool pageClicked = ImGui::InvisibleButton("##page", ImVec2(mf::kTabW, mf::kPageH));
            const bool pageHover = open >= 1.f && selected && ImGui::IsItemHovered();
            const unsigned int phid = ImGui::GetID("##pagehov");   // scoped by the outer tab PushID + this PushID(i)
            const unsigned int psid = ImGui::GetID("##pagesel");
            ImGui::PopID();
            auto phov = ImAnimationHelper(phid, dt); phov.Update(2.f, pageHover ? 2.f : -2.f, 0.5f);
            auto psel = ImAnimationHelper(psid, dt); psel.Update(2.f, page == i ? 2.f : -2.f);
            ImColor pc = ImColor::Blend(ImColor(1.f, 1.f, 1.f, phov.AnimationData->second), accentC, psel.AnimationData->second);
            pc.Value.w *= open;
            const ImVec2 pts = rf->CalcTextSizeA(rf->FontSize, FLT_MAX, 0.f, t.pages[i]);
            dl->AddText(rf, rf->FontSize, ImVec2(ImFloor(pa.x + (mf::kTabW - pts.x) * 0.5f), ImFloor(pa.y + (mf::kPageH - pts.y) * 0.5f)),
                        pc, t.pages[i]);
            if (pageClicked && pageHover) page = i;   // "clicking a page only registers once open"
        }
        dl->PopClipRect();
        used += flyoutH;
    }

    ImGui::PopID();   // matches the PushID(tabIdx) at the top
    topY += used;
    return clicked;
}

// Inline tab row (catalyst's own weapon-group row): plain labels, the selected one in
// the accent over an accent rule sized to the label. Used by the movement tab's three
// sub-tabs.
inline void InlineTabs(const char* const* names, int count, int& sel)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = ImGui::GetTextLineHeight() + 4.f;
    for (int i = 0; i < count; ++i)
    {
        if (i) ImGui::SameLine(0.f, mf::kTabGap);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 ts = ImGui::CalcTextSize(names[i]);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##itab", ImVec2(ts.x, rowH));
        ImGui::PopID();
        if (ImGui::IsItemClicked(0)) sel = i;

        const bool on = (sel == i);
        const ImU32 col = on ? Accent()
                             : ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_TextHovered : ImGuiCol_TextDisabled);
        dl->AddText(ImVec2(p.x, p.y + (rowH - ts.y) * 0.5f - 1.f), col, names[i]);
        if (on) AccentRule(dl, p.x, p.y + rowH - 1.f, ts.x, 0.3f);
    }
    ImGui::Dummy(ImVec2(0.f, 0.f));
}
