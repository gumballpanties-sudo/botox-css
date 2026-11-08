#include "misc_overlays.h"
#include "names.h"                     // Names_IsReviveTarget (auto revive modes 2/3)
#include "../movement/prediction.h"   // TickInterval
#include "../../render/dpi.h"          // botox logger: queued stretch block (dpi scale panels)
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

// ==== healthshot effect =========================================================================
// botox on_healthshot: needs the trigger ticked, a live local player, duration clamped 0.1..5 s.
static std::atomic<unsigned long long> s_hsStart{ 0 }, s_hsEnd{ 0 };

void Healthshot_Trigger(int trigger)
{
    if (!g_hshotEnable || trigger < 0 || trigger > 2 || !(g_hshotTriggers & (1 << trigger))) return;
    if (!IsLocalPlayerAlive()) return;
    const unsigned long long now = GetTickCount64();
    const float duration = std::clamp(g_hshotDuration, 0.1f, 5.f);
    s_hsStart.store(now);
    s_hsEnd.store(now + (unsigned long long)(duration * 1000.f));
}

// CS:GO's effects/healthboost is a screen overlay over the edges for the boost time; TF2 has no
// such material, so: a blue-white edge glow that eases in over 0.15 s, breathes, and eases out
// over the last 40 %. Drawn on the background list = under every other overlay.
static void RenderHealthshot(ImDrawList* dl, const ImVec2& screen)
{
    const unsigned long long now = GetTickCount64(), start = s_hsStart.load(), end = s_hsEnd.load();
    if (!g_hshotEnable || now >= end || end <= start) return;
    if (!IsLocalPlayerAlive()) { s_hsEnd.store(0); return; }   // dead = the boost is gone

    const float total = (float)(end - start) / 1000.f, age = (float)(now - start) / 1000.f;
    const float t = age / total;
    float k = std::min(age / 0.15f, 1.f) * (t > 0.6f ? (1.f - t) / 0.4f : 1.f);
    k *= 0.85f + 0.15f * sinf(age * 6.2831853f * 1.5f);
    if (k <= 0.f) return;

    const float edge = std::min(screen.x, screen.y) * 0.22f;
    const ImU32 on  = IM_COL32(110, 190, 255, (int)(150.f * k));
    const ImU32 off = IM_COL32(110, 190, 255, 0);
    dl->AddRectFilledMultiColor(ImVec2(0.f, 0.f), ImVec2(screen.x, edge), on, on, off, off);
    dl->AddRectFilledMultiColor(ImVec2(0.f, screen.y - edge), screen, off, off, on, on);
    dl->AddRectFilledMultiColor(ImVec2(0.f, 0.f), ImVec2(edge, screen.y), on, off, off, on);
    dl->AddRectFilledMultiColor(ImVec2(screen.x - edge, 0.f), screen, off, on, on, off);
}

// ==== low fps warning: botox n_indicators::fps_warning ==========================================
static void RenderFpsWarning(ImDrawList* dl, const ImVec2& screen)
{
    static float anim = 0.f;
    const ImGuiIO& io = ImGui::GetIO();
    const int tickrate = (int)(1.f / TickInterval() + 0.5f);
    const bool on = g_fpsWarning && IsInGame() && GetLocalPlayerIndex() > 0;
    const bool active = on && (int)(io.Framerate + 0.5f) < tickrate;
    anim = std::clamp(anim + 2.f * io.DeltaTime * (active ? 2.f : -2.f), 0.f, 1.f);   // ImAnimationHelper Update(2, +-2)
    if (!on || anim <= 0.f) return;

    ImFont* bold = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();
    ImFont* reg  = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();
    const auto shadowed = [&](ImFont* f, float x, float y, ImU32 col, const char* s) {
        dl->AddText(f, f->FontSize, ImVec2(floorf(x) + 1.f, floorf(y) + 1.f), IM_COL32(0, 0, 0, (int)(255.f * anim)), s);
        dl->AddText(f, f->FontSize, ImVec2(floorf(x), floorf(y)), col, s);
    };

    const char* warn = "LOW FPS";
    const ImVec2 ws = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0.f, warn);
    const float cx = floorf(screen.x * 0.5f);
    shadowed(bold, cx - ws.x * 0.5f, 200.f, IM_COL32(255, 255, 255, (int)(255.f * anim)), warn);
    shadowed(bold, cx - ws.x, 200.f, IM_COL32(255, 0, 0, 255), "!");
    shadowed(bold, cx + ws.x, 200.f, IM_COL32(255, 0, 0, 255), "!");

    char line[64];
    snprintf(line, sizeof(line), "current fps: %d needed fps: %d+", (int)(io.Framerate + 0.5f), tickrate);
    const ImVec2 ls = reg->CalcTextSizeA(reg->FontSize, FLT_MAX, 0.f, line);
    shadowed(reg, cx - ls.x * 0.5f, 215.f, IM_COL32(178, 178, 178, (int)(255.f * anim)), line);
}

