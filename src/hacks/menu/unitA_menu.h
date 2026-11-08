#pragma once

// Unit A (movement tricks) menu widgets. Plain ImGui, one function per botox row/popup so
// MOVETAB can call each at its exact botox menu_tab_movement.cpp slot (order matters).
void UnitA_Menu_DelayHop();        // botox :318-329, "movement" group, after bunny hop
void UnitA_Menu_AutoCrouch();      // botox :395-405, "movement" group, after jump bug
void UnitA_Menu_Nulls();           // botox :885, "other" group
void UnitA_Menu_HalfSideways();    // botox :887-901, "other" group
void UnitA_Menu_SmoothDemoView();  // botox :873, "other" group ("silent view" renamed)
void UnitA_Menu_Jumpstats();       // botox :1007-1015, "other" group
