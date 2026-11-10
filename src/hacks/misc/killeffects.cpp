#include "killeffects.h"
#include "../movement/movement.h"     // Vec3
#include "../movement/prediction.h"   // EntV3
#include "unitE_logic.h"              // n_unitE::DecodeEHandleIndex
#include <cstdint>
#include <vector>

// ==== engine particle dispatch (blood / confetti / explosion) =====================
// x64 TF2 client.dll sigs, originally verified in the x64 TF2 SDK 
// Helpers/Particles/Particles.h (CTE_DispatchEffect, Get_ParticleSystemIndex) -- never
// guessed. "ParticleEffect" is a real DECLARE_CLIENT_EFFECT name, confirmed identically
// registered in css-community-master game/client/c_particle_system.cpp:260
// (DECLARE_CLIENT_EFFECT("ParticleEffect", ParticleEffectCallback)), so
// CTE_DispatchEffect("ParticleEffect", data) is the same path on CS:S too.
//
// 11-03 CSS re-verify: both patterns below were re-cut via pefile/regex against the live
// cstrike/bin/x64/client.dll and confirmed 1 UNIQUE hit each (python tools/css_sigcheck.py
// also confirms -- not in the MISSING list), landing on exactly the functions the CS:S
// source predicts:
//   dispatch sig  -> 0x273c70 = DispatchEffectToCallback(const char* pEffectName,
//                     const CEffectData& data) (c_te_effect_dispatch.cpp:71) -- walks
//                     CClientEffectRegistration::s_pHead for a name match and calls the
//                     callback. Confirmed via its DevMsg string "DispatchEffect: effect
//                     '%s' not found on client\n" literally inlined a few bytes after.
//   getidx sig    -> 0xd5a40 = GetParticleSystemIndex(const char*) (cdll_client_int.cpp:811)
//                     -- FindStringIndex on g_pStringTableParticleEffectNames (vtable+0x60),
//                     cmp eax,0xFFFF (INVALID_STRING_INDEX). Confirmed via its DevWarning
//                     string "Client: Missing precache for particle system \"%s\"!\n".
// CEffectData field order (effect_dispatch_data.h) matches TfEffectData below exactly --
// same shared SDK struct on both branches, "Some TF2 specific things" comment included
// verbatim in the CS:S source for material/damageType/hitBox. No struct changes needed.
namespace
{
    using DispatchEffect_t = void(__fastcall*)(const char*, const void*);
    using GetParticleIdx_t = int(__fastcall*)(const char*);
    DispatchEffect_t s_dispatchEffect = nullptr;
    GetParticleIdx_t s_getParticleIdx = nullptr;
    bool s_sigsFailed = false;

    // Layout copied verbatim from the verified ref struct -- the engine reads this by
    // offset, so field order/types must match exactly, not just the fields this file uses.
    struct TfParticleControlPoint { int attach; Vec3 offset; };
    struct TfEffectData
    {
        Vec3  origin, start, normal, angles;
        int   flags = 0;
        int   entIndex = 0;
        float scale = 0.f, magnitude = 0.f, radius = 0.f;
        int   attachmentIndex = 0;
        short surfaceProp = 0;
        int   material = 0, damageType = 0, hitBox = 0;
        unsigned char color = 0;
        bool  customColors = false;
        Vec3  customColor1, customColor2;
        bool  controlPoint1 = false;
        TfParticleControlPoint cp1{};
        int   effectName = 0;
    };

    // Re-verified against CS:S 11-03 -- see the file header above. Both patterns already
    // carried the right wildcards (immediate operands only) and matched CS:S byte-for-byte
    // on the fixed opcodes, so no pattern edit was needed, only the re-verification.
    bool ResolveParticleSigs()
    {
        if (s_dispatchEffect && s_getParticleIdx) return true;
        if (s_sigsFailed) return false;

        HMODULE hClient = GetModuleHandleA("client.dll");
        if (!hClient) return false;

        if (!s_dispatchEffect)
            if (uint8_t* a = PatternScan(hClient,
                "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 48 8B FA 48 8D 4C 24"))
                s_dispatchEffect = reinterpret_cast<DispatchEffect_t>(a);

        if (!s_getParticleIdx)
            if (uint8_t* a = PatternScan(hClient,
                "40 53 48 83 EC ? 48 8B D9 48 85 C9 74 ? 48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 3D"))
                s_getParticleIdx = reinterpret_cast<GetParticleIdx_t>(a);

        s_sigsFailed = !s_dispatchEffect || !s_getParticleIdx;
        if (s_sigsFailed)
            DebugLog("KFX: particle sigs missing (dispatch=%p getIdx=%p) -- engine styles off",
                     (void*)s_dispatchEffect, (void*)s_getParticleIdx);
        return !s_sigsFailed;
    }

