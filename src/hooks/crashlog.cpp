#include "crashlog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>   // CrashLog_WatchWrite: thread walk
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <direct.h>

// Everything here runs from inside a fault, so it uses no CRT allocation, no std::string, no
// DebugLog and no engine pointer. WriteFile, a stack buffer and the loader's own module list.

// Overridable so tests/crashlog_test.cpp can point the same code at a scratch file instead of
// appending its deliberate faults to the log someone is about to read a real crash out of.
#ifndef CRASHLOG_DIR
#define CRASHLOG_DIR  "C:\\botox-css"
#endif
#ifndef CRASHLOG_FILE
#define CRASHLOG_FILE "C:\\botox-css\\crash.log"
#endif

// ---- state -------------------------------------------------------------------------------
static CRITICAL_SECTION s_cs;
static bool      s_csInit   = false;
static HANDLE    s_hFile    = INVALID_HANDLE_VALUE;
static bool      s_fileTried= false;       // OPEN failed once -> stop retrying every fault
static bool      s_header   = false;       // session banner written
static uintptr_t s_selfBase = 0;
static uintptr_t s_selfEnd  = 0;
static int       s_records  = 0;
static PVOID     s_veh      = nullptr;

static constexpr int  k_maxRecords = 64;       // a crash loop must not fill the disk
static constexpr int  k_maxHitches = 32;
static constexpr int  k_maxFrames  = 32;
static constexpr DWORD k_hitchMs   = 100;      // main-thread frame gap worth a line

// ---- breadcrumbs -------------------------------------------------------------------------
// Literal pointers only, so the ring needs no copy and no lifetime. Written without a lock:
// a torn ring costs one garbled breadcrumb in a log, never a fault of its own.
static constexpr int k_marks = 16;
static const char* volatile s_mark[k_marks] = {};
static volatile LONG        s_markNext = 0;

void CrashLog_Mark(const char* tag)
{
    if (!tag) return;
    const LONG i = InterlockedIncrement(&s_markNext) - 1;
    s_mark[(unsigned)i & (k_marks - 1)] = tag;
}

// ---- file --------------------------------------------------------------------------------
// Opened on the FIRST record, never in DllMain: the loader lock is no place for CreateFile.
static void OpenFile()
{
    if (s_hFile != INVALID_HANDLE_VALUE || s_fileTried) return;
    s_fileTried = true;
    _mkdir(CRASHLOG_DIR);
    s_hFile = CreateFileA(CRASHLOG_FILE, FILE_APPEND_DATA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (s_hFile == INVALID_HANDLE_VALUE) return;
    // A file that grew past a megabyte is old sessions nobody reads. Start it over rather
    // than let it grow without bound; the current session is the one being debugged.
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(s_hFile, &sz) && sz.QuadPart > (1 << 20)) {
        CloseHandle(s_hFile);
        s_hFile = CreateFileA(CRASHLOG_FILE, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
}

static void Out(const char* fmt, ...)
{
    OpenFile();
    if (s_hFile == INVALID_HANDLE_VALUE) return;
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)sizeof(buf) - 2;
    buf[n]     = '\n';
    buf[n + 1] = '\0';
    DWORD w = 0;
    WriteFile(s_hFile, buf, (DWORD)(n + 1), &w, nullptr);
}

static void Banner()
{
    if (s_header) return;
    s_header = true;
    SYSTEMTIME t{}; GetLocalTime(&t);
    Out("");
    Out("======== botox session %04d-%02d-%02d %02d:%02d:%02d  pid %lu  botox.dll %p..%p ========",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
        GetCurrentProcessId(), (void*)s_selfBase, (void*)s_selfEnd);
}

void CrashLog_Note(const char* fmt, ...)
{
    if (!s_csInit) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    EnterCriticalSection(&s_cs);
    Banner();
    SYSTEMTIME t{}; GetLocalTime(&t);
    Out("[%02d:%02d:%02d] %s", t.wHour, t.wMinute, t.wSecond, buf);
    LeaveCriticalSection(&s_cs);
}

// ---- address -> module + RVA ---------------------------------------------------------------
// Module NAME plus offset, because the dll is loaded at a random base and a bare address means
// nothing later. botox.dll's own offsets resolve against build\Release\botox.map (/MAP is on).
static const char* ModuleOf(uintptr_t a, char* out, size_t cap, uintptr_t* rva)
{
    HMODULE hm = nullptr;
    if (a >= 0x10000 &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &hm) && hm)
    {
        char path[MAX_PATH];
        const DWORD n = GetModuleFileNameA(hm, path, MAX_PATH);
        const char* name = path;
        for (DWORD i = 0; i < n; ++i)
            if (path[i] == '\\' || path[i] == '/') name = path + i + 1;
        _snprintf(out, cap, "%s", name);
        out[cap - 1] = '\0';
        *rva = a - (uintptr_t)hm;
        return out;
    }
    _snprintf(out, cap, "<unmapped>");
    out[cap - 1] = '\0';
    *rva = a;
    return out;
}

