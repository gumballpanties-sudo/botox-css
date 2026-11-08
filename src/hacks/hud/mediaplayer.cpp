#include "mediaplayer.h"
#include "watermark.h"          // g_wmBox -- botox anchors the player under the watermark
#include "watermark_styles.h"   // Watermark_MediaLook / Watermark_MediaFrame -- the style's colours + box
#include "mp_art.h"      // cover art scale + centre crop (gate tested, no winrt in it)
#include "mp_time.h"     // timeline -> position + length (gate tested, no winrt in it)
#include <ole2.h>        // WIN32_LEAN_AND_MEAN drops it; media_file.h's propvarutil.h needs V_VT
#include "kopuz.h"       // botox globals/media_player/kopuz.h, verbatim
#include "media_file.h"  // botox globals/media_player/media_file.h, verbatim
#include "../../dependencies/imgui/imgui_internal.h"   // GImGui->HoveredWindow: a menu window over the player eats the click
#include "../../render/dpi.h"   // botox panel stretch block + panel_mouse (dpi scale panels)

// botox csgo-sdk/globals/media_player/media_player.cpp on_update + run_command + main.cpp
// media_player_thread, 1:1: playing SMTC session > playing kopuz > audible app (song file it has
// open, else its window title; discord + apps with a mic stream skipped) > paused kopuz > paused
// session. Controls: SMTC session / kopuz rpc / WM_APPCOMMAND to the app, media key last.
//
// ONE CHANGE: art. botox hands the encoded bytes to D3DXCreateTextureFromFileInMemory; this dll
// links d3d9 only, so the same bytes go through winrt BitmapDecoder -> BGRA8 here and the render
// thread does CreateTexture + LockRect.
// Waveform = botox visualizer_thread 1:1 (WASAPI process loopback of the media app's tree, else
// everything but this game's tree -> audio_bands.h FFT). Lyrics = botox lyrics_thread 1:1 (lrclib
// synced lyrics over WinHTTP); the json read is mp_lyrics.h (no json lib here). Both are gate
// tested by tests/mpaudio_test.cpp. In game control = botox media_player_binds.
#pragma comment(lib, "windowsapp")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "winhttp")
#pragma comment(lib, "mmdevapi")

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <appmodel.h>
#include <propsys.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include "audio_bands.h"   // botox globals/media_player/audio_bands.h, verbatim
#include "mp_lyrics.h"     // lrclib json read + LRC parse + row window (gate tested)

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../globals/pinned_thread.h"

namespace wf  = winrt::Windows::Foundation;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

using Session = wmc::GlobalSystemMediaTransportControlsSession;
using Manager = wmc::GlobalSystemMediaTransportControlsSessionManager;

// ==== shared state ==============================================================================
// Written by the poll thread, read by the render thread. Everything but the atomics is under s_mtx.
static std::mutex s_mtx;
static std::string s_title, s_artist;

static std::vector<unsigned char> s_px;        // BGRA8, s_pxW * s_pxH * 4
static int  s_pxW = 0, s_pxH = 0;
static bool s_pxDirty = false;                 // -> the render thread rebuilds the texture

static long long s_totalMs = 0, s_curMs = 0;
static double s_rate = 1.0;                    // playback speed, for the render-side slide
static std::chrono::steady_clock::time_point s_stamp{};

static std::atomic<bool> s_has{ false };       // botox m_has_media
static std::atomic<bool> s_playing{ false };   // botox m_is_playing

// botox m_source_app / m_source_kind: session's AppUserModelId (no session: audible app's exe path)
enum Source : int { source_none, source_session, source_kopuz, source_app };
static std::wstring s_source;
static Source s_kind = source_none;

// botox e_command: 1 previous, 2 toggle, 3 next. 0 = none
static std::atomic<int> s_pendingCmd{ 0 };

// poll thread only. an audible-only app ( no SMTC ) goes silent once paused, keep it shown as paused
static bool s_appPaused = false;
static std::chrono::steady_clock::time_point s_appPausedAt{};
static std::vector<unsigned char> s_thumb;     // encoded bytes currently shown (botox m_thumbnail)
static std::wstring s_thumbKey;
static int s_thumbWait = 0;

// Render thread only.
static IDirect3DTexture9* s_tex = nullptr;

static HANDLE s_thread = nullptr;

// botox m_bands / m_bands_mutex: visualizer thread writes, render thread copies
static std::mutex s_bandsMtx;
static float s_bands[n_audio_bands::k_bands]{};
static HANDLE s_visThread = nullptr;

// botox m_lyrics / m_lyrics_generation: lyrics thread writes, render thread copies on a new generation
static std::mutex s_lyricsMtx;
static std::vector<mpl::Line> s_lyrics;
static int s_lyricsGen = 0;
static std::string s_lyricsKey;     // lyrics thread only
static HANDLE s_lyricsThread = nullptr;
// the lrclib request in flight: eject closes it so a 10 s receive timeout cannot outlive the dll
static std::mutex s_httpMtx;
static HINTERNET s_httpReq = nullptr;

// ==== poll thread ===============================================================================
static std::string ToUtf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

// Encoded image (jpg / png / bmp / webp ..) -> straight-alpha BGRA8, always kArt x kArt.
// Straight, not premultiplied: ImGui's dx9 backend blends SRCALPHA/INVSRCALPHA. Scale + crop in
// the decoder (mp_art.h, gate test tests/mediaplayer_test.cpp). 64 = pow2, no NONPOW2 rules.
static constexpr uint32_t kArt = 64;

static bool DecodeArt(const std::vector<unsigned char>& bytes, std::vector<unsigned char>& out, int& w, int& h)
{
    if (bytes.empty()) return false;
    try {
        wss::Buffer in((uint32_t)bytes.size());
        std::copy(bytes.begin(), bytes.end(), in.data());
        in.Length((uint32_t)bytes.size());
        wss::InMemoryRandomAccessStream stream;
        stream.WriteAsync(in).get();
        stream.Seek(0);

        const auto decoder = wgi::BitmapDecoder::CreateAsync(stream).get();
        const uint32_t sw = decoder.PixelWidth(), sh = decoder.PixelHeight();
        if (!sw || !sh) return false;

        const mp::ArtCrop c = mp::ArtCropFor(sw, sh, kArt);
        wgi::BitmapTransform tr;
        tr.InterpolationMode(wgi::BitmapInterpolationMode::Fant);
        tr.ScaledWidth(c.scaledW);
        tr.ScaledHeight(c.scaledH);
        wgi::BitmapBounds b{};
        b.X = c.x; b.Y = c.y; b.Width = c.w; b.Height = c.h;
        tr.Bounds(b);

        const auto bmp = decoder.GetSoftwareBitmapAsync(wgi::BitmapPixelFormat::Bgra8,
                                                        wgi::BitmapAlphaMode::Straight, tr,
                                                        wgi::ExifOrientationMode::IgnoreExifOrientation,
                                                        wgi::ColorManagementMode::DoNotColorManage).get();
        if (!bmp) return false;
        w = (int)bmp.PixelWidth();
        h = (int)bmp.PixelHeight();
        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;

        wss::Buffer buf((uint32_t)(w * h * 4));
        bmp.CopyToBuffer(buf);
        if (buf.Length() < (uint32_t)(w * h * 4)) return false;
        out.assign(buf.data(), buf.data() + (size_t)w * h * 4);
        return true;
    } catch (...) {
        return false;
    }
}

namespace
{
    template<typename t>
    void safe_release(t*& pointer)
    {
        if (pointer) { pointer->Release(); pointer = nullptr; }
    }

    std::wstring lower(std::wstring text)
    {
        for (auto& c : text) c = (wchar_t)std::towlower(c);
        return text;
    }

    // "C:\x\Chrome.EXE" / "Spotify.exe" / "MSEdge" -> "chrome" / "spotify" / "msedge"
    std::wstring exe_stem(std::wstring name)
    {
        name = lower(std::move(name));
        if (const auto slash = name.find_last_of(L"\\/"); slash != std::wstring::npos) name.erase(0, slash + 1);
        if (name.size() > 4 && name.ends_with(L".exe")) name.resize(name.size() - 4);
        return name;
    }

    // apple music desktop sends "artist — album": cut at em dash
    std::wstring strip_album(std::wstring artist)
    {
        const auto dash = artist.find(L'\u2014');
        if (dash == std::wstring::npos) return artist;
        artist.resize(dash);
        while (!artist.empty() && std::iswspace(artist.back())) artist.pop_back();
        return artist;
    }

    // a hung app must not freeze the media thread: null on timeout / error
    template<typename op_t>
    auto await_for(const op_t& op) -> decltype(op.GetResults())
    {
        if (op.wait_for(std::chrono::seconds(2)) == wf::AsyncStatus::Completed) return op.GetResults();
        op.Cancel();
        return nullptr;
    }

    bool is_playing(const Session& session)
    {
        try {
            const auto info = session.GetPlaybackInfo();
            return info && info.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        } catch (...) {
            return false;
        }
    }

    // GetCurrentSession = what windows last focused, often null or a paused tab while another app plays
    Session pick_session(const Manager& manager)
    {
        const auto current = manager.GetCurrentSession();
        if (current && is_playing(current)) return current;

        const auto sessions = manager.GetSessions();
        for (const auto& session : sessions)
            if (is_playing(session)) return session;

        if (current) return current;
        return sessions.Size() ? sessions.GetAt(0) : nullptr;
    }

    std::vector<unsigned char> read_thumbnail(const wmc::GlobalSystemMediaTransportControlsSessionMediaProperties& info)
    {
        try {
            const auto reference = info.Thumbnail();
            if (!reference) return {};

            const auto stream = await_for(reference.OpenReadAsync());
            if (!stream || !stream.Size() || stream.Size() > 16u * 1024u * 1024u) return {};

            wss::Buffer buffer((uint32_t)stream.Size());
            const auto read = await_for(stream.ReadAsync(buffer, buffer.Capacity(), wss::InputStreamOptions::ReadAhead));
            if (!read) return {};
            return { read.data(), read.data() + read.Length() };
        } catch (...) {
            return {};
        }
    }

