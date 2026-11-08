// CSS port of botox csgo-sdk hacks/lua/lua_api.cpp. Non-aim (src/hacks/lua): NO aim/css.h,
// aim/defs.h, aim/sdk.h here -- entities are read through globals/globals.h + globals/common.h
// (GetClientEntity, GetAllClasses/FindPropOffset, VCall, IGameEventVtbl) exactly the way
// src/hooks/netvars.cpp and globals.h's own IsLocalPlayerAlive()/GetPlayerClass() already do,
// plus Vec3/UserCmd_t from hacks/movement/movement.h (the designated non-aim movement header).
//
// Differences from botox csgo-sdk, called out per function group in the port report:
//  - entity:get_prop / set_prop: walks ClientClass->RecvTable by name, same algorithm as
//    botox, using this repo's FindPropOffset/GetAllClasses (globals.h) instead of botox's
//    own recv-table walker. Identical result.
//  - entity:get_datafield / set_datafield: botox walks C_BaseEntity::GetPredDescMap()
//    (datamap_t/typedescription_t). This repo's own local mirror of that struct lives in
//    hacks/movement/airstuck_pure.h (AsDatamapOffset) off the MEMORY-confirmed vtable slot 15
//    (join-crash-1103) -- copied here verbatim rather than making movement.h's lua-unrelated
//    pure-math header a dependency of hacks/lua.
//  - entity:bone_position / hitbox_position: CS:S's animation bone-cache layout and
//    ICollideable OBB accessor are not exposed anywhere in non-aim code and no verified
//    vtable slot for them exists in this session's sources (CLAUDE.md RULE: never guess a
//    vtable slot). Both return nil; see port report.
//  - entity:mins_maxs / class_id: same reason (ICollideable::OBBMins/Maxs and
//    ClientClass::m_ClassID are not resolvable without a vtable slot this session can verify
//    for CS:S specifically) -- nil.
//  - config.* operates on config.cpp's CfgEntry table (config.h) instead of botox's
//    variable_object_t registry (botox-css has no runtime-typed variable registry); FLOAT4
//    rows are 4 raw floats (e.g. g_accentColor), not botox's 0-255 c_color.
//  - cvar.set has no string overload (globals/common.h's ConVar wrapper only exposes
//    SetValueInt + ForceFloat/ForceInt, no SetValueString) -- setting a string cvar errors.
//  - fx.particle / fx.sparks: no client particle / spark dispatch helper exists anywhere in
//    botox-css (grep clean). Both are CS:S-capable in principle but nothing in this codebase
//    wires a dispatch call yet, so both keep the botox name/signature and return false,
//    documented as "blocked: no particle dispatch path in this repo" (not a CS:GO-only gap).
//  - buttons/flags/stages constants use the Source SDK 2013 in_buttons.h / public/const.h
//    (CS:S, i.e. the NOT HL2_DLL/EPISODIC/PORTAL branch) numeric values directly.
//  - fonts table only lists the handful of real global ImFont*s this repo exposes
//    (globals.h g_fontBold/g_fontReg/g_fontMi/g_fontIcon13); botox's e_font_names catalog
//    does not exist here.
#define _SILENCE_CXX20_CISO646_REMOVED_WARNING
#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../../globals/globals.h"
#include "../../dependencies/imgui/imgui.h"
#include "../menu/config.h"
#include "../movement/movement.h"   // Vec3, UserCmd_t (non-aim: the designated movement header)
#include "../../render/render.h"    // W2S_Project
#include "../aim/css_bridge.h"      // CssBridge_*/Css_* -- the sanctioned non-aim CSS game API
#include "../misc/killeffects.h"    // KillEffects_DispatchParticle/_Sparks -- fx.particle/fx.sparks

#include "lua_api.h"

void DebugLog(const char* fmt, ...);

using n_lua::element_t;
using n_lua::script_t;
using word_t = std::uintptr_t;

namespace
{
	constexpr const char* k_element_meta = "botox.element";
	constexpr const char* k_entity_meta  = "botox.entity";
	constexpr const char* k_cmd_meta     = "botox.cmd";
	constexpr const char* k_event_meta   = "botox.event";

	struct element_ref_t { int m_index; };
	struct entity_ref_t  { int m_index; };   // entity index, never a raw pointer (see css_bridge.h rule)

	/* ---- seh: plain functions, no c++ objects, so __try is legal under /EHsc ---- */

	bool seh_copy(void* destination, const void* source, const std::size_t size)
	{
		__try {
			std::memcpy(destination, source, size);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	int seh_strlen(const char* text, const int max)
	{
		__try {
			int length = 0;
			while (length < max && text[length])
				length++;
			return length;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return -1;
		}
	}

	bool seh_write(void* destination, const void* source, const std::size_t size)
	{
		if (seh_copy(destination, source, size))
			return true;

		DWORD old_protect = 0;
		if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old_protect))
			return false;

		const bool ok = seh_copy(destination, source, size);
		VirtualProtect(destination, size, old_protect, &old_protect);
		FlushInstructionCache(GetCurrentProcess(), destination, size);
		return ok;
	}

	template< std::size_t > using word_n = word_t;

	template< std::size_t... I > word_t call_cdecl(word_t fn, const word_t* a, std::index_sequence< I... >)
	{ return reinterpret_cast< word_t(__cdecl*)(word_n< I >...) >(fn)(a[I]...); }
	template< std::size_t... I > word_t call_stdcall(word_t fn, const word_t* a, std::index_sequence< I... >)
	{ return reinterpret_cast< word_t(__stdcall*)(word_n< I >...) >(fn)(a[I]...); }
	template< std::size_t... I > word_t call_thiscall(word_t fn, const word_t* a, std::index_sequence< I... >)
	{ return reinterpret_cast< word_t(__thiscall*)(word_t, word_n< I >...) >(fn)(a[0], a[I + 1]...); }
	template< std::size_t... I > word_t call_fastcall(word_t fn, const word_t* a, std::index_sequence< I... >)
	{ return reinterpret_cast< word_t(__fastcall*)(word_t, word_t, word_n< I >...) >(fn)(a[0], a[1], a[I + 2]...); }

	using invoker_t = word_t(*)(word_t, const word_t*);

	template< int convention, std::size_t count > word_t invoke(word_t fn, const word_t* a)
	{
		if constexpr (convention == 0) return call_cdecl(fn, a, std::make_index_sequence< count >{});
		else if constexpr (convention == 1) return call_stdcall(fn, a, std::make_index_sequence< count >{});
		else if constexpr (convention == 2) return call_thiscall(fn, a, std::make_index_sequence< count >{});
		else return call_fastcall(fn, a, std::make_index_sequence< count >{});
	}

	constexpr std::size_t k_max_call_args = 12;

	template< int convention, std::size_t... N >
	constexpr std::array< invoker_t, sizeof...(N) > invoker_row(std::index_sequence< N... >)
	{ return { &invoke< convention, N >... }; }

	const std::array< std::array< invoker_t, k_max_call_args + 1 >, 4 > k_invokers = {
		invoker_row< 0 >(std::make_index_sequence< k_max_call_args + 1 >{}),
		invoker_row< 1 >(std::make_index_sequence< k_max_call_args + 1 >{}),
		invoker_row< 2 >(std::make_index_sequence< k_max_call_args + 1 >{}),
		invoker_row< 3 >(std::make_index_sequence< k_max_call_args + 1 >{}),
	};

	bool seh_invoke(invoker_t invoker, word_t fn, const word_t* args, word_t* out)
	{
		__try {
			*out = invoker(fn, args);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	/* ---- helpers ---- */

	word_t check_address(lua_State* L, const int index) { return static_cast< word_t >(luaL_checkinteger(L, index)); }

	void push_address(lua_State* L, const void* pointer)
	{
		if (pointer) lua_pushinteger(L, static_cast< lua_Integer >(reinterpret_cast< word_t >(pointer)));
		else lua_pushnil(L);
	}

	float check_float(lua_State* L, const int index) { return static_cast< float >(luaL_checknumber(L, index)); }
	float opt_float(lua_State* L, const int index, const float fallback) { return static_cast< float >(luaL_optnumber(L, index, fallback)); }
	unsigned int opt_color(lua_State* L, const int index) { return static_cast< unsigned int >(luaL_optinteger(L, index, 0xFFFFFFFF)); }

	void push_vector(lua_State* L, const Vec3& v) { lua_pushnumber(L, v.x); lua_pushnumber(L, v.y); lua_pushnumber(L, v.z); }
	Vec3 check_vector(lua_State* L, const int index) { return Vec3(check_float(L, index), check_float(L, index + 1), check_float(L, index + 2)); }

	void set_field(lua_State* L, const char* key, const lua_Number value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); }
	void set_field(lua_State* L, const char* key, const lua_Integer value) { lua_pushinteger(L, value); lua_setfield(L, -2, key); }
	void set_field(lua_State* L, const char* key, const char* value) { lua_pushstring(L, value); lua_setfield(L, -2, key); }
	void set_bool(lua_State* L, const char* key, const bool value) { lua_pushboolean(L, value); lua_setfield(L, -2, key); }

	void add_table(lua_State* L, const char* name, const luaL_Reg* functions)
	{
		lua_newtable(L);
		luaL_setfuncs(L, functions, 0);
		lua_setglobal(L, name);
	}

	/* ---- callbacks (engine-free, 1:1 botox) ---- */

	int l_callbacks_register(lua_State* L)
	{
		const char* name = luaL_checkstring(L, 1);
		luaL_checktype(L, 2, LUA_TFUNCTION);

		lua_getfield(L, LUA_REGISTRYINDEX, "botox.callbacks");
		if (lua_getfield(L, -1, name) != LUA_TTABLE) {
			lua_pop(L, 1);
			lua_newtable(L);
			lua_pushvalue(L, -1);
			lua_setfield(L, -3, name);
		}

		const lua_Integer size = static_cast< lua_Integer >(lua_rawlen(L, -1));
		lua_pushvalue(L, 2);
		lua_rawseti(L, -2, size + 1);
		return 0;
	}

	int l_callbacks_unregister(lua_State* L)
	{
		const char* name = luaL_checkstring(L, 1);
		luaL_checktype(L, 2, LUA_TFUNCTION);

		lua_getfield(L, LUA_REGISTRYINDEX, "botox.callbacks");
		if (lua_getfield(L, -1, name) != LUA_TTABLE)
			return 0;

		const int list         = lua_gettop(L);
		const lua_Integer size = static_cast< lua_Integer >(lua_rawlen(L, list));

		for (lua_Integer i = 1; i <= size; i++) {
			lua_rawgeti(L, list, i);
			const bool same = lua_rawequal(L, -1, 2);
			lua_pop(L, 1);
			if (!same) continue;

			for (lua_Integer j = i; j < size; j++) {
				lua_rawgeti(L, list, j + 1);
				lua_rawseti(L, list, j);
			}
			lua_pushnil(L);
			lua_rawseti(L, list, size);
			break;
		}
		return 0;
	}

	/* ---- ui (engine-free, 1:1 botox) ---- */

	int create_element(lua_State* L, element_t&& element)
	{
		script_t* script = n_lua_api::script_of(L);
		int index        = 0;
		{
			std::lock_guard< std::mutex > lock(g_lua.m_mutex);
			g_lua.apply_saved(*script, element);
			script->m_elements.push_back(std::move(element));
			index = static_cast< int >(script->m_elements.size()) - 1;
		}

		auto* ref    = static_cast< element_ref_t* >(lua_newuserdatauv(L, sizeof(element_ref_t), 0));
		ref->m_index = index;
		luaL_setmetatable(L, k_element_meta);
		return 1;
	}

	element_t make_element(lua_State* L, const n_lua::e_element_type type)
	{
		element_t element{};
		element.m_type  = type;
		element.m_label = luaL_checkstring(L, 1);
		return element;
	}

	int l_ui_checkbox(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_checkbox);
		element.m_bool    = lua_toboolean(L, 2);
		return create_element(L, std::move(element));
	}

