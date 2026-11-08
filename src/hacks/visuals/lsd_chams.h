#pragma once
// botox hacks/visuals/lsd_chams.{h,cpp} (commit 95ef94a): the "lsd" chams material. The layer draws
// a flat white carrier material (aim/chams.cpp); while armed, a DrawIndexedPrimitive detour swaps
// in the shadertoy "70s Melt" vs/ps (fx_lsd_*.hlsl). Plain C API: aim/chams.cpp (aim TU) calls it.
//
// Main thread, around one chams layer draw. Queued onto the render thread through the material
// system's call queue (screenfx.cpp ScreenFx_QueueFunctor) so the arm lands right before the
// model's queued draw and the disarm right after it; mat_queue_mode 0 = set directly.
void LsdChams_Begin(float realTime, float alpha);
void LsdChams_End();

// botox hacks/visuals/two_tone_chams.{h,cpp}: the "two tone" layer draws the model's OWN materials
// (no override, so its normal maps + lighting constants stay bound); while armed the same DIP
// detour swaps in a lit-side / shadow-side cut shader (fx_twotone_*.hlsl). Colours 0..1 display,
// alpha pre-scaled. Same queue rules as lsd. No DIP hook = the own materials draw as is.
//
// Round-4 note (deleted): the TF2 killstreak "normal blend sheen" DIP intercept (g_sheenNormal,
// SheenNormal_Tick, fx_sheen_ps*) that used to live here, and the follow-on claim that botox's
// real weapon sheen needs no DIP intercept at all, were both wrong. See WeaponSheen_Begin/End
// below -- round-5's real port of botox hacks/visuals/weapon_sheen.cpp.
void TwoToneChams_Begin(const float light[4], const float dark[4], float threshold, float edge, bool ignorez);
void TwoToneChams_End();

// botox hacks/visuals/weapon_sheen.{h,cpp} (1:1, round-5): a THIRD DIP layer, same shape as lsd/
// two-tone above -- a forced-material-override additive-black carrier draws one extra pass of the
// local viewmodel weapon while armed, and the DIP detour swaps in the real envmap-cubemap +
// animated-mask-texture + model-space-UV-sweep shader (fx_sheen_vs/ps.hlsl) for that one draw.
// `modelName` is the weapon viewmodel's model path (e.g. "models/weapons/v_glock.mdl"), used to
// look up its local sweep axis (IVModelInfo::GetModelBounds -- see report, UNVERIFIED precision
// vs. botox's own VVD-measured box). NEEDS FROM OTHERS: the call site (one extra DrawModel of the
// local player's viewmodel weapon, vmKind == 2) and Chams_GetSheenCarrierMaterial's
// implementation (aim/chams.cpp, needs the typed KeyValues/CreateMaterial this TU can't include).
bool WeaponSheen_Begin(const char* modelName);
void WeaponSheen_End();
