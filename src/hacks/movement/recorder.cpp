// Movement recorder = botox hacks/movement/movement_recorder.cpp 1:1 (see recorder.h for what
// was left out and why). Function names follow botox's so a side by side read lines up:
// capture_user_input / apply_playback / on_create_move / on_frame_stage / camera_lock / render_*.
#include "recorder.h"
#include "prediction.h"            // EntV3 / EntI, g_globalVars
#include "../aim/aim.h"            // Aim_ChatPrint
#include "../../render/render.h"   // W2S_Project
#include "../../render/dpi.h"      // botox render_indicator: queued stretch block (dpi scale panels)
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>

static constexpr int IN_JUMP      = 1 << 1;
static constexpr int IN_DUCK      = 1 << 2;
static constexpr int IN_FORWARD   = 1 << 3;
static constexpr int IN_BACK      = 1 << 4;
static constexpr int IN_MOVELEFT  = 1 << 9;
static constexpr int IN_MOVERIGHT = 1 << 10;
static constexpr int IN_SPEED     = 1 << 17;
static constexpr int kUserMoveMask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP | IN_DUCK;

static constexpr float k_startRadius    = 28.f;
static constexpr float k_previewLineDist = 300.f;  // botox k_preview_line_dist
static constexpr float k_posCloseEps    = 0.1f;
static constexpr float k_approachGiveUp = 2.f;
static constexpr float k_indicatorLerpK = 6.f;

// ---- engine -----------------------------------------------------------------------------
// IVEngineClient SetViewAngles 20 / GetViewAngles 19 (movement.cpp kEngine_*, file static
// there), GetLevelName 51 (psassist.inl EngineLevelName).
static void RecSetViewAngles(Vec3 ang)
{
    if (!g_engine) return;
    __try { ((void(__fastcall*)(void*, Vec3&))(*(void***)g_engine)[20])(g_engine, ang); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static Vec3 RecGetViewAngles()
{
    Vec3 ang;
    if (!g_engine) return ang;
    __try { ((void(__fastcall*)(void*, Vec3&))(*(void***)g_engine)[19])(g_engine, ang); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return ang;
}

static const char* RecLevelNameRaw()
{
    if (!g_engine) return "";
    const char* n = nullptr;
    __try { n = ((const char*(__fastcall*)(void*))(*(void***)g_engine)[51])(g_engine); }
    __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "";
}

// botox get_level_name_short: "maps/jump_beef.bsp" -> "jump_beef"
static std::string RecLevelName()
{
    std::string s = RecLevelNameRaw();
    if (const size_t sl = s.find_last_of("/\\"); sl != std::string::npos) s.erase(0, sl + 1);
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".bsp") == 0) s.resize(s.size() - 4);
    return s;
}

static float NowReal() { return g_globalVars ? g_globalVars->realtime : 0.f; }

static int ServerTickrate()
{
    const float iv = g_globalVars ? g_globalVars->interval_per_tick : 0.f;
    return iv > 0.f ? (int)(1.f / iv + 0.5f) : 67;
}

// ---- weapons ----------------------------------------------------------------------------
// m_hMyWeapons[i] / m_hActiveWeapon EHANDLEs: & 0xFFFF = entry (NUM_ENT_ENTRY_BITS 13, serial
// shifted by 16), same decode psassist.inl's Atomizer check uses.
static void* WeaponFromHandle(unsigned h)
{
    const int idx = (int)(h & 0xFFFF);
    if (h == 0xFFFFFFFF || idx < 1 || idx >= 2048) return nullptr;
    return GetClientEntity(idx);
}

static unsigned MyWeaponHandle(void* pLocal, int i)
{
    return *(unsigned*)((uintptr_t)pLocal + g_offMyWeapons + i * 4);
}

static bool WeaponsOK() { return g_offMyWeapons > 0 && g_offActiveWeapon > 0 && g_offItemDefIndex > 0; }

// ---- state ------------------------------------------------------------------------------
struct RecClip
{
    std::string file, name, map;
    int tickrate = 67;
    std::vector<RecFrame> frames;
    std::string Label() const { return tickrate == ServerTickrate() ? name : name + " (" + std::to_string(tickrate) + " tick)"; }
};

// one lock for everything: the game thread (cmd, frame stage, mouse) and the render thread
// (menu, overlay, editor) both touch it. Recursive: botox's m_clips_mutex is too.
static std::recursive_mutex  s_lock;
static std::atomic<unsigned> s_requests{ 0u };

static bool                  s_recording = false;
static std::vector<RecFrame> s_recFrames;
static std::deque<RecFrame>  s_clipBuf;

static RecFrame s_userInput;
static int      s_userMask = 0, s_userMaskPrev = 0;

static bool   s_playing = false, s_wishStart = false;
static size_t s_playIdx = 0;
static int    s_approachTicks = 0;
static float  s_stepReal = 0.f, s_smoothT0 = 0.f;
static Vec3   s_smoothFrom;
static unsigned long long s_pathStartMs = 0;
static std::vector<RecFrame> s_playFrames;
static std::string s_activeName;

static std::vector<RecClip> s_clips;
static int    s_selected = -1;
static size_t s_clipsStamp = 0;
static unsigned long long s_lastRefreshMs = 0;
static std::string s_map;

static bool   s_editorOpen = false;
static int    s_editorClip = -1, s_editorStart = 0, s_editorEnd = 0;
static char   s_editorName[128] = {};
static unsigned long long s_editorOpenMs = 0;
static int    s_nearClip = -1;              // route start within k_startRadius (ENTER opens it)

static std::vector<float> s_pointProgress;
static float  s_indAlpha = 0.f;

// per cmd: CmdStart -> CmdEnd hand-off
static bool   s_cmdBegun = false;
static void*  s_cmdLocal = nullptr;
static Vec3   s_cmdPos, s_cmdVel;
// render snapshot of the local hull, written per cmd
static bool   s_localAlive = false;
static Vec3   s_localEye;

static std::vector<std::string> s_msgs;     // chat lines, printed from the game thread

static void Notify(const std::string& s)
{
    s_msgs.push_back(s);
    DebugLog("REC: %s", s.c_str());
}

static bool OwnsCmd() { return s_playing || s_wishStart; }

// playback yaw != original = camera follows the route too, just turned
static bool CameraLocked() { return g_recLockVa || g_recYaw != 0; }

// ---- frames -----------------------------------------------------------------------------
static RecFrame MakeFrame(void* pLocal, const UserCmd_t* c, const Vec3& pos, const Vec3& vel)
{
    RecFrame f;
    f.pitch = c->viewangles.x; f.yaw = c->viewangles.y;
    f.forward = c->forwardmove; f.side = c->sidemove; f.up = c->upmove;
    f.buttons = c->buttons;
    f.mouseDx = c->mousedx; f.mouseDy = c->mousedy;
    f.posX = pos.x; f.posY = pos.y; f.posZ = pos.z;
    f.velX = vel.x; f.velY = vel.y; f.velZ = vel.z;
    f.hasVel = g_offVelocity > 0;
    if (!pLocal || !WeaponsOK()) return f;
    const unsigned active = *(unsigned*)((uintptr_t)pLocal + g_offActiveWeapon);
    if (void* w = WeaponFromHandle(active)) f.weaponDef = EntI(w, g_offItemDefIndex);
    else return f;
    for (int i = 0; i < 48; ++i)
        if (MyWeaponHandle(pLocal, i) == active) { f.weaponSlot = i; break; }
    return f;
}

static void Replay(const RecFrame& f, UserCmd_t* c)
{
    c->viewangles.x = f.pitch; c->viewangles.y = f.yaw;
    c->forwardmove = f.forward; c->sidemove = f.side; c->upmove = f.up;
    c->buttons = f.buttons;
    c->mousedx = f.mouseDx; c->mousedy = f.mouseDy;
}

static bool Playable(const RecClip& c) { return !c.frames.empty() && c.tickrate == ServerTickrate(); }

// ---- files ------------------------------------------------------------------------------
static std::string SanitizeName(std::string name)
{
    for (char& ch : name)
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
            ch = '_';
    while (!name.empty() && isspace((unsigned char)name.front())) name.erase(name.begin());
    while (!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
    return name;
}

static std::filesystem::path Root() { return std::filesystem::path(kRecorderDir); }

static void EnsureRoot()
{
    std::error_code ec;
    if (!std::filesystem::exists(Root(), ec)) std::filesystem::create_directories(Root(), ec);
}

static std::filesystem::path UniqueClipPath(std::string& name)
{
    name = SanitizeName(name);
    if (name.empty()) name = "Clip";
    std::error_code ec;
    auto path = Root() / (name + ".rec");
    if (!std::filesystem::exists(path, ec)) return path;
    const std::string base = name;
    for (int i = 1; i < 1000; ++i)
    {
        name = base + " " + std::to_string(i);
        path = Root() / (name + ".rec");
        if (!std::filesystem::exists(path, ec)) return path;
    }
    return path;
}

static bool ReadClip(const std::filesystem::path& path, RecClip& out)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::vector<uint8_t> bytes;
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0) { bytes.resize((size_t)sz); bytes.resize(fread(bytes.data(), 1, (size_t)sz, f)); }
    fclose(f);
    RecClipData d;
    if (!RecDeserialize(bytes, d)) return false;
    out.file = path.filename().string();
    out.name = path.stem().string();
    out.map = std::move(d.map);
    out.tickrate = d.tickrate;
    out.frames = std::move(d.frames);
    return true;
}

