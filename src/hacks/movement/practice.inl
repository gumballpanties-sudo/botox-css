// ==== practice tab (movement -> practice) =======================================================
// Every line of this file is a CONSOLE COMMAND sent through IVEngineClient::ClientCmd_Unrestricted
// (slot 106 -- the road skins.cpp and fakemove.inl already take). Nothing here writes server
// memory, so the whole requirement is the one you have when you type the command yourself: you
// are the host of the server and sv_cheats is 1. Online it does nothing.
//
// The commands, and what the SDK says each one does:
//   god              CC_God_f (client.cpp) -- a TOGGLE with no argument, so the checkbox sends it
//                    on every flip and this file remembers which way it left it.
//   mp_timelimit 0   the map never ends. The value that was there comes back when the box goes off.
//   noclip           CC_Noclip, server side, so it also survives being stuck.
//   setpos / setang  client.cpp. NEITHER touches velocity (setpos is SetAbsOrigin, setpos_exact is
//                    Teleport(pos, NULL, NULL)) -- which is why the next line exists.
//   sv_maxvelocity 0 CGameMovement::CheckVelocity clamps EVERY axis of m_vecVelocity to it, and it
//                    runs in StartGravity / FinishGravity / WalkMove, i.e. every tick. One tick at
//                    0 IS "kill all velocity". Put back k_prVelKillMs later by the delayed pump.
//   bot -team -name -frozen -teleport   CS bots have no RPG class, so unlike the TF2 port's
//                    tf_bot_temp.cpp "-class" arg there is nothing to pick here -- just a team and
//                    a spot. -teleport/-frozen semantics: NEEDS FROM OTHERS (see report), the CS
//                    bot console command's exact args are unverified against cs_bot* SDK sources.
//   bot_teleport <name> x y z pitch yaw roll   our OWN placement, sent k_prBotFixMs later because
//                    the bot is not alive on the frame `bot` runs. This is the pass that puts an
//                    enemy on a friendly's HEAD instead of inside him.
//
// REMOVED from the TF2 port (TF2-only, no CS equivalent): impulse 101 (CTFPlayer::
// CheatImpulseCommands -- ammo/metal/rage refill, CS has none of that), the whole building
// placement tool (ent_create/ent_fire sentry/dispenser/teleporter, ported from baseobject_shared.cpp
// via buildplace.h, CS has no buildable objects), and (11-03 tabs round) `god`/`mp_timelimit 0`/
// `noclip` -- botox's practice window (menu_tab_movement.cpp:967-979) is checkpoint + teleport +
// "show window" only, no infinite ammo/health/time/noclip rows. g_practiceWindow (cfg/u_mvcore.h)
// is the master enable botox has and this file didn't; g_practiceWindowShow is read by whichever
// unit draws the practice panel (NEEDS FROM OTHERS, see report -- PracticeWindow_ShouldShow()).
#include <cstdarg>
#include <algorithm>

static constexpr unsigned k_prBotFixMs  = 600;   // bot joins team at +0.3 s, so correct it after
static constexpr unsigned k_prVelKillMs = 150;   // ~10 ticks of sv_maxvelocity 0 -- one is enough

// cs_shareddefs.h team indices (same values as aim/css.h CS_TEAM_TERRORIST/CS_TEAM_CT; duplicated
// here rather than pulling css.h into the movement TU for two constants)
static constexpr int CS_TEAM_T_MV  = 2;
static constexpr int CS_TEAM_CT_MV = 3;

static Vec3 s_prSavePos{}, s_prSaveAng{};
static bool s_prSaveHave = false;
static int s_prBotSeq = 0;

// botox "show window" (m_practice_window_show, def true) -- render.cpp RenderPracticeWindow
// (Present thread) reads these. Alive + sv_cheats are sampled by Practice_Tick on the game thread,
// so the draw never touches the entity or the cvar cache itself (botox misc.cpp:1976-1981 gates).
static std::atomic<bool> s_prAlive{ false }, s_prCheats{ false };
bool PracticeWindow_ShouldShow() { return g_practiceWindow && g_practiceWindowShow && s_prAlive.load(); }
bool PracticeWindow_Cheats() { return s_prCheats.load(); }

static void PrCmd(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DebugLog("practice: %s", buf);
    if (!g_engine) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engine);
        if (reinterpret_cast<uintptr_t>(vt[106]) > 0x10000)
            ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, buf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Commands that have to land LATER, not now: the sv_maxvelocity restore and the bot placement.
// Both are "send this string in N ms", so both use one pump, run once per cmd.
struct PrLater_t { uint64_t due; char cmd[160]; };
static PrLater_t s_prLater[8] = {};

static void PrCmdLater(unsigned ms, const char* fmt, ...)
{
    char buf[160];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const uint64_t due = GetTickCount64() + ms;
    for (auto& s : s_prLater)
        if (!s.due) { s.due = due; snprintf(s.cmd, sizeof(s.cmd), "%s", buf); return; }
    PrCmd("%s", buf);   // 8 presses inside one window: send it now rather than drop it
}

static void PrPumpLater()
{
    const uint64_t now = GetTickCount64();
    for (auto& s : s_prLater)
        if (s.due && now >= s.due) { s.due = 0; PrCmd("%s", s.cmd); }
}

static int PrTeam(void* pLocal)
{
    return (pLocal && g_offTeamNum > 0) ? EntI(pLocal, g_offTeamNum) : 0;
}

