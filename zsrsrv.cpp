#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <regex>
#include <sstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <cstdlib>
#include "lib/json/json.hpp" // Thanks to github/nlohmann
#include "util/util.h"
#include "util/template.h"
#include "zsr.h"
#include "Volume.h"
#include "http.h"
#include "prefs.h"

/* TODO:
 * ...
 *
 * Frame issues:
 *     Pressing enter in titlebar does not trigger hashchange and bring you back to the last anchor
 *     Links dynamically added to pages with JavaScript are not bound by the onclick handler that keeps them in the iframe
 *     Browser doesn't remember your position on the page if you e.g. go back
 * Threadsafe libzsr
 */

class handle_error : public std::runtime_error
{
public:
	handle_error(const std::string &msg) : runtime_error{msg} { }
};

std::unique_ptr<zsr::archive> resources{};
Volmgr volumes{};

http::doc resource(const std::string &path, const std::unordered_map<std::string, std::string> &headers = {})
{
	std::ostringstream ret{};
	ret << resources->get(path).open(); // Not checking if resources is null because it is set at program launch
	resources->close(path);
	return http::doc(util::mimetype(path, ret.str()), ret.str(), headers);
}

http::doc error(const std::string &header, const std::string &body)
{
	http::doc ret = resource("html/error.html");
	ret.content(templ::render(ret.content(), {{"header", header}, {"message", body}}));
	return ret;
}

http::doc loadcat(const std::string &name)
{
	http::doc ret = resource("html/home.html");
	std::vector<std::string> sects = templ::split(ret.content(), 7, "home.html");
	std::stringstream buf{};
	std::unordered_set<std::string> volnames = volumes.load(name);
	std::set<std::string> volsort{volnames.begin(), volnames.end()};
	for (const std::string &vol : volsort)
		buf << templ::render(sects[3], volumes.get(vol).tokens());
	ret.content(buf.str());
	return ret;
}

http::doc unloadcat(const std::string &name)
{
	volumes.unload(name);
	return http::doc{};
}

http::doc home(bool privileged)
{
	http::doc ret = resource("html/home.html");
	std::stringstream buf{};
	std::vector<std::string> sects = templ::split(ret.content(), 7, "home.html");
	std::unordered_map<std::string, std::string> doctokens{};
	if (privileged) doctokens["priv"] = "";
	buf << templ::render(sects[0], doctokens) << sects[2] << loadcat("").content() << sects[4];
	for (const std::string &cat : volumes.categories())
	{
		std::unordered_map<std::string, std::string> curtokens = volumes.tokens(cat);
		if (volumes.loaded(cat)) curtokens["loaded"] = "";
 		buf << templ::render(sects[1], curtokens) << sects[2];
		if (volumes.loaded(cat)) buf << loadcat(cat).content();
		buf << sects[4] << templ::render(sects[5], curtokens);
	}
	buf << sects[6];
	ret.content(buf.str());
	return ret;
}

http::doc search(Volume &vol, const std::string &query)
{
	std::string match = vol.quicksearch(query);
	if (match != "") return http::redirect(http::mkpath({"view", vol.id(), match}));
#ifndef ZSR_USE_XAPIAN
	return http::redirect(http::mkpath({"titles", vol.id(), query}));
#endif
	std::unordered_map<std::string, std::string> tokens = vol.tokens();
	tokens["query"] = query;
	http::doc ret = resource("html/search.html");
	std::vector<std::string> sects = templ::split(ret.content(), 3, "search.html");
	std::stringstream buf{};
	buf << templ::render(sects[0], tokens);
	try
	{
		for (const Result &res : vol.search(query, prefs::get("results"), prefs::get("preview")))
		{
			std::unordered_map<std::string, std::string> qtoks = tokens;
			qtoks["url"] = http::mkpath({"view", vol.id(), res.url});
			qtoks["match"] = util::t2s(res.relevance);
			qtoks["title"] = res.title;
			qtoks["preview"] = res.preview;
			buf << templ::render(sects[1], qtoks);
		}
	}
	catch (Volume::error &e) { return error(e.header(), e.body()); }
	buf << templ::render(sects[2], tokens);
	ret.content(buf.str());
	return ret;
}

