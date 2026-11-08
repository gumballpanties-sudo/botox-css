// Passthrough vertex shader for every screen-effect quad. SOURCE OF RECORD for fx_quad_vs.h.
//
// Rebuild after ANY edit here:
//   fxc /T vs_3_0 /E main /Fh src/hacks/screen/shaders/fx_quad_vs.h /Vn k_fxQuadVsBytes src/hacks/screen/shaders/fx_quad_vs.hlsl
//
// WHY A VERTEX SHADER AT ALL. D3D9 does not pair a 3_0 pixel shader with fixed-function
// vertex processing, and the AO and DoF passes are ps_3_0 (dynamic loops, tex2Dlod). The
// colour grade gets away with a D3DFVF_XYZRHW quad only because it is ps_2_0.
//
// The position arrives already in clip space -- the caller builds the quad there and does
// the half-texel shift itself, so this shader transforms nothing.

struct VS_IN  { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VS_OUT main(VS_IN i)
{
    VS_OUT o;
    o.pos = i.pos;
    o.uv  = i.uv;
    return o;
}
