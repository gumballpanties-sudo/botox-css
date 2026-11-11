#include "menu_internal.h"
#include "../misc/clantag.h"   // clantag frame list (botox parse/join_clantag_frames)
#include "../misc/minesweeper.h"   // MsGame limits for the settings popup

// botox menu_tab_settings.cpp tab_misc, main page. Same three groups, same order, same
// popup titles/widths. Rows botox has that this DLL has no feature for are left out (list
// in the commit / report); TF2-only rows sit in the group they fit.
static void Page_MiscMain()
{
    menu_columns_begin();

    if (menu_group_begin("overlays"))
    {
        ImGui::Checkbox("watermark", &g_wmEnable);
        if (g_wmEnable)
            // unit wm: botox menu_tab_settings.cpp "watermark settings" 1:1 (rows, order, gates, ranges,
            // width 220). Vars hacks/menu/cfg/u_wm.h; drawing hud/watermark.cpp + watermark_styles.cpp.
            ImGui::OptionPopup("watermark settings", []() {
                constexpr ImGuiColorEditFlags alphaFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                    ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
                    ImGuiColorEditFlags_NoBorder;   // botox color_picker_alpha_flags
                void GifTex_PathInput(const char*, const char*, char*, size_t);   // hud/gif_tex.cpp

                int& style = g_wmStyle;
                if (style < 0 || style > 22) style = 0;
                ImGui::Combo("style##watermark style", &style,
                    "default\0kamibebra\0clarity\0clarity v2\0delusional\0interwebz\0havoc\0onetap\0airflow\0ev0lve\0legendware\0"
                    "interium\0skebob\0cumidere\0illusory\0lumi\0billware\0dna\0dna clarity\0cucumber\0howeweware\0gamesense\0boxed\0");

                ImGui::InputTextWithHint("name##watermark text", "botox", g_wmLabel, 25);

                if (style == 0 || style == 2 || style == 15) {
                    ImGui::Checkbox("user##watermark user", &g_wmUser);
                    ImGui::Checkbox("tickrate", &g_wmTickrate);
                    ImGui::Checkbox("fps", &g_wmShowFPS);
                    ImGui::Checkbox("time", &g_wmShowTime);
                }
                if (style == 20) {
                    ImGui::Checkbox("time##watermark howeweware", &g_wmShowTime);
                    ImGui::Checkbox("ping##watermark howeweware", &g_wmShowPing);
                    ImGui::Checkbox("tickrate##watermark howeweware", &g_wmTickrate);
                    ImGui::Checkbox("fps##watermark howeweware", &g_wmShowFPS);
                    ImGui::Checkbox("velocity##watermark howeweware", &g_wmVelocity);
                }
                if (style == 21) {
                    ImGui::Checkbox("user##watermark gamesense", &g_wmUser);
                    ImGui::Checkbox("fps##watermark gamesense", &g_wmShowFPS);
                    ImGui::Checkbox("ping##watermark gamesense", &g_wmShowPing);
                    ImGui::Checkbox("tickrate##watermark gamesense", &g_wmTickrate);
                    ImGui::Checkbox("velocity##watermark gamesense", &g_wmVelocity);
                    ImGui::Checkbox("time##watermark gamesense", &g_wmShowTime);
                    ImGui::Checkbox("gradient bar##watermark gamesense", &g_wmGsBar);
                    ImGui::ColorRow("game color##watermark gamesense", g_wmGsGameColor, alphaFlags);
                    ImGui::ColorRow("sense color##watermark gamesense", g_wmGsSenseColor, alphaFlags);
                    ImGui::SliderFloat("offset x##watermark gamesense", &g_wmGsX, -1000.f, 1000.f, "%.0f px");
                    ImGui::SliderFloat("offset y##watermark gamesense", &g_wmGsY, -500.f, 500.f, "%.0f px");
                }

                if (style == 22) {   // boxed watermark popup: show fps / show time / x, y offset
                    ImGui::Checkbox("show fps##watermark boxed", &g_wmShowFPS);
                    ImGui::Checkbox("show time##watermark boxed", &g_wmShowTime);
                    ImGui::SliderFloat("x offset##watermark boxed", &g_wmBoxedX, 0.f, 1.f, "%.3f");
                    ImGui::SliderFloat("y offset##watermark boxed", &g_wmBoxedY, 0.f, 1.f, "%.3f");
                }

                if (style == 12)
                    ImGui::Checkbox("typing effect##watermark skebob", &g_wmSkebobTyping);

                const bool labelStyle = style == 4 || style == 5 || style == 6 || style == 7 || style == 9 || style == 11 ||
                                        style == 13 || style == 14 || style == 16 || style == 17 || style == 18;
                int& label = g_wmLabelType;
                if (label < 0 || label > 3) label = 0;
                if (labelStyle)
                    ImGui::Combo("label##watermark label", &label, "time\0fps\0user\0none\0");

                const bool showsUser = ((style == 0 || style == 2 || style == 15 || style == 21) && g_wmUser) || style == 12 ||
                                       (style == 1 && (g_wmKamiShowFlags & 1)) || style == 3 || style == 8 || style == 10 ||
                                       style == 17 || style == 18 || style == 19 || style == 20 || (labelStyle && label == 2);
                if (showsUser)
                    ImGui::InputTextWithHint("user name##watermark clarity user", "user", g_wmUserName, sizeof(g_wmUserName));

                if (style == 9)
                    ImGui::SliderFloat("alpha override##watermark evolve", &g_wmEvolveAlpha, 0.f, 100.f, "%.0f%%");
                if (style == 6)
                    ImGui::ColorRow("blue color##watermark havoc", g_wmHavocColor, alphaFlags);
                if (style == 17) {
                    ImGui::Checkbox("detection glow##watermark dna", &g_wmDnaGlow);
                    if (g_wmDnaGlow) {
                        ImGui::SliderInt("glow ticks##watermark dna", &g_wmDnaGlowTicks, 1, 64);
                        ImGui::Checkbox("glow on spotted enemy##watermark dna", &g_wmDnaGlowSpotted);
                    }
                }
                if (style == 11)
                    ImGui::ColorRow("background color##watermark interium", g_wmInteriumBg, alphaFlags);

                if (style == 15) {
                    int& design = g_wmLumiDesign;
                    if (design < 0 || design > 1) design = 0;
                    ImGui::Combo("design##watermark lumi", &design, "original\0new\0");
                    ImGui::Checkbox("animated title##watermark lumi", &g_wmLumiAnimTitle);
                    ImGui::Checkbox("no border##watermark lumi", &g_wmLumiNoBorder);
                    ImGui::ColorRow("title color##watermark lumi", g_wmLumiTitleColor, alphaFlags);
                    ImGui::Checkbox("glow##watermark lumi", &g_wmLumiGlow);
                    if (g_wmLumiGlow) {
                        ImGui::SliderFloat("glow radius##watermark lumi", &g_wmLumiGlowRadius, 1.f, 15.f, "%.1f px");
                        ImGui::SliderFloat("glow opacity##watermark lumi", &g_wmLumiGlowOpacity, 0.f, 1.f, "%.2f");
                        ImGui::Checkbox("shadow color##watermark lumi", &g_wmLumiShadowColor);
                    }
                    ImGui::SliderFloat("offset x##watermark lumi", &g_wmLumiX, -500.f, 500.f, "%.0f px");
                    ImGui::SliderFloat("offset y##watermark lumi", &g_wmLumiY, -500.f, 500.f, "%.0f px");
                }

                ImGui::Checkbox("gif next to watermark##watermark gif", &g_wmGif);
                if (g_wmGif) {
                    int& gifType = g_wmGifType;
                    if (gifType < 0 || gifType > 2) gifType = 0;
                    ImGui::Combo("gif type##watermark gif type", &gifType, "original\0shit\0custom\0");
                    if (gifType == 2)
                        GifTex_PathInput("gif file path##watermark gif path", "C:\\mascot.gif or imgur link", g_wmGifPath, sizeof(g_wmGifPath));

                    int& gifSide = g_wmGifSide;
                    if (gifSide < 0 || gifSide > 2) gifSide = 0;
                    ImGui::Combo("gif side##watermark gif side", &gifSide, "auto\0left\0right\0");

                    ImGui::SliderFloat("gif scale##watermark gif", &g_wmGifScale, 25.f, 500.f, "%.0f%%");
                    ImGui::SliderFloat("gif rotation##watermark gif", &g_wmGifRotation, -180.f, 180.f, "%.0f deg");
                    ImGui::SliderFloat("gif gap##watermark gif", &g_wmGifGap, -50.f, 50.f, "%.0f px");
                    ImGui::SliderFloat("gif offset x##watermark gif", &g_wmGifOffsetX, -500.f, 500.f, "%.0f px");
                    ImGui::SliderFloat("gif offset y##watermark gif", &g_wmGifOffsetY, -500.f, 500.f, "%.0f px");
                    ImGui::SliderFloat("gif alpha##watermark gif", &g_wmGifAlpha, 0.f, 100.f, "%.0f%%");
                    ImGui::Checkbox("gif mirror##watermark gif", &g_wmGifFlip);
                }

                if (style != 1)
                    return;

                ImGui::CheckboxFlags("user##watermark kami", &g_wmKamiShowFlags, 1);
                ImGui::CheckboxFlags("time##watermark kami", &g_wmKamiShowFlags, 2);
                ImGui::CheckboxFlags("fps##watermark kami", &g_wmKamiShowFlags, 4);

                ImGui::Checkbox("bold font##watermark kami", &g_wmKamiBold);

                ImGui::Checkbox("custom text##watermark kami", &g_wmKamiCustomText);
                if (g_wmKamiCustomText)
                    ImGui::InputTextWithHint("##watermark kami text", "type shit here...", g_wmKamiText, sizeof(g_wmKamiText));

                ImGui::Checkbox("typing animation##watermark kami", &g_wmKamiTyping);
                if (g_wmKamiTyping) {
                    ImGui::SliderInt("typing speed##watermark kami", &g_wmKamiTypingSpeed, 1, 30, "%d chars/s");
                    ImGui::SliderInt("deleting speed##watermark kami", &g_wmKamiDeletingSpeed, 1, 30, "%d chars/s");
                    ImGui::SliderFloat("hold typed##watermark kami", &g_wmKamiTypingHold, 0.f, 10.f, "%.1f s");
                    ImGui::SliderFloat("hold deleted##watermark kami", &g_wmKamiDeletingHold, 0.f, 10.f, "%.1f s");
                }

                ImGui::Checkbox("mikudere type##watermark kami", &g_wmKamiMikudere);

                ImGui::Checkbox("custom rounding##watermark kami", &g_wmKamiRounding);
                if (g_wmKamiRounding)
                    ImGui::SliderFloat("rounding##watermark kami", &g_wmKamiRoundingValue, 0.f, 15.f, "%.1f");

                ImGui::Checkbox("outline##watermark kami", &g_wmKamiOutline);
                if (g_wmKamiOutline) {
                    ImGui::ColorRow("outline color##watermark kami", g_wmKamiOutlineColor, alphaFlags);
                    ImGui::Checkbox("rgb outline##watermark kami", &g_wmKamiRgb);
                    if (g_wmKamiRgb)
                        ImGui::SliderFloat("rgb speed##watermark kami", &g_wmKamiRgbSpeed, 0.1f, 5.f, "%.1f");
                }

                ImGui::Checkbox("text color like outline##watermark kami", &g_wmKamiTextLikeStroke);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("spectator list", &g_specEnable);
        if (g_specEnable)
            ImGui::OptionPopup("spectators settings", []() {
                // botox menu_tab_settings.cpp spectators popup, rows / gating / order 1:1. Position =
                // drag the panel while the menu is open (RMB resets), same as the donor's drag.
                void SpecList_PathInput(const char* label, const char* hint, char* buf, size_t size);   // spectator_list.cpp
                constexpr ImGuiColorEditFlags kA = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                    ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
                const int cs = g_specStyle;
                const int lumiType = cs == 25 ? g_specLumiType : -1;

                if (cs == 0 || cs == 5 || cs == 6 || cs == 8 || cs == 10 || cs == 23 || cs == 24 || cs == 25 || cs == 26 || cs == 29 || cs == 31 || cs == 32)
                    ImGui::ColorRow("spectating local color##spectator list text color one", g_specColorOne, kA);
                if (cs == 9)
                    ImGui::ColorRow("list color##spectator list text color one", g_specColorOne, kA);
                if (cs == 27) {
                    ImGui::Checkbox("rainbow##spectatorlist inba", &g_specInbaRainbow);
                    if (g_specInbaRainbow)
                        ImGui::SliderFloat("rainbow speed##spectatorlist inba", &g_specInbaRainbowSpeed, 0.f, 5.f, "%.2f");
                    else
                        ImGui::ColorRow("text color##spectator list text color one", g_specColorOne, kA);
                }
                if (cs == 0 || cs == 5 || cs == 8 || cs == 23 || cs == 24 || cs == 26 || cs == 29 || cs == 30 || cs == 31 || cs == 32 || lumiType == 2)
                    ImGui::ColorRow("spectating other color##spectator list text color two", g_specColorTwo, kA);
                if (cs != 9 && cs != 12 && cs != 13 && cs != 14 && cs != 16 && cs != 18 && cs != 19 && cs != 20 && cs != 21 && cs != 24 &&
                    cs != 26 && cs != 28 && cs != 29 && cs != 30 && cs != 31 && (cs != 25 || lumiType == 1))
                    ImGui::Combo("listed players##spectatorlist player", &g_specListType, "all spectators\0local spectators\0");

                if (g_specStyle > 32 || g_specStyle < 0) g_specStyle = 0;
                ImGui::Combo("style##spectatorlist style", &g_specStyle,
                    "default\0delusional\0interwebz\0windows xp\0clarity\0chillware\0lobotomy v2\0kamibebra\0eyes\0cumhacck\0clarity v2\0"
                    "airplane\0bhop cheat\0millionware\0aimware\0havoc\0onetap\0airflow\0ev0lve\0legendware\0interium\0chillware v2\0"
                    "skebob\0cumidere\0illusory\0lumi\0billware\0inkabanium\0dna\0cucumber\0howeweware\0gamesense\0kamidere\0");
                const int style = g_specStyle;

                if (style == 25) {
                    if (g_specLumiType < 0 || g_specLumiType > 2) g_specLumiType = 0;
                    ImGui::Combo("type##spectatorlist lumi type", &g_specLumiType, "default\0new\0interwebz\0");
                }
                if (style == 1 || style == 2 || style == 3 || style == 15 || style == 22 || style == 23 || style == 24 || style == 26 || style == 28 ||
                    style == 29 || style == 30 || style == 31 || style == 32 || (style == 25 && g_specLumiType != 1))
                    ImGui::Checkbox("show targets##spectatorlist targets", &g_specShowTarget);
                if (style == 22) {
                    ImGui::Checkbox("draw background##spectatorlist skebob", &g_specSkebobBg);
                    ImGui::Checkbox("draw title##spectatorlist skebob", &g_specSkebobTitle);
                }
                if ((style == 0 && g_specListType == 0) || style == 4 || style == 5 || style == 6 || style == 11 || style == 15 || style == 17 ||
                    style == 18 || style == 21 || style == 22 || style == 23 || style == 25 || style == 26 || style == 27 || style == 28 ||
                    style == 29 || style == 30 || style == 31 || style == 32)
                    ImGui::Checkbox("always show##spectatorlist always show", &g_specAlwaysShow);
                if (style == 31)
                    ImGui::Checkbox("gradient bar##spectatorlist gamesense", &g_specGamesenseBar);
                if (style == 32) {
                    // TF2 has SourceTV where CS:GO has GOTV (player_info_t ishltv either way)
                    ImGui::Checkbox("hide SourceTV##spectatorlist kamidere", &g_specKamiHideTv);
                    ImGui::Checkbox("observer mode##spectatorlist kamidere", &g_specBhopMode);
                    ImGui::Checkbox("custom title##spectatorlist kamidere", &g_specKbCustomTitle);
                    if (g_specKbCustomTitle)
                        ImGui::InputTextWithHint("title text##spectatorlist kamidere", "Spectators", g_specKbTitle, sizeof(g_specKbTitle));
                    ImGui::Checkbox("blur background##spectatorlist kamidere", &g_specKamiBlur);
                    ImGui::Checkbox("circle avatars##spectatorlist kamidere", &g_specKamiCircle);
                    if (!g_specKamiBlur)
                        ImGui::SliderFloat("list opacity##spectatorlist kamidere", &g_specKamiOpacity, 0.f, 100.f, "%.0f%%");
                }
                if (style == 18)
                    ImGui::SliderFloat("alpha override##spectatorlist evolve", &g_wmEvolveAlpha, 0.f, 100.f, "%.0f%%");
                if (style == 15)
                    ImGui::ColorRow("blue color##spectatorlist havoc", g_specHavocColor, kA);
                if (style == 20)
                    ImGui::ColorRow("background color##spectatorlist interium", g_specInteriumBg, kA);
                if (style == 25) {
                    ImGui::Checkbox("hide SourceTV##spectatorlist lumi", &g_specLumiHideTv);
                    if (lumiType == 0)
                        ImGui::Checkbox("glow##spectatorlist lumi", &g_specLumiGlow);
                    if (lumiType != 2)
                        ImGui::Checkbox("blur background##spectatorlist lumi", &g_specLumiBlur);
                    if (lumiType == 1) {
                        ImGui::Checkbox("mascot##spectatorlist lumi", &g_specLumiMascot);
                        if (g_specLumiMascot) {
                            SpecList_PathInput("mascot file path##spectatorlist lumi", "C:\\mascot.gif or imgur link", g_specLumiMascotPath, sizeof(g_specLumiMascotPath));
                            ImGui::SliderFloat("mascot size##spectatorlist lumi", &g_specLumiMascotSize, 20.f, 150.f, "%.0f px");
                        }
                    }
                }
                if (style == 1)
                    ImGui::Checkbox("left side##spectatorlist delusional left", &g_specDelusionalLeft);
                if (style == 5)
                    ImGui::SliderFloat("max width##spectatorlist chillware width", &g_specChillwareMaxWidth, 190.f, 600.f, "%.0f");
                if (style == 2) {
                    ImGui::SliderInt("width##spectatorlist width", &g_specListWidth, 195, 500);
                    ImGui::Checkbox("background##spectatorlist interwebz background", &g_specInterwebzBg);
                    ImGui::SliderInt("label offset##spectatorlist interwebz label x", &g_specInterwebzLabelX, -150, 150);
                }
                if (style == 7) {
                    ImGui::Checkbox("animate new entries##spectatorlist kamibebra animate", &g_specKbAnimate);
                    ImGui::Checkbox("dynamic list##spectatorlist kamibebra dynamic", &g_specKbDynamic);
                    ImGui::Checkbox("always show##spectatorlist kamibebra always show", &g_specKbAlwaysShow);
                    ImGui::Checkbox("bold font##spectatorlist kamibebra bold", &g_specKbBold);
                    ImGui::Checkbox("window shadow##spectatorlist kamibebra window shadow", &g_specKbWindowShadow);
                    ImGui::ColorRow("local color##spectatorlist kamibebra local color", g_specKbLocalColor, kA);
                    ImGui::ColorRow("other color##spectatorlist kamibebra other color", g_specKbOtherColor, kA);
                    ImGui::Checkbox("local shadow##spectatorlist kamibebra local shadow", &g_specKbLocalShadow);
                    if (g_specKbLocalShadow) {
                        ImGui::SameLine();
                        ImGui::ColorRow("##spectatorlist kamibebra local shadow color", g_specKbLocalShadowColor, kA);
                    }
                    ImGui::Checkbox("other shadow##spectatorlist kamibebra other shadow", &g_specKbOtherShadow);
                    if (g_specKbOtherShadow) {
                        ImGui::SameLine();
                        ImGui::ColorRow("##spectatorlist kamibebra other shadow color", g_specKbOtherShadowColor, kA);
                    }
                    ImGui::Checkbox("custom title##spectatorlist kamibebra custom title", &g_specKbCustomTitle);
                    if (g_specKbCustomTitle)
                        ImGui::InputTextWithHint("title text##spectatorlist kamibebra title", "Spectators", g_specKbTitle, sizeof(g_specKbTitle));
                    ImGui::Checkbox("bebra gif##spectatorlist kamibebra gif", &g_specKbGif);
                    if (g_specKbGif) {
                        ImGui::Combo("gif type##spectatorlist kamibebra gif type", &g_specKbGifType, "original\0shit\0custom\0");
                        if (g_specKbGifType == 2)
                            SpecList_PathInput("gif file path##spectatorlist kamibebra gif path", "C:\\mascot.gif or imgur link", g_specKbGifPath, sizeof(g_specKbGifPath));
                        ImGui::SliderFloat("gif size##spectatorlist kamibebra gif size", &g_specKbGifSize, 32.f, 128.f, "%.0f px");
                    }
                }
                if (style == 8) {
                    ImGui::SliderFloat("size##spectatorlist eyes size", &g_specEyesSize, 16.f, 64.f, "%.0f px");
                    ImGui::Checkbox("emoji##spectatorlist eyes emoji", &g_specEyesEmoji);
                }
                if (style == 11) {
                    ImGui::ColorRow("title color##spectatorlist airplane title", g_specAirplaneTitleColor, kA);
                    ImGui::ColorRow("gradient color##spectatorlist airplane gradient", g_specAirplaneGradientColor, kA);
                    ImGui::ColorRow("local color##spectatorlist airplane local", g_specAirplaneLocalColor, kA);
                    ImGui::ColorRow("other color##spectatorlist airplane other", g_specAirplaneOtherColor, kA);
                    ImGui::Checkbox("outline text##spectatorlist airplane outline", &g_specAirplaneOutline);
                    ImGui::Combo("title case##spectatorlist airplane title case", &g_specAirplaneTitleCase, "Spectators\0spectators\0SPECTATORS\0");
                }
                if (style == 12)
                    ImGui::Checkbox("observer mode##spectatorlist bhopcheat mode", &g_specBhopMode);
                if ((style < 13 && style != 8 && style != 9 && style != 11) || style == 22 || style == 31 || style == 32 ||
                    (style == 25 && g_specLumiType != 2))
                    ImGui::Checkbox("spectator avatars", &g_specAvatar);
                if (style == 10)
                    ImGui::Checkbox("always show##spectatorlist clarity v2", &g_specClarityAlwaysShow);
            }, ImVec2(220.f, -1.f));

        // ---- unit misc: botox menu_tab_settings.cpp 1589..1788, media player .. displayed logs ----
        ImGui::Checkbox("media player", &g_mpEnable);
        if (g_mpEnable)
            ImGui::OptionPopup("media player settings", []() {
                // botox position order (tr / tl / tc / bc / bl / br) over this dll's WatermarkPos
                static constexpr int kToWm[6] = { 2, 0, 1, 4, 3, 5 }, kFromWm[6] = { 1, 2, 0, 4, 3, 5 };
                int position = kFromWm[std::clamp(g_mpPos, 0, 5)];
                if (ImGui::Combo("position##media player position", &position, "top right\0top left\0top center\0bottom center\0bottom left\0bottom right\0"))
                    g_mpPos = kToWm[std::clamp(position, 0, 5)];

                ImGui::Checkbox("lumi layout", &g_mpLumi);
                ImGui::Checkbox("simple", &g_mpSimple);
                if (!g_mpSimple) {
                    ImGui::Checkbox("cover art", &g_mpCoverArt);
                    ImGui::Checkbox("background", &g_mpBackground);
                    ImGui::Checkbox("progress bar", &g_mpProgressBar);

                    ImGui::Checkbox("waveform", &g_mpVisualizer);
                    if (g_mpVisualizer) {
                        ImGui::SliderFloat("height##visualizer", &g_mpVisHeight, 4.f, 64.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::Checkbox("smooth##visualizer", &g_mpVisSmooth);
                        if (!g_mpVisSmooth)
                            ImGui::SliderInt("bar width##visualizer", &g_mpVisBarWidth, 1, 12, "%d px", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::Checkbox("custom color##visualizer", &g_mpVisCustomColor);
                        if (g_mpVisCustomColor) {
                            ImGui::SameLine();
                            ImGui::ColorRow("##visualizer color", g_mpVisColor);
                        }
                    }
                }

                ImGui::Checkbox("lyrics", &g_mpLyrics);
                if (g_mpLyrics) {
                    ImGui::SliderInt("rows##lyrics", &g_mpLyricsRows, 1, 9, "%d", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat("offset##lyrics", &g_mpLyricsOffset, -3000.f, 3000.f, "%.0f ms", ImGuiSliderFlags_AlwaysClamp);
                }

                ImGui::Checkbox("precise time", &g_mpTime);

                ImGui::Checkbox("in game control", &g_mpIngameControl);
                if (g_mpIngameControl) {
                    int unused = 1;   // botox Keybind( ..., false ): key only, no style popup
                    ImGui::Label("previous song");
                    ImGui::Keybind("previous##media key", &g_mpPrevKey, &unused, false);
                    ImGui::Label("pause / play");
                    ImGui::Keybind("pause##media key", &g_mpToggleKey, &unused, false);
                    ImGui::Label("next song");
                    ImGui::Keybind("next##media key", &g_mpNextKey, &unused, false);
                }
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("points", &g_ptsEnable);
        if (g_ptsEnable)
            ImGui::OptionPopup("points settings", []() {
                ImGui::Checkbox("big font##points", &g_ptsBigFont);
                ImGui::SliderFloat("appear speed##points", &g_ptsAppearSpeed, 0.5f, 20.f, "%.2fx");
                ImGui::SliderFloat("bob speed##points", &g_ptsBobSpeed, 0.1f, 10.f, "%.2fx");
                ImGui::SliderFloat("x offset##points", &g_ptsOffsetX, -300.f, 300.f, "%.0f px");
                ImGui::SliderFloat("y offset##points", &g_ptsOffsetY, -50.f, 50.f, "%.0f px");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("low fps warning", &g_fpsWarning);

        ImGui::Checkbox("hit marker", &g_hmEnable);
        if (g_hmEnable)
            ImGui::OptionPopup("hit marker settings", []() {
                ImGui::SliderFloat("size##hitmarkersize", &g_hmSize, 4.f, 30.f, "%.0f");
                ImGui::SliderFloat("speed##hitmarkerspeed", &g_hmSpeed, 0.25f, 3.f, "%.2f");
                ImGui::SliderFloat("thickness##hitmarkerthickness", &g_hmThickness, 0.5f, 4.f, "%.1f");
                ImGui::ColorRow("color##hitmarkercolor", g_hmColor);

                ImGui::Checkbox("outline##hitmarkeroutline", &g_hmOutline);
                if (g_hmOutline) {
                    ImGui::ColorRow("outline color##hitmarkeroutlinecolor", g_hmOutlineColor);
                    ImGui::SliderFloat("outline thickness##hitmarkeroutlinethickness", &g_hmOutlineThickness, 0.5f, 4.f, "%.1f");
                }
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("hit sound", &g_hitSound);
        if (g_hitSound)
            // botox menu_tab_settings.cpp hit sound popup 1:1. Stock paths = pixelsurf.inl's
            // already-verified-in-CSS-vpk picks; volume stored only (engine "play" has no
            // volume arg, see hitsound.cpp / report NEEDS FROM OTHERS).
            ImGui::OptionPopup("hit sound settings", []() {
                ImGui::Combo("sound##hitsound", &g_hitSoundType, "arena switch\0button\0money\0beep\0custom\0");
                if (g_hitSoundType == 4)
                    SoundFileCombo("sound file##hitsndpath", g_hitSoundCustom, sizeof(g_hitSoundCustom));
                ImGui::SliderFloat("volume##hitsndvol", &g_hitSoundVolume, 0.f, 2.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("healthshot effect", &g_hshotEnable);
        if (g_hshotEnable)
            ImGui::OptionPopup("healthshot effect settings", []() {
                static const char* const kTriggers[] = { "kill", "edgebug", "pixelsurf" };
                ImGui::MultiCombo("trigger on##healthshot", &g_hshotTriggers, kTriggers, 3);
                ImGui::SliderFloat("duration##healthshot", &g_hshotDuration, 0.1f, 5.f, "%.1f s");
            }, ImVec2(220.f, -1.f));

        /* where a player died; a pick the map never precached falls back to an energy splash.
           smoke / soul / bouncy balls / minecraft are our own particles, balls only on your own kills.
           melt is not ported (falls back to blood), so it shows no rows of its own */
        ImGui::Checkbox("death particles", &g_dpEnable);
        if (g_dpEnable)
            ImGui::OptionPopup("death particles settings", []() {
                ImGui::Combo("effect##deathparticles", &g_dpStyle,
                    "blood burst\0sparks\0confetti\0smoke puff\0explosion\0soul\0bouncy balls\0melt\0minecraft\0hl2 dissolve\0");

                const int type = g_dpStyle;
                // melt (7) still falls back to blood (killeffects.cpp), goo/speed rows would be
                // dead UI -- not ported, see report N/A
                if ((type < 5 && type != 3) || type == 7 || type == 9) return;

                if (type == 8)
                    ImGui::Combo("drops##deathparticles", &g_dpMcMode, "only x\0both x and xp\0only xp\0");

                if (type == 5)
                    ImGui::Combo("soul##deathparticles", &g_dpSoulStyle, "energy\0heavy electrical\0light electrical\0core\0");

                if (type != 3 && type != 7 && type != 8) {
                    // colour of the body part each particle comes from; colour below = fallback when that part is off screen / behind a wall
                    ImGui::Checkbox("model colors##deathparticles", &g_dpModelColors);
                    ImGui::Checkbox("multicolor##deathparticles", &g_dpMulticolor);
                    if (!g_dpMulticolor)
                        ImGui::ColorRow("color##deathparticles", g_dpColor);
                    else {
                        int alpha = (int)(g_dpColor[3] * 255.f + 0.5f);
                        if (ImGui::SliderInt("alpha##deathparticles", &alpha, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp))
                            g_dpColor[3] = (float)alpha / 255.f;
                    }
                }
                if (type == 5) {
                    ImGui::Checkbox("glow##deathparticles", &g_dpGlow);
                    ImGui::Checkbox("stars##deathparticles", &g_dpStars);
                }

                if (type == 6)
                    ImGui::SliderInt("balls##deathparticles", &g_dpAmount, 5, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
                else if (type != 8)
                    ImGui::SliderInt("amount##deathparticles", &g_dpAmount, 1, 40, "%d per bone", ImGuiSliderFlags_AlwaysClamp);

                ImGui::SliderFloat("size##deathparticles", &g_dpSize, 0.5f, type == 6 ? 4.f : 3.f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
                if (type != 8)
                    ImGui::SliderFloat("lifetime##deathparticles", &g_dpLifetime, 1.f, 8.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
                else if (g_dpMcMode != 0)
                    ImGui::SliderFloat("xp stays##deathparticles", &g_dpMcXpTime, 1.f, 60.f, "%.0f s", ImGuiSliderFlags_AlwaysClamp);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("console color", &g_consoleTintOn);
        if (g_consoleTintOn) {
            ImGui::SameLine();
            ImGui::ColorRow("##console color", g_consoleTint);
        }

        {
            // botox has "damage\0team damage\0purchase\0votes\0": CSS fires no item_purchase
            // event (cs_eventlog.cpp has none), purchase dropped -- see report N/A. team damage
            // added (CSS friendly fire is real, cs_player.cpp TakeDamage), bit 1 now.
            static const char* const kLogs[] = { "damage", "team damage", "votes" };
            ImGui::MultiCombo("displayed logs", &g_logTypes, kLogs, 3);
        }

        // not a botox csgo row; CSS-real and useful (filters the killfeed to your own kills) -- KEPT EXTRA
        ImGui::Checkbox("only your killfeed", &g_selfKillfeedOnly);
        // ---- unit misc end ----
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("game"))
    {
        // botox game group 1:1 (11-03 tabs round). clantag = botox menu_tab_settings.cpp:1882
        // (sender + animation: game_extras.cpp Clantag_Frame, clantag.h).
        SafeCheckbox("clantag", &g_clantag);
        if (g_clantag && !g_safeMode)
            ImGui::OptionPopup("clantag settings", []() {
                int& anim = g_clantagAnim;
                if (anim > 4) anim = 4;
                ImGui::Combo("animation##clantag", &anim, "static\0typewriter\0scroll\0frame by frame\0now playing\0");

                if (anim != 3)
                    ImGui::InputText("tag text##clantag", g_clantagText, sizeof(g_clantagText));

                if (anim == 3)
                {
                    static std::vector<clantag::Frame> rows;
                    static std::string synced = "\x01";   // never a real value, first draw always splits
                    static int selected = 0;

                    if (synced != g_clantagFrames)
                    {
                        rows = clantag::ParseFrames(g_clantagFrames, g_clantagSpeed);
                        synced = g_clantagFrames;
                    }

                    bool changed = false;
                    const int count = (int)rows.size();

                    ImGui::Label("frames");
                    if (ImGui::BeginListBox("##clantag frames", ImVec2(0.f, 92.f)))
                    {
                        for (int i = 0; i < count; i++)
                        {
                            char row[64];
                            snprintf(row, sizeof(row), "%d. %s   %.1f s", i + 1, rows[i].text.empty() ? "(empty)" : rows[i].text.c_str(), rows[i].hold);
                            ImGui::PushID(i);
                            if (ImGui::Selectable(row, selected == i, ImGuiSelectableFlags_DontClosePopups))
                                selected = i;
                            ImGui::PopID();
                        }
                        ImGui::EndListBox();
                    }

                    selected = std::clamp(selected, 0, (std::max)(count - 1, 0));

                    if (count > 0)
                    {
                        auto& frame = rows[selected];
                        char buffer[25];
                        strncpy_s(buffer, frame.text.c_str(), _TRUNCATE);
                        if (ImGui::InputText("frame text##clantag", buffer, sizeof(buffer)))
                        {
                            frame.text = buffer;
                            frame.text.erase(std::remove(frame.text.begin(), frame.text.end(), '|'), frame.text.end());   // cfg frame split
                            changed = true;
                        }
                        if (ImGui::SliderFloat("hold time##clantag frame", &frame.hold, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp))
                            changed = true;
                        if (ImGui::Button("move up##clantag", ImVec2(-1.f, 15.f)) && selected > 0)
                        {
                            std::swap(rows[selected], rows[selected - 1]);
                            selected -= 1;
                            changed = true;
                        }
                        if (ImGui::Button("move down##clantag", ImVec2(-1.f, 15.f)) && selected + 1 < count)
                        {
                            std::swap(rows[selected], rows[selected + 1]);
                            selected += 1;
                            changed = true;
                        }
                    }

                    if (ImGui::Button("add frame##clantag", ImVec2(-1.f, 15.f)) && count < 32)
                    {
                        clantag::Frame frame;
                        if (count > 0) frame.hold = rows[selected].hold;
                        selected = count > 0 ? selected + 1 : 0;
                        rows.insert(rows.begin() + selected, frame);
                        changed = true;
                    }
                    if (ImGui::Button("remove frame##clantag", ImVec2(-1.f, 15.f)) && count > 0)
                    {
                        rows.erase(rows.begin() + selected);
                        changed = true;
                    }

                    if (changed)
                    {
                        strncpy_s(g_clantagFrames, clantag::JoinFrames(rows).c_str(), _TRUNCATE);
                        synced = g_clantagFrames;
                    }
                }

                if (anim == 4)
                    ImGui::TextDisabled("tag text shows when nothing plays");
                if (anim == 1 || anim == 2 || anim == 4)
                    ImGui::SliderFloat("speed##clantag", &g_clantagSpeed, 0.1f, 2.f, "%.1f s");
                if (anim == 1)
                    ImGui::SliderFloat("hold time##clantag", &g_clantagHold, 0.5f, 10.f, "%.1f s");
            }, ImVec2(220.f, -1.f));

        // Left out, see report N/A: deagle spinner (CS:GO-only idle anim layer, no evidence it ships in CSS's weapon_deagle
        // viewmodel), insecure bypass (CS:GO VAC "Trusted Mode" / -insecure; no such check in CSS
        // source), random rank + medal (CS:GO competitive rank badge; CSS scoreboard draws
        // neither), chat images (CSS SayText2 is plain localized text, cs_hud_chat.cpp has no
        // image markup).
        ImGui::Checkbox("chat translator", &g_chatTranslator);
        if (g_chatTranslator)
            ImGui::OptionPopup("chat translator settings", []() {
                ImGui::Combo("translate to##chat translator", &g_chatTranslatorLang, kChatLanguageNames);
                ImGui::InputTextWithHint("libre fallback url##chat translator", "http://localhost:5000", g_chatLibreUrl, sizeof(g_chatLibreUrl));
                ImGui::InputTextWithHint("libre api key##chat translator", "optional", g_chatLibreKey, sizeof(g_chatLibreKey));
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("bot names", &g_botNames);
        if (g_botNames)
            ImGui::OptionPopup("bot names settings", []() {
                const int mode = g_botNamesMode;
                ImGui::Combo("names##botnames", &g_botNamesMode, "original\0same name\0random string\0steam names\0");
                if (mode == 1)
                    ImGui::InputText("name##botnames", g_botNamesSame, sizeof(g_botNamesSame));
                else if (mode == 2)
                    ImGui::SliderInt("length##botnames", &g_botNamesLength, 1, 31);   // player_info_t name is 32 bytes
                else if (mode == 3)
                {
                    char status[96];
                    BotNames_Status(status, sizeof(status));
                    ImGui::Label("%s", status);
                }
                ImGui::Checkbox("remove bot prefix##botnames", &g_botNamesNoPrefix);
                ImGui::Checkbox("random ping##botnames", &g_botNamesPing);
                if (ImGui::Button("randomize##botnames", ImVec2(-1.f, 15.f)))
                    BotNames_Randomize();
            }, ImVec2(220.f, -1.f));

        // fpsoverlay names tab 1:1 (hacks/misc/names.cpp), split over two cogs. Other players'
        // own names (fpsoverlay aliases) = misc > player list.
        static auto namesCycleRows = []() {
            // fpsoverlay CYCLE section: shown whenever any cycle is active
            const bool anyCycle = (g_useCustomUsername && g_enableNameCycle)
                || (g_streamerMode == 1 && (g_enableTeamLabelCycle || g_enableEnemyLabelCycle))
                || (g_streamerMode == 2 && g_enableEveryoneLabelCycle);
            if (!anyCycle) return;
            ImGui::Combo("cycle on##names", &g_nameTrigger, "timer\0on kill\0");
            if (g_nameTrigger == 0)
                ImGui::SliderFloat("interval##names", &g_nameInterval, 0.1f, 60.f, "%.1f s");
        };

        ImGui::Checkbox("custom name", &g_useCustomUsername);
        if (g_useCustomUsername)
            ImGui::OptionPopup("custom name settings", []() {
                ImGui::InputText("your name##customname", g_customUsername, sizeof(g_customUsername));
                ImGui::Checkbox("cycle name##customname", &g_enableNameCycle);
                if (g_enableNameCycle)
                    Names_EntryList("##names", g_nameList, "+ add name");
                namesCycleRows();
            }, ImVec2(220.f, -1.f));

        {
            // botox "others shown as": team labels / one label only (2 modes). The TF2 port's
            // "their class" option is dropped (DE-TF2 rule -- CS has no per-class playermodel,
            // only CT/T skins; kClassNames/GetPlayerClass usage removed from names.cpp).
            static int s_lastMode = 1;   // checkbox brings back the last mode picked
            bool streamer = g_streamerMode != 0;
            if (g_streamerMode != 0) s_lastMode = g_streamerMode;
            if (ImGui::Checkbox("streamer mode", &streamer))
                g_streamerMode = streamer ? s_lastMode : 0;
        }
        if (g_streamerMode != 0)
            ImGui::OptionPopup("streamer mode settings", []() {
                int shown = g_streamerMode - 1;
                if (ImGui::Combo("others shown as##streamer", &shown, "team labels\0one label\0"))
                    g_streamerMode = shown + 1;
                if (g_streamerMode == 1)
                {
                    ImGui::InputText("teammate label##streamer", g_streamerTeamLabel, sizeof(g_streamerTeamLabel));
                    ImGui::Checkbox("cycle teammate label##streamer", &g_enableTeamLabelCycle);
                    if (g_enableTeamLabelCycle)
                        Names_EntryList("##teamlabels", g_teamLabelList, "+ add label");
                    ImGui::InputText("enemy label##streamer", g_streamerEnemyLabel, sizeof(g_streamerEnemyLabel));
                    ImGui::Checkbox("cycle enemy label##streamer", &g_enableEnemyLabelCycle);
                    if (g_enableEnemyLabelCycle)
                        Names_EntryList("##enemylabels", g_enemyLabelList, "+ add label");
                }
                else if (g_streamerMode == 2)
                {
                    ImGui::InputText("everyone label##streamer", g_streamerEveryoneLabel, sizeof(g_streamerEveryoneLabel));
                    ImGui::Checkbox("cycle everyone label##streamer", &g_enableEveryoneLabelCycle);
                    if (g_enableEveryoneLabelCycle)
                        Names_EntryList("##evlabels", g_everyoneLabelList, "+ add label");
                }
                namesCycleRows();
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("auto revive", &g_autoRevive);
        if (g_autoRevive)
            ImGui::OptionPopup("auto revive settings", []() {
                ImGui::Combo("revive##autorevive", &g_autoReviveMode, "revive everyone\0revive only yourself\0only targets\0yourself and targets\0");
                if (g_autoReviveMode >= 2)
                    ImGui::Label("targets: misc > player list");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("auto buy", &g_autoBuy);
        if (g_autoBuy)
            // botox order: primary / secondary / armor / grenades / extras. CSS 1.6-era weapon
            // set: no ssg08/sg553-combo(sg552+aug kept separate)/mp9/mp7/bizon/nova/sawedoff/
            // mag7/m249/negev/auto-sniper/tec9/zeus/molotov/decoy (none exist, cs_weapon_parse.cpp).
            ImGui::OptionPopup("auto buy settings", []() {
                ImGui::Combo("primary##autobuy", &g_autoBuyPrimary,
                             "none\0ak47 / m4a1\0awp\0scout\0sg552 / aug\0galil / famas\0mac10 / tmp\0ump45\0p90\0m3\0xm1014\0");
                ImGui::Combo("secondary##autobuy", &g_autoBuySecondary, "none\0p228\0deagle\0elite / fiveseven\0");
                ImGui::Combo("armor##autobuy", &g_autoBuyArmor, "none\0kevlar\0kevlar + helmet\0");
                static const char* const kGrenades[] = { "he grenade", "one flash", "two flashes", "smoke" };
                ImGui::MultiCombo("grenades##autobuy", &g_autoBuyGrenades, kGrenades, 4);
                static const char* const kExtras[] = { "defuser" };
                ImGui::MultiCombo("extras##autobuy", &g_autoBuyExtras, kExtras, 1);
            }, ImVec2(220.f, -1.f));

        // deagle spinner: CS:GO-only weapon_deagle idle-animation layer (introduced well after
        // CSS's original HL2EP1-era content); no evidence of that layer in CSS's model/anim set
        // -- see report N/A.
        // insecure bypass: CS:GO VAC "Trusted Mode" / -insecure launch flag; zero "insecure" /
        // "trusted" hits anywhere in css-community-master -- see report N/A.
        SafeCheckbox("sv_pure bypass", &g_pureBypass);

        ImGui::Checkbox("performance", &g_perfEnable);
        if (g_perfEnable)
            ImGui::OptionPopup("performance settings", []() {
                ImGui::Checkbox("threaded bone setup", &g_perfThreadedBones);
                ImGui::Checkbox("preload assets", &g_perfForcePreload);
                ImGui::Checkbox("skip unused bone records", &g_perfSkipUnusedRecords);
                ImGui::Checkbox("multicore rendering", &g_perfMulticore);
                ImGui::Checkbox("high priority", &g_perfHighPriority);
                ImGui::Checkbox("game on fast cores", &g_perfFastCores);
                ImGui::Checkbox("full fps when tabbed out", &g_perfNoFocusSleep);
                ImGui::Checkbox("1ms timer", &g_hiTimerRes);   // CSS-real extra, not a botox row -- KEPT EXTRA
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("discord rpc", &g_discordEnable);
        if (g_discordEnable)
            ImGui::OptionPopup("discord rpc settings", []() {
                ImagePick_Input("image url##discord rpc", "https://i.imgur.com/KDUzwZf.png", g_discordImageKey,
                                sizeof(g_discordImageKey), true);
            }, ImVec2(220.f, -1.f));

        // botox menu_tab_settings.cpp:2177 1:1 (window = hacks/misc/minesweeper.cpp, drawn by RenderMenu)
        ImGui::Checkbox("minesweeper", &g_msOpen);
        if (g_msOpen)
            ImGui::OptionPopup("minesweeper settings", []() {
                ImGui::Checkbox("night theme##minesweeper", &g_msNight);
                if (g_msDiff < 0 || g_msDiff > MsGame::kCustom) g_msDiff = 1;
                ImGui::Combo("board size##minesweeper", &g_msDiff, "beginner 9x9\0intermediate 16x16\0expert 30x16\0custom\0");
                if (g_msDiff == MsGame::kCustom)
                {
                    ImGui::SliderInt("width##minesweeper", &g_msWidth, MsGame::kMinSide, MsGame::kMaxW);
                    ImGui::SliderInt("height##minesweeper", &g_msHeight, MsGame::kMinSide, MsGame::kMaxH);
                    ImGui::SliderInt("mines##minesweeper", &g_msMines, 1,
                                     MsGame::MaxMines(ImClamp(g_msWidth, MsGame::kMinSide, MsGame::kMaxW),
                                                      ImClamp(g_msHeight, MsGame::kMinSide, MsGame::kMaxH)));
                }
            }, ImVec2(220.f, -1.f));

        // "net settings" (lerp/rate presets), "net graph color/position" (ENGINE CNetGraphPanel
        // hooks) removed: TF2-cheat-base leftovers, no botox csgo equivalent (netregion.h).
        // botox's own net graph (hud_extra.cpp DrawNetGraph, g_hudNg*) is unrelated and stays.

        ImGui::Checkbox("sv_cheats bypass", &g_cheatsBypass);   // not a botox row, CSS-real -- KEPT EXTRA
    }
    menu_group_end();

    if (menu_group_begin("convenience"))
    {
        ConvenienceButtons();
    }
    menu_group_end();

    menu_columns_end();

    // UNIT_MENU_SLOT misc_main
    UnitE_Menu_misc_main();
}

// botox menu_tab_settings.cpp cvars_page 1:1: one full-height "cvars" box, input + "add" on
// top (';' splits into rows), a rule, the rows (edit / down / up / x) in their own scrolling
// child, "execute" at the bottom. Rows run top to bottom on map load / server join
// (game_extras.cpp). 16 slots of g_cvarRows instead of botox's one '\n' string.
static void Page_MiscCvars()
{
    static char s_input[512] = "";
    static int s_count = 0;
    static uint32_t s_synced = 0;
    auto rowsHash = []() {
        uint32_t h = 2166136261u;
        for (int i = 0; i < kCvarRows; ++i)
            for (const char* c = g_cvarRows[i]; ; ++c) { h = (h ^ (uint8_t)*c) * 16777619u; if (!*c) break; }
        return h | 1u;
    };
    // a config load changed the rows under us: botox re-splits list_var when it != synced
    if (rowsHash() != s_synced)
    {
        s_count = 0;
        for (int i = 0; i < kCvarRows; ++i) if (g_cvarRows[i][0]) s_count = i + 1;
        s_synced = rowsHash();
    }

    // custom InputText spans to WorkRect.Max.x, only a table cell narrows it; it also pushes itself 10 px down, frame 17 tall.
    // button frame = bb + 2 px each side
    constexpr float kButton = 16.f, kGap = 7.f, kSide = kButton * 3.f + kGap * 2.f + 4.f, kFrame = 17.f, kInputLift = 10.f;
    const float spacing = ImGui::GetStyle().WindowPadding.x;

    auto beginRowTable = [&](const char* id) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 0.f));
        const bool open = ImGui::BeginTable(id, 2);
        ImGui::PopStyleVar();
        if (!open) return false;
        ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##side", ImGuiTableColumnFlags_WidthFixed, kSide);
        return true;
    };
    auto nextRow = [&](float height) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, height);
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - kInputLift);
    };
    auto sideColumn = []() {
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    };
    bool changed = false, execute = false;

    if (menu_group_begin("cvars", 0.f, ImGui::GetContentRegionAvail().y - 20.f))   // 20 = the title band above the box
    {
        const float top = ImGui::GetCursorPosY();

        if (beginRowTable("##cvar add"))
        {
            nextRow(kFrame);
            const bool enter = ImGui::InputTextWithHint("##cvar input", "sensitivity 1; noclip; mp_timelimit 999", s_input,
                                                        sizeof(s_input), ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter) ImGui::SetKeyboardFocusHere(-1);
            sideColumn();
            const bool add = ImGui::Button("add", ImVec2(-1.f, kFrame - 4.f));
            ImGui::EndTable();

            if (enter || add)
            {
                for (const std::string& cmd : n_game::SplitCvars(s_input, ';'))
                    if (s_count < kCvarRows) strncpy_s(g_cvarRows[s_count++], cmd.c_str(), _TRUNCATE);
                s_input[0] = '\0';
                changed = true;
            }
        }

        const float lineY = top + kFrame + spacing;
        ImGui::SetCursorPosY(lineY);
        const ImVec2 line = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(line, ImVec2(line.x + ImGui::GetContentRegionAvail().x, line.y),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::SetCursorPosY(lineY + 1.f + spacing);

        // bottom = execute: standard 15 px button, frame 19
        if (ImGui::BeginChild("##cvar rows", ImVec2(0.f, -(15.f + 4.f + spacing)), false, 0))
        {
            int moveFrom = -1, moveTo = -1, remove = -1;
            if (!s_count)
                ImGui::TextDisabled("no commands, run top to bottom on map load / server join");
            else if (beginRowTable("##cvar list"))
            {
                for (int i = 0; i < s_count; ++i)
                {
                    ImGui::PushID(i);
                    nextRow(kFrame + spacing);
                    changed |= ImGui::InputText("##command", g_cvarRows[i], sizeof(g_cvarRows[i]));
                    sideColumn();
                    ImGui::BeginDisabled(i == s_count - 1);
                    if (IconButton("##down", 0, kFrame - 4.f)) moveFrom = i, moveTo = i + 1;
                    ImGui::EndDisabled();
                    ImGui::SameLine(0.f, kGap);
                    ImGui::BeginDisabled(i == 0);
                    if (IconButton("##up", 1, kFrame - 4.f)) moveFrom = i, moveTo = i - 1;
                    ImGui::EndDisabled();
                    ImGui::SameLine(0.f, kGap);
                    if (IconButton("##delete", 2, kFrame - 4.f)) remove = i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (moveFrom >= 0)
            {
                char tmp[sizeof(g_cvarRows[0])];
                memcpy(tmp, g_cvarRows[moveFrom], sizeof(tmp));
                memcpy(g_cvarRows[moveFrom], g_cvarRows[moveTo], sizeof(tmp));
                memcpy(g_cvarRows[moveTo], tmp, sizeof(tmp));
                changed = true;
            }
            if (remove >= 0)
            {
                for (int i = remove; i + 1 < kCvarRows; ++i) memcpy(g_cvarRows[i], g_cvarRows[i + 1], sizeof(g_cvarRows[i]));
                g_cvarRows[kCvarRows - 1][0] = '\0';
                --s_count;
                changed = true;
            }
        }
        ImGui::EndChild();

        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetItemRectMax().y + spacing + 2.f));
        ImGui::BeginDisabled(s_count == 0);
        execute = ImGui::Button("execute", ImVec2(-1.f, 15.f));
        ImGui::EndDisabled();
    }
    menu_group_end();

    if (changed) s_synced = rowsHash();
    if (execute) GameExtras_ExecCvars();
}

// botox misc pages: main, player list, cvars (botox order).
// botox misc > player list slot: here it holds custom names' per-player aliases (fpsoverlay
// names tab alias list), the page botox fills with revive / aimbot picks.
static void Page_MiscPlayerList()
{
    if (menu_group_begin("player list", 0.f, ImGui::GetContentRegionAvail().y - 20.f))   // 20 = the title band above the box
    {
        if (ImGui::BeginChild("##player list rows", ImVec2(0.f, 0.f), false, 0))
            Names_PlayerListMenu();
        ImGui::EndChild();
    }
    menu_group_end();
}

const char* kMiscSub[3] = { "main", "player list", "cvars" };
int s_miscSub = 0;
void Tab_MiscRoot()
{
    if (s_miscSub == 1) { Page_MiscPlayerList(); return; }
    if (s_miscSub == 2) { Page_MiscCvars(); return; }
    Page_MiscMain();
}
