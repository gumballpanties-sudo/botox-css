#include "discord_ex.h"
#include "unitE_logic.h"
#include <string>
#include <ctime>
#include <cstdint>
#include <cstring>
#include "../../globals/pinned_thread.h"

// botox hacks/misc/discord_rpc.cpp 1:1 over the raw Discord IPC pipe. Frame = uint32 op +
// uint32 len + JSON. Old version never showed anything: empty default app id (thread idled
// forever), empty large_image (Discord rejects empty asset strings), SET_ACTIVITY sent before
// READY, no ping/pong or close handling, dead pipe never reopened.
namespace {

constexpr const char* k_client_id = "1554733253632720926";             // botox app id
constexpr const char* k_image     = "https://i.imgur.com/KDUzwZf.png";  // botox image

enum : uint32_t { op_handshake = 0, op_frame = 1, op_close = 2, op_ping = 3, op_pong = 4 };

HANDLE s_pipe   = INVALID_HANDLE_VALUE;
HANDLE s_thread = nullptr;

void ClosePipe()
{
    if (s_pipe != INVALID_HANDLE_VALUE) CloseHandle(s_pipe);
    s_pipe = INVALID_HANDLE_VALUE;
}

bool SendFrame(uint32_t op, const std::string& json)
{
    const uint32_t len = (uint32_t)json.size();
    std::string frame(8, '\0');
    memcpy(frame.data(), &op, 4);
    memcpy(frame.data() + 4, &len, 4);
    frame += json;
    DWORD written = 0;
    if (WriteFile(s_pipe, frame.data(), (DWORD)frame.size(), &written, nullptr) && written == frame.size())
        return true;
    DebugLog("discord rpc: write failed (err %lu), pipe closed", GetLastError());
    ClosePipe();
    return false;
}

const char* ClientId() { return g_discordAppId[0] ? g_discordAppId : k_client_id; }

bool OpenPipe()
{
    for (int i = 0; i < 10; ++i)
    {
        char name[64];
        snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        s_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (s_pipe == INVALID_HANDLE_VALUE) continue;
        DebugLog("discord rpc: pipe %s open, handshake client_id=%s", name, ClientId());
        SendFrame(op_handshake, std::string("{\"v\":1,\"client_id\":\"") + n_unitE::JsonEscape(ClientId()) + "\"}");
        return true;
    }
    return false;
}

// never blocks: reads only frames already whole in the pipe. true = discord answered (READY / ack)
bool Drain()
{
    bool answered = false;
    while (s_pipe != INVALID_HANDLE_VALUE)
    {
        uint32_t hdr[2] = {};
        DWORD peeked = 0, avail = 0;
        if (!PeekNamedPipe(s_pipe, hdr, 8, &peeked, &avail, nullptr)) { DebugLog("discord rpc: pipe gone"); ClosePipe(); break; }
        if (peeked < 8 || avail < 8 + hdr[1]) break;
        if (hdr[1] > 64 * 1024) { ClosePipe(); break; }

        std::string body(hdr[1], '\0');
        DWORD read = 0;
        if (!ReadFile(s_pipe, hdr, 8, &read, nullptr) ||
            (!body.empty() && !ReadFile(s_pipe, body.data(), hdr[1], &read, nullptr)))
        { ClosePipe(); break; }

        switch (hdr[0])
        {
        case op_frame:
            answered = true;
            // discord answers a bad payload with evt ERROR -- log it so the next session explains itself
            if (body.find("\"evt\":\"ERROR\"") != std::string::npos)
                DebugLog("discord rpc: error reply: %.300s", body.c_str());
            break;
        case op_ping:  SendFrame(op_pong, body); break;
        case op_close: DebugLog("discord rpc: closed by discord: %.300s", body.c_str()); ClosePipe(); break;
        }
    }
    return answered && s_pipe != INVALID_HANDLE_VALUE;
}

// GetLevelName = IVEngineClient slot 51, same confirmed slot as recorder.cpp / psassist.inl.
// ponytail: read from this thread (botox reads it on the game thread) -- a plain buffer read,
// SEH-guarded; move to a frame hook if it ever tears.
// (no C++ objects here: MSVC C2712 forbids __try next to unwinding)
const char* LevelNameRaw()
{
    if (!g_engine) return "";
    const char* n = nullptr;
    __try
    {
        if (!VCall<bool>(g_engine, 26)) return "";   // IsInGame, globals.h
        void** vt = *(void***)g_engine;
        n = ((const char*(__fastcall*)(void*))vt[51])(g_engine);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "";
}

DWORD WINAPI DiscordThread(LPVOID)
{
    const long long start = (long long)time(nullptr);
    std::string sent = "\x01";
    bool ready = false, wasReady = false;
    int nonce = 0, retry = 0, fails = 0;

    while (!g_eject)
    {
        if (!g_discordEnable)
        {
            // discord drops the presence when the pipe closes
            if (s_pipe != INVALID_HANDLE_VALUE) DebugLog("discord rpc: disabled, pipe closed");
            ClosePipe();
            ready = false; sent = "\x01"; retry = 0;
        }
        else if (s_pipe == INVALID_HANDLE_VALUE)
        {
            ready = false; sent = "\x01";
            if (--retry <= 0)
            {
                retry = 15;
                if (OpenPipe()) fails = 0;
                else if ((fails++ % 20) == 0)   // log 1st failure, then every ~5 min
                    DebugLog("discord rpc: no discord-ipc pipe (discord not running?) -- retrying every 15s");
            }
        }
        else
        {
            ready |= Drain();
            if (s_pipe == INVALID_HANDLE_VALUE) ready = false;
            if (ready && !wasReady) DebugLog("discord rpc: READY");
            wasReady = ready;

            if (ready)
            {
                const std::string map   = n_unitE::JsonEscape(n_unitE::LevelNameShort(LevelNameRaw()).c_str());
                const std::string where = map.empty() ? "in menu" : "playing on " + map;
                const std::string image = g_discordImageKey[0] ? n_unitE::JsonEscape(g_discordImageKey) : k_image;

                const std::string activity =
                    "{\"details\":\"" + where + "\",\"timestamps\":{\"start\":" + std::to_string(start) +
                    "},\"assets\":{\"large_image\":\"" + image + "\",\"large_text\":\"botox\"}}";

                if (activity != sent)
                {
                    const std::string json =
                        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                        ",\"activity\":" + activity + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
                    // only sent on change, so this log is naturally rate-limited
                    if (SendFrame(op_frame, json))
                    {
                        sent = activity;
                        DebugLog("discord rpc: presence -> %s", where.c_str());
                    }
                }
            }
        }

        for (int i = 0; i < 4 && !g_eject; ++i) Sleep(250);
    }
    ClosePipe();
    return 0;
}

} // namespace

void DiscordRpc_Start()
{
    if (s_thread) return;
    s_thread = CreatePinnedThread([] { DiscordThread(nullptr); });
    if (!s_thread) DebugLog("discord rpc: CreateThread failed (err %lu)", GetLastError());
}

void DiscordRpc_Shutdown()
{
    if (!s_thread) return;
    WaitForSingleObject(s_thread, 3000);
    CloseHandle(s_thread);
    s_thread = nullptr;
    ClosePipe();
}
