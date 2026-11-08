#include "game_extras.h"
#include "game_pure.h"
#include "names.h"                      // custom names: same answer as C_PlayerResource::GetPlayerName
#include "../aim/aim.h"                 // Aim_ChatPrint (CBaseHudChat::ChatPrintf, game thread)
#include "../movement/detections.h"     // g_detectionLabel, the chat brand botox's brand() reads
#include "../screen/screenfx.h"         // ScreenFx_QueueForced -- mat_queue_mode has two owners
#include "../hud/mediaplayer.h"         // MediaPlayer_Track (clantag now playing)
#include "clantag.h"
#include <winhttp.h>
#include <commdlg.h>
#include <atomic>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../globals/pinned_thread.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comdlg32.lib")

const char* const kChatLanguageNames =
    "english\0russian\0german\0french\0spanish\0portuguese\0polish\0turkish\0ukrainian\0"
    "chinese\0japanese\0korean\0italian\0dutch\0swedish\0finnish\0czech\0hungarian\0"
    "greek\0arabic\0hindi\0indonesian\0vietnamese\0romanian\0";

namespace
{
constexpr const char* kLanguageCodes[] = { "en", "ru", "de", "fr", "es", "pt-BR", "pl", "tr", "uk", "zh-CN", "ja", "ko",
                                           "it", "nl", "sv", "fi", "cs", "hu", "el", "ar", "hi", "id", "vi", "ro" };
constexpr int kLanguageCount = (int)(sizeof(kLanguageCodes) / sizeof(kLanguageCodes[0]));

std::atomic<bool> s_stop{ false };

// ==== workers + http ==============================================================
// CreateThread, not a detached std::thread: the CRT is linked static, so a detached thread's
// exit path runs inside this dll -- eject has to WAIT on the handle, not on a flag.
std::mutex s_threadLock;
std::vector<HANDLE> s_threads;

bool StartWorker(LPTHREAD_START_ROUTINE fn, void* arg)
{
    if (s_stop) return false;
    HANDLE h = CreatePinnedThread([fn, arg] { fn(arg); });
    if (!h) { DebugLog("game: CreateThread failed (err %lu)", GetLastError()); return false; }
    std::lock_guard<std::mutex> lock(s_threadLock);
    // reap the finished ones: a chat worker starts per burst of messages
    for (size_t i = 0; i < s_threads.size();)
        if (WaitForSingleObject(s_threads[i], 0) == WAIT_OBJECT_0) { CloseHandle(s_threads[i]); s_threads.erase(s_threads.begin() + i); }
        else ++i;
    s_threads.push_back(h);
    return true;
}

// open sessions, closed by eject so a request in flight returns at once
std::mutex s_sessionLock;
std::vector<HINTERNET> s_sessions;

bool HttpRequest(const std::string& url, const char* verb, const std::string* body, const std::wstring& headers,
                 std::string& out, size_t cap)
{
    if (s_stop || url.compare(0, 8, "https://")) return false;
    const size_t slash = url.find('/', 8);
    const std::string host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
    const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    const std::wstring whost(host.begin(), host.end()), wpath(path.begin(), path.end());
    const std::wstring wverb(verb, verb + strlen(verb));

    HINTERNET session = WinHttpOpen(L"botox", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    {
        std::lock_guard<std::mutex> lock(s_sessionLock);
        if (s_stop) { WinHttpCloseHandle(session); return false; }
        s_sessions.push_back(session);
    }
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 15000);

    HINTERNET conn = WinHttpConnect(session, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req  = conn ? WinHttpOpenRequest(conn, wverb.c_str(), wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (req &&
        WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), (DWORD)-1,
                           body ? (LPVOID)body->data() : WINHTTP_NO_REQUEST_DATA, body ? (DWORD)body->size() : 0,
                           body ? (DWORD)body->size() : 0, 0) &&
        WinHttpReceiveResponse(req, nullptr))
    {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX);
        char buf[4096];
        DWORD read = 0;
        while (out.size() < cap && WinHttpReadData(req, buf, sizeof(buf), &read) && read) out.append(buf, read);
        ok = status == 200;
    }
    if (req)  WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    {
        std::lock_guard<std::mutex> lock(s_sessionLock);
        for (size_t i = 0; i < s_sessions.size(); ++i)
            if (s_sessions[i] == session) { s_sessions.erase(s_sessions.begin() + i); WinHttpCloseHandle(session); break; }
    }
    return ok;
}

bool HttpGet(const std::string& url, std::string& out, size_t cap = 256 * 1024)
{
    return HttpRequest(url, "GET", nullptr, L"", out, cap);
}

