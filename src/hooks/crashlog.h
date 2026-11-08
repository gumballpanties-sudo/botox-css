#pragma once

// ==== crash log -- C:\botox-css\crash.log =================================================
// Independent of the debug log ON PURPOSE: its own file, its own lock, no g_debugLogEnable
// gate, installed from DllMain before a single hook exists. debug.log is off by default and
// is recreated empty every launch, so the one thing nobody ever has after a crash is the
// record of the crash. This file appends, survives the session, and is written from the
// faulting thread itself.

void CrashLog_Install(void* hSelfModule);
void CrashLog_Uninstall();   // eject: VEH off + file closed, before the dll unmaps

// Breadcrumb. `tag` MUST be a string literal -- only the pointer is stored, nothing is copied.
// The last 16 are printed with every record, so a fault that lands inside client.dll still
// names what dih was doing when it got there. Cost: two stores and one atomic increment.
void CrashLog_Mark(const char* tag);

// One call per main-thread frame (CVGui::RunFrame). A frame gap over the threshold writes a
// HITCH line carrying the same breadcrumbs -- the stutter half of "random crashes/stutters",
// in the same file, with no debug log needed.
void CrashLog_Frame();

// One timestamped line, no debug-log toggle needed. For rare diagnostics a feature wants to
// see from a user's session (a draw path silently falling back) -- callers rate-limit it.
void CrashLog_Note(const char* fmt, ...);

// Hardware write watch (DR0, 8 bytes) on every thread: each write logs writer + stack to
// crash.log (24 max per arm). nullptr disarms. Re-arming at a new address moves it.
void CrashLog_WatchWrite(const void* addr);

// Stutter probe -- the half of "it keeps stuttering" that sits UNDER the HITCH floor. A module
// reports how long its slice of a frame took (`tag` a literal, 16 distinct tags max); every 10 s
// one STUTTER line counts the main-thread frames >= 33 ms and names the spans with the worst
// single run and the biggest total, so a 40 ms frame has a suspect instead of a shrug. Silent
// while nothing stutters and no span ever passes 4 ms.
void CrashLog_Span(const char* tag, double ms);
struct CrashSpan {
    const char* tag; long long t0;
    explicit CrashSpan(const char* t);
    ~CrashSpan();
};
