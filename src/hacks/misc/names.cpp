#include "names.h"
#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "../movement/prediction.h"   // g_globalVars -- curtime for the cycle timer
#include "../../dependencies/imgui/imgui.h"
#include <atomic>
#include <map>

GetPlayerName_t o_GetPlayerName = nullptr;
NameList g_nameList, g_teamLabelList, g_enemyLabelList, g_everyoneLabelList;

namespace
{
    // TF2 x64 player_info_t (aim/defs.h player_info_s; game_extras.cpp uses the same offsets)
    constexpr int kPiName = 0x00, kPiNameSize = 32, kPiFriendsId = 0x48, kPiSize = 0x84;

    // ---- aliases: fpsoverlay g_aliases, keyed by account id (steam64 low half) ----
    // Menu/cfg (render thread) write, the hooks (game thread) read: every touch under the lock
    // (a rehash under a concurrent find is a crash), and no engine call while holding it.
    struct Alias { char name[64]; };
    SRWLOCK                    s_aliasLock = SRWLOCK_INIT;
    std::map<uint32_t, Alias>  s_aliases;          // sorted = stable menu order
    std::atomic<int>           s_aliasCount{ 0 };  // lock-free "any at all" for the hot path

    // ---- auto revive targets: botox misc > player list "auto revive" column, keyed by
    // account id (same shape as aliases above). Menu (render thread) writes, FireEvent
    // (game thread, AutoRevive_OnPlayerDeath) reads -- same lock discipline as aliases.
    SRWLOCK                 s_reviveLock = SRWLOCK_INIT;
    std::map<uint32_t, bool> s_reviveTargets;
    std::atomic<int>        s_reviveCount{ 0 };   // lock-free "any at all", same shape as s_aliasCount

    // ---- aimbot ignore / only aimbot: botox menu_tab_settings.cpp player_list_page's two
    // other columns (misc.cpp player_list_aim_allowed). Same shape as revive targets above --
    // one lock + one map per column, menu (render thread) writes, aim_global.cpp's ShouldIgnore
    // (game thread) reads.
    SRWLOCK                 s_aimIgnoreLock = SRWLOCK_INIT;
    std::map<uint32_t, bool> s_aimIgnoreTargets;
    std::atomic<int>        s_aimIgnoreCount{ 0 };
    SRWLOCK                 s_aimOnlyLock = SRWLOCK_INIT;
    std::map<uint32_t, bool> s_aimOnlyTargets;
    std::atomic<int>        s_aimOnlyCount{ 0 };

    // last unmasked name per slot (after bot names, before us) -- the alias name hint and the
    // player list's "real name" column
    char s_real[65][kPiNameSize] = {};

    bool AliasFor(uint32_t account, char* out, size_t size)
    {
        if (!account || s_aliasCount.load(std::memory_order_relaxed) == 0) return false;
        AcquireSRWLockShared(&s_aliasLock);
        const auto it = s_aliases.find(account);
        const bool hit = it != s_aliases.end() && it->second.name[0];
        if (hit) strncpy_s(out, size, it->second.name, _TRUNCATE);
        ReleaseSRWLockShared(&s_aliasLock);
        return hit;
    }