	int l_ui_slider_int(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_slider_int);
		element.m_int_min = static_cast< int >(luaL_checkinteger(L, 2));
		element.m_int_max = static_cast< int >(luaL_checkinteger(L, 3));
		element.m_int     = std::clamp(static_cast< int >(luaL_optinteger(L, 4, element.m_int_min)), element.m_int_min, element.m_int_max);
		element.m_format  = luaL_optstring(L, 5, "%d");
		return create_element(L, std::move(element));
	}

	int l_ui_slider_float(lua_State* L)
	{
		element_t element   = make_element(L, n_lua::element_slider_float);
		element.m_float_min = check_float(L, 2);
		element.m_float_max = check_float(L, 3);
		element.m_float     = std::clamp(opt_float(L, 4, element.m_float_min), element.m_float_min, element.m_float_max);
		element.m_format    = luaL_optstring(L, 5, "%.2f");
		return create_element(L, std::move(element));
	}

	int l_ui_combo(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_combo);
		luaL_checktype(L, 2, LUA_TTABLE);

		const lua_Integer size = static_cast< lua_Integer >(lua_rawlen(L, 2));
		for (lua_Integer i = 1; i <= size; i++) {
			lua_rawgeti(L, 2, i);
			element.m_items.emplace_back(luaL_checkstring(L, -1));
			lua_pop(L, 1);
		}
		if (element.m_items.empty())
			return luaL_error(L, "ui.combo: items table is empty");

		element.m_int = std::clamp(static_cast< int >(luaL_optinteger(L, 3, 1)) - 1, 0, static_cast< int >(element.m_items.size()) - 1);
		return create_element(L, std::move(element));
	}

	int l_ui_color(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_color);
		for (int i = 0; i < 4; i++)
			element.m_color[i] = std::clamp(static_cast< float >(luaL_optinteger(L, 2 + i, 255)) / 255.f, 0.f, 1.f);
		return create_element(L, std::move(element));
	}

	int l_ui_keybind(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_keybind);
		element.m_bind    = n_lua::lua_key_bind_t{ std::clamp(static_cast< int >(luaL_optinteger(L, 2, 0)), 0, 255),
			                                       std::clamp(static_cast< int >(luaL_optinteger(L, 3, 1)), 0, 2) };
		return create_element(L, std::move(element));
	}

	int l_ui_button(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_button);
		if (!lua_isnoneornil(L, 2)) {
			luaL_checktype(L, 2, LUA_TFUNCTION);
			lua_pushvalue(L, 2);
			element.m_callback = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		return create_element(L, std::move(element));
	}

	int l_ui_text(lua_State* L) { return create_element(L, make_element(L, n_lua::element_text)); }

	int l_ui_input(lua_State* L)
	{
		element_t element = make_element(L, n_lua::element_input);
		strncpy_s(element.m_text, luaL_optstring(L, 2, ""), _TRUNCATE);
		return create_element(L, std::move(element));
	}

	int element_index(lua_State* L) { return static_cast< const element_ref_t* >(luaL_checkudata(L, 1, k_element_meta))->m_index; }

	/* never call lua under g_lua.m_mutex: any push / error can run a __gc finalizer that re-enters ui ( std::mutex is not recursive ) */
	bool copy_element(lua_State* L, const int index, element_t& out, bool* active = nullptr)
	{
		std::lock_guard< std::mutex > lock(g_lua.m_mutex);
		auto& elements = n_lua_api::script_of(L)->m_elements;
		if (index < 0 || index >= static_cast< int >(elements.size()))
			return false;

		element_t& element = elements[index];
		if (active && element.m_type == n_lua::element_keybind)
			*active = (element.m_bind.m_key_style == 0 || element.m_bind.m_key) && (element.m_bind.m_key > 0 && (GetAsyncKeyState(element.m_bind.m_key) & 0x8000) != 0);

		out = element;
		return true;
	}

	int l_element_get(lua_State* L)
	{
		element_t element{};
		bool active = false;
		if (!copy_element(L, element_index(L), element, &active))
			return luaL_error(L, "ui element is gone");

		switch (element.m_type) {
		case n_lua::element_checkbox: lua_pushboolean(L, element.m_bool); return 1;
		case n_lua::element_slider_int: lua_pushinteger(L, element.m_int); return 1;
		case n_lua::element_slider_float: lua_pushnumber(L, element.m_float); return 1;
		case n_lua::element_combo:
			lua_pushinteger(L, element.m_int + 1);
			lua_pushstring(L, element.m_items[element.m_int].c_str());
			return 2;
		case n_lua::element_color:
			for (const float channel : element.m_color)
				lua_pushinteger(L, static_cast< lua_Integer >(channel * 255.f + 0.5f));
			return 4;
		case n_lua::element_keybind:
			lua_pushboolean(L, active);
			lua_pushinteger(L, element.m_bind.m_key);
			lua_pushinteger(L, element.m_bind.m_key_style);
			return 3;
		case n_lua::element_input: lua_pushstring(L, element.m_text); return 1;
		case n_lua::element_text: lua_pushstring(L, element.m_label.c_str()); return 1;
		default: lua_pushnil(L); return 1;
		}
	}

	int l_element_set(lua_State* L)
	{
		const int index = element_index(L);
		element_t element{};
		if (!copy_element(L, index, element))
			return luaL_error(L, "ui element is gone");

		switch (element.m_type) {
		case n_lua::element_checkbox: element.m_bool = lua_toboolean(L, 2); break;
		case n_lua::element_slider_int: element.m_int = std::clamp(static_cast< int >(luaL_checkinteger(L, 2)), element.m_int_min, element.m_int_max); break;
		case n_lua::element_slider_float: element.m_float = std::clamp(check_float(L, 2), element.m_float_min, element.m_float_max); break;
		case n_lua::element_combo:
			element.m_int = std::clamp(static_cast< int >(luaL_checkinteger(L, 2)) - 1, 0, static_cast< int >(element.m_items.size()) - 1);
			break;
		case n_lua::element_color:
			for (int i = 0; i < 4; i++)
				element.m_color[i] = std::clamp(static_cast< float >(luaL_optinteger(L, 2 + i, 255)) / 255.f, 0.f, 1.f);
			break;
		case n_lua::element_keybind:
			element.m_bind.m_key       = std::clamp(static_cast< int >(luaL_checkinteger(L, 2)), 0, 255);
			element.m_bind.m_key_style = std::clamp(static_cast< int >(luaL_optinteger(L, 3, element.m_bind.m_key_style)), 0, 2);
			break;
		case n_lua::element_input: strncpy_s(element.m_text, luaL_checkstring(L, 2), _TRUNCATE); break;
		case n_lua::element_text: element.m_label = luaL_checkstring(L, 2); break;
		default: break;
		}

		script_t* script = n_lua_api::script_of(L);
		std::lock_guard< std::mutex > lock(g_lua.m_mutex);
		if (index >= static_cast< int >(script->m_elements.size()))
			return 0;

		element_t& live = script->m_elements[index];
		live.m_bool     = element.m_bool;
		live.m_int      = element.m_int;
		live.m_float    = element.m_float;
		live.m_bind     = element.m_bind;
		std::memcpy(live.m_color, element.m_color, sizeof(live.m_color));
		std::memcpy(live.m_text, element.m_text, sizeof(live.m_text));
		if (live.m_type == n_lua::element_text)
			live.m_label = std::move(element.m_label);

		script->m_dirty = true;
		return 0;
	}

	int l_element_visible(lua_State* L)
	{
		const int index   = element_index(L);
		const bool change = lua_gettop(L) >= 2;
		const bool value  = change && lua_toboolean(L, 2);

		bool found = false, shown = false;
		{
			std::lock_guard< std::mutex > lock(g_lua.m_mutex);
			auto& elements = n_lua_api::script_of(L)->m_elements;
			if (index >= 0 && index < static_cast< int >(elements.size())) {
				if (change) elements[index].m_visible = value;
				shown = elements[index].m_visible;
				found = true;
			}
		}
		if (!found) return luaL_error(L, "ui element is gone");
		lua_pushboolean(L, shown);
		return 1;
	}

	int l_element_label(lua_State* L)
	{
		element_t element{};
		if (!copy_element(L, element_index(L), element))
			return luaL_error(L, "ui element is gone");
		lua_pushstring(L, element.m_label.c_str());
		return 1;
	}

	/* ---- render: paint callback only ---- */

	void require_paint(lua_State* L)
	{
		if (!n_lua_api::s_in_paint)
			luaL_error(L, "render functions only work inside the paint callback");
	}

	// This repo has no botox-style indexed font catalog (e_font_names) -- only a handful of
	// named global ImFont*s (globals.h). id: 0 default(g_fontReg) 1 bold(g_fontBold)
	// 2 indicator(g_fontMi) 3 icons(g_fontIcon13). See lua.fonts table below.
	ImFont* font_of(lua_State* L, const int index)
	{
		const lua_Integer id = luaL_optinteger(L, index, 0);
		switch (id) {
		case 1: return g_fontBold ? g_fontBold : g_fontReg;
		case 2: return g_fontMi ? g_fontMi : g_fontReg;
		case 3: return g_fontIcon13 ? g_fontIcon13 : g_fontReg;
		default: return g_fontReg;
		}
	}

	int l_color(lua_State* L)
	{
		const auto channel = [L](const int index, const lua_Integer fallback) {
			return static_cast< unsigned int >(std::clamp< lua_Integer >(luaL_optinteger(L, index, fallback), 0, 255));
		};
		lua_pushinteger(L, static_cast< lua_Integer >(IM_COL32(channel(1, 255), channel(2, 255), channel(3, 255), channel(4, 255))));
		return 1;
	}

	int l_render_screen_size(lua_State* L)
	{
		const ImVec2 size = ImGui::GetIO().DisplaySize;
		lua_pushnumber(L, size.x);
		lua_pushnumber(L, size.y);
		return 2;
	}

	int l_render_text(lua_State* L)
	{
		require_paint(L);
		ImFont* font = font_of(L, 5);
		if (!font) return 0;

		const unsigned int color = opt_color(L, 4);
		const float size         = opt_float(L, 7, 0.f);
		ImGui::GetBackgroundDrawList()->AddText(font, size > 0.f ? size : font->FontSize, ImVec2(check_float(L, 1), check_float(L, 2)), color,
		                                         luaL_checkstring(L, 3));
		return 0;
	}

	int l_render_text_size(lua_State* L)
	{
		require_paint(L);
		ImFont* font     = font_of(L, 2);
		const char* text = luaL_checkstring(L, 1);
		const float size = opt_float(L, 3, 0.f);
		const ImVec2 dims = font ? font->CalcTextSizeA(size > 0.f ? size : font->FontSize, FLT_MAX, 0.f, text) : ImVec2(0.f, 0.f);
		lua_pushnumber(L, dims.x);
		lua_pushnumber(L, dims.y);
		return 2;
	}

	int l_render_line(lua_State* L)
	{
		require_paint(L);
		ImGui::GetBackgroundDrawList()->AddLine(ImVec2(check_float(L, 1), check_float(L, 2)), ImVec2(check_float(L, 3), check_float(L, 4)),
		                                         opt_color(L, 5), opt_float(L, 6, 1.f));
		return 0;
	}

	int push_rect(lua_State* L, const bool filled)
	{
		require_paint(L);
		ImVec2 mins(check_float(L, 1), check_float(L, 2)), maxs(check_float(L, 3), check_float(L, 4));
		const unsigned int color = opt_color(L, 5);
		ImDrawList* dl           = ImGui::GetBackgroundDrawList();
		if (filled)
			dl->AddRectFilled(mins, maxs, color, opt_float(L, 6, 0.f));
		else
			dl->AddRect(mins, maxs, color, opt_float(L, 7, 0.f), 0, opt_float(L, 6, 1.f));
		return 0;
	}

	int l_render_rect(lua_State* L) { return push_rect(L, false); }
	int l_render_rect_filled(lua_State* L) { return push_rect(L, true); }

	int l_render_gradient(lua_State* L)
	{
		require_paint(L);
		ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(
			ImVec2(check_float(L, 1), check_float(L, 2)), ImVec2(check_float(L, 3), check_float(L, 4)),
			opt_color(L, 5), opt_color(L, 5), opt_color(L, 6), opt_color(L, 6));   // top->bottom gradient (col_upr_left, col_upr_right, col_bot_right, col_bot_left)
		return 0;
	}

	int l_render_circle(lua_State* L)
	{
		require_paint(L);
		ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(check_float(L, 1), check_float(L, 2)), check_float(L, 3), opt_color(L, 4),
		                                           static_cast< int >(luaL_optinteger(L, 5, 32)), opt_float(L, 6, 1.f));
		return 0;
	}

	int l_render_circle_filled(lua_State* L)
	{
		require_paint(L);
		ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(check_float(L, 1), check_float(L, 2)), check_float(L, 3), opt_color(L, 4),
		                                                 static_cast< int >(luaL_optinteger(L, 5, 32)));
		return 0;
	}

	int l_render_triangle(lua_State* L)
	{
		require_paint(L);
		ImVec2 a(check_float(L, 1), check_float(L, 2)), b(check_float(L, 3), check_float(L, 4)), c(check_float(L, 5), check_float(L, 6));
		const unsigned int color = opt_color(L, 7);
		const bool filled        = lua_isnoneornil(L, 8) ? true : lua_toboolean(L, 8);
		ImDrawList* dl           = ImGui::GetBackgroundDrawList();
		if (filled) dl->AddTriangleFilled(a, b, c, color);
		else dl->AddTriangle(a, b, c, color, opt_float(L, 9, 1.f));
		return 0;
	}

	int l_render_poly(lua_State* L)
	{
		require_paint(L);
		luaL_checktype(L, 1, LUA_TTABLE);

		ImVec2 points[32];
		const int count = std::min(static_cast< int >(lua_rawlen(L, 1)) / 2, 32);
		if (count < 3)
			return luaL_error(L, "render.poly needs at least 3 points ( x1, y1, x2, y2, x3, y3 )");

		for (int i = 0; i < count; i++) {
			lua_rawgeti(L, 1, i * 2 + 1);
			lua_rawgeti(L, 1, i * 2 + 2);
			points[i] = ImVec2(static_cast< float >(luaL_checknumber(L, -2)), static_cast< float >(luaL_checknumber(L, -1)));
			lua_pop(L, 2);
		}

		const bool has_outline = !lua_isnoneornil(L, 3);
		const unsigned int fill    = opt_color(L, 2);
		const unsigned int outline = has_outline ? opt_color(L, 3) : 0u;
		const float thickness      = opt_float(L, 4, 1.f);

		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		if (fill) dl->AddConvexPolyFilled(points, count, fill);
		if (has_outline) dl->AddPolyline(points, count, outline, ImDrawFlags_Closed, thickness);
		return 0;
	}

	int l_render_world_to_screen(lua_State* L)
	{
		require_paint(L);
		ImVec2 screen{};
		if (!W2S_Project(check_vector(L, 1), screen)) {
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, screen.x);
		lua_pushnumber(L, screen.y);
		return 2;
	}

	/* ---- netvars: ClientClass->RecvTable walk by name, same algorithm as botox's recv_offset
	   (this repo's FindPropOffset/GetAllClasses, globals.h, instead of botox's own copy) ---- */

	unsigned int class_netvar(const char* class_name, const char* prop)
	{
		static std::unordered_map< std::string, int > s_cache;
		std::string key = std::string(class_name) + "->" + prop;
		if (const auto it = s_cache.find(key); it != s_cache.end())
			return static_cast< unsigned int >(it->second);

		int offset = 0;
		for (ClientClass* entry = GetAllClasses(); entry; entry = entry->m_pNext)
			if (entry->m_pNetworkName && std::strcmp(entry->m_pNetworkName, class_name) == 0) {
				const int found = FindPropOffset(entry->m_pRecvTable, prop, 0, 0);
				offset          = found >= 0 ? found : 0;
				break;
			}

		s_cache.emplace(std::move(key), offset);
		return static_cast< unsigned int >(offset);
	}

	/* "Class->m_prop" */
	unsigned int netvar_offset(const char* name)
	{
		const char* split = std::strstr(name, "->");
		if (!split) return 0;
		return class_netvar(std::string(name, split).c_str(), split + 2);
	}

	/* ---- datafield (predicted fields): local mirror of C_BaseEntity::GetPredDescMap's
	   datamap_t/typedescription_t, same layout + vtable slot 15 hacks/movement/airstuck_pure.h
	   uses (MEMORY join-crash-1103: confirmed 15, not 13) -- copied here so hacks/lua does not
	   have to pull in movement's pure-math header for one struct. ---- */

	struct typedescription_t_mini {
		int fieldType;
		const char* fieldName;
		int fieldOffset[2];
		short fieldSize;
		short flags;
		const char* externalName;
		void* pSaveRestoreOps;
		void* inputFunc;
		struct datamap_t_mini* td;
		int fieldSizeInBytes;
		void* override_field;
		int override_count;
		float fieldTolerance;
	};
	struct datamap_t_mini {
		typedescription_t_mini* dataDesc;
		int dataNumFields;
		const char* dataClassName;
		datamap_t_mini* baseMap;
		bool chains_validated;
		bool packed_offsets_computed;
		int packed_size;
	};
	constexpr int k_predDescMapSlot = 15;
	constexpr int k_fieldEmbedded   = 10;

	datamap_t_mini* EntPredDescMap(void* entity)
	{
		return VCall< datamap_t_mini* >(entity, k_predDescMapSlot);
	}

	int datamap_offset(datamap_t_mini* map, const char* name, int depth = 0)
	{
		for (; map && depth < 32; map = map->baseMap, depth++) {
			for (int i = 0; i < map->dataNumFields; i++) {
				const typedescription_t_mini& td = map->dataDesc[i];
				if (td.fieldName && std::strcmp(td.fieldName, name) == 0)
					return td.fieldOffset[0];
				if (td.fieldType == k_fieldEmbedded && td.td) {
					const int inner = datamap_offset(reinterpret_cast< datamap_t_mini* >(td.td), name, depth + 1);
					if (inner >= 0) return td.fieldOffset[0] + inner;
				}
			}
		}
		return -1;
	}

	/* ---- entities: userdata keeps the index, every call re-resolves it (css_bridge.h rule:
	   no raw c_base_entity* leaves this file -- GetClientEntity(index) is looked up fresh
	   every time, same as botox re-resolving through g_interfaces.m_client_entity_list) ---- */

	void push_entity(lua_State* L, int index)
	{
		if (index <= 0) { lua_pushnil(L); return; }
		auto* ref    = static_cast< entity_ref_t* >(lua_newuserdatauv(L, sizeof(entity_ref_t), 0));
		ref->m_index = index;
		luaL_setmetatable(L, k_entity_meta);
	}

	int entity_index(lua_State* L, const int index) { return static_cast< const entity_ref_t* >(luaL_checkudata(L, index, k_entity_meta))->m_index; }

	void* check_entity_ptr(lua_State* L, const int index = 1)
	{
		const int entnum = entity_index(L, index);
		void* ptr        = GetClientEntity(entnum);
		if (!ptr) luaL_error(L, "entity %d is gone", entnum);
		return ptr;
	}

	int l_entities_local(lua_State* L) { push_entity(L, GetLocalPlayerIndex()); return 1; }
	int l_entities_get(lua_State* L) { push_entity(L, static_cast< int >(luaL_checkinteger(L, 1))); return 1; }
	int l_entities_from_handle(lua_State* L) { push_entity(L, static_cast< int >(luaL_checkinteger(L, 1)) & 0xFFF); return 1; }

	// No verified IClientEntityList::GetHighestEntityIndex slot in this session's sources --
	// scan down from MAX_EDICTS (public/const.h) for the first resolvable entity instead of
	// guessing a slot. Same answer, one linear scan instead of an O(1) accessor.
	constexpr int k_maxEdicts = 2048;
	int highest_entity_index()
	{
		for (int i = k_maxEdicts - 1; i > 0; i--)
			if (GetClientEntity(i)) return i;
		return 0;
	}
	int l_entities_highest_index(lua_State* L) { lua_pushinteger(L, highest_entity_index()); return 1; }

	// No GetMaxClients accessor anywhere in non-aim botox-css code; player loops elsewhere in
	// this repo (hacks/hud/spectator_list.cpp:128) hardcode the CS:S multiplayer cap of 64 --
	// same bound reused here rather than guessing an IVEngineClient vtable slot.
	constexpr int k_maxPlayerSlots = 64;

	int l_entities_players(lua_State* L)
	{
		const bool include_dead    = lua_toboolean(L, 1);
		const bool include_dormant = lua_toboolean(L, 2);

		lua_newtable(L);
		lua_Integer count = 0;
		if (!IsInGame()) return 1;

		for (int i = 1; i <= k_maxPlayerSlots; i++) {
			if (!CssBridge_EntityExists(i)) continue;
			if (!CssBridge_IsDormant(i)) {
				// an entity index can outlive a player disconnecting; this repo has no direct
				// IsPlayer() accessor outside aim, so the class-name check below stands in.
			}
			char name[4];
			// cheapest "is this a player slot" test available non-aim: team > 0 on CSS'
			// player class range is unreliable for non-players too, so fall back to the
			// documented classname check (css_bridge.h Css_ClassnameIs, MEMORY ref-css-clientclass).
			if (!Css_ClassnameIs(i, "CCSPlayer")) continue;
			if (!include_dead && !CssBridge_IsAlive(i)) continue;
			if (!include_dormant && CssBridge_IsDormant(i)) continue;

			push_entity(L, i);
			lua_rawseti(L, -2, ++count);
		}
		return 1;
	}

	int l_entities_by_class(lua_State* L)
	{
		const char* wanted = luaL_checkstring(L, 1);
		lua_newtable(L);
		lua_Integer count = 0;

		const int highest = highest_entity_index();
		for (int i = 0; i <= highest; i++) {
			if (!CssBridge_EntityExists(i)) continue;
			if (!Css_ClassnameIs(i, wanted)) continue;
			push_entity(L, i);
			lua_rawseti(L, -2, ++count);
		}
		return 1;
	}

	int l_entity_index(lua_State* L) { lua_pushinteger(L, entity_index(L, 1)); return 1; }
	int l_entity_ptr(lua_State* L) { push_address(L, check_entity_ptr(L)); return 1; }
	int l_entity_is_valid(lua_State* L) { lua_pushboolean(L, CssBridge_EntityExists(entity_index(L, 1))); return 1; }
	int l_entity_is_player(lua_State* L) { lua_pushboolean(L, Css_ClassnameIs(entity_index(L, 1), "CCSPlayer")); return 1; }
	int l_entity_is_alive(lua_State* L) { lua_pushboolean(L, CssBridge_IsAlive(entity_index(L, 1))); return 1; }
	int l_entity_is_dormant(lua_State* L) { lua_pushboolean(L, CssBridge_IsDormant(entity_index(L, 1))); return 1; }

	int l_entity_is_enemy(lua_State* L)
	{
		const int ent   = entity_index(L, 1);
		const int local = GetLocalPlayerIndex();
		lua_pushboolean(L, local > 0 && local != ent && CssBridge_Team(local) != CssBridge_Team(ent) &&
		                        CssBridge_Team(ent) > 0);
		return 1;
	}

	int l_entity_team(lua_State* L) { lua_pushinteger(L, CssBridge_Team(entity_index(L, 1))); return 1; }
	int l_entity_health(lua_State* L) { lua_pushinteger(L, CssBridge_Health(entity_index(L, 1))); return 1; }
	int l_entity_armor(lua_State* L) { lua_pushinteger(L, CssBridge_GetArmor(entity_index(L, 1))); return 1; }
	int l_entity_flags(lua_State* L) { lua_pushinteger(L, Css_Flags(entity_index(L, 1))); return 1; }

	// MOVETYPE_WALK, public/const.h MoveType_t -- the one botox-css's own EntMoveType
	// (hacks/movement/movement.cpp:218) falls back to when the offset isn't resolved yet.
	constexpr int k_moveTypeWalk = 2;

	// Round 2: movement.cpp's own EntMoveType is `m_MoveType = m_nWaterLevel - 4` (its comment,
	// netvars.cpp g_offWaterLevel) -- same extern offset, read directly (globals.h), no bridge needed.
	int l_entity_move_type(lua_State* L)
	{
		void* ent = check_entity_ptr(L);
		ResolveNetvars();
		if (g_offWaterLevel <= 0) { lua_pushinteger(L, k_moveTypeWalk); return 1; }
		unsigned char value = 0;
		if (!seh_copy(&value, reinterpret_cast< char* >(ent) + (g_offWaterLevel - 4), 1)) { lua_pushinteger(L, k_moveTypeWalk); return 1; }
		lua_pushinteger(L, value);
		return 1;
	}

	int l_entity_origin(lua_State* L)
	{
		float v[3]{};
		if (!CssBridge_GetOrigin(entity_index(L, 1), v)) return 0;
		push_vector(L, Vec3(v[0], v[1], v[2]));
		return 3;
	}

	int l_entity_velocity(lua_State* L)
	{
		float v[3]{};
		if (!CssBridge_GetVelocity(entity_index(L, 1), v)) return 0;
		push_vector(L, Vec3(v[0], v[1], v[2]));
		return 3;
	}

	int l_entity_eye_position(lua_State* L)
	{
		const int ent = entity_index(L, 1);
		float origin[3]{}, viewoff[3]{};
		if (!CssBridge_GetOrigin(ent, origin)) return 0;
		if (Css_ClassnameIs(ent, "CCSPlayer") && Css_ViewOffset(ent, viewoff))
			push_vector(L, Vec3(origin[0] + viewoff[0], origin[1] + viewoff[1], origin[2] + viewoff[2]));
		else
			push_vector(L, Vec3(origin[0], origin[1], origin[2]));
		return 3;
	}

	// Round 2: g_offMins/g_offMaxs (netvars.cpp:139,177, extern globals.h:888-889) -- same
	// m_vecMins/m_vecMaxs netvar botox's own ICollideable path reads, just by direct offset.
	int l_entity_mins_maxs(lua_State* L)
	{
		void* ent = check_entity_ptr(L);
		ResolveNetvars();
		if (g_offMins <= 0 || g_offMaxs <= 0) return 0;

		Vec3 mins{}, maxs{};
		if (!seh_copy(&mins, reinterpret_cast< char* >(ent) + g_offMins, sizeof(mins)) ||
		    !seh_copy(&maxs, reinterpret_cast< char* >(ent) + g_offMaxs, sizeof(maxs)))
			return 0;

		push_vector(L, mins);
		push_vector(L, maxs);
		return 6;
	}

	// Round 2: CssBridge_GetClassName/_GetClassId bridge e->GetClientClass() (css_bridge.cpp,
	// aim TU) -- the same call Css_ClassnameIs already makes, generalised to any class.
	int l_entity_class_name(lua_State* L)
	{
		const char* name = CssBridge_GetClassName(entity_index(L, 1));
		lua_pushstring(L, name ? name : "");
		return 1;
	}

	int l_entity_class_id(lua_State* L) { lua_pushinteger(L, CssBridge_GetClassId(entity_index(L, 1))); return 1; }

	int l_entity_name(lua_State* L)
	{
		char buf[128]{};
		if (!CssBridge_GetPlayerName(entity_index(L, 1), buf, sizeof(buf))) { lua_pushstring(L, ""); return 1; }
		lua_pushstring(L, buf);
		return 1;
	}

	int l_entity_weapon(lua_State* L) { push_entity(L, CssBridge_ActiveWeapon(entity_index(L, 1))); return 1; }
	int l_entity_weapon_id(lua_State* L) { lua_pushinteger(L, CssBridge_GetWeaponID(entity_index(L, 1))); return 1; }

	// Round 2: CssBridge_GetBonePosition/_GetHitboxPosition bridge the exact SetupBones +
	// pHitboxSet/pHitbox call esp.cpp/aim_hitscan.cpp already run per frame/shot.
	int l_entity_bone_position(lua_State* L)
	{
		float pos[3]{};
		if (!CssBridge_GetBonePosition(entity_index(L, 1), static_cast< int >(luaL_checkinteger(L, 2)), pos)) return 0;
		push_vector(L, Vec3(pos[0], pos[1], pos[2]));
		return 3;
	}
	int l_entity_hitbox_position(lua_State* L)
	{
		float pos[3]{};
		if (!CssBridge_GetHitboxPosition(entity_index(L, 1), static_cast< int >(luaL_checkinteger(L, 2)), pos)) return 0;
		push_vector(L, Vec3(pos[0], pos[1], pos[2]));
		return 3;
	}

	/* type names shared by get_prop / set_prop / get_datafield */
	enum e_value_type { value_int, value_float, value_bool, value_short, value_byte, value_vector, value_string, value_handle };

	e_value_type check_value_type(lua_State* L, const int index)
	{
		static const char* const names[] = { "int", "float", "bool", "short", "byte", "vector", "string", "handle", nullptr };
		return static_cast< e_value_type >(luaL_checkoption(L, index, "int", names));
	}

	int push_value(lua_State* L, const word_t address, const e_value_type type)
	{
		switch (type) {
		case value_float: {
			float value = 0.f;
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			lua_pushnumber(L, value);
			return 1;
		}
		case value_bool: {
			unsigned char value = 0;
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			lua_pushboolean(L, value != 0);
			return 1;
		}
		case value_short: {
			short value = 0;
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			lua_pushinteger(L, value);
			return 1;
		}
		case value_byte: {
			unsigned char value = 0;
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			lua_pushinteger(L, value);
			return 1;
		}
		case value_vector: {
			Vec3 value{};
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			push_vector(L, value);
			return 3;
		}
		case value_string: {
			const int length = seh_strlen(reinterpret_cast< const char* >(address), 4096);
			if (length < 0) return luaL_error(L, "bad read");
			lua_pushlstring(L, reinterpret_cast< const char* >(address), static_cast< std::size_t >(length));
			return 1;
		}
		case value_handle: {
			unsigned int value = 0;
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			push_entity(L, static_cast< int >(value & 0xFFF));
			return 1;
		}
		default: {
			int value = 0;
			if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(value))) return luaL_error(L, "bad read");
			lua_pushinteger(L, value);
			return 1;
		}
		}
	}

	void write_value(lua_State* L, const word_t address, const e_value_type type, const int value_index)
	{
		void* const target = reinterpret_cast< void* >(address);
		bool ok             = false;
		switch (type) {
		case value_float: { const float value = check_float(L, value_index); ok = seh_write(target, &value, sizeof(value)); break; }
		case value_bool: { const unsigned char value = lua_toboolean(L, value_index) ? 1 : 0; ok = seh_write(target, &value, sizeof(value)); break; }
		case value_short: { const short value = static_cast< short >(luaL_checkinteger(L, value_index)); ok = seh_write(target, &value, sizeof(value)); break; }
		case value_byte: { const unsigned char value = static_cast< unsigned char >(luaL_checkinteger(L, value_index)); ok = seh_write(target, &value, sizeof(value)); break; }
		case value_vector: { const Vec3 value(check_float(L, value_index), check_float(L, value_index + 1), check_float(L, value_index + 2)); ok = seh_write(target, &value, sizeof(value)); break; }
		case value_string: luaL_error(L, "string props are read only"); return;
		default: { const int value = static_cast< int >(luaL_checkinteger(L, value_index)); ok = seh_write(target, &value, sizeof(value)); break; }
		}
		if (!ok) luaL_error(L, "bad write");
	}

	unsigned int check_netvar(lua_State* L, const int index)
	{
		const char* name          = luaL_checkstring(L, index);
		const unsigned int offset = netvar_offset(name);
		if (!offset) luaL_error(L, "no netvar '%s' ( format: \"CCSPlayer->m_iHealth\" )", name);
		return offset;
	}

	/* entity:get_prop( "CCSPlayer->m_iHealth", "int", extra_offset ) */
	int l_entity_get_prop(lua_State* L)
	{
		void* ent             = check_entity_ptr(L);
		const word_t address  = reinterpret_cast< word_t >(ent) + check_netvar(L, 2) + static_cast< word_t >(luaL_optinteger(L, 4, 0));
		return push_value(L, address, check_value_type(L, 3));
	}

	int l_entity_set_prop(lua_State* L)
	{
		void* ent            = check_entity_ptr(L);
		const word_t address = reinterpret_cast< word_t >(ent) + check_netvar(L, 2);
		write_value(L, address, check_value_type(L, 3), 4);
		return 0;
	}

	unsigned int check_datafield(lua_State* L, void* ent, const int index)
	{
		const char* name = luaL_checkstring(L, index);
		const int offset  = datamap_offset(EntPredDescMap(ent), name);
		if (offset < 0) luaL_error(L, "no datamap field '%s'", name);
		return static_cast< unsigned int >(offset);
	}

	int l_entity_get_datafield(lua_State* L)
	{
		void* ent = check_entity_ptr(L);
		return push_value(L, reinterpret_cast< word_t >(ent) + check_datafield(L, ent, 2), check_value_type(L, 3));
	}

	int l_entity_set_datafield(lua_State* L)
	{
		void* ent = check_entity_ptr(L);
		write_value(L, reinterpret_cast< word_t >(ent) + check_datafield(L, ent, 2), check_value_type(L, 3), 4);
		return 0;
	}

	int l_entity_eq(lua_State* L) { lua_pushboolean(L, entity_index(L, 1) == entity_index(L, 2)); return 1; }
	int l_entity_tostring(lua_State* L) { lua_pushfstring(L, "entity(%d)", entity_index(L, 1)); return 1; }

	/* ---- engine / globals ---- */

	int l_engine_exec(lua_State* L) { CssBridge_ClientCmdUnrestricted(luaL_checkstring(L, 1)); return 0; }
	int l_engine_in_game(lua_State* L) { lua_pushboolean(L, IsInGame()); return 1; }
	int l_engine_connected(lua_State* L) { lua_pushboolean(L, IsInGame()); return 1; }   // no separate "connected but not in game" accessor non-aim
	int l_engine_local_index(lua_State* L) { lua_pushinteger(L, GetLocalPlayerIndex()); return 1; }
	int l_engine_max_clients(lua_State* L) { lua_pushinteger(L, k_maxPlayerSlots); return 1; }   // see k_maxPlayerSlots note above

	int l_engine_map_name(lua_State* L)
	{
		std::string raw = CssBridge_LevelNameRaw();   // "maps/de_dust2.bsp"
		std::size_t slash = raw.find_last_of('/');
		std::size_t dot    = raw.find_last_of('.');
		if (slash == std::string::npos) slash = 0; else slash++;
		lua_pushstring(L, raw.substr(slash, dot == std::string::npos ? std::string::npos : dot - slash).c_str());
		return 1;
	}
	int l_engine_level_name(lua_State* L) { lua_pushstring(L, CssBridge_LevelNameRaw()); return 1; }

	int l_engine_view_angles(lua_State* L)
	{
		float pitch_yaw_roll[3]{};
		if (!CssBridge_GetLocalViewAngles(pitch_yaw_roll)) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 3; }
		lua_pushnumber(L, pitch_yaw_roll[0]);
		lua_pushnumber(L, pitch_yaw_roll[1]);
		lua_pushnumber(L, pitch_yaw_roll[2]);
		return 3;
	}

	// Round 2: CssBridge_SetViewAngles bridges I::EngineClient->SetViewAngles, live in aim_rcs.cpp:148.
	int l_engine_set_view_angles(lua_State* L)
	{
		CssBridge_SetViewAngles(check_float(L, 1), check_float(L, 2), opt_float(L, 3, 0.f));
		return 0;
	}

	int l_engine_player_info(lua_State* L)
	{
		const int ent = static_cast< int >(luaL_checkinteger(L, 1));
		if (!CssBridge_EntityExists(ent)) { lua_pushnil(L); return 1; }

		char name[128]{};
		CssBridge_GetPlayerName(ent, name, sizeof(name));
		lua_newtable(L);
		set_field(L, "name", name);
		set_field(L, "steam_id", static_cast< lua_Integer >(CssBridge_GetSteamID64(ent)));
		set_bool(L, "bot", (Css_Flags(ent) & (1 << 9)) != 0);   // FL_FAKECLIENT, CS:S branch (public/const.h)
		return 1;
	}

	int l_engine_player_for_user_id(lua_State* L) { lua_pushinteger(L, CssBridge_PlayerForUserId(static_cast< int >(luaL_checkinteger(L, 1)))); return 1; }
	int l_engine_playing_demo(lua_State* L) { lua_pushboolean(L, CssBridge_IsPlayingDemo()); return 1; }   // round 2: bridged, same IVEngineClient interface as SetViewAngles

	int l_engine_latency(lua_State* L)
	{
		int ping_ms = 0; float since = 0.f; bool lost = false, choked = false;
		CssBridge_GetNetPing(ping_ms, since, lost, choked);
		lua_pushnumber(L, ping_ms / 1000.f);
		lua_pushnumber(L, ping_ms / 1000.f);
		return 2;
	}

	int l_globals_realtime(lua_State* L) { lua_pushnumber(L, static_cast< double >(GetTickCount64()) / 1000.0); return 1; }
	int l_globals_curtime(lua_State* L) { lua_pushnumber(L, CssBridge_CurTime()); return 1; }
	int l_globals_frametime(lua_State* L) { lua_pushnumber(L, ImGui::GetIO().DeltaTime); return 1; }
	int l_globals_absframetime(lua_State* L) { lua_pushnumber(L, ImGui::GetIO().DeltaTime); return 1; }
	int l_globals_tickcount(lua_State* L) { lua_pushinteger(L, static_cast< lua_Integer >(CssBridge_CurTime() / 0.015f)); return 1; }   // no exposed CGlobalVarsBase::tickcount non-aim; derived from curtime / nominal CS:S tick (64 tick = 0.015)
	int l_globals_tickinterval(lua_State* L) { lua_pushnumber(L, 0.015f); return 1; }
	int l_globals_framecount(lua_State* L) { lua_pushinteger(L, static_cast< lua_Integer >(ImGui::GetFrameCount())); return 1; }
	int l_globals_maxclients(lua_State* L) { lua_pushinteger(L, k_maxPlayerSlots); return 1; }

	/* ---- cvar (globals/common.h ICvar/ConVar) ---- */

	ConVar* find_cvar(lua_State* L) { return g_cvar ? g_cvar->FindVarWalk(luaL_checkstring(L, 1)) : nullptr; }

	int l_cvar_exists(lua_State* L) { lua_pushboolean(L, find_cvar(L) != nullptr); return 1; }
	int l_cvar_get_int(lua_State* L) { ConVar* v = find_cvar(L); if (!v) lua_pushnil(L); else lua_pushinteger(L, static_cast< lua_Integer >(v->GetFloat())); return 1; }
	int l_cvar_get_float(lua_State* L) { ConVar* v = find_cvar(L); if (!v) lua_pushnil(L); else lua_pushnumber(L, v->GetFloat()); return 1; }

	int l_cvar_get_string(lua_State* L)
	{
		ConVar* v = find_cvar(L);
		ConVarStr slot = v ? ConVarStrSlot(v) : ConVarStr{};
		if (!slot.ppStr || !*slot.ppStr) lua_pushnil(L);
		else lua_pushstring(L, *slot.ppStr);
		return 1;
	}

	int l_cvar_set(lua_State* L)
	{
		ConVar* v = find_cvar(L);
		if (!v) return luaL_error(L, "no cvar '%s'", lua_tostring(L, 1));

		if (lua_isinteger(L, 2) || lua_isboolean(L, 2)) {
			const int value = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) ? 1 : 0) : static_cast< int >(lua_tointeger(L, 2));
			if (!v->SetValueInt(lua_tostring(L, 1), value)) v->ForceInt(value);   // name-check miss: fall back to a raw write
		} else if (lua_type(L, 2) == LUA_TNUMBER) {
			v->ForceFloat(static_cast< float >(lua_tonumber(L, 2)));
		} else {
			return luaL_error(L, "cvar.set: string values are not supported (no ConVar::SetValue(char*) wrapper in this repo)");
		}
		return 0;
	}

	/* ---- config: config.cpp's CfgEntry table (config.h), not botox's variable_object_t ---- */

	int l_config_get(lua_State* L)
	{
		const char* key = luaL_checkstring(L, 1);
		const char* type = CfgEntry_TypeName(key);
		if (!type) return luaL_error(L, "config: no variable '%s'", key);

		std::string value;
		if (!CfgEntry_GetAsString(key, value)) { lua_pushnil(L); return 1; }

		if (strcmp(type, "bool") == 0) { lua_pushboolean(L, value == "1"); return 1; }
		if (strcmp(type, "int") == 0) { lua_pushinteger(L, atoi(value.c_str())); return 1; }
		if (strcmp(type, "float") == 0) { lua_pushnumber(L, atof(value.c_str())); return 1; }
		if (strcmp(type, "float4") == 0) {
			float f[4]{};
			sscanf_s(value.c_str(), "%f,%f,%f,%f", &f[0], &f[1], &f[2], &f[3]);
			for (float v : f) lua_pushnumber(L, v);
			return 4;
		}
		lua_pushstring(L, value.c_str());   // string
		return 1;
	}

	int l_config_set(lua_State* L)
	{
		const char* key  = luaL_checkstring(L, 1);
		const char* type = CfgEntry_TypeName(key);
		if (!type) return luaL_error(L, "config: no variable '%s'", key);

		char buf[256];
		if (strcmp(type, "bool") == 0) { CfgEntry_SetFromString(key, lua_toboolean(L, 2) ? "1" : "0"); return 0; }
		if (strcmp(type, "int") == 0) { _snprintf_s(buf, _TRUNCATE, "%lld", static_cast< long long >(luaL_checkinteger(L, 2))); CfgEntry_SetFromString(key, buf); return 0; }
		if (strcmp(type, "float") == 0) { _snprintf_s(buf, _TRUNCATE, "%g", check_float(L, 2)); CfgEntry_SetFromString(key, buf); return 0; }
		if (strcmp(type, "float4") == 0) {
			_snprintf_s(buf, _TRUNCATE, "%g,%g,%g,%g", check_float(L, 2), check_float(L, 3), check_float(L, 4), check_float(L, 5));
			CfgEntry_SetFromString(key, buf);
			return 0;
		}
		if (strcmp(type, "string") == 0) { CfgEntry_SetFromString(key, luaL_checkstring(L, 2)); return 0; }
		return luaL_error(L, "config: cannot set this type");
	}

	int l_config_type(lua_State* L)
	{
		const char* type = CfgEntry_TypeName(luaL_checkstring(L, 1));
		lua_pushstring(L, type ? type : "unknown");
		return 1;
	}

	int l_config_reset(lua_State* L) { (void)luaL_checkstring(L, 1); return luaL_error(L, "config.reset: CfgEntry has no stored default in this repo (config.h has no variable registry defaults, only a load/save path)"); }

	int l_config_list(lua_State* L)
	{
		std::vector< std::string > names;
		CfgEntry_ListNames(names);
		lua_createtable(L, 0, static_cast< int >(names.size()));
		for (const auto& name : names) {
			lua_pushstring(L, CfgEntry_TypeName(name.c_str()));
			lua_setfield(L, -2, name.c_str());
		}
		return 1;
	}

	/* ---- trace: css_bridge.h's world trace (world+props, skips local player) ---- */

	int l_trace_line(lua_State* L)
	{
		Vec3 start = check_vector(L, 1), end = check_vector(L, 4);
		Vec3 dir   = end - start;
		const float dist = dir.Length();
		if (dist > 0.f) dir *= (1.f / dist);

		float hit[3]{}, normal[3]{};
		const bool did_hit = CssBridge_TraceWorld(&start.x, &dir.x, dist, hit, normal);

		lua_newtable(L);
		set_bool(L, "hit", did_hit);
		set_field(L, "x", did_hit ? static_cast< lua_Number >(hit[0]) : static_cast< lua_Number >(end.x));
		set_field(L, "y", did_hit ? static_cast< lua_Number >(hit[1]) : static_cast< lua_Number >(end.y));
		set_field(L, "z", did_hit ? static_cast< lua_Number >(hit[2]) : static_cast< lua_Number >(end.z));
		set_field(L, "normal_x", static_cast< lua_Number >(normal[0]));
		set_field(L, "normal_y", static_cast< lua_Number >(normal[1]));
		set_field(L, "normal_z", static_cast< lua_Number >(normal[2]));
		return 1;
	}

	// Round 2: CssBridge_TraceHullWorld bridges movement.cpp's own TraceHull/TraceFilterWorldProps
	// pattern (Ray_t 4-arg Init). l_trace_hull(start_xyz, end_xyz, mins_xyz, maxs_xyz, skip_entity)
	int l_trace_hull(lua_State* L)
	{
		Vec3 start = check_vector(L, 1), end = check_vector(L, 4), mins = check_vector(L, 7), maxs = check_vector(L, 10);
		const int skip = lua_isnoneornil(L, 14) ? 0 : entity_index(L, 14);

		float out_end[3]{}, normal[3]{}, fraction = 1.f; bool start_solid = false;
		if (!CssBridge_TraceHullWorld(&start.x, &end.x, &mins.x, &maxs.x, out_end, normal, &fraction, &start_solid, skip))
			return 0;

		lua_newtable(L);
		set_field(L, "fraction", static_cast< lua_Number >(fraction));
		set_bool(L, "hit", fraction < 1.f);
		set_bool(L, "start_solid", start_solid);
		set_field(L, "x", static_cast< lua_Number >(out_end[0]));
		set_field(L, "y", static_cast< lua_Number >(out_end[1]));
		set_field(L, "z", static_cast< lua_Number >(out_end[2]));
		set_field(L, "normal_x", static_cast< lua_Number >(normal[0]));
		set_field(L, "normal_y", static_cast< lua_Number >(normal[1]));
		set_field(L, "normal_z", static_cast< lua_Number >(normal[2]));
		return 1;
	}
	int l_trace_point_contents(lua_State* L) { Vec3 p = check_vector(L, 1); lua_pushboolean(L, CssBridge_PointInSolid(&p.x)); return 1; }

	/* ---- fx: round 2 -- killeffects.cpp already has a verified client-particle dispatch
	   (CTE_DispatchEffect, re-verified live on CS:S 11-03) and IEffects001::Sparks (slot 3),
	   both used for the "blood"/"sparks" death-effect styles. Bridged here, not reinvented. ---- */
	int l_fx_particle(lua_State* L)
	{
		const char* name = luaL_checkstring(L, 1);
		Vec3 origin = check_vector(L, 2);
		Vec3 angles(opt_float(L, 5, 0.f), opt_float(L, 6, 0.f), opt_float(L, 7, 0.f));
		lua_pushboolean(L, IsInGame() && KillEffects_DispatchParticle(name, origin, angles));
		return 1;
	}
	int l_fx_sparks(lua_State* L)
	{
		Vec3 at = check_vector(L, 1);
		if (IsInGame()) KillEffects_Sparks(at);
		lua_pushboolean(L, IsInGame());
		return 1;
	}

	/* ---- input ---- */

	int l_input_is_key_down(lua_State* L)
	{
		const lua_Integer key = luaL_checkinteger(L, 1);
		lua_pushboolean(L, key >= 0 && key < 256 && (GetAsyncKeyState(static_cast< int >(key)) & 0x8000) != 0);
		return 1;
	}

	int l_input_mouse_pos(lua_State* L)
	{
		POINT point{};
		GetCursorPos(&point);
		if (g_hwnd) ScreenToClient(g_hwnd, &point);
		lua_pushinteger(L, point.x);
		lua_pushinteger(L, point.y);
		return 2;
	}

	int l_input_menu_open(lua_State* L) { lua_pushboolean(L, g_showMenu); return 1; }
	int l_input_keys_blocked(lua_State* L) { lua_pushboolean(L, g_showMenu); return 1; }   // this repo gates input on the same flag, no separate "keys blocked" accessor

	/* ---- mem: raw access, every read/write seh guarded -- fully engine-free, 1:1 botox ---- */

	int l_mem_module(lua_State* L) { push_address(L, GetModuleHandleA(luaL_checkstring(L, 1))); return 1; }

	int l_mem_find_pattern(lua_State* L)
	{
		void* base = GetModuleHandleA(luaL_checkstring(L, 1));
		if (!base) { lua_pushnil(L); return 1; }
		push_address(L, PatternScan(static_cast< HMODULE >(base), luaL_checkstring(L, 2)));
		return 1;
	}

	int l_mem_interface(lua_State* L)
	{
		push_address(L, GetInterface(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
		return 1;
	}

	int l_mem_export(lua_State* L)
	{
		HMODULE base = GetModuleHandleA(luaL_checkstring(L, 1));
		push_address(L, base ? reinterpret_cast< void* >(GetProcAddress(base, luaL_checkstring(L, 2))) : nullptr);
		return 1;
	}

	template< class T > int l_mem_read(lua_State* L)
	{
		const word_t address = check_address(L, 1);
		T value{};
		if (!seh_copy(&value, reinterpret_cast< const void* >(address), sizeof(T))) return luaL_error(L, "mem: bad read at %p", reinterpret_cast< void* >(address));
		if constexpr (std::is_floating_point_v< T >) lua_pushnumber(L, value);
		else lua_pushinteger(L, static_cast< lua_Integer >(value));
		return 1;
	}

	template< class T > int l_mem_write(lua_State* L)
	{
		const word_t address = check_address(L, 1);
		T value{};
		if constexpr (std::is_floating_point_v< T >) value = static_cast< T >(luaL_checknumber(L, 2));
		else value = static_cast< T >(luaL_checkinteger(L, 2));
		if (!seh_write(reinterpret_cast< void* >(address), &value, sizeof(T))) return luaL_error(L, "mem: bad write at %p", reinterpret_cast< void* >(address));
		return 0;
	}

	int l_mem_read_string(lua_State* L)
	{
		const word_t address = check_address(L, 1);
		const int max        = static_cast< int >(std::clamp< lua_Integer >(luaL_optinteger(L, 2, 256), 0, 1 << 20));
		const int length      = seh_strlen(reinterpret_cast< const char* >(address), max);
		if (length < 0) return luaL_error(L, "mem: bad read at %p", reinterpret_cast< void* >(address));
		lua_pushlstring(L, reinterpret_cast< const char* >(address), static_cast< std::size_t >(length));
		return 1;
	}

	int l_mem_write_string(lua_State* L)
	{
		const word_t address = check_address(L, 1);
		std::size_t length    = 0;
		const char* text      = luaL_checklstring(L, 2, &length);
		if (!seh_write(reinterpret_cast< void* >(address), text, length + 1)) return luaL_error(L, "mem: bad write at %p", reinterpret_cast< void* >(address));
		return 0;
	}

	int l_mem_read_bytes(lua_State* L)
	{
		const word_t address    = check_address(L, 1);
		const std::size_t size  = static_cast< std::size_t >(std::clamp< lua_Integer >(luaL_checkinteger(L, 2), 0, 1 << 20));
		std::string out(size, '\0');
		if (size && !seh_copy(out.data(), reinterpret_cast< const void* >(address), size)) return luaL_error(L, "mem: bad read at %p", reinterpret_cast< void* >(address));
		lua_pushlstring(L, out.data(), out.size());
		return 1;
	}

	int l_mem_write_bytes(lua_State* L)
	{
		const word_t address = check_address(L, 1);
		std::size_t length     = 0;
		const char* bytes      = luaL_checklstring(L, 2, &length);
		if (length && !seh_write(reinterpret_cast< void* >(address), bytes, length)) return luaL_error(L, "mem: bad write at %p", reinterpret_cast< void* >(address));
		return 0;
	}

	int l_mem_vfunc(lua_State* L)
	{
		const word_t object      = check_address(L, 1);
		const lua_Integer index  = luaL_checkinteger(L, 2);
		word_t table = 0, function = 0;
		if (!seh_copy(&table, reinterpret_cast< const void* >(object), sizeof(table)) ||
		    !seh_copy(&function, reinterpret_cast< const void* >(table + static_cast< word_t >(index) * sizeof(word_t)), sizeof(function)))
			return luaL_error(L, "mem: bad vtable at %p", reinterpret_cast< void* >(object));
		lua_pushinteger(L, static_cast< lua_Integer >(function));
		return 1;
	}

	int l_mem_call(lua_State* L)
	{
		static const char* const conventions[] = { "cdecl", "stdcall", "thiscall", "fastcall", nullptr };
		const word_t function = check_address(L, 1);
		const int convention   = luaL_checkoption(L, 2, "cdecl", conventions);
		const int args          = lua_gettop(L) - 2;
		const int fixed          = convention == 2 ? 1 : convention == 3 ? 2 : 0;

		if (args < fixed) return luaL_error(L, "mem.call: %s needs %d leading register arg(s)", conventions[convention], fixed);
		if (args - fixed > static_cast< int >(k_max_call_args)) return luaL_error(L, "mem.call: at most %d args", static_cast< int >(k_max_call_args));

		word_t values[k_max_call_args + 2] = {};
		for (int i = 0; i < args; i++) {
			const int index = i + 3;
			if (lua_isboolean(L, index)) values[i] = lua_toboolean(L, index) ? 1 : 0;
			else if (lua_isnil(L, index)) values[i] = 0;
			else if (lua_type(L, index) == LUA_TSTRING) values[i] = reinterpret_cast< word_t >(lua_tostring(L, index));
			else values[i] = static_cast< word_t >(luaL_checkinteger(L, index));
		}

		if (!function) return luaL_error(L, "mem.call: null function");

		word_t result = 0;
		if (!seh_invoke(k_invokers[convention][args - fixed], function, values, &result))
			return luaL_error(L, "mem.call: crashed inside %p", reinterpret_cast< void* >(function));

		lua_pushinteger(L, static_cast< lua_Integer >(result));
		return 1;
	}

	int l_mem_alloc(lua_State* L)
	{
		const std::size_t size = static_cast< std::size_t >(std::clamp< lua_Integer >(luaL_checkinteger(L, 1), 1, 64 << 20));
		void* memory             = std::calloc(1, size);
		if (!memory) return luaL_error(L, "mem.alloc: out of memory");
		n_lua_api::script_of(L)->m_allocations.push_back(memory);
		push_address(L, memory);
		return 1;
	}

	int l_mem_free(lua_State* L)
	{
		void* memory       = reinterpret_cast< void* >(check_address(L, 1));
		auto& allocations    = n_lua_api::script_of(L)->m_allocations;
		const auto it         = std::find(allocations.begin(), allocations.end(), memory);
		if (it == allocations.end()) return luaL_error(L, "mem.free: not a mem.alloc block");
		std::free(memory);
		allocations.erase(it);
		return 0;
	}

	int l_mem_protect(lua_State* L)
	{
		DWORD old_protect = 0;
		if (!VirtualProtect(reinterpret_cast< void* >(check_address(L, 1)), static_cast< SIZE_T >(luaL_checkinteger(L, 2)),
		                     static_cast< DWORD >(luaL_checkinteger(L, 3)), &old_protect)) { lua_pushnil(L); return 1; }
		lua_pushinteger(L, old_protect);
		return 1;
	}

	int l_mem_netvar(lua_State* L) { lua_pushinteger(L, netvar_offset(luaL_checkstring(L, 1))); return 1; }

	int l_mem_f2i(lua_State* L) { const float value = check_float(L, 1); std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); lua_pushinteger(L, bits); return 1; }
	int l_mem_i2f(lua_State* L) { const std::uint32_t bits = static_cast< std::uint32_t >(luaL_checkinteger(L, 1)); float value = 0.f; std::memcpy(&value, &bits, sizeof(value)); lua_pushnumber(L, value); return 1; }

	int l_mem_interfaces(lua_State* L)
	{
		const std::pair< const char*, const void* > list[] = {
			{ "engine_client", g_engine },
			{ "cvar", g_cvar },
			{ "entity_list", g_entityList },
			{ "material_system", g_matSystem },
			{ "model_render", g_modelRender },
			{ "model_info", g_modelInfo },
			{ "render_view", g_renderView },
		};
		lua_createtable(L, 0, static_cast< int >(std::size(list)));
		for (const auto& [name, pointer] : list) {
			push_address(L, pointer);
			lua_setfield(L, -2, name);
		}
		return 1;
	}

	/* ---- client ---- */

	int l_client_log(lua_State* L) { DebugLog("LUA: [%s] %s", n_lua_api::script_of(L)->m_name.c_str(), luaL_checkstring(L, 1)); return 0; }
	int l_client_chat(lua_State* L) { CssBridge_Say(luaL_checkstring(L, 1), false); return 0; }
	int l_client_name(lua_State* L) { lua_pushstring(L, n_lua_api::script_of(L)->m_name.c_str()); return 1; }
	int l_client_folder(lua_State* L) { lua_pushstring(L, g_lua.folder().string().c_str()); return 1; }
	int l_client_unload(lua_State* L) { g_lua.request(n_lua::request_unload, n_lua_api::script_of(L)->m_name); return 0; }

	int l_print(lua_State* L)
	{
		std::string line;
		const int count = lua_gettop(L);
		for (int i = 1; i <= count; i++) {
			std::size_t length = 0;
			const char* text    = luaL_tolstring(L, i, &length);
			if (i > 1) line += '\t';
			line.append(text, length);
			lua_pop(L, 1);
		}
		n_lua_api::console_msg("[" + n_lua_api::script_of(L)->m_name + "] " + line);
		return 0;
	}

	/* ---- cmd ---- */

	UserCmd_t* check_cmd(lua_State* L)
	{
		luaL_checkudata(L, 1, k_cmd_meta);
		if (!n_lua_api::s_cmd) luaL_error(L, "cmd is only valid inside its create_move / pre_prediction callback");
		return n_lua_api::s_cmd;
	}

	int l_cmd_index(lua_State* L)
	{
		UserCmd_t* cmd              = check_cmd(L);
		const std::string_view key = luaL_checkstring(L, 2);

		if (key == "command_number") lua_pushinteger(L, cmd->command_number);
		else if (key == "tick_count") lua_pushinteger(L, cmd->tick_count);
		else if (key == "pitch") lua_pushnumber(L, cmd->viewangles.x);
		else if (key == "yaw") lua_pushnumber(L, cmd->viewangles.y);
		else if (key == "roll") lua_pushnumber(L, cmd->viewangles.z);
		else if (key == "forwardmove") lua_pushnumber(L, cmd->forwardmove);
		else if (key == "sidemove") lua_pushnumber(L, cmd->sidemove);
		else if (key == "upmove") lua_pushnumber(L, cmd->upmove);
		else if (key == "buttons") lua_pushinteger(L, cmd->buttons);
		else if (key == "impulse") lua_pushinteger(L, cmd->impulse);
		else if (key == "weaponselect") lua_pushinteger(L, cmd->weaponselect);
		else if (key == "weaponsubtype") lua_pushinteger(L, cmd->weaponsubtype);
		else if (key == "random_seed") lua_pushinteger(L, cmd->random_seed);
		else if (key == "mousedx") lua_pushinteger(L, cmd->mousedx);
		else if (key == "mousedy") lua_pushinteger(L, cmd->mousedy);
		else if (key == "has_been_predicted") lua_pushboolean(L, cmd->hasbeenpredicted);
		else if (key == "send_packet") lua_pushboolean(L, n_lua_api::s_send_packet ? *n_lua_api::s_send_packet : true);
		else if (key == "ptr") push_address(L, cmd);
		else lua_pushnil(L);
		return 1;
	}

	int l_cmd_newindex(lua_State* L)
	{
		UserCmd_t* cmd              = check_cmd(L);
		const std::string_view key = luaL_checkstring(L, 2);

		if (key == "command_number") cmd->command_number = static_cast< int >(luaL_checkinteger(L, 3));
		else if (key == "tick_count") cmd->tick_count = static_cast< int >(luaL_checkinteger(L, 3));
		else if (key == "pitch") cmd->viewangles.x = check_float(L, 3);
		else if (key == "yaw") cmd->viewangles.y = check_float(L, 3);
		else if (key == "roll") cmd->viewangles.z = check_float(L, 3);
		else if (key == "forwardmove") cmd->forwardmove = check_float(L, 3);
		else if (key == "sidemove") cmd->sidemove = check_float(L, 3);
		else if (key == "upmove") cmd->upmove = check_float(L, 3);
		else if (key == "buttons") cmd->buttons = static_cast< int >(luaL_checkinteger(L, 3));
		else if (key == "impulse") cmd->impulse = static_cast< unsigned char >(luaL_checkinteger(L, 3));
		else if (key == "weaponselect") cmd->weaponselect = static_cast< int >(luaL_checkinteger(L, 3));
		else if (key == "weaponsubtype") cmd->weaponsubtype = static_cast< int >(luaL_checkinteger(L, 3));
		else if (key == "random_seed") cmd->random_seed = static_cast< int >(luaL_checkinteger(L, 3));
		else if (key == "mousedx") cmd->mousedx = static_cast< short >(luaL_checkinteger(L, 3));
		else if (key == "mousedy") cmd->mousedy = static_cast< short >(luaL_checkinteger(L, 3));
		else if (key == "send_packet") { if (n_lua_api::s_send_packet) *n_lua_api::s_send_packet = lua_toboolean(L, 3); }
		else return luaL_error(L, "cmd has no writable field '%s'", std::string(key).c_str());
		return 0;
	}

	/* ---- event: raw IGameEvent*, globals/common.h IGameEventVtbl ---- */

	void* check_event(lua_State* L)
	{
		luaL_checkudata(L, 1, k_event_meta);
		if (!n_lua_api::s_event) luaL_error(L, "event is only valid inside its callback");
		return n_lua_api::s_event;
	}

	int l_event_name(lua_State* L) { lua_pushstring(L, IGameEventVtbl::GetName(check_event(L))); return 1; }
	int l_event_get_int(lua_State* L) { lua_pushinteger(L, IGameEventVtbl::GetInt(check_event(L), luaL_checkstring(L, 2), static_cast< int >(luaL_optinteger(L, 3, 0)))); return 1; }
	int l_event_get_float(lua_State* L) { lua_pushnumber(L, IGameEventVtbl::GetFloat(check_event(L), luaL_checkstring(L, 2), opt_float(L, 3, 0.f))); return 1; }
	int l_event_get_bool(lua_State* L) { lua_pushboolean(L, IGameEventVtbl::GetBool(check_event(L), luaL_checkstring(L, 2), lua_toboolean(L, 3))); return 1; }
	int l_event_get_string(lua_State* L) { lua_pushstring(L, IGameEventVtbl::GetString(check_event(L), luaL_checkstring(L, 2), luaL_optstring(L, 3, ""))); return 1; }
	int l_event_get_uint64(lua_State* L) { (void)check_event(L); lua_pushinteger(L, 0); return 1; }   // CS:S IGameEvent has no GetUint64 (verified: source-sdk-2013 public/igameevents.h)

	int l_event_get_player(lua_State* L)
	{
		const int user_id = IGameEventVtbl::GetInt(check_event(L), luaL_optstring(L, 2, "userid"), 0);
		push_entity(L, CssBridge_PlayerForUserId(user_id));
		return 1;
	}

	/* ---- registration ---- */

	void new_meta(lua_State* L, const char* name, const luaL_Reg* methods, const luaL_Reg* metamethods)
	{
		luaL_newmetatable(L, name);
		if (metamethods) luaL_setfuncs(L, metamethods, 0);
		if (methods) {
			lua_newtable(L);
			luaL_setfuncs(L, methods, 0);
			lua_setfield(L, -2, "__index");
		}
		lua_pushstring(L, "locked");
		lua_setfield(L, -2, "__metatable");
		lua_pop(L, 1);
	}

	void add_constants(lua_State* L, const char* name, const std::pair< const char*, lua_Integer >* values, const std::size_t count)
	{
		lua_createtable(L, 0, static_cast< int >(count));
		for (std::size_t i = 0; i < count; i++) {
			lua_pushinteger(L, values[i].second);
			lua_setfield(L, -2, values[i].first);
		}
		lua_setglobal(L, name);
	}
}

