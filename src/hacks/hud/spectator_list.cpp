#include "spectator_list.h"
#include "../../render/render.h"   // RenderFadedGradientLine
#include "../../render/dpi.h"      // botox stretch blocks + panel mouse (dpi scale panels)
#include "watermark_styles.h"      // Watermark_GamesenseFrame
#include "mediaplayer.h"           // MediaPlayer_HasSession (illusory track line)
#include "../misc/unitE_logic.h"
#include "avatar_cache.h"
#include "avatar_pure.h"   // n_avatar::FriendIdToSteam64
#include "spec_pure.h"     // Utf8Truncate, UrlDirect, UrlHash, MakeEyeLayout (tests/specpure_test.cpp)
#include "gif_tex.h"
#include "../screen/blur_rect.h"
#include "../menu/menu.h"          // ImGui::Label (path input status)
#include "../../dependencies/data/kamibebra_gif_data.h"   // kamibebra spectator-list gifs
#include "../../dependencies/data/airflow_data.h"        // airflow spectator-list icon
#include "../../dependencies/imgui/imgui_internal.h"     // GImGui->HoveredWindow, ImBezierCubicCalc
#include <commdlg.h>
#include <urlmon.h>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cctype>
#include <cmath>
#include "../../globals/pinned_thread.h"

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "comdlg32.lib")

// botox hacks/misc/spectator_list/spectator_list.cpp, ported style by style. Donor ImGui windows
// (default, winxp, clarity, chillware, lobotomy, kamibebra, eyes, clarity v2) and drag_placed_panel
// styles both land on AnchorPos (g_specPos + the SpecDrag offset) on the background draw list, so
// one drag + one saved position covers every style. chillware v2 stays a real ImGui window.

// ==== netvars (self-resolved, read-only, never retried once found or ruled out) ===========
static int  s_offObsMode   = 0;   // 0 unresolved, -1 not found, >0 offset
static int  s_offObsTarget = 0;

static void ResolveSpecNetvars()
{
    if (s_offObsMode != 0 && s_offObsTarget != 0) return;
    RecvTable* t = FindNetworkTable("DT_BasePlayer");
    if (!t) return;
    const int m = FindPropOffset(t, "m_iObserverMode");
    const int h = FindPropOffset(t, "m_hObserverTarget");
    s_offObsMode   = m > 0 ? m : -1;
    s_offObsTarget = h > 0 ? h : -1;
    DebugLog("spectator list: m_iObserverMode=0x%X m_hObserverTarget=0x%X lifeState=0x%X", s_offObsMode, s_offObsTarget, g_offLifeState);
}

// IVEngineClient::GetPlayerInfo, slot 8 -- same layout scoreboard.cpp already confirmed and reads.
struct SpecPlayerInfo_t
{
    char     name[32];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[32];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};

// shared/shareddefs.h (sdk 2013, TF2): POI sits between CHASE and ROAMING, unlike CS:GO
enum { OBS_NONE = 0, OBS_DEATHCAM, OBS_FREEZECAM, OBS_FIXED, OBS_IN_EYE, OBS_CHASE, OBS_POI, OBS_ROAMING };

// SEH leaf, no destructible locals (C2712). life = m_lifeState (0 = LIFE_ALIVE).
static __declspec(noinline) bool ReadObsState(void* pEnt, int& outMode, unsigned int& outHandle, int& outLife)
{
    __try {
        const uintptr_t b = reinterpret_cast<uintptr_t>(pEnt);
        outMode   = *reinterpret_cast<int*>(b + s_offObsMode);
        outHandle = *reinterpret_cast<unsigned int*>(b + s_offObsTarget);
        outLife   = g_offLifeState > 0 ? *reinterpret_cast<uint8_t*>(b + g_offLifeState) : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static __declspec(noinline) bool ReadLife(void* pEnt, int& outLife)
{
    __try { outLife = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(pEnt) + g_offLifeState); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// botox spectator_entry_t
struct SpecEntry
{
    std::string name, target, mode;
    int  index = 0, targetIndex = 0, obsMode = 0;
    bool watchingLocal = false, fake = false, targetFake = false, hltv = false;
    uint64_t sid = 0, targetSid = 0;
};

static bool PlayerInfo(int idx, SpecPlayerInfo_t& pi)
{
    pi = {};
    return g_engine && VCall<bool>(g_engine, 8, idx, &pi);
}

static uint64_t SteamOf(const SpecPlayerInfo_t& pi)
{
    return (!pi.fakeplayer && pi.friendsID) ? n_avatar::FriendIdToSteam64(pi.friendsID) : 0;
}

// botox collect_spectators' scan, once per frame: every dead player whose observer target is a live
// player (local included when dead -- the donor never skips it either). No dormancy check: this TU
// has no confirmed IClientNetworkable::IsDormant call path (aim TU's SDK can't be included here).
static std::vector<SpecEntry> s_raw;
static int s_rawFrame = -1;

static const std::vector<SpecEntry>& RawEntries()
{
    if (s_rawFrame == ImGui::GetFrameCount()) return s_raw;
    s_rawFrame = ImGui::GetFrameCount();
    s_raw.clear();
    if (!IsInGame()) return s_raw;
    ResolveSpecNetvars();
    if (s_offObsMode < 0 || s_offObsTarget < 0 || g_offLifeState <= 0) return s_raw;

    const int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return s_raw;

    for (int i = 1; i <= 64; ++i)
    {
        void* pEnt = GetClientEntity(i);
        if (!pEnt) continue;
        int mode = 0, life = 0;
        unsigned int handle = 0xFFFFFFFFu;
        if (!ReadObsState(pEnt, mode, handle, life) || life == 0) continue;   // alive = not a spectator

        const int target = n_unitE::DecodeEHandleIndex(handle);
        if (target < 1 || target > 64) continue;
        void* pTarget = GetClientEntity(target);
        int targetLife = 1;
        if (!pTarget || !ReadLife(pTarget, targetLife) || targetLife != 0) continue;

        SpecPlayerInfo_t pi, ti;
        if (!PlayerInfo(i, pi)) continue;
        PlayerInfo(target, ti);

        SpecEntry e;
        e.name          = pi.name;
        e.target        = ti.name;
        e.index         = i;
        e.targetIndex   = target;
        e.obsMode       = mode;
        e.watchingLocal = target == localIdx;
        e.fake          = pi.fakeplayer;
        e.targetFake    = ti.fakeplayer;
        e.hltv          = pi.ishltv;
        e.sid           = SteamOf(pi);
        e.targetSid     = SteamOf(ti);
        e.mode          = mode == OBS_CHASE ? "3rd" : "1st";
        s_raw.push_back(std::move(e));
    }
    return s_raw;
}

// botox collect_spectators( out, all_players, keep_hltv ): listed players combo applies unless all_players
static void Collect(std::vector<SpecEntry>& out, bool allPlayers = false, bool keepHltv = false)
{
    out.clear();
    const bool localOnly = !allPlayers && g_specListType == 1;
    for (const SpecEntry& e : RawEntries()) {
        if (localOnly && !e.watchingLocal) continue;
        if (e.hltv && !keepHltv) continue;   // SourceTV sits on the server as a dead player watching everyone
        out.push_back(e);
    }
}

static void SortByIndex(std::vector<SpecEntry>& v)
{
    std::sort(v.begin(), v.end(), [](const SpecEntry& a, const SpecEntry& b) { return a.index < b.index; });
}

static void EraseNotLocal(std::vector<SpecEntry>& v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](const SpecEntry& e) { return !e.watchingLocal; }), v.end());
}

// player the dead local is spectating (botox local->get_observer_target_handle), -1 none
static int LocalObserverTarget()
{
    void* pLocal = GetClientEntity(GetLocalPlayerIndex());
    if (!pLocal || s_offObsMode <= 0) return -1;
    int mode = 0, life = 0;
    unsigned int handle = 0xFFFFFFFFu;
    if (!ReadObsState(pLocal, mode, handle, life)) return -1;
    const int t = n_unitE::DecodeEHandleIndex(handle);
    return (t >= 1 && t <= 64 && GetClientEntity(t)) ? t : -1;
}

// ==== shared helpers ==========================================================================
static ImTextureID Avatar(uint64_t sid) { return sid ? Avatar_Get(sid, false) : (ImTextureID)0; }

static std::string RowText(const SpecEntry& e) { return g_specShowTarget ? e.name + " -> " + e.target : e.name; }

using n_spec::Utf8Truncate;

static std::string Ellipsize(ImFont* f, float size, const std::string& text, float maxW)
{
    if (maxW <= 0.f || text.empty()) return {};
    if (f->CalcTextSizeA(size, FLT_MAX, 0.f, text.c_str()).x <= maxW) return text;
    const float ew = f->CalcTextSizeA(size, FLT_MAX, 0.f, "...").x;
    std::string kept;
    for (size_t off = 0; off < text.size();) {
        const unsigned char b = (unsigned char)text[off];
        size_t len = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 1;
        len = (std::min)(len, text.size() - off);
        const std::string cand = kept + text.substr(off, len);
        if (f->CalcTextSizeA(size, FLT_MAX, 0.f, cand.c_str()).x + ew > maxW) break;
        kept = cand;
        off += len;
    }
    return kept.empty() ? std::string("...") : kept + "...";
}

static float TextW(ImFont* f, const std::string& s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s.c_str()).x; }
static float TextWs(ImFont* f, float size, const std::string& s) { return f->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str()).x; }
static float ImguiTextW(ImFont* f, const char* s) { return IM_FLOOR(f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s).x + 0.99999f); }
static float VguiTextW(ImFont* f, const std::string& s) { return floorf(TextW(f, s)); }

// c_color::get_u32( alpha ) on a float[4]
static ImU32 U32(const float c[4], float a = 1.f)
{
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), (int)(c[3] * ImSaturate(a) * 255.f));
}
static ImU32 AccentU32(int a)
{
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
    return IM_COL32((int)(acc.x * 255.f), (int)(acc.y * 255.f), (int)(acc.z * 255.f), a);
}
static ImU32 WithAlpha(ImU32 c, float a)
{
    const ImU32 s = (ImU32)((float)((c >> IM_COL32_A_SHIFT) & 0xffu) * ImSaturate(a));
    return (c & ~IM_COL32_A_MASK) | (s << IM_COL32_A_SHIFT);
}

static void OutlinedText(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* text)
{
    constexpr ImVec2 off[8] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
    for (auto& o : off) dl->AddText(f, size, ImVec2(p.x + o.x, p.y + o.y), IM_COL32(0, 0, 0, 255), text);
    dl->AddText(f, size, p, col, text);
}

static void VguiShadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* text)
{
    const ImVec2 at(floorf(pos.x), floorf(pos.y));
    dl->AddText(f, f->FontSize, ImVec2(at.x + 1.f, at.y + 1.f), col & IM_COL32_A_MASK, text);
    dl->AddText(f, f->FontSize, at, col, text);
}

static ImFont* Font(ImFont* f) { return f ? f : ImGui::GetFont(); }

static void AnchorPos(ImGuiIO& io, float w, float h, int pos, float margin, float& x, float& y)
{
    x = margin; y = margin;
    switch (pos) {
    case WM_TOP_LEFT:      x = margin;                        y = margin; break;
    case WM_TOP_CENTER:    x = (io.DisplaySize.x - w) * 0.5f; y = margin; break;
    case WM_TOP_RIGHT:     x = io.DisplaySize.x - w - margin; y = margin; break;
    case WM_BOTTOM_LEFT:   x = margin;                        y = io.DisplaySize.y - h - margin; break;
    case WM_BOTTOM_CENTER: x = (io.DisplaySize.x - w) * 0.5f; y = io.DisplaySize.y - h - margin; break;
    case WM_BOTTOM_RIGHT:  x = io.DisplaySize.x - w - margin; y = io.DisplaySize.y - h - margin; break;
    }
    // drag offset (SpecDrag below), kept on screen so a stale offset can't lose the panel
    x = fmaxf(0.f, fminf(x + g_specOffX, io.DisplaySize.x - w));
    y = fmaxf(0.f, fminf(y + g_specOffY, io.DisplaySize.y - h));
    x = floorf(x); y = floorf(y);
}

// botox drag_placed_panel + list_x/list_y read, in one
static void Place(float w, float h, float& x, float& y) { AnchorPos(ImGui::GetIO(), w, h, g_specPos, 4.f, x, y); }

// ==== file / url images (botox file_image_frame + url_image_path) =============================
// local path -> read as is; http(s) -> C:\botox-css\images\<fnv1a> downloaded once off the render
// thread, empty until then. Decoded + uploaded by gif_tex (one cache entry per resolved path).
static std::mutex s_urlMutex;
static std::vector<std::string> s_urlStarted;

static std::string UrlImagePath(const std::string& setting)
{
    if (setting.rfind("http://", 0) != 0 && setting.rfind("https://", 0) != 0) return setting;
    const std::string url = n_spec::UrlDirect(setting);
    const uint32_t hash = n_spec::UrlHash(url);
    char local[64];
    snprintf(local, sizeof(local), "C:\\botox-css\\images\\%08x", hash);
    const DWORD attr = GetFileAttributesA(local);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return local;

    std::lock_guard<std::mutex> lock(s_urlMutex);
    if (std::find(s_urlStarted.begin(), s_urlStarted.end(), url) == s_urlStarted.end()) {
        s_urlStarted.push_back(url);
        // pinned: an eject mid-download delays the unmap until it ends (pinned_thread.h)
        if (HANDLE h = CreatePinnedThread([url, path = std::string(local)] {
            CreateDirectoryA("C:\\botox-css", nullptr);
            CreateDirectoryA("C:\\botox-css\\images", nullptr);
            const HRESULT hr = URLDownloadToFileA(nullptr, url.c_str(), path.c_str(), 0, nullptr);
            DebugLog("spectator list: download %s -> %s hr=0x%08X", url.c_str(), path.c_str(), (unsigned)hr);
        })) CloseHandle(h);
    }
    return {};
}

struct FileImage { std::string setting, path; GifTex* gif = nullptr; };

static ImTextureID FileImageFrame(FileImage& img, const char* setting)
{
    // re-resolve only on a new setting or while a download is pending
    const std::string path = (img.setting == setting && !img.path.empty()) ? img.path : UrlImagePath(setting);
    img.setting = setting;
    if (path != img.path) {
        img.path = path;
        img.gif = nullptr;
        std::vector<uint8_t> bytes;
        if (!path.empty()) {
            std::ifstream file(path, std::ios::binary);
            if (file) bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        if (!bytes.empty()) img.gif = GifTex_Get(("file:" + path).c_str(), bytes.data(), bytes.size());
    }
    return img.gif ? GifTex_Frame(img.gif, ImGui::GetTime()) : (ImTextureID)0;
}

static FileImage s_lumiMascot, s_kamibebraGif;

// ==== botox image_path_input: text box + browse button ========================================
static std::mutex s_pickMutex;
static std::atomic<bool> s_pickAlive{ false };
static ImGuiID s_pickId = 0;
static std::string s_pickResult, s_pickStatus;
static bool s_pickDone = false;

void SpecList_PathInput(const char* label, const char* hint, char* buf, size_t size)
{
    const ImGuiID id = ImGui::GetID(label);
    std::string status;
    {
        std::lock_guard<std::mutex> lock(s_pickMutex);
        if (s_pickId == id) {
            status = s_pickStatus;
            if (s_pickDone) {
                if (!s_pickResult.empty()) strncpy_s(buf, size, s_pickResult.c_str(), _TRUNCATE);
                s_pickResult.clear();
                s_pickDone = false;
                s_pickStatus.clear();
                status.clear();
            }
        }
    }

    ImGui::InputTextWithHint(label, hint, buf, size);

    const bool busy = s_pickAlive;
    ImGui::BeginDisabled(busy);
    char button[192];
    snprintf(button, sizeof(button), "browse##pick %s", label);
    if (ImGui::Button(button, ImVec2(-1.f, 15.f)) && !busy) {
        {
            std::lock_guard<std::mutex> lock(s_pickMutex);
            s_pickId = id;
            s_pickStatus = "picking...";
            s_pickResult.clear();
            s_pickDone = false;
        }
        s_pickAlive = true;   // before the thread runs
        // pinned: an eject while the dialog is open unmaps once it closes (pinned_thread.h)
        HANDLE h = CreatePinnedThread([] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            char path[MAX_PATH] = {};
            OPENFILENAMEA dlg = {};
            dlg.lStructSize = sizeof(dlg);
            dlg.lpstrFilter = "images\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0all files\0*.*\0";
            dlg.lpstrFile   = path;
            dlg.nMaxFile    = MAX_PATH;
            dlg.lpstrTitle  = "pick image";
            dlg.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            const bool picked = GetOpenFileNameA(&dlg) != FALSE;
            {
                std::lock_guard<std::mutex> lock(s_pickMutex);
                s_pickResult = picked ? path : "";
                s_pickDone = true;
            }
            if (SUCCEEDED(com)) CoUninitialize();
            s_pickAlive = false;
        });
        if (h) CloseHandle(h); else s_pickAlive = false;
    }
    ImGui::EndDisabled();

    if (!status.empty()) ImGui::Label("%s", status.c_str());
}

// ==== style 0, list type 1: botox draw_spectating_local =======================================
static const char* SpecTypeName(int obs)
{
    switch (obs) {
    case OBS_DEATHCAM:  return "deathcam";
    case OBS_FREEZECAM: return "freezecam";
    case OBS_IN_EYE:    return "first person";
    case OBS_CHASE:     return "3rd person";
    case OBS_ROAMING:   return "roaming";
    case OBS_FIXED:     return "fixed";
    default:            return "first person";
    }
}

static void DrawSpectatingLocal(ImDrawList* dl)
{
    static std::vector<int> order;
    std::vector<SpecEntry> data;
    for (const SpecEntry& e : RawEntries())
        if (e.watchingLocal && !e.hltv) data.push_back(e);
    if (data.empty()) { order.clear(); return; }

    order.erase(std::remove_if(order.begin(), order.end(), [&](int idx) {
        return std::none_of(data.begin(), data.end(), [&](const SpecEntry& e) { return e.index == idx; }); }), order.end());
    for (const SpecEntry& e : data)
        if (std::find(order.begin(), order.end(), e.index) == order.end()) order.push_back(e.index);

    ImFont* f = Font(g_fontTahomaBd12);
    const bool avatar = g_specAvatar;
    constexpr float avatarSize = 14.f;
    const ImU32 col = U32(g_specColorOne), shadow = IM_COL32(0, 0, 0, (int)(g_specColorOne[3] * 255.f));
    float y = 5.f;
    for (const int idx : order) {
        auto it = std::find_if(data.begin(), data.end(), [&](const SpecEntry& e) { return e.index == idx; });
        if (it == data.end()) continue;
        const std::string text = Utf8Truncate(it->name, 12) + "... | " + SpecTypeName(it->obsMode);
        if (avatar)
            if (ImTextureID t = Avatar(it->sid)) dl->AddImage(t, ImVec2(10.f, y), ImVec2(10.f + avatarSize, y + avatarSize));
        const float tx = avatar ? 27.f : 10.f;
        dl->AddText(f, f->FontSize, ImVec2(tx + 1.f, y + 1.f), shadow, text.c_str());
        dl->AddText(f, f->FontSize, ImVec2(tx, y), col, text.c_str());
        y += 15.f;
    }
}