    int PlayerTeam(int index)
    {
        void* ent = index > 0 ? GetClientEntity(index) : nullptr;
        return (ent && g_offTeamNum > 0) ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(ent) + g_offTeamNum) : 0;
    }

    // fpsoverlay: timer ticks a list only when it is read
    const char* FromList(const char* fixed, bool cycle, NameList& l)
    {
        if (cycle && l.count > 0 && g_nameTrigger == 0 && g_globalVars)
            n_names::Tick(l.cycle, l.count, g_globalVars->curtime, g_nameInterval);
        return n_names::Choose(fixed, cycle, l.cycle, l.count, [&l](int i) { return l.text[i]; });
    }

    // fpsoverlay GetStreamedPlayerName. nullptr / "" = real name.
    const char* Streamed(int index)
    {
        const int local = GetLocalPlayerIndex();
        if (index == local)   // streamer mode never masks you; custom name still applies
            return g_useCustomUsername ? FromList(g_customUsername, g_enableNameCycle, g_nameList) : nullptr;

        // botox "others shown as": team labels / one label (2 modes). TF2 port's "their class"
        // mode removed (DE-TF2 rule, CS has no per-class playermodel) -- menu_tab_misc.cpp renumbered to match.
        switch (g_streamerMode)
        {
        case 1: {
            // local -1 = not in game; GetClientEntity has no negative-index check
            if (local < 1) return "player";
            const int lt = PlayerTeam(local), pt = PlayerTeam(index);
            if (lt == 0 || pt == 0) return "player";
            return pt == lt ? FromList(g_streamerTeamLabel, g_enableTeamLabelCycle, g_teamLabelList)
                            : FromList(g_streamerEnemyLabel, g_enableEnemyLabelCycle, g_enemyLabelList);
        }
        case 2:
            return FromList(g_streamerEveryoneLabel, g_enableEveryoneLabelCycle, g_everyoneLabelList);
        default:
            return nullptr;
        }
    }

    // fpsoverlay GetPlayerName_Masked order: alias, then streamer / custom name. false = real.
    bool Resolve(int index, uint32_t account, char* out, size_t size)
    {
        if (AliasFor(account, out, size)) return true;
        if (!g_useCustomUsername && g_streamerMode == 0) return false;
        const char* s = Streamed(index);
        if (!s || !s[0]) return false;
        strncpy_s(out, size, s, _TRUNCATE);
        return true;
    }

    __declspec(noinline) bool Resolve_SEH(int index, uint32_t account, char* out, size_t size)
    {
        __try { return Resolve(index, account, out, size); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // account id of a slot. Goes through the (hooked) GetPlayerInfo, which also refreshes s_real.
    // Only paid while any alias OR revive target exists, like fpsoverlay's GetPlayerFriendsID per call.
    __declspec(noinline) uint32_t AccountOf(int index)
    {
        if (!g_engine || (s_aliasCount.load(std::memory_order_relaxed) == 0 && s_reviveCount.load(std::memory_order_relaxed) == 0)) return 0;
        alignas(8) uint8_t pi[0x100] = {};
        __try {
            if (!VCall<bool>(g_engine, 8, index, (void*)pi)) return 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        uint32_t account = 0;
        memcpy(&account, pi + kPiFriendsId, sizeof(account));
        return account;
    }

    // HUD reads the returned pointer right away; one slot per player like fpsoverlay g_nameCache
    char s_shown[65][128] = {};
}

const char* __fastcall h_GetPlayerName(void* self, int index)
{
    MiscHookGuard _g;
    const char* real = o_GetPlayerName(self, index);
    if (g_eject || index < 1 || index > 64) return real;
    return Resolve_SEH(index, AccountOf(index), s_shown[index], sizeof(s_shown[index])) ? s_shown[index] : real;
}

void Names_PatchInfo(int index, void* pinfo)
{
    if (!pinfo || index < 1 || index > 64) return;
    char* name = static_cast<char*>(pinfo) + kPiName;
    strncpy_s(s_real[index], name, _TRUNCATE);

    uint32_t account = 0;
    memcpy(&account, static_cast<uint8_t*>(pinfo) + kPiFriendsId, sizeof(account));

    char buf[128];
    if (!Resolve_SEH(index, account, buf, sizeof(buf))) return;
    memset(name, 0, kPiNameSize);
    memcpy(name, buf, strnlen(buf, kPiNameSize - 1));
}

void Names_OnGameEvent(void* pEvent)
{
    if (!pEvent || g_nameTrigger != 1) return;
    __try {
        const char* name = IGameEventVtbl::GetName(pEvent);
        if (!name || strcmp(name, "player_death") != 0) return;
        const int local    = GetLocalPlayerIndex();
        const int attacker = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
        const int victim   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
        if (local < 1 || attacker != local || victim == local) return;
        // fpsoverlay h_FireEvent on-kill block, 1:1
        if (g_useCustomUsername && g_enableNameCycle) n_names::Step(g_nameList.cycle, g_nameList.count);
        if (g_streamerMode == 1)
        {
            if (g_enableTeamLabelCycle)  n_names::Step(g_teamLabelList.cycle, g_teamLabelList.count);
            if (g_enableEnemyLabelCycle) n_names::Step(g_enemyLabelList.cycle, g_enemyLabelList.count);
        }
        if (g_streamerMode == 2 && g_enableEveryoneLabelCycle)
            n_names::Step(g_everyoneLabelList.cycle, g_everyoneLabelList.count);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- aliases ----
bool Names_HasAlias(uint32_t account)
{
    if (!account) return false;
    AcquireSRWLockShared(&s_aliasLock);
    const bool has = s_aliases.count(account) != 0;
    ReleaseSRWLockShared(&s_aliasLock);
    return has;
}

// fpsoverlay PlayerList_AddOrMark: new entry starts as the player's real name
void Names_AddAlias(uint32_t account, int index)
{
    if (!account) return;
    char hint[kPiNameSize] = {};
    if (index >= 1 && index <= 64) strncpy_s(hint, s_real[index], _TRUNCATE);
    AcquireSRWLockExclusive(&s_aliasLock);
    if (!s_aliases.count(account))
    {
        Alias& a = s_aliases[account];
        strncpy_s(a.name, hint, _TRUNCATE);
    }
    s_aliasCount = (int)s_aliases.size();
    ReleaseSRWLockExclusive(&s_aliasLock);
}

// ---- auto revive targets (botox misc > player list "auto revive" column) -----------------
bool Names_IsReviveTargetAccount(uint32_t account)
{
    if (!account || s_reviveCount.load(std::memory_order_relaxed) == 0) return false;
    AcquireSRWLockShared(&s_reviveLock);
    const auto it = s_reviveTargets.find(account);
    const bool on = it != s_reviveTargets.end() && it->second;
    ReleaseSRWLockShared(&s_reviveLock);
    return on;
}

bool Names_IsReviveTarget(int index)
{
    return Names_IsReviveTargetAccount(AccountOf(index));
}

void Names_SetReviveTarget(uint32_t account, bool on)
{
    if (!account) return;
    AcquireSRWLockExclusive(&s_reviveLock);
    s_reviveTargets[account] = on;
    s_reviveCount = (int)s_reviveTargets.size();
    ReleaseSRWLockExclusive(&s_reviveLock);
}

// ---- aimbot ignore / only aimbot (botox player_list_page + misc.cpp player_list_aim_allowed) ----
bool Names_IsAimIgnoreAccount(uint32_t account)
{
    if (!account || s_aimIgnoreCount.load(std::memory_order_relaxed) == 0) return false;
    AcquireSRWLockShared(&s_aimIgnoreLock);
    const auto it = s_aimIgnoreTargets.find(account);
    const bool on = it != s_aimIgnoreTargets.end() && it->second;
    ReleaseSRWLockShared(&s_aimIgnoreLock);
    return on;
}
bool Names_IsAimIgnore(int index) { return Names_IsAimIgnoreAccount(AccountOf(index)); }
void Names_SetAimIgnore(uint32_t account, bool on)
{
    if (!account) return;
    AcquireSRWLockExclusive(&s_aimIgnoreLock);
    s_aimIgnoreTargets[account] = on;
    s_aimIgnoreCount = (int)s_aimIgnoreTargets.size();
    ReleaseSRWLockExclusive(&s_aimIgnoreLock);
}

bool Names_IsAimOnlyAccount(uint32_t account)
{
    if (!account || s_aimOnlyCount.load(std::memory_order_relaxed) == 0) return false;
    AcquireSRWLockShared(&s_aimOnlyLock);
    const auto it = s_aimOnlyTargets.find(account);
    const bool on = it != s_aimOnlyTargets.end() && it->second;
    ReleaseSRWLockShared(&s_aimOnlyLock);
    return on;
}
bool Names_IsAimOnly(int index) { return Names_IsAimOnlyAccount(AccountOf(index)); }
void Names_SetAimOnly(uint32_t account, bool on)
{
    if (!account) return;
    AcquireSRWLockExclusive(&s_aimOnlyLock);
    s_aimOnlyTargets[account] = on;
    s_aimOnlyCount = (int)s_aimOnlyTargets.size();
    ReleaseSRWLockExclusive(&s_aimOnlyLock);
}

// botox misc.cpp player_list_aim_allowed: ignore wins outright; if nobody anywhere has "only aimbot"
// set, everyone not ignored is allowed; if someone does, only accounts with "only" set are allowed.
// account 0 (bot / no steam id) can only ever be filtered by the second branch, same as botox
// (bots can't be individually ignored/only'd -- no persistent key for them).
bool Names_AimAllowed(int index)
{
    const uint32_t account = AccountOf(index);
    if (Names_IsAimIgnoreAccount(account)) return false;
    if (Names_IsAimOnlyAccount(account)) return true;

    if (s_aimOnlyCount.load(std::memory_order_relaxed) == 0) return true;
    AcquireSRWLockShared(&s_aimOnlyLock);
    bool anyOnly = false;
    for (const auto& [acct, on] : s_aimOnlyTargets) if (on) { anyOnly = true; break; }
    ReleaseSRWLockShared(&s_aimOnlyLock);
    return !anyOnly;
}

namespace
{
    struct Row { int index; uint32_t account; int team; bool you; bool bot; char real[kPiNameSize]; };

    // engine side of the player list, no ImGui / std objects here (__try, C2712)
    __declspec(noinline) int ReadPlayers(Row* rows)
    {
        int n = 0;
        if (!g_engine || !IsInGame()) return 0;
        const int local = GetLocalPlayerIndex();
        for (int i = 1; i <= 64; ++i)
        {
            alignas(8) uint8_t pi[0x100] = {};
            bool ok = false;
            __try {
                ok = VCall<bool>(g_engine, 8, i, (void*)pi);   // hooked: refreshes s_real[i]
                if (ok && !pi[0x6D])                            // ishltv
                {
                    Row& r = rows[n];
                    r.index = i;
                    memcpy(&r.account, pi + kPiFriendsId, sizeof(r.account));
                    r.team = PlayerTeam(i);
                    r.you  = i == local;
                    r.bot  = pi[0x6C] != 0 || !r.account;     // fakeplayer
                    strncpy_s(r.real, s_real[i][0] ? s_real[i] : (const char*)pi, _TRUNCATE);
                    ++n;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return n;
    }
}

// fpsoverlay MenuEntryList 1:1: a box + X per row, "+ add" under them while room is left
// botox InputText ignores item width (spans to WorkRect.Max.x, pushes itself 10 px down, frame
// 17): only a table cell narrows it -- the menu.cpp cvars page layout, same numbers.
namespace
{
    constexpr float kFrame = 17.f, kInputLift = 10.f, kRemoveW = 16.f;

    bool BeginRows(const char* id, int columns)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 0.f));
        const bool open = ImGui::BeginTable(id, columns);
        ImGui::PopStyleVar();
        return open;
    }
    void InputCell() { ImGui::SetCursorPosY(ImGui::GetCursorPosY() - kInputLift); }
    void ButtonCell() { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f); }
    void TextCell() { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f); }
}

void Names_EntryList(const char* id, NameList& l, const char* addLabel)
{
    ImGui::PushID(id);
    int remove = -1;
    if (l.count > 0 && BeginRows("##rows", 2))
    {
        ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, kRemoveW + 4.f);
        for (int i = 0; i < l.count; ++i)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, kFrame + 4.f);
            ImGui::TableNextColumn();
            InputCell();
            ImGui::InputText("##entry", l.text[i], sizeof(l.text[i]));
            ImGui::TableNextColumn();
            ButtonCell();
            if (ImGui::Button("x", ImVec2(kRemoveW, kFrame - 4.f))) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0)
    {
        for (int j = remove; j < l.count - 1; ++j) memcpy(l.text[j], l.text[j + 1], sizeof(l.text[j]));
        l.text[l.count - 1][0] = '\0';
        --l.count;
        if (l.cycle.idx >= l.count) l.cycle.idx = 0;
    }
    if (l.count < n_names::kMaxNames && ImGui::Button(addLabel, ImVec2(-1.f, 15.f)))
        l.text[l.count++][0] = '\0';
    ImGui::PopID();
}

