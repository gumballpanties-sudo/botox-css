#pragma once

// ==== pinned worker threads ======================================================================
// Eject really unloads the dll now (thread.cpp, FreeLibraryAndExitThread), so a worker still
// running after it would execute unmapped code. Every worker takes its own ref on the dll here
// and drops it with FreeLibraryAndExitThread, which lives in kernel32 and never returns into us:
// the dll unmaps when the LAST of eject + workers is out, never earlier. A worker that outlives
// eject (open file dialog, download in flight, a join that timed out) just delays the unmap.

#include <windows.h>
#include <functional>

inline DWORD WINAPI PinnedThreadEntry(LPVOID p)
{
    auto* fn = static_cast<std::function<void()>*>(p);
    (*fn)();
    delete fn;   // captures destruct here, while the dll is still pinned
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&PinnedThreadEntry), &self);
    FreeLibraryAndExitThread(self, 0);
}

// Same contract as CreateThread: nullptr on failure, caller owns (and closes) the handle.
inline HANDLE CreatePinnedThread(std::function<void()> fn)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&PinnedThreadEntry), &self))
        return nullptr;
    auto* p = new std::function<void()>(std::move(fn));
    HANDLE h = CreateThread(nullptr, 0, PinnedThreadEntry, p, 0, nullptr);
    if (!h) { delete p; FreeLibrary(self); }
    return h;
}