// IVEngineClient::ClientCmd_Unrestricted, slot 106 (menu.cpp MenuClientCmd, screenfx.cpp FxEngineExec)
void EngineExec(const char* cmd)
{
    if (!g_engine) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engine);
        if (reinterpret_cast<uintptr_t>(vt) > 0x10000 && reinterpret_cast<uintptr_t>(vt[106]) > 0x10000)
            ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== bot names: IVEngineClient::GetPlayerInfo (slot 8, defs.h IVEngineClient) ==========
// TF2 x64 player_info_t (aim/defs.h player_info_s, REPLAY_ENABLED): name[32] @0, userID @0x20,
// guid[33] @0x24, friendsID @0x48, friendsName[32] @0x4C, fakeplayer @0x6C, ishltv @0x6D.
// botox writes the LISTEN SERVER's userinfo string table (CS:GO demos/gotv record it); here
// the client's own GetPlayerInfo answer is patched -- scoreboard, killfeed, target id, chat
// and this dll's esp all read names through it, and it works in demo playback too.
constexpr int kPiName = 0x00, kPiNameSize = 32, kPiUserId = 0x20, kPiFriendsId = 0x48, kPiFake = 0x6C, kPiHltv = 0x6D;
constexpr int kPiSize = 0x84;

using GetPlayerInfo_t = bool(__fastcall*)(void*, int, void*);
GetPlayerInfo_t o_GetPlayerInfo = nullptr;

constexpr uint64_t kSteamIdBase = 76561197960265728ull;
constexpr size_t   kPoolWant    = 32;

struct PoolEntry { char name[kPiNameSize]; uint32_t account; int uses; };
std::mutex             s_poolLock;      // lock order: s_botLock, then s_poolLock
std::vector<PoolEntry> s_pool;
bool                   s_fetchDone = false;
std::atomic<bool>      s_fetching{ false };

struct BotProfile { char name[kPiNameSize]; uint32_t account; uint32_t sig; bool assigned; };
std::mutex                          s_botLock;
std::unordered_map<int, BotProfile> s_bots;   // key = userID, unique per connection
std::atomic<uint32_t>               s_botSig{ 1 };
std::atomic<int>                    s_generation{ 0 };
const uint32_t                      s_seed = std::random_device{}();

// per bot + generation: same bot keeps its name/ping for the session, randomize rerolls
std::mt19937 BotRng(int userId, uint32_t salt)
{
    return std::mt19937(s_seed ^ (uint32_t)userId * 2654435761u ^ (uint32_t)s_generation.load() * 40503u ^ salt);
}

bool MakeProfile(int userId, BotProfile& b)
{
    b.name[0] = '\0';
    b.account = 0;
    switch (g_botNamesMode)
    {
    case 1:
        strncpy_s(b.name, g_botNamesSame, _TRUNCATE);
        return true;
    case 2: {
        static const char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::mt19937 rng = BotRng(userId, 0x51u);
        std::uniform_int_distribution<int> pick(0, (int)sizeof(kChars) - 2);
        const int len = (std::min)((std::max)(g_botNamesLength, 1), kPiNameSize - 1);
        for (int i = 0; i < len; ++i) b.name[i] = kChars[pick(rng)];
        b.name[len] = '\0';
        return true;
    }
    case 3: {
        std::lock_guard<std::mutex> lock(s_poolLock);
        PoolEntry* best = nullptr;
        for (PoolEntry& e : s_pool) if (!best || e.uses < best->uses) best = &e;
        // reuse a name only once nothing fresh is still coming (botox make_profile)
        if (!best || (best->uses && !s_fetchDone)) return false;
        ++best->uses;
        memcpy(b.name, best->name, sizeof(b.name));
        b.account = best->account;
        return true;
    }
    default:
        return true;
    }
}

void PatchPlayerInfo(uint8_t* p)
{
    if (!p[kPiFake] || p[kPiHltv]) return;   // engine bots only, never sourcetv
    int userId = 0;
    memcpy(&userId, p + kPiUserId, sizeof(userId));
    {
        std::lock_guard<std::mutex> lock(s_botLock);
        if (s_bots.size() > 256) s_bots.clear();
        BotProfile& b = s_bots[userId];
        const uint32_t sig = s_botSig.load();
        if (b.sig != sig) { b.sig = sig; b.assigned = false; }
        if (!b.assigned) b.assigned = MakeProfile(userId, b);
        if (b.assigned && b.name[0])
        {
            memset(p + kPiName, 0, kPiNameSize);
            memcpy(p + kPiName, b.name, strnlen(b.name, kPiNameSize - 1));
        }
        if (b.assigned && b.account) memcpy(p + kPiFriendsId, &b.account, sizeof(b.account));
    }
    if (g_botNamesNoPrefix) p[kPiFake] = 0;   // TF2: no BOT ping icon, no "[bot]" esp tag
}