// ---- stack walk ----------------------------------------------------------------------------
// RtlVirtualUnwind, not dbghelp: no SymInitialize, no pdb, no lock, and it is the same unwinder
// the OS itself uses, so optimised frame-pointer-less x64 code walks correctly.
// SEH-only function -- no C++ objects here (C2712).
static bool UnwindStep(CONTEXT* ctx)
{
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION rf = nullptr;
    __try {
        rf = RtlLookupFunctionEntry(ctx->Rip, &imageBase, nullptr);
        if (!rf) {                                   // leaf: return address sits at [rsp]
            const DWORD64 ret = *(DWORD64*)ctx->Rsp;
            ctx->Rip  = ret;
            ctx->Rsp += 8;
            return ret != 0;
        }
        PVOID   handler = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx->Rip, rf, ctx,
                         &handler, &establisher, nullptr);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ctx->Rip != 0;
}

static int Backtrace(const CONTEXT* from, uintptr_t* out, int cap)
{
    CONTEXT ctx = *from;
    int n = 0;
    while (n < cap && ctx.Rip >= 0x10000) {
        out[n++] = (uintptr_t)ctx.Rip;
        if (!UnwindStep(&ctx)) break;
    }
    return n;
}

static void PrintMarks()
{
    char line[1024];
    int  n = 0;
    line[0] = '\0';
    const LONG next = s_markNext;
    for (int k = k_marks; k > 0 && n < (int)sizeof(line) - 80; --k) {
        const char* m = s_mark[(unsigned)(next - k) & (k_marks - 1)];
        if (!m) continue;
        // _snprintf answers -1 on truncation, which would walk n backwards -- clamp, don't add
        const int w = _snprintf(line + n, sizeof(line) - n - 1, "%s%s", n ? " > " : "", m);
        if (w <= 0) break;
        n += w;
        line[n] = '\0';
    }
    Out("   marks: %s", n ? line : "(none)");
}

static void PrintStack(const uintptr_t* frames, int n)
{
    Out("   stack:");
    for (int i = 0; i < n; ++i) {
        char      mod[64];
        uintptr_t rva = 0;
        ModuleOf(frames[i], mod, sizeof(mod), &rva);
        const bool ours = frames[i] >= s_selfBase && frames[i] < s_selfEnd;
        Out("     %02d  %s+0x%IX%s", i, mod, rva, ours ? "   <<< botox" : "");
    }
    if (n == 0) Out("     (no frames)");
}

// ---- which exceptions are worth a record ----------------------------------------------------
// First-chance handlers see EVERY exception, including the ones dih's own __try guards are there
// to swallow and the C++ throws the game does by the thousand. Only the codes that actually end
// a process get a record, and each (code, address) pair only once -- a guarded probe that faults
// in a loop then costs one line, not a log file.
static bool Fatal(DWORD c)
{
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        return true;
    default:
        return false;
    }
}

static const char* CodeName(DWORD c)
{
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE";
    default:                              return "?";
    }
}

static bool SeenBefore(DWORD code, uintptr_t addr)
{
    static DWORD     seenCode[64];
    static uintptr_t seenAddr[64];
    static int       seenN = 0;
    for (int i = 0; i < seenN; ++i)
        if (seenCode[i] == code && seenAddr[i] == addr) return true;
    if (seenN < 64) { seenCode[seenN] = code; seenAddr[seenN] = addr; ++seenN; }
    return false;
}