    std::wstring process_path(const DWORD pid)
    {
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        return ok ? lower(path) : std::wstring{};
    }

    // exe paths with an active stream on the default `flow` device, `sounding` = peak above silence too
    std::vector<std::wstring> session_apps(IMMDeviceEnumerator* enumerator, const EDataFlow flow, const bool sounding)
    {
        std::vector<std::wstring> out{};
        IMMDevice* device                 = nullptr;
        IAudioSessionManager2* manager    = nullptr;
        IAudioSessionEnumerator* sessions = nullptr;
        int count                         = 0;

        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&manager))) &&
            SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && SUCCEEDED(sessions->GetCount(&count))) {
            for (int i = 0; i < count; i++) {
                IAudioSessionControl* control   = nullptr;
                IAudioSessionControl2* control2 = nullptr;
                IAudioMeterInformation* meter   = nullptr;
                AudioSessionState state         = AudioSessionStateInactive;
                DWORD pid                       = 0;
                float peak                      = 0.f;

                if (SUCCEEDED(sessions->GetSession(i, &control)) && SUCCEEDED(control->GetState(&state)) && state == AudioSessionStateActive &&
                    SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&control2))) && control2->IsSystemSoundsSession() == S_FALSE &&
                    SUCCEEDED(control2->GetProcessId(&pid)) && pid &&
                    (!sounding || (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&meter))) && SUCCEEDED(meter->GetPeakValue(&peak)) && peak > 0.001f))) {
                    if (auto path = process_path(pid); !path.empty()) out.push_back(std::move(path));
                }

                safe_release(meter);
                safe_release(control2);
                safe_release(control);
            }
        }

        safe_release(sessions);
        safe_release(manager);
        safe_release(device);
        return out;
    }

    // steam overlay web browser = steamwebhelper ( CEF ): no SMTC session, and every window it has is hidden
    // ( the overlay draws it inside the game ). page title = one of its hidden SDL_app windows
    bool is_steam(const std::wstring& path) { return exe_stem(path) == L"steamwebhelper"; }

    // "(2) Artist - Song - YouTube" -> "Artist - Song". "" = steam's own ui window ( main / menu / friends .. )
    std::wstring steam_page_title(std::wstring text)
    {
        if (text == L"Menu" || text.starts_with(L"Steam")) return {};
        if (text.size() > 2 && text[0] == L'(') {
            std::size_t close = 1;
            while (close < text.size() && std::iswdigit(text[close])) close++;
            if (close > 1 && text.compare(close, 2, L") ") == 0) text.erase(0, close + 2);
        }
        for (const wchar_t* site : { L" - YouTube Music", L" - YouTube" })
            if (text.ends_with(site)) { text.resize(text.size() - wcslen(site)); break; }
        return text;
    }

    std::wstring window_title(const std::wstring& path)
    {
        struct search_t {
            const std::wstring* m_path;
            bool m_steam;
            std::wstring m_title;
        } search{ &path, is_steam(path), {} };

        EnumWindows(
            [](HWND window, LPARAM param) -> BOOL {
                auto& search = *reinterpret_cast<search_t*>(param);

                // own windows skipped before GetWindowText: same-process text is a SendMessage to the game thread
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (pid == GetCurrentProcessId()) return TRUE;
                if (!search.m_steam && (!IsWindowVisible(window) || GetWindow(window, GW_OWNER))) return TRUE;

                wchar_t text[512]{};
                if (!GetWindowTextW(window, text, 512) || process_path(pid) != *search.m_path) return TRUE;

                if (search.m_steam) {
                    wchar_t cls[64]{};
                    if (!GetClassNameW(window, cls, 64) || wcscmp(cls, L"SDL_app") != 0) return TRUE;

                    // ponytail: name filter is a guess at steam's window names, the log is what tunes it
                    static std::vector<std::wstring> s_seen{};
                    if (s_seen.size() < 64 && std::find(s_seen.begin(), s_seen.end(), text) == s_seen.end()) {
                        s_seen.push_back(text);
                        DebugLog("media: steamwebhelper window \"%S\" visible=%d", text, IsWindowVisible(window) ? 1 : 0);
                    }

                    auto page = steam_page_title(text);
                    if (page.empty()) return TRUE;
                    search.m_title = std::move(page);
                    return FALSE;
                }

                search.m_title = text;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&search));

        return search.m_title;
    }

    // "12. Artist - Title.mp3 - VLC media player" / "Artist - Title [foobar2000]" -> "Artist", "Title"
    void split_title(std::wstring text, const std::wstring& stem, std::wstring& artist, std::wstring& title)
    {
        std::size_t last = 0;
        for (const wchar_t* cut : { L" - ", L" [", L" | " })
            if (const auto at = text.rfind(cut); at != std::wstring::npos) last = (std::max)(last, at);
        if (last && lower(text.substr(last)).find(stem) != std::wstring::npos) text.resize(last);

        std::size_t digits = 0;
        while (digits < text.size() && std::iswdigit(text[digits])) digits++;
        if (digits && text.compare(digits, 2, L". ") == 0) text.erase(0, digits + 2);

        if (const auto dot = text.rfind(L'.'); dot != std::wstring::npos) {
            static constexpr const wchar_t* k_media[]{ L".mp3", L".flac", L".wav", L".ogg", L".opus", L".m4a", L".aac", L".wma", L".mp4", L".mkv", L".webm" };
            const auto ext = lower(text.substr(dot));
            if (std::find_if(std::begin(k_media), std::end(k_media), [&](const wchar_t* e) { return ext == e; }) != std::end(k_media))
                text.resize(dot);
        }

        const auto at = text.find(L" - ");
        artist = at != std::wstring::npos ? text.substr(0, at) : std::wstring{};
        title  = at != std::wstring::npos ? text.substr(at + 3) : text;
    }

    /* no GSMTC player ( vlc 3, winamp, broken SMTC ): an app that makes sound and has no mic stream ( voice chat ).
       one with a song file open beats one with only a window title ( discord tab in a browser ) */
    bool find_audio_app(std::wstring& path, std::wstring& window, std::vector<std::wstring>& files)
    {
        IMMDeviceEnumerator* enumerator = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))))
            return false;

        const auto playing = session_apps(enumerator, eRender, true);
        const auto talking = session_apps(enumerator, eCapture, false);
        enumerator->Release();

        const std::wstring self = process_path(GetCurrentProcessId());
        std::vector<std::wstring> titled{};
        for (const auto& app : playing) {
            // discord / ptb / canary: call + notification sounds, never a song
            if (app == self || exe_stem(app).starts_with(L"discord") || std::find(talking.begin(), talking.end(), app) != talking.end() ||
                std::find(titled.begin(), titled.end(), app) != titled.end())
                continue;

            if (files = n_media_file::open_media(app); !files.empty()) {
                path   = app;
                window = window_title(app);
                return true;
            }

            if (!window_title(app).empty() || is_steam(app)) titled.push_back(app);
        }

        if (titled.empty()) return false;

        path   = titled.front();
        window = window_title(path);
        if (window.empty()) window = L"Steam overlay";   // only steam gets here: playing, page title not found
        return true;
    }

    // botox get_position_ms
    double position_ms()
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        auto current = (double)s_curMs;
        if (s_playing)
            current += s_rate * (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_stamp).count();
        return current;
    }
}