bool __fastcall h_GetPlayerInfo(void* self, int ent, void* pinfo)
{
    MiscHookGuard _g;
    const bool ok = o_GetPlayerInfo(self, ent, pinfo);
    if (ok && pinfo && g_botNames && !g_eject) PatchPlayerInfo(static_cast<uint8_t*>(pinfo));
    if (ok && pinfo && !g_eject) Names_PatchInfo(ent, pinfo);
    return ok;
}

// The engine's own answer, never ours (chat names, bot detection for the ping writes).
__declspec(noinline) bool RawPlayerInfo(int ent, uint8_t* out)
{
    if (!g_engine) return false;
    bool ok = false;
    __try {
        ok = o_GetPlayerInfo ? o_GetPlayerInfo(g_engine, ent, out) : VCall<bool>(g_engine, 8, ent, (void*)out);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

DWORD WINAPI FetchWorker(LPVOID)
{
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<uint32_t> account(20'000'000u, 420'000'000u);
    int failures = 0;
    for (size_t attempt = 0; attempt < kPoolWant * 10 && failures < 6 && !s_stop; ++attempt)
    {
        {
            std::lock_guard<std::mutex> lock(s_poolLock);
            if (s_pool.size() >= kPoolWant) break;
        }
        const uint32_t acct = account(rng);
        std::string body;
        if (!HttpGet("https://steamcommunity.com/profiles/" + std::to_string(kSteamIdBase + acct) + "/?xml=1", body, 64 * 1024))
        {
            ++failures;
            continue;
        }
        failures = 0;
        const std::string name = n_game::SteamXmlName(body, kPiNameSize - 1);
        if (name.empty()) continue;
        PoolEntry e{};
        memcpy(e.name, name.data(), name.size());
        e.account = acct;
        std::lock_guard<std::mutex> lock(s_poolLock);
        s_pool.push_back(e);
    }
    size_t got = 0;
    {
        std::lock_guard<std::mutex> lock(s_poolLock);
        s_fetchDone = true;
        got = s_pool.size();
    }
    DebugLog("bot names: steam pool %zu names (failures in a row %d)", got, failures);
    s_fetching = false;
    return 0;
}

void StartFetch()
{
    {
        std::lock_guard<std::mutex> lock(s_poolLock);
        if (s_fetchDone || s_pool.size() >= kPoolWant) return;
    }
    if (s_fetching.exchange(true)) return;
    if (!StartWorker(FetchWorker, nullptr)) s_fetching = false;
}

uint32_t SettingsSig()
{
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)g_botNamesMode);
    mix((uint32_t)g_botNamesLength);
    mix((uint32_t)s_generation.load());
    for (const char* c = g_botNamesSame; *c; ++c) mix((uint8_t)*c);
    return h | 1u;   // 0 = a fresh profile's "never assigned"
}

// ---- random ping: C_PlayerResource::m_iPing[ent], client copy only ----
// The server sends a bot's ping once (it never changes), so a client write sticks; it is
// re-asserted every render start anyway and put back the moment it stops being wanted.
// CCSPlayerResource found by class name off the entity list, the skins.cpp FindPlayerResource
// way (IClientEntityList 0 GetClientNetworkable / 6 GetHighestEntityIndex, networkable 2
// GetClientClass, all confirmed there).
constexpr int kMaxSlots = 102;
int  s_offPing = 0;           // 0 unresolved, -1 not found
int  s_pingSaved[kMaxSlots];
bool s_pingHeld[kMaxSlots];
int  s_pingWant[kMaxSlots];   // 0 = leave alone
void* s_pingRes = nullptr;

