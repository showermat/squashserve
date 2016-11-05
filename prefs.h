#ifndef ZSR_PREFS_H
#define ZSR_PREFS_H
#include <vector>
#include <map>
#include <fstream>
#include "util/util.h"
#include "lib/json/json.hpp" // Thanks to github/nlohmann

namespace prefs
{
	const nlohmann::json schema = nlohmann::json::parse(R"([
		{
			"name": "basedir",
			"desc": "Directory for ZSR files",
			"default": "."
		}, {
			"name": "port",
			"desc": "Server port",
			"default": 2234
		}, {
			"name": "resources",
			"desc": "ZSR archive of server resources, or “resources.zsr” in the executable directory if empty",
			"default": ""
		}, {
			"name": "accept",
			"desc": "Comma-delimited list of CIDR blocks and IP addresses to accept, or all if empty",
			"default": "127.0.0.1"
		}, {
			"name": "results",
			"desc": "Number of search results to display",
			"default": 20
		}, {
			"name": "preview",
			"desc": "Search result preview length",
			"default": 400
		}
	])"); // TODO Maybe generate this in C++?

	const std::string preffname = "zsrsrv.conf";
	std::vector<std::string> preflocs;
	std::string conffile = "";
	nlohmann::json userp;
	std::map<std::string, nlohmann::basic_json<>> defaults;
	std::vector<std::string> order;

	void init() // TODO Make appname an argument and build preflocs from that
	{
		for (nlohmann::json::const_iterator iter = schema.begin(); iter != schema.end(); iter++)
		{
			order.push_back((*iter)["name"]);
			defaults[(*iter)["name"]] = *iter;
		}

		std::string homedir = util::env_or("HOME", "");
		preflocs = {
			util::env_or("ZSRSRV_CONF", ""),
			util::pathjoin({util::env_or("XDG_CONFIG_HOME", util::pathjoin({homedir, ".config"})), preffname}),
			util::pathjoin({homedir, "." + preffname}),
			util::pathjoin({util::dirname(util::exepath()), preffname})
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
		else if (def.is_number()) userp[pref] = util::s2t<int>(val); // FIXME No float support
		else userp[pref] = val;
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

