#include "unitA_menu.h"
#include "menu_internal.h"   // SliderI/SliderF

// Unit A widgets, split one-per-botox-row so MOVETAB drops each at its exact botox
// menu_tab_movement.cpp slot. Was one flat UnitA_Menu_movement_main() -- split 11-04 so
// order inside "movement" and "other" groups can match botox (delay hop/auto crouch live
// inside "movement"; nulls/half sideways/smooth demo view/jumpstats live in "other").

void UnitA_Menu_DelayHop()
{
    ImGui::Checkbox("delay hop", &g_delayHop);
    if (g_delayHop)
        ImGui::OptionPopup("delay hop settings", [&]() {
            ImGui::Label("delay hop keybind");
            ImGui::Keybind("delay hop key", &g_delayHopKey, &g_delayHopMode);
            SliderI("ticks to wait##delay hop ticks", &g_delayHopTicks, 1, 8, "%d ticks");
        }, ImVec2(220.f, -1));
}

void UnitA_Menu_AutoCrouch()
{
    ImGui::Checkbox("auto crouch", &g_autoCrouch);
    if (g_autoCrouch)
        ImGui::OptionPopup("auto crouch settings", [&]() {
            ImGui::Label("auto crouch keybind");
            ImGui::Keybind("auto crouch key", &g_autoCrouchKey, &g_autoCrouchMode);
            SliderI("ticks##autocrouch", &g_autoCrouchTicks, 1, 32);
        }, ImVec2(220.f, -1));
}

void UnitA_Menu_Nulls()
{
    ImGui::Checkbox("nulls", &g_nulls);
}

void UnitA_Menu_HalfSideways()
{
    ImGui::Checkbox("half sideways bind", &g_hsw);
    if (g_hsw)
        ImGui::OptionPopup("half sideways settings", [&]() {
            ImGui::Label("half sideways keybind");
            ImGui::Keybind("half sideways key", &g_hswKey, &g_hswMode);
            ImGui::Combo("side##hsw", &g_hswSide, "left\0right\0");
            ImGui::Checkbox("allow W / S inputs in air", &g_hswAllowWS);
            ImGui::Checkbox("pre on ground", &g_hswPreGround);
            if (g_hswPreGround)
                ImGui::Checkbox("allow A / D inputs on ground", &g_hswAllowAD);
        }, ImVec2(220.f, -1));
}

void UnitA_Menu_SmoothDemoView()
{
    ImGui::Checkbox("smooth demo view", &g_silentView);
}

void UnitA_Menu_Jumpstats()
{
    // botox :1007-1015 -- just the enable + "show fails" setting (logic: movement_ex.inl JumpstatsTick)
    ImGui::Checkbox("jumpstats", &g_jumpstats);
    if (g_jumpstats)
        ImGui::OptionPopup("jumpstats settings", [&]() {
            ImGui::Checkbox("show fails##jumpstats", &g_jumpStatsShowFails);
        }, ImVec2(220.f, -1));
}
