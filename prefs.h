#ifndef PREFS_H
#define PREFS_H
#include <vector>
#include <fstream>
#include "util/util.h"
#include "lib/json/json.hpp" // Thanks to github/nlohmann

namespace prefs
{
	const nlohmann::json schema = nlohmann::json::parse(R"({
		"order": ["basedir", "port", "accept", "results", "preview"],
		"prefs": {
			"basedir": {
				"desc": "Directory for ZSR files",
				"default": "."
			},
			"port": {
				"desc": "Server port",
				"default": 2234
			},
			"accept": {
				"desc": "Comma-delimited list of CIDR blocks and IP addresses to accept, or all if empty",
				"default": "127.0.0.1"
			},
			"results": {
				"desc": "Number of search results to display",
				"default": 20
			},
			"preview": {
				"desc": "Search result preview length",
				"default": 400
			}
		}
	})");

	std::vector<std::string> preflocs;
	std::string conffile = "";
	nlohmann::json userp;

	void init() // TODO Make appname an argument and build preflocs from that
	{
		std::string homedir = util::env_or("HOME", "");
		preflocs = {
			util::env_or("ZSRSRV_CONF", ""),
			util::pathjoin({util::env_or("XDG_CONFIG_HOME", util::pathjoin({homedir, ".config"})), "zsrsrv.json"}),
			util::pathjoin({homedir, ".zsrsrv.json"}),
			util::pathjoin({util::dirname(util::exepath()), "zsrsrv.json"})
		};
		for (const std::string &loc : preflocs) if (loc != "" && util::fexists(loc) && ! util::isdir(loc))
		{
			conffile = loc;
			std::ifstream{loc} >> userp;
			return;
		}
		userp = nlohmann::json::parse(R"({})");
	}

	void write()
	{
		if (conffile != "") std::ofstream{conffile} << userp;
		else for (const std::string &loc : preflocs) if (loc != "" && util::fexists(loc) && ! util::isdir(loc))
		{
			conffile = loc;
			std::ofstream{loc} << userp.dump(4);
			return;
		}
		else throw std::runtime_error{"Could not write preferences"};
	}

	auto get(const std::string &pref)
	{
		if (userp.count(pref)) return userp[pref];
		return schema["prefs"][pref]["default"];
	}

	std::string getstr(const std::string &pref)
	{
		if (schema["prefs"][pref]["default"].is_string()) return get(pref);
		return util::t2s(get(pref));
	}

	void set(const std::string &pref, const std::string &val)
	{
		auto def = schema["prefs"][pref]["default"]; // Do not support array or object
		if (def.is_boolean()) userp[pref] = util::s2t<bool>(val);
		else if (def.is_number()) userp[pref] = util::s2t<int>(val); // FIXME int vs float
		else userp[pref] = val;
	}

	std::vector<std::string> list()
	{
		std::vector<std::string> ret{};
		for (nlohmann::json::const_iterator iter = schema["order"].begin(); iter != schema["order"].end(); iter++) ret.push_back(iter.value());
		return ret;
	}

	std::string desc(const std::string &pref)
	{
		return schema["prefs"][pref]["desc"];
	}
}

#endif