// ==== displayed logs: botox g_logger + the [prefix] console line ================================
namespace
{
    struct LogLine { std::string text, prefix; float time; };
    std::mutex s_logMtx;               // FireEvent thread pushes, the frame build reads
    std::deque<LogLine> s_logs;

    // IVEngineClient::GetPlayerInfo slot 8, TF2 player_info_t (same layout spectator_list.cpp reads)
    struct LogPlayerInfo_t
    {
        char name[32]; int userID; char guid[33]; uint32_t friendsID; char friendsName[32];
        bool fakeplayer; bool ishltv; uint32_t customFiles[4]; uint8_t filesDownloaded;
    };
    std::string PlayerName(int idx)
    {
        LogPlayerInfo_t pi = {};
        if (!g_engine || idx < 1 || !VCall<bool>(g_engine, 8, idx, &pi)) return "unknown";
        pi.name[sizeof(pi.name) - 1] = 0;
        return std::string(pi.name).substr(0, 24);
    }

    int TeamOf(int idx)
    {
        void* e = GetClientEntity(idx);
        if (!e || g_offTeamNum <= 0) return -1;
        __try { return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(e) + g_offTeamNum); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    // IVEngineClient::ClientCmd_Unrestricted, slot 106 -- own copy, same verified call as
    // menu.cpp / game_extras.cpp / fakemove.inl / practice.inl (each file keeps its own, codebase convention).
    void EngineExecCmd(const char* cmd)
    {
        if (!g_engine) return;
        __try {
            void** vt = *reinterpret_cast<void***>(g_engine);
            if (reinterpret_cast<uintptr_t>(vt) > 0x10000 && reinterpret_cast<uintptr_t>(vt[106]) > 0x10000)
                ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void Log(const char* prefix, const std::string& screen, const std::string& console)
    {
        {
            std::lock_guard<std::mutex> lock(s_logMtx);
            s_logs.push_back({ screen, prefix, 8.f });
        }
        // botox: accent "[prefix] " then the line in console text colour
        ConsoleColorPrint((uint8_t)(g_accentColor[0] * 255.f), (uint8_t)(g_accentColor[1] * 255.f),
                          (uint8_t)(g_accentColor[2] * 255.f), (std::string(prefix) + " ").c_str());
        ConsoleColorPrint(200, 200, 200, (console + "\n").c_str());
        DebugLog("LOG %s %s", prefix, screen.c_str());
    }
}

// ==== hit sound: botox hitmarker.cpp on_hit_sound() / movement.cpp play_trick_sound() ==========
// botox plays through IEngineSound::EmitAmbientSound (stacked per 1.0 for volume > 1), falling
// back to ISurface::PlaySound. Neither interface is resolved in this build yet (NEEDS FROM
// OTHERS, same gap pixelsurf.inl / movement_tricks2.inl already flagged): "play <path>" through
// the verified ClientCmd_Unrestricted call is what every other trick-sound site in this repo
// already does, so this follows suit. Stock paths = pixelsurf.inl's kPsSounds family (already
// verified present in CSS's vpks by that port), reused here so no new unverified path is added.
void HitSound_Trigger()
{
    if (!g_hitSound) return;
    static const char* const kStock[] = { "buttons/lever7.wav", "buttons/button9.wav", "buttons/bell1.wav", "buttons/blip1.wav" };
    const char* path = nullptr;
    if (g_hitSoundType >= 0 && g_hitSoundType <= 3) path = kStock[g_hitSoundType];
    else if (g_hitSoundType == 4) path = g_hitSoundCustom[0] ? g_hitSoundCustom : nullptr;
    if (!path) return;
    // "playvol <sound> <vol>" (CSS engine.dll console cmd), stacked per 1.0 like botox emit_ambient_sound
    for (float left = std::clamp(g_hitSoundVolume, 0.f, 2.f); left > 0.001f; left -= 1.f)
    {
        char cmd[192];
        snprintf(cmd, sizeof(cmd), "playvol %s %.2f", path, left < 1.f ? left : 1.f);
        EngineExecCmd(cmd);
    }
}

// ==== auto revive: botox fire_event_intern player_death "sm_respawn #<userid>" 1:1 =============
// Needs a SourceMod-admin server (same real-world dependency botox's own call has). Modes 2/3
// ("only targets" / "yourself and targets") need a per-player revive-target pick: botox's own
// is misc > player list's "auto revive" column (menu_tab_settings.cpp player_list_page). This
// build's player list page (menu_tab_misc.cpp Page_MiscPlayerList) is the alias list only, no
// revive-target grid yet -- NEEDS FROM OTHERS (whoever extends that page). Until then the
// target set is always empty, so mode 2 correctly revives nobody and mode 3 == "yourself".
void AutoRevive_OnPlayerDeath(int victimIdx, int localIdx, int userId)
{
    if (!g_autoRevive || victimIdx <= 0) return;
    const bool isSelf = victimIdx == localIdx;
    const bool isTarget = Names_IsReviveTarget(victimIdx);   // misc > player list "auto revive" column
    const int mode = g_autoReviveMode;
    const bool revive = mode == 0 || (isSelf && (mode == 1 || mode == 3)) || (isTarget && (mode == 2 || mode == 3));
    if (!revive) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "sm_respawn #%d", userId);
    EngineExecCmd(cmd);
}

// ==== auto buy: botox misc.cpp auto_buy() 1:1 gating/ordering, CSS weapon set scoped down ======
// CSS 1.6-era classnames (cs_weapon_parse.cpp WeaponFileInfo list): no ssg08/sg553/mp9/mp7/
// bizon/nova/sawedoff/mag7/m249/negev/auto-sniper/tec9/zeus -- those botox slots are dropped.
// {T, CT} pairs, side index 0 = T (team 2), 1 = CT (team 3).
//
// Gating netvars (own local resolve, AGENT_BRIEF-sanctioned stopgap: "read a netvar locally in
// your own file via the existing netvar helper"; same FindNetworkTable/FindPropOffset globals.h
// already exposes, same DT_CSPlayer table hooks/netvars.cpp resolves, same 0=unresolved/
// -1=not-found/>0=offset sentinel convention). Confirmed send-table names, CCSPlayer ctor
// (css-community-master game/server/cstrike/cs_player.cpp:279-295) and client header types
// (game/client/cstrike/c_cs_player.h:210,286-291): m_bInBuyZone (bool), m_ArmorValue (int),
// m_bHasHelmet (bool). m_iAccount / m_bHasDefuser resolved too (botox reads account for its
// debug log and has_defuser() for the extras gate) though not required for the buy-zone gate.
namespace
{
    int g_offInBuyZone = 0, g_offArmorValue = 0, g_offHasHelmet = 0, g_offHasDefuser = 0, g_offAccount = 0;

    void AutoBuy_ResolveNetvars()
    {
        static bool done = false;
        if (done) return;
        RecvTable* pCSPlayer = FindNetworkTable("DT_CSPlayer");
        if (!pCSPlayer) return;   // not ready yet -- retry next tick (sentinels stay 0)
        const auto find = [&](const char* name) { const int off = FindPropOffset(pCSPlayer, name); return off > 0 ? off : -1; };
        g_offInBuyZone  = find("m_bInBuyZone");
        g_offArmorValue = find("m_ArmorValue");
        g_offHasHelmet  = find("m_bHasHelmet");
        g_offHasDefuser = find("m_bHasDefuser");
        g_offAccount    = find("m_iAccount");
        done = true;
        DebugLog("autobuy netvars: buyzone=0x%X armor=0x%X helmet=0x%X defuser=0x%X account=0x%X",
                 g_offInBuyZone, g_offArmorValue, g_offHasHelmet, g_offHasDefuser, g_offAccount);
    }

    bool InBuyZone(void* ent) { return g_offInBuyZone > 0 && ent && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(ent) + g_offInBuyZone); }
    int  ArmorValue(void* ent) { return (g_offArmorValue > 0 && ent) ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(ent) + g_offArmorValue) : -1; }
    bool HasHelmet(void* ent) { return g_offHasHelmet > 0 && ent && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(ent) + g_offHasHelmet); }
    bool HasDefuser(void* ent) { return g_offHasDefuser > 0 && ent && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(ent) + g_offHasDefuser); }

    // armed by AutoBuy_OnRoundStart, ticked by AutoBuy_Tick (RenderMiscOverlays, every frame):
    // botox's own "3 tries 0.5s apart, buy-zone gated" retry loop (misc.cpp auto_buy():84-93).
    struct { bool armed = false; int tries = 0; unsigned long long next = 0; } s_ab;
}

