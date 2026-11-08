#pragma once
#include "../../globals/globals.h"

// Unit D port: fullbright + disable interp + aspect ratio stretch (r_aspectratio, botox's
// own convar route) -- all convar pokes, no hook.
//
// world fov / sun angle / dlight / custom blood+precip particle colour / third person /
// world+prop texture replace / reflections / minecraft hud / web on surface / remove hud
// elements / remove visual recoil are NOT here -- see the Unit D return report for why
// each one is blocked (new hook needed, or a file this unit does not own).

// 100ms tick, called from visuals.cpp's UpdateVisualsCvars -- same cadence fog/sway ride.
void WorldFx_Tick();

// Pure, engine-free: the letterbox/pillarbox rect that fits targetAspect (w/h) inside a
// window of size windowW x windowH, centred. No-op (full window) when targetAspect <= 0
// or the window is already that ratio. Inline + header-only so tests/worldfx_test.cpp can
// check it without linking the rest of this DLL's engine globals.
inline RECT WorldFx_ComputeAspectRect(int windowW, int windowH, float targetAspect)
{
    RECT r{ 0, 0, windowW, windowH };
    if (windowW <= 0 || windowH <= 0 || targetAspect <= 0.001f) return r;

    const float windowAspect = (float)windowW / (float)windowH;
    if (targetAspect > windowAspect) {
        // target is wider than the window -- letterbox (bars top/bottom)
        const int h = (int)((float)windowW / targetAspect + 0.5f);
        const int y = (windowH - h) / 2;
        r = { 0, y, windowW, y + h };
    } else if (targetAspect < windowAspect) {
        // target is narrower -- pillarbox (bars left/right)
        const int w = (int)((float)windowH * targetAspect + 0.5f);
        const int x = (windowW - w) / 2;
        r = { x, 0, x + w, windowH };
    }
    return r;
}

// IClientMode::OverrideView (slot 16, verified -- see world_fx.cpp) -- world fov + third
// person. Called once from PollThread, same place ScreenFx_Install() hooks slot 39 on the
// same interface.
void WorldFx_InstallViewHook();
