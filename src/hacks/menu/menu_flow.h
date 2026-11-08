#pragma once
// ==== menu layout math ==============================================================
// Every number the reworked menu positions itself with, in one engine-free header so
// tests/menuflow_test.cpp can run the same code the menu runs. Nothing here touches
// ImGui: it takes rects in and gives rects back.
//
// The look is catalyst's (USELESSSRCCODE\catalyst, project/core/menu/menu.cpp +
// external/zdraw/zui): a window, one panel inset inside it, a header strip with the
// wordmark left and the tabs right, and a content panel holding group boxes -- a box
// whose title strip cuts a notch out of its own top border.
//
// Shapes are catalyst's; the spacing is dih's own, one notch tighter everywhere (see the
// metrics block). zui's values are named beside each for reference. The widget half of
// the density lives in render.cpp's style (padding + spacing) and in the fork's
// Checkbox / SliderScalar / BeginCombo: checkbox 11, slider track 7, combo 18.

namespace mf {

struct Rect { float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    float right()  const { return x + w; }
    float bottom() const { return y + h; } };

// ---- chrome metrics ----------------------------------------------------------------
// catalyst's numbers, pulled in one notch across the board: it draws at 680x855 for
// three tabs, dih has six tabs and thirty-odd boxes in the same window, so every gap
// here is the tighter one the old menu used. Shapes are catalyst's, density is dih's.
constexpr float kInset     = 5.0f;    // gap between the nested panels (zui 6)
constexpr float kHeaderH   = 25.0f;   // header strip (zui 28)
constexpr float kPanelPad  = 8.0f;    // zui window_padding 10
constexpr float kSecTitleH = 17.0f;   // zui group_box_title_height 18 (13px bold needs 17)
constexpr float kSecGap    = 8.0f;    // between stacked group boxes = botox ItemSpacing.y after EndChild
constexpr float kColGap    = 8.0f;    // between the two columns = style.ItemSpacing.x (botox_menu_spec.md section 5: ItemSpacing 8,8)
constexpr float kTabGap    = 13.0f;   // between header tab labels (zui 16)
constexpr float kFadeRatio = 0.15f;   // accent line: share of the width spent fading
constexpr float kSecMinH   = 20.0f;   // a box can never measure shorter than its notch

// ---- sidebar (botox_menu_spec.md section 2) ----------------------------------------
// botox's left sidebar of boxed tabs, numbers copied verbatim -- only the tab/page
// NAMES differ (stage 3). A sub-page row is 18px pitch (17 tall + 1 gap) per the spec.
constexpr float kTabW      = 100.0f;
constexpr float kTabH      = 24.0f;
constexpr float kTabGapY   = 6.0f;    // vertical gap between stacked tab boxes
constexpr float kPageH     = 17.0f;
constexpr float kPageGap   = 1.0f;
constexpr float kPagePitch = kPageH + kPageGap;   // 18, the flyout clip-height unit
constexpr float kSidebarGap = 8.0f;   // between the sidebar and the content area (ItemSpacing-ish)

// Top-left y of tab box `idx` (0-based), given how many of the tabs ABOVE it are
// currently open with `openPages[i]` pages showing (flyout push-down).
inline float TabBoxY(float startY, int idx, const int* openCount, const float* openAnim)
{
    float y = startY;
    for (int i = 0; i < idx; ++i)
        y += kTabH + kTabGapY + (openCount ? (float)openCount[i] * kPagePitch * (openAnim ? openAnim[i] : 0.f) : 0.f);
    return y;
}

// ---- two columns -------------------------------------------------------------------
inline float ColumnWidth(float availW)
{
    const float w = (availW - kColGap) * 0.5f;
    return w > 0.f ? w : 0.f;
}

// Masonry: the next box goes in whichever column is currently shorter, ties to the
// left one. Heights are last frame's measurements, so the assignment is stable --
// it only moves when a box's own content changes size.
inline int PickColumn(float colBottom0, float colBottom1)
{
    return (colBottom0 <= colBottom1 + 0.5f) ? 0 : 1;
}

// ---- group box ---------------------------------------------------------------------
// The body starts half a title down, so the title strip straddles the body's top
// border and the notch fill can cut that border open (catalyst begin_group_box).
inline Rect SectionBody(const Rect& sec)
{
    return { sec.x, sec.y + kSecTitleH * 0.5f, sec.w, sec.h - kSecTitleH * 0.5f };
}

// The notch: a title-height box at the section's top, wide enough for the title plus
// pad on both sides. Filled + outlined over the body border, then the title on top.
inline Rect SectionNotch(const Rect& sec, float titleW, float pad = 4.0f)
{
    float w = titleW + pad * 2.0f;
    const float maxW = sec.w - (kPanelPad - pad) - pad;   // never past the right border
    if (w > maxW) w = maxW > 0.f ? maxW : 0.f;
    return { sec.x + kPanelPad - pad, sec.y, w, kSecTitleH };
}

// Total height of a section whose body needs bodyH. Inverse of SectionBody.
inline float SectionHeight(float bodyH)
{
    const float h = kSecTitleH * 0.5f + bodyH;
    return h > kSecMinH ? h : kSecMinH;
}
inline float SectionBodyHeight(float secH)
{
    const float h = secH - kSecTitleH * 0.5f;
    return h > 1.f ? h : 1.f;
}

// ---- accent line -------------------------------------------------------------------
// The 1px accent rule catalyst runs along the top and bottom inside edge of a panel:
// transparent -> accent over the first slice, solid across the middle, accent ->
// transparent over the last. At small widths the two fades would overlap and the
// middle would come out negative, so the fade is capped at half the width.
struct FadeLine { float fadeW = 0.f, solidX = 0.f, solidW = 0.f, endX = 0.f; };
inline FadeLine AccentLine(float x, float w, float ratio = kFadeRatio)
{
    FadeLine l{};
    if (w <= 0.f) return l;
    l.fadeW = w * ratio;
    if (l.fadeW * 2.0f > w) l.fadeW = w * 0.5f;
    l.solidX = x + l.fadeW;
    l.solidW = w - l.fadeW * 2.0f;
    if (l.solidW < 0.f) l.solidW = 0.f;
    l.endX = l.solidX + l.solidW;                 // start of the trailing fade
    return l;
}

// ---- header tab strip --------------------------------------------------------------
// Tabs are text only, right aligned, laid out right to left off the panel's right
// padding (catalyst draw_header). Returns the x of tab `idx` given every label width.
inline float TabStripWidth(const float* labelW, int n)
{
    if (n <= 0) return 0.f;
    float total = 0.f;
    for (int i = 0; i < n; ++i) total += labelW[i];
    return total + kTabGap * (float)(n - 1);
}
inline float TabX(float rightEdge, const float* labelW, int n, int idx)
{
    float x = rightEdge;
    for (int i = n - 1; i > idx; --i) x -= labelW[i] + kTabGap;
    return x - labelW[idx];
}

// ---- underline under a selected inline tab -----------------------------------------
// Catalyst's own sub-tab row (its weapon groups): the same fade rule as the panel
// rule, sized to the label instead of the panel.
inline FadeLine Underline(float x, float w)
{
    return AccentLine(x, w, 0.3f);
}

} // namespace mf