static bool WriteClip(const std::filesystem::path& path, const RecClip& c)
{
    RecClipData d;
    d.map = c.map; d.tickrate = c.tickrate; d.frames = c.frames;
    std::vector<uint8_t> bytes;
    if (!RecSerialize(d, bytes)) return false;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

static void RefreshClips(bool onlyIfChanged)
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    EnsureRoot();

    // listing stamp: re-parsing every clip each 5 s is the stall botox measured
    std::vector<std::filesystem::path> files;
    std::wstring stamp(s_map.begin(), s_map.end());
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(Root(), ec))
    {
        std::error_code fe;
        if (!e.is_regular_file(fe) || e.path().extension() != ".rec") continue;
        stamp += L'|' + e.path().filename().native() + L':' + std::to_wstring(e.file_size(fe)) + L':' +
                 std::to_wstring(e.last_write_time(fe).time_since_epoch().count());
        files.push_back(e.path());
    }
    const size_t hash = std::hash<std::wstring>{}(stamp);
    if (onlyIfChanged && hash == s_clipsStamp) return;
    s_clipsStamp = hash;

    s_clips.clear();
    for (const auto& p : files)
    {
        RecClip c;
        if (!ReadClip(p, c)) continue;
        if (!s_map.empty() && !c.map.empty() && c.map != s_map) continue;
        s_clips.push_back(std::move(c));
    }
    if (s_selected >= (int)s_clips.size()) s_selected = -1;
    if (s_editorClip >= (int)s_clips.size()) s_editorOpen = false;
}

static void SaveFrames(const std::vector<RecFrame>& frames, const char* kind)
{
    if (s_map.empty()) return;
    if (frames.size() < 2) { Notify("nothing to save"); return; }
    EnsureRoot();

    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d_%H-%M-%S", &tm);

    RecClip c;
    c.map = s_map;
    c.tickrate = ServerTickrate();
    c.name = s_map + " " + kind + " " + date;
    c.frames = frames;
    const auto path = UniqueClipPath(c.name);
    c.name = path.stem().string();
    c.file = path.filename().string();

    if (!WriteClip(path, c)) { Notify("save failed: " + path.string()); return; }
    Notify("saved \"" + c.name + "\" " + std::to_string(c.frames.size()) + " ticks");
    DebugLog("REC: saved %s ticks=%d tick=%d", c.file.c_str(), (int)c.frames.size(), c.tickrate);
    s_clips.push_back(std::move(c));
    s_selected = (int)s_clips.size() - 1;
}

