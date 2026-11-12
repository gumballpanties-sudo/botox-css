// Shader chams pixel shaders, one per MODE (= ChamsLayers index, chams_layers.h). SOURCE OF RECORD for fx_cham_ps.h,
// rebuilt by fx_cham_build.sh. Every look is computed here (no engine envmap / phong / bump), so it is the
// same at every graphics setting. Modes 14..23 are Atlas-TF2 base/render/shaders/model.hlsl 1:1
// (g_color = cColor, g_time = cTime.x). 1..11 = botox's material looks redone in shader: procedural env
// instead of cubemaps, camera key light instead of map lights. 24 = botox grid.
// c40 colour (display, alpha pre-scaled), c41 x = time (realtime mod 1024). s0 = carrier basetexture.
float4 cColor : register( c40 );
float4 cTime : register( c41 );
sampler BaseSampler : register( s0 );

struct PS_INPUT {
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float3 vEye : TEXCOORD2;
	float3 vModel : TEXCOORD3;
	float3 vAmbient : TEXCOORD4;
};

// ---- Atlas helpers ----
float light( float3 normal, float facing )
{
	const float sky = lerp( 0.35f, 1.0f, normal.z * 0.5f + 0.5f );
	return saturate( sky * 0.75f + facing * 0.25f );
}

float hash( float3 position )
{
	position = frac( position * 0.1031f );
	position += dot( position, position.zyx + 31.32f );
	return frac( ( position.x + position.y ) * position.z );
}

float value_noise( float3 position )
{
	const float3 cell = floor( position );
	const float3 part = frac( position );
	const float3 ease = part * part * ( 3.0f - 2.0f * part );

	const float x00 = lerp( hash( cell ), hash( cell + float3( 1, 0, 0 ) ), ease.x );
	const float x10 = lerp( hash( cell + float3( 0, 1, 0 ) ), hash( cell + float3( 1, 1, 0 ) ), ease.x );
	const float x01 = lerp( hash( cell + float3( 0, 0, 1 ) ), hash( cell + float3( 1, 0, 1 ) ), ease.x );
	const float x11 = lerp( hash( cell + float3( 0, 1, 1 ) ), hash( cell + float3( 1, 1, 1 ) ), ease.x );

	return lerp( lerp( x00, x10, ease.y ), lerp( x01, x11, ease.y ), ease.z );
}

float3 hue( float value )
{
	return saturate( abs( frac( value + float3( 0.0f, 2.0f / 3.0f, 1.0f / 3.0f ) ) * 6.0f - 3.0f ) - 1.0f );
}

float3 ramp( float value )
{
	const float  position = saturate( value * 4.0f - 0.5f ) * 3.0f;
	const float3 black    = float3( 0.0f, 0.0f, 0.0f );
	const float3 cyan     = float3( 0.0f, 230.0f, 255.0f ) / 255.0f;
	const float3 yellow   = float3( 255.0f, 230.0f, 0.0f ) / 255.0f;
	const float3 magenta  = float3( 255.0f, 60.0f, 230.0f ) / 255.0f;

	return position < 1.0f ? lerp( black, cyan, position ) : position < 2.0f ? lerp( cyan, yellow, position - 1.0f ) : lerp( yellow, magenta, position - 2.0f );
}

void surface( float3 normal, float3 eye, out float3 unit, out float facing )
{
	unit   = normalize( normal );
	facing = saturate( dot( unit, normalize( eye ) ) );
}

float4 aero( float3 body, float3 glow, float rim )
{
	const float inner   = lerp( 0.15f, 1.0f, pow( rim, 1.6f ) );
	const float outline = smoothstep( 0.80f, 0.90f, rim );

	return float4( body * inner + glow + outline, cColor.a );
}