void AutoBuy_OnRoundStart(int localIdx)
{
    if (!g_autoBuy || localIdx < 1) return;
    s_ab.armed = true;
    s_ab.tries = 0;
    s_ab.next = 0;
}

void AutoBuy_Tick()
{
    if (!s_ab.armed) return;
    if (!g_autoBuy) { s_ab.armed = false; return; }

    const int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1 || !IsLocalPlayerAlive()) { s_ab.tries = 0; return; }   // botox: dead resets tries, never fires

    void* ent = GetClientEntity(localIdx);
    const int team = TeamOf(localIdx);
    if (!ent || (team != 2 && team != 3)) return;

    AutoBuy_ResolveNetvars();
    if (!InBuyZone(ent)) return;   // botox ab_gate("not in buy zone")
    if (s_ab.tries >= 3) { s_ab.armed = false; return; }   // botox ab_gate("3 tries used")

    const unsigned long long now = GetTickCount64();
    if (now < s_ab.next) return;

    const int side = team == 3 ? 1 : 0;   // 0 = T, 1 = CT
    static const char* const kPrimary[][2] = {
        { "ak47", "m4a1" }, { "awp", "awp" }, { "scout", "scout" }, { "sg552", "aug" },
        { "galil", "famas" }, { "mac10", "tmp" }, { "ump45", "ump45" }, { "p90", "p90" },
        { "m3", "m3" }, { "xm1014", "xm1014" },
    };
    static const char* const kSecondary[][2] = { { "p228", "p228" }, { "deagle", "deagle" }, { "elite", "fiveseven" } };
    static const char* const kGrenadeNames[] = { "hegrenade", "flashbang", "flashbang", "smokegrenade" };

    std::string cmd;
    const auto add = [&](const char* w) { if (!cmd.empty()) cmd += "; "; cmd += "buy "; cmd += w; };

    const int primary = g_autoBuyPrimary - 1;
    if (primary >= 0 && primary < (int)(sizeof(kPrimary) / sizeof(kPrimary[0]))) add(kPrimary[primary][side]);

    // armor: skip re-asking once it would be a no-op (botox: get_armor() < 100 / !has_helmet())
    const int armor = ArmorValue(ent);
    if (g_autoBuyArmor == 1 && armor < 100) add("vest");
    else if (g_autoBuyArmor == 2 && (armor < 100 || !HasHelmet(ent))) add("vesthelm");

    const int secondary = g_autoBuySecondary - 1;
    if (secondary >= 0 && secondary < (int)(sizeof(kSecondary) / sizeof(kSecondary[0]))) add(kSecondary[secondary][side]);

    // menu order: he, one flash, two flashes, smoke (index 2 = second flashbang). No per-grenade
    // ammo-count read here (botox's own uses ammo offsets this build hasn't resolved for
    // grenades specifically) -- "buy" past the server's per-round cap is a harmless no-op.
    for (int i = 0; i < 4; ++i)
        if (g_autoBuyGrenades & (1 << i)) add(kGrenadeNames[i]);

    if ((g_autoBuyExtras & 1) && side == 1 && !HasDefuser(ent)) add("defuser");   // CT-only

    // Weapon-already-owned skip for primary/secondary needs each m_hMyWeapons[] slot's
    // classname (a GetClientClass() vtable call this build has no verified slot for -- NEEDS
    // FROM OTHERS / AGENT_BRIEF "no guessed slots"). Re-sending "buy ak47" to an owner is a
    // harmless server no-op (idempotent, no double spend), so this is a documented
    // simplification, not a correctness gap: worst case is a few redundant bytes, never a
    // wrong buy or a crash.
    if (cmd.empty()) { s_ab.armed = false; return; }

    EngineExecCmd(cmd.c_str());
    ++s_ab.tries;
    s_ab.next = now + 500;
    DebugLog("AB: send try=%d account=%d '%s'", s_ab.tries, g_offAccount > 0 ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(ent) + g_offAccount) : -1, cmd.c_str());
}