// ==== style 0: botox draw_spectator_list =======================================================
// window bg #0A0A0A rounded, 25px #191919 header, accent faded line, verdana bold title, (50,50,50)
// border inset 1px, rows slide in 10px + fade at 6/s, whole panel fades at 4/s.
static void DrawDefault(ImDrawList* dl, ImGuiIO& io)
{
    struct Row { std::string text; bool local = false; int index = 0; uint64_t sid = 0; float progress = 0.f; bool seen = false, removing = false; };
    static std::vector<Row> rows;
    static float animW = 0.f, targetW = 0.f, panel = 0.f;
    static bool snap = true;

    std::vector<SpecEntry> entries;
    for (const SpecEntry& e : RawEntries()) if (!e.hltv) entries.push_back(e);

    if (rows.empty()) snap = true;
    for (auto& r : rows) r.seen = false;
    for (const SpecEntry& e : entries) {
        const std::string text = Utf8Truncate(e.name, 24) + " -> " + Utf8Truncate(e.target, 24);
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.index == e.index; });
        if (it == rows.end()) { rows.push_back({ text, e.watchingLocal, e.index, e.sid, 0.f, true, false }); continue; }
        it->text = text; it->local = e.watchingLocal; it->sid = e.sid; it->seen = true; it->removing = false;
    }
    const float dt = io.DeltaTime;
    for (auto& r : rows) {
        if (!r.seen) r.removing = true;
        r.progress = std::clamp(r.progress + dt * 6.f * (r.removing ? -1.f : 1.f), 0.f, 1.f);
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return r.removing && r.progress <= 0.f; }), rows.end());

    const bool alwaysShow = g_specAlwaysShow;
    panel = std::clamp(panel + dt * 4.f * ((alwaysShow || !rows.empty()) ? 1.f : -1.f), 0.f, 1.f);
    if (panel < 0.02f) { snap = true; return; }

    ImFont* fT = Font(g_menuBold);   // verdana bold 11
    ImFont* fR = Font(g_menuReg);    // verdana 11 = botox default font
    const ImGuiStyle& st = ImGui::GetStyle();
    constexpr float headerH = 25.f, padX = 8.f, bottomPad = 2.f, slide = 10.f, avatarSize = 14.f;
    const bool drawAvatar = g_specAvatar;
    const float textH = fR->FontSize;
    const float contentH = drawAvatar ? (std::max)(avatarSize, textH) : textH;
    const float rowH = contentH + st.ItemSpacing.y;
    const float textOffX = drawAvatar ? avatarSize + st.ItemSpacing.x : 0.f;

    const ImVec2 titleSz = fT->CalcTextSizeA(fT->FontSize, FLT_MAX, 0.f, "spectators");
    float widest = 0.f, listH = 0.f;
    for (auto& r : rows) {
        listH += rowH * r.progress;
        widest = (std::max)(widest, textOffX + TextW(fR, r.text));
    }
    if (!rows.empty() || alwaysShow) {
        targetW = (std::max)(titleSz.x + 40.f, widest + padX * 2.f);
        animW = snap ? targetW : ImLerp(animW, targetW, ImSaturate(dt * 12.f));
        snap = false;
    }
    const float h = headerH + bottomPad + listH + bottomPad;
    float x, y; AnchorPos(io, animW, h, g_specPos, 2.f, x, y);
    const float rnd = st.WindowRounding;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + animW, y + h), IM_COL32(10, 10, 10, (int)(255.f * panel)), rnd);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + animW, y + headerH), IM_COL32(25, 25, 25, (int)(255.f * panel)), rnd, ImDrawFlags_RoundCornersTop);
    const ImColor acc = ImGui::GetColorU32(ImGuiCol_Accent);
    RenderFadedGradientLine(dl, ImVec2(x, y + headerH - 1.f), ImVec2(animW, 1.f), ImColor(acc.Value.x, acc.Value.y, acc.Value.z, panel));
    dl->AddText(fT, fT->FontSize, ImVec2(x + (animW - titleSz.x) * 0.5f, y + (headerH - titleSz.y) * 0.5f), ImColor(1.f, 1.f, 1.f, panel), "spectators");
    dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(x + animW - 1.f, y + h - 1.f), IM_COL32(50, 50, 50, (int)(255.f * panel)), rnd);

    const float blockX = (std::max)(padX, (animW - widest) * 0.5f);
    float rowY = headerH + bottomPad;
    for (auto& r : rows) {
        const float rh = rowH * r.progress;
        if (r.progress > 0.f) {
            const float alpha = panel * r.progress;
            const float rowX = x + blockX - (1.f - r.progress) * slide;
            const float contentY = y + rowY + (rh - contentH) * 0.5f;
            dl->PushClipRect(ImVec2(x, y + rowY), ImVec2(x + animW, y + rowY + rh), true);
            if (drawAvatar)
                if (ImTextureID t = Avatar(r.sid)) {
                    const float ay = contentY + (contentH - avatarSize) * 0.5f;
                    dl->AddImageRounded(t, ImVec2(rowX, ay), ImVec2(rowX + avatarSize, ay + avatarSize), ImVec2(0, 0), ImVec2(1, 1), ImColor(1.f, 1.f, 1.f, alpha), 6.f);
                }
            const float* c = r.local ? g_specColorOne : g_specColorTwo;
            dl->AddText(fR, fR->FontSize, ImVec2(rowX + textOffX, contentY + (contentH - textH) * 0.5f), ImColor(c[0], c[1], c[2], c[3] * alpha), r.text.c_str());
            dl->PopClipRect();
        }
        rowY += rh;
    }
}

// style 1: delusional
static void DrawDelusional(ImDrawList* dl, ImGuiIO& io)
{
    std::vector<SpecEntry> entries; Collect(entries);
    if (entries.empty()) return;
    ImFont* font = Font(g_menuBold);   // verdana_bd_11
    const float fs = font->FontSize;
    const bool left = g_specDelusionalLeft;
    float y = 5.f;
    if (!left && g_wmEnable) y += 6.f + 19.f;
    const ImU32 local = ImGui::GetColorU32(ImGuiCol_Accent);
    const bool drawAvatar = g_specAvatar;
    constexpr float avatarSize = 14.f, avatarGap = 4.f;
    const float block = drawAvatar ? avatarSize + avatarGap : 0.f;
    int row = 0;
    for (const SpecEntry& e : entries) {
        const std::string text = RowText(e);
        const float tw = TextWs(font, fs, text);
        const float rx = left ? 6.f + block : io.DisplaySize.x - 6.f - tw;
        const float ry = y + 16.f * row++;
        if (drawAvatar)
            if (ImTextureID t = Avatar(e.sid)) dl->AddImage(t, ImVec2(rx - block, ry), ImVec2(rx - block + avatarSize, ry + avatarSize));
        dl->AddText(font, fs, ImVec2(rx + 1.f, ry + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
        dl->AddText(font, fs, ImVec2(rx, ry), e.watchingLocal ? local : IM_COL32(255, 255, 255, 255), text.c_str());
    }
}

// style 2: interwebz
static void DrawInterwebz(ImDrawList* dl)
{
    std::vector<SpecEntry> entries; Collect(entries);
    const float w = (float)g_specListWidth;
    const int count = (int)entries.size();
    const float panelH = 20.f + count * 20.f;
    float x, y; Place(w + 20.f, panelH, x, y);
    ImFont* title = Font(g_fontTahoma12);
    ImFont* rowF  = Font(g_menuBold);

    if (g_specInterwebzBg) {
        for (int i = 1; i < 20; i++)
            dl->AddLine(ImVec2(x + i + 1.f, y + 20.f - i), ImVec2(x + i + 1.f + w - 1.f, y + 20.f - i), IM_COL32(59, 59, 59, 255));
        for (int i = 1; i <= count; i++)
            dl->AddRectFilled(ImVec2(x, y + i * 20.f), ImVec2(x + w, y + i * 20.f + 20.f), i % 2 == 0 ? IM_COL32(75, 75, 75, 255) : IM_COL32(45, 45, 45, 255));
        constexpr ImU32 frame = IM_COL32(85, 85, 85, 255);
        dl->AddLine(ImVec2(x + 20.f, y), ImVec2(x + 20.f + w + 1.f, y), frame);
        dl->AddLine(ImVec2(x, y + 20.f), ImVec2(x + 20.f, y), frame);
        dl->AddLine(ImVec2(x + w, y + 20.f), ImVec2(x + w + 20.f, y), frame);
        dl->AddLine(ImVec2(x, y + 20.f), ImVec2(x + w, y + 20.f), frame);
        dl->AddLine(ImVec2(x, y + 20.f), ImVec2(x, y + 20.f + count * 20.f), frame);
        dl->AddLine(ImVec2(x + w, y + 20.f), ImVec2(x + w, y + 20.f + count * 20.f), frame);
        dl->AddLine(ImVec2(x, y + 20.f + count * 20.f), ImVec2(x + w, y + 20.f + count * 20.f), frame);
    }
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_Accent);
    OutlinedText(dl, title, title->FontSize, ImVec2(x + w / 2.f - 42.f + (float)g_specInterwebzLabelX, y + 4.f), accent, "SPECTATOR LIST");

    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + panelH), true);
    const bool drawAvatar = g_specAvatar;
    constexpr float avatarSize = 16.f, avatarGap = 4.f;
    const float block = drawAvatar ? avatarSize + avatarGap : 0.f;
    int row = 0;
    for (const SpecEntry& e : entries) {
        const std::string text = RowText(e);
        ++row;
        if (drawAvatar)
            if (ImTextureID t = Avatar(e.sid)) {
                const float ay = y + 2.f + row * 20.f;
                dl->AddImage(t, ImVec2(x + 4.f, ay), ImVec2(x + 4.f + avatarSize, ay + avatarSize));
            }
        OutlinedText(dl, rowF, rowF->FontSize, ImVec2(x + 4.f + block, y + 4.f + row * 20.f), e.watchingLocal ? accent : IM_COL32(255, 255, 255, 255), text.c_str());
    }
    dl->PopClipRect();
}

// style 3: winxp -- a fake Notepad window, frame grows on overflow (measured against last frame)
static void DrawWinXP(ImDrawList* dl)
{
    std::vector<SpecEntry> entries; Collect(entries);
    ImFont* font = Font(g_fontTahomaBd12);
    const float fs = font->FontSize;
    static float sizeX = -200.f, sizeY = -200.f;

    float x, y; Place(400.f + sizeX, 300.f + sizeY, x, y);
    const ImVec2 o(x + 1.f, y + 1.f);

    const auto filled = [&](ImVec2 at, ImVec2 sz, ImU32 col) { dl->AddRectFilled(ImVec2(o.x + at.x, o.y + at.y), ImVec2(o.x + at.x + sz.x, o.y + at.y + sz.y), col); };
    const auto rect   = [&](ImVec2 at, ImVec2 sz, ImU32 col) { dl->AddRect(ImVec2(o.x + at.x, o.y + at.y), ImVec2(o.x + at.x + sz.x, o.y + at.y + sz.y), col); };
    const auto line   = [&](ImVec2 at, ImVec2 d, ImU32 col) { dl->AddLine(ImVec2(o.x + at.x, o.y + at.y), ImVec2(o.x + at.x + d.x, o.y + at.y + d.y), col); };
    const auto text   = [&](const char* s, ImVec2 at, ImU32 col) { dl->AddText(font, fs, ImVec2(o.x + at.x, o.y + at.y), col, s); };
    const auto bevel  = [&](ImVec2 at) {
        filled(at, ImVec2(13.f, 11.f), IM_COL32(212, 208, 200, 255));
        ImVec2 b(at.x - 1.f, at.y - 1.f);
        line(b, ImVec2(15.f, 0.f), IM_COL32(255, 255, 255, 255));
        line(b, ImVec2(0.f, 13.f), IM_COL32(255, 255, 255, 255));
        line(ImVec2(b.x + 1.f, b.y + 12.f), ImVec2(14.f, 0.f), IM_COL32(128, 128, 128, 255));
        line(ImVec2(b.x + 14.f, b.y + 12.f), ImVec2(0.f, -11.f), IM_COL32(128, 128, 128, 255));
        line(ImVec2(b.x + 1.f, b.y + 13.f), ImVec2(15.f, 0.f), IM_COL32(0, 0, 0, 255));
        line(ImVec2(b.x + 15.f, b.y + 12.f), ImVec2(0.f, -12.f), IM_COL32(0, 0, 0, 255));
    };

    filled(ImVec2(1.f, 1.f), ImVec2(397.f + sizeX, 299.f + sizeY), IM_COL32(255, 255, 255, 255));
    rect(ImVec2(0.f, 0.f), ImVec2(399.f + sizeX, 300.f + sizeY), IM_COL32(223, 223, 223, 255));
    filled(ImVec2(2.f, 2.f), ImVec2(395.f + sizeX, 296.f + sizeY), IM_COL32(192, 192, 192, 255));
    line(ImVec2(4.f, 294.f + sizeY), ImVec2(391.f + sizeX, 0.f), IM_COL32(223, 223, 223, 255));
    line(ImVec2(394.f + sizeX, 294.f + sizeY), ImVec2(0.f, -14.f), IM_COL32(223, 223, 223, 255));
    line(ImVec2(4.f, 280.f + sizeY), ImVec2(0.f, 15.f), IM_COL32(128, 128, 128, 255));
    line(ImVec2(4.f, 280.f + sizeY), ImVec2(391.f + sizeX, 0.f), IM_COL32(128, 128, 128, 255));
    filled(ImVec2(4.f, 4.f), ImVec2(391.f + sizeX, 22.f), IM_COL32(0, 0, 128, 255));
    rect(ImVec2(4.f, 46.f), ImVec2(391.f + sizeX, 232.f + sizeY), IM_COL32(128, 128, 128, 255));
    rect(ImVec2(5.f, 47.f), ImVec2(389.f + sizeX, 230.f + sizeY), IM_COL32(0, 0, 0, 255));
    filled(ImVec2(6.f, 48.f), ImVec2(387.f + sizeX, 228.f + sizeY), IM_COL32(255, 255, 255, 255));
    text("Spectators", ImVec2(32.f, 6.f), IM_COL32(255, 255, 255, 255));
    text("File  Edit  Format  Help", ImVec2(7.f, 28.f), IM_COL32(0, 0, 0, 255));
    bevel(ImVec2(378.f + sizeX, 9.f)); text("X", ImVec2(381.f + sizeX, 7.f), IM_COL32(0, 0, 0, 255));
    bevel(ImVec2(344.f + sizeX, 9.f)); text("_", ImVec2(347.f + sizeX, 4.f), IM_COL32(0, 0, 0, 255));
    bevel(ImVec2(360.f + sizeX, 9.f)); rect(ImVec2(362.f + sizeX, 10.f), ImVec2(9.f, 9.f), IM_COL32(0, 0, 0, 255));

    sizeX = sizeY = -200.f;
    float overflowX = 0.f, overflowY = 0.f;
    int total = 0;
    const bool drawAvatar = g_specAvatar;
    constexpr float avatarSize = 13.f, avatarGap = 3.f;
    const float block = drawAvatar ? avatarSize + avatarGap : 0.f;
    for (const SpecEntry& e : entries) {
        const std::string rowText = RowText(e);
        const ImVec2 at(35.f, 55.f + total++ * 15.f);
        if (drawAvatar)
            if (ImTextureID t = Avatar(e.sid))
                dl->AddImage(t, ImVec2(o.x + at.x, o.y + at.y + 1.f), ImVec2(o.x + at.x + avatarSize, o.y + at.y + 1.f + avatarSize));
        text(rowText.c_str(), ImVec2(at.x + block, at.y), IM_COL32(0, 0, 0, 255));
        if (e.watchingLocal) dl->AddCircleFilled(ImVec2(o.x + at.x - 10.f, o.y + at.y + 8.f), 4.f, IM_COL32(0, 0, 0, 255), 24);
        if (at.y > 48.f + 228.f + sizeY - 25.f) overflowY = at.y - (48.f + 228.f + sizeY - 25.f);
        const float rowW = at.x + block + TextWs(font, fs, rowText);
        if (rowW > 387.f + sizeX) overflowX = rowW - (387.f + sizeX);
    }
    if (overflowY > 0.f) sizeY += overflowY;
    if (overflowX > 0.f) sizeX += overflowX;
    char counted[32]; snprintf(counted, sizeof(counted), "%d object(s)", total);
    text(counted, ImVec2(6.f, 279.f + sizeY), IM_COL32(0, 0, 0, 255));
}

// style 4: clarity
static void DrawClarity(ImDrawList* dl)
{
    std::vector<SpecEntry> entries; Collect(entries);
    if (entries.empty() && !g_specAlwaysShow) return;
    ImFont* rowF = Font(g_fontTahoma12);
    ImFont* titleF = Font(g_fontTahomaBd12);
    constexpr float padding = 10.f, topPad = 6.f, botPad = 6.f, avatarSize = 14.f, avatarGap = 4.f;
    const float rfs = rowF->FontSize, lineH = rfs + 6.f;
    const bool drawAvatar = g_specAvatar;
    const float block = drawAvatar ? avatarSize + avatarGap : 0.f;
    float width = TextW(titleF, "spectators") + padding * 2.f;
    std::vector<std::string> rows;
    for (const SpecEntry& e : entries) { rows.push_back(RowText(e)); width = (std::max)(width, block + TextW(rowF, rows.back()) + padding * 2.f); }
    const float height = topPad + entries.size() * lineH + rfs + botPad;
    float x, y; Place(width, height, x, y);

    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(32, 32, 32, 255), IM_COL32(32, 32, 32, 255), IM_COL32(15, 15, 15, 255), IM_COL32(15, 15, 15, 255));
    const float tw = TextW(titleF, "spectators");
    dl->AddText(titleF, titleF->FontSize, ImVec2(x + (width - tw) * 0.5f, y + topPad), IM_COL32(255, 255, 255, 255), "spectators");
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_Accent);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + topPad + (i + 1) * lineH;
        if (drawAvatar)
            if (ImTextureID t = Avatar(entries[i].sid)) {
                const float ay = rowY + (rfs - avatarSize) * 0.5f;
                dl->AddImage(t, ImVec2(x + padding, ay), ImVec2(x + padding + avatarSize, ay + avatarSize));
            }
        dl->AddText(rowF, rfs, ImVec2(x + padding + block, rowY), entries[i].watchingLocal ? accent : IM_COL32(150, 150, 150, 255), rows[i].c_str());
    }
}

