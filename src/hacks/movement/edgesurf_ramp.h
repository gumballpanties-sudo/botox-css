#pragma once
/* ramp slide: edge surf on the side lip of a WALKABLE ramp. NOT csgo ( botox-tf2 research 11-25, its tests/push_tech_lab.cpp S1 ).
   Same file in botox-tf2 and botox-css: edit one, copy to the other. Needs the engine's +1/32 clip push ( TF2 sv_wallbug 0, CSS x64 ).
   The hull rides beside the ramp's side face with its feet in the slope plane's 1/32 band. Never grounded = no friction, so gravity
   runs it like a surf ramp ( lab: 36.9 deg from rest = 662 u/s over a 277u drop ). The pin is the flat ledge's: bump 0 crosses the face
   plane, bump 1 rounds back off it. Only the top is tilted.
   So the csgo core flies it unchanged, on a SHEARED world where that slope is a flat top:
     z'  = z - ztop( xy + the box's uphill corner ) + z0     ztop = the slope's height at xy
     vz' = vz + ( a . vxy ) / c                              a, c = the slope normal's xy, z
   why the core's rules hold there:
   - a box rests on a slope by its uphill corner, so every box touching the slope maps onto the same flat top
   - the face normal is square to the slope normal: the shear moves z by the along-face coordinate only. gaps, closing speeds and face
     ulps are the real ones, bit for bit
   - a free fall stays a free fall ( xy speed constant = z' loses a constant rate ): crossing( ) holds, and its top fraction is the
     engine's ( dist - eps ) / ( v . n dt )
   - a ride END reads vz' = pin + eps / c ( inside the pin tolerance ) and z' = top + eps: verdict( ) holds. `top` = z0 + eps / c - eps,
     so top + eps is the height the engine really stops the hull at ( 1/32 off the plane = eps / c straight up )
   every pin is flown in the engine before it is sent, and tick( ) below flies every other owned move once: a wrong model = no plan.
   Needs: static world brush ( no entity: a mover's plane is stale by the next tick ), AXIS ALIGNED side face square to the slope ( an
   extruded wedge: ramp brushes, stair clips ), slope walkable ( steeper = a surf ramp, the engine never grounds you there anyway ).
   Adapter knobs on top of the core's: B::probe_vz ( view::seen ), B::hit_ent = the last trace met an entity, not the world */
#include "edgesurf_core.h"
#include <cstring>

namespace n_esr
{
	using n_es::hit_t;
	using n_es::hull_t;
	using n_es::mk;
	using n_es::st_t;
	using n_es::v3;

	struct ramp_t {
		bool ok = false;
		double ax = 0., ay = 0., c = 1., ds = 0.; /* slope plane ax x + ay y + c z = ds, unit normal, c in [ 0.7, 0.999 ) */
		v3 fn{ };                                  /* side face: unit xy normal, out toward the hull */
		float fd = 0.f;                            /* fn . xy = fd */
		double z0 = 0.;                            /* the shear's constant: the slope's height under the hull's uphill corner when seated */
		float top = 0.f;                           /* the flat top the core sees */

		double ztop( const double x, const double y ) const { return ( ds - ax * x - ay * y ) / c; }
		/* the box corner that meets the slope first */
		double ox( const v3& mins, const v3& maxs ) const { return ax > 0. ? mins.x : maxs.x; }
		double oy( const v3& mins, const v3& maxs ) const { return ay > 0. ? mins.y : maxs.y; }
		float flat_z( const v3& p, const v3& mins, const v3& maxs ) const
		{
			return static_cast< float >( p.z - ztop( p.x + ox( mins, maxs ), p.y + oy( mins, maxs ) ) + z0 );
		}
		float real_z( const v3& p, const v3& mins, const v3& maxs ) const
		{
			return static_cast< float >( p.z + ztop( p.x + ox( mins, maxs ), p.y + oy( mins, maxs ) ) - z0 );
		}
		/* exact for a free fall ( crossing( ) plans on it ). a ride END ( tangent + eps n - g dt / 2 ) reads pin + eps / c: up to 0.0446 at
		   c 0.7, inside the adapters' 0.05 pin tolerance like the flat pin's + eps */
		float flat_vz( const v3& v ) const { return static_cast< float >( v.z + ( ax * v.x + ay * v.y ) / c ); }
		st_t flat( st_t s, const hull_t& h ) const
		{
			s.o.z = flat_z( s.o, h.mins, h.maxs );
			s.v.z = flat_vz( s.v );
			return s;
		}
		/* a hit plane, real -> sheared. vertical: untouched. parallel to the slope: a flat top where this box rests on it. the rest: the
		   plane the box's mapped points see ( floors stay floors: n.z = c of a level one ), good for the core's floor / steep reads only */
		void flat_plane( hit_t& h, const v3& mins, const v3& maxs ) const
		{
			if ( h.n.z == 0.f )
				return;
			if ( h.n.x * ax + h.n.y * ay + h.n.z * c > 0.99999 ) {
				h.d = static_cast< float >( top + ( h.d - ds ) / c );
				h.n = mk( 0.f, 0.f, 1.f );
				return;
			}
			const double k  = ztop( ox( mins, maxs ), oy( mins, maxs ) ) - z0;
			const double nx = h.n.x - h.n.z * ax / c, ny = h.n.y - h.n.z * ay / c, nz = h.n.z, l = std::sqrt( nx * nx + ny * ny + nz * nz );
			h.d = static_cast< float >( ( h.d - nz * k ) / l );
			h.n = mk( static_cast< float >( nx / l ), static_cast< float >( ny / l ), static_cast< float >( nz / l ) );
		}
	};

