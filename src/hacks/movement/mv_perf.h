#pragma once
// per-call cost of the three things every search feature spends its budget on. CSS sims looked
// ~100x slower than TF2's (TF2 WC: 2843 sims / 80 ms; CSS EB: 4 sims / 7.8 ms), this names which.
// Printed once a second by Movement_Run as "PERF: ..." -- only when something was counted.
#include <windows.h>

struct MvPerfSlot { long long ticks = 0, maxTicks = 0; int n = 0; };
inline MvPerfSlot g_perfSim, g_perfRestore, g_perfTrace;

struct MvPerfScope
{
    MvPerfSlot& s; LARGE_INTEGER t0;
    explicit MvPerfScope(MvPerfSlot& slot) : s(slot) { QueryPerformanceCounter(&t0); }
    ~MvPerfScope()
    {
        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        const long long d = t1.QuadPart - t0.QuadPart;
        s.ticks += d; ++s.n;
        if (d > s.maxTicks) s.maxTicks = d;
    }
};