static void ClipRoute()
{
    const float seconds = std::clamp(g_recClipSeconds, 1.f, 120.f);
    const size_t want = (size_t)(seconds * (float)ServerTickrate());
    const size_t take = std::min(want, s_clipBuf.size());
    if (take < 2) { Notify("clip buffer empty"); return; }

    std::vector<RecFrame> frames(s_clipBuf.end() - (std::ptrdiff_t)take, s_clipBuf.end());
    const size_t first = RecStillStart(frames);
    if (first < frames.size() && frames.size() - first >= 2)
        frames.erase(frames.begin(), frames.begin() + (std::ptrdiff_t)first);
    else
        Notify("clip starts moving: replay may drift");
    SaveFrames(frames, "Clip");
}

static void DeleteClip(size_t index)
{
    if (index >= s_clips.size()) return;
    std::error_code ec;
    std::filesystem::remove(Root() / s_clips[index].file, ec);
    s_clips.erase(s_clips.begin() + index);
    if (s_selected >= (int)s_clips.size()) s_selected = -1;
    if (s_editorClip == (int)index) s_editorOpen = false;
    else if (s_editorClip > (int)index) --s_editorClip;
}

static void RenameClip(size_t index, const std::string& newName)
{
    if (index >= s_clips.size()) return;
    RecClip& c = s_clips[index];
    std::string name = SanitizeName(newName);
    if (name.empty()) name = c.name.empty() ? "Clip" : c.name;

    std::error_code ec;
    const auto oldPath = Root() / c.file;
    auto newPath = Root() / (name + ".rec");
    if (newPath != oldPath && std::filesystem::exists(newPath, ec)) newPath = UniqueClipPath(name);
    if (newPath != oldPath)
    {
        std::filesystem::rename(oldPath, newPath, ec);
        if (ec) std::filesystem::remove(oldPath, ec);
    }
    c.name = name;
    c.file = name + ".rec";
}

static void OpenClipEditor(size_t index)
{
    if (index >= s_clips.size() || s_clips[index].frames.empty()) return;
    s_editorOpen = true;
    s_editorOpenMs = GetTickCount64();
    s_editorClip = (int)index;
    s_editorStart = 0;
    s_editorEnd = (int)s_clips[index].frames.size() - 1;
    memset(s_editorName, 0, sizeof(s_editorName));
    strncpy_s(s_editorName, s_clips[index].name.c_str(), sizeof(s_editorName) - 1);
}

static void ApplyClipEditor()
{
    if (s_editorClip < 0 || s_editorClip >= (int)s_clips.size()) return;
    RecClip& c = s_clips[s_editorClip];
    if (c.frames.empty()) return;
    const std::string oldFile = c.file;

    const int maxFrame = (int)c.frames.size() - 1;
    s_editorStart = std::clamp(s_editorStart, 0, maxFrame);
    s_editorEnd   = std::clamp(s_editorEnd, 0, maxFrame);
    if (s_editorStart > s_editorEnd) std::swap(s_editorStart, s_editorEnd);
    if (s_editorStart > 0 || s_editorEnd < maxFrame)
    {
        std::vector<RecFrame> trimmed(c.frames.begin() + s_editorStart, c.frames.begin() + s_editorEnd + 1);
        c.frames = std::move(trimmed);
        s_editorStart = 0;
        s_editorEnd = (int)c.frames.size() - 1;
    }
    RenameClip((size_t)s_editorClip, s_editorName);
    WriteClip(Root() / s_clips[s_editorClip].file, s_clips[s_editorClip]);
    s_editorOpen = false;
    Notify("edited \"" + oldFile + "\" to \"" + s_clips[s_editorClip].file + "\"");
}

// ---- playback ---------------------------------------------------------------------------
// botox yaw_offset / camera_at
static float YawOffset(size_t idx)
{
    const float rate = (float)ServerTickrate();
    return RecYawOffset(g_recYaw, s_playFrames.size(), idx, g_recSmoothEnd * rate, g_recSpinSpeed / rate);
}

// smooth start: blend from the view at play press to the route camera
static Vec3 CameraAt(size_t idx, float now)
{
    const RecFrame& f = s_playFrames[std::min(idx, s_playFrames.size() - 1)];
    const float dur = g_recSmoothStart;
    const float w = dur > 0.01f ? RecSmoothstep((now - s_smoothT0) / dur) : 1.f;
    if (w >= 1.f) return Vec3(f.pitch, std::remainder(f.yaw + YawOffset(idx), 360.f), 0.f);
    const float pitch = s_smoothFrom.x + (f.pitch - s_smoothFrom.x) * w;
    const float yaw = s_smoothFrom.y + std::remainder(f.yaw - s_smoothFrom.y, 360.f) * w + YawOffset(idx) * w;
    return Vec3(pitch, std::remainder(yaw, 360.f), 0.f);
}