// ---- botox looks ----
// studio env: bright sky, soft box overhead, horizon line, dark floor. stands in for every cubemap
float3 env( float3 d )
{
	const float up    = d.z;
	const float3 sky  = lerp( float3( 0.50f, 0.56f, 0.62f ), float3( 1.0f, 1.0f, 1.0f ), saturate( up * 1.4f + 0.1f ) );
	const float3 floor_ = lerp( float3( 0.40f, 0.38f, 0.36f ), float3( 0.08f, 0.07f, 0.06f ), saturate( -up * 3.0f ) );
	const float  box  = smoothstep( 0.80f, 0.86f, up ) * 0.8f;
	const float  line_ = pow( saturate( 1.0f - abs( up ) * 10.0f ), 3.0f ) * 0.7f;
	return ( up >= 0.0f ? sky : floor_ ) + box + line_;
}

// phong $phongfresnelranges: (1 - n.v)^2 blends min..mid..max (vertexlitgeneric_dx9_helper Fresnel)
float fresnel_ranges( float facing, float3 ranges )
{
	const float f = ( 1.0f - facing ) * ( 1.0f - facing );
	return f > 0.5f ? lerp( ranges.y, ranges.z, 2.0f * f - 1.0f ) : lerp( ranges.x, ranges.y, 2.0f * f );
}

struct lit_t {
	float3 unit;
	float  facing;
	float3 view;
	float3 reflected;
	float  diffuse;
};

lit_t lighting( PS_INPUT i )
{
	lit_t l;
	surface( i.vNormal, i.vEye, l.unit, l.facing );
	l.view      = normalize( i.vEye );
	l.reflected = reflect( -l.view, l.unit );
	// key = the camera, lifted: same light wherever the map's lights are or what mat_ settings drop
	const float3 key = normalize( l.view + float3( 0.0f, 0.0f, 0.6f ) );
	const float  lambert = dot( l.unit, key ) * 0.5f + 0.5f;   // $halflambert
	l.diffuse   = lambert * lambert;
	return l;
}

float spec( lit_t l, float exponent )
{
	const float3 key = normalize( l.view + float3( 0.0f, 0.0f, 0.6f ) );
	return pow( saturate( dot( l.reflected, key ) ), exponent );
}

