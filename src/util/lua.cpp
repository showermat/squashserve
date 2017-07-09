#include "lua.h"

namespace lua
{
	luastate::luastate(lua_State *s) : state{s}, managed{false}
	{
		if (! state) throw std::runtime_error{"Tried to initialize luastate with bad state"};
	}
	luastate::luastate() : state{luaL_newstate()}, managed{true}
	{
		if (! state) throw std::runtime_error{"Couldn't initialize Lua state"};
		luaL_openlibs(state);
	}

	template <> void luastate::push<bool>(bool t) { lua_pushboolean(state, t); }
	template <> void luastate::push<int>(int t) { lua_pushinteger(state, t); }
	template <> void luastate::push<double>(double t) { lua_pushnumber(state, t); }
	template <> void luastate::push<std::string>(std::string t) { lua_pushstring(state, t.c_str()); }
	template <> void luastate::push<std::nullptr_t>(std::nullptr_t t) { lua_pushnil(state); }
	template <> bool luastate::is<bool>(int i) { return lua_isboolean(state, i); }
	template <> bool luastate::is<int>(int i) { return lua_isinteger(state, i); }
	template <> bool luastate::is<double>(int i) { return lua_isnumber(state, i); }
	template <> bool luastate::is<std::string>(int i) { return lua_isstring(state, i); }
	template <> bool luastate::is<std::nullptr_t>(int i) { return lua_isnil(state, i); }
	template <> bool luastate::to<bool>(int i) { return lua_toboolean(state, i); }
	template <> int luastate::to<int>(int i) { return lua_tointeger(state, i); }
	template <> double luastate::to<double>(int i) { return lua_tonumber(state, i); }
	template <> std::string luastate::to<std::string>(int i) { return std::string{lua_tostring(state, i)}; }
	template <> void luastate::push_args<>() { }

	luastate::~luastate()
	{
		if (state && managed) lua_close(state);
	}

	void luastate::pop(int n) { lua_pop(state, n); }
	void luastate::pushlightuserdata(void *data) { lua_pushlightuserdata(state, data); }
	void luastate::pushcclosure(int (*func)(lua_State *), int ndata) { lua_pushcclosure(state, func, ndata); }
	void luastate::setglobal(const std::string &name) { lua_setglobal(state, name.c_str()); }
	int luastate::getglobal(const std::string &name) { return lua_getglobal(state, name.c_str()); }
	int luastate::getfield(int idx, const std::string &name) { return lua_getfield(state, idx, name.c_str()); }
	int luastate::next(int idx) { return lua_next(state, idx); }
	void luastate::remove(int idx) { lua_remove(state, idx); }
	int luastate::gettop() { return lua_gettop(state); }
	int luastate::dostring(const std::string &str) { return luaL_dostring(state, str.c_str()); }
	int luastate::loadfile(const std::string &fname) { return luaL_loadfile(state, fname.c_str()); }
	int luastate::pcall(int nargs, int nres, int msgh) { return lua_pcall(state, nargs, nres, msgh); }

	iter::iter(exec &owner) : o{owner}, offset{0}, valid{true}
	{
		offset = o.state.gettop();
		o.state.push(nullptr);
	}

	iter::iter(iter &&orig) : o{orig.o}, offset{orig.offset}, valid{true}
	{
		orig.valid = false;
	}

	bool iter::next()
	{
		return (o.state.next(offset) != 0);
	}

	void iter::close()
	{
		if (valid) o.state.remove(offset);
		valid = false;
	}

	void exec::err()
	{
		std::string msg = state.to<std::string>(-1);
		state.pop(1);
		throw std::runtime_error{msg};
	}

	exec::exec() : state{}, funcs{} { }

	void exec::loadstr(const std::string &prog)
	{
		if (state.dostring(prog) != LUA_OK) err();
	}

	void exec::load(const std::string &prog)
	{
		if (state.loadfile(prog) != LUA_OK) err();
		if (state.pcall(0, 0, 0) != LUA_OK) err();
	}

	bool exec::exists(const std::string &name)
	{
		state.getglobal(name);
		bool ret = ! state.is<std::nullptr_t>();
		state.pop(1);
		return ret;
	}

	iter exec::table_iter(const std::string &name)
	{
		state.getglobal(name);
		return iter{*this};
	}
}