// botox impl_t::on_update
static void OnUpdate(const Manager& manager)
{
    const auto clear = []() {
        s_has     = false;
        s_playing = false;
        std::lock_guard<std::mutex> lock(s_mtx);
        s_source.clear();
        s_kind = source_none;
    };

    struct now_t {
        std::string m_title{}, m_artist{};
        std::wstring m_source{};
        Source m_kind{};
        std::optional<std::vector<unsigned char>> m_thumbnail{}; // nullopt = keep
        long long m_total_ms{}, m_current_ms{};
        std::chrono::steady_clock::time_point m_stamp{ std::chrono::steady_clock::now() };
        double m_rate{ 1.0 };
        bool m_playing{};
    };

    const auto publish = [](now_t& now) {
        // art changed: decode here, outside the lock (botox: render thread D3DX, see top)
        bool artChanged = now.m_thumbnail && *now.m_thumbnail != s_thumb;
        std::vector<unsigned char> px;
        int pw = 0, ph = 0;
        if (artChanged) {
            s_thumb = std::move(*now.m_thumbnail);
            if (!DecodeArt(s_thumb, px, pw, ph)) { px.clear(); pw = ph = 0; }
        }
        {
            std::lock_guard<std::mutex> lock(s_mtx);

            s_title  = std::move(now.m_title);
            s_artist = std::move(now.m_artist);
            s_source = std::move(now.m_source);
            s_kind   = now.m_kind;
            if (now.m_kind != source_app) s_appPaused = false;

            if (artChanged) {
                s_px = std::move(px);
                s_pxW = pw; s_pxH = ph;
                s_pxDirty = true;
            }

            s_totalMs = now.m_total_ms;
            s_curMs   = now.m_current_ms;
            s_stamp   = now.m_stamp;
            s_rate    = now.m_rate;
        }

        s_playing = now.m_playing;
        s_has     = true;
    };

    /* art on track change, again the next 5 updates ( a skip shows the new title with the old or no art first ), then every 3rd
       while still missing. a failed re-read never wipes shown art, a bad thumbnail never hides the player */
    const auto thumbnail = [](const std::wstring& key, const auto& read, const bool retry = true) -> std::optional<std::vector<unsigned char>> {
        if (key != s_thumbKey) {
            s_thumbKey  = key;
            s_thumbWait = 0;
            return read();
        }

        const int wait = ++s_thumbWait;
        if (!retry || (wait > 5 && (!s_thumb.empty() || wait % 3))) return std::nullopt;

        auto art = read();
        if (art.empty() && !s_thumb.empty()) return std::nullopt;
        return art;
    };

    const auto from_session = [&](const Session& session) {
        const auto info = await_for(session.TryGetMediaPropertiesAsync());
        if (!info) return;

        now_t now{};
        now.m_source    = session.SourceAppUserModelId();
        now.m_kind      = source_session;
        now.m_title     = ToUtf8(info.Title());
        now.m_artist    = ToUtf8(strip_album(std::wstring{ info.Artist() }));
        now.m_thumbnail = thumbnail(now.m_source + L'\n' + std::wstring{ info.Title() } + L'\n' + std::wstring{ info.Artist() },
                                    [&] { return read_thumbnail(info); });

        const auto timeline = session.GetTimelineProperties();

        /* Position is at LastUpdatedTime, not now (browsers only push it on play / pause / seek).
           zero or future stamp = never filled, treat as fresh. */
        auto age = winrt::clock::now() - timeline.LastUpdatedTime();
        if (timeline.LastUpdatedTime().time_since_epoch().count() == 0 || age.count() < 0) age = {};

        now.m_total_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(timeline.EndTime() - timeline.StartTime()).count();
        now.m_current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeline.Position() - timeline.StartTime()).count();
        now.m_stamp -= std::chrono::duration_cast<std::chrono::steady_clock::duration>(age);

        if (const auto playback = session.GetPlaybackInfo()) {
            now.m_playing = playback.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            if (const auto rate = playback.PlaybackRate(); rate && rate.Value() > 0.0) now.m_rate = rate.Value();
        }

        publish(now);
    };

    const auto from_kopuz = [&](n_kopuz::state_t& kopuz) {
        now_t now{};
        now.m_source     = kopuz.m_app;
        now.m_kind       = source_kopuz;
        now.m_title      = std::move(kopuz.m_title);
        now.m_artist     = std::move(kopuz.m_artist);
        now.m_total_ms   = kopuz.m_total_ms;
        now.m_current_ms = kopuz.m_position_ms;
        now.m_playing    = kopuz.m_playing;
        now.m_thumbnail  = thumbnail(L"kopuz\n" + std::to_wstring(kopuz.m_art_version) + L'\n' + std::wstring(kopuz.m_art_request.begin(), kopuz.m_art_request.end()),
                                     [&] { return n_kopuz::get_artwork(kopuz.m_art_request); });
        publish(now);
    };

    const auto from_audio = [&]() {
        static std::wstring s_app{}, s_file{};
        static n_media_file::tags_t s_tags{};
        static std::chrono::steady_clock::time_point s_file_start{};
        static bool s_file_timed{}, s_watching{};
        static int s_quiet{};

        std::wstring path{}, window{};
        std::vector<std::wstring> files{};
        if (!find_audio_app(path, window, files)) {
            // paused from here: stays until the app closes, gap between songs: hold what's shown a few updates
            if (s_appPaused && !s_app.empty() && s_has && !window_title(s_app).empty()) return true;

            if (++s_quiet > 3) s_app.clear();
            return !s_app.empty() && s_has.load();
        }

        s_quiet = 0;

        // sounding again ( resumed in the app itself ). the pause needs a moment to go quiet, so not right after it
        if (s_appPaused && path == s_app && std::chrono::steady_clock::now() - s_appPausedAt > std::chrono::seconds(2)) s_appPaused = false;
        if (s_appPaused && path == s_app) return true;

        // two updates in a row, a notification ding is shorter
        if (path != s_app) {
            s_app      = path;
            s_watching = false;
            s_file.clear();
            return false;
        }

        /* the song file it has open beats the window title. one still open stays ( gapless players open the next early ).
           position only known for a file that showed up while watching */
        if (std::find(files.begin(), files.end(), s_file) == files.end()) {
            const std::wstring file = files.empty() ? std::wstring{} : files.front();
            s_file_timed = s_watching && !file.empty();
            s_file_start = std::chrono::steady_clock::now();
            s_file       = file;
            s_tags       = {};
            if (!file.empty()) n_media_file::read_tags(file, s_tags);
        }
        s_watching = true;

        std::wstring artist{}, title{};
        if (!s_file.empty()) {
            artist = s_tags.m_artist;
            title  = s_tags.m_title;
            if (title.empty()) split_title(s_file.substr(s_file.find_last_of(L"\\/") + 1), exe_stem(path), artist, title);
        } else
            split_title(window, exe_stem(path), artist, title);

        now_t now{};
        now.m_source  = path;
        now.m_kind    = source_app;
        now.m_title   = ToUtf8(title);
        now.m_artist  = ToUtf8(artist);
        now.m_playing = true;
        if (s_file_timed) {
            now.m_total_ms = s_tags.m_total_ms;
            now.m_stamp    = s_file_start;
        }
        now.m_thumbnail = thumbnail(s_file, [&] { return s_file.empty() ? std::vector<unsigned char>{} : n_media_file::read_cover(s_file); }, false);
        publish(now);
        return true;
    };

    /* playing beats paused, SMTC beats the rest. kopuz = own pipe api, its SMTC is broken.
       audible window ( vlc 3, winamp ) before anything paused */
    try {
        const auto session = pick_session(manager);
        if (session && is_playing(session)) return from_session(session);

        n_kopuz::state_t kopuz{};
        const bool has_kopuz = n_kopuz::get_state(kopuz);
        if (has_kopuz && kopuz.m_playing) return from_kopuz(kopuz);

        if (from_audio()) return;

        if (has_kopuz) return from_kopuz(kopuz);

        if (session) return from_session(session);

        clear();
    } catch (...) {
        clear();
    }
}

namespace
{
    bool session_command(const Manager& manager, const std::wstring& source, const std::wstring& title, const int command, bool& found)
    {
        // browsers share one id across tabs: the shown title picks the tab
        Session pick{ nullptr };
        for (const auto& session : manager.GetSessions()) {
            if (std::wstring_view{ session.SourceAppUserModelId() } != source) continue;

            const auto info = await_for(session.TryGetMediaPropertiesAsync());
            if (!pick || (info && std::wstring_view{ info.Title() } == title)) pick = session;
        }

        found = static_cast<bool>(pick);
        if (!pick) return false;

        const auto run = [](const auto& op) {
            if (op.wait_for(std::chrono::seconds(2)) == wf::AsyncStatus::Completed) return op.GetResults();
            op.Cancel();
            return false;
        };

        if (command == 1) return run(pick.TrySkipPreviousAsync());
        if (command == 3) return run(pick.TrySkipNextAsync());

        // some apps only expose play + pause, no toggle
        return run(pick.TryTogglePlayPauseAsync()) || run(is_playing(pick) ? pick.TryPauseAsync() : pick.TryPlayAsync());
    }

    // no SMTC ( vlc 3, winamp, mpc ): the player's own windows, the way a keyboard media key reaches them
    bool app_command(const std::wstring& path, const int command)
    {
        static constexpr short k_commands[]{ 0, APPCOMMAND_MEDIA_PREVIOUSTRACK, APPCOMMAND_MEDIA_PLAY_PAUSE, APPCOMMAND_MEDIA_NEXTTRACK };

        struct search_t {
            const std::wstring* m_path;
            std::vector<HWND> m_windows;
        } search{ &path, {} };

        EnumWindows(
            [](HWND window, LPARAM param) -> BOOL {
                auto& search = *reinterpret_cast<search_t*>(param);
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (IsWindowVisible(window) && !GetWindow(window, GW_OWNER) && pid != GetCurrentProcessId() && process_path(pid) == *search.m_path)
                    search.m_windows.push_back(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));

        for (const HWND window : search.m_windows) {
            DWORD_PTR handled = 0;
            if (SendMessageTimeoutW(window, WM_APPCOMMAND, reinterpret_cast<WPARAM>(window), MAKELPARAM(0, k_commands[command]), SMTO_ABORTIFHUNG, 500, &handled) &&
                handled)
                return true;
        }
        return false;
    }

    // last resort: a real media key, windows routes it to whatever answers media keys
    void media_key(const int command)
    {
        static constexpr WORD k_keys[]{ 0, VK_MEDIA_PREV_TRACK, VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK };

        INPUT input[2]{};
        for (int i = 0; i < 2; i++) {
            input[i].type       = INPUT_KEYBOARD;
            input[i].ki.wVk     = k_keys[command];
            input[i].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (i ? KEYEVENTF_KEYUP : 0);
        }
        SendInput(2, input, sizeof(INPUT));
    }
}

// botox impl_t::run_command. true = ran one
static bool RunCommand(const Manager& manager)
{
    const int command = s_pendingCmd.exchange(0);
    if (command < 1 || command > 3) return false;

    std::wstring source{}, title{};
    Source kind{};
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        source = s_source;
        kind   = s_kind;
        title  = winrt::to_hstring(s_title).c_str();
    }

    static constexpr const char* k_kopuz[]{ "", "Previous", "Toggle", "Next" };

    try {
        bool found = false;
        const bool sent =
            (kind == source_session && (session_command(manager, source, title, command, found) || found)) ||
            (kind == source_kopuz && !n_kopuz::detail::call(k_kopuz[command], {}).empty()) || (kind == source_app && app_command(source, command));
        if (!sent) media_key(command);
    } catch (...) {
        media_key(command);
    }

    if (kind == source_app) {
        s_appPaused = command == 2 && s_playing;
        if (s_appPaused) {
            const auto frozen = (long long)position_ms();
            std::lock_guard<std::mutex> lock(s_mtx);
            s_curMs     = frozen;
            s_stamp     = std::chrono::steady_clock::now();
            s_appPausedAt = s_stamp;
            s_playing   = false;
        }
    }

    return true;
}

// ==== visualizer thread: botox media_player.cpp visualizer_thread + loopback_t, 1:1 ===========
namespace
{
    struct activate_handler_t : IActivateAudioInterfaceCompletionHandler, IAgileObject {
        std::atomic<ULONG> m_refs{ 1 };
        HANDLE m_done          = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        IAudioClient* m_client = nullptr;