// WHERE THE CROSSHAIR POINTS, and the one rule the user asked for: a player under the crosshair
// gives his HEAD, not his chest. RcAimPlayerHead already answers exactly that (vHit.z = the top of
// the target's bbox + 1/32, i.e. where your feet stop when you stand on him) and it fills iHit for
// a TEAMMATE too -- it only returns false for one because a teammate is not solid to the route
// calculator. Here he is: placing an enemy on a friendly's head is the whole point.
static bool PrAimPos(void* pLocal, UserCmd_t* pCmd, Vec3& out)
{
    if (g_offOrigin <= 0 || g_offViewOffset <= 0) return false;
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);

    GameTrace_t tr{};
    const bool bWorld = RcRay(pLocal, vEye, vForward, tr);      // one ray, no marker fan
    const float flWorldT = bWorld ? tr.fraction * 8192.f : 8192.f;

    Vec3 vHead, oHead; int iHead = 0; bool bMate = false;
    RcAimPlayerHead(pLocal, vEye, vForward, flWorldT, vHead, iHead, oHead, bMate);
    if (iHead) { out = vHead; return true; }                    // on his head, either team
    if (!bWorld) return false;
    out = tr.endpos + tr.plane.normal * 2.f;                    // 2u off the face, never inside it
    return true;
}

static void PrTeleport()
{
    if (!s_prSaveHave) { DebugLog("practice: teleport -- no position saved yet"); return; }
    float flMaxVel = CvarFloat("sv_maxvelocity", 3500.f);
    if (flMaxVel < 1.f) flMaxVel = 3500.f;   // pressed again inside a kill window: read the default
    PrCmd("setpos %.4f %.4f %.4f;setang %.3f %.3f 0;sv_maxvelocity 0",
          s_prSavePos.x, s_prSavePos.y, s_prSavePos.z, s_prSaveAng.x, s_prSaveAng.y);
    PrCmdLater(k_prVelKillMs, "sv_maxvelocity %g", flMaxVel);
}

static void PrBot(void* pLocal, UserCmd_t* pCmd, bool bEnemy)
{
    Vec3 vPlace;
    if (!PrAimPos(pLocal, pCmd, vPlace))
    {
        DebugLog("practice: bot refused -- nothing under the crosshair");
        return;
    }
    // unassigned / spectator reads as "not T", so an enemy comes out T and a friendly CT
    const bool bRed = (PrTeam(pLocal) == CS_TEAM_T_MV) != bEnemy;
    const int  id = ++s_prBotSeq;
    char name[24];
    snprintf(name, sizeof(name), "botoxbot%d", id);
    PrCmd("bot -team %s -name %s -frozen -teleport",
          bRed ? "t" : "ct", name);

    // turn him to face you, so a placed dummy is a dummy looking at you
    const Vec3 vMe = EntV3(pLocal, g_offOrigin);
    const float flYaw = NormalizeAngle(atan2f(vMe.y - vPlace.y, vMe.x - vPlace.x) * 57.2957795f);
    PrCmdLater(k_prBotFixMs, "bot_teleport %s %.3f %.3f %.3f 0 %.2f 0",
               name, vPlace.x, vPlace.y, vPlace.z, flYaw);
}

// Called once per user command, BEFORE the pipeline's dead / noclip / blocked returns: a practice
// bind has to answer while you are noclipping or dead, which is most of the time you are using it.
static void Practice_Tick(UserCmd_t* pCmd)
{
    if (!g_practiceWindow) return;   // botox master enable; no bind fires without it
    PrPumpLater();

    if (!pCmd) return;
    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!pLocal) { s_prAlive = false; return; }

    // dead, you are a camera: your own origin and view offset are not a place to aim or save
    // from, so everything that reads them waits until you are alive again. noclip does not.
    const bool bAlive = EntAlive(pLocal);
    s_prAlive = bAlive;
    s_prCheats = CvarFloat("sv_cheats", 0.f) != 0.f;

    // edge press, the rule every NoMode bind in this file uses (MovementKeyOK also holds the
    // is_active_app gate, so nothing fires while the game is not the foreground window)
    auto Pressed = [](int key, bool& held)
    {
        const bool down = key != 0 && MovementKeyOK(key, 1);
        const bool edge = down && !held;
        held = down;
        return edge;
    };
    static bool hSave = false, hTp = false, hEnemy = false, hMate = false;

    if (Pressed(g_prSaveKey, hSave) && bAlive && g_offOrigin > 0)
    {
        s_prSavePos = EntV3(pLocal, g_offOrigin);
        s_prSaveAng = pCmd->viewangles;
        s_prSaveHave = true;
        DebugLog("practice: saved %.2f %.2f %.2f / %.1f %.1f",
                 s_prSavePos.x, s_prSavePos.y, s_prSavePos.z, s_prSaveAng.x, s_prSaveAng.y);
    }
    if (Pressed(g_prTpKey, hTp) && bAlive)           PrTeleport();
    // menu buttons = same actions, no bind needed; dead = request dropped, like a key press
    const int req = g_prMenuReq.exchange(0);
    if ((Pressed(g_prBotEnemyKey, hEnemy) || (req & 1)) && bAlive)  PrBot(pLocal, pCmd, true);
    if ((Pressed(g_prBotMateKey, hMate) || (req & 2)) && bAlive)    PrBot(pLocal, pCmd, false);
}
