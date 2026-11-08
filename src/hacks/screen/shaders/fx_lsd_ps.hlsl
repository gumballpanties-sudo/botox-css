// LSD chams pixel shader: shadertoy "70s Melt" 1:1 (botox hacks/visuals/lsd_chams.cpp k_ps).
// fragCoord from VPOS (gl origin = bottom left of the viewport). SOURCE OF RECORD for fx_lsd_ps.h.
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_lsd_ps.h /Vn k_fxLsdPsBytes src/hacks/screen/shaders/fx_lsd_ps.hlsl
float4 cLsd : register( c20 );
float4 cViewport : register( c21 );

float cosRange( float amt, float range, float minimum )
{
	return ( ( ( 1.0 + cos( radians( amt ) ) ) * 0.5 ) * range ) + minimum;
}

float4 main( float2 vpos : VPOS ) : COLOR
{
	const float brightness = 0.975;
	float time   = cLsd.x * 1.25;
	float2 res   = cViewport.zw;
	float2 frag  = float2( vpos.x - cViewport.x + 0.5, res.y - ( vpos.y - cViewport.y + 0.5 ) );
	float2 p     = ( 2.0 * frag - res ) / max( res.x, res.y );
	float ct     = cosRange( time * 5.0, 3.0, 1.1 );
	float xBoost = cosRange( time * 0.2, 5.0, 5.0 );
	float yBoost = cosRange( time * 0.1, 10.0, 5.0 );
	float fScale = cosRange( time * 15.5, 1.25, 0.5 );

	[loop] for ( int i = 1; i < 40; i++ ) {
		float _i    = float( i );
		float2 newp = p;
		newp.x += 0.25 / _i * sin( _i * p.y + time * cos( ct ) * 0.5 / 20.0 + 0.005 * _i ) * fScale + xBoost;
		newp.y += 0.25 / _i * sin( _i * p.x + time * ct * 0.3 / 40.0 + 0.03 * float( i + 15 ) ) * fScale + yBoost;
		p = newp;
	}

	float3 col = float3( 0.5 * sin( 3.0 * p.x ) + 0.5, 0.5 * sin( 3.0 * p.y ) + 0.5, sin( p.x + p.y ) ) * brightness;
	return float4( saturate( col ), cLsd.y );
}
