#include "gif_tex.h"
#include "gif_frame.h"
#include "wm_pure.h"   // n_wm::UrlDirect / Fnv1a
#include <gdiplus.h>
#include <urlmon.h>
#include <commdlg.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <atomic>
#include "../../globals/pinned_thread.h"

// GDI+ decode, same approach as botox's spectator_list.cpp (kamibebra_gif_data.h):
// Bitmap::FromStream on an in-memory copy, SelectActiveFrame to walk the GIF's own frame
// dimension, PropertyTagFrameDelay (centiseconds, *10 for ms) for timing.
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "comdlg32.lib")

struct GifTex
{
    IStream*           stream  = nullptr;
    Gdiplus::Bitmap*   bitmap  = nullptr;
    std::vector<uint32_t> delaysMs;
    size_t             uploaded = (size_t)-1;   // last frame index uploaded to texture
    IDirect3DTexture9* texture  = nullptr;
    bool               failed   = false;
};

static std::unordered_map<std::string, GifTex> s_cache;
static ULONG_PTR s_gdiplusToken = 0;

static bool EnsureGdiplus()
{
    if (s_gdiplusToken) return true;
    Gdiplus::GdiplusStartupInput input{};
    return Gdiplus::GdiplusStartup(&s_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

// Decodes header + delay table only -- no pixels yet, those come lazily per-frame in
// GifTex_Frame. Runs once per id, result cached whether it succeeded or not (a GIF that
// will not decode must not re-run GDI+ every call).
static bool DecodeHeader(GifTex& g, const uint8_t* data, size_t size)
{
    g.failed = true;   // one-shot unless every step below clears it

    if (!EnsureGdiplus() || !data || !size) return false;

    const HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return false;
    void* locked = GlobalLock(mem);
    if (!locked) { GlobalFree(mem); return false; }
    memcpy(locked, data, size);
    GlobalUnlock(mem);

    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &g.stream))) { GlobalFree(mem); g.stream = nullptr; return false; }

    g.bitmap = Gdiplus::Bitmap::FromStream(g.stream);
    const UINT frames = (g.bitmap && g.bitmap->GetLastStatus() == Gdiplus::Ok)
                             ? std::max(g.bitmap->GetFrameCount(&Gdiplus::FrameDimensionTime), 1u) : 0;
    if (!frames)
    {
        delete g.bitmap; g.bitmap = nullptr;
        g.stream->Release(); g.stream = nullptr;
        return false;
    }

    g.delaysMs.assign(frames, 100);
    if (const UINT propSize = g.bitmap->GetPropertyItemSize(PropertyTagFrameDelay); propSize)
    {
        std::vector<unsigned char> prop(propSize);
        auto* item = reinterpret_cast<Gdiplus::PropertyItem*>(prop.data());
        if (g.bitmap->GetPropertyItem(PropertyTagFrameDelay, propSize, item) == Gdiplus::Ok && item->value)
        {
            const UINT count = std::min(frames, (UINT)(item->length / sizeof(UINT)));
            for (UINT i = 0; i < count; ++i)
                g.delaysMs[i] = static_cast<const UINT*>(item->value)[i] * 10;
        }
    }

    g.failed = false;
    return true;
}

GifTex* GifTex_Get(const char* id, const uint8_t* data, size_t size)
{
    if (!id) return nullptr;
    auto it = s_cache.find(id);
    if (it != s_cache.end()) return &it->second;

    GifTex& g = s_cache[id];
    if (!DecodeHeader(g, data, size))
        DebugLog("gif_tex: '%s' failed to decode", id);
    return &g;
}

ImTextureID GifTex_Frame(GifTex* gif, double timeSec)
{
    if (!gif || gif->failed || !gif->bitmap || !g_d3dDev) return (ImTextureID)0;

    const size_t frame = n_gif::FrameIndexAt(gif->delaysMs, timeSec * 1000.0);

    if (frame != gif->uploaded || !gif->texture)
    {
        if (frame != gif->uploaded)
            gif->bitmap->SelectActiveFrame(&Gdiplus::FrameDimensionTime, (UINT)frame);

        const UINT w = gif->bitmap->GetWidth(), h = gif->bitmap->GetHeight();
        if (!w || !h) return (ImTextureID)0;

        if (!gif->texture)
        {
            // DYNAMIC + DEFAULT only -- MANAGED does not exist on this engine's d3d9ex device.
            if (FAILED(g_d3dDev->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                               D3DPOOL_DEFAULT, &gif->texture, nullptr)) || !gif->texture)
            {
                DebugLog("gif_tex: CreateTexture %ux%u failed", w, h);
                gif->texture = nullptr;
                return (ImTextureID)0;
            }
        }

        Gdiplus::Rect rect(0, 0, (INT)w, (INT)h);
        Gdiplus::BitmapData bits{};
        if (gif->bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bits) == Gdiplus::Ok)
        {
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(gif->texture->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
            {
                for (UINT y = 0; y < h; ++y)
                    memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch,
                           (const uint8_t*)bits.Scan0 + (size_t)y * bits.Stride, (size_t)w * 4);
                gif->texture->UnlockRect(0);
                gif->uploaded = frame;
            }
            gif->bitmap->UnlockBits(&bits);
        }
    }

    return (ImTextureID)gif->texture;
}

