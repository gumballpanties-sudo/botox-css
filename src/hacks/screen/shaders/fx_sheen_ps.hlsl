// Weapon sheen pixel shader (botox hacks/visuals/weapon_sheen.cpp k_ps, 1:1). SOURCE OF RECORD
// for fx_sheen_ps.h. Envmap reflection masked by an animated band along the model's own
// sweep axis (g_vSheenU/V, from the model's local bounding planes -- lsd_chams.cpp/model_planes
// equivalent), same math as botox: band.x = this frame's mask-core u, band.y = 1/width,
// band.z != 0 = "normal blend" (tinted envmap alone, no mask multiply), band.w = env strength.
//
// c30 (cLightScale) is NOT set by our own draw -- the caller reads whatever the engine's last
// draw left resident (ENV_MAP_SCALE: 16 under integer hdr, 1 without, shaderapidx8.cpp) and never
// writes it, same as botox's draw().
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_sheen_ps.h /Vn k_fxSheenPsBytes src/hacks/screen/shaders/fx_sheen_ps.hlsl
samplerCUBE EnvmapSampler : register( s0 );
sampler2D EnvmapMaskSampler : register( s1 );

float4 cLightScale : register( c30 );
float4 g_vSheenU : register( c6 );
float4 g_vSheenV : register( c7 );
float4 g_cSheenTint : register( c8 );
float4 g_vSheenBand : register( c9 );

struct PS_INPUT {
	float3 vWorldNormal : TEXCOORD0;
	float3 vWorldViewVector : TEXCOORD1;
	float3 vModelSpacePos : TEXCOORD2;
};

float4 main( PS_INPUT i ) : COLOR
{
	float3 vEyeDir          = -normalize( i.vWorldViewVector );
	float3 worldSpaceNormal = normalize( i.vWorldNormal );
	float3 vReflect         = 2 * worldSpaceNormal * dot( worldSpaceNormal, vEyeDir ) - vEyeDir;

	float3 envMapColor = cLightScale.z * texCUBE( EnvmapSampler, vReflect ).xyz * g_cSheenTint.xyz;

	float4 vPos            = float4( i.vModelSpacePos, 1.0f );
	float u                = g_vSheenBand.x + ( dot( vPos, g_vSheenU ) - g_vSheenBand.x ) * g_vSheenBand.y;
	float4 envmapMaskTexel = tex2D( EnvmapMaskSampler, float2( u, 1.0f - dot( vPos, g_vSheenV ) ) );

	// normal blend: tinted envmap at band.w strength (botox stock = 10), mask alone = alpha
	if ( g_vSheenBand.z > 0.5f )
		return float4( saturate( envMapColor * g_vSheenBand.w ), envmapMaskTexel.x * g_cSheenTint.w );

	envMapColor *= 10.0f;

	float alpha = max( max( envMapColor.x, envMapColor.y ), envMapColor.z );
	return float4( envMapColor.xyz * envmapMaskTexel.xyz, alpha * envmapMaskTexel.x * g_cSheenTint.w );
}