float4 main( PS_INPUT i ) : COLOR
{
	float3 unit;
	float  facing;
	surface( i.vNormal, i.vEye, unit, facing );
	const float3 model = i.vModel;
	const float3 eye   = i.vEye;
	const float  time  = cTime.x;

#if MODE == 1 || MODE == 3   // flat, wireframe (fill mode from the carrier's $wireframe)
	return cColor;
#elif MODE == 2   // shaded: Atlas shaded
	const float mask = saturate( facing * 1.25f - 0.25f );
	return float4( lerp( cColor.rgb * light( unit, facing ), cColor.rgb, mask ), cColor.a );
#elif MODE == 4   // glow: additive envmap fresnel [0 1 2]
	const float rim = 1.0f - facing;
	return float4( cColor.rgb * rim * rim, cColor.a );
#elif MODE == 5   // metallic: additive envmap, $envmapcontrast 1
	const lit_t l   = lighting( i );
	const float3 e  = env( l.reflected );
	return float4( cColor.rgb * e * e * 0.6f, cColor.a );
#elif MODE == 6   // pearlescent: white phong boost 2 exp 10, sheen tinted by view angle
	const lit_t l     = lighting( i );
	const float3 pearl = lerp( float3( 1.0f, 1.0f, 1.0f ), hue( ( 1.0f - l.facing ) * 0.6f + 0.55f ), 0.45f );
	const float  s    = spec( l, 10.0f ) * 2.0f * fresnel_ranges( l.facing, float3( 0.5f, 0.5f, 0.5f ) );
	return float4( cColor.rgb * pearl * ( 0.35f + 0.65f * l.diffuse ) + s * pearl, cColor.a );
#elif MODE == 7   // ambient: debug/debugambientcube = the engine's ambient cube at this normal
	return float4( cColor.rgb * sqrt( saturate( i.vAmbient ) ), cColor.a );
#elif MODE == 8   // gold: $color2 [.2 .11 .02], envmap [.55 .3 .05] fresnel .6, phong [.6 .35 .08] boost 6 exp 128
	const lit_t l   = lighting( i );
	const float  f  = pow( 1.0f - l.facing, 5.0f );
	const float3 e  = env( l.reflected ) * float3( 0.55f, 0.3f, 0.05f ) * lerp( 1.0f, f, 0.6f ) * 1.6f;
	const float  s  = spec( l, 128.0f ) * 6.0f * fresnel_ranges( l.facing, float3( 0.7f, 0.8f, 1.0f ) );
	return float4( cColor.rgb * ( float3( 0.2f, 0.11f, 0.02f ) * l.diffuse * 2.0f + e + s * float3( 0.6f, 0.35f, 0.08f ) ), cColor.a );
#elif MODE == 9   // damascus: textured (carrier s0), phong boost 4 exp 16 [.5 .75 1]
	const lit_t l    = lighting( i );
	const float3 tex = tex2D( BaseSampler, i.vUv ).rgb;   // sampler srgb read off by the DIP: display value
	const float  s   = spec( l, 16.0f ) * 4.0f * fresnel_ranges( l.facing, float3( 0.5f, 0.75f, 1.0f ) );
	return float4( cColor.rgb * ( tex * ( 0.3f + 0.7f * l.diffuse ) + s ), cColor.a );
#elif MODE == 10   // crystal: see through, desaturated reflections + phong boost 2 exp 16
	const lit_t l   = lighting( i );
	const float3 e  = env( l.reflected );
	const float3 g  = lerp( dot( e, float3( 0.299f, 0.587f, 0.114f ) ).xxx, e, 0.1f );
	const float  f  = pow( 1.0f - l.facing, 3.0f );
	const float  s  = spec( l, 16.0f ) * 2.0f;
	return float4( cColor.rgb * ( g * ( 0.25f + 0.75f * f ) + s ), cColor.a * saturate( 0.3f + 0.7f * f + s ) );
#elif MODE == 11   // wet: additive, env fresnel 1 + phong boost 10 exp 200 [.2 .5 1]
	const lit_t l   = lighting( i );
	const float  f  = pow( 1.0f - l.facing, 5.0f );
	const float  s  = spec( l, 200.0f ) * 10.0f * fresnel_ranges( l.facing, float3( 0.2f, 0.5f, 1.0f ) );
	return float4( cColor.rgb * ( env( l.reflected ) * f + s ), cColor.a );
#elif MODE == 14   // fresnel
	return float4( cColor.rgb * 5.5f * light( unit, facing ) * ( 1.0f - pow( facing, 0.75f ) ), cColor.a );
#elif MODE == 15   // lava
	const float  scale  = 0.75f + 0.22f * sin( time * ( 6.2831853f / 12.7f ) );
	const float3 flow   = model * ( 0.09f / scale ) - float3( 0.0f, 0.0f, time * 0.6f );
	const float  heat   = saturate( value_noise( flow ) * 0.65f + value_noise( flow * 2.3f + 17.0f ) * 0.35f );
	const float  blend  = 0.78f + 0.17f * sin( time * ( 6.2831853f / 6.1f ) ) + 0.05f * sin( time * ( 6.2831853f / 2.3f ) );
	const float3 albedo = cColor.rgb * lerp( 1.0f, heat * 2.0f, blend ) + pow( heat, 6.0f ) * 0.5f;
	const float  mask   = saturate( facing * 1.25f - 0.25f );
	return float4( lerp( albedo * light( unit, facing ), albedo, mask ), cColor.a );
#elif MODE == 16   // pearl
	const float  shift      = ( 1.0f - facing ) * 0.8f + value_noise( model * 0.05f ) * 0.6f + time * 0.05f;
	const float3 iridescent = lerp( float3( 1.0f, 1.0f, 1.0f ), hue( shift ), 0.55f );
	const float3 reflected  = reflect( -normalize( eye ), unit );
	const float  sheen      = 0.2f + pow( saturate( reflected.z ), 24.0f ) * 0.8f + pow( facing, 48.0f ) * 0.5f;
	return float4( iridescent * cColor.rgb + sheen * 0.6f, cColor.a );
#elif MODE == 17   // bubble
	const float3 key = normalize( normalize( eye ) + float3( 0.0f, 0.0f, 0.5f ) );
	return float4( cColor.rgb * 2.0f * ramp( saturate( dot( unit, key ) ) ), cColor.a );
#elif MODE == 18   // neon (Atlas "lsd")
	const float  rim  = 1.0f - facing;
	const float  edge = rim * rim * rim;
	const float3 neon = hue( rim * 0.6f + ( model.z + model.x * 0.5f ) * ( 1.0f / 64.0f ) - time * 0.2f );
	return float4( cColor.rgb * ( 0.2f + rim * 0.3f ) + neon * ( 0.1f + edge * 1.5f ) + edge * edge * 0.4f, cColor.a );
#elif MODE == 19   // network
	const float  cell_size = 18.0f;
	const float3 position  = model / cell_size;
	const float3 corner    = floor( position - 0.5f );
	float nearest = 2.0f;
	[unroll] for ( int x = 0; x < 2; ++x ) {
		[unroll] for ( int y = 0; y < 2; ++y ) {
			[unroll] for ( int z = 0; z < 2; ++z ) {
				const float3 cell  = corner + float3( x, y, z );
				const float  seed  = hash( cell );
				const float3 phase = frac( seed * float3( 1.0f, 7.31f, 13.7f ) ) * 6.2831853f;
				const float3 node  = cell + 0.5f + 0.4f * sin( time * ( 0.6f + seed * 0.5f ) + phase );
				nearest = min( nearest, length( position - node ) );
			}
		}
	}
	const float link = pow( saturate( 1.0f - nearest / 0.6f ), 2.0f );
	return float4( cColor.rgb * ( 0.45f + link * 1.2f ), cColor.a * saturate( 0.2f + link ) );
#elif MODE == 20   // soap
	const float  rim       = 1.0f - facing;
	const float3 view      = normalize( eye );
	const float3 marble    = model * 0.035f;
	const float3 warp      = float3( value_noise( marble + float3( 0.0f, time * 0.12f, 0.0f ) ), value_noise( marble + float3( 5.2f, 1.3f, time * 0.1f ) ), 0.0f );
	const float  swirl     = value_noise( marble * 1.7f + warp * 2.5f + float3( 0.0f, time * 0.18f, 0.0f ) );
	const float  thickness = swirl * 1.4f + model.y * 0.012f + rim * 0.5f - time * 0.05f;
	const float3 film      = 0.5f + 0.5f * cos( 6.2831853f * ( thickness * float3( 1.0f, 1.15f, 1.3f ) + float3( 0.0f, 0.3f, 0.6f ) ) );
	const float  window    = smoothstep( 0.90f, 0.92f, dot( unit, normalize( view + float3( 0.0f, 0.0f, 1.1f ) ) ) );
	const float  bounce    = smoothstep( 0.95f, 0.97f, dot( unit, normalize( view - float3( 0.0f, 0.0f, 0.9f ) ) ) ) * 0.6f;
	return aero( lerp( float3( 0.85f, 0.92f, 1.0f ), film, 0.75f ) * cColor.rgb * 1.2f, window + bounce, rim );
#elif MODE == 21   // aurora
	const float  warp  = value_noise( model * 0.03f + float3( 0.0f, time * 0.08f, 0.0f ) ) * 3.0f;
	const float  green = pow( 1.0f - abs( sin( dot( model, float3( 0.04f, 0.02f, 0.07f ) ) + warp - time * 0.7f ) ), 24.0f );
	const float  blue  = pow( 1.0f - abs( sin( dot( model, float3( -0.03f, 0.05f, 0.06f ) ) + warp * 1.3f - time * 0.5f + 1.7f ) ), 30.0f );
	const float3 glow  = ( green * float3( 0.35f, 1.0f, 0.55f ) + blue * float3( 0.30f, 0.70f, 1.0f ) ) * cColor.rgb;
	return aero( float3( 0.05f, 0.35f, 0.75f ) * cColor.rgb, glow, 1.0f - facing );
#elif MODE == 22   // aquarium
	const float3 water   = model * 0.06f + float3( time * 0.2f, 0.0f, time * 0.15f );
	const float  caustic = pow( saturate( 1.0f - abs( value_noise( water ) - value_noise( water * 1.4f + float3( 5.2f, 1.3f, -time * 0.25f ) ) ) * 3.0f ), 10.0f );
	const float  shaft   = pow( 0.5f + 0.5f * sin( model.x * 0.18f + model.z * 0.11f + value_noise( model * 0.02f + float3( 0.0f, 0.0f, time * 0.1f ) ) * 4.0f + time * 0.6f ), 6.0f ) *
	                       smoothstep( 10.0f, 80.0f, model.y );
	const float3 body    = lerp( float3( 0.0f, 0.10f, 0.45f ), float3( 0.05f, 0.45f, 1.0f ), unit.z * 0.5f + 0.5f ) * cColor.rgb * 1.3f;
	const float3 glow    = ( caustic * float3( 0.44f, 0.68f, 0.8f ) + shaft * float3( 0.075f, 0.225f, 0.5f ) ) * cColor.rgb;
	return aero( body, glow, 1.0f - facing );
#elif MODE == 23   // sky
	const float  height = model.y + ( value_noise( model * 0.08f ) - 0.5f ) * 6.0f;
	const float  above  = smoothstep( 30.0f, 34.0f, height );
	const float  cloud  = value_noise( model * 0.07f + float3( time * 0.8f, 0.0f, 0.0f ) ) * 0.65f +
	                      value_noise( model * 0.16f + float3( time * 1.3f, 3.0f, 0.0f ) ) * 0.35f;
	const float3 air    = lerp( float3( 0.45f, 0.78f, 1.0f ), float3( 0.04f, 0.32f, 0.92f ), smoothstep( 30.0f, 85.0f, height ) );
	const float3 grass  = lerp( float3( 0.10f, 0.45f, 0.05f ), float3( 0.35f, 0.80f, 0.15f ), value_noise( model * 0.35f ) );
	const float  gloss  = smoothstep( 0.80f, 0.88f, dot( unit, normalize( normalize( eye ) + float3( 0.0f, 0.0f, 1.1f ) ) ) ) * 0.3f;
	const float  glow   = smoothstep( 0.55f, 0.72f, cloud ) * above * 0.45f + gloss;
	return aero( lerp( grass, air, above ) * cColor.rgb * 1.2f, glow, 1.0f - facing );
#elif MODE == 24   // grid: 4 unit bind pose cells (position, not uv: ride the bones), 3 plane families = rings + stripes on every limb
	const float3 p     = model / 4.0f;
	const float3 dist  = abs( frac( p - 0.5f ) - 0.5f ) / max( fwidth( p ) * 1.5f, 1e-5f );   // 1.5 px lines at any angle / range
	const float3 flat_ = pow( abs( unit ), 16.0f ) * 4.0f;                                     // only a face lying IN a plane is muted (would light whole)
	const float  line_ = saturate( 1.0f - min( min( dist.x + flat_.x, dist.y + flat_.y ), dist.z + flat_.z ) );
	return float4( cColor.rgb, cColor.a * line_ );
#else
	return cColor;
#endif
}
