#pragma once
// CSS port of botox csgo-sdk hacks/lua/lua_api.h.

#include <string>

#include "../../dependencies/lua/lauxlib.h"
#include "../../dependencies/lua/lua.h"
#include "../../dependencies/lua/lualib.h"

#include "lua.h"

struct UserCmd_t;

/* lua is built as c++ (lua_unity.cpp): include lua.h directly, never lua.hpp (extern "C" = link errors) */
namespace n_lua_api
{
	/* set only while the matching callback runs, null after: a kept cmd / event errors instead of dangling */
	inline UserCmd_t* s_cmd    = nullptr;
	inline bool* s_send_packet = nullptr;
	inline void* s_event       = nullptr;   // raw IGameEvent*, read via IGameEventVtbl
	inline bool s_in_paint     = false;
	inline n_lua::draw_model_t* s_draw = nullptr;
	/* bumped per draw: a model kept from an earlier draw must not write into this one */
	inline unsigned int s_draw_serial = 0;

	void open( lua_State* L );
	int traceback( lua_State* L );
	/* render thread, every frame ( lua.cpp on_paint ): frees render.image pictures 10 s after their last draw */
	void image_sweep( );
	/* right after the paint callbacks: render.blur split the background list, every later overlay draws after the merge */
	void blur_merge( );
	/* game console (tier0 Msg) */
	void console_msg( const std::string& text );

	void push_cmd( lua_State* L );
	void push_event( lua_State* L );
	void push_draw( lua_State* L );

	/* draw_model swaps: paths a script asked for load at FrameStageNotify start ( never inside a draw ),
	   reset on level change ( the engine frees client models ). lua thread only */
	void load_models( );
	void reset_models( );

	inline n_lua::script_t* script_of( lua_State* L )
	{
		return *static_cast< n_lua::script_t** >( lua_getextraspace( L ) );
	}
}