	/* one ramp read twice ( float noise on the normal moves ds by |xy| x 1e-6 ) */
	inline bool same( const ramp_t& a, const ramp_t& b )
	{
		return a.ax * b.ax + a.ay * b.ay + a.c * b.c > 0.999999 && std::fabs( a.ds - b.ds ) < 0.02 && a.fn.x * b.fn.x + a.fn.y * b.fn.y > 0.99999f &&
		       fabsf( a.fd - b.fd ) < 0.04f;
	}
	/* z' = z where the hull is now: the core's numbers stay on the hull's own float grid */
	inline void seat( ramp_t& r, const st_t& s, const hull_t& h )
	{
		r.z0  = r.ztop( s.o.x + r.ox( h.mins, h.maxs ), s.o.y + r.oy( h.mins, h.maxs ) );
		r.top = static_cast< float >( r.z0 + n_es::k_eps / r.c - n_es::k_eps );
		r.ok  = true;
	}

	/* a ramp's side face within range of the falling hull: the core's hull curtain ( feet down the fall, 8 yaws + your travel ), then the
	   top just inside the face read by a point. walkable slope, square to the face, the same plane 8u on along it ( a lip's chamfer tilts
	   toward the face = not square, a one brush notch has no twin ). world brushes only: props / entities / displacements pin by other rules.
	   the nearest face wins */
	template < class W >
	bool find( W& w, const st_t& s, const hull_t& h, const float range, ramp_t& out )
	{
		const float gz      = -2.f * w.pin / w.dt;
		constexpr float k_t = 0.75f;
		float dz            = std::clamp( -( s.v.z + w.pin ) * k_t + 0.5f * gz * k_t * k_t, 4.f, 256.f );
		const v3 lo = mk( h.mins.x, h.mins.y, 0.f ), hi = mk( h.maxs.x, h.maxs.y, 0.f );
		hit_t f;
		w.trace( s.o, mk( s.o.x, s.o.y, s.o.z - dz ), lo, hi, f );
		if ( !f.start_solid && f.frac < 1.f )
			dz = std::max( f.frac * dz - 0.5f, 0.5f );
		v3 dirs[ 9 ];
		int nd = 0;
		for ( int i = 0; i < 8; ++i ) {
			const float a = static_cast< float >( i < 4 ? i * 90 : ( i - 4 ) * 90 + 45 ) / n_es::k_rad;
			dirs[ nd++ ]  = mk( cosf( a ), sinf( a ), 0.f );
		}
		if ( const float vxy = n_es::speed_xy( s.v ); vxy >= 1.f )
			dirs[ nd++ ] = mk( s.v.x / vxy, s.v.y / vxy, 0.f );
		float best = FLT_MAX;
		for ( int i = 0; i < nd; ++i ) {
			hit_t t;
			w.trace( s.o, mk( s.o.x + dirs[ i ].x * range, s.o.y + dirs[ i ].y * range, s.o.z ), mk( h.mins.x, h.mins.y, -dz ), hi, t );
			if ( t.start_solid || t.frac >= 1.f || fabsf( t.n.z ) > 1e-4f || !t.moves || t.ladder || t.disp || !t.world || t.vp || w.hit_ent )
				continue;
			const float l = sqrtf( t.n.x * t.n.x + t.n.y * t.n.y );
			n_es::edge_t e;
			e.n = mk( t.n.x / l, t.n.y / l, 0.f );
			e.d = t.d / l;
			/* axis aligned side faces only: on a turned ramp the lip is a non axial edge, vbsp bevels it ( a chamfer plane between slope
			   and face for box traces ) and the pin's two bumps are not what the hull meets there. unproven = not grabbed. an axial
			   brush plane's normal is exact, so the gate is tight */
			if ( fabsf( e.n.x ) < 0.999999f && fabsf( e.n.y ) < 0.999999f )
				continue;
			const float g = fabsf( n_es::gap( e, s.o, h ) );
			if ( g >= best )
				continue;
			const float sa = n_es::along( e, s.o );
			/* the slope drops up to 24 tan 45 under the hull's far end: the point runs that much past the curtain */
			const auto probe = [ & ]( const float a, hit_t& p ) {
				const v3 q = mk( e.n.x * ( e.d - 0.5f ) - e.n.y * a, e.n.y * ( e.d - 0.5f ) + e.n.x * a, s.o.z + 1.f );
				p          = hit_t{ };
				w.trace( q, mk( q.x, q.y, s.o.z - dz - 26.f ), mk( 0.f, 0.f, 0.f ), mk( 0.f, 0.f, 0.f ), p );
				return !p.start_solid && p.frac < 1.f && p.n.z >= 0.7f && p.n.z < 0.999f && p.moves && !p.disp && p.world && !p.vp && !w.hit_ent;
			};
			hit_t p0, p1;
			/* square: 1e-3 off would leak 0.04 u/s of closing speed into vz', the whole pin tolerance margin at c 0.7 */
			if ( !probe( sa, p0 ) || fabsf( p0.n.x * e.n.x + p0.n.y * e.n.y ) > 1e-5f )
				continue;
			bool twin = false;
			for ( const float o : { 8.f, -8.f } )
				if ( probe( sa + o, p1 ) && p0.n.x * p1.n.x + p0.n.y * p1.n.y + p0.n.z * p1.n.z > 0.99999f && fabsf( p1.d - p0.d ) < 0.02f ) {
					twin = true;
					break;
				}
			if ( !twin )
				continue;
			const double nl = std::sqrt( static_cast< double >( p0.n.x ) * p0.n.x + static_cast< double >( p0.n.y ) * p0.n.y + static_cast< double >( p0.n.z ) * p0.n.z );
			best   = g;
			out    = ramp_t{ };
			out.ax = p0.n.x / nl;
			out.ay = p0.n.y / nl;
			out.c  = p0.n.z / nl;
			out.ds = p0.d / nl;
			out.fn = e.n;
			out.fd = e.d;
			out.ok = true;
		}
		return best < FLT_MAX;
	}

