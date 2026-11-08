#include "unitD_menu.h"
#include "menu.h"          // ImGui::OptionPopup
#include "../screen/screenfx_passes.h"
#include "../visuals/world_fx.h"
#include "../visuals/world_fx3_math.h"   // unit world: world/prop texture presets + categories

// Called inside menu.cpp's "screen" group (Page_VisualsScreen, col1): botox
// menu_tab_visuals.cpp 1132-1178 (third person, aspect ratio). Colour correction follows in
// menu.cpp, then UnitD3_Menu_visuals_screen (sharpen .. show depth).
void UnitD_Menu_visuals_screen()
{
    ImGui::Checkbox("third person", &g_thirdPersonEnable);
    if (g_thirdPersonEnable)
        ImGui::OptionPopup("third person settings", [&]() {
            ImGui::Label("third person keybind");
            ImGui::Keybind("third person key", &g_sfxThirdPersonKey, &g_sfxThirdPersonKeyMode);

            ImGui::SliderFloat("distance##third person", &g_thirdPersonDistance, 20.f, 300.f, "%.0f u",
                               ImGuiSliderFlags_AlwaysClamp);

            // botox menu_tab_settings.cpp:2211-2214 m_third_person_offset_side/up
            ImGui::SliderFloat("offset side##third person", &g_thirdPersonOffsetSide, -60.f, 60.f, "%.0f u",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("offset up##third person", &g_thirdPersonOffsetUp, -60.f, 60.f, "%.0f u",
                               ImGuiSliderFlags_AlwaysClamp);

            ImGui::Checkbox("wall collision##third person", &g_sfxThirdPersonCollision);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("aspect ratio", &g_aspectRatioEnable);
    if (g_aspectRatioEnable)
        // slider edits a pending value, apply commits it (botox: r_aspectratio re-lays the view)
        ImGui::OptionPopup("aspect ratio settings", [&]() {
            static float pending_ratio = 0.f;
            static float applied_ratio = 0.f;
            if (pending_ratio <= 0.f || applied_ratio != g_aspectRatioValue) {
                pending_ratio = g_aspectRatioValue;
                applied_ratio = g_aspectRatioValue;
            }
            ImGui::SliderFloat("ratio##aspect ratio", &pending_ratio, 0.5f, 4.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("apply##aspect ratio", ImVec2(-1.f, 15.f))) {
                g_aspectRatioValue = pending_ratio;
                applied_ratio      = pending_ratio;
            }

            ImGui::Checkbox("stretch hud##aspect ratio", &g_sfxAspectHud);
            ImGui::Checkbox("stretch crosshair##aspect ratio", &g_sfxAspectCrosshair);
            ImGui::Checkbox("stretch cheat overlays##aspect ratio", &g_sfxAspectCheatOverlay);
        }, ImVec2(220.f, -1.f));
}

// Called inside menu.cpp's "world" group (Page_VisualsWorld, col1), after "flip world":
// botox menu_tab_visuals.cpp world group from "dlight" down, verbatim order / labels / ranges /
// popup widths. 11-03 status (tabs_label_diff.txt vis.world):
//   - custom molotov colour: N/A, css-community-master\mp\src\game\shared\cstrike has no
//     molotov/incendiary weapon (grepped -i "molotov|incendiary", 0 hits) -- CS:S's 3 grenades
//     are HE/flash/smoke only (weapon_hegrenade/flashbang/smokegrenade.cpp).
//   - custom smoke colour / custom blood colour / remove smoke / remove flash: CSS-real (CSS has
//     weapon_smokegrenade.cpp and CCSPlayer::m_flFlashDuration/m_flFlashMaxAlpha), NOT ported yet.
//     Blocked this pass: CS:S smoke is a legacy CSimpleEmitter particle entity
//     (c_particle_smokegrenade.cpp), not a single named material like the rain/snow sprite
//     WorldFx2_OnFindMaterial already tints -- needs its own render hook, left for a follow-up
//     pass instead of guessing at an untested hook. Blood: same, no verified material/decal name
//     yet. NEEDS FROM OTHERS / follow-up, see VISTAB report.
//   - old shaders / old shaders settings: N/A. CS:S ships only Source 2004/2006-era shaders
//     (VertexLitGeneric etc, no PBR path) -- there is no "new" shader for botox's toggle to fall
//     back FROM, so the row has nothing to switch.
//   - weapon sheen / delay / normal blend / shift left / shift right: NOT ported yet (see
//     "normal blend sheen" below, which is a TF2 killstreak-sheen reinterpretation, not this row).
//   - bullet impacts "server color": not ported, no verified CS:S bullet_impact client/server
//     colour split found this pass -- NEEDS FROM OTHERS / follow-up.
// World/prop texture presets are TF2 textures under botox's labels (world_fx3_math.h, pre-existing,
// not touched this pass -- not CSS textures yet, flagged for whoever owns world_fx3_math.h).
// Our extra row after botox's: minecraft sky.
// botox menu_internal.h color_picker_alpha_flags / color_picker_no_alpha_flags
static constexpr ImGuiColorEditFlags kWdPickerAlpha = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
    ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
static constexpr ImGuiColorEditFlags kWdPickerNoAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
    ImGuiColorEditFlags_NoBorder;

// botox menu.cpp modulation_row: checkbox, picker on the same line, brightness slider under it
static void WdModulationRow(const char* label, bool* enable, float color[4], float* brightness)
{
    ImGui::Checkbox(label, enable);
    if (!*enable)
        return;
    char picker[64] = {}, slider[64] = {};
    snprintf(picker, sizeof(picker), "##%s color picker", label);
    snprintf(slider, sizeof(slider), "brightness##%s", label);
    ImGui::SameLine();
    ImGui::ColorRow(picker, color, kWdPickerAlpha);
    ImGui::SliderFloat(slider, brightness, 0.f, 400.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
}

void UnitD_Menu_visuals_world()
{
    // botox menu_tab_visuals.cpp:762-777 sun angle. CSS sink = env_shadow_control netvars
    // (css_bridge.h CssBridge_SunAngleSet banner) -- inactive (SUN: log line) on a map with no
    // env_shadow_control entity placed.
    ImGui::Checkbox("sun angle", &g_sunAngleEnable);
    if (g_sunAngleEnable) {
        ImGui::OptionPopup("sun angle settings", [&]() {
            ImGui::SliderFloat("pitch##sun angle", &g_sunAnglePitch, -180.f, 180.f, "%.1f");
            ImGui::SliderFloat("yaw##sun angle", &g_sunAngleYaw, -180.f, 180.f, "%.1f");
            ImGui::SliderFloat("roll##sun angle", &g_sunAngleRoll, -180.f, 180.f, "%.1f");

            ImGui::SliderFloat("rotation speed##sun angle", &g_sunAngleSpeed, 0.f, 90.f, "%.1f deg/s");

            ImGui::SliderFloat("shadow distance##sun angle", &g_sunAngleShadowDistance, 0.f, 3000.f, "%.0f u");
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("dlight", &g_dlightEnable);
    if (g_dlightEnable) {
        ImGui::OptionPopup("dlight settings", [&]() {
            ImGui::ColorRow("enemies##dlight", g_dlightColor, kWdPickerNoAlpha);
            ImGui::Checkbox("local player##dlight", &g_wdDlightLocal);
            if (g_wdDlightLocal)
                ImGui::ColorRow("local##dlight local color", g_wdDlightLocalColor, kWdPickerNoAlpha);
            ImGui::SliderFloat("radius##dlight", &g_dlightRadius, 25.f, 1000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("brightness##dlight", &g_wdDlightBrightness, 0, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Combo("ragdoll gravity", &g_wdRagdollGravity, "off\0away\0fly up\0");
    if (g_wdRagdollGravity > 0)
        ImGui::SliderFloat("strength##ragdoll gravity", &g_wdRagdollGravityStrength, 0.1f, 5.f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);

    // botox :823-842 order: smoke, molotov, blood. molotov = N/A (banner, u_world.h). Tinted via
    // world_fx2.cpp's FindMaterial hook (WorldFx2_OnFindMaterial), same ColorModulate slot 28 the
    // rain/snow tint right below already uses.
    ImGui::Checkbox("custom smoke color", &g_customSmokeEnable);
    if (g_customSmokeEnable) {
        ImGui::SameLine();
        ImGui::ColorRow("##custom smoke color picker", g_customSmokeColor, kWdPickerAlpha);
    }

    ImGui::Checkbox("custom blood color", &g_customBloodEnable);
    if (g_customBloodEnable) {
        ImGui::SameLine();
        ImGui::ColorRow("##custom blood color picker", g_customBloodColor, kWdPickerAlpha);
    }

    // CSS precipitation is the same rain/snow sprite (c_effects.cpp), tinted where it is looked up
    ImGui::Checkbox("custom precipitation color", &g_weatherTintEnable);
    if (g_weatherTintEnable) {
        ImGui::SameLine();
        ImGui::ColorRow("##custom precipitation color picker", g_weatherTintColor, kWdPickerAlpha);
    }

    WdModulationRow("world modulation", &g_worldTintEnable, g_worldTintColor, &g_wdWorldModBright);
    WdModulationRow("prop modulation", &g_propTintEnable, g_propTintColor, &g_wdPropModBright);
    WdModulationRow("skybox modulation", &g_wdSkyMod, g_wdSkyModColor, &g_wdSkyModBright);

    // botox commits a custom path only when the box is left (IsItemDeactivatedAfterEdit), so a
    // half-typed name never reaches the material walk
    constexpr auto presetLabel = [](void*, int i, const char** out) { *out = wfx3::k_texPresets[i].label; return true; };
    auto customBox = [](const char* id, char* edit, size_t editSize, char* committed, size_t committedSize) {
        ImGui::InputTextWithHint(id, "materials/ path, no .vtf", edit, editSize);
        if (ImGui::IsItemDeactivatedAfterEdit())
            strncpy_s(committed, committedSize, edit, _TRUNCATE);
        else if (!ImGui::IsItemActive() && strcmp(edit, committed) != 0)
            strncpy_s(edit, editSize, committed, _TRUNCATE);
    };

    ImGui::Checkbox("world texture", &g_wdWorldTex);
    if (g_wdWorldTex) {
        ImGui::OptionPopup("world texture settings", [&]() {
            int all = g_wdWorldTexPreset[0];
            for (int c = 1; c < wfx3::k_texCategoryCount; ++c)
                all = g_wdWorldTexPreset[c] == all ? all : -1;
            if (ImGui::Combo("all##world texture", &all, presetLabel, nullptr, wfx3::k_texPresetCount))
                for (int c = 0; c < wfx3::k_texCategoryCount; ++c) g_wdWorldTexPreset[c] = all;

            ImGui::Checkbox("remove decals", &g_wdWorldTexDecals);

            static char s_edit[wfx3::k_texCategoryCount][128] = {};
            for (int c = 0; c < wfx3::k_texCategoryCount; ++c) {
                ImGui::PushID(c);
                ImGui::Combo(wfx3::k_texCategories[c], &g_wdWorldTexPreset[c], presetLabel, nullptr, wfx3::k_texPresetCount);
                if (g_wdWorldTexPreset[c] == wfx3::k_texPresetCustom)
                    customBox("##custom", s_edit[c], sizeof(s_edit[c]), g_wdWorldTexCustom[c], sizeof(g_wdWorldTexCustom[c]));
                ImGui::PopID();
            }
        }, ImVec2(260.f, -1.f));
    }

    ImGui::Checkbox("prop texture", &g_wdPropTex);
    if (g_wdPropTex) {
        ImGui::OptionPopup("prop texture settings", [&]() {
            ImGui::Combo("texture##prop texture", &g_wdPropTexPreset, presetLabel, nullptr, wfx3::k_texPresetCount);
            static char s_edit[128] = {};
            if (g_wdPropTexPreset == wfx3::k_texPresetCustom)
                customBox("##prop custom", s_edit, sizeof(s_edit), g_wdPropTexCustom, sizeof(g_wdPropTexCustom));
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("bullet tracers", &g_wdTracers);
    if (g_wdTracers) {
        ImGui::OptionPopup("bullet tracers settings", [&]() {
            ImGui::Checkbox("local##bullet tracers", &g_wdTracersLocal);
            ImGui::SameLine();
            ImGui::ColorRow("##bullet tracers local color", g_wdTracersLocalColor, kWdPickerAlpha);

            ImGui::Checkbox("enemies##bullet tracers", &g_wdTracersEnemy);
            ImGui::SameLine();
            ImGui::ColorRow("##bullet tracers enemy color", g_wdTracersEnemyColor, kWdPickerAlpha);

            ImGui::Checkbox("teammates##bullet tracers", &g_wdTracersTeam);
            ImGui::SameLine();
            ImGui::ColorRow("##bullet tracers team color", g_wdTracersTeamColor, kWdPickerAlpha);

            ImGui::Combo("sprite##bullet tracers", &g_wdTracersSprite, "purple laser\0phys beam\0laser beam\0white\0");
            ImGui::SliderFloat("life##bullet tracers", &g_wdTracersLife, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("width##bullet tracers", &g_wdTracersWidth, 0.5f, 10.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("movement trail", &g_wdTrail);
    if (g_wdTrail) {
        ImGui::OptionPopup("movement trail settings", [&]() {
            ImGui::ColorRow("color##movement trail", g_wdTrailColor, kWdPickerAlpha);
            ImGui::ColorRow("crouch color##movement trail", g_wdTrailCrouchColor, kWdPickerAlpha);
            ImGui::Checkbox("rainbow##movement trail", &g_wdTrailRainbow);
            ImGui::Checkbox("air only##movement trail", &g_wdTrailAirOnly);
            ImGui::Combo("sprite##movement trail", &g_wdTrailSprite, "purple laser\0phys beam\0laser beam\0white\0");
            ImGui::SliderFloat("life##movement trail", &g_wdTrailLife, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("width##movement trail", &g_wdTrailWidth, 0.5f, 20.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("bullet impacts", &g_wdImpacts);
    if (g_wdImpacts) {
        ImGui::OptionPopup("bullet impacts settings", [&]() {
            ImGui::ColorRow("client color##bullet impacts", g_wdImpactsClientColor, kWdPickerAlpha);
            ImGui::SliderFloat("size##bullet impacts", &g_wdImpactsSize, 0.5f, 10.f, "%.1f u", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("duration##bullet impacts", &g_wdImpactsDuration, 0.1f, 30.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("world hit marker", &g_wdHitMarker);
    if (g_wdHitMarker) {
        ImGui::OptionPopup("world hit marker settings", [&]() {
            ImGui::ColorRow("color##world hit marker", g_wdHitMarkerColor, kWdPickerAlpha);
            ImGui::SliderFloat("size##world hit marker", &g_wdHitMarkerSize, 2.f, 20.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("duration##world hit marker", &g_wdHitMarkerDuration, 0.1f, 5.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("minecraft sky + clouds", &g_mcWorldEnable);
}
