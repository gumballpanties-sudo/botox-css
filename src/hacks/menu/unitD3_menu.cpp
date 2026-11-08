#include "unitD3_menu.h"
#include "menu.h"   // ImGui::OptionPopup / ColorRow
#include <algorithm>

// Called inside menu.cpp's "screen" group (Page_VisualsScreen, col1), after colour correction:
// botox menu_tab_visuals.cpp 1239-1625, sharpen .. show depth, 1:1 (labels, ids, ranges, widths).
void UnitD3_Menu_visuals_screen()
{
    // botox menu_internal.h color_picker_alpha_flags / color_picker_no_alpha_flags
    constexpr ImGuiColorEditFlags k_alpha = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
        ImGuiColorEditFlags_NoBorder;
    constexpr ImGuiColorEditFlags k_noAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
        ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoAlpha;
    constexpr ImGuiSliderFlags k_clamp = ImGuiSliderFlags_AlwaysClamp;

    ImGui::Checkbox("sharpen", &g_sharpenEnable);
    if (g_sharpenEnable)
        ImGui::OptionPopup("sharpen settings", [&]() {
            ImGui::SliderFloat("strength##sharpen", &g_sfxSharpenStrength, 0.f, 5.f, "%.2f", k_clamp);
            ImGui::SliderFloat("threshold##sharpen", &g_sfxSharpenThreshold, 0.f, 0.2f, "%.3f", k_clamp);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("film grain", &g_filmGrainEnable);
    if (g_filmGrainEnable)
        ImGui::OptionPopup("film grain settings", [&]() {
            ImGui::SliderFloat("intensity##film grain", &g_sfxGrainIntensity, 0.f, 0.3f, "%.3f", k_clamp);
            ImGui::SliderFloat("size##film grain", &g_sfxGrainSize, 1.f, 4.f, "%.2f", k_clamp);
            ImGui::SliderFloat("response position##film grain", &g_sfxGrainRespPos, 0.f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("response range##film grain", &g_sfxGrainRespRange, 0.05f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("minimum response##film grain", &g_sfxGrainRespMin, 0.f, 1.f, "%.2f", k_clamp);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("vignette", &g_vignetteEnable);
    if (g_vignetteEnable)
        ImGui::OptionPopup("vignette settings", [&]() {
            ImGui::ColorRow("color##vignette", g_sfxVignetteColor, k_alpha);
            ImGui::SliderFloat("radius##vignette", &g_sfxVignetteRadius, 0.f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("feather##vignette", &g_sfxVignetteFeather, 0.f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("aspect ratio##vignette", &g_sfxVignetteAspect, 0.25f, 4.f, "%.2f", k_clamp);
            ImGui::SliderFloat("blur##vignette", &g_sfxVignetteBlur, 0.f, 32.f, "%.1f", k_clamp);
            ImGui::Checkbox("invert##vignette", &g_vignetteInvert);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("invert", &g_invertFilterEnable);
    if (g_invertFilterEnable)
        ImGui::OptionPopup("invert settings", [&]() {
            ImGui::Combo("mode##invert", &g_sfxInvertMode, "color\0" "brightness\0");
            ImGui::SliderFloat("strength##invert", &g_sfxInvertStrength, 0.f, 1.f, "%.2f", k_clamp);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("posterize", &g_posterizeEnable);
    if (g_posterizeEnable)
        ImGui::OptionPopup("posterize settings", [&]() {
            ImGui::SliderInt("levels##posterize", &g_sfxPosterizeLevels, 2, 64, "%d", k_clamp);
            ImGui::SliderFloat("dither##posterize", &g_sfxPosterizeDither, 0.f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("strength##posterize", &g_sfxPosterizeStrength, 0.f, 1.f, "%.2f", k_clamp);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("deband", &g_debandEnable);
    if (g_debandEnable)
        ImGui::OptionPopup("deband settings", [&]() {
            ImGui::SliderInt("quality##deband", &g_sfxDebandIterations, 1, 4, "%d", k_clamp);
            ImGui::SliderFloat("strength##deband", &g_sfxDebandStrength, 1.f, 50.f, "%.0f", k_clamp);
            ImGui::SliderFloat("radius##deband", &g_sfxDebandRadius, 4.f, 48.f, "%.0f px", k_clamp);
            ImGui::SliderFloat("dither##deband", &g_sfxDebandGrain, 0.f, 64.f, "%.0f", k_clamp);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("channel shift", &g_channelShiftEnable);
    if (g_channelShiftEnable)
        ImGui::OptionPopup("channel shift settings", [&]() {
            ImGui::SliderFloat("red x##channel shift", &g_sfxShiftRedX, -32.f, 32.f, "%.1f px");
            ImGui::SliderFloat("red y##channel shift", &g_sfxShiftRedY, -32.f, 32.f, "%.1f px");
            ImGui::SliderFloat("green x##channel shift", &g_sfxShiftGreenX, -32.f, 32.f, "%.1f px");
            ImGui::SliderFloat("green y##channel shift", &g_sfxShiftGreenY, -32.f, 32.f, "%.1f px");
            ImGui::SliderFloat("blue x##channel shift", &g_sfxShiftBlueX, -32.f, 32.f, "%.1f px");
            ImGui::SliderFloat("blue y##channel shift", &g_sfxShiftBlueY, -32.f, 32.f, "%.1f px");

            ImGui::Checkbox("depth based##channel shift", &g_channelShiftDepth);
            if (g_channelShiftDepth) {
                ImGui::Combo("depth side##channel shift", &g_channelShiftDepthMode, "background\0" "foreground\0" "both sides\0");
                ImGui::SliderFloat("distance##channel shift", &g_channelShiftDepthDist, 0.f, 4000.f, "%.0f u", k_clamp);
                ImGui::SliderFloat("range##channel shift", &g_channelShiftDepthRange, 0.f, 2000.f, "%.0f u", k_clamp);
                ImGui::SliderFloat("ramp##channel shift", &g_channelShiftDepthRamp, 1.f, 4000.f, "%.0f u", k_clamp);
            }
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("depth distort", &g_warpEnable);
    if (g_warpEnable)
        ImGui::OptionPopup("depth distort settings", [&]() {
            int& effect = g_warpEffect;
            ImGui::Combo("effect##depth distort", &effect,
                         "bulge / pinch\0" "swirl\0" "spliced radials\0" "zigzag\0" "ripple\0" "wave\0" "drunk\0"
                         "tiny planet\0" "slit scan\0" "radial slit scan\0");
            const bool radial = effect <= 4;

            int& animate = g_warpAnimate;
            static const char* k_animate[] = { "no\0" "yes\0", "no\0" "amplitude\0" "phase\0", "no\0" "amplitude\0" "phase\0" "angle\0" };
            const int anim_kind = (effect == 3 || effect == 4) ? 1 : (effect == 5 ? 2 : 0);
            if (effect != 6 && effect != 7) {
                if (animate < 0 || animate > anim_kind + 1)
                    animate = 0;
                ImGui::Combo("animate##depth distort", &animate, k_animate[anim_kind]);
                if (animate)
                    ImGui::SliderFloat("animation speed##depth distort", &g_warpAnimSpeed, 0.05f, 10.f, "%.2f", k_clamp);
            }

            ImGui::Label("properties");

            switch (effect) {
            case 0:
                ImGui::SliderFloat("magnitude##depth distort", &g_warpMagnitude, -1.f, 1.f, "%.2f", k_clamp);
                break;
            case 1:
            case 2:
                ImGui::SliderFloat("angle##depth distort", &g_warpAngle, -1800.f, 1800.f, "%.0f deg", k_clamp);
                ImGui::SliderFloat("inner radius##depth distort", &g_warpInnerRadius, 0.f, 1.f, "%.3f", k_clamp);
                if (effect == 1)
                    ImGui::Checkbox("inverse angle##depth distort", &g_warpInverse);
                else
                    ImGui::SliderInt("splices##depth distort", &g_warpSplices, 1, 50, "%d", k_clamp);
                break;
            case 3:
            case 4:
                ImGui::SliderFloat("angle##depth distort", &g_warpAngle, -999.f, 999.f, "%.0f", k_clamp);
                ImGui::SliderFloat("period##depth distort", &g_warpPeriod, 0.1f, 10.f, "%.2f", k_clamp);
                ImGui::SliderFloat("amplitude##depth distort", &g_warpAmplitude, -10.f, 10.f, "%.2f", k_clamp);
                ImGui::SliderFloat("phase##depth distort", &g_warpPhase, -5.f, 5.f, "%.2f", k_clamp);
                break;
            case 5:
                ImGui::Combo("wave type##depth distort", &g_warpWaveType, "x/x\0" "x/y\0");
                ImGui::SliderFloat("angle##depth distort", &g_warpWaveAngle, -360.f, 360.f, "%.0f deg", k_clamp);
                ImGui::SliderFloat("period##depth distort", &g_warpWavePeriod, 0.1f, 10.f, "%.2f", k_clamp);
                ImGui::SliderFloat("amplitude##depth distort", &g_warpWaveAmplitude, -1.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("phase##depth distort", &g_warpPhase, -5.f, 5.f, "%.2f", k_clamp);
                break;
            case 6:
                ImGui::SliderFloat("strength x##depth distort", &g_warpDrunkStrength[0], 0.f, 100.f, "%.1f", k_clamp);
                ImGui::SliderFloat("strength y##depth distort", &g_warpDrunkStrength[1], 0.f, 100.f, "%.1f", k_clamp);
                ImGui::SliderFloat("speed##depth distort", &g_warpDrunkSpeed[0], 0.f, 10.f, "%.1f", k_clamp);
                ImGui::SliderFloat("bending angle##depth distort", &g_warpDrunkAngle, -1800.f, 1800.f, "%.0f deg", k_clamp);
                ImGui::SliderFloat("bending speed##depth distort", &g_warpDrunkAngleSpeed, 0.f, 10.f, "%.1f", k_clamp);
                break;
            case 7:
                ImGui::SliderFloat("scale##depth distort", &g_warpPlanetScale, 0.f, 10.f, "%.2f", k_clamp);
                ImGui::SliderFloat("rotation x##depth distort", &g_warpPlanetRot[0], 0.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("rotation y##depth distort", &g_warpPlanetRot[1], 0.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("rotation z##depth distort", &g_warpPlanetRot[2], 0.f, 360.f, "%.1f deg", k_clamp);
                ImGui::SliderFloat("offset x##depth distort", &g_warpPlanetOffset[0], -0.5f, 0.5f, "%.3f", k_clamp);
                ImGui::SliderFloat("offset y##depth distort", &g_warpPlanetOffset[1], -0.5f, 0.5f, "%.3f", k_clamp);
                ImGui::SliderFloat("seam blending##depth distort", &g_warpPlanetSeam, 0.5f, 1.f, "%.3f", k_clamp);
                break;
            case 8:
                ImGui::Combo("direction##depth distort", &g_warpSlitDir, "left\0" "right\0" "up\0" "down\0");
                if (!animate)
                    ImGui::SliderFloat("position##depth distort", &g_warpSlitPos, 0.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("scan speed##depth distort", &g_warpSlitSpeed, 0.f, 3.f, "%.2f", k_clamp);
                break;
            case 9:
                if (!animate)
                    ImGui::SliderFloat("position##depth distort", &g_warpSlitPos, 0.f, 1.f, "%.3f", k_clamp);
                ImGui::ColorRow("border color##depth distort", g_warpBorderColor);
                break;
            default:
                break;
            }

            if (radial || effect == 9) {
                ImGui::SliderFloat("center x##depth distort", &g_warpCenter[0], 0.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("center y##depth distort", &g_warpCenter[1], 0.f, 1.f, "%.3f", k_clamp);
            }

            if (radial) {
                ImGui::SliderFloat("radius##depth distort", &g_warpRadius, 0.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("tension##depth distort", &g_warpTension, 0.f, 10.f, "%.3f", k_clamp);
                ImGui::SliderFloat("aspect ratio##depth distort", &g_warpAspect, -100.f, 100.f, "%.1f", k_clamp);
                ImGui::SliderFloat("aspect angle##depth distort", &g_warpAspectAngle, -180.f, 180.f, "%.1f deg", k_clamp);
                ImGui::Checkbox("use offset coordinates##depth distort", &g_warpUseOffset);
                if (g_warpUseOffset) {
                    ImGui::SliderFloat("offset x##depth distort", &g_warpOffset[0], 0.f, 1.f, "%.3f", k_clamp);
                    ImGui::SliderFloat("offset y##depth distort", &g_warpOffset[1], 0.f, 1.f, "%.3f", k_clamp);
                }
            }

            ImGui::Label("blending");
            ImGui::Combo("blending mode##depth distort", &g_warpBlend,
                         "normal\0" "darken\0" "  multiply\0" "  color burn\0" "  linear burn\0" "lighten\0" "  screen\0"
                         "  color dodge\0" "  linear dodge\0" "  addition\0" "  glow\0" "overlay\0" "  soft light\0"
                         "  hard light\0" "  vivid light\0" "  linear light\0" "  pin light\0" "  hard mix\0" "difference\0"
                         "  exclusion\0" "subtract\0" "  divide\0" "  divide (alternative)\0" "  divide (photoshop)\0"
                         "  reflect\0" "  grain extract\0" "  grain merge\0" "hue\0" "  saturation\0" "  color\0"
                         "  luminosity\0");
            ImGui::SliderFloat("opacity##depth distort", &g_warpOpacity, 0.f, 1.f, "%.2f", k_clamp);

            ImGui::Label("depth");
            ImGui::Combo("depth side##depth distort", &g_warpDepthMode, "background\0" "foreground\0" "both sides\0");
            ImGui::SliderFloat("distance##depth distort", &g_warpDepthDist, 0.f, 4000.f, "%.0f u", k_clamp);
            ImGui::SliderFloat("range##depth distort", &g_warpDepthRange, 0.f, 2000.f, "%.0f u", k_clamp);
            ImGui::SliderFloat("ramp##depth distort", &g_warpDepthRamp, 1.f, 4000.f, "%.0f u", k_clamp);
            if (effect <= 7) {
                ImGui::Checkbox("fade transition##depth distort", &g_warpFade);
                ImGui::Checkbox("protect edges##depth distort", &g_warpDepthProtect);
            }
        }, ImVec2(230.f, -1.f));

    ImGui::Checkbox("reflections", &g_reflEnable);
    if (g_reflEnable)
        ImGui::OptionPopup("reflections settings", [&]() {
            ImGui::SliderFloat("intensity##reflections", &g_reflIntensity, 0.f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("distance##reflections", &g_reflDistance, 0.05f, 1.f, "%.2f", k_clamp);
            ImGui::SliderFloat("blur##reflections", &g_sfxReflBlur, 0.f, 5.f, "%.1f", k_clamp);
            ImGui::SliderFloat("angle falloff##reflections", &g_sfxReflFalloff, 0.f, 1.f, "%.2f", k_clamp);

            ImGui::Label("performance");

            ImGui::SliderFloat("render size##reflections", &g_sfxReflScale, 0.25f, 1.f, "%.2f", k_clamp);
            ImGui::Combo("ray quality##reflections", &g_sfxReflQuality, "low\0" "medium\0" "high\0");
            ImGui::Checkbox("smooth normals##reflections", &g_sfxReflSmoothNormals);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("emphasize", &g_emphasizeEnable);
    if (g_emphasizeEnable)
        ImGui::OptionPopup("emphasize settings", [&]() {
            ImGui::SliderFloat("focus depth##emphasize", &g_emphasizeFocusDepth, 0.f, 1.f, "%.3f", k_clamp);
            ImGui::SliderFloat("focus range##emphasize", &g_emphasizeFocusRange, 0.f, 1.f, "%.3f", k_clamp);
            ImGui::SliderFloat("focus edge##emphasize", &g_emphasizeFocusEdge, 0.f, 1.f, "%.3f", k_clamp);

            ImGui::Checkbox("spherical##emphasize", &g_emphasizeSpherical);
            if (g_emphasizeSpherical) {
                ImGui::SliderInt("sphere fov##emphasize", &g_emphasizeSphereFov, 1, 180, "%d deg", k_clamp);
                ImGui::SliderFloat("focus x##emphasize", &g_emphasizeSphereX, 0.f, 1.f, "%.3f", k_clamp);
                ImGui::SliderFloat("focus y##emphasize", &g_emphasizeSphereY, 0.f, 1.f, "%.3f", k_clamp);
            }

            ImGui::ColorRow("blend color##emphasize", g_emphasizeBlendColor, k_noAlpha);
            ImGui::SliderFloat("blend factor##emphasize", &g_emphasizeBlendFactor, 0.f, 1.f, "%.3f", k_clamp);
            ImGui::SliderFloat("effect factor##emphasize", &g_emphasizeAmount, 0.f, 1.f, "%.3f", k_clamp);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("show depth", &g_showDepthEnable);
    if (g_showDepthEnable)
        ImGui::OptionPopup("show depth settings", [&]() {
            ImGui::Combo("present type##show depth", &g_showDepthType, "depth map\0" "normal map\0" "show both\0");
            ImGui::Checkbox("blend into image##show depth", &g_showDepthBlend);

            ImGui::Checkbox("live preview##show depth", &g_showDepthLive);
            if (g_showDepthLive) {
                ImGui::Checkbox("upside down##show depth", &g_showDepthUpsideDown);
                ImGui::Checkbox("reversed##show depth", &g_showDepthReversed);
                ImGui::Checkbox("logarithmic##show depth", &g_showDepthLog);
                ImGui::SliderFloat("scale x##show depth", &g_showDepthScaleX, 0.f, 2.f, "%.3f", k_clamp);
                ImGui::SliderFloat("scale y##show depth", &g_showDepthScaleY, 0.f, 2.f, "%.3f", k_clamp);

                const ImVec2 screen = ImGui::GetIO().DisplaySize;
                ImGui::SliderInt("offset x##show depth", &g_showDepthOffsetX, -(int)screen.x, (int)screen.x, "%d px");
                ImGui::SliderInt("offset y##show depth", &g_showDepthOffsetY, -(int)screen.y, (int)screen.y, "%d px");

                ImGui::SliderFloat("far plane##show depth", &g_showDepthFarPlane, 0.f, 1000.f, "%.1f", k_clamp);
                ImGui::SliderFloat("multiplier##show depth", &g_showDepthMultiplier, 0.f, 1000.f, "%.3f", k_clamp);
            }
        }, ImVec2(220.f, -1.f));
}
