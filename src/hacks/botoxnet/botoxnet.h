#pragma once
// INCLUDE RULE (docs/port/AGENT_BRIEF.md, 11-03 fix): never include aim/css.h, aim/defs.h or
// aim/sdk.h outside src/hacks/aim/ (ConVar/ICvar/RecvTable/etc. redefinition vs. src/globals/*
// -- confirmed C2011 in this unit's own compile check). Uses css_bridge.h (unit SDK2, plain
// types/free functions over entity indices) instead; see botoxnet.cpp NEEDS FROM OTHERS.
#include "../aim/css_bridge.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// botox network, CSS x64 port of csgo-sdk/hacks/network/botox_net.{h,cpp}.
//
// CS:GO's protocol carried 6 payload kinds piggybacked inside voice packets: hello, esp,
// skin, extra (name tag / stattrak / stickers), sticker, glove. skin/extra/sticker/glove are
// the CS:GO skin economy (paint kits, wear, StatTrak, stickers) -- Counter-Strike: Source
// (2004 engine, no Arms Deal) has NO skin/sticker/StatTrak/knife-economy system at all, so
// those packet kinds have no CSS target and are dropped, not faked (AGENT_BRIEF: "if something
// truly can't exist in CSS, say so, don't fake it"). What survives 1:1: ESP sharing (other
// botox users' position+health over the covert channel) and the web_* kinds (unit WEB's
// screen-sharing placement, basis, url, media-sync), which have nothing to do with the skin
// economy and port cleanly.
//
// Transport (11-03 WEB2 rewrite): CS:GO piggybacked bytes inside CNetMessagePB<CCLCMsg_VoiceData>/
// CSVCMsg_VoiceData (protobuf) -- CSS's engine (css-community-master/public/inetmsghandler.h)
// forward-declares CLC_VoiceData/SVC_VoiceData as classic bitbuf CNetMessage subclasses with NO
// member layout in any public header, and this project has no disassembler to RE the live
// engine.dll (see WEB's original NEEDS FROM OTHERS). Ported instead onto Steam's peer-to-peer
// channel -- the ISteamNetworkingMessages transport (flat steam_api64.dll exports, dumpbin
// /exports verified 11-03): SteamAPI_SteamNetworkingMessages_SteamAPI_v002, _SendMessageToUser,
// _ReceiveMessagesOnChannel, _AcceptSessionWithUser, _CloseSessionWithUser,
// SteamNetworkingMessage_t_Release. Peer discovery: IVEngineClient::GetPlayerInfo (slot 8,
// counted off source-sdk-2013's cdll_int.h IVEngineClient013 vtable; matches globals.h's own
// already-verified slots 9 GetPlayerForUserID and 12 GetLocalPlayer on the same vtable, same
// engine branch as TF2 x64) gives every connected player's `friendsID` (32-bit Steam account
// id) -> SteamID64. "Same server" is exactly how botox CS:GO's voice channel found peers too
// (AGENT_BRIEF: "botox csgo uses voice so peers are found on the same server"), so this is a
// behavioural match even though the wire underneath is different.

// botox: botox network toggles. No menu wired yet (unit SDK/MENU owns it) -- see report MENU.
extern bool g_botox_net_enable;   // "botox network"
extern bool g_botox_net_esp;      // "share esp"
extern bool g_botox_net_web;      // "share web screen"

namespace n_botox_net
{
	constexpr std::uint16_t MAGIC   = 0xB07C;
	constexpr std::uint8_t VERSION  = 1;         // CSS subset: hello/esp/web_* only, renumbered from CS:GO's v2
	constexpr int PAYLOAD_BYTES     = 16;
	constexpr float ESP_TIMEOUT     = 1.5f;
	constexpr float ESP_SNAP        = 256.f;     // teleport vs. glide cutoff, world units
	constexpr float USER_TIMEOUT    = 5.f;
	constexpr int WEB_URL_MAX       = 255;
	constexpr int WEB_URL_CHUNK     = 13;
	constexpr std::uint8_t WEB_SEQ    = 0x7F;
	constexpr std::uint8_t WEB_PLACED = 0x80;

	enum e_packet_type : std::uint8_t {
		packet_hello     = 1,
		packet_esp       = 2,
		packet_web_place = 3,
		packet_web_basis = 4,
		packet_web_url   = 5,
		packet_web_media = 6,
		packet_last      = packet_web_media,
	};

