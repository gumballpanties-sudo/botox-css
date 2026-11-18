// Glass chams vertex shader (botox hacks/visuals/glass_chams.cpp k_vs, 1:1). SOURCE OF RECORD for fx_glass_vs.h.
// Two tone's vs (fx_twotone_vs.hlsl) + the tangent frame projected to screen uv.
// Rebuild after ANY edit here: bash src/hacks/screen/shaders/fx_cham_build.sh
//   -> fx_glass_vs.h k_fxGlassVs0 (float verts) / 1 (float + TANGENT stream) / 2 (compressed)
float4 cEyePos : register( c2 );
float4x4 cViewProj : register( c8 );
float4x3 cModel[ 53 ] : register( c58 );

struct VS_INPUT {
	float4 vPos : POSITION;
	float4 vBoneWeights : BLENDWEIGHT;
	float4 vBoneIndices : BLENDINDICES;
	float4 vNormal : NORMAL;
	float4 vTexCoord0 : TEXCOORD0;
#if HAS_TANGENT
	float4 vUserData : TANGENT;
#endif
};

struct VS_OUTPUT {
	float4 vPos : POSITION;
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float4 vTangent : TEXCOORD2;
	float3 vView : TEXCOORD3;
	float4 vBendTB : TEXCOORD4;
	float2 vBendN : TEXCOORD5;
};

// common_vs_fxc.h _DecompressUByte4NormalTangent 1:1
void decompress( float4 inputNormal, out float3 outputNormal, out float4 outputTangent )
{
	float4 ztztSignBits = ( inputNormal - 128.0f ) < 0;
	float4 xyxyAbs      = abs( inputNormal - 128.0f ) - ztztSignBits;
	float4 xyxySignBits = ( xyxyAbs - 64.0f ) < 0;
	float4 normTan      = ( abs( xyxyAbs - 64.0f ) - xyxySignBits ) / 63.0f;
	outputNormal.xy     = normTan.xy;
	outputTangent.xy    = normTan.zw;

	float4 xyxySigns = 1 - 2 * xyxySignBits;
	float4 ztztSigns = 1 - 2 * ztztSignBits;

	outputNormal.z    = 1.0f - outputNormal.x - outputNormal.y;
	outputNormal.xyz  = normalize( outputNormal.xyz );
	outputNormal.xy  *= xyxySigns.xy;
	outputNormal.z   *= ztztSigns.x;

	outputTangent.z    = 1.0f - outputTangent.x - outputTangent.y;
	outputTangent.xyz  = normalize( outputTangent.xyz );
	outputTangent.xy  *= xyxySigns.zw;
	outputTangent.z   *= ztztSigns.z;
	outputTangent.w    = ztztSigns.w;
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

	float3 normal;
	float4 tangent;
#if COMPRESSED_VERTS
	decompress( i.vNormal, normal, tangent );
#else
	normal = i.vNormal.xyz;
#if HAS_TANGENT
	tangent = i.vUserData;
#else
	tangent = float4( 1, 0, 0, 1 );
#endif
#endif

	float3 world = mul( i.vPos, blendMatrix );
	float3 n     = normalize( mul( normal, ( float3x3 )blendMatrix ) );
	float3 t     = normalize( mul( tangent.xyz, ( float3x3 )blendMatrix ) );
	float3 b     = cross( n, t ) * tangent.w;

	VS_OUTPUT o;
	o.vPos     = mul( float4( world, 1.0f ), cViewProj );
	o.vUv      = i.vTexCoord0.xy;
	o.vNormal  = n;
	o.vTangent = float4( t, tangent.w );
	o.vView    = cEyePos.xyz - world;

	/* clip space of each frame axis / depth -> uv ( y down ): projection is linear, so a normal mapped normal bends by the same
	   mix of these. depth floored at 100 so the viewmodel bends like a model 100 units out */
	float2 to_uv = float2( 0.5f, -0.5f ) / max( o.vPos.w, 100.0f );
	o.vBendTB    = float4( mul( float4( t, 0.0f ), cViewProj ).xy * to_uv, mul( float4( b, 0.0f ), cViewProj ).xy * to_uv );
	o.vBendN     = mul( float4( n, 0.0f ), cViewProj ).xy * to_uv;
	return o;
}
