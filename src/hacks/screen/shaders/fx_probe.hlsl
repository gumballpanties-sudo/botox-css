// Depth readback probe. SOURCE OF RECORD for fx_probe.h (the compiled bytecode).
//
// Rebuild after ANY edit here:
//   fxc /T ps_2_0 /E main /Fh src/hacks/screen/shaders/fx_probe.h /Vn k_fxProbeBytes src/hacks/screen/shaders/fx_probe.hlsl
//
// One tap of the resolved INTZ texture, written out raw. ps_2_0 because the quad is a
// D3DFVF_XYZRHW one (fixed-function vertex processing), which D3D9 will not pair with a
// 3_0 pixel shader -- the same reason cc_shader.hlsl is 2_0. The AO/DoF passes bring
// their own vs_3_0 for that.
//
// The raw projected depth is what comes back, NOT a linear distance: unprojecting needs
// the view setup's z range, and the probe's whole job is to say what the card actually
// resolved. The reader does the unprojection.

sampler2D sDepth : register(s0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float d = tex2D(sDepth, uv).r;
    return float4(d, d, d, 1);
}