// style 5: chillware -- rows link to their target with a reaching bezier per spectator, converging
// on a shared junction per target, then a stub to the target name (botox draw_spectator_list_chillware)
static void DrawChillware(ImDrawList* dl, ImGuiIO& io)
{
    struct Row {
        std::string name, target;
        int index = 0, targetIndex = 0;
        uint64_t sid = 0, targetSid = 0;
        bool watchingLocal = false;
        float progress = 0.f, reach = 0.f;
        bool seen = false, removing = false;
    };
    static std::vector<Row> rows;
    static float animW = 0.f;
    static bool snap = true;
    if (!IsInGame()) { rows.clear(); snap = true; return; }

    std::vector<SpecEntry> entries; Collect(entries);
    for (auto& r : rows) r.seen = false;
    for (const SpecEntry& e : entries) {
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.index == e.index; });
        Row d;
        d.name = Utf8Truncate(e.name, 32); d.target = Utf8Truncate(e.target, 32);
        d.index = e.index; d.targetIndex = e.targetIndex; d.sid = e.sid; d.targetSid = e.targetSid;
        d.watchingLocal = e.watchingLocal; d.seen = true;
        if (it == rows.end()) { rows.push_back(d); continue; }
        d.progress = it->progress; d.reach = it->reach;
        *it = d;
    }
    const float dt = io.DeltaTime;
    for (auto& r : rows) {
        if (!r.seen) r.removing = true;
        r.progress = std::clamp(r.progress + dt * 5.5f * (r.removing ? -1.f : 1.f), 0.f, 1.f);
        const bool reaching = !r.removing && r.progress > 0.4f;
        r.reach = std::clamp(r.reach + dt * 2.6f * (reaching ? 1.f : -1.f), 0.f, 1.f);
    }
    const auto ease = [](float t) { const float c = ImSaturate(t); return c * c * (3.f - 2.f * c); };
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return r.removing && r.progress <= 0.f; }), rows.end());
    if (rows.empty() && !g_specAlwaysShow) { snap = true; return; }

    ImFont* font = Font(g_fontChillware13);
    const float fs = font->FontSize;
    constexpr const char* titleText = "spectator list";
    constexpr float padX = 10.f, padY = 7.f, headerH = 21.f, rowH = 21.f, avatarSize = 16.f, avatarGap = 7.f, linkW = 86.f;
    const bool showAvatar = g_specAvatar;
    const float block = showAvatar ? avatarSize + avatarGap : 0.f;

    struct Group { std::string name; int targetIndex = 0; uint64_t sid = 0; bool watchingLocal = false; float center = 0.f, alpha = 0.f, reach = 0.f; int count = 0; };
    std::vector<Group> groups;
    std::vector<float> centers(rows.size(), 0.f);
    std::vector<size_t> order;
    float listH = 0.f, nameTextW = 0.f, targetTextW = 0.f;
    for (size_t i = 0; i < rows.size(); i++) {
        nameTextW = (std::max)(nameTextW, TextWs(font, fs, rows[i].name));
        if (std::any_of(groups.begin(), groups.end(), [&](const Group& g) { return g.targetIndex == rows[i].targetIndex; })) continue;
        Group g; g.name = rows[i].target; g.targetIndex = rows[i].targetIndex; g.sid = rows[i].targetSid; g.watchingLocal = rows[i].watchingLocal;
        groups.push_back(g);
        targetTextW = (std::max)(targetTextW, TextWs(font, fs, g.name));
    }
    const float maxWidth = std::clamp(g_specChillwareMaxWidth, 190.f, 600.f);
    const float chromeW = padX * 2.f + linkW + block * 2.f;
    const float budget = (std::max)(maxWidth - chromeW, 40.f);
    float nameCap = nameTextW, targetCap = targetTextW;
    if (nameTextW + targetTextW > budget) {
        constexpr float floor_ = 34.f;
        const float share = budget / (nameTextW + targetTextW);
        nameCap = (std::max)(nameTextW * share, floor_);
        targetCap = (std::max)(targetTextW * share, floor_);
        const float overrun = (nameCap + targetCap) - budget;
        if (overrun > 0.f) {
            if (nameCap >= targetCap) nameCap = (std::max)(nameCap - overrun, floor_);
            else targetCap = (std::max)(targetCap - overrun, floor_);
        }
    }
    std::vector<std::string> names(rows.size());
    float nameW = 0.f, targetW = 0.f;
    for (size_t i = 0; i < rows.size(); i++) {
        names[i] = Ellipsize(font, fs, rows[i].name, nameCap);
        nameW = (std::max)(nameW, block + TextWs(font, fs, names[i]));
    }
    for (auto& g : groups) {
        g.name = Ellipsize(font, fs, g.name, targetCap);
        targetW = (std::max)(targetW, block + TextWs(font, fs, g.name));
    }
    for (size_t gi = 0; gi < groups.size(); gi++)
        for (size_t i = 0; i < rows.size(); i++)
            if (rows[i].targetIndex == groups[gi].targetIndex) order.push_back(i);
    for (const size_t i : order) {
        const float hgt = rowH * ease(rows[i].progress);
        centers[i] = listH + hgt * 0.5f;
        listH += hgt;
        auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& c) { return c.targetIndex == rows[i].targetIndex; });
        if (g == groups.end()) continue;
        g->center = (g->center * (float)g->count + centers[i]) / (float)(g->count + 1);
        g->alpha = (std::max)(g->alpha, rows[i].progress);
        g->reach = (std::max)(g->reach, rows[i].reach);
        g->count++;
    }
    for (size_t i = 1; i < groups.size(); i++) groups[i].center = (std::max)(groups[i].center, groups[i - 1].center + rowH);
    float limit = listH - rowH * 0.5f;
    for (size_t i = groups.size(); i-- > 0;) { groups[i].center = (std::min)(groups[i].center, limit); limit = groups[i].center - rowH; }

    const float titleW = TextWs(font, fs, titleText);
    const float width = (std::max)(titleW + 40.f, padX * 2.f + nameW + linkW + targetW);
    animW = snap ? width : ImLerp(animW, width, ImSaturate(dt * 12.f));
    snap = false;
    const float height = headerH + padY * 2.f + listH;
    float left, top; Place(animW, height, left, top);
    const float right = left + animW, bottom = top + height;

    const ImDrawListFlags oldFlags = dl->Flags;
    dl->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
    const float savedTol = dl->_Data->CurveTessellationTol;
    dl->_Data->CurveTessellationTol = 0.35f;

    constexpr float rounding = 12.f, linkThick = 2.f, stubSplit = 0.72f;
    for (int step = 5; step >= 1; step--) {
        const float s = (float)step;
        dl->AddRect(ImVec2(left - s, top - s), ImVec2(right + s, bottom + s), IM_COL32(0, 0, 0, (int)(46.f - s * 7.f)), rounding + s, ImDrawFlags_None, 1.f);
    }
    dl->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(8, 8, 10, 248), rounding);
    dl->AddRectFilled(ImVec2(left, top), ImVec2(right, top + headerH), IM_COL32(22, 22, 27, 248), rounding, ImDrawFlags_RoundCornersTop);
    dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(255, 255, 255, 16), rounding);
    dl->AddText(font, fs, ImVec2(left + (animW - titleW) * 0.5f, top + (headerH - fs) * 0.5f), IM_COL32(196, 196, 206, 255), titleText);

    const auto avatarAt = [&](float ax, float ay, uint64_t sid, float alpha) {
        constexpr float r = avatarSize * 0.5f;
        if (ImTextureID t = Avatar(sid)) {
            dl->AddImageRounded(t, ImVec2(ax, ay), ImVec2(ax + avatarSize, ay + avatarSize), ImVec2(0, 0), ImVec2(1, 1), ImColor(1.f, 1.f, 1.f, alpha), r);
            return;
        }
        dl->AddRectFilled(ImVec2(ax, ay), ImVec2(ax + avatarSize, ay + avatarSize), WithAlpha(IM_COL32(38, 38, 46, 255), alpha), r);
        const float mw = TextWs(font, fs, "?");
        dl->AddText(font, fs, ImVec2(ax + (avatarSize - mw) * 0.5f, ay + (avatarSize - fs) * 0.5f), WithAlpha(IM_COL32(148, 148, 158, 255), alpha), "?");
    };

    const float rowsTop = top + headerH + padY;
    const float nameX = left + padX, targetEnd = right - padX;
    const float columnStart = nameX + nameW + 8.f;
    const float linkEnd = (std::max)(columnStart + 24.f, targetEnd - targetW - 9.f);
    const float junctionX = (std::max)(columnStart + 10.f, linkEnd - 26.f);
    const float convergeRun = (std::min)(34.f, (junctionX - columnStart) * 0.9f);
    const float bendX = junctionX - convergeRun;

    const auto drawReaching = [&](const std::vector<ImVec2>& pts, float fraction, ImU32 color) {
        if (pts.size() < 2 || fraction <= 0.001f) return;
        if (fraction >= 0.999f) { dl->AddPolyline(pts.data(), (int)pts.size(), color, ImDrawFlags_None, linkThick); return; }
        float total = 0.f;
        for (size_t p = 1; p < pts.size(); p++) total += ImSqrt(ImLengthSqr(ImVec2(pts[p].x - pts[p - 1].x, pts[p].y - pts[p - 1].y)));
        float left_ = total * fraction;
        std::vector<ImVec2> drawn; drawn.reserve(pts.size()); drawn.push_back(pts.front());
        for (size_t p = 1; p < pts.size(); p++) {
            const ImVec2 d(pts[p].x - pts[p - 1].x, pts[p].y - pts[p - 1].y);
            const float len = ImSqrt(ImLengthSqr(d));
            if (len <= 0.f) continue;
            if (left_ < len) { const float t = left_ / len; drawn.push_back(ImVec2(pts[p - 1].x + d.x * t, pts[p - 1].y + d.y * t)); break; }
            left_ -= len;
            drawn.push_back(pts[p]);
        }
        if (drawn.size() >= 2) dl->AddPolyline(drawn.data(), (int)drawn.size(), color, ImDrawFlags_None, linkThick);
    };

    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        const float alpha = ease(r.progress);
        const float center = rowsTop + centers[i];
        const ImU32 rowColor = U32(r.watchingLocal ? g_specColorOne : g_specColorTwo, alpha);
        auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& c) { return c.targetIndex == r.targetIndex; });
        const float nameEnd = nameX + block + TextWs(font, fs, names[i]);
        if (g != groups.end()) {
            const float gc = rowsTop + g->center;
            const float linkStart = (std::min)(nameEnd + 8.f, bendX);
            std::vector<ImVec2> path; path.reserve(28);
            path.push_back(ImVec2(linkStart, center));
            if (fabsf(gc - center) < 0.5f) path.push_back(ImVec2(junctionX, center));
            else {
                if (linkStart < bendX - 0.5f) path.push_back(ImVec2(bendX, center));
                const float span = convergeRun * 0.5f;
                const ImVec2 p0(bendX, center), p1(bendX + span, center), p2(junctionX - span, gc), p3(junctionX, gc);
                for (int s = 1; s <= 24; s++) path.push_back(ImBezierCubicCalc(p0, p1, p2, p3, (float)s / 24.f));
            }
            drawReaching(path, ease(r.reach) / stubSplit, rowColor);
        }
        if (showAvatar) avatarAt(nameX, center - avatarSize * 0.5f, r.sid, alpha);
        dl->AddText(font, fs, ImVec2(nameX + block, center - fs * 0.5f), rowColor, names[i].c_str());
    }
    for (const Group& g : groups) {
        const float center = rowsTop + g.center;
        const float reach = ease(g.reach);
        const float stub = ImSaturate((reach - stubSplit) / (1.f - stubSplit));
        const ImU32 line = U32(g.watchingLocal ? g_specColorOne : g_specColorTwo, ease(g.alpha));
        const float tw = TextWs(font, fs, g.name);
        const float targetLinkEnd = (std::max)(junctionX + 6.f, targetEnd - block - tw - 9.f);
        if (stub > 0.f) {
            drawReaching({ ImVec2(junctionX, center), ImVec2(targetLinkEnd, center) }, stub, line);
            if (g.count > 1) dl->AddCircleFilled(ImVec2(junctionX, center), linkThick * 0.5f, line, 16);
        }
        const float ta = ease(g.alpha) * stub;
        if (ta <= 0.f) continue;
        dl->AddText(font, fs, ImVec2(targetEnd - block - tw, center - fs * 0.5f), U32(g.watchingLocal ? g_specColorOne : g_specColorTwo, ta), g.name.c_str());
        if (showAvatar) avatarAt(targetEnd - avatarSize, center - avatarSize * 0.5f, g.sid, ta);
    }
    dl->_Data->CurveTessellationTol = savedTol;
    dl->Flags = oldFlags;
}

// style 6: lobotomy
static void DrawLobotomy(ImDrawList* dl)
{
    std::vector<SpecEntry> entries; Collect(entries); EraseNotLocal(entries);
    if (entries.empty() && !g_specAlwaysShow) return;
    ImFont* font = Font(g_fontLobotomy13);
    constexpr float w = 200.f, titleTop = 5.f, titleToList = 15.f, leftM = 10.f, itemH = 17.f, itemSp = 10.f, bottomPad = 10.f, layoutFs = 11.f;
    constexpr float listTop = titleTop + layoutFs + titleToList;
    const float count = (float)entries.size();
    const float h = listTop + count * itemH + (std::max)(count - 1.f, 0.f) * itemSp + bottomPad;
    const bool drawAvatar = g_specAvatar;
    constexpr float avatarSize = 13.f, avatarGap = 4.f;
    const float block = drawAvatar ? avatarSize + avatarGap : 0.f;
    float x, y; Place(w, h, x, y);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(13, 13, 13, 255), 5.f);
    dl->AddText(font, font->FontSize, ImVec2(x + (w - ImguiTextW(font, "Spectators")) * 0.5f, y + titleTop), IM_COL32(255, 255, 255, 255), "Spectators");
    const ImU32 col = U32(g_specColorOne);
    for (size_t i = 0; i < entries.size(); i++) {
        const float rx = x + leftM, ry = y + listTop + (float)i * (itemH + itemSp);
        if (drawAvatar)
            if (ImTextureID t = Avatar(entries[i].sid)) dl->AddImage(t, ImVec2(rx, ry), ImVec2(rx + avatarSize, ry + avatarSize));
        dl->AddText(font, font->FontSize, ImVec2(rx + block, ry), col, Utf8Truncate(entries[i].name, 24).c_str());
    }
}

// style 7: kamibebra
static void DrawKamibebra(ImDrawList* dl, ImGuiIO& io)
{
    struct Row { std::string key; SpecEntry entry; float progress = 0.f; bool seen = false, removing = false; };
    static std::vector<Row> rows;
    if (!IsInGame()) { rows.clear(); return; }
    ImFont* regular = Font(g_fontKamibebra13);
    ImFont* boldF = g_fontKamibebraBold13 ? g_fontKamibebraBold13 : regular;

    const bool showAll = g_specListType == 0;
    const bool animate = g_specKbAnimate, alwaysShow = g_specKbAlwaysShow, bold = g_specKbBold;
    std::vector<SpecEntry> specs; Collect(specs);
    if (!g_specKbDynamic)
        for (auto& s : specs) {
            const std::string cut = Utf8Truncate(s.target, 9);
            if (cut.size() < s.target.size()) s.target = cut + "...";
        }
    for (auto& r : rows) r.seen = false;
    for (const SpecEntry& s : specs) {
        const std::string key = s.name + "\n" + s.target;
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& c) { return c.key == key; });
        if (it == rows.end()) { rows.push_back({ key, s, animate ? 0.f : 1.f, true, false }); continue; }
        it->entry = s; it->seen = true; it->removing = false;
        if (!animate) it->progress = 1.f;
    }
    for (auto& r : rows) if (!r.seen) r.removing = true;
    if (animate) {
        const float step = io.DeltaTime * 4.f;
        for (auto& r : rows) r.progress = std::clamp(r.progress + (r.removing ? -step : step), 0.f, 1.f);
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Row& r) { return animate ? r.removing && r.progress <= 0.f : !r.seen; }), rows.end());

    const bool hasSpecs = !specs.empty();
    const bool hasRows = animate ? !rows.empty() : hasSpecs;
    if (!hasRows && !alwaysShow) return;

    std::vector<std::pair<const SpecEntry*, float>> render;
    if (animate) for (const auto& r : rows) render.emplace_back(&r.entry, r.progress);
    else for (const auto& s : specs) render.emplace_back(&s, 1.f);

    constexpr float titleTop = 4.f, titleToList = 8.f, leftM = 10.f, itemH = 17.f, itemSp = 1.f, bottomPad = 5.f, layoutFs = 12.f;
    constexpr float listTop = titleTop + layoutFs + titleToList;
    const bool drawAvatar = g_specAvatar;
    constexpr float avatarSize = 13.f, avatarGap = 4.f;
    const float block = drawAvatar ? avatarSize + avatarGap : 0.f;
    float maxTextW = ImguiTextW(regular, "Spectators");
    for (const auto& [e, p] : render) maxTextW = (std::max)(maxTextW, block + ImguiTextW(regular, (e->name + "   >   " + e->target).c_str()));
    const float winW = (std::max)(maxTextW + leftM * 2.f + 10.f, 200.f);
    float listH = 0.f;
    if (hasRows && animate) {
        for (size_t i = 0; i < render.size(); i++) listH += itemH * render[i].second + (i + 1 < render.size() ? itemSp : 0.f);
    } else {
        const float items = hasRows ? (float)render.size() : 1.f;
        listH = items * itemH + (items > 1.f ? (items - 1.f) * itemSp : 0.f);
    }
    const float winH = listTop + listH + bottomPad;
    float x, y; Place(winW, winH, x, y);

    if (g_specKbWindowShadow)
        dl->AddRectFilled(ImVec2(x + 3.f, y + 3.f), ImVec2(x + winW + 3.f, y + winH + 3.f), IM_COL32(0, 0, 0, 128), 5.f, ImDrawFlags_RoundCornersAll);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + winW, y + winH), IM_COL32(13, 13, 13, 255), 5.f);

    ImFont* font = bold ? boldF : regular;
    const float fs = font->FontSize;
    std::string title = bold ? "spectators" : "Spectators";
    if (g_specKbCustomTitle && g_specKbTitle[0]) title = g_specKbTitle;
    dl->AddText(font, fs, ImVec2(x + (winW - ImguiTextW(font, title.c_str())) * 0.5f, y + titleTop), IM_COL32(255, 255, 255, 255), title.c_str());

    if (!hasSpecs && alwaysShow) {
        dl->AddText(font, fs, ImVec2(x + leftM, y + listTop), IM_COL32(100, 100, 100, 255), bold ? "no spectators..." : "No spectators...");
    } else {
        const ImU32 lc = U32(g_specKbLocalColor), oc = U32(g_specKbOtherColor);
        const ImU32 lsc = U32(g_specKbLocalShadowColor), osc = U32(g_specKbOtherShadowColor);
        float rowY = y + listTop;
        for (const auto& [e, p] : render) {
            const float rh = itemH * p;
            if (p <= 0.f) { rowY += rh + itemSp; continue; }
            const std::string text = showAll ? e->name + "   >   " + e->target : e->name;
            const float rx = x + leftM;
            // the reveal: a growing row uncovers the name top down, it never fades
            dl->PushClipRect(ImVec2(rx, rowY), ImVec2(x + winW - leftM, rowY + rh), true);
            if (drawAvatar)
                if (ImTextureID t = Avatar(e->sid)) dl->AddImage(t, ImVec2(rx, rowY), ImVec2(rx + avatarSize, rowY + avatarSize));
            const ImVec2 tp(rx + block, rowY);
            if (e->watchingLocal ? g_specKbLocalShadow : g_specKbOtherShadow)
                dl->AddText(font, fs, ImVec2(tp.x + 1.f, tp.y + 1.f), e->watchingLocal ? lsc : osc, text.c_str());
            dl->AddText(font, fs, tp, e->watchingLocal ? lc : oc, text.c_str());
            dl->PopClipRect();
            rowY += rh + itemSp;
        }
    }

    if (g_specKbGif) {
        const float gs = g_specKbGifSize;
        const ImVec2 gmin(x + (winW - gs) * 0.5f, y - gs), gmax(gmin.x + gs, gmin.y + gs);
        const int type = std::clamp(g_specKbGifType, 0, 2);
        ImTextureID frame = 0;
        if (type == 2) frame = FileImageFrame(s_kamibebraGif, g_specKbGifPath);
        else if (type == 1) frame = GifTex_Frame(GifTex_Get("kamibebra_shit", kamibebra_gif_shit, sizeof(kamibebra_gif_shit)), ImGui::GetTime());
        else frame = GifTex_Frame(GifTex_Get("kamibebra_original", kamibebra_gif_original, sizeof(kamibebra_gif_original)), ImGui::GetTime());
        if (frame) dl->AddImage(frame, gmin, gmax);
        else dl->AddRectFilled(gmin, gmax, IM_COL32(80, 80, 80, 120), 4.f);
    }
}