static void Record(EXCEPTION_RECORD* er, CONTEXT* ctx)
{
    Banner();
    ++s_records;

    char      mod[64];
    uintptr_t rva = 0;
    const uintptr_t at = (uintptr_t)er->ExceptionAddress;
    ModuleOf(at, mod, sizeof(mod), &rva);

    SYSTEMTIME t{}; GetLocalTime(&t);
    char what[128] = {};
    if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         er->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && er->NumberParameters >= 2)
    {
        const ULONG_PTR op = er->ExceptionInformation[0];
        _snprintf(what, sizeof(what), " %s 0x%IX",
                  op == 0 ? "reading" : op == 1 ? "writing" : "executing",
                  (uintptr_t)er->ExceptionInformation[1]);
    }

    // The flag that decides who gets blamed: is dih anywhere on this stack at all.
    uintptr_t frames[k_maxFrames];
    const int nf = Backtrace(ctx, frames, k_maxFrames);
    bool ours = at >= s_selfBase && at < s_selfEnd;
    for (int i = 0; i < nf && !ours; ++i)
        ours = frames[i] >= s_selfBase && frames[i] < s_selfEnd;

    Out("");
    Out("[%02d:%02d:%02d] %s (0x%08lX)%s  at %s+0x%IX  thread %lu%s",
        t.wHour, t.wMinute, t.wSecond, CodeName(er->ExceptionCode),
        (unsigned long)er->ExceptionCode, what, mod, rva,
        GetCurrentThreadId(), ours ? "   *** botox on stack ***" : "");
    Out("   rax=%016IX rbx=%016IX rcx=%016IX rdx=%016IX",
        (uintptr_t)ctx->Rax, (uintptr_t)ctx->Rbx, (uintptr_t)ctx->Rcx, (uintptr_t)ctx->Rdx);
    Out("   rsi=%016IX rdi=%016IX rbp=%016IX rsp=%016IX",
        (uintptr_t)ctx->Rsi, (uintptr_t)ctx->Rdi, (uintptr_t)ctx->Rbp, (uintptr_t)ctx->Rsp);
    Out("   r8 =%016IX r9 =%016IX r10=%016IX r11=%016IX",
        (uintptr_t)ctx->R8, (uintptr_t)ctx->R9, (uintptr_t)ctx->R10, (uintptr_t)ctx->R11);
    PrintMarks();
    PrintStack(frames, nf);
    if (s_records == k_maxRecords)
        Out("   (record cap reached -- no further crash records this session)");
}

// ---- write watch (debug registers) ----------------------------------------------------------
// DR0 = 8-byte write watch, armed on every thread. Each hit logs the writer + stack, then the
// write goes through untouched. Capped: after k_watchMax hits the hitting thread disarms.
static volatile uintptr_t s_watchAddr = 0;
static volatile LONG      s_watchHits = 0;
static constexpr LONG     k_watchMax  = 24;

static bool WatchHit(EXCEPTION_POINTERS* ep)
{
    CONTEXT* c = ep->ContextRecord;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(c->Dr6 & 1) || !s_watchAddr)
        return false;
    c->Dr6 = 0;
    const LONG n = InterlockedIncrement(&s_watchHits);
    if (n > k_watchMax) { c->Dr7 &= ~0x000F0003ull; return true; }
    if (!s_csInit) return true;
    EnterCriticalSection(&s_cs);
    Banner();
    char mod[64]; uintptr_t rva = 0;
    ModuleOf((uintptr_t)c->Rip, mod, sizeof(mod), &rva);   // RIP = instruction after the write
    uintptr_t now = 0;
    __try { now = *(const uintptr_t*)s_watchAddr; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    SYSTEMTIME t{}; GetLocalTime(&t);
    Out("");
    Out("[%02d:%02d:%02d] WATCH %ld write [%IX] = %016IX  by %s+0x%IX  thread %lu",
        t.wHour, t.wMinute, t.wSecond, n, (uintptr_t)s_watchAddr, now, mod, rva, GetCurrentThreadId());
    uintptr_t frames[k_maxFrames];
    PrintStack(frames, Backtrace(c, frames, k_maxFrames));
    LeaveCriticalSection(&s_cs);
    return true;
}

static void WatchSetThread(HANDLE th, uintptr_t addr)
{
    CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(th, &c)) return;
    c.Dr0 = addr;
    c.Dr7 = (c.Dr7 & ~0x000F0003ull) | (addr ? (1ull | (1ull << 16) | (3ull << 18)) : 0);   // L0, RW0=write, LEN0=8
    SetThreadContext(th, &c);
}

static DWORD WINAPI WatchArmThread(LPVOID p)
{
    const uintptr_t addr = (uintptr_t)p;
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) { WatchSetThread(th, addr); ResumeThread(th); }
        CloseHandle(th);
    }
    CloseHandle(snap);
    return 0;
}

