#include "menu_internal.h"

static void CcCurveEditor()
{
    static int s_chan = 0;      // 0 = master (rgb), 1/2/3 = R/G/B
    static int s_drag = -1;

    static const char* k_names[4]  = { "rgb", "r", "g", "b" };
    static const ImU32 k_bright[4] = { IM_COL32(235,235,235,240), IM_COL32(235, 95, 95,240),
                                       IM_COL32( 95,215,110,240), IM_COL32(100,150,255,240) };
    static const ImU32 k_dim[4]    = { IM_COL32(235,235,235, 60), IM_COL32(235, 95, 95, 60),
                                       IM_COL32( 95,215,110, 60), IM_COL32(100,150,255, 60) };

    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float gap   = ImGui::GetStyle().ItemSpacing.x;
        const float bw    = (avail - gap * 3.f) / 4.f;
        for (int ch = 0; ch < 4; ++ch)
        {
            if (ch) ImGui::SameLine(0.f, gap);
            const bool on = (s_chan == ch);
            ImGui::PushID(ch);
            if (on)
            {
                const ImVec4 c = ImGui::ColorConvertU32ToFloat4(k_bright[ch]);
                ImGui::PushStyleColor(ImGuiCol_Button,        c);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.10f, 0.10f, 1.f));
            }
            if (ImGui::Button(k_names[ch], ImVec2(bw, 0.f))) { s_chan = ch; s_drag = -1; }
            if (on) ImGui::PopStyleColor(4);
            ImGui::PopID();
        }
    }

    CcCurve& cv = g_ccCurve[s_chan];

    const float  w  = ImGui::GetContentRegionAvail().x;
    const float  h  = (w < 220.f) ? w : 220.f;      // square until it would eat the tab
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + w, p0.y + h);

    ImGui::InvisibleButton("##ccgraph", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();

    auto ToScreen = [&](float x, float y) { return ImVec2(p0.x + x * w, p0.y + (1.f - y) * h); };

    const ImVec2 mp = ImGui::GetIO().MousePos;
    const float  mx = (mp.x - p0.x) / w;
    const float  my = 1.f - (mp.y - p0.y) / h;

    // Nearest control point to the cursor, in pixels. One hit test drives grab, add and
    // remove, so they can never disagree about which point is under the mouse.
    int   hit  = -1;
    float hitD = 1e9f;
    for (int i = 0; i < cv.n; ++i)
    {
        const ImVec2 s = ToScreen(cv.x[i], cv.y[i]);
        const float  d = hypotf(mp.x - s.x, mp.y - s.y);
        if (d < hitD) { hitD = d; hit = i; }
    }
    const bool onPoint = (hov && hitD <= 11.f);

    if (act && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        s_drag = onPoint ? hit : CcAddPoint(cv, mx, my);
    if (s_drag >= 0 && s_drag < cv.n && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        CcMovePoint(cv, s_drag, mx, my);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = -1;
    if (onPoint && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        CcRemovePoint(cv, hit);
        s_drag = -1;
        hit    = -1;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 18, 235), 3.f);
    for (int i = 1; i < 4; ++i)
    {
        const float gx = p0.x + w * i * 0.25f;
        const float gy = p0.y + h * i * 0.25f;
        dl->AddLine(ImVec2(gx, p0.y), ImVec2(gx, p1.y), IM_COL32(255, 255, 255, 16));
        dl->AddLine(ImVec2(p0.x, gy), ImVec2(p1.x, gy), IM_COL32(255, 255, 255, 16));
    }
    dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), IM_COL32(255, 255, 255, 30));
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), 3.f);

    // Inactive channels first so the one being edited is never drawn under another. A
    // channel still on the diagonal is skipped: three identity lines lying on the
    // reference diagonal is noise, not information.
    const int kSeg = 96;
    ImVec2 pts[kSeg + 1];
    for (int pass = 0; pass < 2; ++pass)
        for (int ch = 0; ch < 4; ++ch)
        {
            const bool on = (ch == s_chan);
            if (on != (pass == 1)) continue;
            if (!on && CcCurveIsIdentity(g_ccCurve[ch])) continue;
            for (int i = 0; i <= kSeg; ++i)
            {
                const float x = (float)i / (float)kSeg;
                pts[i] = ToScreen(x, CcEval(g_ccCurve[ch], x));
            }
            dl->AddPolyline(pts, kSeg + 1, on ? k_bright[ch] : k_dim[ch],
                            ImDrawFlags_None, on ? 1.8f : 1.f);
        }

    for (int i = 0; i < cv.n; ++i)
    {
        const ImVec2 s   = ToScreen(cv.x[i], cv.y[i]);
        const bool   lit = (s_drag == i) || (onPoint && hit == i);
        const float  r   = lit ? 5.0f : 3.8f;
        dl->AddCircleFilled(s, r, lit ? IM_COL32(255, 255, 255, 255) : k_bright[s_chan]);
        dl->AddCircle(s, r, IM_COL32(15, 15, 15, 210), 0, 1.2f);
    }

    // Readout in 8-bit codes, because that is the unit the curve actually lands in.
    if (s_drag >= 0 && s_drag < cv.n)
        ImGui::Text("%s   in %3d   out %3d", k_names[s_chan],
                    (int)(cv.x[s_drag] * 255.f + 0.5f), (int)(cv.y[s_drag] * 255.f + 0.5f));
    else if (hov)
        ImGui::TextDisabled("left add / drag, right remove");
    else
        ImGui::TextDisabled("%s   %d points", k_names[s_chan], cv.n);

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float bw2 = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    if (ImGui::Button("reset curve", ImVec2(bw2, 0.f))) CcResetCurve(cv);
    ImGui::SameLine(0.f, gap);
    if (ImGui::Button("reset all curves", ImVec2(bw2, 0.f)))
        for (int i = 0; i < 4; ++i) CcResetCurve(g_ccCurve[i]);
}

// ==== tab bodies ====================================================================
// Was three side-by-side columns in one window. Same widgets, same order inside a
// section -- they just live in a Tab_* function now and only one runs per frame.
// PushItemWidth(-1) is done ONCE around the switch in RenderMenu, so nothing here
// pushes or pops it.

static void Page_VisualsEsp()
{
    menu_columns_begin();
    if (menu_group_begin("players"))
    {
        // UNIT_MENU_SLOT visuals_esp
        UnitC_Menu_visuals_esp();
    }
    menu_group_end();

    // botox "edicts" group under "players" (dropped weapons, thrown projectiles, grenade path + TF2 buildings)
    if (menu_group_begin("edicts"))
        UnitC_Menu_visuals_world();
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("chams"))
        UnitC_Menu_visuals_chams();
    menu_group_end();
    menu_columns_end();
}