	enum e_hello_flags : std::uint8_t {
		hello_esp = 1 << 0,
		hello_web = 1 << 1,
	};

#pragma pack( push, 1 )
	struct packet_t {
		std::uint16_t m_magic;
		std::uint8_t m_type;
		std::uint8_t m_version;
		std::uint8_t m_data[ PAYLOAD_BYTES ];
	};

	struct esp_data_t {
		std::uint8_t m_index;
		std::uint8_t m_health;
		float m_origin[ 3 ];
	};

	/* m_seq: low 7 bits = sender's share event counter, WEB_PLACED = screen up */
	struct web_place_data_t {
		float m_pos[ 3 ];
		std::uint16_t m_url_hash;
		std::uint8_t m_url_length;
		std::uint8_t m_seq;
	};

	/* unit vectors * 32767 */
	struct web_basis_data_t {
		std::int16_t m_normal[ 3 ];
		std::int16_t m_right[ 3 ];
		std::uint16_t m_scale_centi;
		std::uint8_t m_aspect;
		std::uint8_t m_seq;
	};

	struct web_url_data_t {
		std::uint16_t m_url_hash;
		std::uint8_t m_chunk;
		char m_text[ WEB_URL_CHUNK ];
	};

	struct web_media_data_t {
		float m_time;
		std::uint16_t m_url_hash;
		std::uint8_t m_paused;
	};
#pragma pack( pop )

	static_assert( sizeof( packet_t ) == 20 );
	static_assert( offsetof( packet_t, m_data ) == 4 );
	static_assert( sizeof( esp_data_t ) <= PAYLOAD_BYTES );
	static_assert( sizeof( web_place_data_t ) == PAYLOAD_BYTES );
	static_assert( sizeof( web_basis_data_t ) <= PAYLOAD_BYTES );
	static_assert( sizeof( web_url_data_t ) == PAYLOAD_BYTES );
	static_assert( sizeof( web_media_data_t ) <= PAYLOAD_BYTES );
	static_assert( ( WEB_URL_MAX + WEB_URL_CHUNK - 1 ) / WEB_URL_CHUNK <= 32 );

	struct impl_t {
		void on_create_move();
		void on_frame_stage_notify( int stage );
		void on_level_init();
		// Steam P2P worker join -- call at eject (NEEDS FROM OTHERS).
		void on_shutdown();

		bool shared_player( int index, float origin[ 3 ], int& health ) const;

		std::atomic< int > m_users{ 0 };

	private:
		bool active() const;
		bool web_on() const;
		bool send( const packet_t& packet );
		void receive( int sender, const packet_t& packet );
		void send_esp();
		void send_web();
		void apply_web( int sender );
		void drop_web_peers( bool stale_only );
		void scan_players();     // game thread, 1/s: builds m_sid_by_index via IVEngineClient::GetPlayerInfo
		void pump_transport();   // game thread: drains m_queue out, decoded packets in, over Steam P2P

		/* glides m_from -> m_origin over one packet gap */
		struct shared_t {
			float m_origin[ 3 ]{}, m_from[ 3 ]{};
			int m_health     = 0;
			float m_time     = -1.f;
			float m_interval = 0.1f;
			void at( float time, float out[ 3 ] ) const;
		};
		std::array< shared_t, 65 > m_shared{};

		struct user_t {
			float m_time         = -1.f;
			std::uint8_t m_flags = 0;
		};
		std::array< user_t, 65 > m_user_seen{};

		struct web_user_t {
			bool m_have_place          = false;
			bool m_have_basis          = false;
			std::uint8_t m_place_seq   = 0;
			std::uint8_t m_basis_seq   = 0;
			float m_pos[ 3 ]{};
			float m_normal[ 3 ]{};
			float m_right[ 3 ]{};
			float m_scale              = 1.f;
			int m_aspect               = 0;
			std::uint16_t m_url_hash   = 0;
			std::uint8_t m_url_length  = 0;
			std::uint32_t m_url_chunks = 0;
			char m_url[ WEB_URL_MAX + 1 ]{};
			bool m_applied             = false;
		};
		std::array< web_user_t, 65 > m_web{};

		std::vector< packet_t > m_queue{};          // built by send(), drained by pump_transport() every tick

		// Steam P2P peer discovery, refreshed once a second by scan_players(). Index 1..64 by
		// player slot, 0 = no SteamID known yet (bot, not yet resolved, or empty slot).
		std::array< std::uint64_t, 65 > m_sid_by_index{};
		float m_last_scan      = -1.f;
		std::uint32_t m_seen_out_gen = 0;   // last worker mailbox generation drained

		float m_last_hello     = -1.f;
	};
}

inline n_botox_net::impl_t g_botox_net{};