http::doc complete(Volume &vol, const std::string &query)
{
	constexpr int limit = 40;
	std::function<bool(const std::string &, const std::string &)> strlencomp = [](const std::string &a, const std::string &b) { return a.size() < b.size(); };
	std::unordered_map<std::string, std::string> res = vol.complete(query, limit * 2);
	std::vector<std::string> names{};
	names.reserve(res.size());
	for (const std::pair<const std::string, std::string> &pair : res) names.push_back(pair.first);
	if (names.size() > limit)
	{
		std::nth_element(names.begin(), names.begin() + limit, names.end(), strlencomp);
		names.resize(limit);
	}
	std::sort(names.begin(), names.end(), strlencomp);
	nlohmann::json ret{};
	for (const std::string &name : names) ret.push_back({{"title", name}, {"url", "/view/" + vol.id() + "/" + res[name]}});
	ret.push_back(std::unordered_map<std::string, std::string>{{"title", "<b>See all</b>"}, {"url", "/titles/" + vol.id() + "/" + query}});
	return http::doc{"text/plain", ret.dump()};
}

http::doc titles(Volume &vol, const std::string &query)
{
	http::doc ret = resource("html/titles.html");
	std::stringstream buf{};
	std::vector<std::string> sects = templ::split(ret.content(), 3, "titles.html");
	std::unordered_map<std::string, std::string> titletokens = vol.tokens();
	titletokens["query"] = query;
	buf << templ::render(sects[0], titletokens);
	for (const std::pair<const std::string, std::string> &pair : vol.complete(query)) buf << templ::render(sects[1], {{"title", pair.first}, {"url", "/view/" + vol.id() + "/" + pair.second}}); // Is sorting worth the time consumption?
	buf << templ::render(sects[2], titletokens);
	ret.content(buf.str());
	return ret;
}

http::doc shuffle(Volume &vol)
{
	return http::redirect(http::mkpath({"view", vol.id(), vol.shuffle()}));
}

http::doc view(Volume &vol, const std::string &path)
{
	if (! path.size()) return http::redirect(http::mkpath({"view", vol.id(), vol.info("home")}));
	http::doc ret = resource("html/view.html");
	ret.content(templ::render(ret.content(), vol.tokens(path)));
	return ret;
}

http::doc content(Volume &vol, const std::string &path)
{
	if (! path.size()) return http::redirect(http::mkpath({"content", vol.id(), vol.info("home")}));
	try
	{
		std::pair<std::string, std::string> contpair = vol.get(path);
		return http::doc{contpair.first, contpair.second};
	}
	catch (Volume::error &e) { return error(e.header(), e.body()); }
}

http::doc pref()
{
	http::doc ret = resource("html/pref.html");
	std::vector<std::string> sects = templ::split(ret.content(), 3, "pref.html");
	std::stringstream buf{};
	buf << sects[0];
	for (const std::string &prefname : prefs::list())
	{
		buf << templ::render(sects[1], {{"name", prefs::desc(prefname)}, {"key", prefname}, {"value", prefs::getstr(prefname)}, {"type", prefs::type(prefname)}});
	}
	buf << sects[2];
	ret.content(buf.str());
	return ret;
}

http::doc rsrc(const std::string &path)
{
	if (! path.size()) return error("Bad Request", "Missing path to resource to retrieve");
	try { return resource(path, {{"Cache-control", "max-age=640000"}}); }
	catch (std::runtime_error &e) { return error("Not Found", "The requested resource " + path + " could not be found"); }
}

http::doc action(const std::string &verb, const std::unordered_map<std::string, std::string> &args)
{
	if (verb == "refresh")
	{
		volumes.refresh();
		return http::redirect("/");
	}
	else if (verb == "pref")
	{
		std::string old_basedir = prefs::get("basedir");
		for (const std::pair<const std::string, std::string> &kvpair : args)
		{
			try { prefs::set(kvpair.first, kvpair.second); }
			catch (std::out_of_range &e) { continue; }
		}
		prefs::write();
		if (prefs::get("basedir") != old_basedir) volumes.init(prefs::get("basedir"));
		return http::redirect("/");
	}
	else if (verb == "add")
	{
		for (const std::string & reqarg : {"home", "location", "id"}) if (! args.count(reqarg)) return error("Volume Creation Failed", "Required parameter “" + reqarg + "” is missing.");
		std::string srcdir = args.at("location");
		std::string id = args.at("id");
		std::unordered_map<std::string, std::string> info;
		info["home"] = args.at("home");
		for (const std::string &property : {"title", "favicon"}) if (args.count(property)) info[property] = args.at(property);
		std::unordered_set<std::string> keynames{};
		for (const std::pair<const std::string, std::string> &pair : args)
		{
			std::smatch match{};
			if (std::regex_search(pair.first, match, std::regex{"^key_(.*)$"})) keynames.insert(match[1]);
		}
		for (const std::string &key : keynames) if (args.count("key_" + key) && args.count("value_" + key)) info[args.at("key_" + key)] = args.at("value_" + key);
		try { Volume::create(srcdir, prefs::get("basedir"), id, info); }
		catch(std::runtime_error &e) { return error("Volume Creation Failed", e.what()); }
		volumes.refresh();
		return http::redirect("/");
	}
	else if (verb == "quit") std::exit(0);
	/*else if (verb == "debug")
	{
		std::stringstream buf{};
		for (const std::pair<const std::string, std::string> &arg : args) buf << arg.first << " = " << arg.second << "\n";
		return http::doc{"text/plain", buf.str()};
	}*/
	else return error("Bad Request", "Unknown action “" + verb + "”");
}

