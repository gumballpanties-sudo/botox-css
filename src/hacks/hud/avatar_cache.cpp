#include "avatar_cache.h"
#include "avatar_pure.h"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>

// Steam interface fetch -- same approach scoreboard.cpp's GetSteamFriends() already uses
// (verified: that flat-export path is what resolves ISteamFriends slot 30 there). Duplicated
// rather than exported: each is a handful of lines, and a header shared for two private
// helpers is the speculative abstraction the project avoids.
static void* GetSteamFriends()
{
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) h = GetModuleHandleA("steam_api.dll");
    if (!h) return nullptr;
    if (auto pfn = (void* (*)())GetProcAddress(h, "SteamAPI_SteamFriends_v017"))
        if (void* p = pfn()) return p;
    using FindOrCreate_t = void* (*)(int, const char*);
    auto pfnFOC  = (FindOrCreate_t)GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface");
    auto pfnUser = (int (*)())GetProcAddress(h, "SteamAPI_GetHSteamUser");
    if (pfnFOC && pfnUser)
        if (void* p = pfnFOC(pfnUser(), "SteamFriends017")) return p;
    return nullptr;
}

static void* GetSteamUtils()
{
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) h = GetModuleHandleA("steam_api.dll");
    if (!h) return nullptr;
    if (auto pfn = (void* (*)())GetProcAddress(h, "SteamAPI_SteamUtils_v010"))
        if (void* p = pfn()) return p;
    using FindOrCreate_t = void* (*)(int, const char*);
    auto pfnFOC  = (FindOrCreate_t)GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface");
    auto pfnUser = (int (*)())GetProcAddress(h, "SteamAPI_GetHSteamUser");
    if (pfnFOC && pfnUser)
        if (void* p = pfnFOC(pfnUser(), "SteamUtils010")) return p;
    return nullptr;
}

// Flat C exports instead of vtable slots: hand-counting ISteamFriends/ISteamUtils past the
// SDK's documented layout is how the old slots 34/35/37/5/6 ended up unverifiable. steam_api64.dll
// exports a self-pointer flat wrapper per interface method (SteamAPI_<Interface>_<Method>), so
// resolve those by name instead -- wrong slot can't compile into a crash if there's no slot.
// Verified present in TF2's real steam_api64.dll (bin/x64/, Steam\steamapps\common\Team Fortress 2):
// SteamAPI_ISteamFriends_GetSmallFriendAvatar / GetMediumFriendAvatar / RequestUserInformation,
// SteamAPI_ISteamUtils_GetImageSize / GetImageRGBA (export dump, 2026-10-30).
template <typename Fn>
static Fn ResolveExport(const char* name)
{
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) h = GetModuleHandleA("steam_api.dll");
    return h ? (Fn)GetProcAddress(h, name) : nullptr;
}

using GetAvatar_t    = int  (*)(void*, uint64_t);
using RequestInfo_t  = bool (*)(void*, uint64_t, bool);
using GetImageSize_t = bool (*)(void*, int, uint32_t*, uint32_t*);
using GetImageRGBA_t = bool (*)(void*, int, uint8_t*, int);

static uint64_t CacheKey(uint64_t steamid64, bool medium) { return (steamid64 << 1) | (medium ? 1ull : 0ull); }

struct AvatarEntry
{
    IDirect3DTexture9* tex    = nullptr;
    int                handle = 0;
};
static std::unordered_map<uint64_t, AvatarEntry> s_cache;   // render thread only

struct WantedFetch { uint64_t steamid64; bool medium; };
static std::mutex              s_wantMtx;
static std::vector<WantedFetch> s_wanted;   // render thread writes (arm), game thread drains

struct PendingUpload
{
    uint64_t key;
    int      handle;
    int      width, height;
    std::vector<uint8_t> rgba;
};
static std::mutex                s_uploadMtx;
static std::vector<PendingUpload> s_pendingUploads;   // game thread writes, render thread drains

static std::unordered_map<uint64_t, unsigned long long> s_nextTry;   // game thread only
static std::atomic<bool> s_mapChanged{ false };

ImTextureID Avatar_Get(uint64_t steamid64, bool medium)
{
    if (!steamid64) return (ImTextureID)0;

    const uint64_t key = CacheKey(steamid64, medium);
    auto it = s_cache.find(key);
    if (it != s_cache.end() && it->second.tex) return (ImTextureID)it->second.tex;

    std::lock_guard<std::mutex> lock(s_wantMtx);
    if (s_wanted.size() < 64)   // ponytail: fixed per-frame cap, bump if a lobby ever arms more live requests
        s_wanted.push_back({ steamid64, medium });
    return (ImTextureID)0;
}