// true = already holding it (or nothing recorded / owned); false = cmd now asks for the switch.
// botox matches the def first, then the weapon-data slot; TF2's stand-in for the slot is the
// m_hMyWeapons index the clip was recorded with.
static bool SelectWeapon(void* pLocal, UserCmd_t* c, const RecFrame& f)
{
    if (f.weaponSlot < 0 || !pLocal || !WeaponsOK()) return true;
    const unsigned activeH = *(unsigned*)((uintptr_t)pLocal + g_offActiveWeapon);
    void* active = WeaponFromHandle(activeH);
    if (active && EntI(active, g_offItemDefIndex) == f.weaponDef) return true;

    int sameDef = 0, sameSlot = 0;
    for (int i = 0; i < 48; ++i)
    {
        const unsigned h = MyWeaponHandle(pLocal, i);
        void* w = WeaponFromHandle(h);
        if (!w) continue;
        if (EntI(w, g_offItemDefIndex) == f.weaponDef) { sameDef = (int)(h & 0xFFFF); break; }
        if (!sameSlot && i == f.weaponSlot) sameSlot = (int)(h & 0xFFFF);
    }
    const int target = sameDef ? sameDef : sameSlot;
    if (!target || (active && (int)(activeH & 0xFFFF) == target)) return true;
    c->weaponselect = target;
    return false;
}

static void CaptureUserInput(UserCmd_t* c)
{
    s_userInput = MakeFrame(nullptr, c, s_cmdPos, s_cmdVel);
    s_userMaskPrev = s_userMask;
    s_userMask = c->buttons & kUserMoveMask;
}

// cmd-only: CmdStart/CmdPrePred lay it early too, so features + prediction see the replay
static void ApplyPlayback(UserCmd_t* c)
{
    if (!c || !s_playing || s_playIdx >= s_playFrames.size() || !g_recEnable) return;
    const Vec3 backupView = c->viewangles;
    Replay(s_playFrames[s_playIdx], c);

    if (g_recForceWeapon)
    {
        c->weaponselect = 0;
        c->weaponsubtype = 0;
        SelectWeapon(s_cmdLocal, c, s_playFrames[s_playIdx]);
    }

    // movement fix keeps the recorded world wish dir under any sent view; same view = untouched,
    // bit exact replay
    Vec3 view = CameraLocked() ? CameraAt(s_playIdx, NowReal()) : backupView;
    view.z = c->viewangles.z;
    if (view.x == c->viewangles.x && view.y == c->viewangles.y) return;
    CorrectMovement(c, c->viewangles, view);
    c->viewangles = view;
}

static void ForceStop(bool keepPlayed = false)
{
    const bool wasPlaying = s_playing;
    const size_t played = s_playIdx;
    s_playing = false;
    s_wishStart = false;
    s_playIdx = 0;
    s_approachTicks = 0;

    // take over: the ticks already sent are the newest in the clip buffer, keep recording from there
    if (keepPlayed && wasPlaying && played > 0)
    {
        const size_t n = std::min(played, s_clipBuf.size());
        s_recFrames.assign(s_clipBuf.end() - (std::ptrdiff_t)n, s_clipBuf.end());
        s_recording = true;
        Notify("took over at tick " + std::to_string(n) + ", recording");
    }
}

static void ResetAll()
{
    ForceStop();
    s_recording = false;
    s_clipBuf.clear();
}

static bool PlayClip(size_t index)
{
    if (index >= s_clips.size()) return false;
    const RecClip& c = s_clips[index];
    if (c.frames.empty()) return false;
    if (!Playable(c))
    {
        Notify("\"" + c.name + "\" is " + std::to_string(c.tickrate) + " tick, server is " + std::to_string(ServerTickrate()));
        return false;
    }
    const RecFrame& f0 = c.frames[0];
    const float dist = s_cmdPos.DistTo(Vec3(f0.posX, f0.posY, f0.posZ));
    if (dist >= k_startRadius) return false;

    s_playFrames = c.frames;
    s_activeName = c.name;
    s_selected = (int)index;
    s_wishStart = true;
    s_approachTicks = 0;
    s_smoothT0 = NowReal();
    s_smoothFrom = RecGetViewAngles();
    DebugLog("REC: play %s ticks=%d dist=%.2f yaw=%d in=%.2f out=%.2f", c.file.c_str(), (int)c.frames.size(),
             dist, g_recYaw, g_recSmoothStart, g_recSmoothEnd);
    return true;
}

// ---- game thread entry points -----------------------------------------------------------
bool Recorder_OwnsCmd()
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    return OwnsCmd();
}

void Recorder_CmdStart(void* pLocal, UserCmd_t* pCmd)
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    s_cmdBegun = false;
    s_localAlive = pLocal != nullptr;
    if (!pCmd) return;
    if (!g_recEnable || !pLocal)
    {
        s_requests.exchange(0u);   // botox: requests on a dead/disabled cmd are dropped
        if (s_playing || s_wishStart || s_recording || !s_clipBuf.empty()) ResetAll();
        return;
    }
    s_cmdLocal = pLocal;
    s_cmdPos = g_offOrigin > 0 ? EntV3(pLocal, g_offOrigin) : Vec3();
    s_cmdVel = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity) : Vec3();
    s_localEye = s_cmdPos + (g_offViewOffset > 0 ? EntV3(pLocal, g_offViewOffset) : Vec3(0.f, 0.f, 68.f));
    s_cmdBegun = true;

    CaptureUserInput(pCmd);
    ApplyPlayback(pCmd);
}

void Recorder_CmdPrePred(UserCmd_t* pCmd)
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    if (s_cmdBegun) ApplyPlayback(pCmd);
}