// ==== style 8: eyes -- per-row procedural eye texture (botox eye_row_texture) ==================
namespace {
    constexpr ImVec2 k_eyeLensStart = { 3.2749f, 15.2957f };
    constexpr ImVec2 k_eyeLens[8][3] = {
        { { 2.4250f, 14.1915f }, { 2.0000f, 13.6394f }, { 2.0000f, 12.0000f } },
        { { 2.0000f, 10.3606f }, { 2.4250f, 9.8085f }, { 3.2749f, 8.7043f } },
        { { 4.9720f, 6.4996f }, { 7.8181f, 4.0000f }, { 12.0000f, 4.0000f } },
        { { 16.1819f, 4.0000f }, { 19.0280f, 6.4996f }, { 20.7251f, 8.7043f } },
        { { 21.5750f, 9.8085f }, { 22.0000f, 10.3606f }, { 22.0000f, 12.0000f } },
        { { 22.0000f, 13.6394f }, { 21.5750f, 14.1915f }, { 20.7251f, 15.2957f } },
        { { 19.0280f, 17.5004f }, { 16.1819f, 20.0000f }, { 12.0000f, 20.0000f } },
        { { 7.8181f, 20.0000f }, { 4.9720f, 17.5004f }, { 3.2749f, 15.2957f } },
    };
    constexpr ImVec2 k_eyeIrisCenter = { 12.f, 12.f };
    constexpr float k_eyeIrisRadius = 3.f;
    constexpr ImVec2 k_eyeLid[33] = {
        { 1.9999f, 7.0002f },   { 2.3629f, 7.7191f },   { 2.7732f, 8.4122f },   { 3.2219f, 9.0812f },   { 3.7069f, 9.7243f },
        { 4.2275f, 10.3388f },  { 4.7838f, 10.9213f },  { 5.3757f, 11.4676f },  { 6.0029f, 11.9729f },  { 6.6645f, 12.4322f },
        { 7.3588f, 12.8403f },  { 8.0833f, 13.1922f },  { 8.8341f, 13.4835f },  { 9.6066f, 13.7110f },  { 10.3955f, 13.8728f },
        { 11.1952f, 13.9688f }, { 11.9999f, 14.0002f }, { 12.8047f, 13.9688f }, { 13.6043f, 13.8729f }, { 14.3932f, 13.7110f },
        { 15.1658f, 13.4835f }, { 15.9166f, 13.1922f }, { 16.6410f, 12.8403f }, { 17.3353f, 12.4322f }, { 17.9969f, 11.9729f },
        { 18.6241f, 11.4675f }, { 19.2160f, 10.9213f }, { 19.7723f, 10.3388f }, { 20.2930f, 9.7243f },  { 20.7779f, 9.0812f },
        { 21.2266f, 8.4122f },  { 21.6369f, 7.7191f },  { 21.9999f, 7.0002f },
    };
    constexpr ImVec2 k_eyeLashes[5][2] = {
        { { 18.9955f, 11.1248f }, { 20.4999f, 12.6290f } }, { { 15.5825f, 13.3218f }, { 16.9999f, 15.5002f } },
        { { 11.9999f, 14.0002f }, { 11.9999f, 16.5002f } }, { { 8.4870f, 13.3488f }, { 7.0871f, 15.5003f } },
        { { 5.0496f, 11.1666f }, { 3.5871f, 12.6291f } },
    };
    constexpr float k_eyeLidDrop = 5.f, k_eyeHaloAlpha = 0.4f;
    constexpr int k_eyeLensSteps = 16;

    using n_spec::EyeLayout;
    using n_spec::MakeEyeLayout;

    struct EyeTex { IDirect3DTexture9* tex = nullptr; int face = 0; bool failed = false; };
    EyeTex s_eyeTex[4];   // 0 closed, 1 open, 2 emoji closed, 3 emoji open

    // twemoji eyes, 36 viewbox. ring = outer ellipse minus inner; closed = ring below y 20.1738
    struct EmojiShape { ImVec2 c; float rx, ry, irx, iry; uint32_t color; };
    constexpr float k_emojiLidY = 20.1738f;
    constexpr EmojiShape k_emojiOpen[10] = {
        { { 8.828f, 18.f }, 7.953f, 13.281f, 0.f, 0.f, 0xFFF5F8FA },
        { { 8.828f, 18.f }, 8.703f, 14.031f, 7.203f, 12.531f, 0xFFE1E8ED },
        { { 6.594f, 18.f }, 4.96f, 4.96f, 0.f, 0.f, 0xFF8899A6 },
        { { 6.594f, 18.f }, 3.565f, 3.565f, 0.f, 0.f, 0xFF292F33 },
        { { 7.911f, 15.443f }, 1.426f, 1.426f, 0.f, 0.f, 0xFFF5F8FA },
        { { 27.234f, 18.f }, 7.953f, 13.281f, 0.f, 0.f, 0xFFF5F8FA },
        { { 27.234f, 18.f }, 8.703f, 14.031f, 7.203f, 12.531f, 0xFFE1E8ED },
        { { 25.f, 18.f }, 4.96f, 4.96f, 0.f, 0.f, 0xFF8899A6 },
        { { 25.f, 18.f }, 3.565f, 3.565f, 0.f, 0.f, 0xFF292F33 },
        { { 26.317f, 15.443f }, 1.426f, 1.426f, 0.f, 0.f, 0xFFF5F8FA },
    };
    constexpr EmojiShape k_emojiClosed[2] = {
        { { 8.828f, 18.f }, 8.703f, 14.031f, 7.203f, 12.531f, 0xFFAAAAAA },
        { { 27.234f, 18.f }, 8.703f, 14.031f, 7.203f, 12.531f, 0xFFAAAAAA },
    };

    bool EmojiInside(const EmojiShape& s, const ImVec2& p)
    {
        const auto in = [&](float rx, float ry) { const float dx = (p.x - s.c.x) / rx, dy = (p.y - s.c.y) / ry; return dx * dx + dy * dy <= 1.f; };
        return in(s.rx, s.ry) && !(s.irx > 0.f && in(s.irx, s.iry));
    }

    // 4x4 supersample, painter order, straight alpha out
    void EmojiEyePixels(bool open, const EyeLayout& l, std::vector<uint32_t>& px)
    {
        constexpr int k_samples = 4;
        const float scale = l.icon / 36.f;
        const ImVec2 box(l.pad + l.face + l.gap, l.pad + l.radius - l.icon * 0.5f);
        const EmojiShape* shapes = open ? k_emojiOpen : k_emojiClosed;
        const int n = open ? IM_ARRAYSIZE(k_emojiOpen) : IM_ARRAYSIZE(k_emojiClosed);
        for (int y = 0; y < l.height; y++)
            for (int x = 0; x < l.width; x++) {
                int covered = 0; float r = 0.f, g = 0.f, b = 0.f;
                for (int sy = 0; sy < k_samples; sy++)
                    for (int sx = 0; sx < k_samples; sx++) {
                        const ImVec2 p((x + (sx + 0.5f) / k_samples - box.x) / scale, (y + (sy + 0.5f) / k_samples - box.y) / scale);
                        if (!open && p.y < k_emojiLidY) continue;
                        uint32_t color = 0;
                        for (int i = 0; i < n; i++) if (EmojiInside(shapes[i], p)) color = shapes[i].color;
                        if (!color) continue;
                        covered++;
                        r += (color >> 16) & 0xFF; g += (color >> 8) & 0xFF; b += color & 0xFF;
                    }
                uint32_t out = 0;
                if (covered) {
                    const uint32_t a8 = (uint32_t)(covered * 255 / (k_samples * k_samples));
                    out = (a8 << 24) | ((uint32_t)(r / covered + 0.5f) << 16) | ((uint32_t)(g / covered + 0.5f) << 8) | (uint32_t)(b / covered + 0.5f);
                }
                px[(size_t)y * l.width + x] = out;
            }
    }

    float SegmentDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b)
    {
        const ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
        const float len = ImLengthSqr(ab);
        const float t = len > 0.f ? ImSaturate((ap.x * ab.x + ap.y * ab.y) / len) : 0.f;
        return sqrtf(ImLengthSqr(ImVec2(ap.x - ab.x * t, ap.y - ab.y * t)));
    }

    // emoji rows = full colour, no avatar ring (ring drawn tinted by the caller).
    // DYNAMIC + DEFAULT: MANAGED does not exist on this engine's d3d9ex device (gif_tex.cpp same).
    IDirect3DTexture9* EyeRowTexture(bool open, bool emoji, const EyeLayout& l)
    {
        EyeTex& row = s_eyeTex[(emoji ? 2 : 0) + (open ? 1 : 0)];
        if (row.face == l.face && (row.tex || row.failed)) return row.tex;
        if (!g_d3dDev) return nullptr;
        if (row.tex) { row.tex->Release(); row.tex = nullptr; }
        row.face = l.face;
        row.failed = true;

        const int w = l.width, h = l.height;
        std::vector<uint32_t> px((size_t)w * h);
        if (emoji) EmojiEyePixels(open, l, px);
        else {
            const float scale = l.icon / 24.f, drop = open ? 0.f : k_eyeLidDrop;
            const ImVec2 box(l.pad + l.face + l.gap, l.pad + l.radius - l.icon * 0.5f);
            const auto at = [&](const ImVec2& p) { return ImVec2(box.x + p.x * scale, box.y + (p.y + drop) * scale); };
            std::vector<std::pair<ImVec2, ImVec2>> segs;
            std::vector<std::pair<ImVec2, float>> circles{ { ImVec2(l.pad + l.radius, l.pad + l.radius), l.radius } };
            if (open) {
                ImVec2 from = k_eyeLensStart;
                for (const auto& c : k_eyeLens) {
                    ImVec2 last = at(from);
                    for (int s = 1; s <= k_eyeLensSteps; s++) {
                        const ImVec2 p = at(ImBezierCubicCalc(from, c[0], c[1], c[2], s / (float)k_eyeLensSteps));
                        segs.emplace_back(last, p);
                        last = p;
                    }
                    from = c[2];
                }
                circles.emplace_back(at(k_eyeIrisCenter), k_eyeIrisRadius * scale);
            } else {
                for (int i = 1; i < IM_ARRAYSIZE(k_eyeLid); i++) segs.emplace_back(at(k_eyeLid[i - 1]), at(k_eyeLid[i]));
                for (const auto& lash : k_eyeLashes) segs.emplace_back(at(lash[0]), at(lash[1]));
            }
            const float half = l.stroke * 0.5f;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const ImVec2 p(x + 0.5f, y + 0.5f);
                    float d = FLT_MAX;
                    for (const auto& [a, b] : segs) d = ImMin(d, SegmentDistance(p, a, b));
                    for (const auto& [c, r] : circles) d = ImMin(d, fabsf(sqrtf(ImLengthSqr(ImVec2(p.x - c.x, p.y - c.y))) - r));
                    const float glyph = ImSaturate(half + 0.5f - d);
                    const float halo = ImSaturate(half + 1.5f - d) * k_eyeHaloAlpha;
                    const float alpha = glyph + halo * (1.f - glyph);
                    const float gray = alpha > 0.f ? glyph / alpha : 0.f;
                    const uint32_t a8 = (uint32_t)(alpha * 255.f + 0.5f), g8 = (uint32_t)(gray * 255.f + 0.5f);
                    px[(size_t)y * w + x] = (a8 << 24) | (g8 << 16) | (g8 << 8) | g8;
                }
        }

        if (FAILED(g_d3dDev->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &row.tex, nullptr)) || !row.tex) {
            row.tex = nullptr;
            DebugLog("spectator list: eye texture %dx%d create failed", w, h);
            return nullptr;
        }
        D3DLOCKED_RECT lr = {};
        bool uploaded = false;
        if (SUCCEEDED(row.tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) {
            if (lr.pBits) {
                for (int y = 0; y < h; y++) memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, px.data() + (size_t)y * w, (size_t)w * 4);
                uploaded = true;
            }
            row.tex->UnlockRect(0);
        }
        if (!uploaded) { row.tex->Release(); row.tex = nullptr; return nullptr; }
        row.failed = false;
        return row.tex;
    }
}

static void DrawEyes(ImDrawList* dl)
{
    std::vector<SpecEntry> entries; Collect(entries);
    if (entries.empty()) return;
    const int face = (int)floorf(ImClamp(g_specEyesSize, 16.f, 64.f) + 0.5f);
    const EyeLayout l = MakeEyeLayout(face);
    const int count = (int)entries.size();
    float px, py; Place((float)l.width, (float)(l.height + (count - 1) * l.pitch), px, py);
    const bool emoji = g_specEyesEmoji;
    for (int i = 0; i < count; i++) {
        const SpecEntry& e = entries[i];
        const ImVec2 rowMin(px, py + (float)(i * l.pitch));
        const ImVec2 c(rowMin.x + l.pad + l.radius, rowMin.y + l.pad + l.radius);
        const float r = l.radius;
        if (ImTextureID t = Avatar(e.sid))
            dl->AddImageRounded(t, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r);
        else dl->AddCircleFilled(c, r, IM_COL32(45, 45, 45, 200));
        const ImU32 tint = U32(e.watchingLocal ? g_specColorOne : g_specColorTwo);
        if (emoji) {
            const ImU32 halo = IM_COL32(0, 0, 0, (int)(((tint >> IM_COL32_A_SHIFT) & 0xFF) * k_eyeHaloAlpha));
            dl->AddCircle(c, r, halo, 0, l.stroke + 2.f);
            dl->AddCircle(c, r, tint, 0, l.stroke);
        }
        if (IDirect3DTexture9* art = EyeRowTexture(e.watchingLocal, emoji, l))
            dl->AddImage((ImTextureID)art, rowMin, ImVec2(rowMin.x + l.width, rowMin.y + l.height), ImVec2(0, 0), ImVec2(1, 1), emoji ? IM_COL32_WHITE : tint);
    }
}

// style 9: cumhacck
static void DrawCumhacck(ImDrawList* dl, ImGuiIO& io)
{
    std::vector<SpecEntry> entries; Collect(entries); EraseNotLocal(entries);
    ImFont* fT = Font(g_fontCumhacck35);
    ImFont* fN = Font(g_fontTahomaBd12);
    const float mid = floorf(io.DisplaySize.y * 0.5f);
    const ImU32 col = U32(g_specColorOne);
    const ImU32 shadow = col & IM_COL32_A_MASK;
    const auto shadowed = [&](ImFont* f, float x, float y, const char* t) {
        dl->AddText(f, f->FontSize, ImVec2(x + 1.f, y + 1.f), shadow, t);
        dl->AddText(f, f->FontSize, ImVec2(x, y), col, t);
    };
    for (size_t i = 0; i < entries.size(); i++) {
        const std::string name = entries[i].fake ? entries[i].name + " (BOT)" : entries[i].name;
        shadowed(fN, 4.f, mid - 40.f + 15.f * (float)i, name.c_str());
    }
    shadowed(fT, 4.f, mid - 75.f, "spectators:");
}

// style 10: clarity v2
static void DrawClarityV2(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* titleF = Font(g_fontInterBold15);
    ImFont* font = g_fontInterMedium15 ? g_fontInterMedium15 : titleF;
    std::vector<SpecEntry> entries; Collect(entries);
    static float alpha = 0.f;
    {
        const float target = (g_showMenu || !entries.empty() || g_specClarityAlwaysShow) ? 1.f : 0.f;
        if (alpha != target) alpha = ImLerp(alpha, target, std::clamp(io.DeltaTime * 15.f, 0.f, 1.f));
        if (alpha > 0.99f) alpha = 1.f; else if (alpha <= 0.01f) alpha = 0.f;
    }
    if (alpha == 0.f) return;

    const auto ts = [](const ImFont* f, const std::string& t) {
        const ImVec2 s = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, t.c_str());
        return std::pair<int, int>{ (int)s.x, (int)s.y };
    };
    const auto fit = [&](std::string& text, int budget, size_t keep) {
        if (ts(font, text).first <= budget) return true;
        const int ell = ts(font, "...").first;
        std::vector<size_t> starts;
        for (size_t i = 0; i < text.size(); ++i) if (((unsigned char)text[i] & 0xc0) != 0x80) starts.push_back(i);
        for (size_t chars = starts.size(); chars > keep;) {
            text.resize(starts[--chars]);
            if (ts(font, text).first + ell <= budget) { text += "..."; return true; }
        }
        text += "...";
        return false;
    };
    constexpr int winW = 210, padX = 12, padY = 7, avatar = 20, rowMax = winW - padX * 2;
    const bool allPlayers = g_specListType == 0, drawAvatars = g_specAvatar;
    struct R { std::string text; ImTextureID av; ImU32 color; int h; };
    std::vector<R> rows;
    for (const SpecEntry& e : entries) {
        const ImTextureID tex = drawAvatars && !e.fake ? Avatar(e.sid) : (ImTextureID)0;
        const int budget = rowMax - (tex ? avatar + 5 : 0);
        std::string name = e.name + (e.fake ? " [bot]" : "");
        if (allPlayers) {
            std::string target = e.target + (e.targetFake ? " [bot]" : "");
            const int avail = budget - ts(font, " -> ").first;
            bool ok = fit(target, avail - ts(font, name).first, 5);
            if (!ok) ok = fit(name, avail - ts(font, target).first, 5);
            name = name + " -> " + target;
            if (!ok) fit(name, budget, 0);
        } else fit(name, budget, 0);
        const bool dim = allPlayers && !e.watchingLocal;
        const ImU32 color = dim ? IM_COL32(255, 255, 255, (int)(255.f * alpha * 0.5f)) : U32(g_specColorOne, alpha);
        rows.push_back({ name, tex, color, tex ? (std::max)((int)font->FontSize, avatar) : (int)font->FontSize });
    }
    std::string emptyText = "no spectators";
    if (rows.empty()) { fit(emptyText, rowMax, 0); rows.push_back({ emptyText, (ImTextureID)0, IM_COL32(255, 255, 255, (int)(100.f * alpha)), ts(font, emptyText).second }); }
    int winH = padY + ts(titleF, "spectators").second + 5;
    for (const auto& r : rows) winH += r.h + 3;
    winH += padY;

    float fx, fy; Place((float)winW + 6.f, (float)winH + 6.f, fx, fy);
    const int x = (int)fx + 3, y = (int)fy + 3;
    const ImVec2 m = Dpi_PanelMouse(ImVec2((float)x, (float)y), ImVec2((float)winW, (float)winH));   // layout px
    const bool hovered = g_showMenu && m.x >= x && m.x <= x + winW && m.y >= y && m.y <= y + winH && !GImGui->HoveredWindow;
    const bool dragging = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const auto faded = [&](int r, int g, int b, int a) { return IM_COL32(r, g, b, (int)(a * alpha)); };
    const auto rect = [&](int grow, int extra = 0) {
        return std::pair<ImVec2, ImVec2>{ ImVec2((float)(x - grow), (float)(y - grow)), ImVec2((float)(x + winW + grow + extra), (float)(y + winH + grow + extra)) };
    };
    const auto [fillMin, fillMax] = rect(3);
    dl->AddRectFilled(fillMin, fillMax, faded(7, 7, 7, 255), 5.f);
    const ImU32 rings[] = { faded(0, 0, 0, 255), faded(50, 50, 50, 255), faded(0, 0, 0, 255) };
    for (int g = 0; g < 3; ++g) { const auto [rmin, rmax] = rect(g); dl->AddRect(rmin, rmax, rings[g], 5.f); }
    if (hovered) { const auto [hmin, hmax] = rect(0, 1); dl->AddRect(hmin, hmax, faded(255, 255, 255, dragging ? 200 : 100), 5.f); }
    const auto [tw, th] = ts(titleF, "spectators");
    dl->AddText(titleF, titleF->FontSize, ImVec2((float)(x + winW / 2 - tw / 2), (float)(y + padY)), faded(255, 255, 255, 255), "spectators");
    int rowY = y + padY + th + 5;
    for (const auto& r : rows) {
        int rowX = x + padX;
        if (r.av) {
            dl->AddImageRounded(r.av, ImVec2((float)rowX, (float)rowY), ImVec2((float)(rowX + avatar), (float)(rowY + avatar)), ImVec2(0, 0), ImVec2(1, 1), faded(255, 255, 255, 255), avatar * 0.5f);
            rowX += avatar + 5;
        }
        const int textH = ts(font, r.text).second;
        dl->AddText(font, font->FontSize, ImVec2((float)rowX, (float)(rowY + r.h / 2 - textH / 2)), r.color, r.text.c_str());
        rowY += r.h + 3;
    }
}