void MiscLogs_OnGameEvent(void* pEvent, const char* name, int localIdx)
{
    // healthshot trigger 0 + auto revive (botox fire_event_intern player_death 1:1): both run
    // regardless of g_logTypes (that gate is the log feature's own, not these).
    if (strcmp(name, "player_death") == 0)
    {
        const int victim   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
        const int attacker = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
        if (attacker == localIdx && victim > 0 && victim != localIdx) Healthshot_Trigger(0);
        AutoRevive_OnPlayerDeath(victim, localIdx, IGameEventVtbl::GetInt(pEvent, "userid", 0));
        return;
    }

    // hit sound (botox hitmarker.cpp on_hit_sound, fired off player_hurt like the hit marker):
    // independent of g_logTypes, so check it before that gate.
    if (strcmp(name, "player_hurt") == 0)
    {
        const int victim   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
        const int attacker = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
        if (attacker == localIdx && victim > 0 && victim != localIdx) HitSound_Trigger();
    }

    if (!g_logTypes) return;

    // CS:S player_hurt (modevents.res, confirmed unitE_events.cpp): userid, attacker, health,
    // armor, weapon, dmg_health, dmg_armor, hitgroup.
    if (strcmp(name, "player_hurt") == 0)
    {
        const int victim   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
        const int attacker = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
        if (attacker < 1 || victim < 1 || attacker == victim) return;
        const bool teamHit = TeamOf(attacker) == TeamOf(victim);
        // bit 0 damage (enemy hits), bit 1 team damage, bit 2 votes (MultiCombo index order)
        if (teamHit ? !(g_logTypes & 2) : !(g_logTypes & 1)) return;
        const int dmg = IGameEventVtbl::GetInt(pEvent, "dmg_health", 0);
        const int hp  = IGameEventVtbl::GetInt(pEvent, "health", 0);
        const char* tag = teamHit ? "[team]" : attacker == localIdx ? "[damage]" : "[hurt]";
        char s[160], c[200];
        if (victim == localIdx)
        {
            const std::string who = PlayerName(attacker);
            snprintf(s, sizeof(s), "%s%s hit you for %d", teamHit ? "(team) " : "", who.c_str(), dmg);
            snprintf(c, sizeof(c), "%s%s hit you | dmg: %d hp | %d health remaining", teamHit ? "(team) " : "", who.c_str(), dmg, hp);
            Log(tag, s, c);
        }
        else if (attacker == localIdx)
        {
            const std::string who = PlayerName(victim);
            snprintf(s, sizeof(s), "%shit %s for %d", teamHit ? "(team) " : "", who.c_str(), dmg);
            snprintf(c, sizeof(c), "%shit %s | dealt: %d hp | %d health remaining", teamHit ? "(team) " : "", who.c_str(), dmg, hp);
            Log(tag, s, c);
        }
        return;
    }

    // vote_cast: vote_option, team, entityid (c_vote_controller.cpp:173 / hud_vote.cpp)
    if ((g_logTypes & 4) && strcmp(name, "vote_cast") == 0)
    {
        const int who = IGameEventVtbl::GetInt(pEvent, "entityid", 0);
        const int opt = IGameEventVtbl::GetInt(pEvent, "vote_option", -1);
        const std::string n = PlayerName(who);
        char s[160];
        if (opt == 0 || opt == 1) snprintf(s, sizeof(s), "%s voted %s", n.c_str(), opt == 0 ? "yes" : "no");
        else                      snprintf(s, sizeof(s), "%s voted option %d", n.c_str(), opt + 1);
        Log("[vote]", s, s);
    }
}

