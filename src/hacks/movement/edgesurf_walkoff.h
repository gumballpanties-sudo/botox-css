#pragma once
/* walk off entry: edge surf entered from the ledge you stand on, no jump. NOT csgo ( botox-tf2 research 11-25, its
   tests/push_tech_lab.cpp S4 ). Same file in botox-tf2 and botox-css: edit one, copy to the other.
   Standing feet sit at top + eps + d, d = the lift the clip push left when you landed ( up to 0.00047 ). Step off the side so the
   hull ENDs 1..6 face ulps beside the face: that air START is a ride START ( vz -g dt / 2 = the pin, feet in the top's band ) and
   the core's find_pin holds it from its first tick. d = 0 ( a step up seats the feet exactly ) = no hold window = no pin: the
   planner never takes the cmd there, your keys ( and an edge jump on the same key ) work as if the option were off.
   Per ground cmd:
     B brake   you run at the edge faster than the creep allows: the full press away from it, while that still ENDs on the ledge
     G creep   an across wish that ENDs at most half an air tick's authority closer, never past half the step range
     W step    in range: the across wish whose END is airborne 1..6 ulps beside the face, then find_pin from that END. sent only
               when it pins. that pin press is kept for the first air cmd ( air( ), act P ): after it the core rides as ever
     H hold    the step found no pin ( or the cmd's sims are too few for a whole try ): stay where you are on the ledge, the
               creep's outward speed would walk you off blind. k_fails steps with no pin = this stand is dead, the cmd is yours
               again until the planner skips a cmd ( you leave the ground, let the key go, tap jump )
     b         a cmd with no sims at all while the outward speed under you is the creep's: the one move that is NOT flown, a
               press back sized to stop that speed. away from the edge on the ground cannot walk you off; handing the cmd back
               there could
   B / G / W / H are the exact moves an engine sim flew. the along wish and the duck key stay yours ( your keys along the face ),
   the across wish is the planner's.
   The first air tick has to turn the step's outward speed back with one press: the step leaves at most 0.9 of that authority
   ( k_acc x max speed ), so the edge is left at a creep, never at a run */
#include "edgesurf_core.h"

namespace n_esw
{
	using n_es::edge_t;
	using n_es::hull_t;
	using n_es::mv_t;
	using n_es::st_t;

	constexpr float k_engage  = 16.f; /* the edge is ours from this far in at least: a 300 u/s run straight at it takes 12u of brake */
	constexpr int k_fails     = 3;    /* steps that reached the edge and found no pin: this stand is dead */
	constexpr int k_solve     = 20;   /* solve( ) at worst: 2 bracket sims + 18 steps */
	constexpr int k_step_sims = 70;   /* a step try at worst: its solve 20 + find_pin 48 ( coast, first, 18 bracket, 28 bisect ) */

	struct state_t {
		int cmd = -1; /* last cmd ground( ) ran */
		int fails = 0;
		bool dead = false, own = false, next_ok = false;
		mv_t next{ };   /* the first air cmd's pin press */
		edge_t face{ }; /* ...and its ledge */
		char act = 'N';
	};
	inline void reset( state_t& st ) { st = state_t{ }; }

	/* one press: a along the face ( the core's along( ) axis ), c across it ( + = off the ledge ) */
	inline mv_t wish_of( const edge_t& e, const float a, const float c, const bool duck )
	{
		const float x = -e.n.y * a + e.n.x * c, y = e.n.x * a + e.n.y * c, l = sqrtf( x * x + y * y );
		mv_t m;
		m.duck = duck;
		m.wish = std::min( l, n_es::k_wish_max );
		m.yaw  = l > 0.f ? atan2f( y, x ) * n_es::k_rad : 0.f;
		return m;
	}