// Skybox name combo, shared by the botox-named "sky" row below. Builtins first, then
// whatever the user last typed (custom), so the combo never loses the live value.
static void SkyboxCombo()
{
    // CSS + HL2 (mounted in CSS) skybox base names (materials/skybox/<name><bk|dn|ft|lf|rt|up>.vmt),
    // parsed from the live cstrike_pak_dir.vpk + hl2 vpks, 11-03 (coordinator). Replaces the
    // TF2 sky_tf2_04 etc list.
    static const char* kSkyList[] = {
        "assault", "cx", "de_cobble", "de_piranesi", "hav", "italy", "jungle", "militia_hdr",
        "office", "sky_c17_05", "sky_dust", "tides", "train",
        "sky_borealis01", "sky_day01_01", "sky_day01_04", "sky_day01_05", "sky_day01_06",
        "sky_day01_07", "sky_day01_08", "sky_day01_09", "sky_day02_01", "sky_day02_02",
        "sky_day02_03", "sky_day02_04", "sky_day02_05", "sky_day02_06", "sky_day02_07",
        "sky_day02_09", "sky_day02_10", "sky_day03_01", "sky_day03_02", "sky_day03_03",
        "sky_day03_04", "sky_day03_05", "sky_day03_06", "sky_day03_06b", "sky_wasteland02",
    };
    constexpr int kSkyCount = (int)(sizeof(kSkyList) / sizeof(kSkyList[0]));

    // -1 = custom; -2 = unsynced (match g_skyName once)
    static int s_skySel = -2;
    if (s_skySel == -2)
    {
        s_skySel = -1;
        for (int i = 0; i < kSkyCount; ++i)
            if (strcmp(kSkyList[i], g_skyName) == 0) { s_skySel = i; break; }
    }

    // botox k_skyboxes[0] = "off". g_skyEnable is what h_R_DrawSkyBox gates on -- nothing set it
    // before, so picking a sky did nothing.
    const char* preview = !g_skyEnable ? "off" : (s_skySel >= 0) ? kSkyList[s_skySel] : "custom";
    if (ImGui::BeginCombo("skybox", preview))
    {
        if (ImGui::Selectable("off", !g_skyEnable))
            g_skyEnable = false;
        for (int i = 0; i < kSkyCount; ++i)
            if (ImGui::Selectable(kSkyList[i], g_skyEnable && s_skySel == i))
            {
                s_skySel = i;
                g_skyEnable = true;
                strncpy(g_skyName, kSkyList[i], sizeof(g_skyName) - 1);
                g_skyName[sizeof(g_skyName) - 1] = '\0';
            }
        if (ImGui::Selectable("custom", g_skyEnable && s_skySel == -1))
        {
            s_skySel = -1;
            g_skyEnable = true;
        }
        ImGui::EndCombo();
    }
    if (g_skyEnable && s_skySel == -1)
        ImGui::InputText("##skyname", g_skyName, sizeof(g_skyName));
}