// botox n_logger::on_paint_traverse: x 10, y 5 (m_last_spectators_y default), 15 px rows, 8 s,
// last 0.5 s fades and the top row slides up through its last 0.1 s, max 16 kept.
static void RenderLogs(ImDrawList* dl)
{
    std::lock_guard<std::mutex> lock(s_logMtx);
    const float dt = ImGui::GetIO().DeltaTime;
    for (size_t i = 0; i < s_logs.size();)
    {
        s_logs[i].time -= dt;
        if (s_logs[i].time <= 0.f || i > 15) { s_logs.erase(s_logs.begin() + i); continue; }
        ++i;
    }
    if (s_logs.empty()) return;
    DpiPanelScope block(dl);

    ImFont* bold = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();
    ImFont* reg  = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();
    const float x = 10.f, size = 15.f;
    float y = 5.f;
    for (size_t i = 0; i < s_logs.size(); ++i)
    {
        const LogLine& l = s_logs[i];
        float a = 1.f;
        if (l.time < 0.5f)
        {
            a = std::clamp(l.time, 0.f, 0.5f) / 0.5f;
            if (i == 0 && a < 0.2f) y -= size * (1.f - a / 0.2f);
        }
        const ImU32 shadow = IM_COL32(0, 0, 0, (int)(255.f * a));
        const ImU32 accent = ImColor(g_accentColor[0], g_accentColor[1], g_accentColor[2], a);
        dl->AddText(bold, bold->FontSize, ImVec2(x + 1.f, floorf(y) + 1.f), shadow, l.prefix.c_str());
        dl->AddText(bold, bold->FontSize, ImVec2(x, floorf(y)), accent, l.prefix.c_str());
        const float px = x + bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0.f, l.prefix.c_str()).x + 3.f;
        dl->AddText(reg, reg->FontSize, ImVec2(px + 1.f, floorf(y) + 1.f), shadow, l.text.c_str());
        dl->AddText(reg, reg->FontSize, ImVec2(px, floorf(y)), IM_COL32(255, 255, 255, (int)(255.f * a)), l.text.c_str());
        y += size;
    }
}