bool GifTex_Size(GifTex* gif, float& w, float& h)
{
    if (!gif || gif->failed || !gif->bitmap) return false;
    w = (float)gif->bitmap->GetWidth();
    h = (float)gif->bitmap->GetHeight();
    return w > 0.f && h > 0.f;
}

// ==== file / url images (botox spectator_list.cpp url_image_direct / url_image_path) ====
static std::mutex s_urlMutex;
static std::unordered_set<std::string> s_urlStarted;

// local path as is; http(s) -> C:\botox-css\images\<hash> once downloaded, "" until then
static std::string UrlPath(const std::string& setting)
{
    if (setting.rfind("http://", 0) != 0 && setting.rfind("https://", 0) != 0) return setting;

    const std::string url = n_wm::UrlDirect(setting);
    char local[64];
    snprintf(local, sizeof(local), "C:\\botox-css\\images\\%08x", n_wm::Fnv1a(url));
    const DWORD attr = GetFileAttributesA(local);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return local;

    std::lock_guard<std::mutex> lock(s_urlMutex);
    if (s_urlStarted.insert(url).second)
    {
        // pinned: an eject mid-download delays the unmap until it ends (pinned_thread.h)
        if (HANDLE h = CreatePinnedThread([url, path = std::string(local)] {
            CreateDirectoryA("C:\\botox-css", nullptr);
            CreateDirectoryA("C:\\botox-css\\images", nullptr);
            const std::string part = path + ".part";   // never decode a half-written file
            const HRESULT hr = URLDownloadToFileA(nullptr, url.c_str(), part.c_str(), 0, nullptr);
            if (SUCCEEDED(hr) && MoveFileExA(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
                DebugLog("gif_tex: downloaded %s", url.c_str());
            else { DeleteFileA(part.c_str()); DebugLog("gif_tex: download failed 0x%08X %s", (unsigned)hr, url.c_str()); }
        })) CloseHandle(h);
    }
    return {};
}

GifTex* GifTex_GetFile(const char* setting)
{
    if (!setting || !*setting) return nullptr;

    static std::string lastSetting;
    static GifTex* lastGif = nullptr;
    if (lastGif && lastSetting == setting) return lastGif;

    const std::string path = UrlPath(setting);
    if (path.empty()) return nullptr;   // download pending

    // already decoded (or failed) under this path: reuse, never re-read the file
    if (auto it = s_cache.find(path); it != s_cache.end()) { lastSetting = setting; return lastGif = &it->second; }

    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;          // not there (yet): retry next frame, nothing cached
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return nullptr;

    lastSetting = setting;
    return lastGif = GifTex_Get(path.c_str(), bytes.data(), bytes.size());
}

// ==== browse button (botox image_path_input pick_thread, no upload) ====
static std::mutex  s_pickMutex;
static std::string s_pickResult;
static ImGuiID     s_pickId = 0;
static bool        s_pickDone = false;
static std::atomic<bool> s_pickAlive{ false };

static unsigned long __stdcall PickThread(void*)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    char path[MAX_PATH] = {};
    OPENFILENAMEA dlg = {};
    dlg.lStructSize = sizeof(dlg);
    dlg.lpstrFilter = "images\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0all files\0*.*\0";
    dlg.lpstrFile   = path;
    dlg.nMaxFile    = MAX_PATH;
    dlg.lpstrTitle  = "pick image";
    dlg.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool picked = GetOpenFileNameA(&dlg) && !g_eject.load();
    {
        std::lock_guard<std::mutex> lock(s_pickMutex);
        s_pickResult = picked ? path : "";
        s_pickDone = true;
    }
    if (SUCCEEDED(com)) CoUninitialize();
    s_pickAlive = false;
    return 0;
}

void GifTex_PathInput(const char* label, const char* hint, char* buf, size_t bufSize)
{
    const ImGuiID id = ImGui::GetID(label);
    {
        std::lock_guard<std::mutex> lock(s_pickMutex);
        if (s_pickDone && s_pickId == id)
        {
            if (!s_pickResult.empty()) strncpy_s(buf, bufSize, s_pickResult.c_str(), _TRUNCATE);
            s_pickDone = false;
            s_pickId = 0;
        }
    }

    ImGui::InputTextWithHint(label, hint, buf, bufSize);

    const bool busy = s_pickAlive.load();
    ImGui::BeginDisabled(busy);
    char button[128];
    snprintf(button, sizeof(button), "browse##pick %s", label);
    if (ImGui::Button(button, ImVec2(-1.f, 15.f)) && !busy)
    {
        {
            std::lock_guard<std::mutex> lock(s_pickMutex);
            s_pickId = id;
            s_pickDone = false;
            s_pickResult.clear();
        }
        s_pickAlive = true;
        if (HANDLE h = CreatePinnedThread([] { PickThread(nullptr); })) CloseHandle(h);
        else s_pickAlive = false;
    }
    ImGui::EndDisabled();

    if (busy)
    {
        std::lock_guard<std::mutex> lock(s_pickMutex);
        if (s_pickId == id) ImGui::Label("%s", "picking...");
    }
}

void GifTex_ReleaseAll()
{
    for (auto& kv : s_cache)
    {
        GifTex& g = kv.second;
        if (g.texture) { g.texture->Release(); g.texture = nullptr; }
        g.uploaded = (size_t)-1;   // forces a fresh LockBits+upload next GifTex_Frame
    }
}
