// Weapon sheen vertex shader (botox hacks/visuals/weapon_sheen.cpp k_vs, 1:1). SOURCE OF RECORD
// for fx_sheen_vs0.h/fx_sheen_vs1.h. Same studio skinning + register layout as fx_lsd_vs.hlsl
// (cViewProj c8, cModel[53] c58 -- sdk2013 common_vs_fxc.h), plus world normal + view vector for
// the envmap reflection and model-space position for the UV sweep (lsd has neither: position only).
//
// Rebuild after ANY edit here (two variants, one header each):
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=0 /Fh src/hacks/screen/shaders/fx_sheen_vs0.h /Vn k_fxSheenVs0Bytes src/hacks/screen/shaders/fx_sheen_vs.hlsl
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=1 /Fh src/hacks/screen/shaders/fx_sheen_vs1.h /Vn k_fxSheenVs1Bytes src/hacks/screen/shaders/fx_sheen_vs.hlsl
float4 cEyePos : register( c2 );
float4x4 cViewProj : register( c8 );
float4x3 cModel[ 53 ] : register( c58 );

struct VS_INPUT {
	float4 vPos : POSITION;
	float4 vNormal : NORMAL;
	float4 vBoneWeights : BLENDWEIGHT;
	float4 vBoneIndices : BLENDINDICES;
};

struct VS_OUTPUT {
	float4 vProjPosition : POSITION;
	float3 vWorldNormal : TEXCOORD0;
	float3 vWorldViewVector : TEXCOORD1;
	float3 vModelSpacePos : TEXCOORD2;
};

float3 mul4x3( float4 v, float4x3 m ) { return float3( dot( v, transpose( m )[ 0 ] ), dot( v, transpose( m )[ 1 ] ), dot( v, transpose( m )[ 2 ] ) ); }
float3 mul3x3( float3 v, float3x3 m ) { return float3( dot( v, transpose( m )[ 0 ] ), dot( v, transpose( m )[ 1 ] ), dot( v, transpose( m )[ 2 ] ) ); }

VS_OUTPUT main( const VS_INPUT i )
{
	VS_OUTPUT o;
	float3 vObjNormal;
	float4 vWeights = i.vBoneWeights;
#if COMPRESSED_VERTS
	float2 ztSigns = ( i.vNormal.xy - 128.0f ) < 0;
	float2 xyAbs   = abs( i.vNormal.xy - 128.0f ) - ztSigns;
	float2 xySigns = ( xyAbs - 64.0f ) < 0;
	vObjNormal.xy  = ( abs( xyAbs - 64.0f ) - xySigns ) / 63.0f;
	vObjNormal.z   = 1.0f - vObjNormal.x - vObjNormal.y;
	vObjNormal     = normalize( vObjNormal );
	vObjNormal.xy *= lerp( float2( 1.0f, 1.0f ), float2( -1.0f, -1.0f ), xySigns );
	vObjNormal.z  *= lerp( 1.0f, -1.0f, ztSigns.x );
	vWeights       = ( vWeights + 1 ) / 32768;
#else
	vObjNormal = i.vNormal.xyz;
#endif
	int3 boneIndices = D3DCOLORtoUBYTE4( i.vBoneIndices ).xyz;
	float3 weights   = vWeights.xyz;
	weights[ 2 ]     = 1 - ( weights[ 0 ] + weights[ 1 ] );
	float4x3 blendMatrix = cModel[ boneIndices[ 0 ] ] * weights[ 0 ] + cModel[ boneIndices[ 1 ] ] * weights[ 1 ] + cModel[ boneIndices[ 2 ] ] * weights[ 2 ];

	float3 vWorldPosition = mul4x3( i.vPos, blendMatrix );
	o.vWorldNormal        = mul3x3( vObjNormal, ( float3x3 )blendMatrix );
	o.vProjPosition       = mul( float4( vWorldPosition, 1.0f ), cViewProj );
	o.vWorldViewVector     = normalize( vWorldPosition - cEyePos.xyz );
	o.vModelSpacePos      = i.vPos.xyz;
	return o;
}