void RenderMiscOverlays()
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    if (screen.x <= 0.f || screen.y <= 0.f) return;
    RenderHealthshot(ImGui::GetBackgroundDrawList(), screen);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    RenderFpsWarning(fg, screen);
    RenderLogs(fg);
    AutoBuy_Tick();
}

// ==== console color: botox draw_set_color.cpp ===================================================
// ISurface::DrawSetColor(int, int, int, int) = slot 11: ref ISurface.h, IAppSystem's 5 then
// Shutdown..PopMakeCurrent, the two DrawSetColor overloads at 10/11 in MSVC's reversed overload
// order (Color at 10). Cross-check: the same count puts SetCursor at 51 and LockCursor at 62,
// exactly the slots ref hooks (Hooks/Direct3DDevice9.cpp).
namespace
{
    using DrawSetColor_t = void(__fastcall*)(void*, int, int, int, int);
    DrawSetColor_t o_DrawSetColor = nullptr;
    bool s_consoleDrawing = false;      // vgui paint thread only, set around the console's PaintTraverse
    uintptr_t s_consolePanel = 0, s_completionPanel = 0;

    void __fastcall h_DrawSetColor(void* surface, int r, int g, int b, int a)
    {
        if (s_consoleDrawing && g_consoleTintOn && !g_eject)
        {
            r = r * (int)(g_consoleTint[0] * 255.f) / 255;
            g = g * (int)(g_consoleTint[1] * 255.f) / 255;
            b = b * (int)(g_consoleTint[2] * 255.f) / 255;
            a = a * (int)(g_consoleTint[3] * 255.f) / 255;
        }
        o_DrawSetColor(surface, r, g, b, a);
    }
}

void ConsoleTint_Install()
{
    static const char* const kNames[] = { "VGUI_Surface031", "VGUI_Surface030", nullptr };
    void* surface = GetInterfaceAny("vguimatsurface.dll", kNames);
    if (!surface) { DebugLog("console color: VGUI_Surface not found"); return; }
    InstallVTableHook(surface, 11, "console color: DrawSetColor", (void*)&h_DrawSetColor, (void**)&o_DrawSetColor);
}

// botox paint_traverse: GameConsole + CompletionList (the console's autocomplete popup)
bool ConsoleTint_Begin(void* panels, uintptr_t vpanel)
{
    if (!g_consoleTintOn || !o_DrawSetColor || !panels) return false;
    if (vpanel != s_consolePanel && vpanel != s_completionPanel)
    {
        if (s_consolePanel && s_completionPanel) return false;
        // IPanel::GetName, slot 36 (render.cpp VGui_PanelName)
        using GetName_t = const char* (*)(void*, uintptr_t);
        const char* nm = ((GetName_t)(*(void***)panels)[36])(panels, vpanel);
        if (!nm) return false;
        if (!s_consolePanel && strcmp(nm, "GameConsole") == 0) { s_consolePanel = vpanel; DebugLog("console color: GameConsole panel found"); }
        else if (!s_completionPanel && strcmp(nm, "CompletionList") == 0) s_completionPanel = vpanel;
        else return false;
    }
    s_consoleDrawing = true;
    return true;
}

void ConsoleTint_End() { s_consoleDrawing = false; }