        virtual ~activate_handler_t()
        {
            safe_release(m_client);
            if (m_done) CloseHandle(m_done);
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
        {
            if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
                *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            else if (riid == __uuidof(IAgileObject))
                *out = static_cast<IAgileObject*>(this);
            else { *out = nullptr; return E_NOINTERFACE; }
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG refs = --m_refs;
            if (refs == 0) delete this;
            return refs;
        }
        HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override
        {
            HRESULT result  = E_FAIL;
            IUnknown* added = nullptr;
            if (SUCCEEDED(operation->GetActivateResult(&result, &added)) && SUCCEEDED(result) && added)
                added->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&m_client));
            safe_release(added);
            SetEvent(m_done);
            return S_OK;
        }
    };

    std::wstring window_aumid(HWND window)
    {
        static constexpr PROPERTYKEY k_aumid{ { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };   // PKEY_AppUserModel_ID
        IPropertyStore* store = nullptr;
        if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))) || !store) return {};
        std::wstring out{};
        PROPVARIANT value{};
        if (SUCCEEDED(store->GetValue(k_aumid, &value)) && value.vt == VT_LPWSTR && value.pwszVal) out = value.pwszVal;
        PropVariantClear(&value);
        store->Release();
        return out;
    }

    /* GSMTC names the app by AppUserModelId, process loopback wants a pid. match exe name ( "Chrome", "MSEdge", "Spotify.exe" ),
       packaged id ( "Pkg_hash!App" ), or a window's taskbar id ( firefox hash ). then climb same-exe parents to the root,
       its tree holds the browser's audio child. 0 = no match */
    DWORD media_pid(const std::wstring& aumid)
    {
        if (aumid.empty()) return 0;

        struct proc_t { DWORD m_parent; std::wstring m_exe; };
        std::unordered_map<DWORD, proc_t> procs{};

        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W entry{ sizeof(entry) };
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
            procs[entry.th32ProcessID] = { entry.th32ParentProcessID, exe_stem(entry.szExeFile) };
        CloseHandle(snapshot);

        const DWORD self = GetCurrentProcessId();

        // bounded: a dead parent's pid can be reused
        const auto root = [&](DWORD pid) {
            for (int depth = 0; pid && depth < 16; depth++) {
                const auto child  = procs.find(pid);
                const auto parent = child != procs.end() ? procs.find(child->second.m_parent) : procs.end();
                if (parent == procs.end() || parent->first == pid || parent->first == self || parent->second.m_exe != child->second.m_exe) break;
                pid = parent->first;
            }
            return pid;
        };

        const std::wstring want = lower(aumid);
        const std::wstring stem = exe_stem(aumid);

        // many chrome.exe: the root most of them hang off is the browser, stable across checks so the stream doesn't flap
        std::unordered_map<DWORD, int> roots{};
        for (const auto& [pid, proc] : procs)
            if (pid && pid != self && proc.m_exe == stem) roots[root(pid)]++;

        DWORD found = 0;
        int best = 0;
        for (const auto& [pid, count] : roots)
            if (count > best || (count == best && pid < found)) { found = pid; best = count; }
        if (found) return found;

        if (want.find(L'!') != std::wstring::npos) {
            for (const auto& [pid, proc] : procs) {
                if (!pid || pid == self) continue;
                const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (!process) continue;
                wchar_t id[APPLICATION_USER_MODEL_ID_MAX_LENGTH]{};
                UINT32 size = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
                const bool match = GetApplicationUserModelId(process, &size, id) == ERROR_SUCCESS && lower(id) == want;
                CloseHandle(process);
                if (match) { found = pid; break; }
            }
        }

        if (!found) {
            struct search_t { const std::wstring* m_want; DWORD m_pid; } search{ &want, 0 };
            EnumWindows(
                [](HWND window, LPARAM param) -> BOOL {
                    auto& search = *reinterpret_cast<search_t*>(param);
                    // own windows skipped: a property query on this process's window is a SendMessage to the game thread
                    DWORD pid = 0;
                    GetWindowThreadProcessId(window, &pid);
                    if (pid == GetCurrentProcessId() || !IsWindowVisible(window) || lower(window_aumid(window)) != *search.m_want) return TRUE;
                    search.m_pid = pid;
                    return FALSE;
                },
                reinterpret_cast<LPARAM>(&search));
            found = search.m_pid == self ? 0 : search.m_pid;
        }

        return root(found);
    }

    /* process loopback ( win10 20348+ / win11 ): only the media app's process tree, or ( app not found ) everything but this game's tree.
       the virtual device has no mix format: ask for 48k stereo pcm16 and let AUTOCONVERTPCM resample */
    struct loopback_t {
        IMMDeviceEnumerator* m_enumerator = nullptr;
        IAudioClient* m_client            = nullptr;
        IAudioCaptureClient* m_capture    = nullptr;
        HANDLE m_event                    = nullptr;   // EVENTCALLBACK wants one, the thread still polls
        WAVEFORMATEX m_format{ WAVE_FORMAT_PCM, 2, 48000, 48000 * 4, 4, 16, 0 };
        std::wstring m_device_id{};
        std::wstring m_source{};
        DWORD m_target = 0;   // 0 = exclude this game

        void close()
        {
            if (m_client) m_client->Stop();
            safe_release(m_capture);
            safe_release(m_client);
            if (m_event) { CloseHandle(m_event); m_event = nullptr; }
            m_device_id.clear();
            m_source.clear();
            m_target = 0;
        }

        std::wstring default_id()
        {
            IMMDevice* device = nullptr;
            if (!m_enumerator || FAILED(m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || !device) return {};
            std::wstring out{};
            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id)) && id) { out = id; CoTaskMemFree(id); }
            device->Release();
            return out;
        }

        bool open(const std::wstring& source, const DWORD target)
        {
            close();
            if (!m_enumerator && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                                         reinterpret_cast<void**>(&m_enumerator))))
                return false;

            // remembered only to reopen when the default output / media app switches ( thread checks every 2 s )
            m_device_id = default_id();
            m_source    = source;
            m_target    = target;

            AUDIOCLIENT_ACTIVATION_PARAMS params{};
            params.ActivationType                            = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            params.ProcessLoopbackParams.TargetProcessId     = target ? target : GetCurrentProcessId();
            params.ProcessLoopbackParams.ProcessLoopbackMode = target ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;

            PROPVARIANT activation{};
            activation.vt             = VT_BLOB;
            activation.blob.cbSize    = sizeof(params);
            activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

            auto* handler = new activate_handler_t{};
            IActivateAudioInterfaceAsyncOperation* pending = nullptr;
            if (handler->m_done &&
                SUCCEEDED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activation, handler, &pending)) &&
                WaitForSingleObject(handler->m_done, 5000) == WAIT_OBJECT_0) {
                m_client = handler->m_client;
                handler->m_client = nullptr;
            }
            safe_release(pending);
            handler->Release();

            m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

            constexpr DWORD k_flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
            if (!m_client || !m_event ||
                FAILED(m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, k_flags, 2000000, 0, &m_format, nullptr)) ||
                FAILED(m_client->SetEventHandle(m_event)) ||
                FAILED(m_client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&m_capture))) ||
                FAILED(m_client->Start())) {
                close();
                return false;
            }
            return true;
        }

        static float sample(const BYTE* frame)
        {
            const auto* pcm = reinterpret_cast<const short*>(frame);
            return ((float)pcm[0] + (float)pcm[1]) / 65536.f;
        }
    };

    std::wstring shown_source()   // botox get_source_app
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        return s_source;
    }

    // botox main.cpp visualizer_thread's `wanted`
    bool visualizer_wanted() { return g_mpEnable && g_mpVisualizer && !g_mpSimple; }
}

static DWORD WINAPI VisualizerThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    using steady = std::chrono::steady_clock;
    constexpr std::size_t k_size = n_audio_bands::k_fft_size;

    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    loopback_t loopback{};
    static std::array<float, k_size> ring{}, ordered{};   // 16 KB, off the thread's stack
    std::size_t write = 0;
    float fresh[n_audio_bands::k_bands]{};
    float levels[n_audio_bands::k_bands]{};
    bool logged = false;

    auto last_step = steady::now(), last_audio = steady::now(), last_device = steady::now();

    const auto publish = [&]() {
        std::lock_guard<std::mutex> lock(s_bandsMtx);
        std::copy(std::begin(levels), std::end(levels), std::begin(s_bands));
    };

    while (!g_eject) {
        const std::wstring source = shown_source();

        if (!visualizer_wanted() || source.empty()) {
            loopback.close();
            std::fill(std::begin(levels), std::end(levels), 0.f);
            publish();
            Sleep(100);
            last_step = steady::now();
            continue;
        }

        if (!loopback.m_client) {
            const DWORD pid = media_pid(source);
            if (!loopback.open(source, pid)) {
                if (!logged) { DebugLog("media: waveform loopback open failed (needs win10 20348+ / win11)"); logged = true; }
                Sleep(1000);
                continue;
            }
            DebugLog("media: waveform loopback open, target pid %lu (%s)", pid, pid ? "app tree" : "all but the game");
            last_device = steady::now();
        }

        const auto now = steady::now();

        if (source != loopback.m_source) { loopback.close(); continue; }

        if (now - last_device > std::chrono::seconds(2)) {
            last_device = now;
            if (loopback.default_id() != loopback.m_device_id || media_pid(source) != loopback.m_target) { loopback.close(); continue; }
        }

        UINT32 packet = 0;
        HRESULT result = loopback.m_capture->GetNextPacketSize(&packet);
        while (SUCCEEDED(result) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 count = 0;
            DWORD flags = 0;
            result = loopback.m_capture->GetBuffer(&data, &count, &flags, nullptr, nullptr);
            if (FAILED(result)) break;

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data;
            for (UINT32 frame = 0; frame < count; frame++) {
                ring[write] = silent ? 0.f : loopback_t::sample(data + frame * loopback.m_format.nBlockAlign);
                write = (write + 1) % k_size;
            }
            loopback.m_capture->ReleaseBuffer(count);
            last_audio = now;
            result = loopback.m_capture->GetNextPacketSize(&packet);
        }

        if (FAILED(result)) { loopback.close(); Sleep(250); continue; }

        if (now - last_audio > std::chrono::milliseconds(100)) ring.fill(0.f);

        for (std::size_t i = 0; i < k_size; i++) ordered[i] = ring[(write + i) % k_size];
        n_audio_bands::compute(ordered.data(), (float)loopback.m_format.nSamplesPerSec, fresh);

        const float dt = std::chrono::duration<float>(now - last_step).count();
        last_step = now;
        for (std::size_t band = 0; band < n_audio_bands::k_bands; band++)
            levels[band] = (std::max)(fresh[band], levels[band] - 2.5f * dt);

        publish();
        Sleep(15);
    }

    loopback.close();
    safe_release(loopback.m_enumerator);
    if (com) CoUninitialize();
    return 0;
}

