#pragma once
#include "../../globals/globals.h"

// -- CVar helpers ---------------------------------------------------------------
void CVar_ForceFloat(ConVar* cv, float val);
void CVar_ForceInt(ConVar* cv, int val);

// One-shot resolve, miss remembered.
//
// ICvar::FindVar is a hash lookup, but FindVarWalk falls back to a LINKED-LIST walk of up to
// 16384 command nodes with a virtual GetName + strcmp at each one when FindVar comes back
// empty. Every cached-convar site in this DLL was written `if (!cv) cv = FindVarWalk(name);`
// -- which caches a HIT and retries a MISS forever. A name that does not exist in this build
// (or is not registered yet) therefore paid that whole walk on every 100ms tick for the life
// of the session: ~10 walks/sec per missing name, each one thousands of virtual calls.
// Holding the miss makes the cost one walk, once.
//
// g_cvar is re-checked because the interface pointer is resolved on the poll thread after
// some of these sites first run; a new ICvar invalidates every pointer we hold.
// A miss is retried fast 32 times, then every 64th call -- never latched. A convar registers
// with its module, and a module can finish loading after g_cvar does, so "not found yet" is
// not "not in this build". The old hard cap (~3s) lost that race when dih was loaded at game
// start. 1-in-64 keeps a name that truly does not exist at ~one walk per 6s of ticks.
struct CachedCVar
{
    ConVar*       cv    = nullptr;
    ICvar*        owner = nullptr;
    unsigned      tries = 0;

    static constexpr unsigned kMaxTries = 32;

    ConVar* Get(const char* name)
    {
        if (!g_cvar) return nullptr;
        if (owner != g_cvar) { owner = g_cvar; cv = nullptr; tries = 0; }  // new interface: start over
        // Past kMaxTries a miss is retried every 64th call, never dropped: loaded at game start,
        // the ticks begin before every convar is registered, and a hard cap left those null
        // for the whole session.
        if (!cv && (++tries <= kMaxTries || (tries & 63) == 0))
            cv = g_cvar->FindVarWalk(name);
        return cv;
    }
};

// -- Per-tick cvar pushes (fog, sway, bob, crosshair, sv_cheats) ------------------
void UpdateVisualsCvars();
void UpdateNetStats();
void UpdateFPS();

// -- Eject restore ----------------------------------------------------------------
void ResetFogCvars();

// -- Hat changer / no-hats: removed, no CS:S equivalent (no attached cosmetic-item layer on
// CS player models). Stubs kept only so render.cpp / skins.cpp still link; see report. -------
// model_t* -> is-a-hat memo used by h_DrawModelExecute. Model pointers die with the
// map and the heap reuses the addresses, so wipe it whenever we re-enter a game.
void Visuals_FlushModelCache();

// Applies/reverts the flat viewmodel alpha. Must run OUTSIDE DrawModelExecute: the var it
// writes ($blendtintbybasealpha) is a static shader decision, so each material has to
// re-snapshot. Called from render.cpp's slow tick.
void VmFlatAlpha_Tick();

// -- DrawModelExecute hook (no hats) ---------------------------------------------------
using DrawModelExecute_t = void(__fastcall*)(void*, const void*, const void*, const void*);
extern DrawModelExecute_t o_DrawModelExecute;
void __fastcall h_DrawModelExecute(void* rcx, const void* pState, const void* pInfo, const void* pBones);

// -- Skybox hook -----------------------------------------------------------------------------
using R_DrawSkyBox_t = void(__fastcall*)(float, int);
extern R_DrawSkyBox_t o_R_DrawSkyBox;
void __fastcall h_R_DrawSkyBox(float zFar, int nDrawFlags);

// -- Viewmodel offset hook ---------------------------------------------------------------------
using CalcViewModelView_t = void(__fastcall*)(void*, void*, void*, void*);
extern CalcViewModelView_t o_CalcViewModelView;
void __fastcall h_CalcViewModelView(void* rcx, void* owner, void* pEyePosition, void* pEyeAngles);
void VmCsgo_Install(HMODULE client);   // CS:GO bob hook (vm_csgo_hook.inl)

