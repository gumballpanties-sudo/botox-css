// Shader chams vertex shader (Atlas-TF2 base/render/shaders/model.hlsl skinned_lit, fed by the engine's own
// constants like fx_twotone_vs.hlsl). SOURCE OF RECORD for fx_cham_vs.h, rebuilt by fx_cham_build.sh.
// sdk2013 common_vs_fxc.h:75 cEyePosWaterZ c2, :83 cViewProj c8, :104-106 cAmbientCube c21/c23/c25, :169 cModel[53] c58.
// Out: uv, world normal, eye - world, bind pose position (Atlas "model"), engine ambient cube at the normal.
float4 cEyePos : register( c2 );
float4x4 cViewProj : register( c8 );
float3 cAmbientCubeX[ 2 ] : register( c21 );
float3 cAmbientCubeY[ 2 ] : register( c23 );
float3 cAmbientCubeZ[ 2 ] : register( c25 );
float4x3 cModel[ 53 ] : register( c58 );

struct VS_INPUT {
	float4 vPos : POSITION;
	float4 vBoneWeights : BLENDWEIGHT;
	float4 vBoneIndices : BLENDINDICES;
	float4 vNormal : NORMAL;
	float4 vTexCoord0 : TEXCOORD0;
};

struct VS_OUTPUT {
	float4 vPos : POSITION;
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float3 vEye : TEXCOORD2;
	float3 vModel : TEXCOORD3;
	float3 vAmbient : TEXCOORD4;
};

// common_vs_fxc.h _DecompressUByte4Normal
float3 decompress( float4 inputNormal )
{
	float2 ztSigns   = ( inputNormal.xy - 128.0f ) < 0;
	float2 xyAbs     = abs( inputNormal.xy - 128.0f ) - ztSigns;
	float2 xySigns   = ( xyAbs - 64.0f ) < 0;
	float3 n;
	n.xy  = ( abs( xyAbs - 64.0f ) - xySigns ) / 63.0f;
	n.z   = 1.0f - n.x - n.y;
	n     = normalize( n );
	n.xy *= 1 - 2 * xySigns;
	n.z  *= 1 - 2 * ztSigns.x;
	return n;
}

VS_OUTPUT main( const VS_INPUT i )
{
	float4 vWeights = i.vBoneWeights;
#if COMPRESSED_VERTS
	vWeights = ( vWeights + 1 ) / 32768;
#endif
	int3 boneIndices = D3DCOLORtoUBYTE4( i.vBoneIndices ).xyz;
	float3 weights   = vWeights.xyz;
	weights[ 2 ]     = 1 - ( weights[ 0 ] + weights[ 1 ] );
	float4x3 blendMatrix = cModel[ boneIndices[ 0 ] ] * weights[ 0 ] + cModel[ boneIndices[ 1 ] ] * weights[ 1 ] + cModel[ boneIndices[ 2 ] ] * weights[ 2 ];

#if COMPRESSED_VERTS
	float3 normal = decompress( i.vNormal );
#else
	float3 normal = i.vNormal.xyz;
#endif

	float3 world = mul( i.vPos, blendMatrix );
	float3 n     = normalize( mul( normal, ( float3x3 )blendMatrix ) );

	// common_vs_fxc.h:748 AmbientLight
	float3 nSquared   = n * n;
	int3   isNegative = n < 0.0;

	VS_OUTPUT o;
	o.vPos     = mul( float4( world, 1.0f ), cViewProj );
	o.vUv      = i.vTexCoord0.xy;
	o.vNormal  = n;
	o.vEye     = cEyePos.xyz - world;
	o.vModel   = i.vPos.xyz;
	o.vAmbient = nSquared.x * cAmbientCubeX[ isNegative.x ] + nSquared.y * cAmbientCubeY[ isNegative.y ] + nSquared.z * cAmbientCubeZ[ isNegative.z ];
	return o;
}
