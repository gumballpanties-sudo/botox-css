#include "botoxnet.h"
#include "../web/websurface.h"
#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "../../globals/pinned_thread.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

bool g_botox_net_enable = false;
bool g_botox_net_esp    = true;
bool g_botox_net_web    = true;

// ============================================================================================
// Steam P2P transport (11-03 WEB2 rewrite, replaces the voice-piggyback placeholder -- see
// botoxnet.h header comment for the full rationale). Same ISteamNetworkingMessages flat exports
// and SteamNetworkingIdentity/SteamNetworkingMessage_t layout every flat-Steam-export file in
// this codebase already follows (avatar_cache.cpp included). Own channel id so this never
// collides with another feature's messages on the wire.
namespace
{
	struct SNIdentity { std::int32_t type; std::int32_t size; std::uint64_t steamID64; std::uint8_t pad[ 120 ]; };
	static_assert( sizeof( SNIdentity ) == 136, "SteamNetworkingIdentity is 136 bytes" );
	struct SNMessage
	{
		void* data; std::int32_t size; std::uint32_t conn; SNIdentity peer;
		std::int64_t connUserData, usecReceived, msgNumber;
		void* pfnFree; void* pfnRelease;
		std::int32_t channel, flags; std::int64_t userData;
	};
	static_assert( offsetof( SNMessage, peer ) == 16 && offsetof( SNMessage, channel ) == 192,
		"SteamNetworkingMessage_t layout" );

	constexpr int k_channel    = 0xB07C;             // n_botox_net::MAGIC, reused as the channel id
	constexpr int k_send_flags = 8 | 1 | 32;         // Reliable | NoNagle | AutoRestartBrokenSession

	using SNM_Get_t     = void* (*)();
	using SNM_Send_t    = int   (*)( void*, const SNIdentity*, const void*, std::uint32_t, int, int );
	using SNM_Recv_t    = int   (*)( void*, int, SNMessage**, int );
	using SNM_Accept_t  = bool  (*)( void*, const SNIdentity* );
	using SNM_Close_t   = bool  (*)( void*, const SNIdentity* );
	using SNM_Release_t = void  (*)( SNMessage* );

	struct transport_api_t
	{
		void* self = nullptr;
		SNM_Send_t send = nullptr; SNM_Recv_t recv = nullptr;
		SNM_Accept_t accept = nullptr; SNM_Close_t close = nullptr; SNM_Release_t release = nullptr;
		bool tried = false;
	};
	transport_api_t s_api;

	template< typename fn_t >
	fn_t resolve_export( HMODULE h, const char* name ) { return (fn_t)GetProcAddress( h, name ); }

