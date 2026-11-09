#pragma once
// Plain-type bridge between dih's DrawModelExecute hook (visuals.cpp) and the chams/glow
// implementation in aim/chams.cpp (which needs the aim SDK: IMaterial, KeyValues, CCSPlayer).
// Same split as aim/esp.h: no aim or windows types cross this header.

using ChamsDrawFn = void(__fastcall*)(void*, const void*, const void*, const void*);

// Draws the model through every chams/glow pass it wants and returns true; returns false when
// nothing applies (caller draws it normally). `vmKind` is visuals.cpp's ViewmodelDrawKind
// (0 not viewmodel, 1 arms stack, 2 weapon in hands), `modelName` may be null.
bool Chams_DrawModel(ChamsDrawFn fnOriginal, void* rcx, const void* pState, const void* pInfo, const void* pBones,
                     int entIdx, const char* modelName, int vmKind);

// Fast gate for the hook's cheap path: any chams/glow option that can draw anything.
bool Chams_AnyActive();

// enemy chams on + a backtrack material picked: aim/backtrack.cpp builds records (full bones) for it
bool Chams_BacktrackActive();

// CSoundEmitterSystem::EmitSound peek (visuals.cpp h_EmitSound): sound chams timestamps.
void Chams_OnSound(int entindex, float volume, const float* origin);

// Material lifetime: released on level shutdown (DecRef + DeleteIfUnreferenced), rebuilt
// lazily on the first draw of the next map. Both are called from visuals.cpp Chams_LevelInit /
// Chams_LevelUnload (flipworld.cpp's CViewRender::LevelInit/LevelShutdown hook).
void Chams_ReleaseMaterials();

// ---- player glow (aim/chams.cpp, ref Features/Visuals/Glow on fpsoverlay vmglow's slots) ----
void Glow_LevelInit();     // flipworld.cpp CViewRender::LevelInit hook: render targets + materials
void Glow_LevelUnload();   // no-op: RTs live across maps, remade only on a backbuffer size change
void Glow_Render();        // screenfx.cpp DoPostScreenSpaceEffects, before the original: stencil + halo pass
bool Glow_Rendering();     // true inside Glow_Render's model draws: h_DrawModelExecute passes straight through

// ---- esp preview (hacks/visuals/esp_preview.cpp) ----
// ITexture*, null if Glow_LevelInit's preview target creation failed. Plain void*: chams_api.h stays
// engine-type-free, same as the rest of this header.
void Glow_PreviewInit();   // esp preview: make its targets if no Glow_LevelInit did yet (game thread)
void* Glow_PreviewTarget();
// Glow_Render for one item -- the preview entity -- on the preview target: call right after the
// preview's chams draw, still inside Push3DView with the preview RT bound (aim/chams.cpp has the
// ordering proof). worldToProjection is EspPreview's own camera (float[4][4], row-major); pBones is
// a matrix3x4 array of nBones entries off the drawn model; pHdr is its studiohdr_t*.
void Glow_RenderPreview(void* pEnt, const float (*worldToProjection)[4], const void* pBones, int nBones, const void* pHdr);

// IVModelRender::ForcedMaterialOverride (slot 1), installed by thread.cpp. The engine's own
// overrides (invuln, burning, ...) are dropped while Glow_Rendering so the glow material stays.
using ForcedMaterialOverride_t = void(__fastcall*)(void*, void*, int);
extern ForcedMaterialOverride_t o_ForcedMaterialOverride;
void __fastcall h_ForcedMaterialOverride(void* rcx, void* pMat, int iType);

// ---- NEEDS FROM OTHERS (WX unit, weapon sheen round-5) -----------------------------------------
// Implement in aim/chams.cpp, using the CreateFromVMT helper already there (same "one KeyValues ->
// CreateMaterial -> one IncRef" pattern chams.cpp's own carriers use). Lazy-build once, cache the
// pointer, release it from Chams_ReleaseMaterials() like the other chams materials:
//   IMaterial* CreateFromVMT("botox_sheen_carrier",
//       "\"UnlitGeneric\"\n{\n"
//       "\t\"$basetexture\" \"vgui/white\"\n\t\"$color\" \"[0 0 0]\"\n\t\"$color2\" \"[0 0 0]\"\n"
//       "\t\"$additive\" \"1\"\n\t\"$model\" \"1\"\n\t\"$nofog\" \"1\"\n}\n");
// (botox weapon_sheen.cpp carrier_material(), verbatim VMT.) Return it as a raw void* -- this
// function is the ONLY bridge a non-aim TU (lsd_chams.cpp) needs, since IMaterial/KeyValues are
// aim-only types (INCLUDE RULE) but the material pointer itself is just passed through
// IVModelRender::ForcedMaterialOverride's existing void* parameter, never dereferenced outside aim.
// Until this exists, WeaponSheen_Begin (lsd_chams.cpp) returns false and the sheen stays off.
void* Chams_GetSheenCarrierMaterial();