// botox on_create_move, minus the alive/enabled checks CmdStart already made
static void OnCreateMove(UserCmd_t* c)
{
    const unsigned requests = s_requests.exchange(0u);

    struct Edge
    {
        int* key;
        bool was = false;
        bool Pressed()
        {
            const bool now = *key > 0 && *key < 256 && MovementKeyOK(*key, 1);
            const bool edge = now && !was;
            was = now;
            return edge;
        }
    };
    static Edge kStartRec{ &g_recKeyStartRec }, kStopRec{ &g_recKeyStopRec }, kSave{ &g_recKeySave },
                kStartPlay{ &g_recKeyStartPlay }, kStopPlay{ &g_recKeyStopPlay }, kClear{ &g_recKeyClear },
                kClip{ &g_recKeyClip };

    const bool menu = g_showMenu;
    const bool startRec  = (kStartRec.Pressed() && !menu)  || (requests & REC_START_REC);
    const bool stopRec   = (kStopRec.Pressed() && !menu)   || (requests & REC_STOP_REC);
    const bool save      = (kSave.Pressed() && !menu)      || (requests & REC_SAVE);
    const bool startPlay = (kStartPlay.Pressed() && !menu) || (requests & REC_START_PLAY);
    const bool stopPlay  = (kStopPlay.Pressed() && !menu)  || (requests & REC_STOP_PLAY);
    const bool clear     = (kClear.Pressed() && !menu)     || (requests & REC_CLEAR);
    const bool clip      = (kClip.Pressed() && !menu)      || (requests & REC_CLIP);

    // fresh press only: still holding w from before play must not kill it
    const bool userMoved = g_recStopOnMove && (s_userMask & ~s_userMaskPrev) != 0;
    if ((stopPlay || userMoved) && OwnsCmd())
    {
        DebugLog("REC: stop play idx=%d/%d moved=%d", (int)s_playIdx, (int)s_playFrames.size(), userMoved ? 1 : 0);
        ForceStop(true);
        if (userMoved) Replay(s_userInput, c);
    }

    if (startRec && !OwnsCmd() && !s_recording)
    {
        s_recFrames.clear();
        s_recording = true;
        Notify("recording");
    }
    if (stopRec && s_recording)
    {
        s_recording = false;
        Notify("stopped recording, " + std::to_string(s_recFrames.size()) + " ticks");
    }
    if (clear && !s_recording && !OwnsCmd())
    {
        s_recFrames.clear();
        Notify("cleared route");
    }
    if (save)
    {
        s_recording = false;
        SaveFrames(s_recFrames, "Route");
    }
    if (clip && g_recClipper) ClipRoute();

    if (startPlay && !s_recording && !OwnsCmd())
    {
        size_t target = (size_t)-1;
        const auto startDist = [](const RecClip& rc) { const RecFrame& f = rc.frames[0]; return s_cmdPos.DistTo(Vec3(f.posX, f.posY, f.posZ)); };
        if (s_selected >= 0 && s_selected < (int)s_clips.size() && Playable(s_clips[s_selected])
            && startDist(s_clips[s_selected]) < k_startRadius)
            target = (size_t)s_selected;
        else
        {
            float best = k_startRadius;
            for (size_t i = 0; i < s_clips.size(); ++i)
            {
                if (s_clips[i].frames.empty()) continue;
                const float d = startDist(s_clips[i]);
                if (d < best && (Playable(s_clips[i]) || target == (size_t)-1)) { target = i; best = d; }
            }
        }
        if (target != (size_t)-1) PlayClip(target);
        else Notify("no route start nearby");
    }

    if (s_wishStart && !s_playFrames.empty())
    {
        const RecFrame& first = s_playFrames.front();
        const Vec3 firstPos(first.posX, first.posY, first.posZ);
        const float dist = s_cmdPos.DistTo(firstPos);

        if (CameraLocked())
        {
            const Vec3 sv = CameraAt(0, NowReal());
            c->viewangles.x = sv.x; c->viewangles.y = sv.y;
            c->mousedx = 0; c->mousedy = 0;
            RecSetViewAngles(sv);
        }

        const float giveUpTicks = k_approachGiveUp * (float)ServerTickrate();
        const bool timedOut = (float)(++s_approachTicks) > giveUpTicks;
        const bool atStart = dist <= k_posCloseEps || (timedOut && dist < 1.f);

        // weapon sets max speed: start only once the recorded one is out
        bool weaponOk = true;
        if (g_recForceWeapon)
        {
            c->weaponselect = 0;
            c->weaponsubtype = 0;
            weaponOk = SelectWeapon(s_cmdLocal, c, first);
        }

        if (atStart && weaponOk)
        {
            s_wishStart = false;
            s_playing = true;
            s_playIdx = 0;
            s_pathStartMs = GetTickCount64();
            DebugLog("REC: start dist=%.3f approach=%d wpn=%d", dist, s_approachTicks, first.weaponDef);
        }
        else if (timedOut && !atStart)
        {
            DebugLog("REC: approach gave up dist=%.2f", dist);
            Notify("couldn't reach route start");
            ForceStop();
        }
        else
        {
            RecApproachMove(s_cmdPos.x, s_cmdPos.y, firstPos.x, firstPos.y, c->viewangles.y, c->forwardmove, c->sidemove);
            c->upmove = 0.f;
            c->buttons &= ~(IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_SPEED);
            c->buttons |= first.buttons & IN_DUCK;
        }
    }

    if (s_playing)
    {
        if (s_playIdx >= s_playFrames.size())
        {
            DebugLog("REC: done ticks=%d", (int)s_playFrames.size());
            ForceStop();
        }
        else
        {
            ApplyPlayback(c);
            ++s_playIdx;
            s_stepReal = NowReal();
        }
    }

    c->viewangles.x = std::clamp(c->viewangles.x, -89.f, 89.f);
    c->viewangles.y = std::remainder(c->viewangles.y, 360.f);

    const RecFrame f = MakeFrame(s_cmdLocal, c, s_cmdPos, s_cmdVel);

    // clipper off: buffer only while playing, take over still needs the played ticks
    if (g_recClipper || s_playing)
    {
        const size_t cap = (size_t)(std::clamp(g_recClipSeconds, 1.f, 120.f) * (float)ServerTickrate());
        s_clipBuf.push_back(f);
        while (s_clipBuf.size() > cap) s_clipBuf.pop_front();
    }
    else
        s_clipBuf.clear();

    if (s_recording)
    {
        s_recFrames.push_back(f);
        if ((int)s_recFrames.size() >= kRecMaxTick) { s_recording = false; Notify("record cap hit"); }
    }
}

