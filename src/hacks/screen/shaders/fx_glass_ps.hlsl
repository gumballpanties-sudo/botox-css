// Glass chams pixel shader (botox hacks/visuals/glass_chams.cpp k_ps, 1:1). SOURCE OF RECORD for fx_glass_ps.h.
// What's behind, pulled against the (normal mapped) normal: a lens flips it like real glass. rgb split, rim + sky glint.
// c30 tint ( display, alpha pre-scaled ), c31 refract ( world units ) / bump / alpha test on / alpha ref, c32 xy = 1 / rt size.
// s0 base ( alpha test ), s1 / s3 engine normal map ( fx_twotone_ps.hlsl has the TF2 sampler refs ), s15 frame copy.
// Rebuild after ANY edit here: bash src/hacks/screen/shaders/fx_cham_build.sh -> fx_glass_ps.h k_fxGlassPs[BUMP 0 / s1 / s3]
const float4 cTint : register( c30 );
const float4 cParams : register( c31 );
const float4 cScreen : register( c32 );

sampler BaseSampler : register( s0 );
#if BUMP_SAMPLER == 1
sampler NormalSampler : register( s1 );
#elif BUMP_SAMPLER == 3
sampler NormalSampler : register( s3 );
#endif
sampler FrameSampler : register( s15 );

struct PS_INPUT {
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float4 vTangent : TEXCOORD2;
	float3 vView : TEXCOORD3;
	float4 vBendTB : TEXCOORD4;
	float2 vBendN : TEXCOORD5;
	float2 vPixel : VPOS;
};

float4 main( PS_INPUT i ) : COLOR
{
	// material alpha test: hair, straps keep their cutout
	clip( cParams.z * ( tex2D( BaseSampler, i.vUv ).a - cParams.w ) );

	float3 tn = float3( 0.0f, 0.0f, 1.0f );
#if BUMP_SAMPLER
	tn = tex2D( NormalSampler, i.vUv ).xyz * 2.0f - 1.0f;
	tn = normalize( float3( tn.xy * cParams.y, tn.z ) );
#endif

	// phong_ps20b.fxc:502-553 frame
	float3 n    = normalize( i.vNormal );
	float3 t    = normalize( i.vTangent.xyz );
	float3 b    = cross( n, t ) * i.vTangent.w;
	float3 unit = normalize( tn.x * t + tn.y * b + tn.z * n );

	float2 uv   = ( i.vPixel + 0.5f ) * cScreen.xy;
	float2 bend = ( tn.x * i.vBendTB.xy + tn.y * i.vBendTB.zw + tn.z * i.vBendN ) * cParams.x;
	float3 seen = float3( tex2D( FrameSampler, uv - bend * 0.94f ).r, tex2D( FrameSampler, uv - bend ).g, tex2D( FrameSampler, uv - bend * 1.06f ).b );

	float3 view      = normalize( i.vView );
	float  rim       = 1.0f - saturate( dot( unit, view ) );
	float3 reflected = reflect( -view, unit );
	float  glint     = pow( rim, 4.0f ) * 0.35f + pow( saturate( reflected.z ), 32.0f ) * 0.4f;
	return float4( seen * cTint.rgb * ( 0.85f + rim * 0.3f ) + glint, cTint.a );
}