	/* the adapter the core runs on: B's engine, seen through the shear */
	template < class B >
	struct view : B {
		ramp_t rp;
		explicit view( const B& b ) : B( b ) { }
		/* CategorizePosition skips its ground probe over B::probe_vz ( the adapter's knob: TF2 250, tf_gamemovement.cpp:2358; CSS 140,
		   gamemovement.cpp NON_JUMP_VELOCITY ) = a rampslide: a hull over the ramp there is airborne now and lands the tick vz drops
		   under it. the lip ride has to stay beside the face, so that probe is read here: over the ramp = grounded, as it is at any
		   other speed ( model: 500 u/s up, the pin search drifted 1 ulp a tick onto the ramp ). the engine tests vz before FinishGravity:
		   a tick END's vz minus the pin ( -g dt / 2 ) */
		st_t seen( st_t s )
		{
			const hull_t h = B::hull( s.duck );
			if ( s.walk && !s.ground && s.v.z - B::pin > B::probe_vz ) {
				hit_t g;
				if ( B::trace( s.o, mk( s.o.x, s.o.y, s.o.z - 2.f ), h.mins, h.maxs, g ) && ( g.start_solid || g.n.z >= 0.7f ) )
					s.ground = true;
			}
			return rp.flat( s, h );
		}
		st_t start( ) { return seen( B::start( ) ); }
		st_t sim( const n_es::mv_t& m ) { return seen( B::sim( m ) ); }
		bool trace( const v3& a, const v3& b, const v3& mins, const v3& maxs, hit_t& h )
		{
			const bool hit = B::trace( mk( a.x, a.y, rp.real_z( a, mins, maxs ) ), mk( b.x, b.y, rp.real_z( b, mins, maxs ) ), mins, maxs, h );
			h.end.z        = rp.flat_z( h.end, mins, maxs );
			if ( hit && !h.start_solid )
				rp.flat_plane( h, mins, maxs );
			return hit;
		}
		/* the core's own lines, tagged: "ESURF: ramp act=C ..." ( top / pos z there are the sheared ones ) */
		void note( const char act, const char* line ) const
		{
			char buf[ 420 ];
			snprintf( buf, sizeof( buf ), "ESURF: ramp %s", !strncmp( line, "ESURF: ", 7 ) ? line + 7 : line );
			B::note( act, buf );
		}
	};

