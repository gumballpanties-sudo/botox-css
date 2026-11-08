// Two tone chams pixel shader (botox hacks/visuals/two_tone_chams.cpp k_ps, 1:1). SOURCE OF RECORD for fx_twotone_ps*.h.
// Light = the camera: lit where the surface (normal map included) faces the eye. Ours: c30 light colour,
// c31 dark colour, c32 threshold / edge / alpha test on / alpha ref. TF2 normal maps sit in s3 (skin_ps20b.fxc:107
// NormalMapSampler, vertexlit_and_unlit_generic_bump_ps2x.fxc:102 BumpmapSampler); s1 kept for botox parity.
//
// Rebuild after ANY edit here (three variants, one header each):
//   fxc /nologo /T ps_3_0 /E main /D BUMP_SAMPLER=0 /Fh src/hacks/screen/shaders/fx_twotone_ps0.h /Vn k_fxTwoTonePs0Bytes src/hacks/screen/shaders/fx_twotone_ps.hlsl
//   fxc /nologo /T ps_3_0 /E main /D BUMP_SAMPLER=1 /Fh src/hacks/screen/shaders/fx_twotone_ps1.h /Vn k_fxTwoTonePs1Bytes src/hacks/screen/shaders/fx_twotone_ps.hlsl
//   fxc /nologo /T ps_3_0 /E main /D BUMP_SAMPLER=3 /Fh src/hacks/screen/shaders/fx_twotone_ps2.h /Vn k_fxTwoTonePs2Bytes src/hacks/screen/shaders/fx_twotone_ps.hlsl
const float4 cLightColor : register( c30 );
const float4 cDarkColor : register( c31 );
const float4 cParams : register( c32 );

sampler BaseSampler : register( s0 );
#if BUMP_SAMPLER == 1
sampler NormalSampler : register( s1 );
#elif BUMP_SAMPLER == 3
sampler NormalSampler : register( s3 );
#endif

struct PS_INPUT {
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float4 vTangent : TEXCOORD2;
	float3 vView : TEXCOORD3;
};

float4 main( PS_INPUT i ) : COLOR
{
	// material alpha test done here: picked alpha stays out of the cutout (hair, straps keep shape when translucent)
	clip( cParams.z * ( tex2D( BaseSampler, i.vUv ).a - cParams.w ) );

	float3 n = normalize( i.vNormal );
#if BUMP_SAMPLER
	// phong_ps20b.fxc:502-553
	float3 t  = normalize( i.vTangent.xyz );
	float3 b  = cross( n, t ) * i.vTangent.w;
	float3 tn = tex2D( NormalSampler, i.vUv ).xyz * 2.0f - 1.0f;
	n = normalize( tn.x * t + tn.y * b + tn.z * n );
#endif

	// fwidth floor = 1 px anti aliased line at edge 0
	// ^1.5 = more dark: threshold 0.5 cuts at 51 deg off view (was 60)
	float level = saturate( dot( n, normalize( i.vView ) ) );
	level *= sqrt( level );
	float width = max( max( cParams.y, fwidth( level ) ), 0.0001f );
	return lerp( cDarkColor, cLightColor, smoothstep( cParams.x - width, cParams.x + width, level ) );
}
