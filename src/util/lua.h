#ifndef UTIL_LUA_H
#define UTIL_LUA_H
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <utility>
#include <functional>
#include "util.h"
#include <lua.hpp>

namespace lua
{
	class luastate
	{
	private:
		lua_State *state;
		bool managed;
	public:
		template <typename T> void push(T t);
		template <typename T> bool is(int i = -1);
		template <typename T> T to(int i = -1);
		template <typename... T> void push_args();
		template <typename H> void push_args(H arg) { push<H>(arg); }
		template <typename H, typename... T> void push_args(H arg, T... args) { push<H>(arg); push_args<T...>(args...); }
		template <typename T> T get_pop(int n = 1)
		{
			T ret = to<T>();
			lua_pop(state, 1);
			return ret;
		}
		luastate(lua_State *s);
		luastate();

		void pop(int n);
		void pushlightuserdata(void *data);
		void pushcclosure(int (*func)(lua_State *), int ndata);
		void setglobal(const std::string &name);
		int getglobal(const std::string &name);
		int getfield(int idx, const std::string &name);
		int next(int idx);
		void remove(int idx);
		int gettop();
		int dostring(const std::string &str);
		int loadfile(const std::string &fname);
		int pcall(int nargs, int nres, int msgh);
		virtual ~luastate();
	};

	class fnwrap_base
	{
	public:
		virtual int internal_call(lua_State *s) { return -1; }
		virtual ~fnwrap_base() { }
	};

	template<typename Ret, typename... Args> class fnwrap : public fnwrap_base
	{
	private:
		std::function<Ret(Args...)> func_;
		template <typename H> std::pair<int, std::tuple<H>> get_args_rec(luastate &s)
		{
			return std::make_pair(1, std::make_tuple(s.to<H>(1)));
		}
		template <typename H, typename M, typename... T> std::pair<int, std::tuple<H, M, T...>> get_args_rec(luastate &s)
		{
			std::pair<int, std::tuple<M, T...>> lower = get_args_rec<M, T...>(s);
			return std::make_pair(lower.first + 1, std::tuple_cat(lower.second, std::make_tuple(s.to<H>(lower.first + 1))));
		}
		std::tuple<Args...> get_args(luastate &s)
		{
			return get_args_rec<Args...>(s).second;
		}
		int internal_call(lua_State *s)
		{
			luastate state{s};
			Ret ret = util::apply(func_, get_args(state));
			state.push<Ret>(ret);
			return 1;
		}
	public:
		static int call(lua_State *s)
		{
			fnwrap_base *rthis = static_cast<fnwrap_base *>(lua_touserdata(s, lua_upvalueindex(1)));
			return rthis->internal_call(s);
		}
		fnwrap(luastate &state, std::function<Ret(Args...)> func, const std::string &name) : func_{func}
		{
			state.pushlightuserdata((void *) this);
			state.pushcclosure(&fnwrap::call, 1);
			state.setglobal(name);
		}
	};

	class iter;
	class exec
	{
	private:
		luastate state;
		void err();
		std::unordered_map<std::string, std::unique_ptr<fnwrap_base>> funcs;
		friend class iter;
	public:
		exec();
		void loadstr(const std::string &prog);
		void load(const std::string &path);
		template <typename Ret, typename... Args> Ret call(const std::string &func, Args... args)
		{
			state.getglobal(func);
			state.push_args(args...);
			if (state.pcall(sizeof...(Args), 1, 0) != LUA_OK) err();
			return state.get_pop<Ret>();
		}
		template <typename... Args> void callv(const std::string &func, Args... args) // FIXME Appropriately handle zero or multiple return values
		{
			state.getglobal(func);
			state.push_args(args...);
			if (state.pcall(sizeof...(Args), 0, 0) != LUA_OK) err();
		}
		template <typename... Args> iter calltbl(const std::string &func, Args... args); // FIXME
		template <typename Ret, typename... Args> void expose(std::function<Ret(Args...)> func, const std::string &name)
		{
			 funcs.emplace(name, std::unique_ptr<fnwrap_base>{new fnwrap<Ret, Args...>{state, func, name}});
		}
		bool exists(const std::string &name);
		template <typename T> T getvar(const std::string &name)
		{
			state.getglobal(name);
			return state.get_pop<T>();
		}
		template <typename T> T getfield(const std::string &table, const std::string &field)
		{
			state.getglobal(table);
			state.getfield(-1, field);
			return state.get_pop<T>(2);
		}
		iter table_iter(const std::string &name);
	};

	class iter
	{
	private:
		exec &o;
		int offset;
		bool valid;
		iter(exec &o);
		friend class exec;
	public:
		iter(const iter &orig) = delete;
		iter(iter &&orig);
		bool next();
		template <typename K, typename V> std::pair<K, V> get()
		{
			V val = o.state.get_pop<V>();
			K key = o.state.to<K>();
			return std::make_pair(key, val);
		}
		template <typename K, typename V> std::unordered_map<K, V> tomap()
		{
			std::unordered_map<K, V> ret{};
			while (next()) ret.insert(get<K, V>());
			close();
			return ret;
		}
		template <typename V> std::vector<V> tovec()
		{
			std::vector<V> ret{};
			while (next()) ret.push_back(get<int, V>().second);
			close();
			return ret;
		}
		void close();
		virtual ~iter() { close(); }
	};

	// C++ sucks.
	template <typename... Args> iter exec::calltbl(const std::string &func, Args... args)
	{
		state.getglobal(func);
		state.push_args(args...);
		if (state.pcall(sizeof...(Args), 1, 0) != LUA_OK) err();
		return iter{*this};
	}
}

#endif