	enum { k_none = 0, k_ok, k_short, k_over, k_bad };
	struct end_t {
		int kind = k_none;
		st_t s{ }; /* the engine sits on this END when kind is ok / short / over */
		mv_t mv{ };
		float g = 0.f;
	};
	/* the across wish whose END gap lands in [ lo, hi ]: the gap grows with the wish, so bracket it, then secant ( a bisection every
	   third step ). air = that END must be airborne ( the step ), else grounded ( the creep ). short = the full press off the ledge
	   still ENDs under lo, over = the full press back still ENDs past hi, bad = the gap fits but the ground state does not ( a floor
	   one step under the edge: you walk down it, no ledge ), none = out of sims / the wish's float grid */
	template < class W >
	end_t solve( W& w, const edge_t& e, const hull_t& h, const st_t& s, const float a, const bool duck, const float lo, const float hi, const bool air )
	{
		end_t r;
		float c_lo = 0.f, g_lo = 0.f, c_hi = 0.f, g_hi = 0.f;
		const auto fly = [ & ]( const float c ) {
			r.mv = wish_of( e, a, c, duck );
			w.reset( );
			r.s = w.sim( r.mv );
			r.g = n_es::gap( e, r.s.o, h );
			return r.g;
		};
		/* 1 = done ( r.kind set ), 0 = keep going */
		const auto judge = [ & ]( const float c, const float g ) {
			if ( !r.s.walk ) {
				r.kind = k_bad;
				return 1;
			}
			if ( g >= lo && g <= hi ) {
				r.kind = r.s.ground != air ? k_ok : k_bad;
				return 1;
			}
			if ( g < lo )
				c_lo = c, g_lo = g;
			else
				c_hi = c, g_hi = g;
			return 0;
		};
		if ( w.sims_left( ) < 2 )
			return r;
		const float g0 = fly( 0.f );
		if ( judge( 0.f, g0 ) )
			return r;
		const float c1 = g0 < lo ? n_es::k_wish_max : -n_es::k_wish_max;
		const float g1 = fly( c1 );
		if ( judge( c1, g1 ) )
			return r;
		if ( ( g0 < lo ) == ( g1 < lo ) ) {
			r.kind = g1 < lo ? k_short : k_over;
			return r;
		}
		for ( int i = 0; i < k_solve - 2; ++i ) {
			if ( w.sims_left( ) <= 0 ) {
				r.kind = k_none;
				return r;
			}
			const float mid = 0.5f * ( c_lo + c_hi ), tg = 0.5f * ( lo + hi );
			float c = i % 3 == 2 || !( g_hi > g_lo ) ? mid : c_lo + ( tg - g_lo ) * ( c_hi - c_lo ) / ( g_hi - g_lo );
			if ( !( c > c_lo && c < c_hi ) )
				c = mid;
			if ( !( c > c_lo && c < c_hi ) )
				break; /* the wish's float grid is spent */
			if ( judge( c, fly( c ) ) )
				return r;
		}
		r.kind = k_none;
		return r;
	}