	bool resolve_api()
	{
		if ( s_api.self ) return true;
		if ( s_api.tried ) return false;
		s_api.tried = true;
		HMODULE h = GetModuleHandleA( "steam_api64.dll" );
		if ( !h ) { DebugLog( "botox net: steam_api64.dll not loaded" ); return false; }
		auto get = resolve_export< SNM_Get_t >( h, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002" );
		s_api.send    = resolve_export< SNM_Send_t >( h, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser" );
		s_api.recv    = resolve_export< SNM_Recv_t >( h, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel" );
		s_api.accept  = resolve_export< SNM_Accept_t >( h, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser" );
		s_api.close   = resolve_export< SNM_Close_t >( h, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser" );
		s_api.release = resolve_export< SNM_Release_t >( h, "SteamAPI_SteamNetworkingMessage_t_Release" );
		s_api.self = ( get && s_api.send && s_api.recv && s_api.accept && s_api.close && s_api.release ) ? get() : nullptr;
		DebugLog( "botox net: ISteamNetworkingMessages %p (exports %d%d%d%d%d%d)", s_api.self, !!get,
			!!s_api.send, !!s_api.recv, !!s_api.accept, !!s_api.close, !!s_api.release );
		return s_api.self != nullptr;
	}

	SNIdentity ident( std::uint64_t sid ) { SNIdentity id{}; id.type = 16; id.size = 8; id.steamID64 = sid; return id; }

	// ---- mailbox: game thread <-> worker -------------------------------------------------
	// Fixed-size, no heap allocation under the lock. m_queue's own per-tick worst case is
	// hello + esp + web_place + web_basis + <=32 web_url chunks = 36, so 40 has headroom.
	constexpr int k_max_packets = 40;
	constexpr int k_max_peers   = 64;

	struct mail_in_t
	{
		bool active = false;
		std::uint64_t sids[ k_max_peers ]{}; int n_sids = 0;
		n_botox_net::packet_t packets[ k_max_packets ]{}; int n_packets = 0;
		std::uint32_t gen = 0;
	};
	struct recv_item_t { std::uint64_t sid = 0; n_botox_net::packet_t pkt{}; };
	struct mail_out_t
	{
		recv_item_t items[ k_max_peers ]{}; int n_items = 0;
		std::uint32_t gen = 0;
	};

	std::mutex s_mx;
	mail_in_t  s_in;
	mail_out_t s_out;
	std::atomic< bool > s_stop{ false };
	HANDLE s_thread = nullptr;

	void worker()
	{
		mail_in_t in{};
		std::uint32_t seen_in = ~0u;
		bool was_active = false;
		std::uint64_t known_sids[ k_max_peers ]{}; int n_known = 0;
		SNMessage* msgs[ 16 ];

		auto send_to = [ & ]( std::uint64_t sid, const void* data, std::size_t len )
		{
			const SNIdentity id = ident( sid );
			s_api.accept( s_api.self, &id );   // a session THEY opened is taken up; no-op otherwise
			s_api.send( s_api.self, &id, data, (std::uint32_t)len, k_send_flags, k_channel );
		};
		auto close_sid = [ & ]( std::uint64_t sid ) { const SNIdentity id = ident( sid ); s_api.close( s_api.self, &id ); };

		while ( !s_stop )
		{
			{ std::lock_guard< std::mutex > g( s_mx ); if ( s_in.gen != seen_in ) { in = s_in; seen_in = in.gen; } }

			if ( !in.active )
			{
				if ( was_active ) { for ( int i = 0; i < n_known; i++ ) close_sid( known_sids[ i ] ); n_known = 0; }
				was_active = false;
				Sleep( 50 );
				continue;
			}
			was_active = true;

			// drop sessions for sids no longer eligible (left the server, bot, etc.)
			for ( int i = 0; i < n_known; i++ )
			{
				bool still = false;
				for ( int j = 0; j < in.n_sids; j++ ) if ( in.sids[ j ] == known_sids[ i ] ) { still = true; break; }
				if ( !still ) close_sid( known_sids[ i ] );
			}
			n_known = in.n_sids;
			for ( int i = 0; i < n_known; i++ ) known_sids[ i ] = in.sids[ i ];

			mail_out_t out{};
			for ( ;; )
			{
				const int n = s_api.recv( s_api.self, k_channel, msgs, 16 );
				if ( n <= 0 ) break;
				for ( int i = 0; i < n; i++ )
				{
					SNMessage* m = msgs[ i ];
					if ( m->peer.type == 16 && m->size == (int)sizeof( n_botox_net::packet_t ) && out.n_items < k_max_peers )
					{
						recv_item_t& r = out.items[ out.n_items++ ];
						r.sid = m->peer.steamID64;
						std::memcpy( &r.pkt, m->data, sizeof( r.pkt ) );
					}
					s_api.release( m );
				}
			}

			for ( int p = 0; p < in.n_packets; p++ )
				for ( int j = 0; j < in.n_sids; j++ )
					send_to( in.sids[ j ], &in.packets[ p ], sizeof( n_botox_net::packet_t ) );

			{ std::lock_guard< std::mutex > g( s_mx ); ++out.gen; s_out = out; }
			Sleep( 50 );
		}
		for ( int i = 0; i < n_known; i++ ) close_sid( known_sids[ i ] );
	}

	// IVEngineClient::GetPlayerInfo, slot 8 -- counted off source-sdk-2013's cdll_int.h
	// IVEngineClient013 vtable (GetIntersectingSurfaces=0 .. ClientCmd=7, GetPlayerInfo=8),
	// matches globals.h's own already-verified slots on the same vtable (9 = GetPlayerForUserID,
	// 12 = GetLocalPlayer, 26 = IsInGame). Struct layout copied 1:1 from cdll_int.h (same engine
	// branch as TF2 x64, whose botox-tf2 port uses this exact struct at the same slot).
	struct player_info_sdk_t
	{
		char name[ 32 ]; int user_id; char guid[ 33 ]; std::uint32_t friends_id; char friends_name[ 32 ];
		bool fake_player; bool is_hltv; std::uint32_t custom_files[ 4 ]; std::uint8_t files_downloaded;
	};
	bool get_player_info( int ent, player_info_sdk_t& out ) { return g_engine && VCall< bool >( g_engine, 8, ent, &out ); }
	std::uint64_t steam64( std::uint32_t acct ) { return acct ? ( 0x0110000100000000ULL | acct ) : 0; }

	float now() { return CssBridge_CurTime(); }

	bool fresh( float stamp, float timeout )
	{
		const float age = now() - stamp;
		return stamp >= 0.f && age >= 0.f && age < timeout;
	}

	n_botox_net::packet_t make_packet( n_botox_net::e_packet_type type, const void* data, std::size_t size )
	{
		n_botox_net::packet_t packet{};
		packet.m_magic   = n_botox_net::MAGIC;
		packet.m_type    = type;
		packet.m_version = n_botox_net::VERSION;
		std::memcpy( packet.m_data, data, ( std::min )( size, sizeof( packet.m_data ) ) );
		return packet;
	}

	std::uint16_t hash16( const char* text, std::size_t size )
	{
		std::uint32_t h = 2166136261u;
		for ( std::size_t i = 0; i < size; i++ ) { h ^= (std::uint8_t)text[ i ]; h *= 16777619u; }
		return (std::uint16_t)( h ^ ( h >> 16 ) );
	}

	std::int16_t pack_unit( float value ) { return (std::int16_t)( std::clamp( value, -1.f, 1.f ) * 32767.f ); }

	bool unpack_dir( const std::int16_t packed[ 3 ], float out[ 3 ] )
	{
		for ( int i = 0; i < 3; i++ ) out[ i ] = packed[ i ] / 32767.f;
		const float len = std::sqrt( out[ 0 ] * out[ 0 ] + out[ 1 ] * out[ 1 ] + out[ 2 ] * out[ 2 ] );
		return len > 0.9f && len < 1.1f;
	}

	float dist3( const float a[ 3 ], const float b[ 3 ] )
	{
		const float dx = a[ 0 ] - b[ 0 ], dy = a[ 1 ] - b[ 1 ], dz = a[ 2 ] - b[ 2 ];
		return std::sqrt( dx * dx + dy * dy + dz * dz );
	}
}

namespace n_botox_net
{
	void impl_t::shared_t::at( float time, float out[ 3 ] ) const
	{
		const float f = std::clamp( ( time - m_time ) / m_interval, 0.f, 1.f );
		for ( int i = 0; i < 3; i++ ) out[ i ] = m_from[ i ] + ( m_origin[ i ] - m_from[ i ] ) * f;
	}

	bool impl_t::active() const { return g_botox_net_enable && CssBridge_InGame(); }
	bool impl_t::web_on() const { return active() && g_botox_net_web && Web_On(); }

	bool impl_t::send( const packet_t& packet )
	{
		// Queued here, drained by pump_transport() at the end of this same on_create_move
		// (Steam P2P isn't bandwidth-shared with real voice, so unlike the old voice-piggyback
		// plan there's no need to trickle a burst out over several ticks).
		m_queue.push_back( packet );
		return true;
	}

	void impl_t::receive( int sender, const packet_t& packet )
	{
		if ( packet.m_magic != MAGIC || packet.m_version != VERSION || sender <= 0 || sender > 64 ) return;
		m_user_seen[ sender ].m_time = now();

		switch ( packet.m_type )
		{
		case packet_hello:
			if ( packet.m_data[ 0 ] ) m_user_seen[ sender ].m_flags = packet.m_data[ 0 ];
			m_users.store( std::count_if( std::begin( m_user_seen ) + 1, std::end( m_user_seen ),
				[]( const user_t& u ) { return fresh( u.m_time, USER_TIMEOUT ); } ), std::memory_order_relaxed );
			break;

		case packet_esp:
		{
			esp_data_t d{}; std::memcpy( &d, packet.m_data, sizeof( d ) );
			if ( d.m_index < 1 || d.m_index > 64 ) break;
			shared_t& s = m_shared[ d.m_index ];
			const float* next = d.m_origin;
			if ( s.m_time >= 0.f ) s.at( now(), s.m_from );
			else std::memcpy( s.m_from, next, sizeof( s.m_from ) );
			if ( dist3( s.m_from, next ) > ESP_SNAP ) std::memcpy( s.m_from, next, sizeof( s.m_from ) );   // teleport: no glide
			std::memcpy( s.m_origin, next, sizeof( s.m_origin ) );
			s.m_health   = d.m_health;
			s.m_time     = now();
			break;
		}

		case packet_web_place:
		{
			web_place_data_t d{}; std::memcpy( &d, packet.m_data, sizeof( d ) );
			web_user_t& w = m_web[ sender ];
			w.m_pos[ 0 ] = d.m_pos[ 0 ]; w.m_pos[ 1 ] = d.m_pos[ 1 ]; w.m_pos[ 2 ] = d.m_pos[ 2 ];
			w.m_url_hash = d.m_url_hash; w.m_url_length = d.m_url_length;
			w.m_place_seq = d.m_seq & WEB_SEQ; w.m_have_place = true;
			if ( !( d.m_seq & WEB_PLACED ) ) { Web_PeerHide( sender ); w = web_user_t{}; }
			break;
		}

		case packet_web_basis:
		{
			web_basis_data_t d{}; std::memcpy( &d, packet.m_data, sizeof( d ) );
			web_user_t& w = m_web[ sender ];
			if ( !unpack_dir( d.m_normal, w.m_normal ) || !unpack_dir( d.m_right, w.m_right ) ) break;
			w.m_scale  = d.m_scale_centi / 100.f;
			w.m_aspect = d.m_aspect;
			w.m_basis_seq = d.m_seq; w.m_have_basis = true;
			break;
		}

		case packet_web_url:
		{
			web_url_data_t d{}; std::memcpy( &d, packet.m_data, sizeof( d ) );
			web_user_t& w = m_web[ sender ];
			if ( w.m_url_hash != d.m_url_hash ) { w.m_url_chunks = 0; std::memset( w.m_url, 0, sizeof( w.m_url ) ); w.m_url_hash = d.m_url_hash; }
			const std::size_t off = (std::size_t)d.m_chunk * WEB_URL_CHUNK;
			if ( off + WEB_URL_CHUNK <= sizeof( w.m_url ) )
			{
				std::memcpy( w.m_url + off, d.m_text, WEB_URL_CHUNK );
				if ( d.m_chunk < 32 ) w.m_url_chunks |= ( 1u << d.m_chunk );
			}
			break;
		}

		case packet_web_media:
		{
			web_media_data_t d{}; std::memcpy( &d, packet.m_data, sizeof( d ) );
			web_user_t& w = m_web[ sender ];
			if ( w.m_url_hash == d.m_url_hash ) Web_PeerMedia( sender, d.m_time, d.m_paused != 0 );
			break;
		}

		default: break;
		}
	}

	// botox network ESP shares each sender's OWN position/health (ground truth, no lag
	// compensation / fog of war) with the other botox clients on the server -- it is not a
	// wallhack over the whole player list, every sender only ever broadcasts itself.
	void impl_t::send_esp()
	{
		const int local_idx = CssBridge_LocalPlayerIndex();
		if ( local_idx < 1 || local_idx > 64 ) return;

		esp_data_t d{};
		if ( !CssBridge_GetOrigin( local_idx, d.m_origin ) ) return;
		d.m_index  = (std::uint8_t)local_idx;
		d.m_health = (std::uint8_t)std::clamp( CssBridge_GetHealth( local_idx ), 0, 255 );
		send( make_packet( packet_esp, &d, sizeof( d ) ) );
	}

	void impl_t::send_web()
	{
		web_share_t share{};
		static unsigned s_lastSeq = 0;
		static int s_urlChunk     = 0;

		if ( !Web_ShareGet( share ) )
		{
			if ( s_lastSeq ) { web_place_data_t d{}; d.m_seq = 0; send( make_packet( packet_web_place, &d, sizeof( d ) ) ); s_lastSeq = 0; }
			return;
		}
		if ( share.m_seq == s_lastSeq )
		{
			if ( share.m_media ) { web_media_data_t d{}; d.m_time = share.m_time; d.m_url_hash = hash16( share.m_url.c_str(), share.m_url.size() ); d.m_paused = share.m_paused; send( make_packet( packet_web_media, &d, sizeof( d ) ) ); }
			return;
		}
		s_lastSeq = share.m_seq;
		s_urlChunk = 0;

		const std::uint16_t urlHash = hash16( share.m_url.c_str(), share.m_url.size() );

		web_place_data_t p{};
		p.m_pos[ 0 ] = share.m_pos[ 0 ]; p.m_pos[ 1 ] = share.m_pos[ 1 ]; p.m_pos[ 2 ] = share.m_pos[ 2 ];
		p.m_url_hash = urlHash; p.m_url_length = (std::uint8_t)( std::min )( share.m_url.size(), (size_t)WEB_URL_MAX );
		p.m_seq = (std::uint8_t)( share.m_seq & WEB_SEQ ) | WEB_PLACED;
		send( make_packet( packet_web_place, &p, sizeof( p ) ) );

		web_basis_data_t b{};
		b.m_normal[ 0 ] = pack_unit( share.m_normal[ 0 ] ); b.m_normal[ 1 ] = pack_unit( share.m_normal[ 1 ] ); b.m_normal[ 2 ] = pack_unit( share.m_normal[ 2 ] );
		b.m_right[ 0 ]  = pack_unit( share.m_right[ 0 ] );  b.m_right[ 1 ]  = pack_unit( share.m_right[ 1 ] );  b.m_right[ 2 ]  = pack_unit( share.m_right[ 2 ] );
		b.m_scale_centi = (std::uint16_t)std::clamp( share.m_scale * 100.f, 0.f, 65535.f );
		b.m_aspect = (std::uint8_t)share.m_aspect; b.m_seq = (std::uint8_t)share.m_seq;
		send( make_packet( packet_web_basis, &b, sizeof( b ) ) );

		const int chunks = (int)( ( share.m_url.size() + WEB_URL_CHUNK - 1 ) / WEB_URL_CHUNK );
		for ( int c = 0; c < chunks && c < 32; c++ )
		{
			web_url_data_t u{};
			u.m_url_hash = urlHash; u.m_chunk = (std::uint8_t)c;
			const std::size_t off = (std::size_t)c * WEB_URL_CHUNK;
			std::memcpy( u.m_text, share.m_url.data() + off, ( std::min )( (size_t)WEB_URL_CHUNK, share.m_url.size() - off ) );
			send( make_packet( packet_web_url, &u, sizeof( u ) ) );
		}
	}

	void impl_t::apply_web( int sender )
	{
		web_user_t& w = m_web[ sender ];
		if ( !w.m_have_place || !w.m_have_basis || w.m_url_length == 0 ) return;
		const int needChunks = (int)( ( w.m_url_length + WEB_URL_CHUNK - 1 ) / WEB_URL_CHUNK );
		if ( needChunks > 0 && ( w.m_url_chunks & ( ( 1u << needChunks ) - 1 ) ) != ( ( 1u << needChunks ) - 1 ) ) return;

		web_share_t in{};
		in.m_placed = true;
		std::memcpy( in.m_pos, w.m_pos, sizeof( in.m_pos ) );
		std::memcpy( in.m_normal, w.m_normal, sizeof( in.m_normal ) );
		std::memcpy( in.m_right, w.m_right, sizeof( in.m_right ) );
		in.m_scale = w.m_scale; in.m_aspect = w.m_aspect;
		in.m_url.assign( w.m_url, w.m_url_length );
		w.m_applied = Web_PeerShow( sender, in );
	}

	void impl_t::drop_web_peers( bool stale_only )
	{
		for ( int i = 1; i <= 64; i++ )
		{
			web_user_t& w = m_web[ i ];
			if ( !w.m_applied ) continue;
			if ( stale_only && fresh( m_user_seen[ i ].m_time, USER_TIMEOUT ) ) continue;
			Web_PeerHide( i );
			w = web_user_t{};
		}
	}

	bool impl_t::shared_player( int index, float origin[ 3 ], int& health ) const
	{
		if ( index < 1 || index > 64 ) return false;
		const shared_t& s = m_shared[ index ];
		if ( !fresh( s.m_time, ESP_TIMEOUT ) ) return false;
		s.at( now(), origin );
		health = s.m_health;
		return true;
	}

	// Game thread, 1/s: refresh which connected player owns which SteamID64. Bots and players
	// whose friendsID hasn't resolved yet (brand new connection) stay 0 and are skipped by the
	// transport -- next second's scan picks them up once the engine has them.
	void impl_t::scan_players()
	{
		if ( !active() || fresh( m_last_scan, 1.f ) ) return;
		m_last_scan = now();
		m_sid_by_index = {};
		for ( int i = 1; i <= 64; i++ )
		{
			player_info_sdk_t pi{};
			if ( !get_player_info( i, pi ) || pi.fake_player || pi.is_hltv ) continue;
			m_sid_by_index[ i ] = steam64( pi.friends_id );
		}
	}

	// Game thread, every tick: hand this tick's outgoing packets + the current peer list to the
	// Steam P2P worker, and decode whatever it received since the last call into receive().
	void impl_t::pump_transport()
	{
		const bool on = active();
		if ( on && !s_thread && !s_stop && resolve_api() )
		{
			s_thread = CreatePinnedThread( [] { worker(); } );
			if ( !s_thread ) DebugLog( "botox net: worker CreateThread failed (err %lu)", GetLastError() );
		}

		mail_in_t in{};
		in.active = on && s_thread != nullptr;
		if ( in.active )
		{
			for ( int i = 1; i <= 64 && in.n_sids < k_max_peers; i++ )
				if ( m_sid_by_index[ i ] ) in.sids[ in.n_sids++ ] = m_sid_by_index[ i ];
			const int take = (int)( std::min )( m_queue.size(), (size_t)k_max_packets );
			for ( int i = 0; i < take; i++ ) in.packets[ i ] = m_queue[ i ];
			in.n_packets = take;
		}
		m_queue.clear();

		{ std::lock_guard< std::mutex > g( s_mx ); in.gen = s_in.gen + 1; s_in = in; }

		mail_out_t out;
		{
			std::lock_guard< std::mutex > g( s_mx );
			if ( s_out.gen == m_seen_out_gen ) return;
			m_seen_out_gen = s_out.gen;
			out = s_out;
		}
		for ( int i = 0; i < out.n_items; i++ )
		{
			int idx = 0;
			for ( int k = 1; k <= 64; k++ ) if ( m_sid_by_index[ k ] == out.items[ i ].sid ) { idx = k; break; }
			if ( idx ) receive( idx, out.items[ i ].pkt );
		}
	}

	void impl_t::on_create_move()
	{
		scan_players();

		if ( !active() ) { pump_transport(); return; }

		if ( !fresh( m_last_hello, 2.f ) )
		{
			m_last_hello = now();
			std::uint8_t flags = 0;
			if ( g_botox_net_esp ) flags |= hello_esp;
			if ( web_on() )        flags |= hello_web;
			send( make_packet( packet_hello, &flags, sizeof( flags ) ) );
		}

		if ( g_botox_net_esp ) send_esp();
		if ( web_on() )        send_web();
		else                   drop_web_peers( false );

		drop_web_peers( true );   // anyone who stopped sending hello drops their screen
		for ( int i = 1; i <= 64; i++ )
			if ( !m_web[ i ].m_applied ) apply_web( i );

		pump_transport();
	}

	void impl_t::on_frame_stage_notify( int ) { /* no per-stage work needed -- think runs from on_create_move */ }

	void impl_t::on_level_init()
	{
		m_shared       = {};
		m_user_seen    = {};
		m_web          = {};
		m_queue.clear();
		m_sid_by_index = {};
		m_last_scan    = -1.f;
		m_users.store( 0, std::memory_order_relaxed );
		m_last_hello   = -1.f;
	}

	// Stop + join the worker so the dll never unmaps underneath it. NEEDS FROM OTHERS: call at
	// eject, same spot the skin-sharing worker used to join (src/hooks/thread.cpp, near
	// GameExtras_Shutdown()).
	void impl_t::on_shutdown()
	{
		s_stop = true;
		if ( s_thread )
		{
			if ( WaitForSingleObject( s_thread, 3000 ) != WAIT_OBJECT_0 )
				DebugLog( "botox net: worker did not stop in 3 s" );
			CloseHandle( s_thread );
			s_thread = nullptr;
		}
	}
}
