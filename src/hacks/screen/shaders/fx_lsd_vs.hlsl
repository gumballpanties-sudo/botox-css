// LSD chams vertex shader (botox hacks/visuals/lsd_chams.cpp k_vs, 1:1). SOURCE OF RECORD for fx_lsd_vs.h.
// Skinned studio position only. TF2 engine constants are CS:GO's (sdk2013 common_vs_fxc.h:83 cViewProj c8,
// :169 cModel[53] c58, :609 DecompressBoneWeights (w + 1) / 32768).
//
// Rebuild after ANY edit here (two variants, one header each):
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=0 /Fh src/hacks/screen/shaders/fx_lsd_vs0.h /Vn k_fxLsdVs0Bytes src/hacks/screen/shaders/fx_lsd_vs.hlsl
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=1 /Fh src/hacks/screen/shaders/fx_lsd_vs1.h /Vn k_fxLsdVs1Bytes src/hacks/screen/shaders/fx_lsd_vs.hlsl
float4x4 cViewProj : register( c8 );
float4x3 cModel[ 53 ] : register( c58 );

struct VS_INPUT {
	float4 vPos : POSITION;
	float4 vBoneWeights : BLENDWEIGHT;
	float4 vBoneIndices : BLENDINDICES;
};

float4 main( const VS_INPUT i ) : POSITION
{
	float4 vWeights = i.vBoneWeights;
#if COMPRESSED_VERTS
	vWeights = ( vWeights + 1 ) / 32768;
#endif
	int3 boneIndices = D3DCOLORtoUBYTE4( i.vBoneIndices ).xyz;
	float3 weights   = vWeights.xyz;
	weights[ 2 ]     = 1 - ( weights[ 0 ] + weights[ 1 ] );
	float4x3 blendMatrix = cModel[ boneIndices[ 0 ] ] * weights[ 0 ] + cModel[ boneIndices[ 1 ] ] * weights[ 1 ] + cModel[ boneIndices[ 2 ] ] * weights[ 2 ];

	return mul( float4( mul( i.vPos, blendMatrix ), 1.0f ), cViewProj );
}
