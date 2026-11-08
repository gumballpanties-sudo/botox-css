#pragma once

// ==== how much real time one run of the movement pipeline may spend =====================
// Engine-free so tests/netcvar_test.cpp can brute-force it. prediction.h includes it and
// TbBudgetStart is the only caller.
//
// THE SHARE IS OF WALL CLOCK, NOT OF A TICK.
// The pipeline runs once per CreateMove, and the engine builds a command per RENDERED FRAME,
// not per tick -- that is the whole reason cl.chokedcommands exists, and the reason the aim
// half passes `lastoutgoingcommand + chokedcommands` as outgoing_command. So a budget written
// as "0.40 of one tick" is handed out fps/tickrate times a second. At 66 fps that is the 40%
// it reads as. At 144 it is 86%, at 200 it is 120%, and a budget you cannot physically spend
// is not a budget -- it just pins the main thread and the frame rate falls to meet it. Frame
// time is what the netchannel's out-packet rate is made of, so this is paid for twice.
//
// Pace it off the REAL gap since the last run and the share means what it says at any frame
// rate: one tick's worth is the ceiling (fps at or below tickrate -- unchanged behaviour),
// the gap is the budget above that, and search throughput per SECOND is identical either way
// (66 x 6.0ms == 144 x 2.8ms == 0.40 s/s). The floor is only there so a degenerate gap (first
// call, a QPC that went backwards) cannot hand out a zero.

inline constexpr float k_cmdBudgetFloor = 0.0005f;   // 0.5 ms; bites above ~800 fps

// flSinceLast <= 0 means "no usable measurement" -> the tick ceiling, i.e. the old behaviour.
// Never returns more than the old budget: this can only narrow, never widen.
inline float CmdBudgetSeconds(float flTickInterval, float flSinceLast, float flShare)
{
    if (!(flTickInterval > 0.f) || !(flShare > 0.f)) return 0.f;
    const float ceiling = flTickInterval * flShare;
    if (!(flSinceLast > 0.f)) return ceiling;
    float want = flSinceLast * flShare;
    if (want > ceiling)          want = ceiling;
    if (want < k_cmdBudgetFloor) want = k_cmdBudgetFloor;
    if (want > ceiling)          want = ceiling;   // a tick shorter than the floor still wins
    return want;
}