// style 11: airplane
static void DrawAirplane(ImDrawList* dl)
{
    if (!IsLocalPlayerAlive() && !g_specAlwaysShow) return;
    ImFont* titleF = Font(g_fontAirplaneTitle);
    ImFont* rowF = Font(g_fontAirplaneRows);
    std::vector<SpecEntry> entries; Collect(entries);
    std::vector<std::string> rows;
    int w = 100;
    for (const SpecEntry& e : entries) {
        rows.push_back(e.name + " > " + e.target);
        const int rw = (int)TextW(rowF, rows.back());
        if (rw > w) w = rw + 5;
    }
    const int h = 15 + 15 * (int)rows.size();
    float x, y; Place((float)w, (float)h, x, y);
    const ImU32 left = U32(g_specAirplaneGradientColor);
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h), left, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), left);
    const bool outline = g_specAirplaneOutline;
    const auto drawText = [&](ImFont* f, float tx, float ty, ImU32 col, const char* t) {
        constexpr ImU32 sh = IM_COL32(0, 0, 0, 255);
        if (outline) {
            dl->AddText(f, f->FontSize, ImVec2(tx + 1.f, ty), sh, t);
            dl->AddText(f, f->FontSize, ImVec2(tx - 1.f, ty), sh, t);
            dl->AddText(f, f->FontSize, ImVec2(tx, ty + 1.f), sh, t);
            dl->AddText(f, f->FontSize, ImVec2(tx, ty - 1.f), sh, t);
        } else dl->AddText(f, f->FontSize, ImVec2(tx + 1.f, ty + 1.f), sh, t);
        dl->AddText(f, f->FontSize, ImVec2(tx, ty), col, t);
    };
    constexpr const char* titles[] = { "Spectators", "spectators", "SPECTATORS" };
    const char* title = titles[std::clamp(g_specAirplaneTitleCase, 0, 2)];
    const int tw = (int)TextW(titleF, title);
    drawText(titleF, x + (float)(w / 2 - tw / 2), y + 3.f, U32(g_specAirplaneTitleColor), title);
    const ImU32 lc = U32(g_specAirplaneLocalColor), oc = U32(g_specAirplaneOtherColor);
    for (size_t i = 0; i < rows.size(); i++)
        drawText(rowF, x + 3.f, y + 15.f + 15.f * (float)i, entries[i].watchingLocal ? lc : oc, rows[i].c_str());
}

// style 12: bhop cheat
static void DrawBhopcheat(ImDrawList* dl)
{
    ImFont* font = Font(g_fontTahomaBd12);
    std::vector<SpecEntry> entries; Collect(entries); EraseNotLocal(entries);
    if (entries.empty()) return;
    const auto modeName = [](int obs) -> const char* {
        switch (obs) {
        case OBS_IN_EYE: return "first person";
        case OBS_CHASE: return "3rd person";
        case OBS_ROAMING: return "noclip";
        case OBS_DEATHCAM: return "deathcam";
        case OBS_FREEZECAM: return "freezecam";
        case OBS_FIXED: return "fixed";
        default: return "";
        }
    };
    const bool showMode = g_specBhopMode, drawAvatar = g_specAvatar;
    const float x = drawAvatar ? 25.f : 5.f;
    for (size_t i = 0; i < entries.size(); i++) {
        const SpecEntry& e = entries[i];
        const float y = 5.f + font->FontSize * (float)i;
        const std::string text = showMode ? std::string(e.fake ? "[bot] " : "") + e.name + " | " + modeName(e.obsMode) : e.name;
        if (drawAvatar)
            if (ImTextureID t = Avatar(e.sid)) dl->AddImage(t, ImVec2(9.f, y), ImVec2(21.f, y + 12.f));
        dl->AddText(font, font->FontSize, ImVec2(x + 1.f, y + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
        dl->AddText(font, font->FontSize, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text.c_str());
    }
}

// style 13: millionware -- first/third person watchers of whoever we see (us alive, our target dead)
static void DrawMillionware(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Font(g_fontTahoma12);
    const bool localAlive = IsLocalPlayerAlive();
    int localTarget = 0;
    if (!localAlive) { localTarget = LocalObserverTarget(); if (localTarget < 0) return; }
    std::vector<SpecEntry> entries; Collect(entries, true);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const SpecEntry& e) {
        if (e.obsMode != OBS_IN_EYE && e.obsMode != OBS_CHASE) return true;
        return localAlive ? !e.watchingLocal : e.targetIndex != localTarget; }), entries.end());
    for (size_t i = 0; i < entries.size(); i++) {
        const float tw = TextW(font, entries[i].name);
        dl->AddText(font, font->FontSize, ImVec2(io.DisplaySize.x - tw - 4.f, 4.f + 16.f * (float)i), IM_COL32(255, 255, 255, 255), entries[i].name.c_str());
    }
}

// style 14: aimware
static void DrawAimware(ImDrawList* dl)
{
    ImFont* f = Font(g_fontSunflower30);
    const int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return;
    const int targetIdx = IsLocalPlayerAlive() ? localIdx : LocalObserverTarget();
    std::vector<SpecEntry> entries;
    if (targetIdx != -1 && !g_showMenu) {
        Collect(entries, true);
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const SpecEntry& e) {
            if (e.index == localIdx) return true;
            if (e.obsMode != OBS_IN_EYE && e.obsMode != OBS_CHASE) return true;
            return e.targetIndex != targetIdx; }), entries.end());
    }
    float x, y; Place(200.f, 24.f, x, y);
    constexpr ImU32 red = IM_COL32(200, 40, 40, 255), white = IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 200.f, y + 24.f), red, 3.f);
    dl->AddRectFilled(ImVec2(x, y + 23.f), ImVec2(x + 200.f, y + 24.f), red);
    constexpr auto title = "Spectators list";
    const float halfH = f->CalcTextSizeA(15.f, FLT_MAX, 0.f, title).y / 2.f;
    dl->AddText(f, 15.f, ImVec2(x + 8.f, y + (12.f - halfH)), white, title);
    float ext = 0.f;
    for (const SpecEntry& e : entries) ext += 8.f + f->CalcTextSizeA(14.f, FLT_MAX, 0.f, e.name.c_str()).y;
    ext += 8.f;
    dl->AddRectFilled(ImVec2(x, y + 24.f), ImVec2(x + 200.f, y + ext + 24.f), IM_COL32(0, 0, 0, 75));
    dl->AddRectFilled(ImVec2(x, y + ext + 24.f), ImVec2(x + 200.f, y + ext + 28.f), red);
    ext = 0.f;
    for (const SpecEntry& e : entries) {
        ext += 8.f;
        dl->AddText(f, 14.f, ImVec2(x + 6.f, y + ext + 24.f), white, e.name.c_str());
        ext += f->CalcTextSizeA(14.f, FLT_MAX, 0.f, e.name.c_str()).y;
    }
}

// style 15: havoc
static ImU32 HavocGrey(int shade, float alpha) { return IM_COL32(shade, shade, shade, (int)(255.f * alpha)); }
static void HavocLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) { dl->AddLine(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x + 0.5f, b.y + 0.5f), col); }
static void DrawHavoc(ImDrawList* dl, ImGuiIO& io)
{
    static ImVec2 absSize{ 120.f, 20.f };
    static float menuAlpha = 0.f;
    ImFont* font = Font(g_fontHavocTahoma13);
    const float dt = io.DeltaTime;
    menuAlpha = ImClamp(menuAlpha + (g_showMenu ? 5.f : -5.f) * dt, 0.f, 1.f);

    struct R { std::string name, mode; };
    std::vector<SpecEntry> entries; Collect(entries);
    std::vector<R> rows;
    ImVec2 target{ 120.f, 20.f };
    for (const SpecEntry& e : entries) {
        R r{ e.name, g_specShowTarget ? "[" + e.target + "]" : e.obsMode == OBS_CHASE ? "[Third person]" : "[First person]" };
        target.x = ImMax(target.x, VguiTextW(font, r.name + r.mode) + 20.f + 40.f);
        rows.push_back(std::move(r));
    }
    constexpr float appendLen = 17.f;
    if (!rows.empty() || menuAlpha > 0.f) target.y += 9.f + (float)rows.size() * appendLen;
    absSize.x += (target.x - absSize.x) * 10.f * dt;
    absSize.y += (target.y - absSize.y) * 10.f * dt;

    float alpha = 1.f;
    const bool alwaysShow = g_specAlwaysShow && IsInGame();
    if (!IsLocalPlayerAlive() && menuAlpha < 1.f && !alwaysShow) alpha = menuAlpha;
    if (alpha < 0.001f) return;
    if (absSize.y < 1.f) return;
    float px, py; Place(ImMax(absSize.x, 100.f), absSize.y, px, py);
    const ImVec2 pos(px, py), size = absSize;

    const auto shell = [&](float inset, int shade) { dl->AddRectFilled(ImVec2(pos.x + inset, pos.y + inset), ImVec2(pos.x + size.x - inset, pos.y + size.y - inset), HavocGrey(shade, alpha), 5.f); };
    shell(-1.f, 10); shell(0.f, 55); shell(1.f, 10); shell(2.f, 20);
    const ImU32 accent = U32(g_specHavocColor, alpha), accent2 = WithAlpha(U32(g_accentColor), alpha);
    for (int i = 0; i < 3; ++i) {
        const float f = (float)i;
        HavocLine(dl, ImVec2(pos.x + size.x - 19.f - f, pos.y + 2.f), ImVec2(pos.x + size.x - 4.f - f, pos.y + 17.f), accent);
        HavocLine(dl, ImVec2(pos.x + size.x - 12.f - f, pos.y + 2.f), ImVec2(pos.x + size.x - 2.f, pos.y + 12.f + f), accent2);
        HavocLine(dl, ImVec2(pos.x + 19.f + f, pos.y + 2.f), ImVec2(pos.x + 4.f + f, pos.y + 17.f), accent);
        HavocLine(dl, ImVec2(pos.x + 11.f + f, pos.y + 2.f), ImVec2(pos.x + 1.f, pos.y + 12.f + f), accent2);
    }
    if (size.y > 20.f) {
        HavocLine(dl, ImVec2(pos.x + 1.f, pos.y + 17.f), ImVec2(pos.x + size.x - 2.f, pos.y + 17.f), HavocGrey(10, alpha));
        HavocLine(dl, ImVec2(pos.x + 1.f, pos.y + 18.f), ImVec2(pos.x + size.x - 2.f, pos.y + 18.f), HavocGrey(55, alpha));
        HavocLine(dl, ImVec2(pos.x + 1.f, pos.y + 19.f), ImVec2(pos.x + size.x - 2.f, pos.y + 19.f), HavocGrey(10, alpha));
    }
    const std::string first = "Spec", second = "tators";
    const float titleX = floorf(size.x / 2.f - (VguiTextW(font, first) + VguiTextW(font, second)) / 2.f);
    VguiShadowText(dl, font, ImVec2(pos.x + titleX, pos.y + 3.f), HavocGrey(255, alpha), first.c_str());
    VguiShadowText(dl, font, ImVec2(pos.x + titleX + VguiTextW(font, first), pos.y + 3.f), accent, second.c_str());
    dl->PushClipRect(ImVec2(pos.x + 2.f, pos.y + 2.f), ImVec2(pos.x + size.x - 1.f, pos.y + size.y - 1.f), true);
    for (size_t i = 0; i < rows.size(); i++) {
        const float rowY = pos.y + 25.f + appendLen * (float)i;
        VguiShadowText(dl, font, ImVec2(pos.x + 20.f, rowY + 1.f), HavocGrey(255, alpha), rows[i].name.c_str());
        VguiShadowText(dl, font, ImVec2(pos.x + size.x - 20.f - VguiTextW(font, rows[i].mode), rowY), accent, rows[i].mode.c_str());
    }
    dl->PopClipRect();
}

// style 16: onetap
static void DrawOnetap(ImDrawList* dl)
{
    ImFont* f = Font(g_fontTahoma12);
    std::vector<SpecEntry> entries; Collect(entries, true); EraseNotLocal(entries);
    for (size_t i = 0; i < entries.size(); i++)
        dl->AddText(f, f->FontSize, ImVec2(20.f, 200.f + 12.f * (float)i), IM_COL32(230, 230, 230, 255), entries[i].name.c_str());
}

// style 17: airflow
static void AirflowLerp(float& v, bool cond, float speed) { v = ImClamp(ImLerp(v, cond ? 1.f : 0.f, ImGui::GetIO().DeltaTime * 25.f * speed), 0.f, 1.f); }
static void DrawAirflow(ImDrawList* dl)
{
    struct R { std::string name, chase; double start = 0.0; float step = 0.f; bool spectated = false, was = false; };
    static std::map<int, R> rows;
    static float alpha = 0.f;
    ImFont* font = Font(g_fontAirflow14);
    const bool inGame = IsInGame() && GetLocalPlayerIndex() > 0;
    std::vector<SpecEntry> entries;
    if (inGame) Collect(entries); else rows.clear();
    const auto cut = [](const std::string& t, size_t n) { const std::string k = Utf8Truncate(t, n); return k.size() < t.size() ? k + "..." : k; };
    for (auto& [i, r] : rows) r.spectated = false;
    for (const SpecEntry& e : entries) {
        R& r = rows[e.index];
        r.name = cut(e.name, 10);
        r.chase = "[ " + cut(e.target, 8) + " ]";
        r.spectated = true;
    }
    const double now = ImGui::GetTime();
    int active = 0;
    for (auto& [i, r] : rows) {
        if (r.spectated != r.was) { r.start = now; r.was = r.spectated; }
        active += r.was ? 1 : 0;
    }
    const float winH = 40.f + 25.f * (float)active;
    AirflowLerp(alpha, g_showMenu || (inGame && (active > 0 || g_specAlwaysShow)), 1.f);
    if (alpha <= 0.f) return;
    float px, py; Place(184.f, winH + 2.f, px, py);
    const float x = px + 4.f, y = py + 1.f;

    const ImDrawListFlags oldFlags = dl->Flags;
    dl->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;
    const int a = (int)(255.f * alpha);
    // donor: blurred backbuffer drawn tinted white (header) / 100 grey (body). BlurRect replaces the
    // rect in place, the body's 100/255 tint = black at 155/255 over it.
    BlurRectFade(dl, ImVec2(x, y), ImVec2(x + 176.f, y + winH), 1.f, alpha);
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + 176.f, y + winH), true);
    dl->AddRectFilled(ImVec2(x, y + 32.f), ImVec2(x + 176.f, y + 32.f + winH), IM_COL32(0, 0, 0, (int)(155.f * alpha)), 4.f, ImDrawFlags_RoundCornersBottom);
    if (ImTextureID icon = GifTex_Frame(GifTex_Get("airflow_spectators_icon", airflow_spectators_icon, sizeof(airflow_spectators_icon)), 0.0))
        dl->AddImage(icon, ImVec2(x + 41.f, y + 9.f), ImVec2(x + 57.f, y + 25.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a));
    dl->AddText(font, 14.f, ImVec2(x + 65.f, y + 8.f), IM_COL32(255, 255, 255, a), "spectators");
    dl->AddLine(ImVec2(x, y + 31.f), ImVec2(x + 176.f, y + 31.f), IM_COL32(255, 255, 255, (int)(12.75f * alpha)));
    dl->AddRect(ImVec2(x, y), ImVec2(x + 176.f, y + winH), IM_COL32(100, 100, 100, (int)(100.f * alpha)), 4.f);
    float maxPos = 0.f;
    for (auto it = rows.begin(); it != rows.end();) {
        R& r = it->second;
        float progress = ImClamp((float)((now - r.start) / 0.2), 0.f, 1.f);
        if (!r.spectated && !r.was) progress = 1.f - progress;
        if (progress <= 0.f) { it = r.was ? std::next(it) : rows.erase(it); continue; }
        const float rowY = y + 39.f + maxPos;
        dl->AddText(font, 14.f, ImVec2(x + 12.f, rowY), IM_COL32(255, 255, 255, (int)(255.f * alpha * progress)), r.name.c_str());
        dl->AddText(font, 14.f, ImVec2(x + 165.f - ImguiTextW(font, r.chase.c_str()), rowY), IM_COL32(255, 255, 255, (int)(0.4f * 255.f * alpha * progress)), r.chase.c_str());
        AirflowLerp(r.step, r.spectated && r.was, 0.6f);
        maxPos += 25.f * r.step;
        ++it;
    }
    dl->PopClipRect();
    dl->Flags = oldFlags;
}