http::doc urlhandle(const std::string &url, const std::string &querystr, const uint32_t remoteip)
{
	//std::cout << url << "\n";
	const uint32_t localip = util::str2ip("127.0.0.1"); // TODO Make this configurable
	std::vector<std::string> path = util::strsplit(url, '/');
	if (path.size() && path[0] == "") path.erase(path.begin());
	for (std::string &elem : path) elem = util::urldecode(elem);
	std::unordered_map<std::string, std::string> query{};
	if (querystr.size() > 0) for (const std::string &qu : util::strsplit(querystr, '&'))
	{
		std::string::size_type idx = qu.find("=");
		if (idx == qu.npos) query[util::urldecode(qu)] = "";
		else query[util::urldecode(qu.substr(0, idx), true)] = util::urldecode(qu.substr(idx + 1), true);
	}
	try
	{
		if (path.size() == 0 || path[0] == "") return home(remoteip == localip);
		if (path[0] == "search" || path[0] == "view" || path[0] == "content" || path[0] == "complete" || path[0] == "titles" || path[0] == "shuffle")
		{
			if (path.size() < 2) return error("Bad Request", "Missing volume ID");
			if (! volumes.check(path[1])) return error("Not Found", "No volume with ID “" + path[1] + "” exists");
			std::string input{};
			std::string::const_iterator start = util::find_nth(url.begin(), url.end(), '/', 3) + 1;
			if (start > url.end()) input = "";
			else input = util::urldecode(std::string{start, url.end()});
			std::string input_qstr = input + (querystr.size() ? "?" + querystr : "");
			if (path[0] == "search") return search(volumes.get(path[1]), input_qstr);
			else if (path[0] == "view") return view(volumes.get(path[1]), input_qstr);
			else if (path[0] == "complete") return complete(volumes.get(path[1]), input_qstr);
			else if (path[0] == "titles") return titles(volumes.get(path[1]), input_qstr);
			else if (path[0] == "shuffle") return shuffle(volumes.get(path[1]));
			else return content(volumes.get(path[1]), input);
		}
		else if (path[0] == "load" || path[0] == "unload")
		{
			if (path.size() < 2) return error("Bad Request", "Missing category name");
			if (path[0] == "load") return loadcat(path[1]);
			else return unloadcat(path[1]);
		}
		else if (path[0] == "external")
		{
			if (path.size() < 2) return error("Bad Request", "Missing external path");
			if (remoteip != localip) return error("Denied", "You do not have permission to access this functionality");
			return http::doc{"text/plain", volumes.load_external(path[1])};
		}
		else if (path[0] == "pref") return pref();
		else if (path[0] == "rsrc") return rsrc(util::strjoin(path, '/', 1));
		else if (path[0] == "add") return http::doc{resource("html/add.html")};
		else if (path[0] == "action")
		{
			if (path.size() < 2) return error("Bad Request", "No action selected");
			if (remoteip != localip) return error("Denied", "You do not have permission to access this functionality");
			return action(path[1], query);
		}
		else throw handle_error{"Unknown action “" + path[0] + "”"};
	}
	catch (std::exception &e)
	{
		return error("Error", e.what());
	}
}

int main(int argc, char **argv)
{
	prefs::init();
	std::string rsrcpath = prefs::get("resources");
	if (rsrcpath == "") rsrcpath = util::pathjoin({util::dirname(util::exepath()), "resources.zsr"});
	std::ifstream rsrcfile{rsrcpath};
	if (! rsrcfile) throw std::runtime_error{"Couldn't open resource archive at " + rsrcpath + "\n"};
	resources.reset(new zsr::archive{std::move(rsrcfile)});
	volumes.init(prefs::get("basedir"));
	http::server{prefs::get("localonly") ? "127.0.0.1" : "0.0.0.0", static_cast<uint16_t>(prefs::get("port")), urlhandle, prefs::get("accept")}.serve();
	return 0;
}

