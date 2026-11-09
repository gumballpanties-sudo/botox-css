#include "menu_internal.h"

// rs::MoveBit mask as a botox MultiCombo. MultiCombo is bit i = item i, so the drawn rows are the
// bits in `allowedBits`, in MoveBit order, and only those bits are rewritten. Never all off: a
// route that may press nothing solves nothing and blames the markers
static bool RcMoveMaskCombo(const char* id, int& mask, unsigned allowedBits = 0xFFFFFFFFu)
{
    const char* names[32]; int bits[32]; int n = 0;
    for (int b = 0; b < RC_MoveCount() && n < 32; b++)
        if (allowedBits & (1u << b)) { names[n] = RC_MoveName(b); bits[n++] = b; }
    int flags = 0;
    for (int i = 0; i < n; i++) if (mask & (1 << bits[i])) flags |= 1 << i;
    if (!ImGui::MultiCombo(id, &flags, names, n)) return false;
    int drawn = 0, built = 0;
    for (int i = 0; i < n; i++)
    {
        drawn |= 1 << bits[i];
        if (flags & (1 << i)) built |= 1 << bits[i];
    }
    if (!built) return false;
    mask = (mask & ~drawn) | built;
    return true;
}

static void Page_MovementMain()
{
    menu_columns_begin();

    if (menu_group_begin("movement"))
    {
        // botox :308-316
        ImGui::Checkbox("bunny hop", &g_bhop);
        if (g_bhop)
            ImGui::OptionPopup("bunny hop settings", [&]() {
                SliderI("success rate##bunny hop chance", &g_bhopChance, 0, 100, "%d%%");
            }, ImVec2(220.f, -1));

        // botox :318-329
        UnitA_Menu_DelayHop();

        ImGui::Checkbox("edge jump", &g_edgeJump);
        if (g_edgeJump)
            ImGui::OptionPopup("edge jump settings", [&]() {
                ImGui::Label("edge jump keybind");
                ImGui::Keybind("edge jump key", &g_edgeJumpKey, &g_edgeJumpMode);
                ImGui::Checkbox("on ladders##edge jump ladder", &g_edgeJumpLadder);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("long jump", &g_longJump);
        if (g_longJump)
            ImGui::OptionPopup("long jump settings", [&]() {
                ImGui::Label("long jump keybind");
                ImGui::Keybind("long jump key", &g_longJumpKey, &g_longJumpMode);
                ImGui::Checkbox("adaptive key cancelling", &g_longJumpAdaptive);
                if (g_longJumpAdaptive)
                {
                    // botox's 3 stop-reason bools (let go/landed/time passes) -> one MultiCombo
                    // mask, g_longJumpAdaptiveStopMask (bit0/1/2 same order, cfg/u_mvcore.h)
                    static const char* const kLjStop[] = { "let go", "landed", "time passes" };
                    ImGui::MultiCombo("stop cancelling when##ljadstop", &g_longJumpAdaptiveStopMask, kLjStop, 3);
                    if (g_longJumpAdaptiveStopMask & (1 << 2))
                        ImGui::SliderFloat("time after jump##ljadtime", &g_longJumpAdaptiveTime, 0.02f, 1.f, "%.2f s");
                }
                ImGui::Checkbox("edge jump##ljej", &g_longJumpEdge);
                if (g_longJumpEdge)
                    ImGui::Checkbox("on ladders##ljejladder", &g_longJumpEdgeLadder);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("mini jump", &g_miniJump);
        if (g_miniJump)
            ImGui::OptionPopup("mini jump settings", [&]() {
                ImGui::Label("mini jump keybind");
                ImGui::Keybind("mini jump key", &g_miniJumpKey, &g_miniJumpMode);
                ImGui::Checkbox("edge jump##mjej", &g_miniJumpEdge);
                ImGui::Checkbox("hold crouch##mjhc", &g_miniJumpHoldDuck);
            }, ImVec2(220.f, -1));

        // botox :378-393. "predict the hit"/"yield to pixel surf" dropped 11-03 (no botox row,
        // now internal to AutoJumpbug). Real botox rows added here.
        ImGui::Checkbox("jump bug", &g_jumpbug);
        if (g_jumpbug)
            ImGui::OptionPopup("jump bug settings", [&]() {
                ImGui::Label("jump bug keybind");
                ImGui::Keybind("jump bug key", &g_jumpbugKey, &g_jumpbugMode);
                ImGui::Checkbox("while crouching", &g_jumpbugCrouch);
                ImGui::Checkbox("kangaroo", &g_jumpbugKangaroo);
                // "jb only" dropped 11-07: the rewrite only ever touches the cmd on an engine-proven jb
                ImGui::Checkbox("height assist", &g_jumpbugAssist);
                if (g_jumpbugAssist)
                    SliderI("assist ticks##jba", &g_jumpbugAssistTicks, 1, 8);
            }, ImVec2(220.f, -1));

        // botox :395-405
        UnitA_Menu_AutoCrouch();

        ImGui::Checkbox("auto strafe", &g_autoStrafe);
        if (g_autoStrafe)
            ImGui::OptionPopup("auto strafe settings", [&]() {
                ImGui::Label("auto strafe keybind");
                ImGui::Keybind("auto strafe key", &g_autoStrafeKey, &g_autoStrafeMode);
                ImGui::Combo("type##astype", &g_autoStrafeType, "standard\0WASD\0circle (mouse)\0");
                if (g_autoStrafeType == 1)
                    ImGui::Checkbox("strafe in last pressed direction", &g_autoStrafeLastKeys);
                if (g_autoStrafeType == 2)
                    ImGui::Checkbox("strafe in last mouse direction", &g_autoStrafeLastMouse);
                ImGui::Checkbox("avoid wall collisions", &g_autoStrafeAvoidWalls);
                if (g_autoStrafeAvoidWalls)
                    SliderF("avoid distance", &g_autoStrafeAvoidDist, 0.f, 200.f, "%.0f units");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("strafe optimizer", &g_strafeOpt);
        if (g_strafeOpt)
            ImGui::OptionPopup("strafe optimizer settings", [&]() {
                SliderF("gain", &g_strafeOptGain, 0.f, 100.f, "%.0f%%");
                SliderF("min speed", &g_strafeOptMinSpeed, 0.f, 320.f, "%.0f");
            }, ImVec2(220.f, -1));

        // botox :438-461. "ladder bug" drives anti_ladder (CSS has no up-move ladder ride).
        // "poop joomp"/"poop soorf" = N/A, no sv_jump_impulse / jetpack on CSS.
        ImGui::Checkbox("ladder bug", &g_antiLadder);
        ImGui::Keybind("ladder bug key", &g_antiLadderKey, &g_antiLadderMode);

        ImGui::Checkbox("ladder glide", &g_ladderGlide);
        ImGui::Keybind("ladder glide key", &g_ladderGlideKey, &g_ladderGlideMode);

        ImGui::Checkbox("ladder freelook climb", &g_ladderFreelookClimb);
        ImGui::Keybind("ladder freelook climb key", &g_ladderFreelookClimbKey, &g_ladderFreelookClimbMode);

        ImGui::Checkbox("fast ladder", &g_fastLadder);
        ImGui::Keybind("fast ladder key", &g_fastLadderKey, &g_fastLadderMode);

        ImGui::Checkbox("edge bug", &g_edgeBug);
        if (g_edgeBug)
            ImGui::OptionPopup("edge bug settings", [&]() {
                ImGui::Label("edgebug keybind");
                ImGui::Keybind("edge bug key", &g_edgeBugKey, &g_edgeBugMode);
                ImGui::Combo("style##ebstyle", &g_edgeBugStyle, "botox\0delusional\0dna\0hotwheels\0lobotomy v2\0");
                const int style = g_edgeBugStyle;
                if (style == 4)
                {
                    // botox menu_tab_movement.cpp:489-501
                    ImGui::Checkbox("advanced search##eblobadv", &g_edgeBugLobAdvanced);
                    ImGui::Checkbox("silent edgebug##eblobsilent", &g_edgeBugLobSilent);
                    ImGui::Checkbox("extra advanced search##eblobmega", &g_edgeBugLobMega);
                    if (g_edgeBugLobAdvanced)
                    {
                        ImGui::Checkbox("autostrafe to edge##eblobas", &g_edgeBugLobAutostrafe);
                        if (!g_edgeBugLobAutostrafe)
                        {
                            SliderF("strafe angle##eblobangle", &g_edgeBugLobStrafeAngle, 1.f, 179.f, "%.0f");
                            ImGui::Combo("delta scale##eblobdelta", &g_edgeBugLobDeltaType, "1x\x00" "0.8x\x00" "0.5x\x00" "0.25x\x00");
                        }
                    }
                    SliderI("edgebug ticks##eblobticks", &g_edgeBugLobTicks, 1, 64);
                    SliderF("edgebug lock##ebloblock", &g_edgeBugLobLock, 0.f, 10.f, "%.2f");
                }
                if (style == 1)
                {
                    ImGui::Checkbox("mouse fix##ebdelmousefix", &g_edgeBugDelMouseFix);
                    SliderI("ticks to predict##ebdelticks", &g_edgeBugDelTicks, 0, 128);
                    ImGui::Checkbox("advanced detection##ebdeladv", &g_edgeBugDelAdvanced);
                    if (g_edgeBugDelAdvanced)
                    {
                        SliderF("angle limit##ebdelangle", &g_edgeBugDelAngleLimit, 0.f, 180.f, "%.2f");
                        if (g_edgeBugDelAngleLimit > 0.f)
                        {
                            SliderI("search amount##ebdelsearch", &g_edgeBugDelSearch, 1, 15);
                            ImGui::Checkbox("silent edgebug##ebdelsilent", &g_edgeBugDelSilent);
                        }
                    }
                }
                else if (style == 2)
                {
                    SliderF("scan time##ebdnascan", &g_edgeBugDnaScan, 0.f, 1.f, "%.2f s");
                    ImGui::Checkbox("advanced mode##ebdnaadv", &g_edgeBugDnaAdvanced);
                    if (g_edgeBugDnaAdvanced)
                    {
                        SliderI("advanced range##ebdnarange", &g_edgeBugDnaRange, 1, 10);
                        ImGui::Checkbox("strafe custom angle##ebdnacustom", &g_edgeBugDnaCustomAngle);
                        SliderF("angle limit##ebdnaangle", &g_edgeBugDnaAngleLimit, 0.f, 45.f, "%.1f");
                    }
                }
                else if (style == 3)
                {
                    SliderI("edge bug predicted ticks##ebhwticks", &g_edgeBugHwTicks, 0, 32);
                    SliderF("mouse lock scale##ebhwlock", &g_edgeBugHwLockAmt, 0.f, 1.f, "%.2f");
                    ImGui::Checkbox("advanced detection##ebhwadv", &g_edgeBugHwAdvanced);
                    if (g_edgeBugHwAdvanced)
                        SliderF("max strafe delta##ebhwdelta", &g_edgeBugHwStrafeDeltaMax, 0.1f, 100.f, "%.1f%%");
                }
                if (style == 0)
                {
                    SliderF("edge bug predict time", &g_edgeBugPredictTime, 0.02f, 2.f, "%.2f s");
                    // none picked = both
                    static const char* const kEbTypes[] = { "ducked", "standing" };
                    ImGui::MultiCombo("stance##ebtypes", &g_edgeBugStance, kEbTypes, 2);
                    ImGui::Checkbox("autostrafe to edge", &g_edgeBugToEdge);
                    if (g_edgeBugToEdge)
                    {
                        SliderF("max path bend##ebstrafemax", &g_edgeBugStrafeAngleMax, 0.f, 180.f, "%.0f deg");
                        ImGui::Checkbox("lip refine##ebrefine", &g_edgeBugLipRefine);
                        if (g_edgeBugLipRefine)
                            SliderI("refine steps##ebrefsteps", &g_edgeBugRefineSteps, 1, 12);
                        ImGui::Checkbox("path bend##ebbend", &g_edgeBugPathBend);
                        ImGui::Checkbox("ebanat shapes##ebshapes", &g_edgeBugEbanatShapes);
                    }
                    ImGui::Checkbox("duck timing##ebducktiming", &g_edgeBugDuckTiming);
                    if (g_edgeBugDuckTiming)
                        ImGui::Checkbox("full window##ebfullwin", &g_edgeBugFullWindow);
                    SliderF("search effort##ebeffort", &g_edgeBugSearchEffort, 0.25f, 3.f, "%.2fx");
                    ImGui::Checkbox("mouse steer##ebsteer", &g_edgeBugMouseSteer);
                    if (g_edgeBugMouseSteer)
                        SliderI("paths##ebpaths", &g_edgeBugPaths, 1, 8);
                }
                if (style != 3)
                    ImGui::Checkbox("lock mouse on find", &g_edgeBugMouseLock);
                if (style != 3 && g_edgeBugMouseLock)
                {
                    ImGui::Combo("lock type##eblocktype", &g_edgeBugLockType,
                                 style == 0 ? "smooth\0full\0" : "static\0full\0");
                    SliderF("lock strength##eblockstr", &g_edgeBugLockStrength, 0.f, 100.f, "%.0f%%");
                }
                ImGui::Combo("detection sound##ebsound", &g_edgeBugSoundMode, "none\0arena switch\0button\0money\0beep\0custom\0");
                if (g_edgeBugSoundMode == 5)
                    ImGui::InputTextWithHint("sound path##ebsndpath", "sound/weapons/myhit.wav", g_edgeBugSoundCustom, sizeof(g_edgeBugSoundCustom));
                if (g_edgeBugSoundMode > 0)
                    SliderF("sound volume##ebsndvol", &g_edgeBugSoundVolume, 0.f, 2.f, "%.2f");
                if (style == 0)
                {
                    ImGui::Combo("detection mode##ebdetectmode", &g_edgeBugDetectMode, "normal\0enhanced\0ebanat\0");
                    if (g_edgeBugDetectMode == 1)
                        SliderI("search amount##ebsearch", &g_edgeBugSearchAmount, 1, 4);
                }
                // "visualize"/"pad size" moved out: botox has no visualize row in THIS popup --
                // it's the "edgebug visualize" row on the detections page (case 1, see unitB).
            }, ImVec2(200.f, -1));

        // botox :568-603. g_edgeSkipTicks (old "search window ticks") is dead -- NEEDS FROM
        // OTHERS to delete outright (lives in dllmain.cpp/globals.h, shared file, not ours).
        ImGui::Checkbox("edge skip", &g_edgeSkip);
        if (g_edgeSkip)
            ImGui::OptionPopup("edge skip settings", [&]() {
                ImGui::Label("edge skip keybind");
                ImGui::Keybind("edge skip key", &g_edgeSkipKey, &g_edgeSkipMode);
                ImGui::Checkbox("use edgebug settings##esuseeb", &g_edgeSkipUseEb);
                if (g_edgeSkipUseEb)
                    return;
                SliderF("predict time##esptime", &g_edgeSkipPredictTime, 0.02f, 2.f, "%.2f s");
                SliderF("max path bend##esbend", &g_edgeSkipMaxBend, 0.f, 180.f, "%.0f deg");
                SliderI("search amount##essearch", &g_edgeSkipDepth, 1, 4);

                ImGui::Checkbox("lock mouse on find##eslock", &g_edgeSkipMouseLock);
                if (g_edgeSkipMouseLock)
                {
                    ImGui::Combo("lock type##eslocktype", &g_edgeSkipMouseLockType, "smooth\0full\0");
                    SliderF("lock strength##eslockstr", &g_edgeSkipMouseLockStrength, 0.f, 100.f, "%.0f%%");
                }

                ImGui::Combo("detection sound##essound", &g_edgeSkipSoundMode, "none\0arena switch\0button\0money\0beep\0custom\0");
                if (g_edgeSkipSoundMode == 5)
                    ImGui::InputTextWithHint("sound path##essndpath", "sound/weapons/myhit.wav", g_edgeSkipSoundCustom, sizeof(g_edgeSkipSoundCustom));
                if (g_edgeSkipSoundMode > 0)
                    SliderF("sound volume##essndvol", &g_edgeSkipSoundVolume, 0.f, 2.f, "%.2f");

                ImGui::Checkbox("visualize##esviz", &g_edgeSkipVisualize);
            }, ImVec2(220.f, -1));

        // "auto align" moved to the "other" group (botox :877-883)

        ImGui::Checkbox("pixel surf", &g_pixelSurf);
        if (g_pixelSurf)
            ImGui::OptionPopup("pixel surf settings", [&]() {
                ImGui::Label("pixel surf keybind");
                ImGui::Keybind("pixel surf key", &g_pixelSurfKey, &g_pixelSurfMode);
                // g_pixelSurfPredictTicks stays (routecalc.inl reads it) but is cfg-only, like botox
                ImGui::Checkbox("strafe into wall##pswallstrafe", &g_pixelSurfWallStrafe);
                ImGui::Combo("detection sound##pssound", &g_psSound, "none\0arena switch\0button\0money\0beep\0custom\0");
                if (g_psSound == 5)
                    ImGui::InputTextWithHint("sound path##pssndpath", "sound/weapons/myhit.wav", g_psSoundCustom, sizeof(g_psSoundCustom));
                if (g_psSound > 0)
                    SliderF("sound volume##pssndvol", &g_psSoundVolume, 0.f, 2.f, "%.2f");
            }, ImVec2(220.f, -1));

        // botox "pixelsurf assist" (:644-674), 1:1 (PSPORT round 2 11-04): PixelSurfAssistRun
        // (psassist.inl) is now the real n_assist::arrival_surf planner, botox's own
        // pixelsurf_assist.cpp, not the old PsSimHop/JT_* point-steering design this row used to
        // front. set/delete-point-key + snap distance are CSS kept extras (botox's own point
        // placement UI, points_ui.cpp, is not ported -- see report).
        ImGui::Checkbox("pixelsurf assist", &g_psAssist);
        if (g_psAssist || g_psFinder)
            ImGui::OptionPopup("pixelsurf assist settings", [&]() {
                ImGui::Label("assist keybind");
                ImGui::Keybind("pixelsurf assist key", &g_psAssistKey, &g_psAssistMode);
                ImGui::Label("set pixelsurf point key");
                BindKeyNoMode("set pixelsurf point key", &g_psSetPointKey);
                SliderI("assist ticks##pxticks", &g_psAssistTicks, 0, 128);
                ImGui::Combo("assist type##pxtyppee", &g_psAssistType, "standard\0delta strafe\0");
                ImGui::Checkbox("stamina hops", &g_psAssistBrokeHop);
                ImGui::Checkbox("ground help", &g_psGroundHelp);
                if (g_psGroundHelp)
                    SliderF("ground angle limit", &g_psGroundAngleLimit, 0.f, 45.f, "%.1f");
                ImGui::Checkbox("pixel surf assist render", &g_psAssistRender);
                if (g_psAssistRender)
                    SliderF("render height", &g_psAssistRenderHeight, 0.f, 100.f, "%.1f");
                // -- kept extras --
                ImGui::Label("delete point");
                BindKeyNoMode("##padelbind", &g_psDeletePointKey);
                SliderF("snap distance", &g_psAssistSnap, 4.f, 256.f, "%.0f");
            }, ImVec2(240.f, -1));

        ImGui::Checkbox("auto bounce", &g_autoBounce);
        if (g_autoBounce)
            ImGui::OptionPopup("auto bounce settings", [&]() {
                ImGui::Label("auto bounce keybind");
                ImGui::Keybind("auto bounce key", &g_autoBounceKey, &g_autoBounceMode);
                // botox menu_tab_movement.cpp:596-606. mode 3 = lumi (autobounce.inl AbLumi, 11-02)
                ImGui::Combo("mode##abmode", &g_abMode, "drop in\0keep speed\0stop\0lumi\0");
                if (g_abMode == 3)
                {
                    SliderI("rays##ablumi", &g_abLumiRays, 4, 64);
                    SliderF("radius##ablumi", &g_abLumiRadius, 5.f, 100.f, "%.0f u");
                    SliderF("depth##ablumi", &g_abLumiDepth, 50.f, 4096.f, "%.0f u");
                    SliderF("min angle##ablumi", &g_abLumiMinAngle, 5.f, 60.f, "%.0f deg");
                    SliderF("max angle##ablumi", &g_abLumiMaxAngle, 20.f, 90.f, "%.0f deg");
                    SliderF("smoothing##ablumi", &g_abLumiSmooth, 0.f, 1.f, "%.2f");
                    ImGui::Checkbox("auto jump", &g_abJump);
                    ImGui::Checkbox("crouch near face##abcrouch", &g_abCrouch);
                }
                else
                {
                    SliderF("min speed gain", &g_abMinGain, 0.f, 500.f, "%.0f");
                    SliderF("max sim reach##abreach", &g_abReach, 16.f, 512.f, "%.0f u");
                    ImGui::Checkbox("auto jump", &g_abJump);
                    ImGui::Checkbox("crouch jump##abcrouch", &g_abCrouch);
                    ImGui::Checkbox("auto align##abalign", &g_abAlign);
                    ImGui::Checkbox("visualize##abviz", &g_abViz);
                    ImGui::ColorEdit4("visualize color##abvizcolor", g_abVizColor,
                        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                        ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                        ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder);
                }
            }, ImVec2(220.f, -1));

        // g_bounceAssist (one-trace redirect, no points file) deleted: not the botox feature,
        // see head bounce assist above -- movement_tricks2.inl:28-33.

        ImGui::Checkbox("texture bug", &g_textureBug);
        if (g_textureBug)
            // also drives head surf + head bounce, which have no toggle of their own. Rows below
            // are botox csgo-sdk's own texture bug settings (menu_tab_movement.cpp:745-769) --
            // 11-03: the TF2-port's own extra knobs (hug range, predict*, ramp nz, exact solve
            // pass, no off-grid stick, park*) deleted, folded to fixed constants in texturebug.inl
            // (same old defaults, just no longer exposed -- see MVTB report). types allowed/auto
            // crouch/wallstrafe are real + persisted but NOT YET threaded into texturebug.inl's
            // internal stance/speed decisions (see MVTB report "NOT FULLY WIRED"). detection sound
            // is fully wired (reuses edgebug.inl's EbPlaySound, same stock CSS sounds).
            ImGui::OptionPopup("texture bug settings", [&]() {
                ImGui::Label("texture bug keybind");
                ImGui::Keybind("texture bug key", &g_textureBugKey, &g_textureBugMode);
                SliderF("reach##tbreach", &g_tbReach, 0.f, 48.f, "%.1f u");
                {
                    static const char* const kTbTypes[] = { "ducking", "standing" };
                    ImGui::MultiCombo("types allowed##tbtypes", &g_tbTypesMask, kTbTypes, 2);
                }
                ImGui::Checkbox("auto crouch##tbautoduck", &g_tbAutoDuck);
                ImGui::Checkbox("wallstrafe##tbws", &g_tbWallstrafeOpt);
                ImGui::Combo("detection sound##tbsound", &g_tbSound, "none\0arena switch\0button\0money\0beep\0custom\0");
                if (g_tbSound == 5)
                    ImGui::InputTextWithHint("sound path##tbsndpath", "sound/weapons/myhit.wav", g_tbSoundCustom, sizeof(g_tbSoundCustom));
                if (g_tbSound > 0)
                    SliderF("sound volume##tbsndvol", &g_tbSoundVolume, 0.f, 2.f, "%.2f");
            }, ImVec2(220.f, -1));

        // botox :785-797. style combo (cumidere/other engines) dropped: airstuck.inl is
        // now the real botox air_stuck.cpp 1:1 port, one engine, no style knob. "search budget"
        // renamed "fan budget" + range fixed 5-90 (was 10-90) -- 11-03.
        ImGui::Checkbox("air stuck", &g_airStuck);
        if (g_airStuck)
            ImGui::OptionPopup("air stuck settings", [&]() {
                ImGui::Label("air stuck keybind");
                ImGui::Keybind("air stuck key", &g_airStuckKey, &g_airStuckMode);
                ImGui::Checkbox("fix air stucks##as", &g_asFixStucks);
                ImGui::Checkbox("use movement fix##as", &g_asMovementFix);
                ImGui::Checkbox("screen message##as", &g_asScreenMessage);
                SliderF("extra reach##as", &g_asExtraReach, 0.f, 32.f, "%.2f");
                SliderF("fan budget##as", &g_asBudget, 5.f, 90.f, "%.0f%% tick");
            }, ImVec2(220.f, -1));

        // ref pixel hop (wallengine.inl, ref falling = send a cmd that lands this tick, ground = hop
        ImGui::Checkbox("pixel hop", &g_pixelHop);
        if (g_pixelHop)
            ImGui::OptionPopup("pixel hop settings", [&]() {
                ImGui::Label("pixel hop keybind");
                ImGui::Keybind("pixel hop key", &g_pixelHopKey, &g_pixelHopMode);
                // steer onto a pixel hop up to N ticks out (wallengine.inl reach pass)
                ImGui::Checkbox("reach pixel hops", &g_phReach);
                if (g_phReach)
                {
                    SliderI("reach ticks##phreach", &g_phReachTicks, 4, 32);
                    SliderF("max steer##phreach", &g_phReachAngle, 8.f, 135.f, "%.0f deg");
                }
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("wall climb", &g_wallClimb);
        if (g_wallClimb)
            ImGui::OptionPopup("wall climb settings", [&]() {
                ImGui::Label("wall climb keybind");
                ImGui::Keybind("wall climb key", &g_wallClimbKey, &g_wallClimbMode);
                ImGui::Checkbox("prevent slow", &g_wcPreventSlow);
                ImGui::Checkbox("crouch catches", &g_wcDuck);
                ImGui::Checkbox("steer to seam", &g_wallClimbSeamSteer);
                if (g_wallClimbSeamSteer)
                    ImGui::SliderFloat("delta strafe scale", &g_wallClimbSteerScale, 0.25f, 2.f, "%.2fx");
                ImGui::Checkbox("visualize##wcviz", &g_wcViz);
                ImGui::ColorEdit4("visualize color##wcvizcolor", g_wcVizColor,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                    ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder);
                ImGui::Checkbox("show wall slope", &g_showWallSlope);
                if (g_showWallSlope) {
                    ImGui::Label("show wall slope key");
                    ImGui::Keybind("show wall slope key", &g_showWallSlopeKey, &g_showWallSlopeMode);
                }
            }, ImVec2(200.f, -1));

        // botox menu_tab_movement.cpp:813-839. approach/reach/crouch jump -> fireman.inl (CSS
        // rebuild 11-07: CSS LadderMove grabs+flings in one tick, see its header).
        ImGui::Checkbox("fire man", &g_fireMan);
        if (g_fireMan)
            ImGui::OptionPopup("fire man settings", [&]() {
                ImGui::Label("fire man keybind");
                ImGui::Keybind("fire man key", &g_fireManKey, &g_fireManMode);
                ImGui::Checkbox("approach##fireman", &g_firemanApproach);
                SliderF("reach##fireman", &g_firemanReach, 64.f, 640.f, "%.0f u");
                ImGui::Checkbox("drop in##fireman", &g_firemanDropIn);
                // stops stray keys strafing you out of the shaft; eats fwd/side/duck, never jump
                if (g_firemanDropIn)
                    ImGui::Checkbox("lock movement##fireman", &g_firemanDropInLock);
                ImGui::Checkbox("crouch jump##fireman", &g_firemanCrouchJump);
                ImGui::Checkbox("sides only##fireman", &g_firemanSideOnly);
                if (g_firemanSideOnly)
                    ImGui::Keybind("sides only key", &g_firemanSideOnlyKey, &g_firemanSideOnlyMode);
            }, ImVec2(220.f, -1));
    }
    menu_group_end();

    menu_columns_next();

    // "fake movement" group (and column) removed entirely (11-03/11-04): no botox csgo
    // equivalent (CS:GO has no such feature, including "wallbug fix (sv_wallbug 1)" -- a TF2 x64
    // engine-build cvar). Code/globals/tests all deleted, see report CMAKE + files list.

    // botox "other" group, case 0 :870-1015. CSS used to split this across "movement"/"practice"/
    // "pixel surf" groups -- folded back into one "other" group, botox order, 11-04.
    if (menu_group_begin("other"))
    {
        UnitA_Menu_SmoothDemoView();

        // botox "pixel surf fix" (m_pixel_surf_fix): CSS's matching hold-duck-into-ride var
        // (g_psSurfFix) used to live inside the ps finder popup -- moved to its botox slot.
        ImGui::Checkbox("pixel surf fix", &g_psSurfFix);

        // plain on/off (11-07 rewrite: press into a face in reach, falling; "pixel jumps" creep dropped)
        ImGui::Checkbox("auto align", &g_autoAlign);
        if (g_autoAlign)
            ImGui::OptionPopup("auto align settings", [&]() {
                // one pinned tick = latched: presses into the face every tick (wall-stuck hold search)
                ImGui::Checkbox("hold pixel surfs", &g_aaHoldSurf);
            }, ImVec2(220.f, -1));

        UnitA_Menu_Nulls();
        UnitA_Menu_HalfSideways();

        ImGui::Checkbox("auto one hop", &g_autoOneHopLadder);
        ImGui::Keybind("auto one hop key", &g_autoOneHopLadderKey, &g_autoOneHopLadderMode);

        // botox :906-907 -- plain checkbox + keybind, no popup
        ImGui::Checkbox("blockbot", &g_blockBot);
        ImGui::Keybind("blockbot key", &g_blockBotKey, &g_blockBotMode);

        ImGui::Checkbox("fast stop", &g_fastStop);
        // "fast accelerate" / "no push" removed (11-03/11-04): no botox csgo equivalent
        // (grep hacks/movement/movement.cpp -- no such features; "no push" was
        // C_TFPlayer::AvoidPlayers, TF2-only, CCSPlayer has no such call)

        // botox :913-936, 1:1 (PSPORT 11-04: added "pixelsurfs" row, g_psFinderPixelsurf --
        // it had no standalone var before, always on while the finder ran). Scan margin/budget/
        // strict/jump surfs/ignore props/draw length are CSS kept extras, appended after botox's rows.
        ImGui::Checkbox("pixel finder", &g_psFinder);
        if (g_psFinder)
            ImGui::OptionPopup("pixel finder settings", [&]() {
                ImGui::Label("scan key");
                ImGui::Keybind("pixel finder key", &g_psFinderKey, &g_psFinderMode);
                ImGui::Checkbox("pixelsurfs##pf", &g_psFinderPixelsurf);
                ImGui::Checkbox("texturebugs##pf", &g_psFinderTb);
                ImGui::Checkbox("headbounces##pf", &g_psFinderHeadbounce);
                ImGui::Checkbox("pixeljumps##pf", &g_psFinderPixeljump);
                // -- kept extras --
                SliderF("scan margin", &g_psFinderMargin, 0.f, 64.f, "%.0f");
                SliderF("scan budget", &g_psFinderBudget, 0.25f, 32.f, "%.2f ms");
                ImGui::Checkbox("strict##pf", &g_psFinderStrict);
                ImGui::Checkbox("jump surfs##pf", &g_psFinderJumpSurf);
                ImGui::Checkbox("ignore props##pf", &g_psFinderNoProps);
                ImGui::Checkbox("draw pixelsurf length##pf", &g_psFinderLength);

                // whole-map scan: the same scanner, pointed at every brush edge in the map
                PsMapScanState ms;
                PS_MapScanGet(ms);
                char pmLabel[64];
                if (ms.busy) snprintf(pmLabel, sizeof(pmLabel), "cancel scan  %d%%", int(ms.progress * 100.f));
                else         snprintf(pmLabel, sizeof(pmLabel), "search whole map");
                if (ImGui::Button(pmLabel, ImVec2(-1.f, 0.f)))
                    PS_MapScanToggle();
                // PSPORT 11-04 fix: this row reused g_psFinderTb (the column scan's own
                // texturebug toggle, def true) -- toggling the column scan's pass silently
                // flipped the whole-map scan's too. botox keeps them separate vars and
                // separate defaults (m_pixel_finder_texturebugs true, _map_texturebugs false).
                ImGui::Checkbox("map texturebugs##pf", &g_psFinderMapTb);
                ImGui::Checkbox("show map lines##pm", &g_psMapLines);
                // skybox seams sit thousands of units out: cap how far map marks draw (0 = all)
                SliderF("max visible distance##pm", &g_psMapMaxDist, 0.f, 8192.f,
                        g_psMapMaxDist <= 0.f ? "no limit" : "%.0f");
                if (ms.busy || ms.lines > 0 || ms.tb > 0)
                {
                    char pmInfo[96];
                    snprintf(pmInfo, sizeof(pmInfo), "seam %d / %d   %d surf%s   %d texturebug%s",
                             ms.done, ms.total, ms.lines, ms.lines == 1 ? "" : "s",
                             ms.tb, ms.tb == 1 ? "" : "s");
                    ImGui::TextUnformatted(pmInfo);
                }
            }, ImVec2(260.f, -1));

        // botox :938-954, 1:1. show marker / show answer panel are CSS's own split of botox's
        // single "show point" toggle, kept extra.
        ImGui::Checkbox("pixel calculator", &g_pixelCalc);
        if (g_pixelCalc)
            ImGui::OptionPopup("pixel calculator settings", [&]() {
                ImGui::Label("aim key");
                BindKeyNoMode("pixel calculator place key", &g_pcPlaceKey);
                ImGui::Label("solve key");
                BindKeyNoMode("pixel calculator solve key", &g_pcSolveKey);
                ImGui::Checkbox("show point", &g_pcShowMarker);   // botox m_pixel_calc_show_point
                ImGui::Checkbox("show answer panel", &g_pcShowPanel);
                ImGui::Combo("type##pixel calculator type", &g_pcType, "pixelsurf\0pixeljump\0");
                ImGui::Checkbox("advanced readout##pixel calculator", &g_pcAdvanced);
                ImGui::Label("allowed jumps");
                RcMoveMaskCombo("##pixel calculator jump types", g_rcMoveMask);
                // "building calculator" removed: no buildings in CSS (TF2 sentry/dispenser/teleporter only)
            }, ImVec2(260.f, -1));

        // dist calc (movement.h g_distCalc*, DistCalc_Render): two-click distance tool off an
        // aim trace. botox :956-965 wraps it in its own popup.
        ImGui::Checkbox("distance calculator", &g_distCalc);
        if (g_distCalc)
            ImGui::OptionPopup("distance calculator settings", [&]() {
                ImGui::Label("point key");
                ImGui::Keybind("distance calculator key", &g_distCalcKey, &g_distCalcMode);
            }, ImVec2(220.f, -1));

        // check floor level (movement.h g_floorLevel*, FloorLevel_Render/FloorLevel_World): botox
        // :925-945 -- toggle key + button clears/starts the scan, size slider is the grid radius.
        ImGui::Checkbox("check floor level", &g_floorLevel);
        if (g_floorLevel)
            ImGui::OptionPopup("check floor level settings", [&]() {
                ImGui::Label("toggle key");
                ImGui::Keybind("check floor level key", &g_floorLevelKey, &g_floorLevelMode);
                if (ImGui::Button(FloorLevelShown() ? "clear floor level" : "check floor level##button", ImVec2(-1.f, 15.f)))
                    FloorLevelToggle();
                ImGui::SliderFloat("size##floorlevel", &g_floorLevelSize, 16.f, 512.f, "%.0f u");
            }, ImVec2(220.f, -1));

        // botox :967-979 -- wraps the checkpoint/teleport binds + adds "show window"
        ImGui::Checkbox("practice window", &g_practiceWindow);
        if (g_practiceWindow)
            ImGui::OptionPopup("practice window settings", [&]() {
                ImGui::Label("practice checkpoint key");
                BindKeyNoMode("practice cp key", &g_prSaveKey);
                ImGui::Label("practice teleport key");
                BindKeyNoMode("practice tp key", &g_prTpKey);
                ImGui::Checkbox("show window", &g_practiceWindowShow);
            }, ImVec2(220.f, -1));

        // botox :981-1003
        ImGui::Checkbox("fake pov in demos", &g_fakePov);
        if (g_fakePov)
            ImGui::OptionPopup("fake pov settings", [&]() {
                ImGui::Label("fake pov keybind");
                ImGui::Keybind("fake pov key", &g_fakePovKey, &g_fakePovKeyMode);
                ImGui::Combo("angle##fake pov", &g_fakePovMode, "right\0left\0up\0bottom\0backwards\0spinning\0");
                if (g_fakePovMode == 5)
                    SliderF("spin speed##fake pov", &g_fakePovSpinSpeed, 10.f, 540.f, "%.0f deg/s");
                SliderF("smoothing##fake pov", &g_fakePovSmooth, 0.5f, 20.f, "%.1f");
                ImGui::Checkbox("snap view on disable##fake pov", &g_fakePovSnapView);

                ImGui::Checkbox("indicator arrow##fake pov", &g_fakePovArrow);
                if (g_fakePovArrow)
                {
                    ImGui::ColorEdit4("arrow color##fake pov", g_fakePovArrowColor,
                        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                        ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                        ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder);
                    SliderF("arrow size##fake pov", &g_fakePovArrowSize, 4.f, 64.f, "%.0f");
                    SliderF("arrow distance##fake pov", &g_fakePovArrowDist, 0.f, 400.f, "%.0f");
                }
            }, ImVec2(220.f, -1));

        UnitA_Menu_Jumpstats();
    }
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementIndicators()
{
    // botox menu_tab_movement.cpp case 1: "indicators" | "detections"
    menu_columns_begin();

    if (menu_group_begin("indicators"))
    {
        // UNIT_MENU_SLOT movement_indicators
        UnitB_Menu_movement_indicators();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("detections"))
        UnitB_Menu_movement_detections();
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementCalculator()
{
    // botox menu_tab_movement.cpp route_calc_page 1:1: "route calculator" + "keys" | "points".
    // TF2 has no ducked ground jump (tf_gamemovement.cpp:1220 "Cannot jump while ducked"), so the
    // point list has no "jump (ducked)" row; headbounce is not modelled. pixeljump = botox's lip
    // (routesim PT_PIXELJUMP), placed on a yellow finder dot
    menu_columns_begin();

    if (menu_group_begin("route calculator"))
    {
        ImGui::Checkbox("route calculator", &g_routeCalc);

        if (ImGui::Checkbox("use current pos for start jump", &g_rcStartHere))
            RC_DropAnswer();
        if (ImGui::Checkbox("allow selecting invalid pixels", &g_rcAllowInvalid))
            RC_DropAnswer();

        ImGui::Checkbox("snap points to nearest edge", &g_rcSnapEdge);
        ImGui::Checkbox("show key list", &g_rcShowKeys);
        ImGui::Checkbox("show path", &g_rcShowPath);
        if (g_rcShowPath)
        {
            SliderI("path alpha", &g_rcPathAlpha, 0, 255, "%d");
            SliderF("path thickness", &g_rcPathThick, 1.f, 8.f, "%.1f px");
        }
        ImGui::Checkbox("show combos on screen", &g_rcShowPanel);
        ImGui::Checkbox("advanced readout##route calc", &g_rcAdvanced);

        SliderI("popup duration", &g_rcPopupTime, 1, 30, "%d s");
        SliderI("max displayed combos", &g_rcMaxShown, 1, 64, "%d combos");

        if (SliderI("delay hop ticks", &g_rcDelayTicks, 0, 8, "%d ticks"))
            RC_DropAnswer();

        ImGui::Label("jump types to calculate");
        if (RcMoveMaskCombo("##global jump types", g_rcMoveMask))
            RC_DropAnswer();
    }
    menu_group_end();

    if (menu_group_begin("keys"))
    {
        ImGui::Label("add point");
        BindKeyNoMode("route calc add key", &g_rcAddKey);
        ImGui::Label("calculate combos");
        BindKeyNoMode("route calc solve key", &g_rcSolveKey);
        ImGui::Label("delete point");
        BindKeyNoMode("route calc delete key", &g_rcDeleteKey);
        ImGui::Label("clear all points");
        BindKeyNoMode("route calc clear key", &g_rcClearKey);
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("points"))
    {
        // row 0 = the starting jump, row i = marker i (botox route.selected_row)
        static int s_rcRow = 0;
        const int nPts = RC_PointCount();
        if (ImGui::BeginListBox("##route points", ImVec2(-1.f, 160.f)))
        {
            if (ImGui::Selectable(g_rcStartHere ? "starting jump (current position)" : "starting jump (point 1)", s_rcRow == 0))
                s_rcRow = 0;
            for (int i = 0; i < nPts; i++)
            {
                RcPointInfo pt;
                if (!RC_PointGet(i, pt)) break;
                char row[96];
                snprintf(row, sizeof(row), "%d. %s   z %d%s##rcrow%d", i + 1, RC_TypeName(pt.type), (int)pt.pos.z,
                         pt.enabled ? "" : "   (off)", i);
                if (ImGui::Selectable(row, s_rcRow == i + 1))
                    s_rcRow = i + 1;
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button("clear all points", ImVec2(-1.f, 15.f)))
            RC_PointsClear();

        if (s_rcRow > RC_PointCount())
            s_rcRow = 0;

        if (s_rcRow == 0)
        {
            ImGui::Label("types to calculate");
            if (RcMoveMaskCombo("##types to calculate", g_rcStartMoves, RC_StartMoves()))
                RC_DropAnswer();
        }
        else
        {
            const int row = s_rcRow - 1;
            RcPointInfo edit;
            if (RC_PointGet(row, edit))
            {
                if (ImGui::Checkbox("point enabled", &edit.enabled))
                    RC_PointSet(row, edit);

                const int n = RC_PointCount();
                if (ImGui::Button("move up", ImVec2(-1.f, 15.f)) && row > 0)
                {
                    RC_PointMove(row, -1);
                    s_rcRow -= 1;
                }
                else if (ImGui::Button("move down", ImVec2(-1.f, 15.f)) && row + 1 < n)
                {
                    RC_PointMove(row, 1);
                    s_rcRow += 1;
                }
                else if (ImGui::Button("delete point", ImVec2(-1.f, 15.f)))
                {
                    RC_PointDelete(row);
                    if (s_rcRow > RC_PointCount()) s_rcRow = RC_PointCount();
                }
                else if (RC_PointGet(row, edit))
                {
                    bool changed = false;
                    // order = rs::PointType, so the combo index IS the type. RC_PointSet refuses
                    // pixelsurf / texturebug / pixeljump / headbounce on a marker no finder pinned
                    // (botox: "a pixelsurf point must be placed on a finder dot"; headbounce needs
                    // its own ascent pin the same way), unless invalid pixels are allowed
                    static const char* kTypeItems =
                        "ground\0pixelsurf\0edgebug\0headbang\0texturebug\0pixeljump\0headbounce\0";
                    const int was = edit.type;
                    if (ImGui::Combo("point type", &edit.type, kTypeItems) && edit.type != was)
                        changed = true;
                    if (!edit.snapped && !g_rcAllowInvalid)
                        ImGui::TextDisabled("pixelsurf / texturebug / pixeljump / headbounce: place it on a finder dot");
                    // botox has no "ceiling" row: pf_hb_is_ceiling is never asked, it is read off
                    // the finder's own scan (here, RcAddPoint's RcHeadbounceCeiling, at placement)
                    if (edit.type == 6 && edit.snapped)
                        ImGui::TextDisabled(edit.hbCeiling ? "ceiling face" : "seam window face");
                    // botox has no menu row for pj_skin either (route_calc.cpp classifies it the
                    // instant the marker is placed, RcAddPoint's RcPixelJumpSkin)
                    if (edit.type == 5 && edit.snapped && edit.pjSkin)
                        ImGui::TextDisabled("flush skin lip (catches higher than the pin)");

                    ImGui::Label("types to calculate");
                    // a pixel jump is PRESSED off like a floor, so it keeps its list (botox route_calc)
                    if (edit.type == 0 || edit.type == 5)
                    {
                        int mask = (int)edit.moves;
                        if (RcMoveMaskCombo("##types to calculate", mask, RC_MarkerMoves()))
                        {
                            edit.moves = (unsigned)mask;
                            changed = true;
                        }
                    }
                    else
                    {
                        changed |= ImGui::Checkbox("arrive standing", &edit.stand);
                        changed |= ImGui::Checkbox("arrive ducked", &edit.duck);
                    }
                    if (changed) RC_PointSet(row, edit);
                }
            }
        }
    }
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementRecorder()
{
    // UNIT_MENU_SLOT movement_recorder -- botox case 3: the unit fn owns its columns + groups
    UnitB_Menu_movement_recorder();
}

// botox menu_tab_movement.cpp points_page 1:1 ("lumi points page: global look | per kind design,
// preview right"). Vars cfg/u_points.h, look movement/points_style.h, drawn by render.cpp
static void Page_MovementPoints()
{
    constexpr ImGuiColorEditFlags alphaFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
        ImGuiColorEditFlags_NoBorder;   // botox color_picker_alpha_flags
    static int selected = PS_STYLE_PS;

    menu_columns_begin();

    if (menu_group_begin("points"))
    {
        ImGui::Checkbox("render ps assist points", &g_pointsRenderPs);
        ImGui::Checkbox("render calculator points", &g_pointsRenderCalc);
        ImGui::Checkbox("render finder points", &g_pointsRenderFinder);
        ImGui::SliderFloat("points opacity", &g_pointsOpacity, 0.1f, 1.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("max render distance", &g_pointsMaxDist, 200.f, 4000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("distance fade", &g_pointsDistanceFade);
        ImGui::Checkbox("distance shrink", &g_pointsDistanceShrink);
        ImGui::Checkbox("radius color", &g_pointsRadiusColor);
        if (g_pointsRadiusColor)
        {
            ImGui::SameLine();
            ImGui::ColorEdit4("##points radius color", g_pointsRadiusClr, alphaFlags);
        }
        ImGui::Checkbox("delete points", &g_pointsDelete);
        if (g_pointsDelete)
            BindKeyNoMode("delete points key", &g_pointsDeleteKey);
    }
    menu_group_end();

    if (menu_group_begin("appearance"))
    {
        ImGui::SliderFloat("point size", &g_pointsSize, 0.5f, 3.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("outline thickness", &g_pointsThickness, 0.5f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("background opacity", &g_pointsFill, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("hover size", &g_pointsHoverScale, 1.f, 2.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("design size", &g_pointsDesignSize, 0.2f, 1.2f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("glow", &g_pointsGlow);
        ImGui::Checkbox("pulse in radius", &g_pointsPulse);
        ImGui::Checkbox("point labels", &g_pointsLabels);
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("point design"))
    {
        // bounce / pixeljump assist styles: features removed 11-08, slots kept so cfg indices stay put
        static const char* names[PS_STYLE_COUNT] = {};
        static int ids[PS_STYLE_COUNT] = {}, n = 0;
        if (!n)
            for (int s = 0; s < PS_STYLE_COUNT; ++s)
                if (s != PS_STYLE_HB && s != PS_STYLE_PJ) { ids[n] = s; names[n++] = PointStyleGet(s).name; }
        int row = 0;
        for (int i = 0; i < n; ++i) if (ids[i] == selected) row = i;
        ImGui::Combo("point type", &row, names, n);
        selected = ids[row];
        const PointStyle& style = PointStyleGet(selected);

        // assist + calculator points follow the accent
        if (style.color)
            ImGui::ColorEdit4("point color", style.color, alphaFlags);

        if (style.gaps)
        {
            ImGui::Combo("outline shape", style.shape, kPointShapeNames, kPointShapeCount);
            static const char* kGaps[] = { "top", "bottom", "left", "right" };
            ImGui::MultiCombo("outline gaps", style.gaps, kGaps, 4);
        }

        if (style.design)
        {
            ImGui::Checkbox("draw design", style.design);
            if (*style.design)
                ImGui::Combo("design type", style.designType, kPointDesignNames, kPointDesignCount);
        }
    }
    menu_group_end();

    if (menu_group_begin("preview"))
    {
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 220.f));
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        const float cx = (lo.x + hi.x) * 0.5f, cy = (lo.y + hi.y) * 0.5f - 8.f;
        const float r = std::clamp((std::min)(hi.x - lo.x, hi.y - lo.y) * 0.15f * g_pointsSize, 2.5f, (hi.y - lo.y) * 0.35f);
        PointStylePreview(selected, cx, cy, r);
        const char* label = PointStyleGet(selected).name;
        const ImVec2 textS = ImGui::CalcTextSize(label);
        ImGui::GetWindowDrawList()->AddText(ImVec2(std::floor(cx - textS.x * 0.5f), cy + r + 10.f), ImGui::GetColorU32(ImGuiCol_Text), label);
    }
    menu_group_end();

    menu_columns_end();
}

const char* kMoveSub[5] = { "main", "indicators", "calculator", "recorder", "points" };
int s_moveSub = 0;
void Tab_MovementRoot()
{
    if      (s_moveSub == 0) Page_MovementMain();
    else if (s_moveSub == 1) Page_MovementIndicators();
    else if (s_moveSub == 2) Page_MovementCalculator();
    else if (s_moveSub == 3) Page_MovementRecorder();
    else                     Page_MovementPoints();
}