// -- Weather -----------------------------------------------------------------------------------
using FindMaterial_t = void*(__fastcall*)(void*, const char*, const char*, bool, const char*);
extern FindMaterial_t o_FindMaterial;
void* __fastcall h_FindMaterial(void* rcx, const char* pName, const char* pTextureGroup,
    bool bComplain, const char* pComplainPrefix);
void Precip_Park(int kSlot);      // bounds -> 0, entity stays alive (the "off" path)
void Precip_TearDown(int kSlot);  // Release() -- frees the entity, mode flip / eject only
bool Precip_IsLive();             // is there an entity to tear down at all?
void Precip_OnLevelChange();      // drop the stale entity/ownership state, NO Release()
void Rain_Tick();

// -- Remove ragdolls (botox csgo-sdk approach, draw_model_execute.cpp should_skip_ragdoll +
// frame_stage_notify.cpp cl_ragdoll_physics_enable -- NOT a CreateTFRagdoll-style create hook,
// that was botox-tf2's own ref substitute and botox csgo-sdk never had one) ---------------
// Skip is inlined into h_DrawModelExecute_impl (visuals.cpp); the cvar toggle rides
// UpdateVisualsCvars (same file). No standalone hook/decl needed.

// -- Remove prop fade (ref approach: force CStaticProp::m_Alpha = 255 in ComputePropOpacity) --
using CStaticPropMgr_ComputePropOpacity_t = void(__fastcall*)(void*, void*);
extern CStaticPropMgr_ComputePropOpacity_t o_ComputePropOpacity;
void __fastcall h_ComputePropOpacity(void* rcx, void* pProp);

// -- Remove screen effects (ref: swallow Shake/Fade/Rumble in DispatchUserMessage, VClient slot 36) --
using DispatchUserMessage_t = bool(__fastcall*)(void*, int, void*);
extern DispatchUserMessage_t o_DispatchUserMessage;
bool __fastcall h_DispatchUserMessage(void* rcx, int type, void* msgData);

// -- Remove screen overlays (underwater/nightvision fullscreen material overlays; CS:S has
// no jarate/milk/uber/dead-ringer, those were TF2-only) --
// CViewRender::PerformScreenOverlay -- skip original -> m_ScreenOverlayMaterial never drawn.
using PerformScreenOverlay_t = void(__fastcall*)(void*, int, int, int, int);
extern PerformScreenOverlay_t o_PerformScreenOverlay;
void __fastcall h_PerformScreenOverlay(void* rcx, int x, int y, int w, int h);

// -- FPS boost (bone cache: serve cached m_CachedBoneData for non-local players) --
using SetupBones_t = bool(__fastcall*)(void*, void*, int, int, float);
extern SetupBones_t o_SetupBones;
bool __fastcall h_SetupBones(void* rcx, void* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime);

// -- Unit C2: Sound ESP (CSoundEmitterSystem_EmitSound, client.dll; pattern verified against
// ref's Hooks/CSoundEmitterSystem_EmitSound.cpp). Observe-only: filter/ep are read-only
// peeks and the original is ALWAYS called, so no sound (including SND_STOP) is ever dropped.
using EmitSound_t = void(__fastcall*)(void*, void*, int, const void*);
extern EmitSound_t o_EmitSound;
void __fastcall h_EmitSound(void* rcx, void* filter, int entindex, const void* ep);

// -- Unit C2: Chams, piggybacked on this file's own DrawModelExecute hook (no 2nd MinHook on
// the target). Lifecycle rides CViewRender::LevelInit/LevelShutdown, which flipworld.cpp
// already hooks for its own RT -- called from there, not a new hook.
void Chams_LevelInit();
void Chams_LevelUnload();

// -- Unit C3: Glow, piggybacked on the same DrawModelExecute hook as chams (extra
// $ignorez overlay pass, no 2nd MinHook). Lifecycle rides the same LevelInit/LevelShutdown
// flipworld.cpp already hooks, called from there right next to Chams_LevelInit/Unload.
void Glow_LevelInit();
void Glow_LevelUnload();