	/* one ground cmd. wx, wy = your keys as a world direction ( unit, or 0 0 ): the edge they walk away from is not ours. duck = your
	   duck key: every move goes out with it as you hold it */
	template < class W >
	n_es::out_t ground( W& w, state_t& st, const int cmd_no, const n_es::in_t& in, const float wx, const float wy, const bool duck )
	{
		n_es::out_t r;
		const bool chain = st.cmd == cmd_no - 1;
		const bool mine  = chain && st.own; /* the last cmd was the planner's: so is the speed under your feet */
		if ( !chain ) {
			st.fails = 0;
			st.dead  = false;
		}
		st.cmd = cmd_no;
		st.own = st.next_ok = false;
		st.act              = 'N';
		const st_t s        = w.start( );
		if ( !s.walk || !s.ground || st.dead )
			return r;
		const hull_t h    = w.hull( s.duck );
		const float reach = std::max( in.reach, k_engage );
		edge_t c[ n_es::k_cand ];
		/* + 1: the sweep in from outside starts solid when the hull overlaps the top by its whole length */
		const int nc = n_es::scan( w, s, h, reach + 1.f, false, c, n_es::k_cand );
		/* the nearest edge of the top you stand on. a prop pins over its top only ( vphys ): no beside START there */
		int best = -1;
		float g  = -FLT_MAX;
		for ( int i = 0; i < nc; ++i ) {
			const float gi = n_es::gap( c[ i ], s.o, h );
			if ( gi >= 0.f || gi < -reach || gi <= g || n_es::vphys( c[ i ] ) || fabsf( s.o.z - ( c[ i ].top + n_es::k_eps ) ) >= n_es::k_ztol ||
			     wx * c[ i ].n.x + wy * c[ i ].n.y < -0.1f )
				continue;
			best = i;
			g    = gi;
		}
		if ( best < 0 )
			return r;
		const edge_t e   = c[ best ];
		const float u    = n_es::face_ulp( e, s.o );
		const float lift = s.o.z - ( e.top + n_es::k_eps );
		/* no lift under the feet = no hold window, whatever is pressed: never ours ( no brake, no creep, an edge jump keeps its run ) */
		if ( !( lift > 0.f ) ) {
			if ( w.debug ) {
				char buf[ 160 ];
				snprintf( buf, sizeof( buf ), "ESURF: walk no lift gap=%.5f top=%.4f z=%.6f ( land on the ledge first )", g, e.top, s.o.z );
				w.note( 'Y', buf );
			}
			return r;
		}
		/* what the first air tick turns back with one press ( friction 1 right off the ground: the adapter's k_acc says so ). the
		   duck key crops it as the stance does */
		const float av    = w.k_acc * w.max_speed * ( s.duck || duck ? w.crop : 1.f );
		const float range = 0.9f * av * w.dt;
		const float a     = ( wx * -e.n.y + wy * e.n.x ) * n_es::k_wish_max;
		const auto own = [ & ]( const char act, const mv_t& mv ) {
			r.own = true;
			r.mv  = mv;
			r.act = st.act = act;
			st.own         = true;
			if ( w.debug ) {
				char buf[ 256 ];
				snprintf( buf, sizeof( buf ), "ESURF: walk act=%c gap=%.5f top=%.4f n=%.3f %.3f lift=%.6f fails=%d xy=%.1f sims=%d", act, g, e.top, e.n.x, e.n.y, lift,
				          st.fails, n_es::speed_xy( s.v ), w.used( ) );
				w.note( act, buf );
			}
			return r;
		};
		/* act b ( header ): nothing can be flown this cmd. the creep's outward speed is stopped by a press back sized on the air
		   gain ( the ground's is the same or half of it: half a stop twice is a stop ). not ours = yours, untouched */
		const auto blind = [ & ]( ) {
			const float vo = -n_es::closing( e, s.v );
			if ( !mine || !( vo > 0.f ) || !( w.k_acc > 0.f ) )
				return r;
			return own( 'b', wish_of( e, a, std::max( -vo / w.k_acc, -n_es::k_wish_max ), duck ) );
		};
		if ( w.sims_left( ) < k_solve )
			return blind( );
		/* the move that keeps you where you stand ( else the full press back while that ENDs on the ledge ), flown: H / B */
		mv_t hold_mv;
		char hold_act = 0;
		const auto hold_solve = [ & ]( ) {
			const float lo = g - 0.02f, hi = std::min( g + 0.02f, -2.f * u );
			if ( !( lo < hi ) )
				return;
			const end_t q = solve( w, e, h, s, a, duck, lo, hi, false );
			if ( q.kind == k_ok )
				hold_mv = q.mv, hold_act = 'H';
			else if ( q.kind == k_over && q.s.walk && q.s.ground && q.g < -2.f * u )
				hold_mv = q.mv, hold_act = 'B';
		};
		/* p = no pin from the step's END ( counts: k_fails = dead ), b = too few sims this cmd for a whole try ( says nothing about the
		   ledge: never counts ), f = a floor one step under the edge ( no ledge: dead at once, you walk down it ). p / b keep you on
		   the ledge with the hold flown before the try */
		const auto miss = [ & ]( const char why ) {
			if ( ( why == 'p' && ++st.fails >= k_fails ) || why == 'f' )
				st.dead = true;
			if ( w.debug ) {
				char buf[ 200 ];
				snprintf( buf, sizeof( buf ), "ESURF: walk miss why=%c gap=%.5f lift=%.6f fails=%d dead=%d", why, g, lift, st.fails, st.dead ? 1 : 0 );
				w.note( 'w', buf );
			}
			if ( why == 'f' )
				return r;
			return hold_act ? own( hold_act, hold_mv ) : blind( );
		};
		bool closer = false;
		if ( -g <= range ) {
			/* the fallback first, while the cmd still has its sims ( the sim budget is a clock too: it can run out inside the try ) */
			hold_solve( );
			/* the whole try or none of it: a find_pin cut short by the budget is no verdict */
			if ( w.sims_left( ) < k_step_sims )
				return miss( 'b' );
			end_t q = solve( w, e, h, s, a, duck, 0.5f * u, 6.5f * u, true );
			if ( q.kind == k_ok ) {
				/* the engine sits on the step's END: the pin searched from there. its hold window is 0.5 f0 face ulps wide ( f0 = the lift
				   over the band / the fall step ): the search floor follows a small lift down, as edgesurf_ramp.h's tick does */
				const float res0 = w.pin_res, step = -w.dt * ( q.s.v.z + w.pin );
				const float f0   = step > 0.f ? ( q.s.o.z - ( e.top + n_es::k_eps ) ) / step : 0.f;
				mv_t b;
				bool pin = false;
				if ( w.save( 0 ) ) {
					w.pin_res = std::clamp( 0.125f * f0, 2e-5f, std::max( res0, 2e-5f ) );
					pin       = n_es::find_pin( w, e, h, q.s, [ & ] { w.load( 0 ); }, n_es::face_ulp( e, q.s.o ), b );
					w.pin_res = res0;
				}
				if ( !pin )
					return miss( w.sims_left( ) <= 0 ? 'b' : 'p' );
				st.next    = b;
				st.next_ok = true;
				st.face    = e;
				st.fails   = 0;
				return own( 'W', q.mv );
			}
			if ( q.kind == k_bad )
				return miss( 'f' );
			if ( q.kind == k_none )
				return miss( 'b' );
			if ( q.kind == k_over )
				return r; /* you leave the edge this tick whatever is pressed: yours */
			closer = true;
		}
		/* creep: half an air tick's authority a cmd, never past half the step range. closer = the step could not cross from here */
		const float gt = closer ? 0.5f * g : std::min( g + 0.5f * av * w.dt, -0.5f * range );
		const float lo = gt - 0.02f, hi = std::min( gt + 0.02f, -2.f * u );
		if ( !( lo < hi ) )
			return hold_act ? own( hold_act, hold_mv ) : blind( );
		const end_t q = solve( w, e, h, s, a, duck, lo, hi, false );
		if ( q.kind == k_ok )
			return own( 'G', q.mv );
		/* too fast for the creep: the full press back, while its END is still on the ledge */
		if ( q.kind == k_over && q.s.walk && q.s.ground && q.g < -2.f * u )
			return own( 'B', q.mv );
		return q.kind == k_none ? blind( ) : r;
	}

	/* the first air cmd after a step: its proven pin press, flown once more from the real START */
	template < class W >
	n_es::out_t air( W& w, state_t& st, const int cmd_no )
	{
		n_es::out_t r;
		const bool go = st.next_ok && st.cmd == cmd_no - 1;
		st.next_ok    = false;
		st.own        = false;
		if ( !go || w.sims_left( ) <= 0 )
			return r;
		const st_t s = w.start( );
		if ( !s.walk || s.ground )
			return r;
		const int v = n_es::verdict( w, st.face, w.hull( s.duck ), w.sim( st.next ), n_es::speed_xy( s.v ) );
		if ( w.debug ) {
			char buf[ 160 ];
			snprintf( buf, sizeof( buf ), "ESURF: walk air pin=%d gap=%.3e vz=%.2f", v == n_es::k_pin ? 1 : 0, n_es::gap( st.face, s.o, w.hull( s.duck ) ), s.v.z );
			w.note( 'p', buf );
		}
		if ( v != n_es::k_pin )
			return r;
		r.own = r.pin = true;
		r.mv          = st.next;
		r.act = st.act = 'P';
		return r;
	}
}
