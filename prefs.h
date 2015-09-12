#ifndef PREFS_H
#define PREFS_H
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <typeinfo>
#include "util.h"

class prefs
{
private:
	struct base_pref
	{
		std::string name;
		bool set;
		std::string desc;
		virtual void read(std::istream &in) = 0;
		virtual void write(std::ostream &out) const = 0;
		virtual std::string tostr() const = 0;
		virtual void fromstr(const std::string &newval) = 0;
		virtual ~base_pref() { }
	};

	template <typename T> struct pref : public base_pref
	{
		T val, def;
		void read(std::istream &in) { throw std::runtime_error{"No rule to read preferences of type " + std::string{typeid(T).name()}}; }
		void write(std::ostream &out) const { throw std::runtime_error{"No rule to write preferences of type " + std::string{typeid(T).name()}}; }
		std::string tostr() const { return set ? util::t2s(val) : util::t2s(def); }
		void fromstr(const std::string &newval) { val = util::s2t<T>(newval); set = true; }
	};

	std::map<std::string, std::unique_ptr<base_pref>> store;
	std::vector<base_pref *> order;
	std::string source;

	void read(std::istream &in)
	{
		for (base_pref *ptr : order) ptr->read(in);
	}

	void write(std::ostream &out) const
	{
		for (base_pref *ptr : order) ptr->write(out);
	}
public:
	template <typename T> void addpref(const std::string &name, const std::string &desc, const T &def)
	{
		pref<T> *p = new pref<T>{};
		p->name = name;
		p->desc = desc;
		p->def = def;
		p->set = false;
		store[name] = std::unique_ptr<base_pref>{p};
		order.push_back(p);
	}

	template <typename T> void set(const std::string &name, const T &val)
	{
		pref<T> *p = dynamic_cast<pref<T> *>(store.at(name).get());
		p->val = val;
		p->set = true;
	}

	template <typename T> T get(const std::string &name) const
	{
		pref<T> *p = dynamic_cast<pref<T> *>(store.at(name).get());
		if (p->set) return p->val;
		return p->def;
	}

	std::string getstr(const std::string &name) const { return store.at(name)->tostr(); }

	void setstr(const std::string &name, const std::string &val) { store.at(name)->fromstr(val); }

	void unset(const std::string &name) { store.at(name)->set = false; }

	bool is_set(const std::string &name) const { return store.at(name)->set; }

	std::string desc(const std::string &name) const { return store.at(name)->desc; }

	std::vector<std::string> list() const
	{
		std::vector<std::string> ret{};
		ret.reserve(store.size());
		for (base_pref *ptr : order) ret.push_back(ptr->name);
		return ret;
	}

	prefs(const std::string &file) : store{}, order{}, source{file} { }

	void read()
	{
		if (source == "") return;
		std::ifstream in{source};
		if (! in) return;
		read(in);
	}

	void write()
	{
		if (source == "") throw std::runtime_error{"No persistence specified for preferences"};
		std::ofstream out{source};
		if (! out) throw std::runtime_error{"Could not open persistent storage for preferences"};
		write(out);
	}

	~prefs()
	{
		if (source == "") return;
		std::ofstream out{source};
		if (! out) return;
		write(out);
	}
};

template <> void prefs::pref<int>::read(std::istream &in) // This and the next function could apply to any type without pointers, but I don't know enough about metaprogramming to implement it
{
	in.read(reinterpret_cast<char *>(&set), sizeof(set));
	if (! set) return;
	in.read(reinterpret_cast<char *>(&val), sizeof(val));
}

template <> void prefs::pref<int>::write(std::ostream &out) const
{
	out.write(reinterpret_cast<const char *>(&set), sizeof(set));
	if (! set) return;
	out.write(reinterpret_cast<const char *>(&val), sizeof(val));
}

template <> void prefs::pref<std::string>::read(std::istream &in)
{
	in.read(reinterpret_cast<char *>(&set), sizeof(set));
	if (! set) return;
	std::string::size_type len;
	in.read(reinterpret_cast<char *>(&len), sizeof(len));
	val.resize(len);
	in.read(&val[0], len);
}

template <> void prefs::pref<std::string>::write(std::ostream &out) const
{
	out.write(reinterpret_cast<const char *>(&set), sizeof(set));
	if (! set) return;
	std::string::size_type len = val.size();
	out.write(reinterpret_cast<const char *>(&len), sizeof(len));
	out.write(&val[0], len);
}

#endif

