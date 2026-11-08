#pragma once
// dih -- shared includes + engine helpers

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <type_traits>
#include <vector>
#include <ctime>
#include <direct.h>

#include "../dependencies/imgui/imgui.h"
#include "../dependencies/imgui/imgui_impl_dx9.h"
#include "../dependencies/imgui/imgui_impl_win32.h"
#include "../dependencies/minhook/MinHook.h"

// ==== ConVar / ICvar ============================================================
// x64 ConVar member offsets. A child registration keeps its values on the parent.
inline constexpr uintptr_t k_cvFlags  = 0x28;   // m_nFlags (ConCommandBase)
inline constexpr uintptr_t k_cvParent = 0x38;   // m_pParent
inline constexpr uintptr_t k_cvString = 0x48;   // m_pszString
inline constexpr uintptr_t k_cvStrLen = 0x50;   // m_StringLength
inline constexpr uintptr_t k_cvFloat  = 0x54;   // m_fValue
inline constexpr uintptr_t k_cvInt    = 0x58;   // m_nValue

// Where a ConVar's values actually live. A parent pointer outside user space means
// there is no parent, i.e. this object owns them. Dereferences cv -- call inside __try.
inline uintptr_t ConVarParent(const void* cv)
{
    const uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
    const uintptr_t parent = *reinterpret_cast<const uintptr_t*>(base + k_cvParent);
    return (parent < 0x10000 || parent > 0x7FFFFFFFFFFF) ? base : parent;
}

// The m_pszString / m_StringLength pair, for the features that redirect a ConVar's
// string at a buffer of their own (skybox name, fog colour). ppStr null = unresolvable.
struct ConVarStr { char** ppStr; int* pLen; };

__declspec(noinline) inline ConVarStr ConVarStrSlot(const void* cv)
{
    ConVarStr s{ nullptr, nullptr };
    if (!cv) return s;
    __try {
        const uintptr_t parent = ConVarParent(cv);
        s.ppStr = reinterpret_cast<char**>(parent + k_cvString);
        s.pLen  = reinterpret_cast<int*>  (parent + k_cvStrLen);
    } __except(EXCEPTION_EXECUTE_HANDLER) { s.ppStr = nullptr; s.pLen = nullptr; }
    return s;
}