// style 18: ev0lve
struct EvolveAnim {
    float value, start, end, time = 1.f;
    explicit EvolveAnim(float v) : value(v), start(v), end(v) {}
    void Direct(float to) { start = value; end = to; time = 0.f; }
    void Animate(float duration, bool easeOut) {
        time = ImMin(time + ImGui::GetIO().DeltaTime / duration, 1.f);
        const float u = easeOut ? 1.f - (1.f - time) * (1.f - time) * (1.f - time) : time;
        value = start + (end - start) * u;
    }
};
static void EvolveShadow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float radius, float alpha)
{
    const ImU32 dark = IM_COL32(0, 0, 0, (int)(255.f * alpha)), clear = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilled(mn, mx, dark);
    dl->AddRectFilledMultiColor(ImVec2(mn.x - radius, mn.y), ImVec2(mn.x, mx.y), clear, dark, dark, clear);
    dl->AddRectFilledMultiColor(ImVec2(mx.x, mn.y), ImVec2(mx.x + radius, mx.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(mn.x, mn.y - radius), ImVec2(mx.x, mn.y), clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y + radius), dark, dark, clear, clear);
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    const auto corner = [&](ImVec2 c, float from) {
        constexpr int seg = 8;
        dl->PrimReserve(seg * 3, seg * 3);
        for (int i = 0; i < seg; ++i) {
            const float a0 = from + IM_PI * 0.5f * (float)i / seg, a1 = from + IM_PI * 0.5f * (float)(i + 1) / seg;
            dl->PrimVtx(c, uv, dark);
            dl->PrimVtx(ImVec2(c.x + cosf(a0) * radius, c.y + sinf(a0) * radius), uv, clear);
            dl->PrimVtx(ImVec2(c.x + cosf(a1) * radius, c.y + sinf(a1) * radius), uv, clear);
        }
    };
    corner(mn, IM_PI); corner(ImVec2(mx.x, mn.y), IM_PI * 1.5f); corner(mx, 0.f); corner(ImVec2(mn.x, mx.y), IM_PI * 0.5f);
}
static void DrawEvolve(ImDrawList* dl)
{
    static EvolveAnim alpha{ 0.f }, width{ 160.f }, height{ 60.f };
    static bool hasContent = false;
    ImFont* font = Font(g_fontEvolve16);
    ImFont* bold = g_fontEvolveBold16 ? g_fontEvolveBold16 : font;
    const bool inGame = IsInGame();
    const bool forced = g_showMenu || (inGame && g_specAlwaysShow);
    alpha.Animate(0.1f, false); width.Animate(0.2f, true); height.Animate(0.2f, true);
    float px, py; Place(width.value, height.value, px, py);
    const ImVec2 mn(px, py), mx(mn.x + width.value, mn.y + height.value);
    const float maxAlpha = ImClamp(g_wmEvolveAlpha / 100.f, 0.f, 1.f);

    if (alpha.value > 0.f) {
        const float bodyAlpha = ImMin(alpha.value, maxAlpha);
        EvolveShadow(dl, mn, mx, 12.f, bodyAlpha * 0.25f);
        BlurRectFade(dl, mn, mx, 1.f, alpha.value);
        dl->AddRectFilled(mn, mx, IM_COL32(18, 18, 18, (int)(255.f * bodyAlpha)), 4.f);
        dl->AddRectFilled(mn, ImVec2(mn.x + width.value, mn.y + 24.f), IM_COL32(29, 29, 29, (int)(255.f * alpha.value)), 4.f, ImDrawFlags_RoundCornersTop);
        const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
        dl->AddRectFilled(ImVec2(mn.x, mn.y + 24.f), ImVec2(mn.x + width.value, mn.y + 25.f), ImColor(acc.x, acc.y, acc.z, alpha.value));
        constexpr auto title = "Spectators";
        const ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0.f, title);
        dl->AddText(bold, bold->FontSize, ImVec2(floorf(mn.x + (width.value - ts.x) * 0.5f), floorf(mn.y + (24.f - ts.y) * 0.5f)), IM_COL32(220, 220, 220, (int)(255.f * alpha.value)), title);
    }

    struct L { std::string name; uint64_t sid; bool fake; };
    std::vector<L> lines;
    float maxW = 0.f;
    if (inGame && IsLocalPlayerAlive()) {
        std::vector<SpecEntry> entries; Collect(entries, true);
        for (const SpecEntry& e : entries) {
            if (!e.watchingLocal) continue;
            maxW = ImMax(maxW, ImguiTextW(font, e.name.c_str()) + 60.f);
            lines.push_back({ e.name, e.sid, e.fake });
        }
    }
    maxW = ImClamp(maxW, 160.f, 400.f);
    if (((lines.empty() && hasContent) || !inGame) && !forced) {
        hasContent = false;
        width.Direct(160.f); height.Direct(30.f); alpha.Direct(0.f);
        return;
    }
    if ((!lines.empty() && !hasContent && inGame) || forced) { hasContent = true; alpha.Direct(1.f); }
    if (const float h = ((float)lines.size() + 1.f) * 24.f + 10.f; h != height.end) { width.Direct(maxW); height.Direct(h); }
    if (alpha.value > 0.f) {
        const int a = (int)(255.f * alpha.value);
        dl->PushClipRect(mn, mx, true);
        float offset = 32.f;
        for (const L& l : lines) {
            if (ImTextureID t = l.fake ? (ImTextureID)0 : Avatar(l.sid))
                dl->AddImageRounded(t, ImVec2(mn.x + 10.f, mn.y + offset), ImVec2(mn.x + 30.f, mn.y + offset + 20.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a), 10.f);
            dl->AddText(font, font->FontSize, ImVec2(mn.x + 38.f, mn.y + offset), IM_COL32(255, 255, 255, a), l.name.c_str());
            offset += 24.f;
        }
        dl->PopClipRect();
    }
}

// style 19: legendware
static void DrawLegendware(ImDrawList* dl, ImGuiIO& io)
{
    if (!IsLocalPlayerAlive()) return;
    ImFont* f = Font(g_fontLegendwareLucida10);
    std::vector<SpecEntry> entries; Collect(entries, true); EraseNotLocal(entries);
    const float top = g_wmEnable ? 30.f : 6.f;
    for (size_t i = 0; i < entries.size(); i++)
        VguiShadowText(dl, f, ImVec2(io.DisplaySize.x - (VguiTextW(f, entries[i].name) + 6.f), top + 16.f * (float)i), IM_COL32(255, 255, 255, 255), entries[i].name.c_str());
}

// style 20: interium
static ImU32 InteriumHsv(float hue, int alpha)
{
    float r = 0.f, g = 0.f, b = 0.f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.f, 0.7f, r, g, b);
    return IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), alpha);
}
static void InteriumBand(ImDrawList* dl, float x, float y, float w, float dt)
{
    static float hueL = 0.f, hueR = 0.25f;
    static int lastFrame = -1;
    if (ImGui::GetFrameCount() != lastFrame) {
        lastFrame = ImGui::GetFrameCount();
        hueL = fmodf(hueL + dt * 0.1f, 1.f);
        hueR = fmodf(hueR + dt * 0.1f, 1.f);
    }
    dl->AddRectFilledMultiColor(ImVec2(x, y + 11.f), ImVec2(x + w, y + 22.f), InteriumHsv(hueL, 0), InteriumHsv(hueR, 0), InteriumHsv(hueR, 70), InteriumHsv(hueL, 70));
    dl->AddRectFilledMultiColor(ImVec2(x, y + 22.f), ImVec2(x + w, y + 24.f), InteriumHsv(hueL, 255), InteriumHsv(hueR, 255), InteriumHsv(hueR, 255), InteriumHsv(hueL, 255));
    dl->AddRectFilledMultiColor(ImVec2(x + 2.f, y + 24.f), ImVec2(x + w - 2.f, y + 29.f), InteriumHsv(hueL, 70), InteriumHsv(hueR, 70), InteriumHsv(hueR, 0), InteriumHsv(hueL, 0));
}
static void DrawInterium(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* f = Font(g_fontInteriumDroid24);
    std::string list;
    const int localIdx = GetLocalPlayerIndex();
    if (localIdx > 0 && IsInGame()) {
        const int targetIdx = IsLocalPlayerAlive() ? localIdx : LocalObserverTarget();
        if (targetIdx != -1) {
            std::vector<SpecEntry> entries; Collect(entries, true); SortByIndex(entries);
            for (const SpecEntry& e : entries) {
                if (e.targetIndex != targetIdx || e.index == localIdx || e.name.empty()) continue;
                if (e.obsMode == OBS_IN_EYE || e.obsMode == OBS_CHASE) list += e.name + "\n";
            }
        }
    }
    constexpr float w = 150.f;
    const float h = list.empty() ? 33.f : f->CalcTextSizeA(16.f, FLT_MAX, 0.f, list.c_str()).y + 31.f;
    float x, y; Place(w, h, x, y);
    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 22.f), U32(g_specInteriumBg), 3.f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(x + 2.f, y + 22.f), ImVec2(x + w - 2.f, y + h), U32(g_specInteriumBg, 127.f / 255.f), 6.f, ImDrawFlags_RoundCornersBottom);
    constexpr auto title = "Spectator List";
    dl->AddText(f, 16.f, ImVec2(x + w * 0.5f - f->CalcTextSizeA(16.f, FLT_MAX, 0.f, title).x * 0.5f, y + 2.f), white, title);
    InteriumBand(dl, x, y, w, io.DeltaTime);
    if (!list.empty()) dl->AddText(f, 16.f, ImVec2(x + 8.f, y + 26.f), white, list.c_str());
}

// style 21: chillware v2 -- a real ImGui window (auto-resize, draggable while the menu is open). Its
// position is saved in g_specCv2X/Y (imgui.ini is off in this dll), re-applied when a cfg load moves it.
static void DrawChillwareV2()
{
    ImFont* titleF = Font(g_menuBold);   // verdana_bd_11
    ImFont* rowF = Font(g_menuReg);      // verdana_11
    std::vector<SpecEntry> entries; Collect(entries, true);
    const int localIdx = GetLocalPlayerIndex();
    const int watched = IsLocalPlayerAlive() ? localIdx : LocalObserverTarget();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const SpecEntry& e) { return e.index == localIdx; }), entries.end());
    if (entries.empty() && !g_showMenu && !(g_specAlwaysShow && IsInGame())) return;

    const int ar = (int)(g_accentColor[0] * 255.f), ag = (int)(g_accentColor[1] * 255.f), ab = (int)(g_accentColor[2] * 255.f);
    int flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (!g_showMenu) flags |= ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(32.f, 32.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 5.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(7, 7, 7, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(ar, ag, ab, 50));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, IM_COL32(3, 3, 3, 255));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(3, 3, 3, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    // last position we wrote; a differing g_specCv2X/Y = cfg load -> force it
    static float s_lastX = -2.f, s_lastY = -2.f;
    if (g_specCv2X < 0.f || g_specCv2Y < 0.f)
        ImGui::SetNextWindowPos(ImVec2(15.f, ImGui::GetMainViewport()->Size.y * 0.3f), ImGuiCond_FirstUseEver);   // logical px (render/dpi.cpp)
    else if (g_specCv2X != s_lastX || g_specCv2Y != s_lastY)
        ImGui::SetNextWindowPos(ImVec2(g_specCv2X, g_specCv2Y), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(100.f, 20.f), ImVec2(400.f, 800.f));

    ImGui::PushFont(titleF);
    ImGui::Begin("spectator list##botox-spectators-chillware-v2", nullptr, flags);
    ImGui::PopFont();
    {
        ImGui::PushFont(rowF);
        for (const SpecEntry& e : entries) {
            const char* mode = e.obsMode == OBS_IN_EYE ? "fp" : e.obsMode == OBS_CHASE ? "tp" : e.obsMode == OBS_ROAMING ? "roam" : "?";
            const std::string text = e.name + " -" + mode + "-> " + e.target;
            const ImVec4 color = e.targetIndex == watched ? ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], g_accentColor[3]) : ImColor(90, 90, 90, 200).Value;
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(text.c_str()).x) * 0.5f);
            ImGui::TextColored(color, "%s", text.c_str());
        }
        ImGui::PopFont();

        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        g_specCv2X = s_lastX = pos.x;
        g_specCv2Y = s_lastY = pos.y;
        const float left = pos.x, top = pos.y, right = pos.x + size.x, bottom = pos.y + size.y;
        const float midX = left + size.x * 0.5f, midY = top + size.y * 0.5f;
        const ImU32 solid = IM_COL32(ar, ag, ab, 255), clear = IM_COL32(ar, ag, ab, 0);
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->PushClipRectFullScreen();
        wdl->AddRectFilledMultiColor(ImVec2(left - 1.f, top - 1.f), ImVec2(midX + 1.f, top), solid, clear, clear, solid);
        wdl->AddRectFilledMultiColor(ImVec2(left - 1.f, top), ImVec2(left, midY + 1.f), solid, solid, clear, clear);
        wdl->AddRectFilledMultiColor(ImVec2(midX, bottom), ImVec2(right, bottom + 1.f), clear, solid, solid, clear);
        wdl->AddRectFilledMultiColor(ImVec2(right, midY), ImVec2(right + 1.f, bottom + 1.f), clear, clear, solid, solid);
        wdl->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(7);
}

// style 22: skebob -- donor runs its own scan (rows with or without a target); ported on the shared
// one, so a dead player with no observer target is left out.
static void DrawSkebob(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Font(g_fontSkebobArial12);
    ImFont* titleF = g_fontSkebobArial13 ? g_fontSkebobArial13 : font;
    struct R { std::string text; uint64_t sid; bool highlight; int index; };
    std::vector<R> rows;
    const bool showTarget = g_specShowTarget;
    const bool localOnly = !showTarget || g_specListType == 1;
    const int localIdx = GetLocalPlayerIndex();
    for (const SpecEntry& e : RawEntries()) {
        if (e.hltv) continue;
        const bool self = e.index == localIdx;
        if (localOnly && (self || !e.watchingLocal)) continue;
        rows.push_back({ showTarget ? e.name + " -> " + e.target : e.name, e.sid, self || e.watchingLocal, e.index });
    }
    std::sort(rows.begin(), rows.end(), [](const R& a, const R& b) { return a.index < b.index; });
    if (rows.empty() && !g_specAlwaysShow) return;

    const bool background = g_specSkebobBg, avatars = g_specAvatar, title = g_specSkebobTitle;
    const float titleH = title ? 40.f : 20.f, rowH = avatars ? 25.f : 15.f, textX = avatars ? 35.f : 10.f;
    float widest = title ? TextWs(titleF, 12.f, "spectators") + 20.f : 0.f;
    for (const R& r : rows) widest = ImMax(widest, textX + TextWs(font, 12.f, r.text) + 10.f);
    const float w = ImMax(210.f, widest);
    const float h = (title ? 30.f : 10.f) + (float)rows.size() * rowH;
    float x, y; Place(w, h, x, y);
    const ImVec2 m = Dpi_PanelMouse(ImVec2(x, y), ImVec2(w, h));   // layout px
    const bool hovered = g_showMenu && m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h;
    if (background) {
        dl->AddRectFilled(ImVec2(x + 1.f, y + 1.f), ImVec2(x + w - 1.f, y + h - 1.f), IM_COL32(19, 19, 19, 242), 5.f);
        dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(x + w - 1.f, y + h - 1.f), AccentU32(hovered ? 255 : 200), 5.f, 0, 1.f);
    }
    const auto rp = [](float v) { return floorf(v + 0.5f); };
    if (title) {
        const float tx = x + 3.f + (w - 3.f) / 2.f - TextWs(titleF, 12.f, "spectators") / 2.f, ty = y + 10.f;
        dl->AddText(titleF, 12.f, ImVec2(rp(tx + 1.f), rp(ty + 1.f)), IM_COL32(0, 0, 0, 255), "spectators");
        dl->AddText(titleF, 12.f, ImVec2(rp(tx), rp(ty)), IM_COL32(255, 255, 255, 255), "spectators");
    }
    constexpr ImVec2 diag[4] = { { 1.f, 1.f }, { -1.f, -1.f }, { 1.f, -1.f }, { -1.f, 1.f } };
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + titleH - 3.f + (float)i * rowH;
        if (avatars)
            if (ImTextureID t = Avatar(rows[i].sid))
                dl->AddImageRounded(t, ImVec2(x + 10.f, rowY - 10.f), ImVec2(x + 30.f, rowY + 10.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10.f);
        const float tx = x + textX, ty = rowY - 6.f;
        for (const auto& o : diag) dl->AddText(font, 12.f, ImVec2(rp(tx + o.x), rp(ty + o.y)), IM_COL32(0, 0, 0, 255), rows[i].text.c_str());
        dl->AddText(font, 12.f, ImVec2(rp(tx), rp(ty)), rows[i].highlight ? AccentU32(255) : IM_COL32(255, 255, 255, 255), rows[i].text.c_str());
    }
}

// style 23: cumidere
static void DrawCumidere(ImDrawList* dl)
{
    if (!IsInGame()) return;
    ImFont* f = Font(g_fontCumidereTahoma16);
    std::vector<SpecEntry> entries; Collect(entries, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget || g_specListType == 1) EraseNotLocal(entries);
    if (entries.empty() && !g_specAlwaysShow) return;
    SortByIndex(entries);
    const float size = f->FontSize, rowH = size + 4.f, header = size + 8.f;
    std::vector<std::string> texts;
    float widest = 0.f;
    for (const SpecEntry& e : entries) { texts.push_back(showTarget ? e.name + " -> " + e.target : e.name); widest = ImMax(widest, TextWs(f, size, texts.back())); }
    const float titleW = TextWs(f, size, "spectators");
    const float w = ImMax(ImMax(widest, titleW), titleW * 2.f) + 16.f;
    const float h = (float)entries.size() * rowH + header + 1.f;
    float x, y; Place(w, h, x, y);
    const ImU32 accent = AccentU32(255);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(23, 23, 23, 255));
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), accent, 1.f);
    dl->AddLine(ImVec2(x, y), ImVec2(x, y + h), accent, 1.f);
    dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + h), accent, 1.f);
    dl->AddText(f, size, ImVec2(x + 8.f, y + 4.f), accent, "spectators");
    dl->AddLine(ImVec2(x, y + header), ImVec2(x + w, y + header), accent, 1.f);
    for (size_t i = 0; i < entries.size(); ++i)
        dl->AddText(f, size, ImVec2(x + 8.f, y + header + 1.f + (float)i * rowH), U32(entries[i].watchingLocal ? g_specColorOne : g_specColorTwo), texts[i].c_str());
}

// style 24: illusory
static void DrawIllusory(ImDrawList* dl, ImGuiIO& io)
{
    if (!IsInGame()) return;
    ImFont* f = Font(g_fontTahomaBd12);
    std::vector<SpecEntry> entries; Collect(entries, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget) EraseNotLocal(entries);
    SortByIndex(entries);
    const bool trackLine = g_mpEnable && MediaPlayer_HasSession();
    const float top = (g_wmEnable ? 30.f : 5.f) + (trackLine ? 15.f : 0.f);
    const int w = (int)io.DisplaySize.x;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string text = showTarget ? entries[i].name + " -> " + entries[i].target : entries[i].name;
        const int tw = (int)TextWs(f, 12.f, text);
        const float y = top + 16.f * (float)i;
        dl->AddText(f, 12.f, ImVec2((float)(w - tw - 5), y + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
        dl->AddText(f, 12.f, ImVec2((float)(w - 6 - tw), y), U32(entries[i].watchingLocal ? g_specColorOne : g_specColorTwo), text.c_str());
    }
}

// style 25: lumi -- type 0 default, 1 new (+ mascot), 2 interwebz
static void DrawLumiDefault(ImDrawList* dl, ImGuiIO& io)
{
    struct R { std::string text; int index = 0; uint64_t sid = 0; bool local = false, seen = false; float progress = 0.f; };
    static std::vector<R> rows;
    ImFont* font = Font(g_fontLumiRubik16);
    if (!IsInGame()) { rows.clear(); return; }
    std::vector<SpecEntry> entries; Collect(entries, true, !g_specLumiHideTv);
    const bool showTarget = g_specShowTarget;
    for (auto& r : rows) r.seen = false;
    for (const SpecEntry& e : entries) {
        auto it = std::find_if(rows.begin(), rows.end(), [&](const R& r) { return r.index == e.index; });
        if (it == rows.end()) { rows.push_back({}); it = rows.end() - 1; it->index = e.index; }
        it->text = showTarget ? e.name + "   >   " + e.target : e.name;
        it->sid = e.sid; it->local = e.watchingLocal; it->seen = true;
    }
    const float step = io.DeltaTime * 4.f;
    for (auto& r : rows) r.progress = ImClamp(r.progress + (r.seen ? step : -step), 0.f, 1.f);
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const R& r) { return !r.seen && r.progress <= 0.f; }), rows.end());
    if (rows.empty() && !g_specAlwaysShow) return;

    const bool avatars = g_specAvatar;
    const float size = font->FontSize, pad = avatars ? 20.f : 17.f;
    float widest = ceilf(TextWs(font, size, "Spectators"));
    for (const R& r : rows) widest = ImMax(widest, ceilf(TextWs(font, size, r.text)) + pad);
    const float w = ImMax(200.f, widest + 30.f);
    float rowsH = 0.f;
    if (rows.empty()) rowsH = 17.f;
    else for (size_t i = 0; i < rows.size(); ++i) rowsH += rows[i].progress * 17.f + (i + 1 < rows.size() ? 1.f : 0.f);
    const float h = size + 17.f + rowsH;
    float x, y; Place(w, h, x, y);
    const bool blur = g_specLumiBlur;
    if (g_specLumiGlow) dl->AddRectFilled(ImVec2(x + 3.f, y + 3.f), ImVec2(x + w + 3.f, y + h + 3.f), IM_COL32(0, 0, 0, 128), 5.f);
    if (blur) BlurRect(dl, ImVec2(x, y), ImVec2(x + w, y + h), 1.f, 5.f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(13, 13, 13, blur ? 210 : 255), 5.f);
    const float titleW = ceilf(TextWs(font, size, "Spectators"));
    dl->AddText(font, size, ImVec2(x + (w - titleW) * 0.5f, y + 4.f), IM_COL32(255, 255, 255, 255), "Spectators");
    float rowY = y + size + 12.f;
    if (rows.empty()) dl->AddText(font, size, ImVec2(x + 10.f, rowY), IM_COL32(100, 100, 100, 255), "No spectators...");
    const ImU32 local = U32(g_specColorOne);
    for (const R& r : rows) {
        const float rh = r.progress * 17.f;
        if (rh > 0.f) {
            dl->PushClipRect(ImVec2(x + 10.f, rowY), ImVec2(x + w - 10.f, rowY + rh), true);
            float tx = x + 10.f;
            if (avatars) {
                const ImVec2 mn(tx, rowY + 1.5f), mx(tx + 14.f, rowY + 15.5f);
                if (ImTextureID t = Avatar(r.sid)) dl->AddImageRounded(t, mn, mx, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 7.f);
                else { dl->AddRectFilled(mn, mx, IM_COL32(70, 70, 70, 255), 7.f); dl->AddRect(mn, mx, IM_COL32(0, 0, 0, 220), 7.f, 0, 1.f); }
                tx += 20.f;
            }
            dl->AddText(font, size, ImVec2(tx, rowY), r.local ? local : IM_COL32(170, 170, 170, 255), r.text.c_str());
            dl->PopClipRect();
        }
        rowY += rh + 1.f;
    }
}