static bool OnCreateMoveSeh(UserCmd_t* c)
{
    __try { OnCreateMove(c); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Recorder_CmdEnd(UserCmd_t* pCmd)
{
    std::vector<std::string> msgs;
    {
        std::lock_guard<std::recursive_mutex> l(s_lock);
        if (s_cmdBegun && pCmd)
        {
            s_cmdBegun = false;
            if (!OnCreateMoveSeh(pCmd)) { DebugLog("REC: SEH in OnCreateMove"); ResetAll(); }
        }
        msgs.swap(s_msgs);
    }
    // detections.cpp chat look; ChatPrintf formats, so no '%' may reach it
    for (std::string& m : msgs)
    {
        m.erase(std::remove(m.begin(), m.end(), '%'), m.end());
        char line[320];
        snprintf(line, sizeof(line), "\x07%02X%02X%02X" "recorder\x07" "BEBEBE | %s",
                 (int)(g_accentColor[0] * 255), (int)(g_accentColor[1] * 255), (int)(g_accentColor[2] * 255), m.c_str());
        Aim_ChatPrint(line);
    }
}

// botox on_frame_stage(FRAME_START)
void Recorder_FrameStart()
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    if (!CameraLocked() || s_playFrames.empty()) return;
    const float now = NowReal();

    // approach: per frame so smooth start eases at fps, not tick rate
    if (s_wishStart) { RecSetViewAngles(CameraAt(0, now)); return; }
    if (!s_playing || s_playIdx == 0 || s_playIdx >= s_playFrames.size()) return;

    // sent frame idx-1 last tick, idx goes out next: lerp by real time so the camera is smooth at any fps
    const Vec3 from = CameraAt(s_playIdx - 1, now);
    const Vec3 to = CameraAt(s_playIdx, now);
    const float iv = g_globalVars ? g_globalVars->interval_per_tick : 0.f;
    const float t = std::clamp(iv > 1e-6f ? (now - s_stepReal) / iv : 1.f, 0.f, 1.f);
    RecSetViewAngles(Vec3(from.x + (to.x - from.x) * t, RecWrap180(from.y + RecWrap180(to.y - from.y) * t), 0.f));
}

// botox camera_lock
void Recorder_MouseInput(float* x, float* y)
{
    if (!x || !y || !g_recEnable) return;
    std::lock_guard<std::recursive_mutex> l(s_lock);
    const bool lockva = CameraLocked();
    if ((s_playing && lockva) || (s_wishStart && (lockva || g_recLockGoingToStart))) { *x = 0.f; *y = 0.f; }
}

// ---- render thread ----------------------------------------------------------------------
void Recorder_Request(unsigned actions) { s_requests.fetch_or(actions); }

void Recorder_Refresh() { RefreshClips(false); }

bool Recorder_EditorOpen()
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    return s_editorOpen;
}

// botox: ENTER near a route start opens its editor; in the editor ESC closes, ENTER closes
// unless a text field has it. 250 ms debounce so the opening press can't close it (the ps
// point window uses the same rule).
bool Recorder_EditorKey(unsigned vk)
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    if (!g_recEnable) return false;
    if (s_editorOpen)
    {
        if (GetTickCount64() - s_editorOpenMs <= 250) return vk == VK_RETURN;
        if (vk == VK_ESCAPE || (vk == VK_RETURN && !ImGui::GetIO().WantTextInput)) { s_editorOpen = false; return true; }
        return false;
    }
    if (vk != VK_RETURN || s_nearClip < 0 || s_nearClip >= (int)s_clips.size() || OwnsCmd()) return false;
    s_selected = s_nearClip;
    OpenClipEditor((size_t)s_nearClip);
    return s_editorOpen;
}

static ImU32 Rgba(int r, int g, int b, int a) { return IM_COL32(r, g, b, a); }

static ImU32 AccentU32(float alpha)
{
    const ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, alpha));
}

// botox text_flag_outline | text_flag_dropshadow
static void OutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 p, ImU32 col, const char* text)
{
    const ImU32 black = IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) & 0xFF);
    dl->AddText(font, size, ImVec2(p.x + 1.f, p.y + 1.f), black, text);
    dl->AddText(font, size, ImVec2(p.x - 1.f, p.y), black, text);
    dl->AddText(font, size, ImVec2(p.x + 1.f, p.y), black, text);
    dl->AddText(font, size, ImVec2(p.x, p.y - 1.f), black, text);
    dl->AddText(font, size, ImVec2(p.x, p.y + 1.f), black, text);
    dl->AddText(font, size, p, col, text);
}

static void WorldFloorDisc(ImDrawList* dl, const Vec3& c, float radius, ImU32 fill, ImU32 outline, int seg, float th)
{
    ImVec2 pts[64];
    seg = std::min(seg, 64);
    for (int i = 0; i < seg; ++i)
    {
        const float a = ((float)i / (float)seg) * 6.28318530718f;
        if (!W2S_Project(Vec3(c.x + cosf(a) * radius, c.y + sinf(a) * radius, c.z + 1.f), pts[i])) return;
    }
    dl->AddConvexPolyFilled(pts, seg, fill);
    dl->AddPolyline(pts, seg, outline, ImDrawFlags_Closed, th);
}

static Vec3 FramePos(const RecFrame& f) { return Vec3(f.posX, f.posY, f.posZ); }