class ConVar
{
public:
    float GetFloat()
    {
        float v = 0.f;
        __try { v = *reinterpret_cast<float*>(ConVarParent(this) + k_cvFloat); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        return v;
    }

    void ForceFloat(float value)
    {
        __try {
            const uintptr_t parent = ConVarParent(this);
            *reinterpret_cast<float*>(parent + k_cvFloat) = value;
            *reinterpret_cast<int*>  (parent + k_cvInt)   = static_cast<int>(value);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    void ForceInt(int value)
    {
        __try {
            const uintptr_t parent = ConVarParent(this);
            *reinterpret_cast<float*>(parent + k_cvFloat) = static_cast<float>(value);
            *reinterpret_cast<int*>  (parent + k_cvInt)   = value;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // m_nFlags of the parent (+0x28) -- the word ConVar::IsFlagSet tests and AddFlags ORs
    // (client.dll ConVar primary slots 2/3: `mov rax,[rcx+38h]; test/or [rax+28h],edx`).
    // A raw write fires nothing. -1 = unreadable.
    int GetFlags()
    {
        int f = -1;
        __try { f = *reinterpret_cast<int*>(ConVarParent(this) + k_cvFlags); }
        __except(EXCEPTION_EXECUTE_HANDLER) { f = -1; }
        return f;
    }

    void SetFlags(int flags)
    {
        __try { *reinterpret_cast<int*>(ConVarParent(this) + k_cvFlags) = flags; }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // The real SetValue: change callbacks AND the engine's global one run, which ForceInt's raw
    // write never reaches. IConVar vptr sits at this+0x30; MSVC emits its three SetValue
    // overloads in REVERSE declaration order, 0=SetValue(int) 1=SetValue(float)
    // 2=SetValue(char*) -- read off client.dll's ConVar vtable (slot 0 -> InternalSetIntValue)
    // and engine SetDirect calling slot 2 with a string. The name goes through primary slot 4
    // first and the value is read back, so a layout mismatch is a no-op, not a wild call.
    bool SetValueInt(const char* expectName, int value)
    {
        bool ok = false;
        __try {
            void** vtbl = *reinterpret_cast<void***>(this);
            using GetName_t   = const char*(__fastcall*)(void*);
            using IsCommand_t = bool(__fastcall*)(void*);
            const char* name = ((GetName_t)vtbl[4])(this);
            if (name && !((IsCommand_t)vtbl[1])(this) && strcmp(name, expectName) == 0) {
                void* p = reinterpret_cast<char*>(this) + 0x30;
                using SetInt_t = void(__fastcall*)(void*, int);
                ((SetInt_t)(*reinterpret_cast<void***>(p))[0])(p, value);
                ok = (static_cast<int>(GetFloat()) == value);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        return ok;
    }
};

class ICvar
{
public:
    ConVar* FindVar(const char* name)
    {
        ConVar* r = nullptr;
        __try {
            using Fn = ConVar*(__thiscall*)(void*, const char*);
            r = ((Fn)(*(void***)(this))[12])(this, name);
        } __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        return r;
    }

    void* GetCommandsRaw()
    {
        void* r = nullptr;
        __try {
            using Fn = void*(__thiscall*)(void*);
            r = ((Fn)(*(void***)(this))[16])(this);
        } __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        return r;
    }

    ConVar* FindVarWalk(const char* name)
    {
        ConVar* r = FindVar(name);
        if (r) return r;

        uintptr_t pBase = reinterpret_cast<uintptr_t>(GetCommandsRaw());
        int safety = 0;
        while (pBase != 0 && ++safety < 16384)
        {
            const char* pName = nullptr;
            uintptr_t   pNext = 0;
            __try {
                void** vtbl = *reinterpret_cast<void***>(pBase);
                using GetName_t = const char*(__thiscall*)(void*);
                pName = ((GetName_t)vtbl[4])(reinterpret_cast<void*>(pBase));
                pNext = *reinterpret_cast<uintptr_t*>(pBase + 0x08);
            } __except(EXCEPTION_EXECUTE_HANDLER) { break; }

            if (pName && strcmp(pName, name) == 0)
                return reinterpret_cast<ConVar*>(pBase);
            pBase = pNext;
        }
        return nullptr;
    }
};

// Safe vtable call -- validates the vtable and the slot, SEH-wrapped. A call that cannot
// be made returns a zero-initialised Ret, so a caller that needs to tell "failed" from a
// genuine zero must check the interface pointer itself first.
template<typename Ret, typename Self, typename... Args>
inline Ret VCall(Self* self, int idx, Args... args) {
    using Fn = Ret(__fastcall*)(Self*, Args...);
    if constexpr (std::is_void_v<Ret>) {
        __try {
            void** vtbl = *(void***)self;
            if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return;
            Fn fn = reinterpret_cast<Fn>(vtbl[idx]);
            if (reinterpret_cast<uintptr_t>(fn) < 0x10000) return;
            fn(self, args...);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } else {
        Ret result{};
        __try {
            void** vtbl = *(void***)self;
            if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return result;
            Fn fn = reinterpret_cast<Fn>(vtbl[idx]);
            if (reinterpret_cast<uintptr_t>(fn) < 0x10000) return result;
            result = fn(self, args...);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return result;
    }
}

struct IEngineClient   {};
struct INetChannelInfo {};

// TF2 IGameEvent vtable, from a slot probe:
//   0 ~dtor  1 GetName  2 IsReliable  3 IsLocal  4 IsEmpty
//   5 GetBool  6 GetInt  7 GetFloat  8 GetString
//   9 SetBool 10 SetInt 11 SetFloat 12 SetString
struct IGameEventVtbl {
    static inline const char* GetName(void* pEvent)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return nullptr;
        if (reinterpret_cast<uintptr_t>(vtbl[1]) < 0x10000) return nullptr;
        using Fn = const char*(__fastcall*)(void*);
        return ((Fn)vtbl[1])(pEvent);
    }
    static inline int GetInt(void* pEvent, const char* key, int def = 0)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return def;
        if (reinterpret_cast<uintptr_t>(vtbl[6]) < 0x10000) return def;
        using Fn = int(__fastcall*)(void*, const char*, int);
        return ((Fn)vtbl[6])(pEvent, key, def);
    }
    // IGameEvent slot order (public/igameevents.h): 0 dtor, 1 GetName, 2 IsReliable,
    // 3 IsLocal, 4 IsEmpty, 5 GetBool, 6 GetInt, 7 GetFloat, 8 GetString, 9 SetBool,
    // 10 SetInt, 11 SetFloat, 12 SetString.
    static inline float GetFloat(void* pEvent, const char* key, float def = 0.f)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return def;
        if (reinterpret_cast<uintptr_t>(vtbl[7]) < 0x10000) return def;
        using Fn = float(__fastcall*)(void*, const char*, float);
        return ((Fn)vtbl[7])(pEvent, key, def);
    }
    static inline bool GetBool(void* pEvent, const char* key, bool def = false)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return def;
        if (reinterpret_cast<uintptr_t>(vtbl[5]) < 0x10000) return def;
        using Fn = bool(__fastcall*)(void*, const char*, bool);
        return ((Fn)vtbl[5])(pEvent, key, def);
    }
    static inline const char* GetString(void* pEvent, const char* key, const char* def = "")
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return def;
        if (reinterpret_cast<uintptr_t>(vtbl[8]) < 0x10000) return def;
        using Fn = const char*(__fastcall*)(void*, const char*, const char*);
        return ((Fn)vtbl[8])(pEvent, key, def);
    }
    static inline void SetInt(void* pEvent, const char* key, int val)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return;
        if (reinterpret_cast<uintptr_t>(vtbl[10]) < 0x10000) return;
        using Fn = void(__fastcall*)(void*, const char*, int);
        ((Fn)vtbl[10])(pEvent, key, val);
    }
    static inline void SetString(void* pEvent, const char* key, const char* val)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return;
        if (reinterpret_cast<uintptr_t>(vtbl[12]) < 0x10000) return;
        using Fn = void(__fastcall*)(void*, const char*, const char*);
        ((Fn)vtbl[12])(pEvent, key, val);
    }
};

// ==== Pattern scanner (IDA-style, walks committed pages only) ===================
struct PatternScanToken { uint8_t byte; bool wild; };

// Longest live pattern is ~30 bytes. Fixed cap keeps the parse off the heap.
inline constexpr size_t k_patternMaxTokens = 128;

// Parse "48 89 5C 24 ? 41 56" into tokens. Returns count, 0 on empty/overflow.
// Shared: mmfix.cpp's in-function scan uses this parser rather than its own copy.
inline size_t PatternParse(const char* ida_pattern, PatternScanToken* out, size_t cap)
{
    size_t n = 0;
    for (const char* p = ida_pattern; *p && n < cap; )
    {
        if (*p == ' ') { ++p; continue; }
        if (*p == '?') {
            out[n++] = { 0, true };
            while (*p == '?') ++p;
        } else {
            out[n++] = { (uint8_t)strtoul(p, nullptr, 16), false };
            while (*p && *p != ' ') ++p;
        }
    }
    return n;
}

// Compare at one candidate. Anchor byte is already known to match, so skip it.
inline bool PatternMatchAt(const uint8_t* at, const PatternScanToken* pat, size_t plen, size_t anchor)
{
    for (size_t j = 0; j < plen; ++j)
        if (j != anchor && !pat[j].wild && at[j] != pat[j].byte) return false;
    return true;
}

// Anchored scan of one committed span. Testing every byte offset cost 22 serial scans
// over ~60MB of client.dll at startup; memchr on one fixed byte walks the same span
// SSE-wide and only stops where a match is possible.
inline const uint8_t* PatternScanSpan(const uint8_t* begin, const uint8_t* end,
                                      const PatternScanToken* pat, size_t plen, size_t anchor)
{
    if ((size_t)(end - begin) < plen) return nullptr;
    const uint8_t* last = end - plen;               // last legal start
    const uint8_t  want = pat[anchor].byte;
    for (const uint8_t* i = begin; i <= last; )
    {
        // hunt the anchor byte in [i+anchor, last+anchor] -- all inside the span
        const uint8_t* hit = (const uint8_t*)memchr(i + anchor, want, (size_t)(last - i) + 1);
        if (!hit) return nullptr;
        i = hit - anchor;
        if (PatternMatchAt(i, pat, plen, anchor)) return i;
        ++i;
    }
    return nullptr;
}

// ---- anchor picking ------------------------------------------------------------------
// Any fixed token is a legal anchor (PatternMatchAt skips it and checks the rest), and
// the pick decides everything: memchr stops once per occurrence of that byte. The FIRST
// fixed byte is the worst choice -- in x64 code it is nearly always a REX prefix
// (0x48/0x4C) or 0x8B/0x89, so memchr stopped every ~20 bytes for a ~1.6x win. Rank the
// pattern's fixed bytes by how common they are in THIS module and anchor on the rarest.
// The histogram samples every 16th byte: ranking 256 values needs a distribution, not an
// exact count, so the extra pass costs a few ms against the ~22 scans it speeds up.
// Lock-free on purpose -- a torn table costs a slower anchor, never a wrong or missed hit.
struct PatternHist { const uint8_t* base; size_t sz; bool valid; uint32_t freq[256]; };
inline PatternHist g_patHist{};
inline constexpr size_t k_patHistStride = 16;

// A region both scans may read: committed, readable, not a guard page.
inline bool PatternRegionReadable(const MEMORY_BASIC_INFORMATION& mbi)
{
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE |
                                PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mbi.State == MEM_COMMIT && (mbi.Protect & kReadable) && !(mbi.Protect & PAGE_GUARD);
}

__declspec(noinline) inline void PatternBuildHist(const uint8_t* base, size_t sz)
{
    if (g_patHist.valid && g_patHist.base == base && g_patHist.sz == sz) return;
    memset(g_patHist.freq, 0, sizeof(g_patHist.freq));
    g_patHist.base  = base;
    g_patHist.sz    = sz;
    g_patHist.valid = false;
    __try {
        MEMORY_BASIC_INFORMATION mbi{};
        size_t off = 0;
        while (off < sz)
        {
            if (!VirtualQuery(base + off, &mbi, sizeof(mbi)) || mbi.RegionSize == 0) break;
            size_t regionEnd = (size_t)((const uint8_t*)mbi.BaseAddress - base) + mbi.RegionSize;
            if (regionEnd > sz) regionEnd = sz;
            if (PatternRegionReadable(mbi))
                for (size_t i = off; i < regionEnd; i += k_patHistStride)
                    ++g_patHist.freq[base[i]];
            off = regionEnd;
        }
        g_patHist.valid = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_patHist.valid = false; }
}

// Rarest fixed token, or plen when the pattern is all wildcards.
// No histogram (not built, or a different module) -> first fixed token = old behaviour.
inline size_t PatternPickAnchor(const PatternScanToken* pat, size_t plen, const uint8_t* base)
{
    const bool useHist = g_patHist.valid && g_patHist.base == base;
    size_t   anchor = plen;
    uint32_t best   = 0;
    for (size_t j = 0; j < plen; ++j)
    {
        if (pat[j].wild) continue;
        if (anchor == plen) { anchor = j; best = useHist ? g_patHist.freq[pat[j].byte] : 0; continue; }
        if (!useHist) continue;
        const uint32_t f = g_patHist.freq[pat[j].byte];
        if (f < best) { anchor = j; best = f; }
    }
    return anchor;
}

__declspec(noinline) inline uint8_t* PatternScan_SEH(
    uint8_t* base, size_t sz,
    const PatternScanToken* pat, size_t plen)
{
    uint8_t* result = nullptr;
    __try {
        const size_t anchor = PatternPickAnchor(pat, plen, base);

        MEMORY_BASIC_INFORMATION mbi{};
        size_t off = 0;
        while (off + plen <= sz)
        {
            if (!VirtualQuery(base + off, &mbi, sizeof(mbi)) || mbi.RegionSize == 0)
                break;
            size_t regionEnd = (size_t)((uint8_t*)mbi.BaseAddress - base) + mbi.RegionSize;
            if (regionEnd > sz) regionEnd = sz;
            if (PatternRegionReadable(mbi) && regionEnd >= plen)
            {
                const uint8_t* found = (anchor < plen)
                    ? PatternScanSpan(base + off, base + regionEnd, pat, plen, anchor)
                    : nullptr;
                if (!found && anchor >= plen)
                {
                    for (size_t i = off; i + plen <= regionEnd; ++i)
                        if (PatternMatchAt(base + i, pat, plen, plen)) { found = base + i; break; }
                }
                if (found) { result = const_cast<uint8_t*>(found); break; }
            }
            off = regionEnd;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

inline uint8_t* PatternScan(HMODULE mod, const char* ida_pattern)
{
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi));
    auto base = (uint8_t*)mi.lpBaseOfDll;
    size_t sz  = mi.SizeOfImage;

    PatternScanToken pat[k_patternMaxTokens];
    const size_t plen = PatternParse(ida_pattern, pat, k_patternMaxTokens);
    if (!plen || !base || !sz) return nullptr;
    PatternBuildHist(base, sz);   // once per module; later scans of it reuse the table
    return PatternScan_SEH(base, sz, pat, plen);
}

// Same scan, but null unless the pattern matches EXACTLY once in the module.
// For a hook target an extra match is survivable -- the wrong function gets a detour that
// forwards. For an address that is USED AS CODE it is not: a jump target, or an address
// compared against _ReturnAddress() to decide whether to rewrite a return slot, has to be THE
// one site. A second match means the sig stopped identifying a single place, and acting on
// whichever one the scan reached first is a jump into an unrelated function. Ambiguous ->
// null, the caller stands the feature down, and the log says which sig went soft.
// Span form -- the actual logic, so tests/crashlog_test.cpp can drive it over a buffer it
// planted itself instead of a copy of it.
inline uint8_t* PatternScanUniqueIn(uint8_t* base, size_t sz, const char* ida_pattern,
                                    bool* ambiguous = nullptr)
{
    if (ambiguous) *ambiguous = false;
    PatternScanToken pat[k_patternMaxTokens];
    const size_t plen = PatternParse(ida_pattern, pat, k_patternMaxTokens);
    if (!plen || !base || !sz) return nullptr;
    PatternBuildHist(base, sz);

    uint8_t* first = PatternScan_SEH(base, sz, pat, plen);
    if (!first) return nullptr;

    const size_t off = (size_t)(first - base) + 1;
    if (off < sz && PatternScan_SEH(base + off, sz - off, pat, plen)) {
        if (ambiguous) *ambiguous = true;
        return nullptr;
    }
    return first;
}

inline uint8_t* PatternScanUnique(HMODULE mod, const char* ida_pattern, bool* ambiguous = nullptr)
{
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi));
    return PatternScanUniqueIn((uint8_t*)mi.lpBaseOfDll, mi.SizeOfImage, ida_pattern, ambiguous);
}
