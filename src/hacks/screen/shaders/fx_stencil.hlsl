// Player stencil / internal glow / full-res player glow: botox hacks/visuals/screen/player_stencil.cpp
// shader strings, 1:1, one entry point each. SOURCE OF RECORD for fx_stencil_*.h.
// Rebuild after ANY edit here (from the repo root):  bash src/hacks/screen/shaders/fx_stencil_build.sh
//
// Mask texture: r = visible player pixels (stencil 2), g = occluded ones (stencil 1), written by
// visuals/player_stencil.cpp from the stencil aim/chams.cpp's glow pass leaves behind.
sampler2D s0 : register( s0 );
float4 c0 : register( c0 );
float4 c1 : register( c1 );
float4 c2 : register( c2 );
float4 c3 : register( c3 );
float4 c4 : register( c4 );
float4 c5 : register( c5 );
float4 c6 : register( c6 );
float4 c7 : register( c7 );
float4 c8 : register( c8 );
float4 c9 : register( c9 );
float4 c10 : register( c10 );
float4 c11 : register( c11 );
float4 c12 : register( c12 );   // outline wave: on, phase, 1/size, strength
float4 c13 : register( c13 );   // outline wave colour
float4 c14 : register( c14 );   // wave angle: glow cos sin, outline cos sin
float4 c15 : register( c15 );   // wave anchor: glow on, player box count, outline on
float4 pb[32] : register( c16 );   // per player: uv box x0 y0 x1 y1
float4 po[32] : register( c48 );   // per player: wave phase offset

float2 cls( float2 m ) { float v = step( m.g, m.r ); return step( 0.5, m.r + m.g ) * float2( v, 1.0 - v ); }

// wave coordinate: screen diagonal, or (on) height along the nearest player's box (0 head .. 1 feet) plus
// that player's phase offset, so the sweep rides the body and turning the view never slides it.
// r = cos sin of the angle slider, turns that direction; x in screen-height / body-height units so bands stay square
float wpos( float2 uv, float on, float2 r )
{
	float a = c0.y / c0.x;
	float t = dot( float2( uv.x * a, uv.y ), float2( r.x - r.y, r.y + r.x ) );
	if ( on < 0.5 || c15.y < 0.5 ) return t;
	float best = 1e9;
	[loop] for ( int i = 0; i < 32; i++ ) {
		if ( i >= c15.y ) break;
		float4 b = pb[i];
		float2 e = max( b.zw - b.xy, 1e-4 );
		float2 d = uv - ( b.xy + b.zw ) * 0.5;
		float m = max( abs( d.x ) / e.x, abs( d.y ) / e.y );
		if ( m < best ) { best = m; t = dot( float2( d.x * a, d.y ) / e.y, float2( -r.y, r.x ) ) + 0.5 + po[i].x; }
	}
	return t;
}

float4 ps_fill() : COLOR0 { return 1; }

float4 ps_rows( float2 uv : TEXCOORD0 ) : COLOR0
{
	float2 m = tex2Dlod( s0, float4( uv, 0, 0 ) ).rg;
	float far = c0.z + 2.0;
	float2 h = far * ( 1.0 - cls( m ) );
	[loop] for ( int i = 1; i <= 32; i++ ) {
		float fi = (float)i;
		if ( fi >= max( h.x, h.y ) ) break;
		float2 o = float2( c0.x * fi, 0 );
		float2 a = max( cls( tex2Dlod( s0, float4( uv + o, 0, 0 ) ).rg ), cls( tex2Dlod( s0, float4( uv - o, 0, 0 ) ).rg ) );
		h = min( h, lerp( far.xx, fi.xx, a ) );
	}
	return float4( h / 255.0, saturate( m.r + m.g ), 1 );
}

float2 row( float2 uv ) { return floor( tex2Dlod( s0, float4( uv, 0, 0 ) ).rg * 255.0 + 0.5 ); }
float3 dist( float2 uv )
{
	float3 s = tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb;
	float2 h = floor( s.rg * 255.0 + 0.5 );
	float2 d2 = h * h;
	[loop] for ( int i = 1; i <= 32; i++ ) {
		float fi = (float)i;
		if ( fi * fi >= max( d2.x, d2.y ) ) break;
		float2 o = float2( 0, c0.y * fi );
		float2 a = min( row( uv + o ), row( uv - o ) );
		d2 = min( d2, fi * fi + a * a );
	}
	return float3( sqrt( d2 ), s.b );
}

float2 gauss( float2 uv, float2 o, float2 m )
{
	float k = -6.125 / ( c7.z * c7.z );
	float2 s = m;
	float n = 1.0;
	[loop] for ( int i = 1; i <= 32; i++ ) {
		float fi = (float)i;
		if ( fi >= c7.z + 1.0 ) break;
		float w = exp( fi * fi * k );
		s += w * ( tex2Dlod( s0, float4( uv + o * fi, 0, 0 ) ).rg + tex2Dlod( s0, float4( uv - o * fi, 0, 0 ) ).rg );
		n += 2.0 * w;
	}
	return s / n;
}