static void RenderPath(ImDrawList* dl, const std::vector<RecFrame>& frames, unsigned long long startMs)
{
    if (!g_recShowLine || frames.size() < 2) return;
    const float progress = startMs ? std::clamp((float)(GetTickCount64() - startMs) / 250.f, 0.f, 1.f) : 1.f;
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_Accent);
    const ImU32 col = Rgba((int)(255.f + (acc.x * 255.f - 255.f) * progress), (int)(255.f + (acc.y * 255.f - 255.f) * progress),
                           (int)(255.f + (acc.z * 255.f - 255.f) * progress), (int)(255.f * progress));

    WorldFloorDisc(dl, FramePos(frames[0]), k_startRadius * 0.45f, Rgba(0, 0, 0, 100), AccentU32(0.85f), 64, 1.5f);

    ImVec2 prev;
    bool hasPrev = false;
    for (const RecFrame& f : frames)
    {
        ImVec2 cur;
        const bool on = W2S_Project(FramePos(f), cur);
        if (on && hasPrev) dl->AddLine(prev, cur, col, 0.5f);
        prev = cur;
        hasPrev = on;
    }
}

static void RenderRoutePreview(ImDrawList* dl)
{
    if (s_clips.empty()) return;
    const float dt = ImGui::GetIO().DeltaTime;
    if (s_pointProgress.size() != s_clips.size()) s_pointProgress.assign(s_clips.size(), 1.f);
    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();

    for (size_t i = 0; i < s_clips.size(); ++i)
    {
        const RecClip& route = s_clips[i];
        if (route.frames.empty()) continue;
        const RecFrame& first = route.frames[0];
        const Vec3 start = FramePos(first);
        const float distance = s_cmdPos.DistTo(start);
        if (distance < k_startRadius && s_nearClip < 0) s_nearClip = (int)i;

        if (distance < k_startRadius && font)
        {
            Vec3 fwd;
            AngleVectors(Vec3(first.pitch, first.yaw, 0.f), &fwd);
            ImVec2 aim;
            if (W2S_Project(s_localEye + fwd * 100.f, aim))
            {
                dl->AddCircleFilled(aim, 4.f, Rgba(0, 0, 0, 100), 32);
                dl->AddCircle(aim, 4.f, Rgba(255, 255, 255, 255), 32);
                const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, "aim here");
                OutlinedText(dl, font, font->FontSize, ImVec2(aim.x + 7.f, aim.y - ts.y * 0.5f), Rgba(255, 255, 255, 255), "aim here");
            }
        }

        ImVec2 screen;
        if (!W2S_Project(start, screen)) continue;
        s_pointProgress[i] += (1.f - s_pointProgress[i]) * std::clamp(dt * 5.f, 0.f, 1.f);

        float alphaMul = 1.f;
        if (distance > 900.f) alphaMul = std::clamp((1000.f - distance) / 100.f, 0.f, 1.f);
        if (alphaMul <= 0.f) continue;

        /* idle line = own checkbox, only near the start */
        if (g_recPreviewLine && distance < k_previewLineDist)
        {
            const float lineMul = std::clamp((k_previewLineDist - distance) / 50.f, 0.f, 1.f);
            const ImU32 col = AccentU32(0.55f * lineMul);
            ImVec2 prev;
            bool hasPrev = false;
            for (const RecFrame& f : route.frames)
            {
                ImVec2 cur;
                const bool on = W2S_Project(FramePos(f), cur);
                if (on && hasPrev) dl->AddLine(prev, cur, col, 1.f);
                prev = cur;
                hasPrev = on;
            }
        }

        WorldFloorDisc(dl, start, k_startRadius * 0.45f * s_pointProgress[i], Rgba(0, 0, 0, (int)(100.f * alphaMul)),
                       AccentU32(0.85f * alphaMul), 64, 1.5f);

        if (font && !route.name.empty())
        {
            const float size = font->FontSize * 0.82f;
            const std::string title = route.Label();
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, title.c_str());
            OutlinedText(dl, font, size, ImVec2(screen.x - ts.x * 0.5f, screen.y - 34.f), Rgba(225, 225, 225, (int)(255.f * alphaMul)),
                         title.c_str());
        }
    }
}

static void RenderIndicator(ImDrawList* dl)
{
    if (!g_recRender) return;
    const float dt = ImGui::GetIO().DeltaTime;
    const float target = (s_recording || OwnsCmd()) ? 1.f : 0.f;
    s_indAlpha = std::clamp(s_indAlpha + (target - s_indAlpha) * (1.f - std::exp(-k_indicatorLerpK * dt)), 0.f, 1.f);
    if (s_indAlpha <= 0.01f) return;
    DpiPanelScope block(dl);
    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();

    const std::string text = s_recording ? "rec " + std::to_string(s_recFrames.size())
                                         : std::to_string(s_playIdx) + " / " + std::to_string(s_playFrames.size());
    const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text.c_str());
    const ImVec2 pad(7.f, 7.f), margin(4.f, 3.f);
    const ImVec2 size(ts.x + margin.x * 2.f, ts.y + margin.y * 2.f);
    const ImVec2 disp = ImGui::GetIO().DisplaySize;

    ImVec2 pos;
    switch (g_recPosition)
    {
    default:
    case 0: pos = ImVec2(pad.x, pad.y); break;
    case 1: pos = ImVec2(pad.x, disp.y - size.y - pad.y); break;
    case 2: pos = ImVec2(disp.x - size.x - pad.x, disp.y - size.y - pad.y); break;
    }
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.08f, s_indAlpha)));
    dl->AddRectFilled(ImVec2(pos.x, pos.y - 1.f), ImVec2(pos.x + size.x, pos.y), AccentU32(s_indAlpha));
    OutlinedText(dl, font, font->FontSize, ImVec2(pos.x + margin.x, pos.y + margin.y),
                 ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, s_indAlpha)), text.c_str());
}

