// CSS port of botox csgo-sdk hacks/lua/lua.cpp (1:1 engine logic). Differences from botox:
//  - no g_config/variables.h here: the script folder is CONFIG_PATH's directory + "lua"
//    (types.h: CONFIG_PATH = "C:\botox-css\default.cfg" -> "C:\botox-css\lua").
//  - no nlohmann::json dependency in this repo (ponytail: ADD NONE for one save format) --
//    saved element settings use a flat "label=value" text format, one line per element,
//    values escaped the same way config.cpp already escapes its own string values.
//  - no g_logger: DebugLog_* (dllmain.cpp) + the game console (Msg) carry every message botox
//    split across g_logger.print/botox_dbg_log/console_msg.
#define _SILENCE_CXX20_CISO646_REMOVED_WARNING
#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "../../dependencies/lua/lauxlib.h"
#include "../../dependencies/lua/lua.h"
#include "../../dependencies/lua/lualib.h"
#include "../../globals/globals.h"

#include "lua.h"
#include "lua_api.h"

void DebugLog(const char* fmt, ...);

using n_lua::element_t;
using n_lua::script_t;

namespace
{
	// one flat key=value store per loaded script, same lifetime rule as botox's s_saved
	// (erased on unload, re-read on load -- a script's settings never leak into another's).
	using saved_map_t = std::unordered_map< std::string, std::string >;
	std::unordered_map< const script_t*, saved_map_t > s_saved;

	std::string EscapeLine(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (char c : s) {
			if (c == '\\') out += "\\\\";
			else if (c == '\n') out += "\\n";
			else if (c == '=') out += "\\=";
			else out += c;
		}
		return out;
	}

	std::string UnescapeLine(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (std::size_t i = 0; i < s.size(); i++) {
			if (s[i] == '\\' && i + 1 < s.size()) {
				char n = s[++i];
				out += (n == 'n') ? '\n' : n;
			} else
				out += s[i];
		}
		return out;
	}

	saved_map_t LoadSavedMap(const std::filesystem::path& path)
	{
		saved_map_t out;
		std::ifstream file(path);
		for (std::string line; std::getline(file, line); ) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const std::size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			out[UnescapeLine(line.substr(0, eq))] = UnescapeLine(line.substr(eq + 1));
		}
		return out;
	}

	void WriteSavedMap(const std::filesystem::path& path, const saved_map_t& map)
	{
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		std::ofstream file(path, std::ios::trunc);
		if (!file.good()) return;
		for (const auto& [key, value] : map)
			file << EscapeLine(key) << '=' << EscapeLine(value) << '\n';
	}
}

static int lua_panic(lua_State* L)
{
	const char* message = lua_tostring(L, -1);
	DebugLog("LUA: panic %s", message ? message : "?");
	return 0;
}

/* push( L ) pushes the args and returns how many */
template< class push_t >
static void call_script(script_t& script, const char* name, push_t&& push)
{
	lua_State* L = script.m_state;
	if (!L || script.m_unload_requested)
		return;

	const int top = lua_gettop(L);

	lua_getfield(L, LUA_REGISTRYINDEX, "botox.callbacks");
	if (lua_istable(L, -1) && lua_getfield(L, -1, name) == LUA_TTABLE) {
		const int list         = lua_gettop(L);
		const lua_Integer size = static_cast< lua_Integer >(lua_rawlen(L, list));

		for (lua_Integer i = 1; i <= size; i++) {
			lua_pushcfunction(L, n_lua_api::traceback);
			const int handler = lua_gettop(L);

			if (lua_rawgeti(L, list, i) != LUA_TFUNCTION) {
				lua_settop(L, list);
				continue;
			}

			const int args = push(L);
			if (lua_pcall(L, args, 0, handler) != LUA_OK) {
				g_lua.fail(script, name, lua_tostring(L, -1));
				break;
			}

			lua_settop(L, list);
		}
	}

	lua_settop(L, top);
}

template< class push_t >
static void dispatch(const char* name, push_t&& push)
{
	for (auto& script : g_lua.m_scripts)
		call_script(*script, name, push);
}

static int no_args(lua_State*)
{
	return 0;
}