    // log each unprecached name once (names are the static literals below)
    void LogMissingOnce(const char* name)
    {
        static const char* s_logged[16] = {};
        for (const char*& p : s_logged)
        {
            if (p == name) return;
            if (!p) { p = name; DebugLog("KFX: particle \"%s\" not precached, skipped", name); return; }
        }
    }

    int LookupParticle(const char* name)
    {
        __try { return s_getParticleIdx(name); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool DispatchEngineParticle(const char* name, const Vec3& origin, const Vec3& angles)
    {
        if (!ResolveParticleSigs()) return false;
        // GetParticleSystemIndex returns 0 (not -1) for an unprecached name
        // (sdk cdll_client_int.cpp:816) -- 0 = invalid, skip it.
        const int idx = LookupParticle(name);
        if (idx <= 0) { LogMissingOnce(name); return false; }
        __try
        {
            TfEffectData data{};
            data.origin = origin;
            data.start  = origin;
            data.angles = angles;
            data.scale  = 1.f;   // CEffectData ctor default
            // ParticleEffectCallback reads the system index from m_nHitBox (sdk
            // c_particle_system.cpp:186, particle_parse.cpp:409). m_iEffectName is the
            // EffectDispatch table slot -- putting idx there left hitBox 0 = wrong system.
            data.hitBox = idx;
            s_dispatchEffect("ParticleEffect", &data);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // botox effects(): client.dll IEffects001 (sdk EffectsClient.cpp:74). Vtable: IPredictionSystem's
    // virtual dtor 0, Beam 1, Smoke 2, Sparks 3, Dust 4, MuzzleFlash 5, MetalSparks 6, EnergySplash 7
    // (sdk game/shared/IEffects.h) -- the same slots botox calls on CS:GO.
    void* Effects()
    {
        static void* fx = GetInterface("client.dll", "IEffects001");
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("KFX: IEffects001 %p (sparks / energy splash)", fx); }
        return fx;
    }

    void FxSparks(const Vec3& at)
    {
        void* fx = Effects();
        if (!fx) return;
        using Sparks_t = void(__fastcall*)(void*, const Vec3&, int, int, const Vec3*);
        __try { ((Sparks_t)(*(void***)fx)[3])(fx, at, 8, 8, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Round 2 lua port exports -- same two calls above, just reachable from outside this TU.
bool KillEffects_DispatchParticle(const char* name, const Vec3& origin, const Vec3& angles)
{
    return DispatchEngineParticle(name, origin, angles);
}
void KillEffects_Sparks(const Vec3& at)
{
    FxSparks(at);
}

namespace
{

    void FxEnergySplash(const Vec3& at, const Vec3& dir)
    {
        void* fx = Effects();
        if (!fx) return;
        using Splash_t = void(__fastcall*)(void*, const Vec3&, const Vec3&, bool);
        __try { ((Splash_t)(*(void***)fx)[7])(fx, at, dir, true); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // botox try_names(): first precached system in the list wins
    template <size_t N>
    bool TryNames(const char* const (&names)[N], const Vec3& origin, const Vec3& angles)
    {
        for (const char* n : names)
            if (DispatchEngineParticle(n, origin, angles)) return true;
        return false;
    }
}

// ==== custom (non-engine) style pool, defined in killeffects_world.cpp ============
// all take the victim's feet (abs origin), as botox's spawn_* read victim->get_abs_origin()
void KfxPush_Soul(const Vec3& feet);
void KfxPush_Smoke(const Vec3& feet);
void KfxPush_Balls(const Vec3& feet);
void KfxPush_Mc(const Vec3& feet, const Vec3& obbMins, const Vec3& obbMaxs);

// ==== hl2 dissolve (botox kill_effects.cpp hl2_dissolve_paint) =====================
// The game's own DissolveEffect (sdk ragdoll_shared.cpp:1096, client_base = in every 2013 client)
// on the victim's ragdoll. x64 client.dll: target rcx, time xmm1. Sig = the standalone copy found
// from its "sprites/blueglow1.vmt" xref (the other xref is TransferDissolveFrom, inlined); unique
// in css + tf2 client.dll, only the alloc size differs.
namespace
{
    using DissolveEffect_t = void*(__fastcall*)(void*, float);
    struct DissolveWait { int victim; float since; };
    std::vector<DissolveWait> s_dissolveWait;

    DissolveEffect_t DissolveFn()
    {
        static DissolveEffect_t fn = nullptr;
        static bool tried = false;
        if (!tried) {
            tried = true;
            if (HMODULE hClient = GetModuleHandleA("client.dll"))
                fn = reinterpret_cast<DissolveEffect_t>(PatternScan(hClient,
                    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 30 48 8B F1 0F 29 74 24 20 B9 ? ? 00 00 0F 28 F1 E8"));
            DebugLog("KFX: DissolveEffect %p", (void*)fn);
        }
        return fn;
    }

    // DSV: last look per wait, printed when it times out
    struct DissolveSeen { unsigned hRagdoll; void* ragdoll; unsigned hPlayer; void* victim; };
    DissolveSeen s_dissolveSeen{};

    // true = done with this victim (dissolved or gone), false = ragdoll not here yet.
    // Ragdoll = victim's m_hRagdoll whose m_hPlayer is the victim (botox melt_ragdoll).
    bool TryDissolve(int victimIdx, float cur)
    {
        // 0 = tables not up yet, -1 = not in this build
        const auto resolve = [](int& off, const char* table, const char* name) {
            if (off != 0) return;
            if (RecvTable* t = FindNetworkTable(table)) { const int o = FindPropOffset(t, name); off = o > 0 ? o : -1; }
        };
        static int offRagdoll = 0, offRagPlayer = 0;
        resolve(offRagdoll, "DT_CSPlayer", "m_hRagdoll");
        resolve(offRagPlayer, "DT_CSRagdoll", "m_hPlayer");
        const DissolveEffect_t fn = DissolveFn();
        if (!fn || offRagdoll < 0 || offRagPlayer < 0) {
            DebugLog("DSV: off fn=%p offRagdoll=%d offRagPlayer=%d", (void*)fn, offRagdoll, offRagPlayer);
            return true;
        }
        if (offRagdoll == 0 || offRagPlayer == 0) return false;

        __try {
            void* victim = GetClientEntity(victimIdx);
            s_dissolveSeen = { 0xFFFFFFFFu, nullptr, 0xFFFFFFFFu, victim };
            if (!victim) { DebugLog("DSV: victim %d gone", victimIdx); return true; }
            const auto handleAt = [](void* ent, int off) { return *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(ent) + off); };
            // index -> GetClientEntity: the raw handle into slot 4 returned null every time (tf2 DSV 11-10)
            const unsigned hRagdoll = handleAt(victim, offRagdoll);
            const int ragIdx = n_unitE::DecodeEHandleIndex(hRagdoll);
            void* ragdoll = ragIdx > 0 ? GetClientEntity(ragIdx) : nullptr;
            s_dissolveSeen.hRagdoll = hRagdoll;
            s_dissolveSeen.ragdoll  = ragdoll;
            if (!ragdoll) return false;
            const unsigned hPlayer = handleAt(ragdoll, offRagPlayer);
            s_dissolveSeen.hPlayer = hPlayer;
            if (n_unitE::DecodeEHandleIndex(hPlayer) != victimIdx) return false;
            void* made = fn(ragdoll, cur);
            DebugLog("DSV: start victim=%d rag=%p made=%p offs=%d/%d", victimIdx, ragdoll, made, offRagdoll, offRagPlayer);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { DebugLog("DSV: exception victim=%d code=0x%X", victimIdx, GetExceptionCode()); }
        return true;
    }
}

// FSN FRAME_RENDER_START, game thread: DissolveEffect makes a client entity
void KillEffects_FrameStart()
{
    if (!g_dpEnable || g_dpStyle != 9 || g_eject || !g_globalVars || !g_entityList) { s_dissolveWait.clear(); return; }
    const float cur = g_globalVars->curtime;
    for (size_t i = 0; i < s_dissolveWait.size();) {
        const DissolveWait w = s_dissolveWait[i];
        const bool timedOut = cur < w.since || cur - w.since > 1.f;
        if (timedOut)
            DebugLog("DSV: timeout victim=%d victimPtr=%p hRagdoll=0x%X rag=%p hPlayer=0x%X", w.victim, s_dissolveSeen.victim,
                     s_dissolveSeen.hRagdoll, s_dissolveSeen.ragdoll, s_dissolveSeen.hPlayer);
        if (timedOut || TryDissolve(w.victim, cur))
            s_dissolveWait.erase(s_dissolveWait.begin() + i);
        else
            ++i;
    }
}

void KillEffects_OnDeath(int victimIdx, int attackerIdx, bool localDied)
{
    if (!g_dpEnable || localDied) return;   // botox: never on the local player's own death
    if (victimIdx < 1) return;

    // botox: `if ( !victim || victim->is_dormant( ) ) return;` -- dormant (out of PVS) = stale
    // m_vecNetworkOrigin, effect spawned at junk spot. entlist slot 0 GetClientNetworkable,
    // networkable slot 8 IsDormant (sdk icliententitylist.h / iclientnetworkable.h).
    void* pNet = g_entityList ? VCall<void*>(g_entityList, 0, victimIdx) : nullptr;
    bool dormant = true;
    __try { dormant = !pNet || VCall<bool>(pNet, 8); }
    __except (EXCEPTION_EXECUTE_HANDLER) { dormant = true; }
    if (dormant) return;

    void* pVictim = GetClientEntity(victimIdx);
    if (!pVictim || g_offOrigin <= 0) return;

    Vec3 feet{};
    __try { feet = EntV3(pVictim, g_offOrigin); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    Vec3 at = feet;
    at.z += 40.f;
    const Vec3 up(0.f, 0.f, 0.f);

    const int localIdx = GetLocalPlayerIndex();
    const int style = g_dpStyle;

    switch (style)
    {
    case 3: KfxPush_Smoke(feet); return;
    case 5: KfxPush_Soul(feet);  return;
    case 8: KfxPush_Mc(feet, Vec3(-16.f, -16.f, 0.f), Vec3(16.f, 16.f, 62.f)); return;   // CS:S player hull (g_CSViewVectors, cs_gamerules.cpp:75-76: hull min/max)
    case 9:   // ragdoll comes a frame or two later
        if (g_globalVars) s_dissolveWait.push_back({ victimIdx, g_globalVars->curtime });
        DebugLog("DSV: queued victim=%d globals=%p", victimIdx, (void*)g_globalVars);
        return;
    case 6:
        if (attackerIdx == localIdx) KfxPush_Balls(feet);   // botox: attacker-is-local gate
        return;
    default: break;   // 0 blood, 1 sparks(->blood), 2 confetti, 4 explosion, 7 melt(->blood)
    }

    // 11-03 CSS re-verify: names checked against cstrike_pak_007.vpk's particles/*.pcf
    // (blood_impact.pcf, fire_01.pcf) via strings on the vpk, since CS:S ships no holiday/
    // confetti content and uses its own blood/explosion pcf set, not TF2's tf2_misc_*.vpk.
    // blood_impact_red_01 confirmed present (blood_impact.pcf); bomb_explosion_huge /
    // explosion_huge confirmed present (fire_01.pcf). No confetti-equivalent pcf exists on
    // CS:S at all -- that style falls through to the sparks/splash fallback below by design
    // (same "never precached = EnergySplash" rule the botox comment at the bottom documents).
    static const char* const k_blood[]     = { "blood_impact_red_01" };
    static const char* const k_explosion[] = { "bomb_explosion_huge", "explosion_huge" };
    const Vec3 bloodAng(-90.f, 0.f, 0.f);   // botox: blood sprays up

    bool done = false;
    if (style == 2)      done = false;   // confetti: no CS:S pcf equivalent, see comment above
    else if (style == 4) done = TryNames(k_explosion, at, up);
    else if (style != 1) done = TryNames(k_blood, at, bloodAng);   // 0 blood, 7 melt (not ported -> blood)
    if (done) return;

    // botox: sparks = IEffects::Sparks( at, 8, 8 ), a pick this map never precached = EnergySplash
    if (style == 1) FxSparks(at);
    else FxEnergySplash(at, Vec3(0.f, 0.f, 1.f));
}