void CrashLog_WatchWrite(const void* addr)
{
    const uintptr_t a = (uintptr_t)addr & ~(uintptr_t)7;
    if (a == s_watchAddr) return;
    if (!a) {   // disarm: registers first, address last, so a late hit still finds WatchHit
        if (HANDLE h = CreateThread(nullptr, 0, WatchArmThread, nullptr, 0, nullptr)) {
            WaitForSingleObject(h, 2000); CloseHandle(h);
        }
        CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;   // the caller's own thread
        if (GetThreadContext(GetCurrentThread(), &c)) { c.Dr7 &= ~0x000F0003ull; SetThreadContext(GetCurrentThread(), &c); }
        s_watchAddr = 0;
        return;
    }
    s_watchAddr = a; s_watchHits = 0;
    if (s_csInit) {
        uintptr_t now = 0;
        __try { now = a ? *(const uintptr_t*)a : 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        EnterCriticalSection(&s_cs); Banner();
        Out("WATCH armed [%IX] = %016IX", a, now);
        LeaveCriticalSection(&s_cs);
    }
    // a helper thread, so the caller (the game thread, the likely writer) gets armed too
    if (HANDLE h = CreateThread(nullptr, 0, WatchArmThread, (LPVOID)a, 0, nullptr)) {
        WaitForSingleObject(h, 2000); CloseHandle(h);
    }
}

static LONG CALLBACK Veh(EXCEPTION_POINTERS* ep)
{
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (WatchHit(ep)) return EXCEPTION_CONTINUE_EXECUTION;
    if (!Fatal(ep->ExceptionRecord->ExceptionCode))        return EXCEPTION_CONTINUE_SEARCH;
    if (s_records >= k_maxRecords || !s_csInit)            return EXCEPTION_CONTINUE_SEARCH;

    EnterCriticalSection(&s_cs);
    if (!SeenBefore(ep->ExceptionRecord->ExceptionCode,
                    (uintptr_t)ep->ExceptionRecord->ExceptionAddress))
        Record(ep->ExceptionRecord, ep->ContextRecord);
    LeaveCriticalSection(&s_cs);

    // Never handled here. This only watches -- swallowing a fault the game meant to catch (or
    // meant to die on) would turn a readable crash into a hang.
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- hitch trace ------------------------------------------------------------------------------
// ---- spans ---------------------------------------------------------------------------------
// Per-tag max / sum / count since the last summary. Written from the game thread and the render
// thread without a lock: a torn number costs one odd figure in a diagnostic line.
struct Span { const char* tag; double maxMs, sumMs; long n; };
static Span s_span[16];
static int  s_spanCount = 0;
static LARGE_INTEGER s_spanFreq{};

void CrashLog_Span(const char* tag, double ms)
{
    if (!tag) return;
    int i = 0;
    for (; i < s_spanCount; ++i) if (s_span[i].tag == tag) break;
    if (i == s_spanCount) { if (i >= 16) return; s_span[i] = { tag, 0, 0, 0 }; ++s_spanCount; }
    if (ms > s_span[i].maxMs) s_span[i].maxMs = ms;
    s_span[i].sumMs += ms; ++s_span[i].n;
}
CrashSpan::CrashSpan(const char* t) : tag(t), t0(0)
{
    if (!s_spanFreq.QuadPart) QueryPerformanceFrequency(&s_spanFreq);
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c); t0 = c.QuadPart;
}
CrashSpan::~CrashSpan()
{
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
    if (s_spanFreq.QuadPart) CrashLog_Span(tag, double(c.QuadPart - t0) * 1000.0 / double(s_spanFreq.QuadPart));
}

static constexpr double k_stutterMs   = 33.0;    // one frame of 30 fps
static constexpr double k_spanNoteMs  = 4.0;     // a span worth naming even in a smooth window
static constexpr int    k_maxSummaries = 180;    // 30 minutes of 10 s windows

// Every 10 s: the window's stutter frames + the spans that stood out. Caller holds nothing.
static void StutterSummary(double worstFrameMs, int stutterFrames, double windowMs)
{
    static int s_summaries = 0;
    double worstMax = 0; int iMax = -1; double worstSum = 0; int iSum = -1;
    for (int i = 0; i < s_spanCount; ++i) {
        if (s_span[i].maxMs > worstMax) { worstMax = s_span[i].maxMs; iMax = i; }
        if (s_span[i].sumMs > worstSum) { worstSum = s_span[i].sumMs; iSum = i; }
    }
    const bool say = (stutterFrames > 0 || worstMax >= k_spanNoteMs) && s_summaries < k_maxSummaries;
    if (say) {
        ++s_summaries;
        EnterCriticalSection(&s_cs);
        Banner();
        SYSTEMTIME t{}; GetLocalTime(&t);
        Out("[%02d:%02d:%02d] STUTTER %d frame(s) >= %.0f ms in %.0f s, worst %.0f ms | worst span %s %.1f ms"
            " (x%ld, %.0f ms total) | busiest %s %.0f ms total (x%ld, %.1f avg)",
            t.wHour, t.wMinute, t.wSecond, stutterFrames, k_stutterMs, windowMs / 1000.0, worstFrameMs,
            iMax >= 0 ? s_span[iMax].tag : "-", worstMax, iMax >= 0 ? s_span[iMax].n : 0L, iMax >= 0 ? s_span[iMax].sumMs : 0.0,
            iSum >= 0 ? s_span[iSum].tag : "-", worstSum, iSum >= 0 ? s_span[iSum].n : 0L,
            (iSum >= 0 && s_span[iSum].n) ? s_span[iSum].sumMs / s_span[iSum].n : 0.0);
        if (s_summaries == k_maxSummaries) Out("   (stutter summary cap reached -- no further lines)");
        LeaveCriticalSection(&s_cs);
    }
    for (int i = 0; i < s_spanCount; ++i) { s_span[i].maxMs = 0; s_span[i].sumMs = 0; s_span[i].n = 0; }
}

void CrashLog_Frame()
{
    if (!s_csInit) return;
    static LARGE_INTEGER s_freq{};
    static LARGE_INTEGER s_last{};
    static LARGE_INTEGER s_window{};
    static int    s_hitches = 0;
    static int    s_stutterFrames = 0;
    static double s_worstFrame = 0;
    if (!s_freq.QuadPart) { QueryPerformanceFrequency(&s_freq); if (!s_freq.QuadPart) return; }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const LONGLONG prev = s_last.QuadPart;
    s_last = now;
    if (!prev) { s_window = now; return; }

    const double ms = double(now.QuadPart - prev) * 1000.0 / double(s_freq.QuadPart);
    if (ms >= k_stutterMs) { ++s_stutterFrames; if (ms > s_worstFrame) s_worstFrame = ms; }
    const double windowMs = double(now.QuadPart - s_window.QuadPart) * 1000.0 / double(s_freq.QuadPart);
    if (windowMs >= 10000.0) {
        StutterSummary(s_worstFrame, s_stutterFrames, windowMs);
        s_window = now; s_stutterFrames = 0; s_worstFrame = 0;
    }

    if (s_hitches >= k_maxHitches) return;
    if (ms < (double)k_hitchMs) return;
    ++s_hitches;

    EnterCriticalSection(&s_cs);
    Banner();
    SYSTEMTIME t{}; GetLocalTime(&t);
    Out("");
    Out("[%02d:%02d:%02d] HITCH %.0f ms on the main thread (vgui frame gap)",
        t.wHour, t.wMinute, t.wSecond, ms);
    PrintMarks();
    if (s_hitches == k_maxHitches) Out("   (hitch cap reached -- no further hitch lines)");
    LeaveCriticalSection(&s_cs);
}

// ---- install ------------------------------------------------------------------------------
// Called from DllMain, under the loader lock: PE headers off hSelfModule (no psapi call), a
// critical section, and AddVectoredExceptionHandler. No file, no CRT, no thread.
void CrashLog_Install(void* hSelfModule)
{
    if (s_csInit) return;
    s_selfBase = (uintptr_t)hSelfModule;
    s_selfEnd  = s_selfBase + 0x1000;
    if (s_selfBase) {
        auto* dos = (IMAGE_DOS_HEADER*)s_selfBase;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            auto* nt = (IMAGE_NT_HEADERS*)(s_selfBase + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                s_selfEnd = s_selfBase + nt->OptionalHeader.SizeOfImage;
        }
    }
    InitializeCriticalSection(&s_cs);
    s_csInit = true;
    s_veh = AddVectoredExceptionHandler(1, Veh);   // 1 = first in line, before anyone's __except
}

// Eject, right before the dll unmaps: a VEH left registered would be the process's next
// exception jumping into freed pages. The lock is NOT deleted (same reason as DebugLog_Shutdown).
void CrashLog_Uninstall()
{
    CrashLog_WatchWrite(nullptr);   // debug registers off before the VEH that answers them goes
    if (s_veh) { RemoveVectoredExceptionHandler(s_veh); s_veh = nullptr; }
    if (!s_csInit) return;
    EnterCriticalSection(&s_cs);
    if (s_hFile != INVALID_HANDLE_VALUE) { CloseHandle(s_hFile); s_hFile = INVALID_HANDLE_VALUE; }
    s_fileTried = true;   // never reopen
    LeaveCriticalSection(&s_cs);
}