static void DrawLumiNew(ImDrawList* dl, ImGuiIO& io)
{
    if (!IsInGame()) return;
    ImFont* font = Font(g_fontLumiRubik16);
    std::vector<SpecEntry> entries; Collect(entries, false, !g_specLumiHideTv);
    if (entries.empty() && !g_specAlwaysShow) return;
    const bool avatars = g_specAvatar;
    const float size = font->FontSize, pad = avatars ? 20.f : 17.f;
    std::vector<std::string> texts;
    float widest = 120.f;
    for (const SpecEntry& e : entries) { texts.push_back(e.name + "   >   " + e.target); widest = ImMax(widest, TextWs(font, size, texts.back()) + pad); }
    const float w = widest + 30.f;
    const float h = (entries.empty() ? 22.f : (float)entries.size() * 22.f) + 40.f;
    float x, y; Place(w, h, x, y);
    const bool blur = g_specLumiBlur;
    const ImVec2 mn(x, y), mx(x + w, y + h);
    if (blur) BlurRect(dl, mn, mx, 1.f, 6.f);
    dl->AddRectFilled(mn, mx, IM_COL32(16, 16, 16, blur ? 210 : 255), 6.f);
    dl->AddRect(mn, mx, IM_COL32(0, 0, 0, 255), 6.f, 0, 1.f);
    const float headY = y + (28.f - size) * 0.5f;
    const std::string count = "(" + std::to_string(entries.size()) + ")";
    dl->AddText(font, size, ImVec2(x + 10.f, headY), IM_COL32(255, 255, 255, 255), "spectators");
    dl->AddText(font, size, ImVec2(x + w - 10.f - TextWs(font, size, count), headY), AccentU32(255), count.c_str());
    dl->AddLine(ImVec2(x + 10.f, y + 28.f), ImVec2(x + w - 10.f, y + 28.f), IM_COL32(36, 36, 36, 255), 1.f);
    const float rowsY = y + 34.f, textDy = (22.f - size) * 0.5f;
    if (entries.empty())
        dl->AddText(font, size, ImVec2(x + (w - TextWs(font, size, "no spectators")) * 0.5f, rowsY + textDy), IM_COL32(170, 170, 170, 255), "no spectators");
    const ImU32 local = U32(g_specColorOne);
    for (size_t i = 0; i < entries.size(); ++i) {
        const SpecEntry& e = entries[i];
        const float rowY = rowsY + (float)i * 22.f;
        float tx = x + 10.f;
        if (avatars) {
            const ImVec2 amn(tx, rowY + 4.f), amx(tx + 14.f, rowY + 18.f);
            if (ImTextureID t = Avatar(e.sid)) dl->AddImageRounded(t, amn, amx, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 7.f);
            else { dl->AddRectFilled(amn, amx, IM_COL32(70, 70, 70, 255), 7.f); dl->AddRect(amn, amx, IM_COL32(0, 0, 0, 220), 7.f, 0, 1.f); }
            tx += 20.f;
        }
        dl->AddText(font, size, ImVec2(tx, rowY + textDy), e.watchingLocal ? local : IM_COL32(170, 170, 170, 255), texts[i].c_str());
    }

    if (!g_specLumiMascot) return;
    const ImTextureID mascot = FileImageFrame(s_lumiMascot, g_specLumiMascotPath);
    if (!mascot) return;
    const float side = g_specLumiMascotSize;
    g_specLumiMascotX = ImClamp(g_specLumiMascotX, -100.f, 100.f);
    g_specLumiMascotY = ImClamp(g_specLumiMascotY, -150.f, 150.f);
    const ImVec2 at(x + (w - side) * 0.5f + g_specLumiMascotX, y - side + g_specLumiMascotY);
    static bool dragging = false;
    static ImVec2 grabMouse, grabOffset;
    if (g_showMenu) {
        // botox: screen mouse against where the mascot shows, the move divided back by the scale
        const ImVec2 m = Dpi_ScreenMouse();
        const ImVec2 shown = Dpi_PanelPos(at, ImVec2(side, side));
        const float scale = Dpi_PanelScale();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
        else if (!dragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !GImGui->HoveredWindow &&
                 m.x >= shown.x && m.x <= shown.x + side * scale && m.y >= shown.y && m.y <= shown.y + side * scale) {
            dragging = true; grabMouse = m; grabOffset = ImVec2(g_specLumiMascotX, g_specLumiMascotY);
        }
        if (dragging && scale > 0.f) {
            g_specLumiMascotX = ImClamp(grabOffset.x + (m.x - grabMouse.x) / scale, -100.f, 100.f);
            g_specLumiMascotY = ImClamp(grabOffset.y + (m.y - grabMouse.y) / scale, -150.f, 150.f);
        }
    } else dragging = false;
    ImDrawList* const fg = ImGui::GetForegroundDrawList();
    DpiPanelScope top(fg);   // botox top_block: the mascot pivots on its own
    fg->AddImage(mascot, at, ImVec2(at.x + side, at.y + side));
}

static void DrawLumiInterwebz(ImDrawList* dl)
{
    if (!IsInGame()) return;
    ImFont* font = Font(g_fontLumiRubik16);
    ImFont* titleF = g_fontLumiProggy13 ? g_fontLumiProggy13 : font;
    std::vector<SpecEntry> entries; Collect(entries, true, !g_specLumiHideTv);
    if (entries.empty() && !g_specAlwaysShow) return;
    const bool showTarget = g_specShowTarget;
    const float size = font->FontSize;
    const auto widthOf = [&](ImFont* f, const std::string& t) { return ceilf(TextWs(f, f->FontSize, t)); };
    struct R { std::string name, sep, target; float nw = 0.f, sw = 0.f, tw = 0.f; bool local = false; };
    std::vector<R> rows;
    float widest = widthOf(titleF, "spectators");
    for (const SpecEntry& e : entries) {
        R r{ e.name, showTarget ? " -> " : "", showTarget ? e.target : "" };
        r.nw = widthOf(font, r.name);
        r.sw = r.sep.empty() ? 0.f : widthOf(font, r.sep);
        r.tw = r.target.empty() ? 0.f : widthOf(font, r.target);
        r.local = e.watchingLocal;
        widest = ImMax(widest, r.nw + r.sw + r.tw);
        rows.push_back(std::move(r));
    }
    const float w = ImMax(200.f, widest + 96.f);
    const float rowsH = (float)rows.size() * 22.f;
    float x, y; Place(w + 20.f, 23.f + rowsH, x, y);
    const float bottom = y + 23.f + rowsH;
    const ImVec2 shape[6] = { ImVec2(x + 23.f, y), ImVec2(x + w + 20.f, y), ImVec2(x + w, y + 23.f), ImVec2(x + w, bottom), ImVec2(x, bottom), ImVec2(x, y + 23.f) };
    dl->AddConvexPolyFilled(shape, 6, IM_COL32(52, 52, 52, 255));
    const ImU32 rim = IM_COL32(150, 150, 150, 50);
    dl->AddLine(ImVec2(x + 24.f, y + 1.f), ImVec2(x + w + 19.f, y + 1.f), rim, 1.f);
    dl->AddLine(ImVec2(x + w + 19.f, y + 1.f), ImVec2(x + w - 1.f, y + 22.f), rim, 1.f);
    dl->AddLine(ImVec2(x + 1.f, y + 22.f), ImVec2(x + 24.f, y + 1.f), rim, 1.f);
    dl->AddLine(ImVec2(x + 1.f, y + 22.f), ImVec2(x + w - 1.f, y + 22.f), rim, 1.f);
    if (!rows.empty()) {
        const ImVec2 frame[4] = { ImVec2(x, y + 23.f), ImVec2(x, bottom), ImVec2(x + w, bottom), ImVec2(x + w, y + 23.f) };
        dl->AddPolyline(frame, 4, IM_COL32(69, 69, 69, 255), ImDrawFlags_None, 2.f);
    }
    const ImU32 lc = U32(g_specColorOne), oc = U32(g_specColorTwo);
    for (size_t i = 0; i < rows.size(); ++i) {
        const R& r = rows[i];
        const float rowY = y + 23.f + (float)i * 22.f;
        dl->AddRectFilled(ImVec2(x, rowY), ImVec2(x + w, rowY + 22.f), (i & 1) ? IM_COL32(69, 69, 69, 255) : IM_COL32(41, 41, 41, 255));
        const ImU32 c = r.local ? lc : oc;
        const float ty = rowY + 11.f - size * 0.5f - 1.f, tx = x + (w - (r.nw + r.sw + r.tw)) * 0.5f;
        dl->AddText(font, size, ImVec2(tx, ty), c, r.name.c_str());
        if (!r.sep.empty()) {
            dl->AddText(font, size, ImVec2(tx + r.nw, ty), c, r.sep.c_str());
            dl->AddText(font, size, ImVec2(tx + r.nw + r.sw, ty), c, r.target.c_str());
        }
    }
    const float titleW = widthOf(titleF, "spectators");
    dl->AddText(titleF, titleF->FontSize, ImVec2(x + w * 0.5f - titleW * 0.5f, y + (23.f - titleF->FontSize) * 0.5f), lc, "spectators");
}

// style 26: billware
static void BillwareRounded(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 bg, ImU32 ul, ImU32 ur, ImU32 br, ImU32 bl, float rounding)
{
    const float r = (std::min)(rounding, (std::min)(fabsf(mx.x - mn.x) * 0.5f - 1.f, fabsf(mx.y - mn.y) * 0.5f - 1.f));
    if (r <= 0.f) return;
    dl->AddRectFilledMultiColor(mn, mx, ul, ur, br, bl);
    struct C { ImVec2 corner, center; float a0, a1; };
    const C corners[] = {
        { mn, ImVec2(mn.x + r, mn.y + r), 4.82f, 3.1f },
        { ImVec2(mx.x, mn.y), ImVec2(mx.x - r, mn.y + r), 6.34f, 4.62f },
        { mx, ImVec2(mx.x - r, mx.y - r), 7.96f, 6.24f },
        { ImVec2(mn.x, mx.y), ImVec2(mn.x + r, mx.y - r), 9.5f, 7.77f },
    };
    for (auto& c : corners) { dl->PathLineTo(c.corner); dl->PathArcTo(c.center, r, c.a0, c.a1, 0); dl->PathFillConvex(bg); }
}
static void BillwareBox(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float alpha)
{
    const auto grey = [&](float v, float a) { return IM_COL32((int)(v * 255.f), (int)(v * 255.f), (int)(v * 255.f), (int)(a * 255.f)); };
    const auto grown = [&](float g) { return std::pair{ ImVec2(mn.x - g, mn.y - g), ImVec2(mx.x + g, mx.y + g) }; };
    const ImU32 dark = grey(0.07f, alpha), light = grey(0.14f, alpha);
    auto [fmn, fmx] = grown(2.f);
    BillwareRounded(dl, fmn, fmx, dark, dark, dark, light, light, 5.5f);
    BillwareRounded(dl, fmn, fmx, dark, light, light, dark, dark, 5.5f);
    const ImU32 rings[3] = { grey(0.15f, alpha), grey(0.3f, alpha * 0.7f), grey(0.15f, alpha) };
    for (int i = 0; i < 3; ++i) { auto [rmn, rmx] = grown(2.f + (float)i); dl->AddRect(rmn, rmx, rings[i], 5.5f, 0, 1.f); }
}
static void DrawBillware(ImDrawList* dl, ImGuiIO& io)
{
    if (!IsInGame()) return;
    ImFont* f = Font(g_fontTahomaBd12);
    std::vector<SpecEntry> entries; Collect(entries, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget) EraseNotLocal(entries);
    SortByIndex(entries);
    static float fade = 0.f;
    const float step = io.DeltaTime * 25.5f;
    fade = (!entries.empty() || g_specAlwaysShow) ? ImMin(fade + step, 1.f) : ImMax(fade - step, 0.f);
    if (fade <= 0.f) return;
    std::vector<std::string> texts;
    float widest = 0.f;
    for (const SpecEntry& e : entries) { texts.push_back(showTarget ? e.name + " -> " + e.target : e.name); widest = ImMax(widest, TextWs(f, 12.f, texts.back())); }
    const float w = widest + 14.f, h = (float)entries.size() * 16.f + 14.f;
    float x, y; Place(w, h, x, y);
    BillwareBox(dl, ImVec2(x, y), ImVec2(x + w, y + h), fade);
    const ImU32 shadow = AccentU32((int)(ImSaturate(fade * 255.f) * 255.f + 0.5f));
    const int ta = (int)(fade * 255.f);
    for (size_t i = 0; i < entries.size(); ++i) {
        const ImVec2 pos(x + 7.f, y + 7.f + (float)i * 16.f);
        const float* c = entries[i].watchingLocal ? g_specColorOne : g_specColorTwo;
        dl->AddText(f, 12.f, ImVec2(pos.x + 1.f, pos.y + 1.f), shadow, texts[i].c_str());
        dl->AddText(f, 12.f, pos, IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), ta * (int)(c[3] * 255.f) / 255), texts[i].c_str());
    }
}

// style 27: inkabanium
static void DrawInkabanium(ImDrawList* dl)
{
    ImFont* f = Font(g_fontInbaTahoma13);
    std::vector<SpecEntry> entries; Collect(entries);
    if (entries.empty() && !g_specAlwaysShow) return;
    const float line = f->FontSize;
    const float textH = entries.empty() ? line : line * (float)(entries.size() + 1);
    constexpr float w = 195.f, titleH = 19.f;
    const float h = textH + 30.f;
    float x, y; Place(w, h, x, y);
    ImU32 color;
    if (g_specInbaRainbow) {
        const float t = (float)ImGui::GetTime() * g_specInbaRainbowSpeed;
        const auto ch = [&](float phase) { return (int)((sinf(t + phase) + 1.f) * 0.5f * 255.f); };
        color = IM_COL32(ch(0.f), ch(2.0943952f), ch(4.1887903f), 255);
    } else color = IM_COL32((int)(g_specColorOne[0] * 255.f), (int)(g_specColorOne[1] * 255.f), (int)(g_specColorOne[2] * 255.f), 255);
    dl->AddRectFilled(ImVec2(x, y + titleH), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.05f, 0.05f, 0.7f)));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.05f, 0.05f, 1.f)));
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), AccentU32(255), 0.f, 0, 1.f);
    const float tw = f->CalcTextSizeA(line, FLT_MAX, 0.f, "SPECTATORS").x;
    dl->AddText(f, line, ImVec2(floorf(x + (w - tw) * 0.5f), y + 3.f), color, "SPECTATORS");
    for (size_t i = 0; i < entries.size(); ++i)
        dl->AddText(f, line, ImVec2(x + 8.f, y + titleH + 8.f + line * (float)i), color, entries[i].name.c_str());
}

// style 28: dna
static ImU32 DnaGrey(float v, float a = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(v, v, v, a)); }
static ImU32 DnaAccent(float a) { const ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Accent); return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)); }
static void DrawDna(ImDrawList* dl, ImGuiIO& io)
{
    if (!IsInGame()) return;
    ImFont* f = Font(g_fontDnaMontserrat15);
    std::vector<SpecEntry> entries; Collect(entries, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget) EraseNotLocal(entries);
    SortByIndex(entries);
    static float alpha = 0.f;
    const float step = io.DeltaTime * 25.5f;
    alpha = ImClamp(alpha + ((!entries.empty() || g_specAlwaysShow) ? step : -step), 0.f, 1.f);
    if (alpha <= 0.f) return;
    constexpr float size = 15.f, pad = 7.f, spacing = 3.f;
    std::vector<std::string> texts;
    float widest = TextWs(f, size, "Spectators");
    for (const SpecEntry& e : entries) { texts.push_back(showTarget ? e.name + " > " + e.target : e.name); widest = ImMax(widest, TextWs(f, size, texts.back())); }
    const float w = widest + pad * 2.f, titleH = size + pad * 2.f;
    const float bodyH = entries.empty() ? 0.f : (float)entries.size() * (size + spacing) - spacing + pad * 2.f;
    float x, y; Place(w, titleH + bodyH, x, y);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), DnaGrey(0.07f), 4.5f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(x, y + titleH), ImVec2(x + w, y + titleH + bodyH), DnaGrey(0.07f, 0.8f), 4.5f, ImDrawFlags_RoundCornersBottom);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + titleH + bodyH), DnaGrey(0.1f, 0.7f), 5.5f);
    const float center = x + w * 0.5f;
    for (int i = 0; i < 60; ++i) {
        const float x0 = x + w / 60.f * (float)i, x1 = x0 + w / 60.f;
        const float a = 1.f - fabsf((x0 + x1) * 0.5f - center) / (w * 0.5f);
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 3.f), DnaAccent(0.75f * a));
    }
    dl->AddText(f, size, ImVec2(x + pad, y + pad), DnaGrey(0.8f), "Spectators");
    for (size_t i = 0; i < texts.size(); ++i)
        dl->AddText(f, size, ImVec2(x + pad, y + titleH + (float)i * (size + spacing) + pad), DnaGrey(0.8f, alpha), texts[i].c_str());
}

