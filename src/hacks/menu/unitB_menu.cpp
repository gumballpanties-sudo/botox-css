#include "unitB_menu.h"
#include "menu.h"        // MenuBindKey
#include "../hud/indicators.h"
#include "../movement/detections.h"
#include "../movement/ps_lines.h"
#include "../movement/movement.h"   // PS_ClearTrajectory
#include "../movement/recorder.h"
#include "menu_chrome.h"   // menu_columns_* / menu_group_*
#include <cstdio>

// botox menu_tab_movement.cpp case 1, "indicators" group body, 1:1. Stamina indicator KEPT:
// CSS has a real stamina stat (unlike TF2, which had none) -- see ref-css-ladder-physics.
// color_picker_alpha_flags = ColorRow's defaults here
void UnitB_Menu_movement_indicators()
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    ImGui::Checkbox("velocity indicator", &g_viEnable);
    if (g_viEnable)
        ImGui::OptionPopup("velocity indicator settings", [&]() {
            ImGui::Combo("style##velocity indicator style", &g_viStyle, kIndStyleNames);

            if (g_viStyle == IND_STYLE_SMOOTH)
                ImGui::Combo("comes from##velocity indicator", &g_viSmoothOrigin, kSmoothOriginNames);

            if (g_viStyle == IND_STYLE_KAMIDERE)
                ImGui::ColorRow("kami shadow color##velocity indicator", g_indKamiShadowColor);

            if (g_viCustomColor)
            {
                ImGui::ColorRow("velocity colors##velocity indicator color 1", g_viColor1);
                ImGui::SameLine();
                ImGui::ColorRow("##velocity indicator color 2", g_viColor2, 0, 1);
            }
            else
            {
                ImGui::ColorRow("velocity colors##velocity indicator color 3", g_viColor3);
                ImGui::SameLine();
                ImGui::ColorRow("##velocity indicator color 4", g_viColor4, 0, 1);
                ImGui::SameLine();
                ImGui::ColorRow("##velocity indicator color 5", g_viColor5, 0, 2);
            }

            ImGui::Checkbox("show pre speed##velocity indicator", &g_viPreSpeed);
            if (g_viPreSpeed)
            {
                ImGui::Combo("pre speed position##velocity indicator", &g_viPreLayout, kPreLayoutNames);
                ImGui::SliderFloat("pre speed scale##velocity indicator", &g_viPreScale, 0.25f, 2.f, "%.2fx");
                if (g_viPreLayout == PRE_LAYOUT_RIGHT)
                    ImGui::Combo("pre speed align##velocity indicator", &g_viPreAlign, kPreAlignNames);
            }

            ImGui::Checkbox("fade alpha##velocity indicator", &g_viFadeAlpha);

            ImGui::Checkbox("custom color##velocity indicator", &g_viCustomColor);

            ImGui::Checkbox("shadow##velocity indicator", &g_viShadow);
            if (g_viShadow)
            {
                ImGui::SliderFloat("shadow blur##velocity indicator", &g_viShadowBlur, 0.f, 20.f, "%.1f");
                if (g_viStyle != IND_STYLE_KAMIDERE)
                    ImGui::ColorRow("shadow color##velocity indicator", g_viShadowColor);
                ImGui::SliderInt("shadow x##velocity indicator", &g_viShadowX, -10, 10, "%d px");
                ImGui::SliderInt("shadow y##velocity indicator", &g_viShadowY, -10, 10, "%d px");
            }

            ImGui::Checkbox("outline##velocity indicator", &g_viOutline);
            if (g_viOutline)
            {
                ImGui::ColorRow("outline color##velocity indicator", g_viOutlineColor);
                ImGui::SliderInt("outline thickness##velocity indicator", &g_viOutlinePx, 1, 5, "%d px");
            }

            ImGui::SliderFloat("scale##velocity indicator", &g_viScale, kIndScaleMin, 3.f, "%.2fx");

            ImGui::SliderInt("position##velocity indicator", &g_viPadding, 30, (int)screen.y);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("stamina indicator", &g_siEnable);
    if (g_siEnable)
        ImGui::OptionPopup("stamina indicator settings", [&]() {
            ImGui::Combo("style##stamina indicator style", &g_siStyle, kIndStyleNames);

            if (g_siStyle == IND_STYLE_SMOOTH)
                ImGui::Combo("comes from##stamina indicator", &g_siSmoothOrigin, kSmoothOriginNames);

            if (g_siStyle == IND_STYLE_KAMIDERE)
                ImGui::ColorRow("kami shadow color##stamina indicator", g_indKamiShadowColor);

            ImGui::ColorRow("stamina colors##stamina indicator color 1", g_siColor1);
            ImGui::SameLine();
            ImGui::ColorRow("##stamina indicator color 2", g_siColor2, 0, 1);

            ImGui::Checkbox("show pre speed##stamina indicator", &g_siPreSpeed);
            if (g_siPreSpeed)
            {
                ImGui::Combo("pre speed position##stamina indicator", &g_siPreLayout, kPreLayoutNames);
                ImGui::SliderFloat("pre speed scale##stamina indicator", &g_siPreScale, 0.25f, 2.f, "%.2fx");
                if (g_siPreLayout == PRE_LAYOUT_RIGHT)
                    ImGui::Combo("pre speed align##stamina indicator", &g_siPreAlign, kPreAlignNames);
            }

            ImGui::Checkbox("fade alpha##stamina indicator", &g_siFadeAlpha);

            ImGui::Checkbox("shadow##stamina indicator", &g_siShadow);
            if (g_siShadow)
            {
                ImGui::SliderFloat("shadow blur##stamina indicator", &g_siShadowBlur, 0.f, 20.f, "%.1f");
                if (g_siStyle != IND_STYLE_KAMIDERE)
                    ImGui::ColorRow("shadow color##stamina indicator", g_siShadowColor);
                ImGui::SliderInt("shadow x##stamina indicator", &g_siShadowX, -10, 10, "%d px");
                ImGui::SliderInt("shadow y##stamina indicator", &g_siShadowY, -10, 10, "%d px");
            }

            ImGui::Checkbox("outline##stamina indicator", &g_siOutline);
            if (g_siOutline)
            {
                ImGui::ColorRow("outline color##stamina indicator", g_siOutlineColor);
                ImGui::SliderInt("outline thickness##stamina indicator", &g_siOutlinePx, 1, 5, "%d px");
            }

            ImGui::SliderFloat("scale##stamina indicator", &g_siScale, kIndScaleMin, 3.f, "%.2fx");

            ImGui::SliderInt("position##stamina indicator", &g_siPadding, 30, (int)screen.y);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("velocity graph", &g_vgEnable);
    if (g_vgEnable)
        ImGui::OptionPopup("velocity graph settings", [&]() {
            ImGui::ColorRow("line color##velocity graph", g_vgColor);
            ImGui::SliderFloat("thickness##velocity graph", &g_vgThickness, 0.5f, 6.f, "%.1f");

            ImGui::Checkbox("gain colors##velocity graph", &g_vgGainColors);
            if (g_vgGainColors)
            {
                ImGui::ColorRow("gain / loss##velocity graph gain", g_vgColorGain);
                ImGui::SameLine();
                ImGui::ColorRow("##velocity graph loss", g_vgColorLoss, 0, 1);
            }

            ImGui::Checkbox("fill##velocity graph", &g_vgFill);
            if (g_vgFill)
                ImGui::ColorRow("fill color##velocity graph", g_vgFillColor);

            ImGui::Checkbox("fade sides##velocity graph", &g_vgFadeSides);
            if (g_vgFadeSides)
                ImGui::SliderFloat("fade width##velocity graph", &g_vgFade, 0.01f, 0.5f, "%.2f");

            ImGui::Checkbox("jump speeds##velocity graph", &g_vgJumpSpeeds);
            if (g_vgJumpSpeeds)
            {
                ImGui::ColorRow("text color##velocity graph", g_vgTextColor);
                ImGui::Checkbox("text shadow##velocity graph", &g_vgTextShadow);
                ImGui::SliderFloat("text scale##velocity graph", &g_vgTextScale, kIndScaleMin, 3.f, "%.2fx");
            }

            ImGui::SliderInt("length##velocity graph", &g_vgLength, 16, 1024);
            ImGui::SliderFloat("max speed##velocity graph", &g_vgMax, 0.f, 1000.f, g_vgMax <= 0.f ? "auto" : "%.0f");
            ImGui::SliderInt("width##velocity graph", &g_vgWidth, 50, (int)screen.x);
            ImGui::SliderInt("height##velocity graph", &g_vgHeight, 10, 400);
            ImGui::SliderInt("offset x##velocity graph", &g_vgOffsetX, -(int)screen.x / 2, (int)screen.x / 2);
            ImGui::SliderInt("position##velocity graph", &g_vgPosition, 0, (int)screen.y);
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("keybind indicators", &g_kiEnable);
    if (g_kiEnable)
        ImGui::OptionPopup("keybind indicator settings", [&]() {
            ImGui::Combo("style##keybind indicator style", &g_kiStyle, kIndStyleNames);

            if (g_kiStyle == IND_STYLE_SMOOTH)
                ImGui::Combo("comes from##keybind indicators", &g_kiSmoothOrigin, kSmoothOriginNames);

            if (g_kiStyle == IND_STYLE_STATIC)
                ImGui::SliderFloat("fade in##keybind indicators", &g_kiStaticFade, 0.f, 1.f, "%.2f s");

            if (g_kiStyle == IND_STYLE_KAMIDERE)
                ImGui::ColorRow("kami shadow color##keybind indicator", g_indKamiShadowColor);

            if (g_kiStyle == IND_STYLE_KAMIBEBRA)
                ImGui::ColorRow("detect accent##keybind indicator", g_indKamibebraAccent);

            ImGui::ColorRow("keybind colors##keybind color", g_kiColor);
            ImGui::SameLine();
            ImGui::ColorRow("##keybind success color", g_kiColorSuccess, 0, 1);

            ImGui::ColorRow("in prediction color##keybind predict color", g_kiColorPredict);

            ImGui::Checkbox("horizontal layout", &g_kiHorizontal);

            ImGui::SliderInt("spacing##keybind indicators", &g_kiSpacing, -10, 40, "%d px");

            ImGui::Checkbox("shadow##keybind indicators", &g_kiShadow);
            if (g_kiShadow)
            {
                ImGui::SliderFloat("shadow blur##keybind indicators", &g_kiShadowBlur, 0.f, 20.f, "%.1f");

                if (g_kiStyle != IND_STYLE_KAMIDERE)
                    ImGui::ColorRow("shadow color##keybind indicators", g_kiShadowColor);

                ImGui::Checkbox("detect on shadow", &g_kiShadowDetect);
                ImGui::SliderInt("shadow x##keybind indicators", &g_kiShadowX, -10, 10, "%d px");
                ImGui::SliderInt("shadow y##keybind indicators", &g_kiShadowY, -10, 10, "%d px");
            }

            ImGui::Checkbox("outline##keybind indicators", &g_kiOutline);
            if (g_kiOutline)
            {
                ImGui::ColorRow("outline color##keybind indicators", g_kiOutlineColor);
                ImGui::SliderInt("outline thickness##keybind indicators", &g_kiOutlinePx, 1, 5, "%d px");
            }

            ImGui::SliderFloat("scale##keybind indicators", &g_kiScale, kIndScaleMin, 3.f, "%.2fx");

            ImGui::Checkbox("detection particles", &g_kiParticles);
            if (g_kiParticles)
            {
                ImGui::Checkbox("random colors##key particles", &g_kiParticleRandom);
                if (!g_kiParticleRandom)
                    ImGui::ColorRow("particle color##key particles", g_kiParticleColor);
                ImGui::SliderInt("amount##key particles", &g_kiParticleAmount, 1, 60);
                ImGui::SliderFloat("size##key particles", &g_kiParticleSize, 1.f, 12.f, "%.1f");
                ImGui::SliderFloat("speed##key particles", &g_kiParticleSpeed, 10.f, 600.f, "%.0f");
                ImGui::SliderFloat("gravity##key particles", &g_kiParticleGravity, -300.f, 800.f, "%.0f");
                ImGui::SliderFloat("life##key particles", &g_kiParticleLife, 0.2f, 3.f, "%.1f s");
                ImGui::SliderFloat("cooldown##key particles", &g_kiParticleCooldown, 0.f, 2.f, "%.2f s");
            }

            ImGui::SliderInt("position##sub indicators", &g_kiPosition, 30, (int)screen.y);

            ImGui::MultiCombo("displayed keybinds", &g_kiShow, kKiToggleNames, KI_COUNT);

            // per-bind label override; empty = stock label (hint). 16 = botox's buffer
            ImGui::Checkbox("custom labels", &g_kiCustomLabels);
            if (g_kiCustomLabels)
            {
                float name_width = 0.f;
                for (int i = 0; i < LBL_COUNT; i++)
                    name_width = ImMax(name_width, ImGui::CalcTextSize(kKiLabels[i][0]).x);
                name_width += 8.f;

                for (int i = 0; i < LBL_COUNT; i++)
                {
                    ImGui::PushID(i);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7.f - (i > 0 ? ImGui::GetStyle().ItemSpacing.y : 0.f));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + name_width);
                    ImGui::InputTextWithHint("##label", kKiLabels[i][1], g_kiLabels[i], sizeof(g_kiLabels[i]));
                    const ImVec2 box = ImGui::GetItemRectMin();
                    if (!ImGui::IsSearchHidden())   // menu search hid the box: no name off the last shown row
                    ImGui::GetWindowDrawList()->AddText(ImVec2(box.x - name_width, box.y + (17.f - ImGui::GetFontSize()) * 0.5f),
                                                        ImGui::GetColorU32(ImGuiCol_Text), kKiLabels[i][0]);
                    ImGui::PopID();
                }
            }
        }, ImVec2(220.f, -1.f));

    ImGui::Checkbox("keystroke indicators", &g_kpEnable);
    if (g_kpEnable)
        ImGui::OptionPopup("keystroke indicator settings", [&]() {
            ImGui::Combo("layout##key press", &g_kpLayout, "clarity (a w d / c s j)\0keyboard (c w j / a s d)\0");

            ImGui::Combo("released##key press", &g_kpReleased, "underscore\0dim letter\0hidden\0");

            ImGui::ColorRow("key colors##key press color", g_kpColor);
            ImGui::SameLine();
            ImGui::ColorRow("##key press released color", g_kpColorReleased, 0, 1);

            ImGui::Checkbox("duck / jump##key press", &g_kpDuckJump);
            ImGui::Checkbox("mouse direction##key press", &g_kpMouse);

            ImGui::Checkbox("shadow##key press", &g_kpShadow);
            if (g_kpShadow)
            {
                ImGui::SliderFloat("shadow blur##key press", &g_kpShadowBlur, 0.f, 20.f, "%.1f");
                ImGui::SliderInt("shadow x##key press", &g_kpShadowX, -10, 10, "%d px");
                ImGui::SliderInt("shadow y##key press", &g_kpShadowY, -10, 10, "%d px");
            }

            ImGui::Checkbox("outline##key press", &g_kpOutline);
            if (g_kpOutline)
            {
                ImGui::ColorRow("outline color##key press", g_kpOutlineColor);
                ImGui::SliderInt("outline thickness##key press", &g_kpOutlinePx, 1, 5, "%d px");
            }

            ImGui::SliderFloat("fade##key press", &g_kpFade, 0.f, 0.3f, "%.2f s");

            ImGui::SliderInt("key gap##key press", &g_kpGapX, 0, 60);
            ImGui::SliderInt("row gap##key press", &g_kpGapY, -10, 60);

            ImGui::SliderFloat("scale##key press", &g_kpScale, kIndScaleMin, 3.f, "%.2fx");

            ImGui::SliderInt("position##key press", &g_kpPosition, 30, (int)screen.y);
            ImGui::SliderInt("x offset##key press", &g_kpOffsetX, -(int)(screen.x / 2.f), (int)(screen.x / 2.f));
        }, ImVec2(220.f, -1.f));
}

// botox menu_tab_movement.cpp case 1, "detections" group body (1398-1438: edgebug visualize +
// pixelsurf lines live here, not in the edge bug popup -- MOVETAB 11-04 moved them to match)
void UnitB_Menu_movement_detections()
{
    // botox menu_tab_movement "detections": label list order must match DetType
    ImGui::Checkbox("detections", &g_detections);
    if (g_detections)
        ImGui::OptionPopup("detection settings", [&]() {
            static const char* const kTypes[DET_MAX] = { "edgebug", "texturebug", "airstuck", "pixelsurf", "wallclimb" };
            ImGui::MultiCombo("detections", &g_detectionTypes, kTypes, DET_MAX);
            ImGui::Checkbox("stack edgebugs##detstack", &g_detectionStackEb);
            ImGui::InputText("chat label##detlabel", g_detectionLabel, sizeof(g_detectionLabel));
        }, ImVec2(220.f, -1));

    // botox :1398-1422
    ImGui::Checkbox("edgebug visualize", &g_edgeBugViz);
    if (g_edgeBugViz)
        ImGui::OptionPopup("edgebug visualize settings", [&]() {
            ImGui::ColorRow("outline color##ebvizcolor", g_edgeBugVizColor);
            ImGui::SliderFloat("visualize thickness##ebvizthick", &g_edgeBugVizThickness, 0.5f, 10.f, "%.1f");
            ImGui::Combo("pad shape##ebvizshape", &g_edgeBugVizShape, "square\0circle\0");
            ImGui::Combo("pad fill##ebvizfill", &g_edgeBugVizFill, "outline\0filled\0filled + outline\0");
            if (g_edgeBugVizFill != 0)
            {
                ImGui::ColorRow("pad color##ebvizpadcolor", g_edgeBugVizPadColor);
                ImGui::SliderFloat("fill opacity##ebvizfilla", &g_edgeBugVizFillAlpha, 0.05f, 1.f, "%.2f");
            }
            ImGui::SliderFloat("pad size##ebvizsize", &g_edgeBugVizSize, 4.f, 64.f, "%.0f");
            ImGui::Checkbox("project onto geometry", &g_edgeBugVizProjected);
        }, ImVec2(220.f, -1));

    // botox :1424-1438, 1:1 (PSPORT 11-04: added line style combo + thickness slider,
    // g_psLinesStyle/g_psLinesThickness -- ps_lines.h only had color + full before).
    ImGui::Checkbox("pixelsurf lines", &g_psLinesEnable);
    if (g_psLinesEnable)
        ImGui::OptionPopup("pixelsurf lines settings", [&]() {
            ImGui::ColorRow("line color##pslinecolor", g_psLinesColor);
            ImGui::Combo("line style##pslinestyle", &g_psLinesStyle, "full lines\0dotted lines\0dots\0");
            ImGui::SliderFloat("line thickness##pslinethick", &g_psLinesThickness, 0.5f, 10.f, "%.1f");
            // 11-09: "full trajectory" removed (lines are finder-walked, see ps_lines.h); lines stay
            // per map, so this is the reset
            if (ImGui::Button("clear lines##psl"))
                PS_ClearTrajectory();
        }, ImVec2(220.f, -1));
}

// botox menu_tab_movement case 3, 1:1. Emits its own columns + groups (Page_MovementRecorder
// only calls this).
void UnitB_Menu_movement_recorder()
{
    static bool s_firstList = true;
    if (s_firstList) { Recorder_Refresh(); s_firstList = false; }

    menu_columns_begin();

    if (menu_group_begin("recorder"))
    {
        ImGui::Checkbox("movement recorder", &g_recEnable);
        ImGui::Checkbox("show route lines", &g_recShowLine);
        ImGui::Checkbox("route lines near start", &g_recPreviewLine);
        ImGui::Checkbox("show recorder timer", &g_recRender);
        if (g_recRender)
            ImGui::OptionPopup("recorder timer settings", [&]() {
                ImGui::Combo("position##recorder timer", &g_recPosition, "top left\0bottom left\0bottom right\0");
            }, ImVec2(200.f, -1));
        ImGui::Checkbox("clipper", &g_recClipper);
        if (g_recClipper)
        {
            ImGui::Checkbox("show clipper box", &g_recClipperBox);
            ImGui::SliderFloat("clipper duration##recorder", &g_recClipSeconds, 1.f, 120.f, "%.0f s");
        }
        ImGui::Checkbox("original playback viewangles", &g_recLockVa);
        ImGui::Combo("playback yaw", &g_recYaw, "original\0left\0right\0backwards\0spinning\0");
        if (g_recYaw == 4)
            ImGui::SliderFloat("spin speed##recorder", &g_recSpinSpeed, -1800.f, 1800.f, "%.0f deg/s");
        ImGui::SliderFloat("smooth start##recorder", &g_recSmoothStart, 0.f, 3.f, g_recSmoothStart > 0.01f ? "%.2f s" : "off");
        ImGui::SliderFloat("smooth end##recorder", &g_recSmoothEnd, 0.f, 3.f, g_recSmoothEnd > 0.01f ? "%.2f s" : "off");
        ImGui::Checkbox("lock while aiming to position", &g_recLockGoingToStart);
        ImGui::Checkbox("stop playback on movement", &g_recStopOnMove);
        ImGui::Checkbox("force same weapons", &g_recForceWeapon);

        // press actions: a style (always on / toggle) would fire them on every menu close
        int unused = 1;
        ImGui::Label("start recording");
        ImGui::Keybind("recorder start rec key", &g_recKeyStartRec, &unused, false);
        ImGui::Label("stop recording");
        ImGui::Keybind("recorder stop rec key", &g_recKeyStopRec, &unused, false);
        ImGui::Label("save route");
        ImGui::Keybind("recorder save key", &g_recKeySave, &unused, false);
        ImGui::Label("start playback");
        ImGui::Keybind("recorder start play key", &g_recKeyStartPlay, &unused, false);
        ImGui::Label("stop playback");
        ImGui::Keybind("recorder stop play key", &g_recKeyStopPlay, &unused, false);
        ImGui::Label("clear route");
        ImGui::Keybind("recorder clear key", &g_recKeyClear, &unused, false);
        if (g_recClipper)
        {
            ImGui::Label("clip route");
            ImGui::Keybind("recorder clip key", &g_recKeyClip, &unused, false);
        }
    }
    menu_group_end();

    if (menu_group_begin("actions"))
    {
        static constexpr struct { const char* label; unsigned action; } kActions[] = {
            { "start recording", REC_START_REC }, { "stop recording", REC_STOP_REC }, { "save route", REC_SAVE },
            { "start playback", REC_START_PLAY }, { "stop playback", REC_STOP_PLAY }, { "clear route", REC_CLEAR },
            { "clip route", REC_CLIP },
        };
        ImGui::BeginDisabled(!g_recEnable);
        for (const auto& a : kActions)
        {
            if (a.action == REC_CLIP && !g_recClipper) continue;
            char id[64];
            snprintf(id, sizeof(id), "%s##recorder action", a.label);
            if (ImGui::Button(id, ImVec2(-1.f, 15.f))) Recorder_Request(a.action);
        }
        ImGui::EndDisabled();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("routes"))
        Recorder_MenuRoutes();
    menu_group_end();

    menu_columns_end();
}
