#ifndef ZSR_PREFS_H
#define ZSR_PREFS_H
#include <vector>
#include <map>
#include <fstream>
#include "util/util.h"
#include "fileinclude.h"
#include "../lib/json/json.hpp" // Thanks to github/nlohmann

namespace prefs
{
	const nlohmann::json schema = nlohmann::json::parse(fileinclude::loaded_file("prefs.json"));

	std::vector<std::string> preflocs;
	std::string conffile = "";
	nlohmann::json userp;
	std::map<std::string, nlohmann::basic_json<>> defaults;
	std::vector<std::string> order;

	void init(const std::string &appname)
	{
		for (nlohmann::json::const_iterator iter = schema.begin(); iter != schema.end(); iter++)
		{
			order.push_back((*iter)["name"]);
			defaults[(*iter)["name"]] = *iter;
		}

		const std::string preffname = appname + ".conf";
		std::string homedir = util::env_or("HOME", "");
		preflocs = {
			util::env_or(util::utf8upper(appname) + "_CONF", ""),
			util::pathjoin({util::dirname(util::exepath()), preffname}),
			util::pathjoin({util::env_or("XDG_CONFIG_HOME", util::pathjoin({homedir, ".config"})), preffname}),
			util::pathjoin({homedir, "." + preffname}),
		};
		for (const std::string &loc : preflocs) if (loc != "" && util::fexists(loc) && ! util::isdir(loc))
		{
			conffile = loc;
			std::ifstream in{loc};
			if (! in) throw std::runtime_error{"Couldn't open input file " + loc + " for reading"};
			in.exceptions(std::ios_base::badbit);
			in >> userp;
			return;
		}
		userp = nlohmann::json::parse(R"({})");
	}

	void write()
	{
		if (conffile != "")
		{
			std::ofstream out{conffile};
			if (! out) throw std::runtime_error{"Couldn't open output file " + conffile + " for writing"};
			out.exceptions(std::ios_base::badbit);
			out << userp;
		}
		else for (const std::string &loc : preflocs) if (loc != "" && util::fexists(loc) && ! util::isdir(loc))
		{
			conffile = loc;
			std::ofstream out{loc};
			if (! out) throw std::runtime_error{"Couldn't open output file " + loc + " for writing"};
			out.exceptions(std::ios_base::badbit);
			out << userp.dump(4);
			return;
		}
		else throw std::runtime_error{"Could not write preferences"};
	}

	auto get(const std::string &pref)
	{
		if (userp.count(pref)) return userp[pref];
		return defaults[pref]["default"];
	}

	std::string getstr(const std::string &pref)
	{
		if (defaults[pref]["default"].is_string()) return get(pref);
		return util::t2s(get(pref));
	}

	void set(const std::string &pref, const std::string &val)
	{
		auto def = defaults[pref]["default"]; // Do not support array or object
		if (def.is_boolean()) userp[pref] = util::s2t<bool>(val);
		else if (def.is_number()) userp[pref] = util::s2t<int>(val); // This willl error on floats
		else userp[pref] = val;
	}

	std::string type(const std::string &pref)
	{
		auto def = defaults[pref]["default"];
		if (def.is_string()) return "str";
		if (def.is_number()) return "num";
		if (def.is_boolean()) return "bool";
		if (def.is_object()) return "obj";
		if (def.is_array()) return "arr";
		return "null";
	}

	std::vector<std::string> list()
	{
		std::vector<std::string> ret{};
		for (std::vector<std::string>::const_iterator iter = order.begin(); iter != order.end(); iter++) ret.push_back(*iter);
		return ret;
	}

	std::string desc(const std::string &pref)
	{
		return defaults[pref]["desc"];
	}
}

#endif