std::filesystem::path n_lua::impl_t::folder() const
{
	// CONFIG_PATH = "C:\botox-css\default.cfg" (globals/types.h) -- same root every other
	// botox-css folder (points/, visuals/, images/) hangs off (see CLAUDE.md paths table).
	return std::filesystem::path(CONFIG_PATH).parent_path() / "lua";
}

void n_lua::impl_t::request(const e_request type, const std::string& name)
{
	std::lock_guard< std::mutex > lock(this->m_mutex);
	this->m_requests.emplace_back(type, name);
}

void n_lua::impl_t::fail(script_t& script, const char* where, const char* message)
{
	char text[512];
	_snprintf_s(text, _TRUNCATE, "%s (%s): %s", script.m_name.c_str(), where, message ? message : "unknown error");

	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		script.m_error            = std::string(where) + ": " + (message ? message : "unknown error");
		script.m_unload_requested = true;
	}

	n_lua_api::console_msg(std::string("[lua] ") + text);
	DebugLog("LUA: error %s", text);
}

void n_lua::impl_t::refresh()
{
	std::vector< std::filesystem::path > files;

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(this->folder(), ec))
		if (entry.is_regular_file(ec) && entry.path().extension() == ".lua")
			files.push_back(entry.path());

	std::lock_guard< std::mutex > lock(this->m_mutex);

	std::erase_if(this->m_scripts, [&](const std::unique_ptr< script_t >& script) {
		return !script->m_state && std::find(files.begin(), files.end(), script->m_path) == files.end();
	});

	for (const auto& path : files) {
		const bool known = std::any_of(this->m_scripts.begin(), this->m_scripts.end(),
		                                [&](const std::unique_ptr< script_t >& script) { return script->m_path == path; });
		if (known)
			continue;

		auto script    = std::make_unique< script_t >();
		script->m_name = path.stem().string();
		script->m_path = path;
		this->m_scripts.push_back(std::move(script));
	}

	std::sort(this->m_scripts.begin(), this->m_scripts.end(),
	          [](const std::unique_ptr< script_t >& a, const std::unique_ptr< script_t >& b) { return a->m_name < b->m_name; });
}

void n_lua::impl_t::load(script_t& script)
{
	if (script.m_state)
		return;

	std::ifstream file(script.m_path, std::ios::binary);
	if (!file.good()) {
		std::lock_guard< std::mutex > lock(this->m_mutex);
		script.m_error = "load: cannot open " + script.m_path.string();
		return;
	}

	std::stringstream source;
	source << file.rdbuf();
	const std::string code = source.str();

	s_saved[&script] = LoadSavedMap(this->folder() / "settings" / (script.m_name + ".txt"));

	lua_State* L = luaL_newstate();
	if (!L) {
		std::lock_guard< std::mutex > lock(this->m_mutex);
		script.m_error = "load: out of memory";
		return;
	}

	lua_atpanic(L, lua_panic);
	*static_cast< script_t** >(lua_getextraspace(L)) = &script;

	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		script.m_error.clear();
		script.m_elements.clear();
		script.m_unload_requested = false;
		script.m_state            = L;
	}

	luaL_openlibs(L);
	n_lua_api::open(L);

	lua_pushcfunction(L, n_lua_api::traceback);
	const int handler = lua_gettop(L);

	const std::string chunk = "@" + script.m_name + ".lua";
	if (luaL_loadbuffer(L, code.data(), code.size(), chunk.c_str()) != LUA_OK || lua_pcall(L, 0, 0, handler) != LUA_OK) {
		this->fail(script, "load", lua_tostring(L, -1));
		this->unload(script, false);
		return;
	}

	lua_settop(L, 0);

	DebugLog("LUA: loaded %s", script.m_name.c_str());
}

void n_lua::impl_t::unload(script_t& script, const bool run_callback)
{
	lua_State* L = script.m_state;
	if (!L)
		return;

	if (run_callback) {
		const bool failed = script.m_unload_requested;
		script.m_unload_requested = false;
		call_script(script, "unload", no_args);
		script.m_unload_requested = failed;
	}

	this->save_settings(script);

	lua_close(L);

	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		script.m_state = nullptr;
		script.m_elements.clear();
		script.m_unload_requested = false;
		script.m_dirty            = false;
	}

	for (void* allocation : script.m_allocations)
		std::free(allocation);
	script.m_allocations.clear();

	s_saved.erase(&script);

	DebugLog("LUA: unloaded %s", script.m_name.c_str());
}