void Avatar_Pump()
{
    std::vector<WantedFetch> wanted;
    {
        std::lock_guard<std::mutex> lock(s_wantMtx);
        if (s_wanted.empty()) return;
        wanted.swap(s_wanted);
    }

    void* friends = GetSteamFriends();
    void* utils   = GetSteamUtils();
    if (!friends || !utils) return;

    static GetAvatar_t    pfnSmall   = ResolveExport<GetAvatar_t>("SteamAPI_ISteamFriends_GetSmallFriendAvatar");
    static GetAvatar_t    pfnMedium  = ResolveExport<GetAvatar_t>("SteamAPI_ISteamFriends_GetMediumFriendAvatar");
    static RequestInfo_t  pfnReqInfo = ResolveExport<RequestInfo_t>("SteamAPI_ISteamFriends_RequestUserInformation");
    static GetImageSize_t pfnImgSize = ResolveExport<GetImageSize_t>("SteamAPI_ISteamUtils_GetImageSize");
    static GetImageRGBA_t pfnImgRGBA = ResolveExport<GetImageRGBA_t>("SteamAPI_ISteamUtils_GetImageRGBA");
    if (!pfnSmall || !pfnMedium || !pfnReqInfo || !pfnImgSize || !pfnImgRGBA) return;

    const unsigned long long now = GetTickCount64();

    for (const WantedFetch& w : wanted)
    {
        const uint64_t key = CacheKey(w.steamid64, w.medium);

        auto nt = s_nextTry.find(key);
        if (nt != s_nextTry.end() && now < nt->second) continue;
        s_nextTry[key] = now + 1000ull;

        const int handle = (w.medium ? pfnMedium : pfnSmall)(friends, w.steamid64);
        if (handle <= 0)
        {
            // Not downloaded yet -- ask, and pick it up on a later Pump once it lands.
            pfnReqInfo(friends, w.steamid64, false);
            continue;
        }

        uint32_t width = 0, height = 0;
        if (!pfnImgSize(utils, handle, &width, &height) ||
            !width || !height || width > 256 || height > 256)
            continue;

        PendingUpload up;
        up.key = key; up.handle = handle; up.width = (int)width; up.height = (int)height;
        up.rgba.resize((size_t)width * height * 4);
        if (!pfnImgRGBA(utils, handle, up.rgba.data(), (int)up.rgba.size()))
            continue;

        std::lock_guard<std::mutex> lock(s_uploadMtx);
        s_pendingUploads.push_back(std::move(up));
    }
}

void Avatar_ReleaseAll()
{
    for (auto& kv : s_cache)
        if (kv.second.tex) { kv.second.tex->Release(); kv.second.tex = nullptr; }
    s_cache.clear();
}

void Avatar_OnMapChange() { s_mapChanged = true; }

void Avatar_Tick(IDirect3DDevice9* dev)
{
    if (!dev) return;
    if (s_mapChanged.exchange(false)) Avatar_ReleaseAll();

    std::vector<PendingUpload> ups;
    {
        std::lock_guard<std::mutex> lock(s_uploadMtx);
        if (s_pendingUploads.empty()) return;
        ups.swap(s_pendingUploads);
    }

    for (PendingUpload& up : ups)
    {
        n_avatar::RgbaToBgraInPlace(up.rgba.data(), (size_t)up.width * up.height);

        AvatarEntry& e = s_cache[up.key];
        if (e.tex) { e.tex->Release(); e.tex = nullptr; }

        IDirect3DTexture9* tex = nullptr;
        // DYNAMIC + DEFAULT only -- MANAGED does not exist on this engine's d3d9ex device.
        HRESULT hr = dev->CreateTexture((UINT)up.width, (UINT)up.height, 1, D3DUSAGE_DYNAMIC,
                                        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
        if (FAILED(hr) || !tex)
        {
            DebugLog("avatar: CreateTexture %dx%d failed hr=0x%08X", up.width, up.height, (unsigned)hr);
            continue;
        }

        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
        {
            for (int y = 0; y < up.height; ++y)
                memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch,
                       up.rgba.data() + (size_t)y * up.width * 4, (size_t)up.width * 4);
            tex->UnlockRect(0);
        }

        e.tex    = tex;
        e.handle = up.handle;
    }
}
