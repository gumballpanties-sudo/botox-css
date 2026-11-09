#include "unitC_menu.h"
#include "menu.h"       // ImGui::OptionPopup / ColorRow
#include "../../globals/globals.h"
#include "../visuals/chams_layers.h"   // botox chams layer lists (config strings)
#include "../visuals/esp_preview.h"    // g_espPreview, top-of-group "esp preview" checkbox
#include <cstdio>
#include <initializer_list>

// botox menu_internal.h color_picker_alpha_flags / color_picker_no_alpha_flags
static constexpr ImGuiColorEditFlags kPickerAlpha = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
    ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
static constexpr ImGuiColorEditFlags kPickerNoAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
    ImGuiColorEditFlags_NoBorder;

// Called inside menu.cpp's "players" group (Page_VisualsEsp). botox menu_tab_visuals.cpp case 0
// "players" group, verbatim order / labels / ranges / popup widths. TF2 rows that differ: flags
// list (TF2 conditions); no weapon icon; skeleton has no "lag
// compensated" (backtrack removed). Player stencil / internal glow = aim/player_stencil.cpp.
void UnitC_Menu_visuals_esp()
{
    // botox csgo-sdk menu_tab_visuals.cpp: "esp preview" sits above "players", own window right
    // of the menu (menu.cpp, visuals > esp page only). See src/hacks/visuals/esp_preview.h.
    ImGui::Checkbox("esp preview", &g_espPreview);

    const bool players = g_espEnable;

    ImGui::Checkbox("players", &g_espEnable);
    if (g_espEnable) {
        ImGui::OptionPopup("esp settings##player esp settings", [&]() {
            ImGui::Checkbox("include teammates", &g_espTeammates);
            ImGui::SliderFloat("max distance##player esp", &g_espMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            if (g_espMaxDistance > 0.f)
                ImGui::Checkbox("fade with distance", &g_espDistanceFade);
            ImGui::Checkbox("only on sound", &g_espSoundOnly);
            if (g_espSoundOnly) {
                ImGui::SliderFloat("sound duration##player esp", &g_espSoundDuration, 0.2f, 5.f, "%.1fs");
                ImGui::Checkbox("sound fade##player esp", &g_espSoundFade);
                ImGui::SliderFloat("min volume##sound esp players", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("sound max distance##sound esp players", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            }
        }, ImVec2(220.f, -1));

        ImGui::Checkbox("bounding box##player", &g_espBox);
        if (g_espBox)
            ImGui::OptionPopup("bounding box settings##player bounding box settings", [&]() {
                ImGui::Checkbox("visibility colors##player box", &g_espBoxVisColors);
                if (g_espBoxVisColors) {
                    ImGui::ColorRow("visible##player box visible color", g_espBoxVisibleColor, kPickerAlpha);
                    ImGui::ColorRow("invisible##player box invisible color", g_espBoxInvisibleColor, kPickerAlpha);
                } else
                    ImGui::ColorRow("box color##player bounding box color", g_espBoxColor, kPickerAlpha);
                ImGui::Checkbox("3d bounding box##player", &g_espBox3d);
                if (!g_espBox3d)
                    ImGui::Checkbox("corner bounding box##player", &g_espBoxCorner);
                ImGui::SliderFloat("thickness##player box thickness", &g_espBoxThickness, 1.f, 5.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("rounding##player box rounding", &g_espBoxRounding, 0.f, 20.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("bounding box outline##player", &g_espBoxOutline);
                if (g_espBoxOutline) {
                    ImGui::ColorRow("outline color##player bounding box outline color", g_espBoxOutlineColor, kPickerAlpha);
                    ImGui::SliderFloat("outline thickness##player box outline thickness", &g_espBoxOutlineThickness, 1.f, 5.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                    if (!g_espBoxCorner && !g_espBox3d)
                        ImGui::Combo("outline side##player box outline side", &g_espBoxOutlineSide, "inside\0outside\0both\0");
                }
            }, ImVec2(220.f, -1));

        /* own row: fill without a frame is a valid look, box toggle must not gate it */
        ImGui::Checkbox("box fill##player", &g_espBoxFill);
        if (g_espBoxFill)
            ImGui::OptionPopup("box fill settings##player box fill settings", [&]() {
                ImGui::ColorRow(g_espBoxFillGradient ? "top color##player box fill color" : "fill color##player box fill color", g_espBoxFillColor, kPickerAlpha);
                ImGui::Checkbox("gradient##player box fill", &g_espBoxFillGradient);
                if (g_espBoxFillGradient)
                    ImGui::ColorRow("bottom color##player box fill bottom color", g_espBoxFillBottomColor, kPickerAlpha);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("name##player", &g_espName);
        if (g_espName)
            ImGui::OptionPopup("name settings##player name settings", [&]() {
                ImGui::Combo("position##player name", &g_espNamePosition, "above box\0below box\0inside top\0inside bottom\0");
                ImGui::Checkbox("visibility colors##player name", &g_espNameVisColors);
                if (g_espNameVisColors) {
                    ImGui::ColorRow("visible##player name visible color", g_espNameVisibleColor, kPickerAlpha);
                    ImGui::ColorRow("invisible##player name invisible color", g_espNameInvisibleColor, kPickerAlpha);
                } else
                    ImGui::ColorRow("name color##player name color", g_espNameColor, kPickerAlpha);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("health bar##player", &g_espHealthBar);
        if (g_espHealthBar)
            ImGui::OptionPopup("health bar settings##player health bar settings", [&]() {
                ImGui::Combo("bar side##player health bar", &g_espHealthBarSide, "left\0right\0top\0bottom\0corner\0");
                if (g_espHealthBarSide == 4)
                    ImGui::Combo("which corner##player health bar", &g_espHealthBarCornerPick, "bottom left\0bottom right\0top left\0top right\0");
                if (g_espHealthBarSide == 4 && !g_espBox) {
                    ImGui::SliderFloat("corner rounding##player health bar", &g_espHealthBarCornerRounding, 0.f, 20.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat("corner size##player health bar", &g_espHealthBarCornerSize, 0.1f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                }
                ImGui::SliderFloat("bar thickness##player health bar", &g_espHealthBarThickness, 1.f, 10.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("health bar outline", &g_espHealthBarOutline);
                ImGui::Checkbox("custom health bar color", &g_espHealthBarCustomColor);
                if (g_espHealthBarCustomColor) {
                    ImGui::ColorRow(g_espHealthBarGradient ? "top color##health bar color" : "bar color##health bar color", g_espHealthBarColor, kPickerAlpha);
                    ImGui::Checkbox("gradient##health bar", &g_espHealthBarGradient);
                    if (g_espHealthBarGradient)
                        ImGui::ColorRow("bottom color##health bar bottom color", g_espHealthBarBottomColor, kPickerAlpha);
                }
                ImGui::ColorRow("background color##health bar background color", g_espHealthBarBgColor, kPickerAlpha);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("health number##player", &g_espHealthText);
        if (g_espHealthText)
            ImGui::OptionPopup("health number settings##player health number settings", [&]() {
                ImGui::Checkbox("health number suffix", &g_espHealthSuffix);
                ImGui::Combo("health number style", &g_espHealthTextStyle, "standalone bottom\0follow health bar\0");
                ImGui::Checkbox("custom health number color", &g_espHealthTextCustomColor);
                if (g_espHealthTextCustomColor)
                    ImGui::ColorRow("number color##player health number color", g_espHealthTextColor, kPickerAlpha);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("weapon name##player", &g_espWeaponName);
        if (g_espWeaponName) {
            ImGui::SameLine();
            ImGui::ColorRow("##weapon name color", g_espWeaponNameColor, kPickerAlpha);
        }

        ImGui::Checkbox("weapon icon##player", &g_espWeaponIcon);
        if (g_espWeaponIcon) {
            ImGui::SameLine();
            ImGui::ColorRow("##weapon icon color", g_espWeaponIconColor, kPickerAlpha);
        }

        ImGui::Checkbox("weapon ammo bar##player", &g_espAmmoBar);
        if (g_espAmmoBar) {
            ImGui::SameLine();
            ImGui::ColorRow("##player ammo bar color", g_espAmmoBarColor, kPickerAlpha);
        }

        ImGui::Checkbox("distance##player", &g_espDistance);
        if (g_espDistance)
            ImGui::OptionPopup("distance settings##player distance settings", [&]() {
                ImGui::Combo("unit##player distance", &g_espDistanceUnit, "meters\0units\0feet\0");
                ImGui::ColorRow("distance color##player distance color", g_espDistanceColor, kPickerAlpha);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("flags##player", &g_espFlags);
        if (g_espFlags)
            ImGui::OptionPopup("flag settings##player flag settings", [&]() {
                ImGui::MultiCombo("flags##player flag list", &g_espFlagList, EspFlagNames(), EF_COUNT);
                ImGui::ColorRow("flag color##player flag color", g_espFlagsColor, kPickerAlpha);
                ImGui::ColorRow("alert color##player flag alert color", g_espFlagsAlertColor, kPickerAlpha);
            }, ImVec2(220.f, -1));
    }

    ImGui::BeginDisabled(!players);

    ImGui::Checkbox("player skeleton", &g_espSkeleton);
    if (g_espSkeleton)
        ImGui::OptionPopup("skeleton settings##player skeleton settings", [&]() {
            ImGui::Checkbox("visibility colors##player skeleton", &g_espSkeletonVisColors);
            if (g_espSkeletonVisColors) {
                ImGui::ColorRow("visible##player skeleton visible color", g_espSkeletonVisibleColor, kPickerAlpha);
                ImGui::ColorRow("invisible##player skeleton invisible color", g_espSkeletonInvisibleColor, kPickerAlpha);
            } else
                ImGui::ColorRow("skeleton color##player skeleton color", g_espSkeletonColor, kPickerAlpha);
            // cfg keeps 0 normal / 2 smooth (1 was lag compensated, gone with backtrack)
            int iSkel = g_espSkeletonType == 2 ? 1 : 0;
            if (ImGui::Combo("skeleton type##player skeleton", &iSkel, "normal\0smooth\0"))
                g_espSkeletonType = iSkel ? 2 : 0;
            ImGui::SliderFloat("thickness##player skeleton", &g_espSkeletonThickness, 0.5f, 5.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("show hitboxes", &g_espHitboxes);
    if (g_espHitboxes)
        ImGui::OptionPopup("hitboxes settings##player hitboxes settings", [&]() {
            ImGui::Combo("show##player hitboxes", &g_espHitboxesMode, "always\0on hit\0");
            ImGui::ColorRow("color##player hitboxes color", g_espHitboxColor, kPickerAlpha);
            ImGui::SliderFloat("thickness##player hitboxes", &g_espHitboxesThickness, 0.5f, 5.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
            if (g_espHitboxesMode == 1)
                ImGui::SliderFloat("duration##player hitboxes", &g_espHitboxesDuration, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1));

    // botox menu_tab_visuals.cpp "player glow" row + popup, verbatim order/labels/ranges.
    // What each option drives in TF2: aim/chams.cpp GlowPass banner.
    ImGui::Checkbox("player glow", &g_glowEnable);
    if (g_glowEnable)
        ImGui::OptionPopup("player glow settings", [&]() {
            const bool legacy   = g_glowLegacy;
            const bool gradient = !legacy && g_glowGradient;

            ImGui::ColorRow(gradient ? "visible inner##player vis glow color" : "player visible##player vis glow color", g_glowVisColor, kPickerAlpha);
            if (gradient)
                ImGui::ColorRow("visible outer##player vis glow outer color", g_glowVisOuterColor, kPickerAlpha);
            ImGui::ColorRow(gradient ? "invisible inner##player invis glow color" : "player invisible##player invis glow color", g_glowInvisColor, kPickerAlpha);
            if (gradient)
                ImGui::ColorRow("invisible outer##player invis glow outer color", g_glowInvisOuterColor, kPickerAlpha);

            ImGui::Checkbox("accent wave##player glow", &g_glowWave);
            if (g_glowWave) {
                ImGui::ColorRow("wave color##player glow wave", g_glowWaveColor, kPickerNoAlpha);
                ImGui::SliderFloat("wave speed##player glow wave", &g_glowWaveSpeed, 0.05f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
                if (!legacy)
                    ImGui::SliderFloat("wave size##player glow wave", &g_glowWaveSize, 0.05f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("wave strength##player glow wave", &g_glowWaveStrength, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }

            ImGui::Checkbox("edge pulse##player glow", &g_glowPulse);
            if (g_glowPulse) {
                ImGui::SliderFloat("pulse speed##player glow pulse", &g_glowPulseSpeed, 0.1f, 4.f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("pulse min##player glow pulse", &g_glowPulseMin, 0.f, 95.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            }

            ImGui::Checkbox("legacy##player glow", &g_glowLegacy);
            if (legacy) {
                ImGui::Combo("style##player glow legacy", &g_glowLegacyStyle, "default\0rim\0outline\0pulse\0");
                return;
            }

            ImGui::Checkbox("normal blending##player glow", &g_glowNormalBlend);
            if (g_glowNormalBlend)
                ImGui::SliderFloat("thickness##player glow", &g_glowThickness, 2.f, 30.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Checkbox("gradient##player glow", &g_glowGradient);
            if (gradient)
                ImGui::SliderFloat("inner size##player glow gradient", &g_glowGradientInner, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("player stencil", &g_esxStencil);
    if (g_esxStencil)
        ImGui::OptionPopup("player stencil settings", [&]() {
            ImGui::ColorRow("player visible##player stencil vis color", g_esxStencilVisColor, kPickerAlpha);
            ImGui::ColorRow("player invisible##player stencil invis color", g_esxStencilInvisColor, kPickerAlpha);
            ImGui::SliderFloat("thickness##player stencil", &g_esxStencilThickness, 0.5f, 8.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("internal player glow", &g_esxInnerGlow);
    if (g_esxInnerGlow)
        ImGui::OptionPopup("internal player glow settings", [&]() {
            ImGui::ColorRow("player visible##inner glow vis color", g_esxInnerGlowVisColor, kPickerAlpha);
            ImGui::ColorRow("player invisible##inner glow invis color", g_esxInnerGlowInvisColor, kPickerAlpha);
            ImGui::Checkbox("follow bones##inner glow", &g_esxInnerGlowBones);
            if (g_esxInnerGlowBones)
                ImGui::SliderFloat("width##inner glow", &g_esxInnerGlowBoneWidth, 10.f, 150.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            else
                ImGui::SliderFloat("size##inner glow", &g_esxInnerGlowSize, 2.f, 30.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("intensity##inner glow", &g_esxInnerGlowIntensity, 0.25f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1));

    ImGui::EndDisabled();

    ImGui::BeginDisabled(!players);

    // botox menu_tab_visuals.cpp:439, players.cpp:1517-1539 1:1: white 1px dot per backtrack
    // record at the head bone (aim/backtrack.h ring -- drawn in esp.cpp's bone block).
    ImGui::Checkbox("lag compensated trail", &g_espBacktrackTrail);

    ImGui::Checkbox("player avatar", &g_espAvatar);
    if (g_espAvatar)
        ImGui::OptionPopup("avatar settings##player avatar settings", [&]() {
            ImGui::Combo("position##player avatar", &g_espAvatarPosition, "next to name\0above box\0below box\0cover head\0");
            if (g_espAvatarPosition == 3)
                ImGui::SliderFloat("head scale##player avatar", &g_espAvatarHeadScale, 0.25f, 3.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            else
                ImGui::SliderFloat("avatar size##player avatar", &g_espAvatarSize, 6.f, 40.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("damage numbers", &g_espDamageNumbers);
    if (g_espDamageNumbers)
        ImGui::OptionPopup("damage numbers settings", [&]() {
            ImGui::ColorRow("damage numbers color##damage numbers color", g_espDmgColor, kPickerAlpha);
            ImGui::SliderFloat("duration", &g_espDmgDuration, 0.2f, 5.f, "%.1f s");
            ImGui::SliderFloat("rise speed", &g_espDmgSpeed, 0.f, 100.f, "%.0f u/s");
            ImGui::Checkbox("stack fast hits", &g_espDmgStack);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("out of fov arrows", &g_espOofArrows);
    if (g_espOofArrows)
        ImGui::OptionPopup("fov arrows settings", [&]() {
            ImGui::Combo("arrows show##out of fov arrows type", &g_espOofType, "players\0enemy sounds\0players + sounds\0");
            if (g_espOofType != 1) {
                ImGui::ColorRow("visible color##out of fov arrows color", g_espOofColor, kPickerAlpha);
                ImGui::ColorRow("behind wall color##out of fov arrows wall color", g_espOofWallColor, kPickerAlpha);
            }
            if (g_espOofType != 0) {
                ImGui::ColorRow("sound color##out of fov arrows sound color", g_espOofSoundColor, kPickerAlpha);
                ImGui::SliderFloat("sound arrow duration", &g_espOofSoundDuration, 0.2f, 5.f, "%.1fs");
                ImGui::Checkbox("sound arrow fade", &g_espOofSoundFade);
                ImGui::SliderFloat("min volume##sound esp arrows", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("max distance##sound esp arrows", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::Combo("arrows style##out of fov arrows style", &g_espOofStyle, "default\0interium\0");
            ImGui::SliderFloat("arrows width", &g_espOofWidth, 1.f, 60.f, "%.1f px");
            ImGui::SliderFloat("arrows height", &g_espOofHeight, 1.f, 60.f, "%.1f px");
            ImGui::SliderInt("arrows distance", &g_espOofDistance, 10, 500, "%d px");
            if (g_espOofType != 1)
                ImGui::Checkbox("player avatars", &g_espOofAvatar);
            ImGui::Checkbox("follow aspect ratio", &g_espOofAspect);
        }, ImVec2(220.f, -1));

    // TF2 extra (not in botox): an expanding ring where a sound was made
    ImGui::Checkbox("sound pings", &g_espSoundPings);
    if (g_espSoundPings) {
        ImGui::SameLine();
        ImGui::ColorRow("##ping color esp", g_espSoundColor, kPickerAlpha);
    }

    ImGui::EndDisabled();
}

// botox menu.cpp chams_dropdown: combo of every material; a picked one shows its order number
// and a colour swatch, clicking it again removes it. The list lives in a config string.
static void ChamsDropdown(const char* label, char* cfg)
{
    using namespace ChamsLayers;
    List l;
    Parse(cfg, l);

    // stack buffer: rebuilt every frame, never longer than all names once
    char preview[192] = "none";
    int written = 0;
    for (int i = 0; i < l.used; i++) {
        const int printed = std::snprintf(preview + written, sizeof(preview) - written, written ? ", %s" : "%s", kNames[l.slots[i]]);
        if (printed <= 0) break;
        written += printed;
        if (written >= (int)sizeof(preview) - 1) break;
    }

    if (!ImGui::BeginCombo(label, preview))
        return;

    bool changed = false;
    for (int material = 0; material < Count; material++) {
        const int slot = IndexOf(l, material);

        // same ##id either way: toggling must not change the item id
        char text[64] = {};
        if (slot >= 0) std::snprintf(text, sizeof(text), "%i. %s##chams%i", slot + 1, kNames[material], material);
        else           std::snprintf(text, sizeof(text), "%s##chams%i", kNames[material], material);

        const bool toggled = ImGui::Selectable(text, slot >= 0, ImGuiSelectableFlags_DontClosePopups | ImGuiSelectableFlags_AllowItemOverlap);
        ImGui::SetItemAllowOverlap();

        if (slot >= 0) {
            const ImVec2 next_position = ImGui::GetCursorPos();
            ImGui::SameLine();
            char color_id[96] = {};
            std::snprintf(color_id, sizeof(color_id), "##chams color%s%s", label, kNames[material]);
            // two tone: lit side swatch left (slot 1), shadow side right (slot 0)
            const bool two_tone = material == TwoTone;
            if (ImGui::ColorRow(color_id, l.colors[slot], kPickerAlpha, two_tone ? 1 : 0))
                changed = true;
            if (two_tone) {
                ImGui::SameLine();
                char dark_id[96] = {};
                std::snprintf(dark_id, sizeof(dark_id), "##chams dark color%s%s", label, kNames[material]);
                if (ImGui::ColorRow(dark_id, l.darks[slot], kPickerAlpha, 0))
                    changed = true;
            }
            ImGui::SetCursorPos(next_position);
        }

        if (toggled) {
            changed |= Toggle(l, material);
            break;   // botox: EndCombo + return right after a toggle
        }
    }
    ImGui::EndCombo();

    if (changed)
        Format(l, cfg, kCfgLen);
}

static bool ChamsHasLayers(const char* cfg)
{
    ChamsLayers::List l;
    ChamsLayers::Parse(cfg, l);
    return l.used > 0;
}

// Called inside menu.cpp's "chams" group (Page_VisualsEsp, col2). botox menu_tab_visuals.cpp
// "chams" group, verbatim order/labels/ranges. gloves = c_ arms, sleeve = classic v_ viewmodels;
// ubered/cloaked material rows removed (TF2 uber/cloak, no CS:GO/CSS counterpart).
void UnitC_Menu_visuals_chams()
{
    ImGui::Checkbox("enable enemy chams", &g_chamsEnable);

    ImGui::BeginDisabled(!g_chamsEnable);

    ChamsDropdown("visible material", g_chamsVisibleCfg);
    ChamsDropdown("occluded material", g_chamsOccludedCfg);

    ImGui::Checkbox("ragdolls##chams ragdolls", &g_chamsRagdolls);

    ChamsDropdown("backtrack material", g_chamsBacktrackCfg);

    ChamsDropdown("sound material", g_chamsSoundCfg);

    if (ChamsHasLayers(g_chamsSoundCfg)) {
        ImGui::SliderFloat("sound duration", &g_chamsSoundDuration, 0.2f, 5.f, "%.1fs");
        ImGui::Checkbox("sound fade", &g_chamsSoundFade);

        ImGui::SliderFloat("sound min volume##sound esp chams", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("sound max distance##sound esp chams", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
    }

    ImGui::Checkbox("backtrack xqz", &g_chamsBacktrackXqz);
    ImGui::Combo("backtrack type", &g_chamsBacktrackType, "oldest record\0all records\0aimbot target record\0");

    if (g_chamsBacktrackType == 1)
        ImGui::SliderInt("backtrack max ghosts", &g_chamsBacktrackMaxGhosts, 2, 64);

    ImGui::Checkbox("backtrack tick gradient", &g_chamsBacktrackGradient);

    ImGui::EndDisabled();

    ImGui::Checkbox("enable team chams", &g_chamsTeam);

    ImGui::BeginDisabled(!g_chamsTeam);

    ChamsDropdown("visible material##team chams", g_chamsTeamVisibleCfg);
    ChamsDropdown("occluded material##team chams", g_chamsTeamOccludedCfg);

    ImGui::EndDisabled();

    ImGui::Checkbox("enable viewmodel chams", &g_chamsLocalEnable);

    ImGui::BeginDisabled(!g_chamsLocalEnable);

    // botox menu_tab_visuals.cpp:690-694 has separate weapon / arms / sleeve dropdowns -- NOT
    // separable on CSS (checked 11-08, not re-guessed): visuals.cpp:508-519 ViewmodelModelKind
    // only returns "1 = arms stack" for a CS:GO-style loadout attachment model
    // ("/v_models/" or "*_arms.mdl", econ_entity.cpp's CEconEntity::DrawOverriddenViewmodel path).
    // CS:S has no loadout/skin system (grepped source-sdk-2013 + cssourcex64 for "_arms.mdl" /
    // "v_models/arms", 0 hits both) -- its stock content is one v_<weapon>.mdl per weapon with
    // the hands baked into the SAME mesh (chams.cpp:675's "CSS v_ model = gun + hands in one
    // mesh, one material" -- confirmed by ViewmodelModelKind's own "/weapons/v_" -> kind 3
    // "regardless of index" rule). One DrawModelExecute call, one ForceMaterialOverride, no
    // second draw or second material slot to point a different chams config at. Gloves/sleeve
    // skins: N/A anyway, CSS has no skins (never will). So one dropdown stays correct for CSS.
    ChamsDropdown("viewmodel material", g_chamsWeaponCfg);

    ImGui::EndDisabled();

    bool two_tone_used = false;
    for (const char* cfg : { g_chamsVisibleCfg, g_chamsOccludedCfg, g_chamsSoundCfg, g_chamsTeamVisibleCfg, g_chamsTeamOccludedCfg,
                             g_chamsWeaponCfg }) {
        ChamsLayers::List l;
        ChamsLayers::Parse(cfg, l);
        two_tone_used |= ChamsLayers::IndexOf(l, ChamsLayers::TwoTone) >= 0;
    }

    if (two_tone_used) {
        ImGui::SliderFloat("two tone threshold", &g_chamsTwoToneThreshold, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("two tone edge", &g_chamsTwoToneEdge, 0.f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
}

// Called inside menu.cpp's "edicts" group (Page_VisualsEsp, under "players"). botox
// menu_tab_visuals.cpp "edicts" group, verbatim order / labels / popups (aim/esp.cpp draws them):
// dropped weapons, planted bomb, bomb timer bar, thrown projectiles, grenade path.
static void UnitC_BombRows()
{
    // botox menu_tab_visuals.cpp:570-597
    ImGui::Checkbox("planted bomb", &g_espC4);
    if (g_espC4)
        ImGui::OptionPopup("planted bomb settings", []() {
            ImGui::Checkbox("icon##bomb esp", &g_espC4Icon);
            ImGui::Checkbox("countdown##bomb esp", &g_espC4Timer);
            ImGui::ColorRow("ticking##bomb esp color", g_espC4Color, kPickerAlpha);
            ImGui::ColorRow("defuse in time##bomb esp defuse color", g_espC4DefuseColor, kPickerAlpha);
            ImGui::ColorRow("defuse too late##bomb esp fail color", g_espC4FailColor, kPickerAlpha);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("bomb timer bar", &g_espC4Bar);
    if (g_espC4Bar)
        ImGui::OptionPopup("bomb timer bar settings", []() {
            ImGui::SliderInt("height##bomb timer bar", &g_espC4BarPosition, 0, 1000, "%d px");
        }, ImVec2(220.f, -1.f));
}

void UnitC_Menu_visuals_world()
{
    ImGui::Checkbox("dropped weapons", &g_esxDropped);
    if (g_esxDropped)
        ImGui::OptionPopup("dropped weapons settings", [&]() {
            ImGui::Checkbox("bounding box##dropped weapons", &g_esxDroppedBox);
            if (g_esxDroppedBox) {
                ImGui::ColorRow("box color##dropped weapons bounding box color", g_esxDroppedBoxColor, kPickerAlpha);
                ImGui::Checkbox("corner bounding box##dropped weapons", &g_esxDroppedBoxCorner);
                ImGui::Checkbox("bounding box outline##dropped weapons", &g_esxDroppedBoxOutline);
                if (g_esxDroppedBoxOutline)
                    ImGui::ColorRow("outline color##dropped weapons bounding box outline color", g_esxDroppedBoxOutlineColor, kPickerAlpha);
            }
            ImGui::Checkbox("name##dropped weapons", &g_esxDroppedName);
            if (g_esxDroppedName) {
                ImGui::SameLine();
                ImGui::ColorRow("##dropped weapons name color", g_esxDroppedNameColor, kPickerAlpha);
            }
            ImGui::Checkbox("icons##dropped weapons icon", &g_esxDroppedIcon);
            if (g_esxDroppedIcon) {
                ImGui::SameLine();
                ImGui::ColorRow("##dropped weapons icon color", g_esxDroppedIconColor, kPickerAlpha);
            }
        }, ImVec2(220.f, -1));

    UnitC_BombRows();

    ImGui::Checkbox("thrown projectiles", &g_esxThrown);
    if (g_esxThrown)
        ImGui::OptionPopup("thrown projectiles settings", [&]() {
            ImGui::Checkbox("thrown objects name", &g_esxThrownName);
            if (g_esxThrownName) {
                ImGui::SameLine();
                ImGui::ColorRow("##thrown objects name color", g_esxThrownNameColor, kPickerAlpha);
            }
            ImGui::Checkbox("thrown objects icon", &g_esxThrownIcon);
            if (g_esxThrownIcon) {
                ImGui::SameLine();
                ImGui::ColorRow("##thrown objects icon color", g_esxThrownIconColor, kPickerAlpha);
            }
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("grenade path", &g_esxGrenadePath);
    if (g_esxGrenadePath)
        ImGui::OptionPopup("grenade path settings", [&]() {
            ImGui::ColorRow("path##grenade path color", g_esxGrenadePathColor, kPickerAlpha);
            ImGui::ColorRow("bounce##grenade path bounce color", g_esxGrenadePathBounceColor, kPickerAlpha);
            ImGui::ColorRow("detonation##grenade path detonate color", g_esxGrenadePathDetonateColor, kPickerAlpha);
            ImGui::Checkbox("effect radius##grenade path", &g_esxGrenadePathRadius);
            ImGui::SliderFloat("thickness##grenade path", &g_esxGrenadePathThickness, 1.f, 5.f, "%.1f px");
        }, ImVec2(220.f, -1));
}