// style 29: cucumber
static ImU32 CucumberFade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a + 0.5f) << IM_COL32_A_SHIFT); }
static void DrawCucumber(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* f = Font(g_fontCucumberTahoma14);
    std::vector<SpecEntry> entries; Collect(entries, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget) EraseNotLocal(entries);
    SortByIndex(entries);
    static float alpha = 0.f;
    const bool on = !entries.empty() || g_specAlwaysShow;
    alpha = ImClamp(on ? alpha + io.DeltaTime * 5.f : alpha - io.DeltaTime * 5.f, 0.f, 1.f);
    if (!on && alpha <= 0.f) return;
    constexpr ImU32 bg = IM_COL32(18, 15, 18, 255);
    constexpr float w = 160.f, line = 14.f;
    const float textH = entries.empty() ? line : line * (float)entries.size();
    float x, y; Place(w, entries.empty() ? 22.f : textH + 27.f, x, y);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 22.f), bg, 15.f);
    const float tw = (float)(int)(f->CalcTextSizeA(14.f, FLT_MAX, 0.f, "Spectator List").x + 0.95f);
    dl->AddText(f, line, ImVec2(x + (w - tw) * 0.5f, y), CucumberFade(IM_COL32(255, 255, 255, 255), alpha), "Spectator List");
    dl->AddRectFilled(ImVec2(x + 15.f, y + 20.f), ImVec2(x + 145.f, y + 22.f), DnaAccent(1.f));
    if (!entries.empty()) {
        dl->AddRectFilled(ImVec2(x, y + 25.f), ImVec2(x + w, y + 27.f + textH), bg, 15.f);
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string text = showTarget ? entries[i].name + " => " + entries[i].target : entries[i].name;
            dl->AddText(f, line, ImVec2(x + 6.f, y + 26.f + line * (float)i), CucumberFade(U32(entries[i].watchingLocal ? g_specColorOne : g_specColorTwo), alpha), text.c_str());
        }
    }
}

// style 30: howeweware
static void DrawHoweweware(ImDrawList* dl)
{
    ImFont* f = Font(g_fontLumiProggy13);
    std::vector<SpecEntry> entries; Collect(entries, true, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget) EraseNotLocal(entries);
    if (entries.empty() && !g_showMenu && !g_specAlwaysShow) return;
    SortByIndex(entries);
    const int watched = IsLocalPlayerAlive() ? -1 : LocalObserverTarget();   // dead local: our target's rows count as ours
    const std::string title = "         Spectators         ";
    constexpr float size = 13.f, line = size + 4.f, pad = 8.f;
    const auto hw = [&](const std::string& t) { return (float)(int)(f->CalcTextSizeA(size, FLT_MAX, 0.f, t.c_str()).x + 0.95f); };
    std::vector<std::pair<std::string, float>> rows;
    const float titleW = hw(title);
    float widest = titleW;
    for (const SpecEntry& e : entries) {
        std::string text = showTarget ? e.name + "->" + e.target + " " : e.name;
        text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
        const float tw = hw(text);
        widest = ImMax(widest, tw);
        rows.emplace_back(std::move(text), tw);
    }
    const float w = ImMax(32.f, widest + pad * 2.f);
    const float h = ImMax(32.f, 2.f + line + 4.f + line * (float)rows.size() - 4.f + pad);
    float x, y; Place(w, h, x, y);
    const ImU32 bg = IM_COL32(15, 15, 15, 240), bgTop = IM_COL32(31, 31, 31, 240);
    const ImU32 border = IM_COL32(110, 110, 128, 128), borderClear = IM_COL32(110, 110, 128, 0);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg, 7.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), border, 7.f, 0, 1.f);
    dl->PushClipRect(ImVec2(x + 4.f, y + 1.f), ImVec2(x + w - 4.f, y + h - 1.f), true);
    const float header = y + size + 5.f;
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, header), bgTop, bgTop, bg, bg);
    dl->AddRectFilledMultiColor(ImVec2(x, header + 1.f), ImVec2(x + w * 0.5f, header), borderClear, border, border, borderClear);
    dl->AddRectFilledMultiColor(ImVec2(x + w * 0.5f, header + 1.f), ImVec2(x + w, header), border, borderClear, borderClear, border);
    dl->AddText(f, size, ImVec2(x + w * 0.5f - titleW * 0.5f, y + 2.f), IM_COL32(255, 255, 255, 255), title.c_str());
    for (size_t i = 0; i < rows.size(); ++i) {
        const bool ours = entries[i].watchingLocal || (watched >= 0 && entries[i].targetIndex == watched);
        dl->AddText(f, size, ImVec2(x + w * 0.5f - rows[i].second * 0.5f, y + 2.f + line + 4.f + line * (float)i), ours ? DnaAccent(1.f) : U32(g_specColorTwo), rows[i].first.c_str());
    }
    dl->PopClipRect();
}

// style 31: gamesense -- same frame / bar / font as its gamesense watermark (sf pro bold 15 drawn at 13)
static void DrawGamesense(ImDrawList* dl)
{
    ImFont* font = Font(g_fontGamesenseSfBold15);
    std::vector<SpecEntry> entries; Collect(entries, true);
    const bool showTarget = g_specShowTarget;
    if (!showTarget) EraseNotLocal(entries);
    if (entries.empty() && !g_specAlwaysShow) return;
    SortByIndex(entries);
    constexpr float px = 13.f;
    constexpr const char* title = "spectators"; constexpr const char* arrow = " -> ";
    const auto gw = [&](const std::string& s) { return font->CalcTextSizeA(px, FLT_MAX, 0.f, s.c_str()).x; };
    const bool bar = g_specGamesenseBar, avatars = g_specAvatar;
    const float textH = font->CalcTextSizeA(px, FLT_MAX, 0.f, "A").y;
    const float header = textH + 10.f + (bar ? 3.f : 0.f), rowH = textH + 4.f, avatarW = avatars ? textH + 4.f : 0.f;
    const float titleW = gw(title);
    float widest = titleW;
    for (const SpecEntry& e : entries) widest = ImMax(widest, avatarW + gw(showTarget ? e.name + arrow + e.target : e.name));
    const float width = ImMax(150.f, widest + 14.f);
    const float height = header + (entries.empty() ? 0.f : (float)entries.size() * rowH + 4.f);
    float x, y; Place(width + 6.f, height + 6.f, x, y);   // frame hangs 3 px outside the box
    x += 3.f; y += 3.f;
    const ImVec2 mn(x, y), mx(x + width, y + height);
    Watermark_GamesenseFrame(dl, mn, mx, bar);
    constexpr ImU32 white = IM_COL32(255, 255, 255, 230), grey = IM_COL32(210, 210, 210, 255);
    dl->AddText(font, px, ImVec2(x + (width - titleW) * 0.5f, y + 3.f + (bar ? 2.f : 0.f)), white, title);
    if (!entries.empty()) dl->AddRectFilled(ImVec2(x + 2.f, y + header - 1.f), ImVec2(mx.x - 2.f, y + header), IM_COL32(62, 62, 62, 255));
    const ImU32 local = U32(g_specColorOne), other = U32(g_specColorTwo);
    dl->PushClipRect(ImVec2(x + 2.f, y), ImVec2(mx.x - 2.f, mx.y), true);
    for (size_t i = 0; i < entries.size(); ++i) {
        const SpecEntry& e = entries[i];
        const float rowY = y + header + 2.f + (float)i * rowH;
        float tx = x + 7.f;
        if (avatars) {
            if (ImTextureID t = Avatar(e.sid)) dl->AddImage(t, ImVec2(tx, rowY + 2.f), ImVec2(tx + textH, rowY + 2.f + textH));
            else dl->AddRectFilled(ImVec2(tx, rowY + 2.f), ImVec2(tx + textH, rowY + 2.f + textH), IM_COL32(36, 36, 36, 255));
            tx += avatarW;
        }
        const auto put = [&](const std::string& s, ImU32 c) { dl->AddText(font, px, ImVec2(tx, rowY + 2.f), c, s.c_str()); tx += gw(s); };
        put(e.name, e.watchingLocal ? local : other);
        if (showTarget) { put(arrow, grey); put(e.target, white); }
    }
    dl->PopClipRect();
}

// style 32: kamidere (Desktop\Kami-Speclist): fixed 160 wide, header 24, rows 20, body eases at rate 16
static void DrawKamidere(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = Font(g_fontLumiRubik16);
    static float body = 8.f;
    static int lastFrame = -2;
    if (!IsInGame()) return;
    std::vector<SpecEntry> entries; Collect(entries, false, !g_specKamiHideTv);
    const bool showTarget = g_specShowTarget, showMode = g_specBhopMode;
    if (entries.empty() && !g_specAlwaysShow) return;
    SortByIndex(entries);
    const char* title = g_specKbCustomTitle && g_specKbTitle[0] ? g_specKbTitle : "Spectators";
    constexpr float w = 160.f, header = 24.f, rowH = 20.f, avatar = 16.f, inset = 8.f, rounding = 3.f;
    const float target = entries.empty() ? 8.f : (float)entries.size() * rowH + 4.f;
    const int frame = ImGui::GetFrameCount();
    if (lastFrame + 1 != frame) body = target;
    else {
        body += (target - body) * (1.f - expf(-16.f * ImClamp(io.DeltaTime, 0.f, 0.1f)));
        if (fabsf(body - target) < 0.05f) body = target;
    }
    lastFrame = frame;
    float x, y; Place(w, header + body, x, y);
    const bool blur = g_specKamiBlur;
    const ImVec2 mn(x, y), mx(x + w, y + header + body), bodyMin(x, y + header);
    const float opacity = blur ? 0.8f : ImClamp(g_specKamiOpacity, 0.f, 100.f) * 0.01f;
    // one rounded silhouette clipped into header / body, no seam
    if (blur) BlurRect(dl, bodyMin, mx, 1.f, rounding);
    dl->PushClipRect(bodyMin, mx, true);
    dl->AddRectFilled(mn, mx, IM_COL32(20, 20, 20, (int)(255.f * opacity)), rounding);
    dl->PopClipRect();
    const float size = font->FontSize;
    dl->PushClipRect(mn, ImVec2(mx.x, bodyMin.y), true);
    dl->AddRectFilled(mn, mx, IM_COL32(13, 13, 13, 255), rounding);
    const float titleY = roundf(y + (header - size) * 0.5f);
    dl->AddText(font, size, ImVec2(x + inset + 1.f, titleY + 1.f), IM_COL32(0, 0, 0, 220), title);
    dl->AddText(font, size, ImVec2(x + inset, titleY), IM_COL32(219, 219, 219, 255), title);
    dl->PopClipRect();
    const bool avatars = g_specAvatar;
    const float corner = g_specKamiCircle ? avatar * 0.5f : 5.f;
    const ImU32 local = U32(g_specColorOne), other = U32(g_specColorTwo);
    const float textX = avatars ? x + inset + avatar + 8.f : x + inset;
    dl->PushClipRect(bodyMin, mx, true);
    for (size_t i = 0; i < entries.size(); ++i) {
        const float rowY = bodyMin.y + 2.f + (float)i * rowH;
        if (rowY >= mx.y) break;
        const SpecEntry& e = entries[i];
        if (avatars) {
            const ImVec2 amn(x + inset, rowY + (rowH - avatar) * 0.5f), amx(amn.x + avatar, amn.y + avatar);
            if (ImTextureID t = Avatar(e.sid)) dl->AddImageRounded(t, amn, amx, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, corner);
            else dl->AddRectFilled(amn, amx, IM_COL32(48, 52, 60, 255), corner);
        }
        const ImU32 color = e.watchingLocal ? local : other;
        const float modeW = showMode ? ceilf(TextWs(font, size, e.mode)) : 0.f;
        const float nameMax = mx.x - inset - textX - (showMode ? modeW + 6.f : 0.f);
        const std::string name = Ellipsize(font, size, showTarget ? e.name + " -> " + e.target : e.name, nameMax);
        const float textY = roundf(rowY + (rowH - size) * 0.5f) - 2.f;
        const int shadow = (int)((color >> IM_COL32_A_SHIFT) & 0xff) * 220 / 255;
        dl->AddText(font, size, ImVec2(textX + 1.f, textY + 1.f), IM_COL32(0, 0, 0, shadow), name.c_str());
        dl->AddText(font, size, ImVec2(textX, textY), color, name.c_str());
        if (showMode) {
            const float modeX = mx.x - inset - modeW;
            dl->AddText(font, size, ImVec2(modeX + 1.f, textY + 1.f), IM_COL32(0, 0, 0, 220), e.mode.c_str());
            dl->AddText(font, size, ImVec2(modeX, textY), IM_COL32(150, 150, 150, 255), e.mode.c_str());
        }
    }
    dl->PopClipRect();
}

// ==== drag + dispatch ==========================================================================
// Styles that hardcode their x/y instead of calling Place(); SpecDrag shifts the vertices they added
// (none of them push a clip rect or a BlurRect).
static bool StyleFixedPos(int style)
{
    switch (style) {
    case 1: case 9: case 12: case 13: case 16: case 19: case 24: return true;
    case 0: return g_specListType == 1;   // draw_spectating_local
    default: return false;
    }
}

// Panel rect = bounds of the vertices this frame's Draw* added (from v0). SpecPlace runs inside
// the panel's dpi block (layout px): shifts the fixed-position styles by the drag offset.
static bool SpecPlace(ImDrawList* dl, ImGuiIO& io, int v0, ImVec2& mn, ImVec2& mx)
{
    ImDrawVert* v = dl->VtxBuffer.Data;
    const int v1 = dl->VtxBuffer.Size;
    if (v1 <= v0) return false;

    mn = ImVec2(FLT_MAX, FLT_MAX); mx = ImVec2(-FLT_MAX, -FLT_MAX);
    for (int i = v0; i < v1; ++i) { mn = ImMin(mn, v[i].pos); mx = ImMax(mx, v[i].pos); }

    if (StyleFixedPos(g_specStyle)) {
        const float dx = floorf(fmaxf(-mn.x, fminf(g_specOffX, io.DisplaySize.x - mx.x)));
        const float dy = floorf(fmaxf(-mn.y, fminf(g_specOffY, io.DisplaySize.y - mx.y)));
        for (int i = v0; i < v1; ++i) { v[i].pos.x += dx; v[i].pos.y += dy; }
        mn.x += dx; mn.y += dy; mx.x += dx; mx.y += dy;
    }
    return true;
}

// Drag to reposition, menu open only: LMB drag moves, RMB resets to the bare g_specPos anchor.
// Runs after the dpi block ended: the vertices are where they show (screen px), so the hit test
// takes the screen mouse, and the drag maps the shown move back to layout px (botox
// drag_placed_panel: dpi_panel_layout_pos). lmn / lmx = the layout bounds. Delta lands next frame.
static void SpecDrag(ImDrawList* dl, ImGuiIO& io, int v0, bool drew, ImVec2 lmn, ImVec2 lmx)
{
    static bool s_drag = false;
    if (!drew) { s_drag = false; return; }
    ImDrawVert* v = dl->VtxBuffer.Data;
    ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) { mn = ImMin(mn, v[i].pos); mx = ImMax(mx, v[i].pos); }

    if (!g_showMenu) { s_drag = false; return; }

    const ImVec2 m = Dpi_ScreenMouse();
    const bool hovered = !GImGui->HoveredWindow && m.x >= mn.x && m.x <= mx.x && m.y >= mn.y && m.y <= mx.y;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_drag = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { g_specOffX = g_specOffY = 0.f; s_drag = false; }

    if (s_drag) {
        // io.MouseDelta is logical px (the mouse imgui sees is screen / dpi scale)
        const float k = Dpi_Scale();
        const ImVec2 d(fmaxf(-mn.x, fminf(io.MouseDelta.x * k, io.DisplaySize.x - mx.x)),
                       fmaxf(-mn.y, fminf(io.MouseDelta.y * k, io.DisplaySize.y - mx.y)));
        const ImVec2 at = Dpi_PanelLayoutPos(ImVec2(mn.x + d.x, mn.y + d.y), ImVec2(lmx.x - lmn.x, lmx.y - lmn.y));
        g_specOffX += at.x - lmn.x;
        g_specOffY += at.y - lmn.y;
    }
    if (hovered || s_drag) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        dl->AddRect(ImVec2(mn.x - 2.f, mn.y - 2.f), ImVec2(mx.x + 2.f, mx.y + 2.f), ImGui::GetColorU32(ImGuiCol_Accent));
    }
}

static void DrawStyle(ImDrawList* dl, ImGuiIO& io)
{
    switch (g_specStyle) {
    case 0:  if (g_specListType == 0) DrawDefault(dl, io); else DrawSpectatingLocal(dl); break;
    case 1:  DrawDelusional(dl, io);    break;
    case 2:  DrawInterwebz(dl);         break;
    case 3:  DrawWinXP(dl);             break;
    case 4:  DrawClarity(dl);           break;
    case 5:  DrawChillware(dl, io);     break;
    case 6:  DrawLobotomy(dl);          break;
    case 7:  DrawKamibebra(dl, io);     break;
    case 8:  DrawEyes(dl);              break;
    case 9:  DrawCumhacck(dl, io);      break;
    case 10: DrawClarityV2(dl, io);     break;
    case 11: DrawAirplane(dl);          break;
    case 12: DrawBhopcheat(dl);         break;
    case 13: DrawMillionware(dl, io);   break;
    case 14: DrawAimware(dl);           break;
    case 15: DrawHavoc(dl, io);         break;
    case 16: DrawOnetap(dl);            break;
    case 17: DrawAirflow(dl);           break;
    case 18: DrawEvolve(dl);            break;
    case 19: DrawLegendware(dl, io);    break;
    case 20: DrawInterium(dl, io);      break;
    case 22: DrawSkebob(dl, io);        break;
    case 23: DrawCumidere(dl);          break;
    case 24: DrawIllusory(dl, io);      break;
    case 25:
        if (g_specLumiType == 1) DrawLumiNew(dl, io);
        else if (g_specLumiType == 2) DrawLumiInterwebz(dl);
        else DrawLumiDefault(dl, io);
        break;
    case 26: DrawBillware(dl, io);      break;
    case 27: DrawInkabanium(dl);        break;
    case 28: DrawDna(dl, io);           break;
    case 29: DrawCucumber(dl, io);      break;
    case 30: DrawHoweweware(dl);        break;
    case 31: DrawGamesense(dl);         break;
    case 32: DrawKamidere(dl, io);      break;
    default: break;
    }
}

void RenderSpectatorList()
{
    if (!g_specEnable) return;

    // botox always_show_scope_t: menu open = every style acts as always show; restored before return
    // so the cfg never saves the force.
    bool* const flags[3] = { &g_specAlwaysShow, &g_specClarityAlwaysShow, &g_specKbAlwaysShow };
    bool saved[3] = { *flags[0], *flags[1], *flags[2] };
    const bool force = g_showMenu;
    if (force) for (bool* f : flags) *f = true;

    if (g_specStyle == 21) DrawChillwareV2();   // real ImGui window, draggable on its own
    else {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const int v0 = dl->VtxBuffer.Size;
        ImVec2 lmn, lmx;
        bool drew;
        {
            DpiPanelScope block(dl);   // botox: each spectator style is one stretch block
            DrawStyle(dl, io);
            drew = SpecPlace(dl, io, v0, lmn, lmx);
        }
        SpecDrag(dl, io, v0, drew, lmn, lmx);
    }

    if (force) for (int i = 0; i < 3; ++i) *flags[i] = saved[i];
}

void SpectatorList_ReleaseTextures()
{
    for (auto& t : s_eyeTex) {
        if (t.tex) t.tex->Release();
        t = {};
    }
}