void n_lua::impl_t::apply_saved(script_t& script, element_t& element)
{
	const auto saved = s_saved.find(&script);
	if (saved == s_saved.end())
		return;
	const auto it = saved->second.find(element.m_label);
	if (it == saved->second.end())
		return;

	const std::string& value = it->second;
	switch (element.m_type) {
	case element_checkbox: element.m_bool = value == "1"; break;
	case element_slider_int: element.m_int = std::clamp(atoi(value.c_str()), element.m_int_min, element.m_int_max); break;
	case element_combo: element.m_int = std::clamp(atoi(value.c_str()), 0, std::max(static_cast< int >(element.m_items.size()) - 1, 0)); break;
	case element_slider_float: element.m_float = std::clamp(static_cast< float >(atof(value.c_str())), element.m_float_min, element.m_float_max); break;
	case element_color: {
		float c[4] = { 1.f, 1.f, 1.f, 1.f };
		if (sscanf_s(value.c_str(), "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]) == 4)
			for (int i = 0; i < 4; i++)
				element.m_color[i] = std::clamp(c[i], 0.f, 1.f);
		break;
	}
	case element_keybind: {
		int key = 0, style = 1;
		if (sscanf_s(value.c_str(), "%d,%d", &key, &style) == 2)
			element.m_bind = lua_key_bind_t{ std::clamp(key, 0, 255), std::clamp(style, 0, 2) };
		break;
	}
	case element_input: strncpy_s(element.m_text, value.c_str(), _TRUNCATE); break;
	default: break;
	}
}

void n_lua::impl_t::save_settings(script_t& script)
{
	/* merge: a script that failed half way only re-saves what it built, the rest of the file stays */
	auto saved        = s_saved.find(&script);
	saved_map_t out   = saved != s_saved.end() ? saved->second : saved_map_t{};

	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		script.m_dirty = false;

		for (const element_t& element : script.m_elements) {
			char buf[256];
			switch (element.m_type) {
			case element_checkbox: out[element.m_label] = element.m_bool ? "1" : "0"; break;
			case element_slider_int:
			case element_combo: out[element.m_label] = std::to_string(element.m_int); break;
			case element_slider_float: out[element.m_label] = std::to_string(element.m_float); break;
			case element_color:
				_snprintf_s(buf, _TRUNCATE, "%f,%f,%f,%f", element.m_color[0], element.m_color[1], element.m_color[2], element.m_color[3]);
				out[element.m_label] = buf;
				break;
			case element_keybind:
				_snprintf_s(buf, _TRUNCATE, "%d,%d", element.m_bind.m_key, element.m_bind.m_key_style);
				out[element.m_label] = buf;
				break;
			case element_input: out[element.m_label] = std::string(element.m_text); break;
			default: break;
			}
		}
	}

	if (out.empty())
		return;

	if (saved != s_saved.end())
		saved->second = out;

	WriteSavedMap(this->folder() / "settings" / (script.m_name + ".txt"), out);
}

void n_lua::impl_t::write_autoload()
{
	std::string names;
	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		for (const auto& script : this->m_scripts)
			if (script->m_state)
				names += script->m_name + "\n";
	}

	std::error_code ec;
	std::filesystem::create_directories(this->folder(), ec);
	std::ofstream file(this->folder() / "autoload.txt", std::ios::trunc);
	if (file.good())
		file << names;
}

static script_t* find_script(const std::string& name)
{
	for (auto& script : g_lua.m_scripts)
		if (script->m_name == name)
			return script.get();

	return nullptr;
}