// ==== lyrics thread: botox lrclib_get / fetch_lyrics / update_lyrics + main.cpp lyrics_thread ====
// own thread: the fetch blocks up to its http timeouts on every track change, on the media thread
// that froze title, art and every control (botox's note)
static std::string LrclibGet(const std::wstring& path)
{
    std::string out{};
    HINTERNET session = WinHttpOpen(L"botox", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return out;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

    HINTERNET connection = WinHttpConnect(session, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        WINHTTP_FLAG_SECURE)
                                   : nullptr;
    {
        std::lock_guard<std::mutex> lock(s_httpMtx);
        if (g_eject && request) { WinHttpCloseHandle(request); request = nullptr; }
        s_httpReq = request;
    }

    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, status_size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) &&
            status == 200) {
            char buffer[8192]{};
            DWORD read = 0;
            while (WinHttpReadData(request, buffer, sizeof(buffer), &read) && read && out.size() < 1024u * 1024u) out.append(buffer, read);
        }
    }

    {
        // eject may have closed it already (MediaPlayer_Shutdown): never twice
        std::lock_guard<std::mutex> lock(s_httpMtx);
        if (s_httpReq == request && request) WinHttpCloseHandle(request);
        s_httpReq = nullptr;
    }
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return out;
}

static std::vector<mpl::Line> FetchLyrics(const std::string& title, const std::string& artist, long long totalMs)
{
    const std::wstring base = L"/api/get?artist_name=" + mpl::UrlEncode(artist) + L"&track_name=" + mpl::UrlEncode(title);
    std::string body{};
    if (totalMs > 0) body = LrclibGet(base + L"&duration=" + std::to_wstring((totalMs + 500) / 1000));
    if (body.empty() && !g_eject) body = LrclibGet(base);

    std::string synced;
    if (!mpl::JsonStringField(body, "syncedLyrics", synced)) return {};
    return mpl::ParseLrc(synced);
}

static void UpdateLyrics(bool wanted)
{
    std::string title{}, artist{};
    long long totalMs = 0;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        title = s_title; artist = s_artist; totalMs = s_totalMs;
    }

    // no timeline = position unknown, synced lines would be wrong
    const std::string key = wanted && s_has.load() && !title.empty() && totalMs > 0 ? title + '\n' + artist : std::string{};
    if (key == s_lyricsKey) return;
    s_lyricsKey = key;

    {
        std::lock_guard<std::mutex> lock(s_lyricsMtx);
        s_lyrics.clear();
        ++s_lyricsGen;
    }
    if (key.empty()) return;

    auto lines = FetchLyrics(title, artist, totalMs);
    DebugLog("media: lyrics %zu synced lines", lines.size());

    std::lock_guard<std::mutex> lock(s_lyricsMtx);
    s_lyrics = std::move(lines);
    ++s_lyricsGen;
}

static DWORD WINAPI LyricsThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    while (!g_eject) {
        UpdateLyrics(g_mpEnable && g_mpLyrics);
        Sleep(250);
    }
    return 0;
}

// botox main.cpp media_player_thread
static DWORD WINAPI MediaThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    /* fresh thread, so it has no apartment yet - winrt throws CO_E_NOTINITIALIZED without this */
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}

    std::optional<Manager> manager;
    try { manager = Manager::RequestAsync().get(); } catch (...) {}
    if (!manager.has_value()) {
        DebugLog("media: failed to initialise media player");
        return 0;
    }

    while (!g_eject) {
        if (g_mpEnable || (g_clantag && g_clantagAnim == 4)) OnUpdate(*manager);   // botox main.cpp:325

        // a control click refreshes 250 ms later, the app needs that long to switch track
        for (int i = 0; i < 20 && !g_eject; i++) {
            if (RunCommand(*manager)) {
                Sleep(250);
                break;
            }
            Sleep(50);
        }
    }

    // eject: drop the proxy on its own MTA thread while the process is healthy
    try { manager.reset(); } catch (...) {}
    return 0;
}

void MediaPlayer_Start()
{
    if (s_thread) return;
    s_thread       = CreatePinnedThread([] { MediaThread(nullptr); });
    s_visThread    = CreatePinnedThread([] { VisualizerThread(nullptr); });
    s_lyricsThread = CreatePinnedThread([] { LyricsThread(nullptr); });
}

void MediaPlayer_Shutdown()
{
    // a lyrics request in flight: closing it fails its receive now instead of after 10 s
    {
        std::lock_guard<std::mutex> lock(s_httpMtx);
        if (s_httpReq) { WinHttpCloseHandle(s_httpReq); s_httpReq = nullptr; }
    }
    // The loops test g_eject at most 250ms apart, but a winrt / WASAPI activation already in
    // flight can hold one longer -- the loopback activation waits 5 s at most.
    for (HANDLE* t : { &s_thread, &s_visThread, &s_lyricsThread }) {
        if (!*t) continue;
        WaitForSingleObject(*t, t == &s_visThread ? 6000 : 3000);
        CloseHandle(*t);
        *t = nullptr;
    }
    MediaPlayer_ReleaseTexture();
}

// ==== render thread =============================================================================
void MediaPlayer_ReleaseTexture()
{
    if (s_tex) { s_tex->Release(); s_tex = nullptr; }
    // The decoded pixels outlive the texture, so a device change rebuilds from them rather than
    // waiting for the next track.
    std::lock_guard<std::mutex> lock(s_mtx);
    if (!s_px.empty()) s_pxDirty = true;
}

void MediaPlayer_Tick(IDirect3DDevice9* dev)
{
    if (!dev || !g_mpEnable) return;

    std::vector<unsigned char> px;
    int w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (!s_pxDirty) return;
        s_pxDirty = false;
        px = s_px;            // copy: LockRect below must not hold the poll thread out
        w = s_pxW; h = s_pxH;
    }

    if (s_tex) { s_tex->Release(); s_tex = nullptr; }
    if (px.empty() || w <= 0 || h <= 0) return;

    // DYNAMIC + DEFAULT, the exact pair imgui_impl_dx9 uses for its own font atlas -- and that
    // atlas is proof the combination binds in both passes (the vgui one draws every string we
    // have). MANAGED was the bug: the engine's device is D3D9Ex, where the managed pool does
    // not exist, so CreateTexture just failed and the art was silently never there. ref
    // and fpsoverlay both hit it and both moved off MANAGED.
    // DEFAULT dies on a device Reset, so h_Reset releases it and the pixels above rebuild it.
    HRESULT hr = dev->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &s_tex, nullptr);
    if (FAILED(hr)) { DebugLog("media: CreateTexture %dx%d failed hr=0x%08X", w, h, (unsigned)hr); s_tex = nullptr; return; }

    D3DLOCKED_RECT lr{};
    hr = s_tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD);
    if (FAILED(hr)) { DebugLog("media: LockRect failed hr=0x%08X", (unsigned)hr); s_tex->Release(); s_tex = nullptr; return; }
    for (int y = 0; y < h; ++y)
        memcpy((unsigned char*)lr.pBits + (size_t)y * lr.Pitch, px.data() + (size_t)y * w * 4, (size_t)w * 4);
    s_tex->UnlockRect(0);
}

// ==== botox misc.cpp draw_media_player ==========================================================
// The poll runs once a second; age the stored position by the time since it so the bar slides
// instead of stepping. Same function the poll uses, with the track already resolved.
static mp::Track CurrentTrack()
{
    std::lock_guard<std::mutex> lock(s_mtx);
    const long long ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - s_stamp).count();
    return mp::TrackAt(0, s_totalMs, 0, 0, s_curMs, ageMs, s_rate, s_playing.load());
}

// g_utilities.truncate_utf8: cut after N codepoints, never inside one
static std::string TruncateUtf8(const std::string& s, size_t maxChars)
{
    size_t chars = 0, i = 0;
    for (; i < s.size(); ++i) {
        if (((unsigned char)s[i] & 0xc0) != 0x80 && chars++ == maxChars) break;
    }
    return s.substr(0, i);
}