// misc > player list. Who is on the server (real name, team, alias box -- typing in an empty
// one creates the alias), then saved aliases of players not here. Engine reads first, lock after.
void Names_PlayerListMenu()
{
    Row rows[64];
    const int n = ReadPlayers(rows);

    static char s_new[65][64] = {};   // a not-yet-aliased player's box
    uint32_t removeAccount = 0;
    AcquireSRWLockExclusive(&s_aliasLock);

    ImGui::TextDisabled("custom name = shown instead of theirs everywhere");
    ImGui::TextDisabled("also: right-click a player in the scoreboard > add to alias list");

    auto aliasCells = [&](uint32_t account, char* fresh) {
        ImGui::TableNextColumn();
        InputCell();
        const auto it = s_aliases.find(account);
        if (it != s_aliases.end())
        {
            ImGui::InputTextWithHint("##alias", "custom name", it->second.name, sizeof(it->second.name));
            ImGui::TableNextColumn();
            ButtonCell();
            if (ImGui::Button("x", ImVec2(kRemoveW, kFrame - 4.f))) removeAccount = account;
        }
        else
        {
            if (fresh && ImGui::InputTextWithHint("##alias", "custom name", fresh, 64) && fresh[0])
            {
                strncpy_s(s_aliases[account].name, fresh, _TRUNCATE);
                fresh[0] = '\0';
            }
            ImGui::TableNextColumn();
        }
    };

    // botox player_list_page header: name | team | auto revive | aimbot ignore | only aimbot |
    // custom name (menu_tab_settings.cpp:902). Gate: aim_global.cpp ShouldIgnore -> Names_AimAllowed.
    if (BeginRows("##players", 7))
    {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("##team", ImGuiTableColumnFlags_WidthFixed, 34.f);
        ImGui::TableSetupColumn("##revive", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("##ignore", ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("##only", ImGuiTableColumnFlags_WidthFixed, 72.f);
        ImGui::TableSetupColumn("##alias", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, kRemoveW + 4.f);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextDisabled("name");
        ImGui::TableNextColumn(); ImGui::TextDisabled("team");
        ImGui::TableNextColumn(); ImGui::TextDisabled("auto revive");
        ImGui::TableNextColumn(); ImGui::TextDisabled("aimbot ignore");
        ImGui::TableNextColumn(); ImGui::TextDisabled("only aimbot");
        ImGui::TableNextColumn(); ImGui::TextDisabled("custom name");
        ImGui::TableNextColumn();

        for (int k = 0; k < n; ++k)
        {
            const Row& r = rows[k];
            ImGui::PushID(r.index);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, kFrame + 4.f);
            ImGui::TableNextColumn();
            TextCell();
            ImGui::Text("%s%s%s", r.bot ? "BOT " : "", r.real, r.you ? " (you)" : "");
            ImGui::TableNextColumn();
            TextCell();
            if (r.team == 2)      ImGui::TextColored(ImVec4(0.72f, 0.24f, 0.22f, 1.f), "T");
            else if (r.team == 3) ImGui::TextColored(ImVec4(0.35f, 0.48f, 0.66f, 1.f), "CT");
            else                  ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            TextCell();
            if (!r.account)   // bot / no steam id: account 0 can't key the revive map, same
                ImGui::TextDisabled("-");   // exclusion aliasCells already applies below
            else
            {
                bool target = Names_IsReviveTargetAccount(r.account);
                if (ImGui::Checkbox("##revive", &target)) Names_SetReviveTarget(r.account, target);
            }

            ImGui::TableNextColumn();
            TextCell();
            // botox player_list_page: "aimbot never targets you" -- dash like a teamless team cell
            if (r.you || !r.account)
                ImGui::TextDisabled("-");
            else
            {
                bool ignore = Names_IsAimIgnoreAccount(r.account);
                if (ImGui::Checkbox("##ignore", &ignore)) Names_SetAimIgnore(r.account, ignore);
            }

            ImGui::TableNextColumn();
            TextCell();
            if (r.you || !r.account)
                ImGui::TextDisabled("-");
            else
            {
                bool only = Names_IsAimOnlyAccount(r.account);
                if (ImGui::Checkbox("##only", &only)) Names_SetAimOnly(r.account, only);
            }

            if (r.bot)
            {
                ImGui::TableNextColumn();
                TextCell();
                ImGui::TextDisabled("no steam id");
                ImGui::TableNextColumn();
            }
            else
                aliasCells(r.account, s_new[r.index]);
            ImGui::PopID();
        }

        // saved, not on this server
        for (auto& [account, a] : s_aliases)
        {
            bool here = false;
            for (int k = 0; k < n && !here; ++k) here = rows[k].account == account;
            if (here) continue;
            ImGui::PushID((int)account);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, kFrame + 4.f);
            ImGui::TableNextColumn();
            TextCell();
            ImGui::TextDisabled("[U:1:%u]", account);
            ImGui::TableNextColumn();
            TextCell();
            ImGui::TextDisabled("saved");
            ImGui::TableNextColumn();
            TextCell();
            {
                bool target = Names_IsReviveTargetAccount(account);
                if (ImGui::Checkbox("##revive", &target)) Names_SetReviveTarget(account, target);
            }
            ImGui::TableNextColumn();
            TextCell();
            {
                bool ignore = Names_IsAimIgnoreAccount(account);
                if (ImGui::Checkbox("##ignore", &ignore)) Names_SetAimIgnore(account, ignore);
            }
            ImGui::TableNextColumn();
            TextCell();
            {
                bool only = Names_IsAimOnlyAccount(account);
                if (ImGui::Checkbox("##only", &only)) Names_SetAimOnly(account, only);
            }
            aliasCells(account, nullptr);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!n) ImGui::TextDisabled("not in game");

    if (removeAccount) s_aliases.erase(removeAccount);
    s_aliasCount = (int)s_aliases.size();
    ReleaseSRWLockExclusive(&s_aliasLock);
}

// ---- cfg: fpsoverlay keys for the lists, one "alias=<account> <name>" line per alias ----
namespace
{
    struct ListKey { const char* prefix; NameList* list; };
    const ListKey kListKeys[] = {
        { "nameCycle",          &g_nameList },
        { "teamLabelCycle",     &g_teamLabelList },
        { "enemyLabelCycle",    &g_enemyLabelList },
        { "everyoneLabelCycle", &g_everyoneLabelList },
    };
}

void Names_SaveCfg(FILE* f)
{
    fprintf(f, "namesBlock=1\n");   // load: start from empty lists + aliases (cfg switch)
    for (const ListKey& k : kListKeys)
    {
        fprintf(f, "%sCount=%d\n", k.prefix, k.list->count);
        for (int i = 0; i < k.list->count; ++i)
            fprintf(f, "%s%d=%s\n", k.prefix, i, k.list->text[i]);
    }
    AcquireSRWLockShared(&s_aliasLock);
    for (const auto& [account, a] : s_aliases)
        fprintf(f, "alias=%u %s\n", account, a.name);
    ReleaseSRWLockShared(&s_aliasLock);

    AcquireSRWLockShared(&s_reviveLock);
    for (const auto& [account, on] : s_reviveTargets)
        if (on) fprintf(f, "reviveTarget=%u\n", account);
    ReleaseSRWLockShared(&s_reviveLock);
}

bool Names_LoadCfg(const char* key, const char* val)
{
    if (strcmp(key, "namesBlock") == 0)
    {
        for (const ListKey& k : kListKeys) *k.list = NameList{};
        AcquireSRWLockExclusive(&s_aliasLock);
        s_aliases.clear();
        s_aliasCount = 0;
        ReleaseSRWLockExclusive(&s_aliasLock);
        AcquireSRWLockExclusive(&s_reviveLock);
        s_reviveTargets.clear();
        s_reviveCount = 0;
        ReleaseSRWLockExclusive(&s_reviveLock);
        return true;
    }
    if (strcmp(key, "alias") == 0)
    {
        unsigned account = 0;
        int used = 0;
        if (sscanf(val, "%u %n", &account, &used) >= 1 && account)
        {
            AcquireSRWLockExclusive(&s_aliasLock);
            strncpy_s(s_aliases[account].name, val + used, _TRUNCATE);
            s_aliasCount = (int)s_aliases.size();
            ReleaseSRWLockExclusive(&s_aliasLock);
        }
        return true;
    }
    if (strcmp(key, "reviveTarget") == 0)
    {
        const unsigned account = (unsigned)atoi(val);
        if (account) Names_SetReviveTarget(account, true);
        return true;
    }
    for (const ListKey& k : kListKeys)
    {
        const size_t pl = strlen(k.prefix);
        if (strncmp(key, k.prefix, pl) != 0) continue;
        const char* rest = key + pl;
        if (strcmp(rest, "Count") == 0)
        {
            const int c = atoi(val);
            k.list->count = c < 0 ? 0 : (c > n_names::kMaxNames ? n_names::kMaxNames : c);
            return true;
        }
        if (*rest >= '0' && *rest <= '9')
        {
            const int i = atoi(rest);
            if (i >= 0 && i < n_names::kMaxNames) strncpy_s(k.list->text[i], val, _TRUNCATE);
            return true;
        }
    }
    return false;
}