static void RenderClipperBox()
{
    if (!g_recClipper || !g_recClipperBox) return;
    const float dt = g_globalVars ? g_globalVars->interval_per_tick : 0.015f;
    char buf[48] = {};
    if (s_playing)               snprintf(buf, sizeof(buf), "%.1f / %.1f", (float)s_playIdx * dt, (float)s_playFrames.size() * dt);
    else if (s_recording)        snprintf(buf, sizeof(buf), "rec %.1f", (float)s_recFrames.size() * dt);
    else if (!s_clipBuf.empty()) snprintf(buf, sizeof(buf), "clip %.1f", (float)s_clipBuf.size() * dt);
    else return;

    if (g_menuReg) ImGui::PushFont(g_menuReg);
    const float width = ImGui::CalcTextSize(buf).x + 14.f;
    ImGui::SetNextWindowPos(ImVec2(24.f, 96.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, 22.f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.04f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f, 4.f));
    if (ImGui::Begin("##mr_clipper_box", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoFocusOnAppearing))
    {
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ts.x) * 0.5f);
        ImGui::TextUnformatted(buf);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (g_menuReg) ImGui::PopFont();
}

static void RenderClipEditor()
{
    if (!s_editorOpen) return;
    if (s_editorClip < 0 || s_editorClip >= (int)s_clips.size() || s_clips[s_editorClip].frames.empty()) { s_editorOpen = false; return; }
    RecClip& clip = s_clips[s_editorClip];

    const ImVec2 disp = ImGui::GetMainViewport()->Size;   // a window: logical (dpi) px, not the screen
    ImGui::SetNextWindowPos(ImVec2(disp.x - 330.f, 20.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310.f, 165.f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.04f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.02f, 0.02f, 0.02f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.02f, 0.02f, 0.02f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.f);
    if (g_menuReg) ImGui::PushFont(g_menuReg);

    if (ImGui::Begin("recorder clip", &s_editorOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%zu ticks  %d tick", clip.frames.size(), clip.tickrate);
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##mr_editor_name", s_editorName, sizeof(s_editorName));

        const int maxFrame = (int)clip.frames.size() - 1;
        s_editorStart = std::clamp(s_editorStart, 0, maxFrame);
        s_editorEnd   = std::clamp(s_editorEnd, 0, maxFrame);
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("##mr_start_frame", &s_editorStart, 0, maxFrame, "start tick %d");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("##mr_end_frame", &s_editorEnd, 0, maxFrame, "end tick %d");

        if (ImGui::Button("apply", ImVec2(-1.f, 15.f))) ApplyClipEditor();
        if (ImGui::Button("close", ImVec2(-1.f, 15.f))) s_editorOpen = false;
    }
    ImGui::End();
    if (g_menuReg) ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(7);
}

// botox check_map_change, on the render tick like botox's paint
static void CheckMapChange()
{
    static bool s_wasInGame = false;
    if (!IsInGame())
    {
        if (s_wasInGame || !s_map.empty())
        {
            s_map.clear();
            s_clips.clear();
            s_selected = -1;
            s_recFrames.clear();
            s_editorOpen = false;
            ResetAll();
        }
        s_wasInGame = false;
        return;
    }
    const std::string level = RecLevelName();
    if (level.empty() || (s_wasInGame && s_map == level)) return;
    s_wasInGame = true;
    s_map = level;
    s_selected = -1;
    s_editorOpen = false;
    s_recFrames.clear();
    ResetAll();
    RefreshClips(false);
    DebugLog("REC: joined %s, %d routes", s_map.c_str(), (int)s_clips.size());
}

static void RenderBody()
{
    CheckMapChange();
    s_nearClip = -1;
    if (!g_recEnable || !IsInGame()) { s_editorOpen = false; return; }

    if (!OwnsCmd() && GetTickCount64() - s_lastRefreshMs > 5000)
    {
        s_lastRefreshMs = GetTickCount64();
        RefreshClips(true);
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (s_localAlive)
    {
        if (OwnsCmd())        RenderPath(dl, s_playFrames, s_pathStartMs);
        else if (s_recording) RenderPath(dl, s_recFrames, 0);
        else                  RenderRoutePreview(dl);
    }
    RenderIndicator(dl);
    RenderClipEditor();
    RenderClipperBox();
}

static bool RenderSeh()
{
    __try { RenderBody(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Recorder_Render()
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    if (!RenderSeh()) s_editorOpen = false;
}

// botox menu_tab_movement "routes" group body
void Recorder_MenuRoutes()
{
    std::lock_guard<std::recursive_mutex> l(s_lock);
    if (s_map.empty())
        ImGui::TextDisabled("join a map to see its routes");
    else
        ImGui::TextDisabled("%s   %d routes   server %d tick", s_map.c_str(), (int)s_clips.size(), ServerTickrate());

    if (ImGui::BeginListBox("##recorder routes", ImVec2(-1.f, 220.f)))
    {
        for (size_t i = 0; i < s_clips.size(); ++i)
        {
            const RecClip& c = s_clips[i];
            const float seconds = (float)c.frames.size() / (float)std::max(c.tickrate, 1);
            char row[384];
            snprintf(row, sizeof(row), "%s   %zu ticks  %.1fs##route %zu", c.Label().c_str(), c.frames.size(), seconds, i);
            if (ImGui::Selectable(row, s_selected == (int)i, ImGuiSelectableFlags_AllowDoubleClick))
            {
                s_selected = (int)i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenClipEditor(i);
            }
        }
        ImGui::EndListBox();
    }

    const bool has = s_selected >= 0 && s_selected < (int)s_clips.size();
    ImGui::BeginDisabled(!has);
    if (ImGui::Button("edit route", ImVec2(-1.f, 15.f)) && has) OpenClipEditor((size_t)s_selected);
    if (ImGui::Button("delete route", ImVec2(-1.f, 15.f)) && has) DeleteClip((size_t)s_selected);
    ImGui::EndDisabled();

    if (ImGui::Button("refresh routes", ImVec2(-1.f, 15.f))) RefreshClips(false);
}