float4 ps_blur( float2 uv : TEXCOORD0 ) : COLOR0
{
	float2 m = tex2Dlod( s0, float4( uv, 0, 0 ) ).rg;
	return float4( gauss( uv, float2( c0.x, 0 ), m ), saturate( m.r + m.g ), 1 );
}

float3 soft( float3 x ) { return x * pow( 1.0 + pow( x, 2.625 ), -0.381 ); }
float3 bright( float3 p ) { float3 e = p * dot( p, 1.0 ); return min( e, pow( max( e, 1.0 ), 0.35 ) ); }

float4 ps_glow( float2 uv : TEXCOORD0 ) : COLOR0
{
	float3 c = tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb;
	float2 b = gauss( uv, float2( 0, c0.y ), c.rg );
	float a = pow( soft( ( b.x + b.y ) * 6.75 ).x, 0.8 );
	float g = saturate( ( a - c7.x ) * c7.y );
	float4 v = lerp( float4( c5.rgb * c5.a, c5.a ), float4( c3.rgb * c3.a, c3.a ), g );
	float4 h = lerp( float4( c6.rgb * c6.a, c6.a ), float4( c4.rgb * c4.a, c4.a ), g );
	float w = 0.5 + 0.5 * cos( 6.2831853 * ( wpos( uv, c15.x, c14.xy ) * c8.z - c8.y ) );
	float k = w * w * w * c8.w * c8.x;
	v.rgb = lerp( v.rgb, c9.rgb * v.a, k );
	h.rgb = lerp( h.rgb, c9.rgb * h.a, k );
	if ( c7.w > 0.5 ) {
		float3 e = soft( 6.75 * ( bright( v.rgb ) * b.x + bright( h.rgb ) * b.y ) );
		return float4( e, pow( max( e.r, max( e.g, e.b ) ), 0.8 ) ) * ( 1.0 - c.b );
	}
	return lerp( h, v, b.x / max( b.x + b.y, 1e-5 ) ) * ( a * ( 1.0 - c.b ) );
}

float4 ps_inner( float2 uv : TEXCOORD0 ) : COLOR0
{
	float3 c = tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb;
	float2 b = gauss( uv, float2( 0, c0.y ), c.rg );
	float e = smoothstep( 0.5, 1.0, b.x + b.y ) * c.b * c7.x / max( b.x + b.y, 1e-5 );
	return float4( ( c10.rgb * c10.a * b.x + c11.rgb * c11.a * b.y ) * e, 0 );
}

// internal glow, bones: one quad per hitbox capsule, gaussian of the distance to the segment
// (sigma lerped along it) x mask coverage, so it never leaves the model. BLENDOP_MAX unions them.
float4 ps_capsule( float2 v : VPOS, float4 s : TEXCOORD0, float2 r : TEXCOORD1 ) : COLOR0
{
	float2 p = v + 0.5;
	float2 e = s.zw - s.xy;
	float t = saturate( dot( p - s.xy, e ) / max( dot( e, e ), 1e-4 ) );
	float q = length( p - s.xy - e * t ) / lerp( r.x, r.y, t );
	return float4( exp( -0.5 * q * q ) * tex2Dlod( s0, float4( p * c0.xy, 0, 0 ) ).rg, 0, 0 );
}

float4 ps_bone( float2 uv : TEXCOORD0 ) : COLOR0
{
	float2 b = tex2Dlod( s0, float4( uv, 0, 0 ) ).rg * c7.x;
	return float4( c10.rgb * c10.a * b.x + c11.rgb * c11.a * b.y, 0 );
}

float4 ps_columns( float2 uv : TEXCOORD0 ) : COLOR0
{
	float3 k = dist( uv );
	float4 col = lerp( c2, c1, saturate( 0.5 + 0.5 * ( k.y - k.x ) ) );
	float w = 0.5 + 0.5 * cos( 6.2831853 * ( wpos( uv, c15.z, c14.zw ) * c12.z - c12.y ) );
	col.rgb = lerp( col.rgb, c13.rgb, w * w * w * c12.w * c12.x );
	return float4( col.rgb, col.a * saturate( c0.w + 1.0 - min( k.x, k.y ) ) * ( 1.0 - k.z ) );
}

// ps_3_0 needs a vs: clip-space position + the capsule's two texcoords straight through
struct CAPSULE_VS { float4 pos : POSITION; float4 s : TEXCOORD0; float4 r : TEXCOORD1; };
CAPSULE_VS vs_capsule( CAPSULE_VS i ) { return i; }