	/* one fall's ramp + the core's memory on it */
	struct slide_t {
		ramp_t rp{ };
		n_es::mem_t m{ };
	};
	inline void drop( slide_t& sl )
	{
		sl.rp.ok = false;
		n_es::new_fall( sl.m );
	}
	inline bool chained( const slide_t& sl, const int cmd_no ) { return sl.rp.ok && sl.m.own && sl.m.cmd == cmd_no - 1; }

	/* one cmd of ramp slide. an owned chain keeps its ramp ( the core's own scan ends the ride ), else the ramp is read fresh: the same
	   one keeps its seat ( the core's memory is in that frame ), a new one starts a new fall */
	template < class W >
	n_es::out_t tick( W& w, slide_t& sl, const int cmd_no, const n_es::in_t& in )
	{
		const st_t s0 = w.start( );
		if ( !chained( sl, cmd_no ) ) {
			const hull_t h = w.hull( s0.duck );
			ramp_t rp;
			if ( !s0.walk || s0.ground || !find( w, s0, h, std::max( in.range, in.reach ), rp ) ) {
				sl.rp.ok = false;
				return { };
			}
			if ( !( sl.rp.ok && same( sl.rp, rp ) ) ) {
				seat( rp, s0, h );
				sl.rp = rp;
				n_es::new_fall( sl.m );
				sl.m.up_cmd = -1; /* act Q's top was in the old ramp's frame */
			}
		}
		view< W > v( w );
		v.rp             = sl.rp;
		const float res0 = v.pin_res;
		/* a ride's hold window is 0.5 f0 / ( 1 - f0 ) face ulps wide, f0 = this tick's top fraction = the lift over the band / the fall
		   step. a full tick of push lifts 0.00047 ( f0 0.0026, the adapter's floor fits ), the tick after an arrival that hit late lifts a
		   few z ulps only: the bisection floor follows the window down ( model: arrival f0 0.86 = window 2.3e-4, a 2.5e-4 floor stepped
		   over it and the ride was dropped on its first tick ) */
		if ( const st_t s = v.seen( s0 ); s.walk && !s.ground && fabsf( s.v.z - v.pin ) < v.pin_tol ) {
			const float step = -v.dt * ( s.v.z + v.pin ), f0 = step > 0.f ? ( s.o.z - ( sl.rp.top + n_es::k_eps ) ) / step : 0.f;
			v.pin_res        = std::clamp( 0.125f * f0, 2e-5f, std::max( res0, 2e-5f ) );
		}
		n_es::out_t r = n_es::tick( v, sl.m, cmd_no, in );
		/* the core's pins and guide presses are flown, a few of its holds are not ( H beside / out of sims, K after a failed arrival ):
		   fine on a flat ledge it has flown for weeks, here the model is new. every owned move that is no pin is flown once more: an
		   END on the ground ( or off MOVETYPE_WALK ) = not sent, the cmd goes back. no sim left to fly it = not sent either */
		if ( r.own && !r.pin ) {
			bool ok = false;
			if ( v.sims_left( ) > 0 ) {
				v.reset( );
				const st_t e = v.sim( r.mv );
				ok           = e.walk && !e.ground;
			}
			if ( !ok ) {
				sl.m.own = false;
				sl.m.act = 'Y'; /* drawn as handed back */
				r        = { };
			}
		}
		/* back: the sim count. never the floor: the flat core may still run this cmd on w */
		v.pin_res              = res0;
		static_cast< W& >( w ) = v;
		return r;
	}
}
