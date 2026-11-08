#pragma once
// botox aspect ratio "stretch crosshair" / "stretch cheat overlays": horizontal stretch of our
// own ImGui overlays by screen aspect / wanted aspect (render.cpp BuildImGuiFrame wraps the
// calls). Screen-space panels stretch about their own pivot (botox begin_stretch_block), the
// crosshair about the screen centre. World-anchored overlays (esp, route / eb / ps markers)
// are left alone: TF2's world-to-screen matrix already carries r_aspectratio (CViewSetup
// m_flAspectRatio, sdk view.cpp), so they line up without it.
enum AsKind { AsPanel = 0, AsCrosshair = 1 };
void AspectStretch_Run(void (*draw)(), AsKind kind);
