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
#include "nlohmann/json.hpp" // Thanks to github/nlohmann
#include "util/util.h"
#include "util/template.h"
#include "zsr/zsr.h"
#include "Volume.h"
#include "http.h"
#include "prefs.h"

class handle_error : public std::runtime_error
{
public:
	handle_error(const std::string &msg) : runtime_error{msg} { }
};

std::unique_ptr<const zsr::archive> resources{};
Volmgr volumes{};

http::doc resource(const std::string &path, const std::unordered_map<std::string, std::string> &headers = {})
{
	std::ostringstream ret{};
	zsr::node file = resources->get(path); // Not checking if resources is null because it is set at program launch
	ret << file.content().rdbuf();
	return http::doc(file.meta("type"), ret.str(), headers);
}

http::doc error(const std::string &header, const std::string &body)
{
	http::doc ret = resource("html/error.html");
	ret.content(templ::render(ret.content(), {{"header", header}, {"message", body}}));
	return ret;
}

std::string viewbase() //(Volume &vol, const std::string &path)
{
	return prefs::get("toolbar") ? "view" : "content";
}

http::doc loadcat(const std::string &name)
{
	http::doc ret = resource("html/home.html");
	std::vector<std::string> sects = templ::split(ret.content(), 7, "home.html");
	std::stringstream buf{};
	std::unordered_set<std::string> volnames = volumes.load(name);
	std::set<std::string> volsort{volnames.begin(), volnames.end()};
	for (const std::string &vol : volsort)
	{
		std::unordered_map<std::string, std::string> tokens = volumes.get(vol).tokens();
		tokens["viewbase"] = "/" + viewbase();
		buf << templ::render(sects[3], tokens);
	}
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
	if (match != "") return http::redirect(http::mkpath({viewbase(), vol.id(), match}));
#ifndef ZSR_USE_XAPIAN
	return http::redirect(http::mkpath({"titles", vol.id(), query}));
#endif
	std::unordered_map<std::string, std::string> tokens = vol.tokens();
	tokens["query"] = query;
	tokens["viewbase"] = "/" + viewbase();
	http::doc ret = resource("html/search.html");
	std::vector<std::string> sects = templ::split(ret.content(), 3, "search.html");
	std::stringstream buf{};
	buf << templ::render(sects[0], tokens);
	try
	{
		for (const Result &res : vol.search(query, prefs::get("results"), prefs::get("preview")))
		{
			std::unordered_map<std::string, std::string> qtoks = tokens;
			qtoks["member"] = res.url;
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
	unsigned int limit = prefs::get("complete");
	std::function<bool(const std::string &, const std::string &)> strlencomp = [](const std::string &a, const std::string &b) { return a.size() < b.size(); };
	std::unordered_map<std::string, std::string> res = vol.complete(query, 0, limit * 2);
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
	for (const std::string &name : names) ret.push_back({{"title", name}, {"url", http::mkpath({viewbase(), vol.id(), res[name]})}});
	ret.push_back(std::unordered_map<std::string, std::string>{{"title", "<b>See all</b>"}, {"url", "/titles/" + vol.id() + "/" + query}});
	return http::doc{"text/plain", ret.dump()};
}

http::doc titles(Volume &vol, const std::string &query, int page = 1)
{
	int pagelen = prefs::get("pagesize");
	int npages = (pagelen == 0 ? 1 : vol.completions(query) / pagelen + 1);
	http::doc ret = resource("html/titles.html");
	std::stringstream buf{};
	std::vector<std::string> sects = templ::split(ret.content(), 3, "titles.html");
	std::unordered_map<std::string, std::string> titletokens = vol.tokens();
	titletokens["query"] = query;
	titletokens["queryesc"] = util::urlencode(query);
	titletokens["viewbase"] = "/" + viewbase();
	titletokens["pages"] = util::t2s(npages);
	titletokens["page"] = util::t2s(page);
	buf << templ::render(sects[0], titletokens);
	for (const std::pair<const std::string, std::string> &pair : vol.complete(query, (page - 1) * pagelen, pagelen)) buf << templ::render(sects[1], {{"title", pair.first}, {"url", http::mkpath({viewbase(), vol.id(), pair.second})}});
	buf << templ::render(sects[2], titletokens);
	ret.content(buf.str());
	return ret;
}

http::doc shuffle(Volume &vol)
{
	return http::redirect(http::mkpath({viewbase(), vol.id(), vol.shuffle()}));
}

http::doc view(Volume &vol, const std::string &path, const std::unordered_map<std::string, std::string> &query)
{
	/* Using an iframe like this requires hacky JavaScript link rewriters to make things as transparent as possible, which causes
	 * issues including the following.  However, it's the best way of doing a toolbar that I've been able to come up with so far.
	 *   - Links dynamically added to pages with JavaScript are not bound by the onclick handler that keeps them in the iframe
	 *   - The browser doesn't remember your position on the page if you e.g. go back
	 */
	if (! path.size()) return http::redirect(http::mkpath({"view", vol.id(), vol.info("home")}));
	http::doc ret = resource("html/view.html");
	std::unordered_map<std::string, std::string> tokens = vol.tokens(path);
	tokens["query"] = util::mkquerystr(query);
	ret.content(templ::render(ret.content(), tokens));
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
		catch (std::runtime_error &e) { return error("Volume Creation Failed", e.what()); }
		volumes.refresh();
		return http::redirect("/");
	}
	else if (verb == "quit") std::exit(0);
	else return error("Bad Request", "Unknown action “" + verb + "”");
}

std::string url_input(const std::string &url, int start)
{
	std::string::const_iterator startpos = util::find_nth(url.begin(), url.end(), '/', start + 1) + 1;
	if (startpos > url.end()) return std::string{};
	return util::urldecode(std::string{startpos, url.end()});
}

http::doc urlhandle(const std::string &url, const std::unordered_map<std::string, std::string> &query, uint32_t remoteip)
{
	const uint32_t localip = util::str2ip("127.0.0.1");
	std::vector<std::string> path = util::strsplit(url, '/');
	if (path.size() && path[0] == "") path.erase(path.begin());
	if (path.size() && path[path.size() - 1] == "") path.erase(path.end());
	for (std::string &elem : path) elem = util::urldecode(elem);
	try
	{
		if (path.size() == 0 || path[0] == "") return home(remoteip == localip);
		if (path[0] == "search" || path[0] == "view" || path[0] == "content" || path[0] == "complete" || path[0] == "titles" || path[0] == "shuffle")
		{
			if (path.size() < 2) return error("Bad Request", "Missing volume ID");
			if (! volumes.check(path[1])) return error("Not Found", "No volume with ID “" + path[1] + "” exists");
			if (path[0] == "content") return content(volumes.get(path[1]), url_input(url, 2));
			if (path[0] == "view") return view(volumes.get(path[1]), url_input(url, 2), query);
			if (path[0] == "complete")
			{
				if (path.size() > 3) return error("Bad Request", "Trailing path elements");
				return complete(volumes.get(path[1]), path[2]);
			}
			if (path[0] == "titles")
			{
				if (path.size() > 4) return error("Bad Request", "Trailing path elements");
				return titles(volumes.get(path[1]), path[2], path.size() > 3 ? util::s2t<int>(path[3]) : 1);
			}
			if (path[0] == "shuffle")
			{
				if (path.size() > 2) return error("Bad Request", "Trailing path elements");
				return shuffle(volumes.get(path[1]));
			}
			if (path[0] == "search")
			{
				if (path.size() > 3) return error("Bad Request", "Trailing path elements");
				return search(volumes.get(path[1]), path[2]);
			}
		}
		if (path[0] == "load" || path[0] == "unload")
		{
			if (path.size() < 2) return error("Bad Request", "Missing category name");
			if (path.size() > 2) return error("Bad Request", "Trailing path elements");
			if (path[0] == "load") return loadcat(path[1]);
			else return unloadcat(path[1]);
		}
		if (path[0] == "external")
		{
			if (path.size() < 2) return error("Bad Request", "Missing external path");
			if (path.size() > 2) return error("Bad Request", "Trailing path elements");
			if (remoteip != localip) return error("Denied", "You do not have permission to access this functionality");
			return http::doc{"text/plain", volumes.load_external(path[1])};
		}
		if (path[0] == "rsrc") return rsrc(url_input(url, 1));
		if (path[0] == "action")
		{
			if (path.size() < 2) return error("Bad Request", "No action selected");
			if (path.size() > 2) return error("Bad Request", "Trailing path elements");
			if (remoteip != localip) return error("Denied", "You do not have permission to access this functionality");
			return action(path[1], query);
		}
		if (path[0] == "pref" || path[0] == "add")
		{
			if (path.size() > 1) return error("Bad Request", "Trailing path elements");
			if (path[0] == "pref") return pref();
			if (path[0] == "add") return http::doc{resource("html/add.html")};
		}
		throw handle_error{"Unknown action “" + path[0] + "”"};
	}
	catch (std::exception &e)
	{
		return error("Error", e.what());
	}
}

int main(int argc, char **argv) try
{
	prefs::init("zsrsrv");
	std::string rsrcpath = prefs::get("resources");
	if (rsrcpath == "") rsrcpath = util::pathjoin({util::dirname(util::exepath()), "resources.zsr"});
	if (! util::fexists(rsrcpath)) throw std::runtime_error{"Can't find resource archive at " + rsrcpath + "\n"};
	resources.reset(new zsr::archive{rsrcpath});
	volumes.init(prefs::get("basedir"));
	http::server{prefs::get("localonly") ? "127.0.0.1" : "0.0.0.0", static_cast<uint16_t>(prefs::get("port")), urlhandle, prefs::get("accept")}.serve();
	return 0;
}
catch (std::exception &e)
{
	std::cerr << "Error: " << e.what() << "\n";
	return 1;
}