__declspec(noinline) void* FindPlayerRes()
{
    static void* s_cached = nullptr;
    static int   s_idx    = 0;
    __try {
        if (s_cached && s_idx > 0 && GetClientEntity(s_idx) == s_cached) return s_cached;
        s_cached = nullptr; s_idx = 0;
        if (!g_entityList) return nullptr;
        const int highest = VCall<int>(g_entityList, 6);
        const int cap = (highest > 0 && highest < 2048) ? highest : 2048;
        for (int i = 1; i <= cap; ++i)
        {
            void* net = VCall<void*>(g_entityList, 0, i);
            if (!net) continue;
            ClientClass* cc = VCall<ClientClass*>(net, 2);
            if (!cc || !cc->m_pNetworkName || strcmp(cc->m_pNetworkName, "CCSPlayerResource") != 0) continue;
            s_cached = GetClientEntity(i);
            s_idx = i;
            return s_cached;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_cached = nullptr; s_idx = 0; }
    return nullptr;
}

__declspec(noinline) bool PingRead(void* res, int idx, int& v)
{
    __try { v = *reinterpret_cast<int*>(static_cast<uint8_t*>(res) + s_offPing + idx * 4); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) void PingWrite(void* res, int idx, int v)
{
    __try { *reinterpret_cast<int*>(static_cast<uint8_t*>(res) + s_offPing + idx * 4) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void PingReleaseAll()
{
    for (int i = 0; i < kMaxSlots; ++i)
    {
        if (s_pingHeld[i] && s_pingRes) PingWrite(s_pingRes, i, s_pingSaved[i]);
        s_pingHeld[i] = false;
        s_pingWant[i] = 0;
    }
    s_pingRes = nullptr;
}

// 100ms: which slots are bots and what ping each one shows
void PingScan()
{
    if (!g_botNames || !g_botNamesPing || !IsInGame()) { PingReleaseAll(); return; }
    if (s_offPing == 0)
    {
        // DT_CSPlayerResource (c_cs_playerresource.cpp) -- m_iPing itself lives in the base
        // DT_PlayerResource table (c_playerresource.cpp:22) that CS:S's table derives from;
        // FindPropOffset walks nested/baseclass tables so the derived name still finds it.
        RecvTable* t = FindNetworkTable("DT_CSPlayerResource");
        s_offPing = t ? FindPropOffset(t, "m_iPing") : -1;
        if (s_offPing <= 0) s_offPing = -1;
        DebugLog("bot names: m_iPing offset %d", s_offPing);
    }
    if (s_offPing <= 0) return;
    void* res = FindPlayerRes();
    if (res != s_pingRes) { PingReleaseAll(); s_pingRes = res; }
    if (!res) return;

    const int maxClients = (std::min)(VCall<int>(g_engine, 21), kMaxSlots - 1);   // GetMaxClients
    uint8_t pi[kPiSize + 64];
    for (int i = 1; i < kMaxSlots; ++i)
    {
        s_pingWant[i] = 0;
        if (i > maxClients || !RawPlayerInfo(i, pi) || !pi[kPiFake] || pi[kPiHltv]) continue;
        int userId = 0;
        memcpy(&userId, pi + kPiUserId, sizeof(userId));
        std::mt19937 rng = BotRng(userId, 0x9u);
        s_pingWant[i] = std::uniform_int_distribution<int>(17, 155)(rng);   // botox roll_stats
    }
}

void PingApply()
{
    if (!s_pingRes || s_offPing <= 0) return;
    for (int i = 1; i < kMaxSlots; ++i)
    {
        if (!s_pingWant[i])
        {
            if (s_pingHeld[i]) { PingWrite(s_pingRes, i, s_pingSaved[i]); s_pingHeld[i] = false; }
            continue;
        }
        int cur = 0;
        if (!PingRead(s_pingRes, i, cur)) continue;
        if (!s_pingHeld[i]) { s_pingSaved[i] = cur; s_pingHeld[i] = true; }
        else if (cur != s_pingWant[i]) s_pingSaved[i] = cur;   // the server sent a new real value
        if (cur != s_pingWant[i]) PingWrite(s_pingRes, i, s_pingWant[i]);
    }
}

// ==== chat translator ===============================================================
struct ChatJob { std::string name, text, lang; };
std::mutex              s_chatLock;
std::deque<ChatJob>     s_jobs;
std::deque<std::string> s_lines;    // finished, printed on the game thread
bool                    s_chatWorker = false;   // under s_chatLock

DWORD WINAPI ChatWorker(LPVOID)
{
    for (;;)
    {
        ChatJob job;
        {
            std::lock_guard<std::mutex> lock(s_chatLock);
            if (s_jobs.empty() || s_stop) { s_chatWorker = false; return 0; }
            job = std::move(s_jobs.front());
            s_jobs.pop_front();
        }
        std::string body, out, source;
        if (!HttpGet("https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=" + job.lang +
                     "&dt=t&q=" + n_game::UrlEncode(job.text), body))
        {
            static int s_fails = 0;
            if ((s_fails++ % 20) == 0) DebugLog("chat translator: request failed (%d so far)", s_fails);
            continue;
        }
        if (!n_game::ParseGtx(body, job.text, job.lang, out, source)) continue;

        // botox worker html, as TF2 chat colours: name grey, [src->dst] dark grey, text green
        const std::string line = "\x07" "BDC3C7" + n_game::NoPercent(job.name) + "\x07" "7D7D7D [" +
                                 n_game::NoPercent(n_game::Lower(n_game::StripControls(source))) + "->" +
                                 n_game::Lower(job.lang) + "]: \x07" "00FF00" +
                                 n_game::NoPercent(n_game::StripControls(out));
        std::lock_guard<std::mutex> lock(s_chatLock);
        if (s_lines.size() < 32) s_lines.push_back(line);
    }
}

__declspec(noinline) int CopyUserMessage(void* bf, uint8_t* out, int cap)
{
    __try {
        const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(bf);   // bf_read::m_pData
        int bytes = *reinterpret_cast<const int*>(static_cast<uint8_t*>(bf) + 8);   // m_nDataBytes
        if (!data || bytes <= 0) return 0;
        if (bytes > cap) bytes = cap;
        memcpy(out, data, bytes);
        return bytes;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void ChatPrintPending()
{
    std::deque<std::string> ready;
    {
        std::lock_guard<std::mutex> lock(s_chatLock);
        ready.swap(s_lines);
    }
    if (ready.empty()) return;
    // botox brand(): accent label + " | "
    char brand[160];
    snprintf(brand, sizeof(brand), "\x07%02X%02X%02X%s\x07" "7D7D7D | ",
             (int)(g_accentColor[0] * 255.f + 0.5f), (int)(g_accentColor[1] * 255.f + 0.5f),
             (int)(g_accentColor[2] * 255.f + 0.5f),
             n_game::NoPercent(g_detectionLabel[0] ? g_detectionLabel : "botox").c_str());
    for (const std::string& l : ready) Aim_ChatPrint((brand + l).c_str());
}

// ==== performance (botox n_misc::impl_t::performance) ===============================
struct Knob { const char* name; bool* var; int value; bool console; ConVar* cv; bool captured; int stored; };
Knob s_knobs[] = {
    { "cl_threaded_bone_setup", &g_perfThreadedBones, 1, false, nullptr, false, 0 },
    { "cl_forcepreload",        &g_perfForcePreload,  1, false, nullptr, false, 0 },
    // through the console like screenfx.cpp: its effect rides the command, not the value
    { "mat_queue_mode",         &g_perfMulticore,     2, true,  nullptr, false, 0 },
    // engine sleeps 50 ms a frame while unfocused (recording tabbed out)
    { "engine_no_focus_sleep",  &g_perfNoFocusSleep,  0, false, nullptr, false, 0 },
};
DWORD s_storedClass = 0;
bool  s_fastCores   = false;

void KnobSet(Knob& k, int v)
{
    if (k.console)
    {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "%s %d", k.name, v);
        EngineExec(cmd);
    }
    else if (!k.cv->SetValueInt(k.name, v))
        k.cv->ForceInt(v);   // SetValue refused (layout check): the raw write at least lands the number
}

void Performance(bool restore)
{
    const bool enabled = !restore && g_perfEnable;
    for (Knob& k : s_knobs)
    {
        // dev-only ones need the walk; a cvar this build lacks is looked for 20 times, then never again
        static int s_misses[sizeof(s_knobs) / sizeof(s_knobs[0])] = {};
        int& miss = s_misses[&k - s_knobs];
        if (!k.cv && (miss > 20 || !(k.cv = g_cvar ? g_cvar->FindVarWalk(k.name) : nullptr)))
        {
            if (g_cvar && ++miss == 21) DebugLog("performance: %s not found -- row does nothing", k.name);
            continue;
        }
        // screenfx forces mat_queue_mode 0 for its depth passes: hands off until it lets go
        if (k.console && !restore && ScreenFx_QueueForced()) continue;
        // TF2: threaded bone setup runs SetupBones on worker threads, and the fps boost /
        // lag resolver SetupBones hook (visuals.cpp) keeps per-player statics -- never both
        const bool want = enabled && *k.var && !(k.var == &g_perfThreadedBones && (g_fpsBoost || g_lagResolver));
        const int now = (int)k.cv->GetFloat();
        if (!want)
        {
            if (k.captured) { KnobSet(k, k.stored); k.captured = false; DebugLog("performance: %s back to %d", k.name, k.stored); }
            continue;
        }
        if (!k.captured) { k.stored = now; k.captured = true; DebugLog("performance: %s %d -> %d", k.name, now, k.value); }
        if (now != k.value) KnobSet(k, k.value);
    }

    const HANDLE process = GetCurrentProcess();
    // ABOVE_NORMAL, not HIGH: Source's spin-waiting worker threads (mat_queue_mode 2, threaded
    // bones) at HIGH class starve dwm/input/audio = the whole desktop lags (log: 100-400 ms
    // hitches with every botox span < 6 ms)
    if (enabled && g_perfHighPriority)
    {
        if (!s_storedClass && (s_storedClass = GetPriorityClass(process)) != 0)
            SetPriorityClass(process, ABOVE_NORMAL_PRIORITY_CLASS);
    }
    else if (s_storedClass)
    {
        SetPriorityClass(process, s_storedClass);
        s_storedClass = 0;
    }

    // EcoQoS off: Windows keeps a throttled process on the efficiency cores of a hybrid cpu
    const bool fast = enabled && g_perfFastCores;
    if (fast != s_fastCores)
    {
        PROCESS_POWER_THROTTLING_STATE st{};
        st.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        st.ControlMask = fast ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION : 0;
        if (!SetProcessInformation(process, ProcessPowerThrottling, &st, sizeof(st)) && fast)
        {
            st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            SetProcessInformation(process, ProcessPowerThrottling, &st, sizeof(st));
        }
        s_fastCores = fast;
        DebugLog("performance: fast cores %s", fast ? "on" : "off");
    }
}

// ==== image pick (botox image_path_input) =============================================
std::mutex  s_pickLock;
ImGuiID     s_pickId = 0;
std::string s_pickResult, s_pickStatus;
bool        s_pickDone = false;
std::atomic<bool>  s_pickBusy{ false };
std::atomic<DWORD> s_pickThread{ 0 };

void PickFinish(const char* status, const std::string& result)
{
    std::lock_guard<std::mutex> lock(s_pickLock);
    s_pickStatus = status;
    s_pickResult = result;
    s_pickDone   = true;
}

// discord fetches the image itself, so a local file needs a public url. freeimage.host public
// key (their api page). not catbox: discord proxy gets 502 from files.catbox.moe = "?" image
std::string ImageUpload(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.empty() || data.size() > 20u * 1024u * 1024u) return {};

    // only the extension leaves the pc, not the file name
    std::string ext = path.substr(path.find_last_of("\\/") + 1);
    const size_t dot = ext.find_last_of('.');
    ext = dot == std::string::npos ? "" : ext.substr(dot);
    ext.erase(std::remove_if(ext.begin(), ext.end(), [](char c) { return !isalnum((unsigned char)c) && c != '.'; }), ext.end());

    char boundary[48];
    snprintf(boundary, sizeof(boundary), "----botox%08lx%08lx", GetTickCount(), GetCurrentThreadId());
    std::string body = std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"key\"\r\n\r\n6d207e02198a847aa98d0a2a901485a5\r\n--" +
                       boundary + "\r\nContent-Disposition: form-data; name=\"format\"\r\n\r\ntxt\r\n--" + boundary +
                       "\r\nContent-Disposition: form-data; name=\"source\"; filename=\"image" + ext +
                       "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    body += data;
    body += std::string("\r\n--") + boundary + "--\r\n";
    const std::string hdr = std::string("Content-Type: multipart/form-data; boundary=") + boundary;

    std::string out;
    HttpRequest("https://freeimage.host/api/1/upload", "POST", &body, std::wstring(hdr.begin(), hdr.end()), out, 4096);
    while (!out.empty() && isspace((unsigned char)out.back())) out.pop_back();
    return out.compare(0, 8, "https://") == 0 && out.size() < 256 ? out : std::string();
}

DWORD WINAPI PickWorker(LPVOID upload)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    char path[MAX_PATH] = {};
    OPENFILENAMEA dlg{};
    dlg.lStructSize = sizeof(dlg);
    dlg.lpstrFilter = "images\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0all files\0*.*\0";
    dlg.lpstrFile   = path;
    dlg.nMaxFile    = MAX_PATH;
    dlg.lpstrTitle  = upload ? "pick image (uploads to freeimage.host)" : "pick image";
    dlg.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    s_pickThread = GetCurrentThreadId();
    const bool picked = GetOpenFileNameA(&dlg) && !s_stop;
    s_pickThread = 0;

    if (!picked) PickFinish("", "");
    else if (!upload) PickFinish("", path);
    else
    {
        { std::lock_guard<std::mutex> lock(s_pickLock); s_pickStatus = "uploading..."; }
        const std::string url = ImageUpload(path);
        DebugLog("image pick: upload %s", url.empty() ? "FAILED" : "ok");
        PickFinish(url.empty() ? "upload failed" : "uploaded", url);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    s_pickBusy = false;
    return 0;
}

bool s_restored = false;
bool s_wasInGame = false;
} // namespace

// ==== public ========================================================================
void GameExtras_Install()
{
    if (o_GetPlayerInfo || !g_engine) return;
    InstallVTableHook(g_engine, 8, "bot names: GetPlayerInfo", (void*)h_GetPlayerInfo, (void**)&o_GetPlayerInfo);
}

void ChatTranslator_OnUserMessage(int type, void* bf)
{
    if (!g_chatTranslator || g_eject || s_stop || !bf || (type != 3 && type != 4)) return;

    uint8_t data[2048];
    const int bytes = CopyUserMessage(bf, data, sizeof(data));
    if (bytes <= 0) return;

    n_game::SayMsg say;
    if (!(type == 4 ? n_game::ParseSayText2(data, bytes, say) : n_game::ParseSayText(data, bytes, say))) return;
    // ent 0 = server adverts / notices; bWantsToChat 0 = not a chat line
    if (!say.chat || say.ent <= 0 || say.ent == VCall<int>(g_engine, 12)) return;

    std::string playerName;
    uint8_t pi[kPiSize + 64] = {};
    if (RawPlayerInfo(say.ent, pi)) playerName.assign((const char*)pi + kPiName, strnlen((const char*)pi + kPiName, kPiNameSize));

    std::string name, text;
    if (!n_game::PickChatLine(type, say, playerName, name, text) || !n_game::HasLetter(text)) return;

    const int lang = (std::min)((std::max)(g_chatTranslatorLang, 0), kLanguageCount - 1);
    std::lock_guard<std::mutex> lock(s_chatLock);
    if (s_jobs.size() >= 16) return;
    s_jobs.push_back({ std::move(name), std::move(text), kLanguageCodes[lang] });
    if (!s_chatWorker)
    {
        s_chatWorker = StartWorker(ChatWorker, nullptr);
    }
}

void GameExtras_ExecCvars()
{
    for (int i = 0; i < kCvarRows; ++i)
        if (g_cvarRows[i][0]) EngineExec(g_cvarRows[i]);
}

// ==== clantag: botox misc.cpp update_clantag (animation = clantag.h) ==========================
// CSS x64 engine.dll RVA 0x6a650 (disasm 11-08), called by cl_clanid's change callback: ONE arg
// (CS:GO's took tag + name). new KeyValues("ClanTagChanged"), SetString("tag", arg),
// ServerCmdKeyValues. server.dll ClientCommandKeyValues: GetString("tag") -> SetClanTag, no
// Steam group check, so any text shows. Game thread (FSN), like botox's paint_traverse call.
using SetClanTag_t = void(__fastcall*)(const char*);

static void SendClanTag(SetClanTag_t fn, const char* tag)
{
    __try { fn(tag); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int LocalTeam(int localIdx)
{
    void* e = localIdx > 0 && g_offTeamNum > 0 ? GetClientEntity(localIdx) : nullptr;
    if (!e) return -1;
    __try { return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(e) + g_offTeamNum); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void Clantag_Frame()
{
    static const SetClanTag_t setTag = [] {
        auto p = reinterpret_cast<SetClanTag_t>(PatternScan(GetModuleHandleA("engine.dll"),
            "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 B9 48 00 00 00 E8 ? ? ? ? 48 85 C0 74 14 48 8D 15 ? ? ? ? "
            "48 8B C8 E8 ? ? ? ? 48 8B D8 EB 02 33 DB 4C 8B C7 48 8D 15"));
        DebugLog("clantag: SetClanTag %s", p ? "found" : "NOT FOUND");
        return p;
    }();
    if (!setTag || !g_engine) return;

    static std::string lastSent, lastLevel;
    static bool cleared = true;
    static clantag::State state;

    const int localIdx = GetLocalPlayerIndex();
    const int team = LocalTeam(localIdx);
    const bool onTeam = IsInGame() && (team == 2 || team == 3);

    if (!g_clantag || g_safeMode || g_eject)
    {
        if (!cleared)
        {
            // same send rule as below: never emit CmdKeyValues while not on a team
            if (onTeam) SendClanTag(setTag, "");
            lastSent.clear();
            cleared = true;
        }
        return;
    }

    if (!VCall<bool>(g_engine, 27)) { lastSent.clear(); return; }   // IsConnected
    if (!IsInGame() || localIdx < 1) return;
    if (!onTeam) { lastSent.clear(); return; }

    // changelevel keeps IsConnected true but the new server never got the tag
    const char* level = VCall<const char*>(g_engine, 51);   // GetLevelName
    if (level && lastLevel != level) { lastLevel = level; lastSent.clear(); }

    clantag::Settings s;
    s.anim = std::clamp(g_clantagAnim, 0, 4);
    s.text = g_clantagText;
    s.speed = g_clantagSpeed;
    s.hold = g_clantagHold;
    s.frames = g_clantagFrames;
    if (s.anim == 4) MediaPlayer_Track(s.title, s.artist);

    const std::string tag = clantag::Step(state, s, GetTickCount64() / 1000.0);
    if (tag == lastSent || tag.length() > 32) return;

    SendClanTag(setTag, tag.c_str());
    lastSent = tag;
    cleared = false;
}

void GameExtras_Frame(int stage)
{
    if (stage != 5) return;   // FRAME_RENDER_START: once a frame, after the net update, before paint

    if (g_eject)
    {
        if (!s_restored) { s_restored = true; PingReleaseAll(); Performance(true); Clantag_Frame(); }
        return;
    }

    Clantag_Frame();
    ChatPrintPending();

    static DWORD s_last = 0;
    const DWORD now = GetTickCount();
    if (now - s_last >= 100)
    {
        s_last = now;
        Performance(false);

        if (g_botNames)
        {
            const uint32_t sig = SettingsSig();
            if (sig != s_botSig.load()) s_botSig = sig;
            if (g_botNamesMode == 3) StartFetch();
        }
        PingScan();

        // botox exec_cvar_list: the list runs on the edge into the game (map load / join)
        const bool inGame = IsInGame();
        if (inGame && !s_wasInGame)
        {
            int n = 0;
            for (int i = 0; i < kCvarRows; ++i) n += g_cvarRows[i][0] != 0;
            if (n) { DebugLog("cvars: running %d rows on join", n); GameExtras_ExecCvars(); }
        }
        s_wasInGame = inGame;
    }
    PingApply();
}

void GameExtras_Shutdown()
{
    // no frame ran after g_eject (minimised): put the knobs back from here
    if (!s_restored) { s_restored = true; PingReleaseAll(); Performance(true); }

    s_stop = true;
    {
        std::lock_guard<std::mutex> lock(s_sessionLock);
        for (HINTERNET h : s_sessions) WinHttpCloseHandle(h);
        s_sessions.clear();
    }
    // an open file dialog holds its thread: cancel it like botox image_pick_cancel
    if (const DWORD t = s_pickThread)
        EnumThreadWindows(t, [](HWND w, LPARAM) -> BOOL {
            char cls[16] = {};
            if (GetClassNameA(w, cls, sizeof(cls)) && !strcmp(cls, "#32770")) PostMessageA(w, WM_COMMAND, IDCANCEL, 0);
            return TRUE;
        }, 0);

    std::vector<HANDLE> threads;
    {
        std::lock_guard<std::mutex> lock(s_threadLock);
        threads.swap(s_threads);
    }
    for (HANDLE h : threads)
    {
        if (WaitForSingleObject(h, 4000) != WAIT_OBJECT_0) DebugLog("game: a worker did not stop in 4s");
        CloseHandle(h);
    }
}

void BotNames_Status(char* out, size_t size)
{
    std::lock_guard<std::mutex> lock(s_poolLock);
    if (s_fetching) snprintf(out, size, "fetching steam names... %zu/%zu", s_pool.size(), kPoolWant);
    else if (s_fetchDone && s_pool.empty()) snprintf(out, size, "steam unreachable, randomize to retry");
    else snprintf(out, size, "%zu steam names", s_pool.size());
}

void BotNames_Randomize()
{
    ++s_generation;
    if (g_botNamesMode == 3 && !s_fetching)
    {
        std::lock_guard<std::mutex> lock(s_poolLock);
        s_pool.clear();
        s_fetchDone = false;
    }
}

void ImagePick_Input(const char* label, const char* hint, char* value, size_t size, bool upload)
{
    const ImGuiID id = ImGui::GetID(label);
    std::string status;
    {
        std::lock_guard<std::mutex> lock(s_pickLock);
        if (s_pickId == id)
        {
            status = s_pickStatus;
            // botox image_pick_apply, applied by the box that asked (render thread = the menu's)
            if (s_pickDone)
            {
                if (!s_pickResult.empty()) strncpy_s(value, size, s_pickResult.c_str(), _TRUNCATE);
                s_pickResult.clear();
                s_pickDone = false;
            }
        }
    }

    ImGui::InputTextWithHint(label, hint, value, size);

    const bool busy = s_pickBusy;
    char button[96];
    snprintf(button, sizeof(button), "%s##pick %s", upload ? "upload from pc" : "browse", label);
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(button, ImVec2(-1.f, 15.f)) && !busy)
    {
        {
            std::lock_guard<std::mutex> lock(s_pickLock);
            s_pickId = id;
            s_pickStatus = "picking...";
            s_pickResult.clear();
            s_pickDone = false;
        }
        s_pickBusy = true;   // before the thread runs: an eject right now must still wait for it
        if (!StartWorker(PickWorker, upload ? reinterpret_cast<void*>(1) : nullptr)) s_pickBusy = false;
    }
    ImGui::EndDisabled();

    if (!status.empty()) ImGui::Label("%s", status.c_str());
}