void n_lua::impl_t::on_paint()
{
	if (this->m_shutdown)
		return;

	if (!this->m_initialised) {
		this->m_initialised = true;

		std::error_code ec;
		std::filesystem::create_directories(this->folder() / "settings", ec);
		std::filesystem::create_directories(this->folder() / "lib", ec);

		this->refresh();

		std::ifstream autoload(this->folder() / "autoload.txt");
		for (std::string line; std::getline(autoload, line); ) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (script_t* script = find_script(line))
				this->load(*script);
		}
	}

	std::vector< std::pair< e_request, std::string > > requests;
	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		requests.swap(this->m_requests);
	}

	bool changed = false;
	for (const auto& [type, name] : requests) {
		if (type == request_refresh) {
			this->refresh();
			continue;
		}

		script_t* script = find_script(name);
		if (!script)
			continue;

		if (type == request_unload || type == request_reload)
			this->unload(*script, true);

		if (type == request_load || type == request_reload)
			this->load(*script);

		changed = true;
	}

	for (auto& script : this->m_scripts) {
		if (script->m_state && script->m_unload_requested) {
			this->unload(*script, true);
			changed = true;
		}
	}

	if (changed)
		this->write_autoload();

	std::vector< std::pair< script_t*, int > > clicks;
	{
		std::lock_guard< std::mutex > lock(this->m_mutex);
		for (auto& script : this->m_scripts)
			for (element_t& element : script->m_elements)
				if (element.m_clicked) {
					element.m_clicked = false;
					if (script->m_state && element.m_callback >= 0)
						clicks.emplace_back(script.get(), element.m_callback);
				}
	}

	for (const auto& [script, ref] : clicks) {
		lua_State* L = script->m_state;
		if (!L || script->m_unload_requested)
			continue;

		lua_pushcfunction(L, n_lua_api::traceback);
		const int handler = lua_gettop(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		if (lua_pcall(L, 0, 0, handler) != LUA_OK)
			this->fail(*script, "button", lua_tostring(L, -1));
		lua_settop(L, handler - 1);
	}

	static unsigned long long next_save = 0ull;
	if (const unsigned long long now = GetTickCount64(); now >= next_save) {
		next_save = now + 1000ull;
		/* m_dirty is written by the menu under the lock: read it there too */
		std::vector< script_t* > dirty;
		{
			std::lock_guard< std::mutex > lock(this->m_mutex);
			for (auto& script : this->m_scripts)
				if (script->m_state && script->m_dirty)
					dirty.push_back(script.get());
		}
		for (script_t* script : dirty)
			this->save_settings(*script);
	}

	// Level init/shutdown: no LevelInitPostEntity/LevelShutdown hook exists anywhere in
	// botox-css this session (grep clean -- CLAUDE.md's own layout table lists no such hook).
	// on_paint already runs every render frame, so the in-game state edge IsInGame() (globals.h)
	// already tracks is read here instead of adding a new engine hook for one transition.
	{
		static bool s_wasInGame = false;
		const bool inGame       = IsInGame();
		if (inGame && !s_wasInGame) this->on_level_init();
		else if (!inGame && s_wasInGame) this->on_level_shutdown();
		s_wasInGame = inGame;
	}

	n_lua_api::s_in_paint = true;
	dispatch("paint", no_args);
	n_lua_api::s_in_paint = false;
}

void n_lua::impl_t::on_create_move(UserCmd_t* cmd, bool* send_packet, const bool pre_prediction)
{
	if (this->m_shutdown || !cmd)
		return;

	n_lua_api::s_cmd         = cmd;
	n_lua_api::s_send_packet = send_packet;

	dispatch(pre_prediction ? "pre_prediction" : "create_move", [](lua_State* L) {
		n_lua_api::push_cmd(L);
		return 1;
	});

	n_lua_api::s_cmd         = nullptr;
	n_lua_api::s_send_packet = nullptr;
}

void n_lua::impl_t::on_frame_stage(const int stage)
{
	if (this->m_shutdown)
		return;

	dispatch("frame_stage", [stage](lua_State* L) {
		lua_pushinteger(L, stage);
		return 1;
	});
}

void n_lua::impl_t::on_event(void* event)
{
	if (this->m_shutdown || !event)
		return;

	const char* name = IGameEventVtbl::GetName(event);
	if (!name || !*name)
		return;

	n_lua_api::s_event = event;

	const auto push = [](lua_State* L) {
		n_lua_api::push_event(L);
		return 1;
	};

	dispatch("game_event", push);
	dispatch(name, push);

	n_lua_api::s_event = nullptr;
}

void n_lua::impl_t::on_level_init()
{
	if (!this->m_shutdown)
		dispatch("level_init", no_args);
}

void n_lua::impl_t::on_level_shutdown()
{
	if (!this->m_shutdown)
		dispatch("level_shutdown", no_args);
}

void n_lua::impl_t::shutdown()
{
	if (this->m_shutdown.exchange(true))
		return;

	for (auto& script : this->m_scripts)
		this->unload(*script, true);
}