// botox media_time_text: "4:11 / 6:09", hours once a track runs past one. empty = length unknown
static std::string TimeText(const mp::Track& t)
{
    if (t.totalMs <= 0) return {};
    const auto format = [](long long ms) {
        const long long s = ms / 1000;
        char out[32];
        if (s >= 3600) snprintf(out, sizeof(out), "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
        else           snprintf(out, sizeof(out), "%lld:%02lld", s / 60, s % 60);
        return std::string(out);
    };
    return format(std::clamp(t.curMs, 0LL, t.totalMs)) + " / " + format(t.totalMs);
}

// botox media_player_fonts: title (bold) / artist (regular) face per watermark style, plus the
// draw size where the donor's atlas px and draw px differ (interium 24 -> 16, illusory 13 ->
// 12, gamesense 15 -> 13). Default = the menu's verdana bold 11 / verdana 11.
static void StyleFonts(int style, ImFont*& title, ImFont*& artist, float& size)
{
    title = artist = nullptr; size = 0.f;
    const auto pick = [&](ImFont* b, ImFont* r) { title = b; artist = r; };
    switch (style) {
    case 1:  pick(g_fontKamibebraBold13, g_fontKamibebra13); break;
    case 3:  pick(g_fontClarityInter, g_fontClarityInter); break;
    case 4:
    case 7:  pick(g_fontTahoma12, g_fontTahoma12); break;
    case 5:  pick(g_fontInterwebzCalibri, g_fontInterwebzCalibri); break;
    case 6:  pick(g_fontHavocTahoma13, g_fontHavocTahoma13); break;
    case 8:  pick(g_fontAirflow14, g_fontAirflow14); break;
    case 9:  pick(g_fontEvolveBold16, g_fontEvolve16); break;
    case 10: pick(g_fontLegendwareVerdana12, g_fontLegendwareVerdana12); break;
    case 11: pick(g_fontInteriumDroid24, g_fontInteriumDroid24); size = 16.f; break;
    case 12: pick(g_fontSkebobArial12, g_fontSkebobArial12); break;
    case 13: pick(g_fontCumidereTahoma16, g_fontCumidereTahoma16); break;
    case 14: pick(g_fontIllusoryTahoma13, g_fontIllusoryTahoma13); size = 12.f; break;
    case 15: pick(g_fontLumiRubik16, g_fontLumiRubik16); break;
    case 16: pick(g_fontTahomaBd12, g_fontTahomaBd12); break;
    case 17: pick(g_fontDnaMontserrat15, g_fontDnaMontserrat15); break;
    case 18: pick(g_fontDnaPtRootBold15, g_fontDnaPtRootBold15); break;
    case 19: pick(g_fontCucumberTahoma14, g_fontCucumberTahoma14); break;
    case 20: pick(g_fontHowewewareMinecraft14, g_fontHowewewareMinecraft14); break;
    case 21: pick(g_fontGamesenseSfBold15, g_fontGamesenseSfBold15); size = 13.f; break;
    case 22: pick(g_fontArialBd16, g_fontArialBd16); break;   // boxed music display face
    default: break;
    }
    if (!title || !artist) {
        pick(g_menuBold ? g_menuBold : ImGui::GetFont(), g_menuReg ? g_menuReg : ImGui::GetFont());
        size = 0.f;
    }
}

// botox media_player_binds: fires on press. A tap shorter than one frame only shows in the
// key's "pressed since last ask" bit, which stands in for botox's release counter.
static void MediaBinds()
{
    struct Bind { int key; bool fired; };
    static Bind binds[3]{};
    const int keys[3] = { g_mpPrevKey, g_mpToggleKey, g_mpNextKey };
    static constexpr int cmds[3] = { 1, 2, 3 };   // botox e_command: previous, toggle, next

    // botox keys_blocked: menu open, or the game is not the focused window; the console being up
    // too (IVEngineClient::Con_IsVisible, slot 11 -- GetPlayerInfo 8 / GetLocalPlayer 12 bracket it)
    // asked only when a bind fires: the engine getter stays off the render thread's hot path
    const auto blocked = []() { return g_showMenu || (g_hwnd && GetForegroundWindow() != g_hwnd) || (g_engine && VCall<bool>(g_engine, 11)); };

    for (int i = 0; i < 3; i++) {
        Bind& bind = binds[i];
        const int key = keys[i];
        // media keys already reach the player through windows, firing too would skip twice
        if (!g_mpIngameControl || key <= 0 || key > 255 || (key >= VK_MEDIA_NEXT_TRACK && key <= VK_MEDIA_PLAY_PAUSE)) { bind.key = 0; continue; }

        const SHORT state = GetAsyncKeyState(key);
        const bool down = (state & 0x8000) != 0, tapped = (state & 1) != 0;
        if (bind.key != key) { bind = { key, down }; continue; }

        const bool fire = !bind.fired && (down || tapped);
        bind.fired = down;
        if (fire && !blocked()) s_pendingCmd = cmds[i];
    }
}

void RenderMediaPlayer()
{
    if (!g_mpEnable) return;
    MediaBinds();
    if (!s_has.load()) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f) return;

    std::string title, artist;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        title = s_title; artist = s_artist;
    }
    title  = TruncateUtf8(title, 64);
    artist = TruncateUtf8(artist, 64);
    if (title.empty()) return;

    const int style = g_wmStyle;
    ImFont* titleFont; ImFont* artistFont; float sizeOverride;
    StyleFonts(style, titleFont, artistFont, sizeOverride);
    const float titlePx  = sizeOverride > 0.f ? sizeOverride : titleFont->FontSize;
    const float artistPx = sizeOverride > 0.f ? sizeOverride : artistFont->FontSize;

    // botox position 0..5 = tr/tl/tc/bc/bl/br -> align (0 left, 1 centre, 2 right) + from_bottom.
    // Ours is the WatermarkPos enum, same six corners.
    const bool fromBottom = g_mpPos >= WM_BOTTOM_LEFT;
    const int align = (g_mpPos == WM_TOP_LEFT || g_mpPos == WM_BOTTOM_LEFT) ? 0
                    : (g_mpPos == WM_TOP_CENTER || g_mpPos == WM_BOTTOM_CENTER) ? 1 : 2;
    const float screenW = io.DisplaySize.x, screenH = io.DisplaySize.y, midX = screenW * 0.5f;
    const ImVec4 wm = g_wmBox;
    const bool wmColumn = wm.z > wm.x && wm.y < screenH * 0.5f &&
                          (align == 2 ? wm.z > midX : align == 0 ? wm.x < midX : wm.x < midX && wm.z > midX);
    const bool underWm = !fromBottom && wmColumn;
    // botox lumi = 10 px edge, 2 px under the watermark (lumi.dll default 10 / 37); normal = 8 / 4
    const float margin = g_mpLumi ? 10.f : 8.f;
    const float anchorX = align == 1 ? midX : align == 2 ? (underWm ? wm.z : screenW - margin) : (underWm ? wm.x : margin);
    const float anchorY = fromBottom ? screenH - margin : underWm ? wm.w + (g_mpLumi ? 2.f : 4.f) : margin;
    const auto leftOf = [&](float w) { return align == 0 ? anchorX : align == 1 ? anchorX - w * 0.5f : anchorX - w; };

    const MediaLook look  = Watermark_MediaLook(style);
    const bool background = g_mpBackground;
    const int frameStyle  = look.box ? style : 0;
    const int bareFx      = look.fx == 2 || look.fx == 3 ? look.fx : 1;
    const int fx          = background ? look.fx : bareFx;

    static constexpr ImVec2 outline[8] = { { -1.f, -1.f }, { 0.f, -1.f }, { 1.f, -1.f }, { -1.f, 0.f },
                                           { 1.f, 0.f },   { -1.f, 1.f }, { 0.f, 1.f },  { 1.f, 1.f } };
    const auto put = [](ImDrawList* list, int effect, ImFont* font, float px, ImVec2 pos, ImU32 color, const std::string& text) {
        const ImU32 black = color & IM_COL32_A_MASK;
        if (effect == 1)      list->AddText(font, px, ImVec2(pos.x + 1.f, pos.y + 1.f), black, text.c_str());
        else if (effect == 3) list->AddText(font, px, ImVec2(pos.x - 1.f, pos.y + 1.f), black, text.c_str());
        else if (effect == 2) for (const auto& o : outline) list->AddText(font, px, ImVec2(pos.x + o.x, pos.y + o.y), black, text.c_str());
        list->AddText(font, px, pos, color, text.c_str());
    };

    constexpr float artSize = 30.f, barHeight = 1.f, rowGap = 4.f;

    // botox draw_lyrics: `rows` lines centred on the one playing, the rest fading out by distance,
    // scrolled + resized with a 10/s ease, in its own frame when the player has one
    const auto drawLyrics = [&](ImDrawList* list, float edge, float playerW, bool boxed, int effect, ImRect& bounds) {
        static std::vector<mpl::Line> lines{};
        static int generation = -1;
        static float scroll = 0.f, boxW = 0.f;

        int fresh;
        {
            std::lock_guard<std::mutex> lock(s_lyricsMtx);
            if (generation != s_lyricsGen) lines = s_lyrics;
            fresh = s_lyricsGen;
        }
        const bool snap = fresh != generation;
        generation = fresh;
        if (!g_mpLyrics || lines.empty()) return;

        const double now = position_ms() + g_mpLyricsOffset;
        const int rows = std::clamp(g_mpLyricsRows, 1, 9);
        int active, first, shown;
        mpl::Window(lines, now, rows, active, first, shown);
        const int count = (int)lines.size();

        const float pad  = boxed ? 10.f : 0.f;
        const float step = artistPx + rowGap;
        const float fadeStep = 0.55f / (float)(std::max)(1, rows / 2);
        const auto textAt = [&](int k) { return TruncateUtf8(lines[k].text, 96); };

        float targetW = playerW;
        for (int k = first; k < first + shown; k++)
            targetW = (std::max)(targetW, artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, textAt(k).c_str()).x + pad * 2.f);

        const float ease = 1.f - expf(-io.DeltaTime * 10.f);
        scroll = snap ? (float)first : scroll + ((float)first - scroll) * ease;
        boxW   = snap || boxW <= 0.f ? targetW : boxW + (targetW - boxW) * ease;

        const float boxH = pad * 2.f + (float)shown * artistPx + (float)(shown - 1) * rowGap;
        const float top  = fromBottom ? edge - boxH : edge;
        const ImVec2 mn(leftOf(boxW), top), mx(mn.x + boxW, top + boxH);
        bounds.Add(ImRect(mn, mx));
        if (boxed) Watermark_MediaFrame(list, mn, mx, frameStyle);

        list->PushClipRect(mn, mx, true);
        for (int k = (std::max)(0, first - 1); k <= (std::min)(count - 1, first + shown); k++) {
            const std::string text = textAt(k);
            if (text.empty()) continue;
            const float row  = (float)k - scroll;
            const float rim  = std::clamp((std::min)(row + 1.f, (float)shown - row), 0.f, 1.f);
            const float fade = (std::max)(0.f, 1.f - (float)std::abs(k - active) * fadeStep) * rim;
            const ImU32 alpha = (ImU32)((float)((look.title >> IM_COL32_A_SHIFT) & 0xFF) * fade);
            if (!alpha) continue;
            const float textW = artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, text.c_str()).x;
            const float textX = align == 0 ? mn.x + pad : align == 1 ? floorf((mn.x + mx.x - textW) * 0.5f) : mx.x - pad - textW;
            put(list, effect, artistFont, artistPx, ImVec2(textX, mn.y + pad + row * step), (look.title & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT), text);
        }
        list->PopClipRect();
    };

    // prev / play-pause / next while the menu is open. drawn last: the hit test needs the whole
    // block's bounds, dpi panel scale pivots on them (same mapping as the spectator list drag)
    const mp::Track track   = CurrentTrack();
    const bool buttons      = g_showMenu;
    const std::string timeNow = g_mpTime ? TimeText(track) : std::string{};
    const float icon        = std::clamp(floorf(artistPx * 0.7f), 7.f, 14.f);
    // lumi: 22 x 16 boxes, 30 pitch
    const float cell        = g_mpLumi ? 22.f : floorf(icon * 1.6f), cellGap = g_mpLumi ? 8.f : floorf(icon * 0.4f);
    const float buttonsW    = buttons ? cell * 3.f + cellGap * 2.f : 0.f;
    const ImU32 accent      = ImGui::GetColorU32(ImGuiCol_Accent);

    const auto drawButtons = [&](ImDrawList* list, float x, float midY, int effect, const ImRect& bounds) {
        const ImVec2 at = Dpi_PanelMouse(bounds.Min, bounds.GetSize());
        const bool live  = !GImGui->HoveredWindow;   // a menu window over the player eats the click
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const ImU32 hoverColor = look.title != look.artist ? look.title : accent;
        const float h = icon * 0.5f;
        static constexpr int cmds[3] = { 2, 0, 1 };   // MediaPlayer_RequestControl: 0 toggle, 1 next, 2 previous

        for (int b = 0; b < 3; b++) {
            const float cx = x + (float)b * (cell + cellGap) + cell * 0.5f;
            const bool hovered = live && fabsf(at.x - cx) <= cell * 0.5f + 1.f && fabsf(at.y - midY) <= cell * 0.5f + 1.f;
            if (hovered && click) MediaPlayer_RequestControl(cmds[b]);

            // clockwise on screen, the aa fringe goes outward
            const auto shape = [&](float sx, float sy, ImU32 color) {
                if (b == 0) {
                    list->AddTriangleFilled(ImVec2(sx - h, sy), ImVec2(sx, sy - h), ImVec2(sx, sy + h), color);
                    list->AddTriangleFilled(ImVec2(sx, sy), ImVec2(sx + h, sy - h), ImVec2(sx + h, sy + h), color);
                } else if (b == 2) {
                    list->AddTriangleFilled(ImVec2(sx + h, sy), ImVec2(sx, sy + h), ImVec2(sx, sy - h), color);
                    list->AddTriangleFilled(ImVec2(sx, sy), ImVec2(sx - h, sy + h), ImVec2(sx - h, sy - h), color);
                } else if (s_playing.load()) {
                    list->AddRectFilled(ImVec2(sx - h * 0.8f, sy - h), ImVec2(sx - h * 0.2f, sy + h), color);
                    list->AddRectFilled(ImVec2(sx + h * 0.2f, sy - h), ImVec2(sx + h * 0.8f, sy + h), color);
                } else
                    list->AddTriangleFilled(ImVec2(sx - h * 0.7f, sy - h), ImVec2(sx + h * 0.9f, sy), ImVec2(sx - h * 0.7f, sy + h), color);
            };

            const ImU32 color = hovered ? hoverColor : look.artist;
            if (effect) shape(cx + (effect == 3 ? -1.f : 1.f), midY + 1.f, color & IM_COL32_A_MASK);
            shape(cx, midY, color);
        }
    };

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    DpiPanelScope block(dl);   // botox begin_stretch_block: the player is one hud panel

    // botox misc.cpp draw_media_player lumi branch (lumi.dll FUN_100b87a0 geometry), 1:1.
    // one combined "artist - title [ time]" line either bare + icon, or in a fixed 172-wide box.
    if (g_mpLumi) {
        std::string line = artist.empty() ? title : artist + " - " + title;
        if (!timeNow.empty()) line += "  " + timeNow;
        const ImVec2 lineSize = artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, line.c_str());
        const bool progressBar = g_mpProgressBar;
        const bool art = g_mpCoverArt && s_tex;
        const float progress = mp::ProgressOf(track);

        if (g_mpSimple) {
            // 14 px icon (lumi: spotify png, here the cover) + 20, one text line, 2 px progress under the line
            const float iconW = art ? 20.f : 0.f;
            const float lineW = iconW + lineSize.x;
            const float totalW = lineW + (buttons ? cellGap * 2.f + buttonsW : 0.f);
            const float h = lineSize.y + (progressBar ? 4.f : 0.f);
            const float x = floorf(leftOf(totalW)), y = fromBottom ? anchorY - h : anchorY;

            if (art) {
                const float iy = floorf(y + (lineSize.y - 14.f) * 0.5f);
                dl->AddImage((ImTextureID)s_tex, ImVec2(x + 1.f, iy + 1.f), ImVec2(x + 15.f, iy + 15.f), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(0, 0, 0, 128));
                dl->AddImage((ImTextureID)s_tex, ImVec2(x, iy), ImVec2(x + 14.f, iy + 14.f));
            }
            put(dl, bareFx, artistFont, artistPx, ImVec2(x + iconW, y), look.title, line);

            if (progressBar) {
                const float by = y + lineSize.y + 2.f;
                dl->AddRectFilled(ImVec2(x, by), ImVec2(x + lineW, by + 2.f), IM_COL32(60, 60, 60, 255));
                dl->AddRectFilled(ImVec2(x, by), ImVec2(x + lineW * progress, by + 2.f), accent);
            }

            ImRect bounds(ImVec2(x, y), ImVec2(x + totalW, y + h));
            drawLyrics(dl, fromBottom ? y - rowGap : y + h + rowGap, totalW, false, bareFx, bounds);
            if (buttons) drawButtons(dl, x + lineW + cellGap * 2.f, floorf(y + lineSize.y * 0.5f), bareFx, bounds);
            return;
        }

        // box 172 wide: 8 pad, 36 cover, 8, 22 bars 3 wide on a 5 pitch (112), 8. text under the
        // cover, 22 x 16 buttons centred. marquee once wider than the box.
        constexpr float cover = 36.f, width = 172.f, columnW = 112.f;
        const float height = artistPx + 12.f + cover + 8.f + (buttons ? 24.f : 0.f);
        const float x = floorf(leftOf(width)), y = fromBottom ? anchorY - height : anchorY;
        const float colX = x + 8.f + cover + 8.f;

        if (background) Watermark_MediaFrame(dl, ImVec2(x, y), ImVec2(x + width, y + height), frameStyle);

        const ImVec2 c0(x + 8.f, y + 8.f), c1(c0.x + cover, c0.y + cover);
        dl->AddRectFilled(c0, c1, IM_COL32(30, 30, 30, 255), 3.f);
        dl->AddRect(c0, c1, IM_COL32(0, 0, 0, 255), 3.f, 0, 1.f);
        if (art) dl->AddImageRounded((ImTextureID)s_tex, c0, c1, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE, 3.f);

        if (g_mpVisualizer) {
            float bands[n_audio_bands::k_bands]{};
            { std::lock_guard<std::mutex> lock(s_bandsMtx); std::copy(std::begin(s_bands), std::end(s_bands), bands); }
            const ImU32 rgb = g_mpVisCustomColor ? (ImU32)ImColor(g_mpVisColor[0], g_mpVisColor[1], g_mpVisColor[2], g_mpVisColor[3]) : accent;
            const ImU32 top = (rgb & ~IM_COL32_A_MASK) | (45u << IM_COL32_A_SHIFT), bottom = rgb | IM_COL32_A_MASK;
            const float base = y + 8.f + 31.f;
            for (int bar = 0; bar < 22; bar++) {
                const float h = std::clamp(n_audio_bands::bar_level(bands, (std::size_t)bar, 22), 0.f, 1.f) * 27.f + 1.f;
                const float bx = colX + (float)bar * 5.f;
                dl->AddRectFilledMultiColor(ImVec2(bx, base - h), ImVec2(bx + 3.f, base), top, top, bottom, bottom);
            }
        }

        if (progressBar) {
            static float eased = 0.f;
            if (progress < eased) eased = progress;
            eased += (progress - eased) * 0.1f;
            const ImVec2 p0(colX, y + 8.f + 34.f), p1(colX + columnW, p0.y + 3.f);
            dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 18, 255), 1.f);
            dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 255), 1.f, 0, 1.f);
            if (eased > 0.f) dl->AddRectFilled(p0, ImVec2(colX + columnW * eased, p1.y), accent, 1.f);
        }

        // marquee once wider than the box: scrolls 30 px/s over text + 40, drawn twice so the wrap doesn't jump
        const float textY = c1.y + 4.f, clipL = x + 8.f, clipR = x + width - 8.f;
        dl->PushClipRect(ImVec2(clipL, textY), ImVec2(clipR, textY + artistPx + 2.f), true);
        if (lineSize.x <= clipR - clipL)
            put(dl, fx, artistFont, artistPx, ImVec2(clipL, textY), look.title, line);
        else {
            const float span = ceilf(lineSize.x) + 40.f;
            const float off  = fmodf((float)(ImGui::GetTime() * 30.0), span);
            const ImVec4 shown = dl->_CmdHeader.ClipRect;
            dl->PushClipRect(ImVec2(clipL - 4.f, shown.y), ImVec2(clipL + span + 4.f, shown.w), false);
            const ImVec4 wide = dl->_CmdHeader.ClipRect;
            const int cmd0 = dl->CmdBuffer.Size - 1, vtx0 = dl->VtxBuffer.Size;
            put(dl, fx, artistFont, artistPx, ImVec2(clipL, textY), look.title, line);
            const int vtx1 = dl->VtxBuffer.Size;
            put(dl, fx, artistFont, artistPx, ImVec2(clipL, textY), look.title, line);
            for (int v = vtx0; v < dl->VtxBuffer.Size; v++)
                dl->VtxBuffer[v].pos.x += (v >= vtx1 ? span : 0.f) - off;
            for (int c = cmd0; c < dl->CmdBuffer.Size; c++)
                if (memcmp(&dl->CmdBuffer[c].ClipRect, &wide, sizeof(ImVec4)) == 0)
                    dl->CmdBuffer[c].ClipRect = shown;
            dl->PopClipRect();
        }
        dl->PopClipRect();

        ImRect bounds(ImVec2(x, y), ImVec2(x + width, y + height));
        drawLyrics(dl, fromBottom ? y - rowGap : y + height + rowGap, width, background, fx, bounds);

        if (buttons) {
            const float rowY = c1.y + artistPx + 10.f, bx = x + (width - buttonsW) * 0.5f;
            for (int b = 0; b < 3; b++) {
                const ImVec2 b0(bx + (float)b * (cell + cellGap), rowY), b1(b0.x + cell, rowY + 16.f);
                dl->AddRectFilled(b0, b1, IM_COL32(22, 22, 22, 255), 2.f);
                dl->AddRect(b0, b1, IM_COL32(0, 0, 0, 255), 2.f, 0, 1.f);
            }
            drawButtons(dl, bx, rowY + 8.f, fx, bounds);
        }
        return;
    }

    if (g_mpSimple) {
        std::string line = artist.empty() ? title : title + " - " + artist;
        if (!timeNow.empty()) line += "  " + timeNow;
        const ImVec2 lineSize = artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, line.c_str());
        const float totalW = lineSize.x + (buttons ? cellGap * 2.f + buttonsW : 0.f);

        const float lineY = fromBottom ? anchorY - lineSize.y : anchorY;
        const float lineX = floorf(leftOf(totalW));
        put(dl, bareFx, artistFont, artistPx, ImVec2(lineX, lineY), look.title, line);
        ImRect bounds(ImVec2(lineX, lineY), ImVec2(lineX + totalW, lineY + lineSize.y));
        drawLyrics(dl, fromBottom ? lineY - rowGap : lineY + lineSize.y + rowGap, totalW, false, bareFx, bounds);
        if (buttons) drawButtons(dl, lineX + lineSize.x + cellGap * 2.f, floorf(lineY + lineSize.y * 0.5f), bareFx, bounds);
        return;
    }

    const ImVec2 titleSize  = titleFont->CalcTextSizeA(titlePx, FLT_MAX, 0.f, title.c_str());
    const ImVec2 artistSize = artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, artist.c_str());

    const float padding  = background ? 10.f : 0.f;
    const float minWidth = background ? 140.f : 0.f;
    const bool progressBar = g_mpProgressBar;
    const bool visualizer  = g_mpVisualizer;
    const float visualH    = std::clamp(g_mpVisHeight, 4.f, 64.f);
    const bool art = g_mpCoverArt && s_tex;
    const float artGap = (std::max)(padding, 6.f);
    const float artOffsetX = art ? artSize + artGap : 0.f;

    const float textColW = (std::max)(titleSize.x, artistSize.x);
    const float textColH = titleSize.y + rowGap + artistSize.y;
    const float contentH = art ? (std::max)(artSize, textColH) : textColH;

    const float timeW = timeNow.empty() ? 0.f : artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, timeNow.c_str()).x;
    const bool controlRow = buttons || timeW > 0.f;
    const float controlH  = controlRow ? (std::max)(artistSize.y, cell) : 0.f;
    const float controlW  = buttonsW + timeW + (buttons && timeW > 0.f ? 8.f : 0.f);

    const float width  = (std::max)({ textColW + artOffsetX + padding * 2.f, controlW + padding * 2.f, minWidth });
    const float height = padding + contentH + (progressBar || visualizer ? rowGap + barHeight : 0.f) + (controlRow ? rowGap + controlH : 0.f) + padding;

    const float posX = floorf(leftOf(width));
    const float posY = fromBottom ? anchorY - height : anchorY;

    if (background) Watermark_MediaFrame(dl, ImVec2(posX, posY), ImVec2(posX + width, posY + height), frameStyle);

    // botox waveform: 64 bands as bars (bar width + 1 px gap) or a catmull-rom curve ("smooth"),
    // standing on the progress bar line, behind the art + text; accent (or custom) at alpha 185
    if (visualizer) {
        float bands[n_audio_bands::k_bands]{};
        {
            std::lock_guard<std::mutex> lock(s_bandsMtx);
            std::copy(std::begin(s_bands), std::end(s_bands), bands);
        }
        const ImU32 rgb   = g_mpVisCustomColor ? (ImU32)ImColor(g_mpVisColor[0], g_mpVisColor[1], g_mpVisColor[2], g_mpVisColor[3]) : accent;
        const ImU32 color = (rgb & ~IM_COL32_A_MASK) | (185u << IM_COL32_A_SHIFT);

        const bool smooth  = g_mpVisSmooth;
        const float left   = posX + padding;
        const float right  = posX + width - padding;
        const float floorY = posY + padding + contentH + rowGap;
        const float gap    = 1.f;
        const float barW   = (float)std::clamp(g_mpVisBarWidth, 1, 12);
        const int bars     = smooth ? (int)n_audio_bands::k_bands : (std::max)(1, (int)((right - left + gap) / (barW + gap)));
        const float slot   = (right - left) / (float)bars;

        if (smooth) {
            const auto curveTop = [&](float x) {
                const float u = (x - left) / slot - 0.5f;
                const int i   = (int)floorf(u);
                const float t = u - (float)i;
                const auto at = [&](int k) { return bands[std::clamp(k, 0, bars - 1)]; };
                const float p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
                const float level = 0.5f * (2.f * p1 + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t * t + (3.f * (p1 - p2) + p3 - p0) * t * t * t);
                const float h = std::clamp(level, 0.f, 1.f) * visualH;
                return floorY - (progressBar ? h : (std::max)(1.f, h));
            };
            const auto oldFlags = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (float x = left; x < right; x += 1.f) {
                const float x2 = (std::min)(x + 1.f, right);
                dl->AddQuadFilled(ImVec2(x, curveTop(x)), ImVec2(x2, curveTop(x2)), ImVec2(x2, floorY), ImVec2(x, floorY), color);
            }
            dl->Flags = oldFlags;
            for (float x = left; x < right; x += 1.f) dl->PathLineTo(ImVec2(x, curveTop(x)));
            dl->PathLineTo(ImVec2(right, curveTop(right)));
            dl->PathStroke(color, ImDrawFlags_None, 1.f);
        }

        for (int bar = 0; bar < bars && !smooth; bar++) {
            const float level = n_audio_bands::bar_level(bands, (std::size_t)bar, (std::size_t)bars);
            const float barH  = level * visualH;
            if (progressBar && barH < 1.f) continue;
            const float top = floorY - (std::max)(1.f, barH);
            const float x   = floorf(left + slot * (float)bar);
            const float x2  = bar == bars - 1 ? right : floorf(left + slot * (float)(bar + 1)) - gap;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x2, floorY), color);
        }
    }

    const bool leftSide = align == 0;
    const float artX = leftSide ? posX + padding : posX + width - padding - artSize;
    const float artY = posY + padding + (contentH - artSize) * 0.5f;
    if (art)
        dl->AddImageRounded((ImTextureID)s_tex, ImVec2(artX, artY), ImVec2(artX + artSize, artY + artSize), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                            IM_COL32_WHITE, 2.f);

    const float textLeft  = art ? artX + artSize + artGap : posX + padding;
    const float textRight = art ? artX - artGap : posX + width - padding;
    const float textTop   = posY + padding + (contentH - textColH) * 0.5f;

    const ImVec2 titlePos(leftSide ? textLeft : textRight - titleSize.x, textTop);
    const ImVec2 artistPos(leftSide ? textLeft : textRight - artistSize.x, textTop + titleSize.y + rowGap);

    put(dl, fx, titleFont, titlePx, titlePos, look.title, title);
    put(dl, fx, artistFont, artistPx, artistPos, look.artist, artist);

    if (progressBar) {
        const float barX = posX + padding;
        const float barY = posY + padding + contentH + rowGap;
        const float barWidth = posX + width - padding - barX;
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), IM_COL32(60, 60, 60, 255), 1.f);
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth * mp::ProgressOf(track), barY + barHeight), accent, 1.f);
    }

    ImRect bounds(ImVec2(posX, posY), ImVec2(posX + width, posY + height));
    drawLyrics(dl, fromBottom ? posY - rowGap : posY + height + rowGap, width, background, fx, bounds);

    if (controlRow) {
        const float rowY = posY + height - padding - controlH;
        if (timeW > 0.f) {
            const float timeX = leftSide ? posX + width - padding - timeW : posX + padding;
            put(dl, fx, artistFont, artistPx, ImVec2(floorf(timeX), floorf(rowY + (controlH - artistSize.y) * 0.5f)), look.artist, timeNow);
        }
        if (buttons)
            drawButtons(dl, leftSide ? posX + padding : posX + width - padding - buttonsW, floorf(rowY + controlH * 0.5f), fx, bounds);
    }
}

bool   MediaPlayer_HasSession()    { return g_mpEnable && s_has.load(); }

bool MediaPlayer_Track(std::string& title, std::string& artist)
{
    if (!s_has.load()) return false;
    std::lock_guard<std::mutex> lock(s_mtx);
    title = s_title; artist = s_artist;
    return true;
}
// 0 toggle / 1 next / 2 previous -> botox e_command 2 / 3 / 1
void   MediaPlayer_RequestControl(int cmd)
{
    static constexpr int k_map[3] = { 2, 3, 1 };
    if (cmd >= 0 && cmd <= 2) s_pendingCmd = k_map[cmd];
}