static void Page_VisualsWorld()
{
    menu_columns_begin();
    if (menu_group_begin("world"))
    {
        // botox menu_tab_visuals.cpp world group, verbatim order / labels / ranges. TF2 cuts are
        // listed in unitD_menu.cpp (UnitD_Menu_visuals_world, which carries the rows from dlight on).
        ImGui::Checkbox("world fov", &g_worldFovEnable);
        if (g_worldFovEnable)
            ImGui::SliderFloat("fov##world", &g_wdWorldFov, -30.f, 60.f, "%.1f");

        ImGui::Checkbox("precipitation", &g_weatherEnable);
        if (g_weatherEnable)
            ImGui::OptionPopup("precipitation settings", [&]() {   // ours: TF2's r_rain* knobs
                if (g_weatherMode != 2)
                {
                    // Floors are NOT taste: speed 0 divides by zero in ComputeEmissionArea,
                    // length 0 collapses the tracer quad, radius 0 is an empty emission box.
                    SliderF("alpha##rain", &g_rainAlpha, 0.05f, 1.f, "%.2f");
                    SliderF("speed##rain", &g_rainSpeed, 50.f, 2000.f, "%.0f");
                    SliderF("width##rain", &g_rainWidth, 0.05f, 5.f, "%.2f");
                    SliderF("length##rain", &g_rainLength, 0.01f, 1.f, "%.2f");
                    SliderF("radius##rain", &g_rainRadius, 256.f, 5000.f, "%.0f");
                    SliderF("side vel##rain", &g_rainSideVel, 0.f, 500.f, "%.0f");
                }
                else
                {
                    SliderF("alpha##snow", &g_snowAlpha, 0.05f, 1.f, "%.2f");
                    SliderF("speed##snow", &g_snowSpeed, 20.f, 500.f, "%.0f");
                    SliderF("size##snow", &g_snowWidth, 0.05f, 10.f, "%.1f");
                    // A flake is still a tracer quad -- r_rainlength 0 draws literally nothing.
                    SliderF("trail##snow", &g_snowLength, 0.01f, 1.f, "%.2f");
                    SliderF("radius##snow", &g_snowRadius, 256.f, 5000.f, "%.0f");
                    SliderF("drift##snow", &g_snowSideVel, 0.f, 100.f, "%.0f");
                    SliderF("density##snow", &g_snowDensity, 0.05f, 1.f, "%.2f");
                }
            }, ImVec2(220.f, -1));
        if (g_weatherEnable)
        {
            // botox "rain\0ash\0rain storm\0snow\0": TF2's C_Precipitation only spawns ash where a
            // trace hits the func_precipitation brush (c_effects.cpp:886 UTIL_TraceModel), which the
            // entity this DLL injects has none of, and has no storm type (precipitation_shared.h).
            // combo index 0/1 = g_weatherMode 1/2 (0 is "off").
            int type = (g_weatherMode == 2) ? 1 : 0;
            if (ImGui::Combo("type##precipitation", &type, "rain\0" "snow\0"))
                g_weatherMode = type + 1;
        }

        SkyboxCombo();

        ImGui::Checkbox("fog", &g_fogEnable);
        if (g_fogEnable)
            ImGui::OptionPopup("fog settings", [&]() {
                ImGui::ColorEdit4("color##fog color picker", g_fogColor);
                ImGui::SliderFloat("start##fog", &g_fogStart, -5000.f, 5000.f, "%.0f u");
                ImGui::SliderFloat("end##fog", &g_fogEnd, -5000.f, 5000.f, "%.0f u");
                float pct = g_fogDensity * 100.f;   // ours is 0..1 (fog_maxdensity), botox shows %
                if (ImGui::SliderFloat("density##fog", &pct, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
                    g_fogDensity = pct * 0.01f;
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("fullbright", &g_fullbrightEnable);

        ImGui::Checkbox("flip world", &g_flipWorld);
        if (g_flipWorld)
            ImGui::Checkbox("gun other side##flip world", &g_wdFlipGunOtherSide);

        // UNIT_MENU_SLOT visuals_world: dlight .. world hit marker, then minecraft sky
        UnitD_Menu_visuals_world();
    }
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("removals"))
    {
        // botox safe_checkbox: greyed + shown off under safe mode (world_fx.cpp skips it too)
        {
            bool off = false;
            ImGui::BeginDisabled(g_safeMode);
            ImGui::Checkbox("disable post processing", g_safeMode ? &off : &g_sfxDisablePostProcessing);
            ImGui::EndDisabled();
            if (g_safeMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("safe mode enabled");
        }
        // botox order (remove panorama blur / smoke / flash: no panorama, smokes or flashbangs in TF2)
        ImGui::Checkbox("remove 3d skybox", &g_wdNo3dSky);

        ImGui::Checkbox("remove hud elements", &g_wdNoHud);
        if (g_wdNoHud)
        {
            // TF2 vgui panels (world_fx3_math.h k_hudElems): botox's radar and c4 icon have none
            const char* items[wfx3::k_hudElemCount];
            int mask = 0;
            for (int i = 0; i < wfx3::k_hudElemCount; ++i)
            {
                items[i] = wfx3::k_hudElems[i].label;
                if (g_wdNoHudElems[i]) mask |= 1 << i;
            }
            if (ImGui::MultiCombo("elements##remove hud", &mask, items, wfx3::k_hudElemCount))
                for (int i = 0; i < wfx3::k_hudElemCount; ++i) g_wdNoHudElems[i] = (mask >> i) & 1;
        }

        ImGui::Checkbox("remove ragdolls", &g_removeRagdolls);

        // botox :1052-1072, CSS-real (css-community-master weapon_smokegrenade.cpp / CCSPlayer
        // m_flFlashDuration+m_flFlashMaxAlpha), via world_fx2.cpp's FindMaterial hook -- same
        // MATERIAL_VAR_NO_DRAW / ColorModulate slots cssourcex64/dllmain.cpp:45-55 verifies live.
        ImGui::Checkbox("remove smoke", &g_removeSmokeEnable);
        if (g_removeSmokeEnable)
            ImGui::OptionPopup("remove smoke settings", []() {
                ImGui::Combo("smoke##remove smoke mode", &g_removeSmokeMode, "wireframe\0disabled\0");
                if (ImGui::IsItemHovered())
                    MenuTooltip("no verified wireframe material-var toggle on this engine branch --\n"
                                "both options are NO_DRAW (fully hidden) until one turns up.");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("remove flash", &g_removeFlashEnable);
        if (g_removeFlashEnable)
            ImGui::OptionPopup("remove flash settings", []() {
                ImGui::SliderFloat("opacity##remove flash", &g_removeFlashOpacity, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("remove visual recoil", &g_removeVisualRecoilEnable);
        ImGui::Checkbox("disable interp", &g_disableInterpEnable);

        // "no hats" / "no unusual effects" removed: TF2-only cosmetics, CSS has neither
        ImGui::Checkbox("remove prop fade", &g_removePropFade);
        ImGui::Checkbox("fps boost", &g_fpsBoost);
        ImGui::Checkbox("remove screen effects", &g_removeScreenEffects);
    }
    menu_group_end();

    if (menu_group_begin("viewmodel"))
    {
        // botox order + labels.
        // bob changer / jump lower / landing dip / shift: no hook, vm_csgo.h math applied as an eye
        // nudge in h_CalcViewModelView (vm_csgo_hook.inl VmCs_EyeAdjustSafe, timing = g_globalVars).
        // bob changer also zeroes CSS's own bob (cl_bobup 0, visuals.cpp) so the two don't stack.
        ImGui::Checkbox("viewmodel offset", &g_vmOffsetEnable);
        if (g_vmOffsetEnable)
            ImGui::OptionPopup("viewmodel offset settings", [&]() {
                ImGui::SliderFloat("x##viewmodel", &g_vmOffsetX, -30.f, 30.f, "%.2f");
                ImGui::SliderFloat("y##viewmodel", &g_vmOffsetY, -30.f, 30.f, "%.2f");
                ImGui::SliderFloat("z##viewmodel", &g_vmOffsetZ, -30.f, 30.f, "%.2f");
                ImGui::SliderFloat("roll##viewmodel", &g_wdVmRoll, -180.f, 180.f, "%.1f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("viewmodel fov", &g_wdVmFovEnable);
        if (g_wdVmFovEnable)
            ImGui::OptionPopup("viewmodel fov settings", [&]() {
                ImGui::SliderFloat("fov##viewmodel", &g_wdVmFov, 1.f, 179.f, "%.1f");
            }, ImVec2(220.f, -1.f));

        // botox :1113-1130 "weapon sheen" -- round-3 replaced the TF2 killstreak pixel-shader
        // intercept with a real $envmaptint sweep on CS:S weapon viewmodel materials
        // (world_fx2.cpp Fx2_SheenTick). Round-4: the old intercept itself (g_sheenNormal*,
        // lsd_chams.cpp DrawSheenNormal/SheenPassModel/SheenNormal_Tick, fx_sheen_ps*.hlsl) is
        // now physically deleted -- it shared lsd_chams.cpp's DIP detour with the still-active,
        // unrelated lsd_chams/two-tone chams features, so only its own branch + shaders were
        // removed, the shared hook stayed.
        ImGui::Checkbox("weapon sheen", &g_wpnSheenEnable);
        if (g_wpnSheenEnable)
            ImGui::OptionPopup("weapon sheen settings", [&]() {
                ImGui::ColorRow("color##weapon sheen color", g_wpnSheenColor);
                ImGui::SliderFloat("delay##weapon sheen", &g_wpnSheenDelay, 0.f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("width##weapon sheen", &g_wpnSheenWidth, 0.1f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("normal blend##weapon sheen", &g_wpnSheenNormalBlend);
                if (g_wpnSheenNormalBlend)
                    ImGui::SliderFloat("env strength##weapon sheen", &g_wpnSheenEnv, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("weapon sway", &g_vmSwayEnable);
        if (g_vmSwayEnable)
            ImGui::OptionPopup("weapon sway settings", [&]() {
                ImGui::SliderFloat("scale##weapon sway", &g_vmSwayScale, 0.f, 50.f, "%.2f");
                SliderF("interp##weapon sway", &g_vmSwayInterp, 0.f, 1.f, "%.2f");   // ours: cl_wpn_sway_interp
                if (ImGui::Button("csgo sway##vs", ImVec2(-1.f, 0.f)))
                {
                    g_vmSwayScale = 1.6f;    // cl_wpn_sway_scale  (CS:GO default)
                    g_vmSwayInterp = 0.1f;   // cl_wpn_sway_interp (CS:GO default)
                }
            }, ImVec2(220.f, -1.f));

        ImGui::BeginDisabled(g_vmRemoveBob);
        ImGui::Checkbox("bob changer", &g_vmCsBob);   // CS:GO bob, ported (vm_csgo.h)
        if (g_vmCsBob)
            ImGui::OptionPopup("bob changer settings", [&]() {
                ImGui::SliderFloat("cycle", &g_vmCsBobCycle, 0.f, 5.f, "%.2f");
                ImGui::SliderFloat("vertical", &g_vmCsBobVert, 0.f, 10.f, "%.2f");
                ImGui::SliderFloat("lateral", &g_vmCsBobLat, 0.f, 10.f, "%.2f");
                ImGui::SliderFloat("lower amount", &g_vmCsBobLower, 0.f, 100.f, "%.1f");

                // botox menu_tab_visuals.cpp:1142-1143 -- live weapon inaccuracy shift (round-3)
                ImGui::SliderFloat("shift left", &g_wdVmShiftLeft, 0.f, 10.f, "%.2f");
                ImGui::SliderFloat("shift right", &g_wdVmShiftRight, 0.f, 10.f, "%.2f");

                SliderF("bob up time##vcb", &g_vmCsBobUp, 0.05f, 0.95f, "%.2f");   // ours: cl_bobup
                if (ImGui::Button("csgo defaults##vcb", ImVec2(-1.f, 0.f)))
                {
                    g_vmCsBobLower = 21.f; g_vmCsBobVert = 0.25f; g_vmCsBobLat = 0.4f;
                    g_vmCsBobCycle = 0.98f; g_vmCsBobUp = 0.5f;
                }
            }, ImVec2(220.f, -1.f));
        ImGui::EndDisabled();

        // ours (TF2)
        ImGui::Checkbox("remove bob", &g_vmRemoveBob);

        ImGui::Checkbox("csgo jump lower", &g_vmCsGunLower);
        if (g_vmCsGunLower)
            ImGui::OptionPopup("csgo jump lower settings", [&]() {
                SliderF("angle##vcgl", &g_vmCsGunLowerAngle, 0.f, 20.f, "%.1f");
                SliderF("speed##vcgl", &g_vmCsGunLowerSpeed, 0.01f, 2.f, "%.2f");
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("csgo landing dip", &g_vmCsLandDip);
        if (g_vmCsLandDip)
            ImGui::OptionPopup("csgo landing dip settings", [&]() {
                SliderF("dip##vcld", &g_vmCsLandDipAmt, 0.f, 20.f, "%.0f");
                SliderF("dip time##vcld", &g_vmCsLandDipTime, 0.03f, 0.5f, "%.2fs");
            }, ImVec2(200.f, -1));

        // The cfg has been found sitting on vmAlpha=1.0000 -- the value the feature itself
        // treats as off (h_DrawModelExecute gates on < 0.999f). Ticking the box there does
        // nothing and reads as a broken feature, so an enable at the opaque end lands on a
        // visible value instead.
        const bool wasVmAlpha = g_vmAlphaEnable;
        ImGui::Checkbox("transparent", &g_vmAlphaEnable);
        if (g_vmAlphaEnable && !wasVmAlpha && g_vmAlpha >= 0.999f)
            g_vmAlpha = 0.5f;
        if (g_vmAlphaEnable)
            ImGui::OptionPopup("transparent settings", [&]() {
                SliderF("alpha##vm", &g_vmAlpha, 0.f, 1.f, "%.2f");
                ImGui::Checkbox("flat alpha##vm", &g_vmFlatAlpha);
            }, ImVec2(200.f, -1));
    }
    menu_group_end();
    menu_columns_end();
}

static void Page_VisualsScreen()
{
    menu_columns_begin();
    if (menu_group_begin("screen"))
    {
        // UNIT_MENU_SLOT visuals_screen (third person, aspect ratio)
        UnitD_Menu_visuals_screen();

        // botox menu_tab_visuals.cpp 1181-1237, 1:1 (one shader pass with the screen filters,
        // colorcorrect.cpp / fx_grade.hlsl)
        ImGui::Checkbox("color correction", &g_ccEnable);
        if (g_ccEnable)
            ImGui::OptionPopup("color correction settings", [&]() {
                ImGui::SliderFloat("exposure##color correction", &g_ccExposure, -3.f, 3.f, "%+.2f ev");

                ImGui::Label("levels");
                ImGui::SliderFloat("in black##color correction", &g_ccLevInBlack, 0.f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("in white##color correction", &g_ccLevInWhite, 0.01f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("gamma##color correction", &g_ccGamma, 0.1f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("out black##color correction", &g_ccLevOutBlack, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("out white##color correction", &g_ccLevOutWhite, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

                // the remap divides by ( in white - in black ), so they can never meet
                if (g_ccLevInBlack >= g_ccLevInWhite)
                    g_ccLevInWhite = g_ccLevInBlack + 0.01f;

                CcCurveEditor();

                ImGui::Label("tone");
                ImGui::SliderFloat("highlights##color correction", &g_ccHighlights, -1.f, 1.f, "%+.2f");
                ImGui::SliderFloat("shadows##color correction", &g_ccShadows, -1.f, 1.f, "%+.2f");

                ImGui::Label("color");
                ImGui::SliderFloat("red##color correction", &g_ccGainR, 0.f, 2.f, "%.2f");
                ImGui::SliderFloat("green##color correction", &g_ccGainG, 0.f, 2.f, "%.2f");
                ImGui::SliderFloat("blue##color correction", &g_ccGainB, 0.f, 2.f, "%.2f");
                ImGui::SliderFloat("hue##color correction", &g_ccHue, -180.f, 180.f, "%.1f");
                ImGui::SliderFloat("saturation##color correction", &g_ccSat, 0.f, 3.f, "%.2f");
                ImGui::SliderFloat("contrast##color correction", &g_ccContrast, 0.5f, 1.5f, "%.3f");
                ImGui::SliderFloat("temperature##color correction", &g_ccTemp, -1.f, 1.f, "%+.2f");

                if (ImGui::Button("reset##color correction", ImVec2(-1.f, 15.f))) CcResetAll();
            }, ImVec2(260.f, -1.f));

        // UNIT_MENU_SLOT visuals_screen (sharpen .. show depth, reflections between depth distort and emphasize)
        UnitD3_Menu_visuals_screen();

        // unit post: botox menu_tab_visuals.cpp:1627-1856, same order (dof, ao, motion blur, true
        // motion blur, bloom), same labels. Our own knobs ride after botox's rows in each popup.
        const char* ScreenFx_RouteName();    // hacks/screen/screenfx.cpp -- the depth readout
        const char* ScreenFx_VerifyName();

        ImGui::Checkbox("depth of field", &g_dofEnable);
        if (g_dofEnable)
            ImGui::OptionPopup("depth of field settings", [&]() {
                ImGui::Label("focus");
                ImGui::Checkbox("auto focus##depth of field", &g_dofAutoFocus);
                if (!g_dofAutoFocus)
                    SliderF("distance##depth of field", &g_dofDistance, 16.f, 4000.f, "%.0f u");
                else
                {
                    SliderF("focus point x##depth of field", &g_dofFocusX, 0.f, 1.f, "%.2f");
                    SliderF("focus point y##depth of field", &g_dofFocusY, 0.f, 1.f, "%.2f");
                    SliderF("focus speed##depth of field", &g_dofFocusSpeed, 0.5f, 40.f, "%.1f");
                    SliderF("focus limit##depth of field", &g_dofFocusLimit, 256.f, 8192.f, "%.0f u");
                    // glass, grates, debris don't steal focus
                    ImGui::Checkbox("see through glass##depth of field", &g_dofSeeThrough);
                }

                ImGui::Label("range");
                SliderF("sharp range##depth of field", &g_dofRange, 0.f, 3000.f, "%.0f u");
                SliderF("blur ramp##depth of field", &g_dofRamp, 1.f, 4000.f, "%.0f u");

                ImGui::Label("blur");
                SliderF("max blur##depth of field", &g_dofMaxBlur, 0.f, 60.f, "%.1f px");
                SliderF("highlights##depth of field", &g_dofBokehBoost, 0.f, 16.f, "%.1f");
                SliderF("highlight cutoff##depth of field", &g_dofBokehThreshold, 0.f, 1.f, "%.2f");
                ImGui::Combo("quality##depth of field", &g_dofQuality, "16 taps\0" "32 taps\0" "64 taps\0" "128 taps\0");
                ImGui::Checkbox("smooth blur##depth of field", &g_dofSmooth);
                ImGui::Checkbox("blur near side##depth of field", &g_dofNearBlur);
                if (g_dofNearBlur)
                    ImGui::Checkbox("show near bleed##depth of field", &g_dofDebugNear);
                ImGui::Checkbox("keep viewmodel sharp##depth of field", &g_dofViewmodelSharp);

                ImGui::Label("debug");
                ImGui::Checkbox("show depth##depth of field", &g_dofDebugDepth);
                if (g_dofDebugDepth)
                    SliderF("white at##depth of field", &g_dofDebugWhite, 100.f, 8000.f, "%.0f u");
                ImGui::Checkbox("universal depth##depth of field", &g_fxUniversalDepth);
                ImGui::TextDisabled("depth: %s %s", ScreenFx_RouteName(), ScreenFx_VerifyName());

                // ours, after botox's rows
                SliderF("tap density##dof", &g_dofTapDensity, 0.5f, 2.5f, "%.2f /px");
                SliderF("particle guard##dof", &g_fxParticleGuard, 0.f, 1.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("ambient occlusion", &g_aoEnable);
        if (g_aoEnable)
            ImGui::OptionPopup("ambient occlusion settings", [&]() {
                ImGui::Label("shading");
                SliderF("radius##ambient occlusion", &g_aoRadius, 4.f, 300.f, "%.0f u");
                SliderF("strength##ambient occlusion", &g_aoIntensity, 0.f, 4.f, "%.2f");
                SliderF("bias##ambient occlusion", &g_aoBias, 0.f, 0.8f, "%.2f");
                SliderF("blur##ambient occlusion", &g_aoBlur, 0.f, 3.f, "%.2f");

                ImGui::Label("range");
                SliderF("fade from##ambient occlusion", &g_aoFadeStart, 0.f, 8000.f, "%.0f u");
                SliderF("fade to##ambient occlusion", &g_aoFadeEnd, 0.f, 16000.f, "%.0f u");
                if (g_aoFadeEnd < g_aoFadeStart + 1.f) g_aoFadeEnd = g_aoFadeStart + 1.f;
                ImGui::Checkbox("fade under fog##ambient occlusion", &g_aoFogFade);
                // CSS HAS smoke grenades (css-community-master c_particle_smokegrenade.cpp),
                // unlike TF2 -- ported, per-pixel sphere test in fx_ao_composite.hlsl, nearest
                // active cloud published by hud_aim.cpp's CollectSmoke (game thread) via HudSnap.
                ImGui::Checkbox("fade under smoke##ambient occlusion", &g_vsAoSmokeFade);
                ImGui::Checkbox("skip viewmodel##ambient occlusion", &g_aoViewmodel);

                ImGui::Label("performance");
                SliderI("samples##ambient occlusion", &g_aoSamples, 4, 64);
                SliderF("render size##ambient occlusion", &g_aoScale, 0.25f, 1.f, "%.2f");

                ImGui::Label("debug");
                ImGui::Checkbox("show occlusion##ambient occlusion", &g_aoDebug);
            }, ImVec2(220.f, -1.f));

        // botox "motion blur" = Source's own DoImageSpaceMotionBlur, which this build drives
        // through mat_motion_blur_* (screenfx.cpp FxMotionBlurTick) rather than botox's hand
        // rolled 4-value version (csgo cut the engine's own path, so botox re-wrote it; CSS
        // kept it). "roll intensity" (botox m_motion_blur_roll_intensity) stays left out: it is
        // flMotionBlurRollIntensity in viewpostprocess.cpp:2718/2909, hardcoded to 0.3 with the
        // cvar that would read it commented out (line 2698) -- same engine file CSS runs, so
        // the row has nothing to control without patching that hardcoded float in client.dll.
        // NEEDS FROM OTHERS: a verified pattern/byte patch for flMotionBlurRollIntensity would
        // unlock this; not attempted here (no guessed offsets).
        ImGui::Checkbox("motion blur", &g_mbEnable);
        if (g_mbEnable)
            ImGui::OptionPopup("motion blur settings", [&]() {
                ImGui::Checkbox("blur forward motion", &g_mbForward);
                SliderF("blur strength", &g_mbStrength, 0.f, 10.f, "%.2f");
                SliderF("rotation intensity", &g_mbRotation, 0.f, 8.f, "%.2f");
                SliderF("falling intensity", &g_mbFalling, 0.f, 8.f, "%.2f");
                SliderF("falling min", &g_mbFallMin, 0.f, 50.f, "%.2f");
                SliderF("falling max", &g_mbFallMax, 0.f, 50.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        // our frame blending (frameblend.cpp) is botox's "true motion blur"
        ImGui::Checkbox("true motion blur", &g_fbEnable);
        if (g_fbEnable)
            ImGui::OptionPopup("true motion blur settings", [&]() {
                SliderF("shutter##true motion blur", &g_fbShutter, 0.f, 40.f, "%.1f ms");

                ImGui::Label("amount");
                SliderF("strength##true motion blur", &g_fbAmount, 0.f, 3.f, "%.2f");
                ImGui::Checkbox("blur players##true motion blur", &g_fbPlayers);
                SliderF("player trail##true motion blur", &g_fbObject, 0.f, 12.f, "%.2f");
                ImGui::Checkbox("blur viewmodel##true motion blur", &g_fbViewmodel);
                // 1 = true streak. gun only, never the world
                if (g_fbViewmodel)
                {
                    SliderF("viewmodel amount##true motion blur", &g_fbVmAmount, 0.f, 32.f, "%.2f");
                    SliderF("viewmodel sway##true motion blur", &g_fbVmSway, 0.f, 8.f, "%.2f");
                }

                ImGui::Label("quality");
                SliderI("max samples##true motion blur", &g_fbSamples, 4, 64);
                SliderF("max streak##true motion blur", &g_fbMaxStreak, 0.005f, 0.1f, "%.3f");
                // 2 = old look. higher = bright streaks stay bright
                SliderF("blur gamma##true motion blur", &g_fbGamma, 1.f, 10.f, "%.2f");
                ImGui::Checkbox("universal depth##true motion blur", &g_fxUniversalDepth);
                ImGui::TextDisabled("depth: %s %s", ScreenFx_RouteName(), ScreenFx_VerifyName());
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("bloom", &g_bloomEnable);
        if (g_bloomEnable)
            ImGui::OptionPopup("bloom settings", [&]() {
                SliderF("intensity##bloom", &g_bloomIntensity, 0.f, 3.f, "%.2f");
                // ours: a luma knee 0..1 (botox's is a 0..10 curve on its own bloom pyramid)
                SliderF("threshold##bloom", &g_bloomThreshold, 0.f, 1.f, "%.2f");
                SliderF("radius##bloom", &g_bloomRadius, 0.f, 1.f, "%.2f");
                SliderF("saturation##bloom", &g_bloomSat, 0.f, 3.f, "%.2f");
                SliderF("aspect ratio##bloom", &g_bloomAspect, -1.f, 1.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        // botox menu_tab_visuals.cpp:1688, same spot (screen group, after bloom)
        ImGui::Checkbox("low fps viewmodel", &g_vmLowFps);
        if (g_vmLowFps)
            ImGui::OptionPopup("low fps viewmodel settings", [&]() {
                SliderF("fps##low fps viewmodel", &g_vmLowFpsRate, 15.f, 60.f, "%.0f fps");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("lag resolver", &g_lagResolver);
        if (g_lagResolver)
            ImGui::OptionPopup("lag resolver settings", [&]() {
                SliderF("smoothing##lr", &g_lrSmoothMs, 15.f, 200.f, "%.0f ms");
                SliderF("carry##lr", &g_lrExtrapMs, 40.f, 400.f, "%.0f ms");
                SliderF("snap past##lr", &g_lrMaxErr, 8.f, 128.f, "%.0f u");
                ImGui::Checkbox("smooth turning##lr", &g_lrYaw);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("queue passes onto the render thread", &g_fxUseCallQueue);
        ImGui::Checkbox("force mat_queue_mode 0", &g_fxForceQueueMode);
        ImGui::Checkbox("depth probe", &g_fxDepthProbe);

        // resolution spoofer (hacks/screen/resolution_spoof.h) -- botox "resolution spoofer"
        // (menu_tab_visuals.cpp:1206-1256). 11-04: UI rebuilt to botox's exact widget (one
        // "resolution" dropdown, preview = current WxH, presets filtered below the real desktop
        // size) in place of the width/height sliders + separate preset combo this had before --
        // same backing vars (ResSpoof::g_width/g_height), just set only by picking a preset now,
        // matching botox (botox has no manual width/height sliders either). "smooth filter" /
        // "overlay mode" are KEPT EXTRAS (not botox rows), left after botox's own 3.
        ImGui::Checkbox("resolution spoofer", &ResSpoof::g_enable);
        if (ResSpoof::g_enable)
            ImGui::OptionPopup("resolution spoofer settings", [&]() {
                // presets filtered below the real desktop size (io.DisplaySize = engine backbuffer px)
                const ImVec2 screenSz = ImGui::GetIO().DisplaySize;
                int presetCount = 0;
                const ResSpoof::Preset* presets = ResSpoof::Presets((int)screenSz.x, (int)screenSz.y, presetCount);

                const std::string preview = std::to_string(ResSpoof::g_width) + "x" + std::to_string(ResSpoof::g_height);
                if (ImGui::BeginCombo("resolution##res spoof", preview.c_str()))
                {
                    for (int i = 0; i < presetCount; i++)
                        if (ImGui::Selectable(presets[i].label,
                                ResSpoof::g_width == presets[i].w && ResSpoof::g_height == presets[i].h))
                        {
                            ResSpoof::g_width = presets[i].w;
                            ResSpoof::g_height = presets[i].h;
                        }
                    ImGui::EndCombo();
                }

                ImGui::Checkbox("stretch to fill##res spoof", &ResSpoof::g_stretch);
                ImGui::Checkbox("scale net graph##res spoof", &ResSpoof::g_netGraph);

                ImGui::Checkbox("smooth filter##resspoof", &ResSpoof::g_smooth);
                ImGui::Checkbox("overlay mode##resspoof", &ResSpoof::g_overlayMode);
            }, ImVec2(220.f, -1.f));

        // "web on surface" (hacks/web/websurface.h) -- botox "web on surface" (menu_tab_visuals.cpp
        // :2211-2305, m_web*). 11-04: the backing logic for every row below already existed in
        // websurface.h/.cpp (unit WEB) -- only this popup was a stub.
        ImGui::Checkbox("web on surface", &g_webEnable);
        if (g_webEnable)
            ImGui::OptionPopup("web settings", [&]() {
                ImGui::TextWrapped("%s", Web_Status());

                static char s_webUrlBuf[512] = {};
                static bool s_webUrlInit = false;
                if (!s_webUrlInit) { strncpy_s(s_webUrlBuf, g_webUrl.c_str(), sizeof(s_webUrlBuf) - 1); s_webUrlInit = true; }
                const bool urlEnter = ImGui::InputTextWithHint("address##web url", "address or search",
                    s_webUrlBuf, sizeof(s_webUrlBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("go##web url", ImVec2(-1.f, 15.f)) || urlEnter)
                    { g_webUrl = s_webUrlBuf; Web_Navigate(s_webUrlBuf); }

                ImGui::Combo("shape##web", &g_webAspect, "16:9\0" "4:3\0" "1:1\0");

                if (ImGui::Button("place at crosshair##web", ImVec2(-1.f, 15.f))) Web_PlaceAtCrosshair();
                if (ImGui::Button("remove##web", ImVec2(-1.f, 15.f))) Web_Unplace();

                SliderF("size##web", &g_webScale, 0.1f, 8.f, "%.2fx");
                SliderF("opacity##web world", &g_webWorldOpacity, 0.f, 1.f, "%.2f");
                ImGui::Combo("draw##web", &g_webDepthMode, "decal\0" "under hud\0" "on top\0");
                // index order is CSS's own (websurface.cpp:94-95 defaults/switches), relabelled
                // to botox's words: 0 = draw both sides, 1 = hide back face, 2 = mirror on back
                ImGui::Combo("back side##web", &g_webBackMode, "draw both\0" "hidden\0" "mirror\0");

                SliderF("volume##web", &g_webVolume, 0.f, 1.f, "%.2f");
                ImGui::Checkbox("fade with distance##web", &g_webVolDistance);
                if (g_webVolDistance) {
                    SliderF("full volume within##web", &g_webVolNear, 0.f, 2048.f, "%.0f");
                    SliderF("silent past##web", &g_webVolFar, 16.f, 4096.f, "%.0f");
                }

                bool_combo("playback##web", { { "play when placed", &g_webAutoPlay },
                                              { "from the beginning", &g_webPlayFromStart },
                                              { "remove when video ends", &g_webAutoRemove } });

                bool_combo("controls##web", { { "crosshair is the mouse", &g_webCrosshairPointer },
                                              { "hover only on click", &g_webClickOnly },
                                              { "don't shoot at the screen", &g_webBlockFire },
                                              { "wheel / arrows scroll page", &g_webScrollInput } });

                static int unused = 0;
                ImGui::Label("place key");
                ImGui::Keybind("place key##web", &g_webPlaceKey, &unused, false);
                ImGui::Label("remove key");
                ImGui::Keybind("remove key##web", &g_webRemoveKey, &unused, false);
                ImGui::Label("click key");
                ImGui::Keybind("click key##web", &g_webClickKey, &unused, false);
                ImGui::Label("right click key");
                ImGui::Keybind("right click key##web", &g_webRightClickKey, &unused, false);

                ImGui::Label("browser window");
                ImGui::Checkbox("show##web panel", &g_webPanel);
                ImGui::Checkbox("keep with menu closed##web", &g_webPanelAlways);
                if (g_webPanel)
                    SliderF("window opacity##web", &g_webPanelOpacity, 0.05f, 1.f, "%.2f");

                if (ImGui::Button("clear cache##web", ImVec2(-1.f, 15.f))) Web_ClearCache();
                if (ImGui::Button("clear memory##web", ImVec2(-1.f, 15.f))) Web_ClearMemory();
            }, ImVec2(260.f, -1.f));
    }
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("hud"))
    {
        // ---- botox menu_tab_visuals.cpp "hud" group, botox order (unit hud, 11-01; SCREEN
        // revisited 11-04 -- chud/cod4/urt/mc huds removed 11-04; web is ported, in the
        // "screen" group above (menu_tab_visuals.cpp:1943-2305 botox source == CSS "screen"
        // group here, not "hud" -- the two groups got merged differently on this port). ----
        // "default crosshair color" / "classic crosshair color" / "classic crosshair outline
        // color" (botox m_panorama_crosshair_color / m_classic_crosshair_color(_outline)):
        // CS:GO has two crosshair skins (panorama dynamic reticle = "default", vgui bars =
        // "classic"); CSS only ever had the vgui-bar one. Already covered below by "csgo
        // crosshair settings" style 0/1 (default/default static, g_chCsgoColor) vs style
        // 2/3/4 (classic standard/dynamic/static, same popup's own color + outline rows) --
        // renamed, not missing.
        // N/A (CS:GO panorama/scaleform UI, CSS has neither): "hud color" (global panorama
        // HUD tint), "scaleform", "force hud update" (reloads the scaleform/panorama VM), "panorama
        // script text" / "context panel" / "run panorama script" (#ifdef _DEBUG in botox, a
        // raw ActionScript console -- no such VM in CSS to run it against).
        // "square radar" (botox scaleform.cpp square_radar(), forces the panorama radar's JS
        // to a square layout): CSS's radar is hud_radar.cpp, a fixed vgui panel with a round
        // background image baked into the compiled client HUD; cl_radartype exists but is
        // unused dead code in css-community-master (grep: declared hud_radar.cpp:36, read
        // nowhere). N/A this round -- would need a material override (world_fx2.cpp-style)
        // with a square radar texture asset that does not exist yet.
        ImGui::Checkbox("keep killfeed", &g_vsKeepKillfeed);
        if (ImGui::IsItemHovered())
            MenuTooltip("pins hud_deathnotice_time so kill notices never expire while this is on");


        ImGui::Checkbox("aimbot fov circle", &g_hudFovCircle);
        if (g_hudFovCircle)
            ImGui::OptionPopup("aimbot fov circle settings", [&]() {
                ImGui::ColorRow("color##aimbot fov circle", g_hudFovCircleColor);
                SliderF("thickness##aimbot fov circle", &g_hudFovCircleThick, 0.5f, 5.f, "%.1f");
                ImGui::Checkbox("only while aim key held##aimbot fov circle", &g_hudFovCircleKeyOnly);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("custom net graph", &g_hudNg);
        if (g_hudNg)
            ImGui::OptionPopup("net graph settings", [&]() {
                ImGui::TextDisabled("shown with net_graph 1");
                ImGui::TextDisabled("font: fonts > net graph");
                static const char* const kNgItems[11] = { "fps", "frame var", "ping", "up rate", "loss", "choke",
                                                          "cmd rate", "tick", "sv time", "sv var", "server type" };
                ImGui::MultiCombo("shown##net graph", &g_hudNgItems, kNgItems, 11);
                ImGui::Checkbox("lerp##net graph", &g_hudNgLerp);
                ImGui::Checkbox("in / out rows##net graph", &g_hudNgInOut);
                ImGui::ColorRow("text color##net graph", g_hudNgColor);
                ImGui::Checkbox("warning colors##net graph", &g_hudNgWarn);
                if (g_hudNgWarn)
                {
                    ImGui::ColorRow("low / mid / high##net graph warn3", g_hudNgWarn3);
                    ImGui::SameLine();
                    ImGui::ColorRow("##net graph warn2", g_hudNgWarn2, 0, 1);
                    ImGui::SameLine();
                    ImGui::ColorRow("##net graph warn1", g_hudNgWarn1, 0, 2);
                }
                ImGui::Combo("text style##net graph", &g_hudNgTextStyle, "none\0shadow\0outline\0");
                if (g_hudNgTextStyle != 0)
                    ImGui::ColorRow("outline color##net graph", g_hudNgOutlineColor);
                ImGui::Checkbox("background##net graph", &g_hudNgBg);
                if (g_hudNgBg)
                {
                    ImGui::SameLine();
                    ImGui::ColorRow("##net graph background color", g_hudNgBgColor);
                }
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImGui::SliderInt("x##net graph", &g_hudNgX, -1, (int)ds.x, g_hudNgX < 0 ? "stock" : "%d px");
                ImGui::SliderInt("y##net graph", &g_hudNgY, -1, (int)ds.y, g_hudNgY < 0 ? "stock" : "%d px");
            }, ImVec2(220.f, -1.f));

        // ---- TF2 crosshair rows (ours, after botox's) -------------------------------------------
        // cog first, then the swatch one slot left of it, both on the feature row (SimpleVisual:
        // arrow then swatch). The swatch on its own row put slot 0 exactly under the cog, and the
        // swatch's click-release picker replaced the settings popup the cog opened on press.
        ImGui::Checkbox("crosshair##ch", &g_chEnable);
        if (g_chEnable)
        {
            ImGui::OptionPopup("crosshair settings", [&]() {
                static bool  s_testRunning = false;
                static float s_testSpeed   = 0.f;
                static double s_testT0     = 0.0;
                static bool  s_previewBg   = false;

                const auto sides = [](const char* id, bool* left, bool* right, bool* top, bool* bottom) {
                    ImGui::PushID(id);
                    ImGui::Checkbox("left", left);
                    ImGui::Checkbox("right", right);
                    ImGui::Checkbox("top", top);
                    ImGui::Checkbox("bottom", bottom);
                    ImGui::PopID();
                };

                ImGui::Checkbox("antialias", &g_chAntialias);
                if (Crosshair_BlendCombo("blend##chblend"))
                    MenuTooltip("how the arms + dot mix with the frame under them. additive adds in\n"
                                "linear light so the colour survives instead of clipping to white;\n"
                                "the rest are krita's blending modes. alpha is the opacity, the\n"
                                "outline stays normal");
                SliderF("thickness##ch", &g_chThickness, 0.f, 10.f, "%.1f");
                SliderF("length##ch", &g_chLength, 0.f, 30.f, "%.1f");
                SliderF("arm aspect##ch", &g_chAspect, 0.25f, 4.f, "%.2f");
                if (ImGui::IsItemHovered())
                    MenuTooltip("width : height of the arm pair. 1.00 = square, above 1 stretches\n"
                                "the left/right arms, below 1 the top/bottom -- the short pair\n"
                                "always keeps the length above, so nothing shrinks");
                SliderF("gap##ch", &g_chGap, 0.f, 20.f, "%.1f");
                SliderF("gap aspect##ch", &g_chGapAspect, 0.25f, 4.f, "%.2f");
                if (ImGui::IsItemHovered())
                    MenuTooltip("width : height of the gap. above 1 pushes the left/right arms\n"
                                "out, below 1 the top/bottom -- the close pair keeps the gap\n"
                                "above, so nothing pulls in");
                ImGui::Checkbox("only some sides##ch", &g_chSidesOnly);
                if (g_chSidesOnly)
                    sides("ch sides", &g_chSideL, &g_chSideR, &g_chSideT, &g_chSideB);

                ImGui::Checkbox("taper##ch", &g_chTaper);
                if (g_chTaper)
                {
                    SliderF("near width##ch", &g_chTaperNear, 0.f, 3.f, "%.2f");
                    SliderF("far width##ch", &g_chTaperFar, 0.f, 3.f, "%.2f");
                }

                ImGui::Checkbox("gradient##ch", &g_chGradient);
                if (g_chGradient)
                {
                    ImGui::ColorRow("center##chgradcenter", g_chGradCenter);
                    ImGui::ColorRow("outer##chgradouter", g_chGradOuter);
                    SliderF("gradient start##ch", &g_chGradStart, 0.f, 1.f, "%.2f");
                    SliderF("gradient end##ch", &g_chGradEnd, 0.f, 1.f, "%.2f");
                    g_chGradEnd = ImMax(g_chGradEnd, g_chGradStart);
                }

                ImGui::Checkbox("dynamic gap##ch", &g_chDynamic);
                if (g_chDynamic)
                {
                    SliderF("min speed##ch", &g_chDynamicMin, 0.f, 500.f, "%.0f");
                    g_chDynamicRef = ImMax(g_chDynamicRef, g_chDynamicMin + 1.f);
                    SliderF("max speed##ch", &g_chDynamicRef, g_chDynamicMin + 1.f, 600.f, "%.0f");
                    SliderF("max extra gap##ch", &g_chDynamicMax, 0.f, 20.f, "%.1f");
                }

                SliderF("rotation##ch", &g_chRotation, 0.f, 360.f, "%.0f");

                ImGui::Checkbox("dot##ch", &g_chDot);
                if (g_chDot)
                {
                    ImGui::Checkbox("round dot##ch", &g_chDotCircle);
                    ImGui::Checkbox("custom dot color##ch", &g_chDotCustomColor);
                    if (g_chDotCustomColor)
                        ImGui::ColorRow("##chdotcolor", g_chDotColor);
                }

                ImGui::Checkbox("outline##ch", &g_chOutline);
                if (g_chOutline)
                {
                    ImGui::Checkbox("outline on lines##ch", &g_chOutlineOnLines);
                    ImGui::Checkbox("outline on dot##ch", &g_chOutlineOnDot);
                    ImGui::ColorRow("outline color##choutcol", g_chOutlineColor);
                    SliderF("outline thickness##ch", &g_chOutlineThick, 0.f, 10.f, "%.1f");
                    ImGui::Checkbox("soft outline (drop shadow)##ch", &g_chOutlineBlur);
                    if (g_chOutlineBlur)
                        SliderF("softness##ch", &g_chOutlineBlurRadius, 0.1f, 10.f, "%.1f");
                    ImGui::Checkbox("outline only some sides##ch", &g_chOutlineSidesOnly);
                    if (g_chOutlineSidesOnly)
                        sides("ch outline sides", &g_chOutlineSideL, &g_chOutlineSideR, &g_chOutlineSideT, &g_chOutlineSideB);
                }

                ImGui::Separator();
                ImGui::Checkbox("moving preview background##ch", &s_previewBg);
                ImGui::Checkbox("test speed##ch", &s_testRunning);
                if (s_testRunning)
                {
                    const double now = ImGui::GetTime();
                    if (s_testT0 == 0.0) s_testT0 = now;
                    s_testSpeed = 250.f + 250.f * sinf((float)(now - s_testT0) * 0.5f);
                }
                else s_testT0 = 0.0;

                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float side = 90.f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (s_previewBg) CustomCrosshair_DrawPreviewScene(dl, p0, side);
                else dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(40, 40, 40, 255));
                CustomCrosshair_DrawPreview(dl, p0, ImVec2(p0.x + side, p0.y + side), s_testRunning ? s_testSpeed : -1.f);
                ImGui::Dummy(ImVec2(side, side));
            }, ImVec2(230.f, -1));
            ImGui::SameLine();
            ImGui::ColorRow("##chcol", g_chColor, 0, 1);
        }

        ImGui::Checkbox("sniper crosshair##sch", &g_sniperChEnable);
        if (g_sniperChEnable)
        {
            ImGui::OptionPopup("sniper crosshair settings", [&]() {
                ImGui::Combo("style##schstyle", &g_sniperChStyle, "clarity\0" "clarity + outline\0" "interwebz\0" "custom (bars)\0");
                if (ImGui::IsItemHovered())
                    MenuTooltip("drawn instead of the crosshair above while a scoped weapon\n"
                                "(awp/sg550/g3sg1/scout) is held and NOT currently zoomed in");

                ImGui::ColorRow("color##schcolor", g_sniperChColor);

                if (g_sniperChStyle == 1 || g_sniperChStyle == 2 || (g_sniperChStyle == 3 && g_sniperChOutline))
                    ImGui::ColorRow("outline color##schoutline", g_sniperChOutlineColor);

                if (g_sniperChStyle == 2)
                {
                    SliderF("size##schiw", &g_sniperChInterwebzSize, 0.5f, 4.f, "%.1fx");
                    SliderI("thickness##schiw", &g_sniperChInterwebzThickness, 1, 8, "%d px");
                    SliderI("gap##schiw", &g_sniperChInterwebzGap, 0, 20, "%d px");
                    ImGui::ColorRow("dot color##schiwdot", g_sniperChDotColor);
                    ImGui::Checkbox("arm corners##schiw", &g_sniperChInterwebzArmCorners);
                    ImGui::Checkbox("dot corners##schiw", &g_sniperChInterwebzDotCorners);
                }
                else if (g_sniperChStyle == 3)
                {
                    SliderF("size##schsize", &g_sniperChSize, 0.f, 10.f, "%.1f");
                    SliderF("thickness##schthick", &g_sniperChThickness, 0.f, 6.f, "%.1f");
                    SliderF("gap##schgap", &g_sniperChGap, -5.f, 5.f, "%.1f");
                    ImGui::Checkbox("dot##schdot", &g_sniperChDot);
                    ImGui::Checkbox("t style##scht", &g_sniperChT);
                    ImGui::Checkbox("outline##scho", &g_sniperChOutline);
                    if (g_sniperChOutline)
                        SliderF("outline thickness##schot", &g_sniperChOutlineThickness, 0.1f, 3.f, "%.1f");
                }
            }, ImVec2(220.f, -1));
        }

        // botox misc.cpp force_crosshair checkbox: keeps the crosshair above up while holding an
        // unscoped sniper weapon instead of yielding to the sniper crosshair's own hide rule.
        // CSS has no weapon_debug_spread_show-equivalent debug convar (grepped, see report), so
        // unlike botox this never forces anything to draw while actually zoomed in.
        ImGui::Checkbox("force crosshair##ch", &g_forceCrosshair);
        if (ImGui::IsItemHovered())
            MenuTooltip("keep the crosshair up while holding a scoped weapon that isn't\n"
                        "zoomed in yet, instead of hiding it for the sniper crosshair");

        ImGui::Checkbox("csgo crosshair", &g_chCsgoEnable);
        if (g_chCsgoEnable)
        {
            ImGui::OptionPopup("csgo crosshair settings", [&]() {
                // exact cl_crosshairstyle numbering: 0/1 = panorama reticle, 2/3/4 = vgui bars
                ImGui::Combo("style##chcsgostyle", &g_chCsgoStyle,
                    "default\0" "default static\0" "classic standard\0" "classic dynamic\0" "classic static\0");

                if (g_chCsgoStyle <= 1)
                {
                    SliderF("gap offset##csgo", &g_chCsgoGap, -20.f, 20.f, "%.1f");
                    SliderF("gap aspect##csgo", &g_chCsgoGapAspect, 0.25f, 4.f, "%.2f");
                    SliderF("scale##csgo", &g_chCsgoScale, 0.5f, 8.f, "%.2f");
                    ImGui::Checkbox("crisp##csgo", &g_chCsgoCrisp);
                    ImGui::Checkbox("linear##csgo", &g_chCsgoLinear);
                    ImGui::Checkbox("additive##csgo", &g_chCsgoAdditive);
                    SliderF("black##csgo", &g_chCsgoBlack, 0.f, 1.f, "%.2f");
                    SliderF("velocity sens##csgo",  &g_chCsgoVelSens, 0.f, 2.f, "%.2f");
                    SliderF("velocity smooth##csgo", &g_chCsgoSmooth, 0.f, 0.5f, "%.2f s");
                    SliderF("arm aspect##csgo", &g_chCsgoAspect, 0.25f, 4.f, "%.2f");
                    if (g_chCsgoStyle == 0)
                        ImGui::Checkbox("spread lines##csgo", &g_chCsgoArcs);
                    ImGui::Checkbox("arms##csgo", &g_chCsgoPips);
                }
                else
                {
                    SliderF("size##csx",      &g_csXSize,  0.f, 20.f, "%.1f");
                    SliderF("arm aspect##csx", &g_chCsgoAspect, 0.25f, 4.f, "%.2f");
                    SliderF("thickness##csx", &g_csXThick, 0.f, 6.f,  "%.1f");
                    SliderF("gap##csx",       &g_csXGap,  -5.f, 20.f, "%.1f");
                    SliderF("gap aspect##csx", &g_chCsgoGapAspect, 0.25f, 4.f, "%.2f");
                    SliderI("alpha##csx",       &g_csXAlpha, 0, 255);
                    ImGui::Checkbox("dot##csx",     &g_csXDot);
                    ImGui::Checkbox("outline##csx", &g_csXOutline);
                    if (g_csXOutline)
                        // whole pixels: csgo's outline is an int vgui rect
                        SliderF("outline thickness##csx", &g_csXOutlineTh, 1.f, 3.f, "%.0f");
                    ImGui::Checkbox("t style##csx",   &g_csXT);
                    ImGui::Checkbox("use alpha##csx", &g_csXUseAlpha);
                    if (g_chCsgoStyle <= 3)   // 2/3 are the dynamic ones; 4 never moves
                    {
                        SliderF("velocity sens##csx",   &g_chCsgoVelSens, 0.f, 2.f, "%.2f");
                        SliderF("velocity smooth##csx", &g_chCsgoSmooth,  0.f, 0.5f, "%.2f s");
                    }
                    if (g_chCsgoStyle == 2)
                    {
                        SliderF("split dist##csx",  &g_csXSplitDist,  0.f, 25.f, "%.1f");
                        SliderF("split ratio##csx", &g_csXSplitRatio, 0.f, 1.f,  "%.2f");
                        SliderF("split alpha in##csx",  &g_csXInnerMod, 0.f, 1.f, "%.2f");
                        SliderF("split alpha out##csx", &g_csXOuterMod, 0.f, 1.f, "%.2f");
                    }
                }
            }, ImVec2(220.f, -1));
            ImGui::SameLine();
            ImGui::ColorRow("##chcsgocol", g_chCsgoColor, 0, 1);
        }
    }
    menu_group_end();
    menu_columns_end();
}

const char* kVisualsSub[3] = { "esp", "world", "screen" };
int s_visualsSub = 0;
void Tab_VisualsRoot()
{
    if      (s_visualsSub == 0) Page_VisualsEsp();
    else if (s_visualsSub == 1) Page_VisualsWorld();
    else                        Page_VisualsScreen();
}