void n_lua_api::console_msg(const std::string& text)
{
	using msg_fn         = void(__cdecl*)(const char*, ...);
	static const auto fn = reinterpret_cast< msg_fn >(GetProcAddress(GetModuleHandleA("tier0.dll"), "Msg"));
	if (fn) fn("%s\n", text.c_str());
}

int n_lua_api::traceback(lua_State* L)
{
	const char* message = lua_tostring(L, 1);
	if (!message) message = luaL_tolstring(L, 1, nullptr);
	luaL_traceback(L, L, message, 1);
	return 1;
}

void n_lua_api::push_cmd(lua_State* L) { lua_newuserdatauv(L, 1, 0); luaL_setmetatable(L, k_cmd_meta); }
void n_lua_api::push_event(lua_State* L) { lua_newuserdatauv(L, 1, 0); luaL_setmetatable(L, k_event_meta); }

void n_lua_api::open(lua_State* L)
{
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "botox.callbacks");

	/* require( "name" ) -> C:\botox-css\lua\lib\name.lua ( lib is not scanned, so modules never show as scripts ) */
	if (lua_getglobal(L, "package") == LUA_TTABLE) {
		const std::string folder = g_lua.folder().string();
		lua_pushstring(L, (folder + "\\lib\\?.lua;" + folder + "\\lib\\?\\init.lua").c_str());
		lua_setfield(L, -2, "path");
	}
	lua_pop(L, 1);

	lua_pushcfunction(L, l_print);
	lua_setglobal(L, "print");

	lua_pushcfunction(L, l_color);
	lua_setglobal(L, "color");

	static const luaL_Reg callbacks[] = { { "register", l_callbacks_register }, { "unregister", l_callbacks_unregister }, { nullptr, nullptr } };
	add_table(L, "callbacks", callbacks);

	static const luaL_Reg ui[] = { { "checkbox", l_ui_checkbox }, { "slider_int", l_ui_slider_int }, { "slider_float", l_ui_slider_float },
		                            { "combo", l_ui_combo },       { "color", l_ui_color },           { "keybind", l_ui_keybind },
		                            { "button", l_ui_button },     { "text", l_ui_text },             { "input", l_ui_input },
		                            { nullptr, nullptr } };
	add_table(L, "ui", ui);

	static const luaL_Reg element_methods[] = { { "get", l_element_get }, { "set", l_element_set }, { "visible", l_element_visible },
		                                         { "label", l_element_label }, { nullptr, nullptr } };
	new_meta(L, k_element_meta, element_methods, nullptr);

	static const luaL_Reg render[] = { { "screen_size", l_render_screen_size }, { "text", l_render_text }, { "text_size", l_render_text_size },
		                                { "line", l_render_line }, { "rect", l_render_rect }, { "rect_filled", l_render_rect_filled },
		                                { "gradient", l_render_gradient }, { "circle", l_render_circle }, { "circle_filled", l_render_circle_filled },
		                                { "triangle", l_render_triangle }, { "poly", l_render_poly }, { "world_to_screen", l_render_world_to_screen },
		                                { nullptr, nullptr } };
	add_table(L, "render", render);

	static const luaL_Reg entities[] = { { "local_player", l_entities_local }, { "get", l_entities_get }, { "from_handle", l_entities_from_handle },
		                                  { "highest_index", l_entities_highest_index }, { "players", l_entities_players }, { "by_class", l_entities_by_class },
		                                  { nullptr, nullptr } };
	add_table(L, "entities", entities);

	static const luaL_Reg entity_methods[] = { { "index", l_entity_index }, { "ptr", l_entity_ptr }, { "is_valid", l_entity_is_valid },
		                                        { "is_player", l_entity_is_player }, { "is_alive", l_entity_is_alive }, { "is_dormant", l_entity_is_dormant },
		                                        { "is_enemy", l_entity_is_enemy }, { "team", l_entity_team }, { "health", l_entity_health },
		                                        { "armor", l_entity_armor }, { "flags", l_entity_flags }, { "move_type", l_entity_move_type },
		                                        { "origin", l_entity_origin }, { "velocity", l_entity_velocity }, { "eye_position", l_entity_eye_position },
		                                        { "mins_maxs", l_entity_mins_maxs }, { "class_name", l_entity_class_name }, { "class_id", l_entity_class_id },
		                                        { "name", l_entity_name }, { "weapon", l_entity_weapon }, { "weapon_id", l_entity_weapon_id },
		                                        { "bone_position", l_entity_bone_position }, { "hitbox_position", l_entity_hitbox_position },
		                                        { "get_prop", l_entity_get_prop }, { "set_prop", l_entity_set_prop },
		                                        { "get_datafield", l_entity_get_datafield }, { "set_datafield", l_entity_set_datafield },
		                                        { nullptr, nullptr } };
	static const luaL_Reg entity_meta[] = { { "__eq", l_entity_eq }, { "__tostring", l_entity_tostring }, { nullptr, nullptr } };
	new_meta(L, k_entity_meta, entity_methods, entity_meta);

	static const luaL_Reg engine[] = { { "exec", l_engine_exec }, { "in_game", l_engine_in_game }, { "connected", l_engine_connected },
		                                { "local_index", l_engine_local_index }, { "max_clients", l_engine_max_clients }, { "map_name", l_engine_map_name },
		                                { "level_name", l_engine_level_name }, { "view_angles", l_engine_view_angles }, { "set_view_angles", l_engine_set_view_angles },
		                                { "player_info", l_engine_player_info }, { "player_for_user_id", l_engine_player_for_user_id },
		                                { "playing_demo", l_engine_playing_demo }, { "latency", l_engine_latency }, { nullptr, nullptr } };
	add_table(L, "engine", engine);

	static const luaL_Reg globals[] = { { "realtime", l_globals_realtime }, { "curtime", l_globals_curtime }, { "frametime", l_globals_frametime },
		                                 { "absframetime", l_globals_absframetime }, { "tickcount", l_globals_tickcount }, { "tickinterval", l_globals_tickinterval },
		                                 { "framecount", l_globals_framecount }, { "maxclients", l_globals_maxclients }, { nullptr, nullptr } };
	add_table(L, "globals", globals);

	static const luaL_Reg cvar[] = { { "exists", l_cvar_exists }, { "get_int", l_cvar_get_int }, { "get_float", l_cvar_get_float },
		                              { "get_string", l_cvar_get_string }, { "set", l_cvar_set }, { nullptr, nullptr } };
	add_table(L, "cvar", cvar);

	static const luaL_Reg config[] = { { "get", l_config_get }, { "set", l_config_set }, { "type", l_config_type },
		                                { "reset", l_config_reset }, { "list", l_config_list }, { nullptr, nullptr } };
	add_table(L, "config", config);

	static const luaL_Reg trace[] = { { "line", l_trace_line }, { "hull", l_trace_hull }, { "point_contents", l_trace_point_contents }, { nullptr, nullptr } };
	add_table(L, "trace", trace);

	static const luaL_Reg fx[] = { { "particle", l_fx_particle }, { "sparks", l_fx_sparks }, { nullptr, nullptr } };
	add_table(L, "fx", fx);

	static const luaL_Reg input[] = { { "is_key_down", l_input_is_key_down }, { "mouse_pos", l_input_mouse_pos }, { "menu_open", l_input_menu_open },
		                               { "keys_blocked", l_input_keys_blocked }, { nullptr, nullptr } };
	add_table(L, "input", input);

	static const luaL_Reg mem[] = { { "module", l_mem_module }, { "find_pattern", l_mem_find_pattern }, { "interface", l_mem_interface },
		                             { "export", l_mem_export }, { "interfaces", l_mem_interfaces }, { "netvar", l_mem_netvar },
		                             { "read_i8", l_mem_read< std::int8_t > }, { "read_u8", l_mem_read< std::uint8_t > },
		                             { "read_i16", l_mem_read< std::int16_t > }, { "read_u16", l_mem_read< std::uint16_t > },
		                             { "read_i32", l_mem_read< std::int32_t > }, { "read_u32", l_mem_read< std::uint32_t > },
		                             { "read_i64", l_mem_read< std::int64_t > }, { "read_float", l_mem_read< float > },
		                             { "read_double", l_mem_read< double > }, { "read_ptr", l_mem_read< word_t > },
		                             { "read_string", l_mem_read_string }, { "read_bytes", l_mem_read_bytes },
		                             { "write_i8", l_mem_write< std::int8_t > }, { "write_u8", l_mem_write< std::uint8_t > },
		                             { "write_i16", l_mem_write< std::int16_t > }, { "write_u16", l_mem_write< std::uint16_t > },
		                             { "write_i32", l_mem_write< std::int32_t > }, { "write_u32", l_mem_write< std::uint32_t > },
		                             { "write_i64", l_mem_write< std::int64_t > }, { "write_float", l_mem_write< float > },
		                             { "write_double", l_mem_write< double > }, { "write_ptr", l_mem_write< word_t > },
		                             { "write_string", l_mem_write_string }, { "write_bytes", l_mem_write_bytes },
		                             { "vfunc", l_mem_vfunc }, { "call", l_mem_call }, { "alloc", l_mem_alloc }, { "free", l_mem_free },
		                             { "protect", l_mem_protect }, { "f2i", l_mem_f2i }, { "i2f", l_mem_i2f }, { nullptr, nullptr } };
	add_table(L, "mem", mem);

	static const luaL_Reg client[] = { { "log", l_client_log }, { "chat", l_client_chat }, { "name", l_client_name },
		                                { "folder", l_client_folder }, { "unload", l_client_unload }, { nullptr, nullptr } };
	add_table(L, "client", client);

	static const luaL_Reg cmd_meta[] = { { "__index", l_cmd_index }, { "__newindex", l_cmd_newindex }, { nullptr, nullptr } };
	new_meta(L, k_cmd_meta, nullptr, cmd_meta);

	static const luaL_Reg event_methods[] = { { "name", l_event_name }, { "get_int", l_event_get_int }, { "get_float", l_event_get_float },
		                                       { "get_bool", l_event_get_bool }, { "get_string", l_event_get_string }, { "get_uint64", l_event_get_uint64 },
		                                       { "get_player", l_event_get_player }, { nullptr, nullptr } };
	new_meta(L, k_event_meta, event_methods, nullptr);

	// Source SDK 2013 game/shared/in_buttons.h -- same bits on every Source multiplayer game.
	static const std::pair< const char*, lua_Integer > buttons[] = {
		{ "attack", 1 << 0 },     { "jump", 1 << 1 },      { "duck", 1 << 2 },       { "forward", 1 << 3 },
		{ "back", 1 << 4 },       { "use", 1 << 5 },       { "left", 1 << 7 },       { "right", 1 << 8 },
		{ "moveleft", 1 << 9 },   { "moveright", 1 << 10 }, { "attack2", 1 << 11 },  { "reload", 1 << 13 },
		{ "score", 1 << 16 },     { "speed", 1 << 17 },    { "walk", 1 << 18 },      { "zoom", 1 << 19 },
		{ "bullrush", 1 << 22 },
	};
	add_constants(L, "buttons", buttons, std::size(buttons));

	// public/const.h, CS:S branch (not HL2_DLL/EPISODIC/PORTAL) m_fFlags bit layout.
	static const std::pair< const char*, lua_Integer > flags[] = {
		{ "onground", 1 << 0 }, { "ducking", 1 << 1 }, { "animducking", 1 << 2 }, { "waterjump", 1 << 3 },
		{ "frozen", 1 << 6 },   { "inwater", 1 << 10 }, { "fakeclient", 1 << 9 },  { "godmode", 1 << 15 },
		{ "partialground", 1 << 18 },
	};
	add_constants(L, "flags", flags, std::size(flags));

	// public/cdll_int.h ClientFrameStage_t -- same enum on every Source game.
	static const std::pair< const char*, lua_Integer > stages[] = {
		{ "start", 0 }, { "net_update_start", 1 }, { "net_update_postdataupdate_start", 2 },
		{ "net_update_postdataupdate_end", 3 }, { "net_update_end", 4 }, { "render_start", 5 }, { "render_end", 6 },
	};
	add_constants(L, "stages", stages, std::size(stages));

	// This repo's own real global ImFont*s (globals.h) -- see font_of() above.
	static const std::pair< const char*, lua_Integer > fonts[] = {
		{ "default", 0 }, { "bold", 1 }, { "indicator", 2 }, { "icons", 3 },
	};
	add_constants(L, "fonts", fonts, std::size(fonts));

	static const std::pair< const char*, lua_Integer > text_flags[] = { { "none", 0 }, { "shadow", 1 }, { "outline", 2 } };
	add_constants(L, "text_flags", text_flags, std::size(text_flags));
}
