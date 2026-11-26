// Player stencil / internal glow / full-res player glow: botox hacks/visuals/screen/player_stencil.cpp
// shader strings, 1:1, one entry point each. SOURCE OF RECORD for fx_stencil_*.h.
// Rebuild after ANY edit here (from the repo root):  bash src/hacks/screen/shaders/fx_stencil_build.sh
//
// Mask texture: r g b = class 1 2 3. Players: r = visible (stencil 2), g = occluded (stencil 1), b empty,
// written by visuals/player_stencil.cpp from the stencil aim/chams.cpp's glow pass leaves behind.
// model glow (aim/chams.cpp ModelGlow_*): viewmodel = r, dropped weapons r = seen, g = hidden.
// Coverage ("M") is read straight off the mask on s1 by the passes that need it (botox 11-26).
sampler2D s0 : register( s0 );
sampler2D s1 : register( s1 );
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
float4 c80 : register( c80 );   // class 3 outline colour
float4 c81 : register( c81 );   // class 3 glow inner
float4 c82 : register( c82 );   // class 3 glow outer

// class = strongest channel (ties to the lower class), body = sum >= 0.5. 2 classes (b = 0) = the old float2 cls
float3 cls( float3 m )
{
	float a = step( max( m.g, m.b ), m.r );
	float b = ( 1.0 - a ) * step( m.b, m.g );
	return step( 0.5, dot( m, 1.0 ) ) * float3( a, b, 1.0 - a - b );
}

float cover( float2 uv ) { return saturate( dot( tex2Dlod( s1, float4( uv, 0, 0 ) ).rgb, 1.0 ) ); }

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
	float3 m = tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb;
	float far = c0.z + 2.0;
	float3 h = far * ( 1.0 - cls( m ) );
	[loop] for ( int i = 1; i <= 32; i++ ) {
		float fi = (float)i;
		if ( fi >= max( h.x, max( h.y, h.z ) ) ) break;
		float2 o = float2( c0.x * fi, 0 );
		float3 a = max( cls( tex2Dlod( s0, float4( uv + o, 0, 0 ) ).rgb ), cls( tex2Dlod( s0, float4( uv - o, 0, 0 ) ).rgb ) );
		h = min( h, lerp( far.xxx, fi.xxx, a ) );
	}
	return float4( h / 255.0, 1 );
}

float3 row( float2 uv ) { return floor( tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb * 255.0 + 0.5 ); }
float4 dist( float2 uv )
{
	float3 h = row( uv );
	float3 d2 = h * h;
	[loop] for ( int i = 1; i <= 32; i++ ) {
		float fi = (float)i;
		if ( fi * fi >= max( d2.x, max( d2.y, d2.z ) ) ) break;
		float2 o = float2( 0, c0.y * fi );
		float3 a = min( row( uv + o ), row( uv - o ) );
		d2 = min( d2, fi * fi + a * a );
	}
	return float4( sqrt( d2 ), cover( uv ) );
}

float3 gauss( float2 uv, float2 o, float3 m )
{
	float k = -6.125 / ( c7.z * c7.z );
	float3 s = m;
	float n = 1.0;
	[loop] for ( int i = 1; i <= 32; i++ ) {
		float fi = (float)i;
		if ( fi >= c7.z + 1.0 ) break;
		float w = exp( fi * fi * k );
		s += w * ( tex2Dlod( s0, float4( uv + o * fi, 0, 0 ) ).rgb + tex2Dlod( s0, float4( uv - o * fi, 0, 0 ) ).rgb );
		n += 2.0 * w;
	}
	return s / n;
}

float4 ps_blur( float2 uv : TEXCOORD0 ) : COLOR0
{
	return float4( gauss( uv, float2( c0.x, 0 ), tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb ), 1 );
}

float3 soft( float3 x ) { return x * pow( 1.0 + pow( x, 2.625 ), -0.381 ); }
float3 bright( float3 p ) { float3 e = p * dot( p, 1.0 ); return min( e, pow( max( e, 1.0 ), 0.35 ) ); }

// normal blend: colour = blurred share of each class. 2 classes (b.z = 0) = lerp( h, v, b.x / ( b.x + b.y ) )
float4 ps_glow( float2 uv : TEXCOORD0 ) : COLOR0
{
	float3 b = gauss( uv, float2( 0, c0.y ), tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb );
	float m = cover( uv );
	float a = pow( soft( dot( b, 1.0 ) * 6.75 ).x, 0.8 );
	float g = saturate( ( a - c7.x ) * c7.y );
	float4 v = lerp( float4( c5.rgb * c5.a, c5.a ), float4( c3.rgb * c3.a, c3.a ), g );
	float4 h = lerp( float4( c6.rgb * c6.a, c6.a ), float4( c4.rgb * c4.a, c4.a ), g );
	float4 t = lerp( float4( c82.rgb * c82.a, c82.a ), float4( c81.rgb * c81.a, c81.a ), g );
	float w = 0.5 + 0.5 * cos( 6.2831853 * ( wpos( uv, c15.x, c14.xy ) * c8.z - c8.y ) );
	float k = w * w * w * c8.w * c8.x;
	v.rgb = lerp( v.rgb, c9.rgb * v.a, k );
	h.rgb = lerp( h.rgb, c9.rgb * h.a, k );
	t.rgb = lerp( t.rgb, c9.rgb * t.a, k );
	if ( c7.w > 0.5 ) {
		float3 e = soft( 6.75 * ( bright( v.rgb ) * b.x + bright( h.rgb ) * b.y + bright( t.rgb ) * b.z ) );
		return float4( e, pow( max( e.r, max( e.g, e.b ) ), 0.8 ) ) * ( 1.0 - m );
	}
	return ( v * b.x + h * b.y + t * b.z ) * ( a * ( 1.0 - m ) / max( dot( b, 1.0 ), 1e-5 ) );
}

float4 ps_inner( float2 uv : TEXCOORD0 ) : COLOR0
{
	float3 b = gauss( uv, float2( 0, c0.y ), tex2Dlod( s0, float4( uv, 0, 0 ) ).rgb );
	float e = smoothstep( 0.5, 1.0, b.x + b.y ) * cover( uv ) * c7.x / max( b.x + b.y, 1e-5 );
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

// nearer class colour, 1 px blend: weight = sat( 0.5 + 0.5 ( nearest other - own ) ). 2 classes =
// lerp( c2, c1, sat( 0.5 + 0.5 ( dh - dv ) ) ); an absent class sits at far >= T + 2, weight 0 wherever alpha > 0
float4 ps_columns( float2 uv : TEXCOORD0 ) : COLOR0
{
	float4 k = dist( uv );
	float3 q = saturate( 0.5 + 0.5 * ( min( k.yzx, k.zxy ) - k.xyz ) );
	float4 col = ( c1 * q.x + c2 * q.y + c80 * q.z ) / max( dot( q, 1.0 ), 1e-5 );
	float w = 0.5 + 0.5 * cos( 6.2831853 * ( wpos( uv, c15.z, c14.zw ) * c12.z - c12.y ) );
	col.rgb = lerp( col.rgb, c13.rgb, w * w * w * c12.w * c12.x );
	return float4( col.rgb, col.a * saturate( c0.w + 1.0 - min( k.x, min( k.y, k.z ) ) ) * ( 1.0 - k.w ) );
}

// ps_3_0 needs a vs: clip-space position + the capsule's two texcoords straight through
struct CAPSULE_VS { float4 pos : POSITION; float4 s : TEXCOORD0; float4 r : TEXCOORD1; };
CAPSULE_VS vs_capsule( CAPSULE_VS i ) { return i; }
