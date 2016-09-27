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
#include "lib/json/json.hpp" // Thanks to github/nlohmann
#include "util/util.h"
#include "util/prefs.h"
#include "zsr.h"
#include "Volume.h"

/* TODO:
 * Include schema in pref file
 * Add preference for which IP addresses from which to accept requests
 * Cleanup on exit!  Install a signal handler to make sure all destructors (esp. prefs, Volume) are called and remove *.tmp files if writing
 * Implement automatic ZSR file creator
 * ZSR files without info.txt (and "home" attr?) should not be included in the list of volumes
 * Make sure that we're forgiving about extra lines and whitespace in info.txt
 *
 * FIXME:
 * Pressing enter in titlebar does not trigger hashchange and bring you back to the last anchor
 * Links dynamically added to pages with JavaScript are not bound by the onclick handler that keeps them in the iframe
 */

class handle_error : public std::runtime_error
{
public:
	handle_error(const std::string &msg) : runtime_error{msg} { }
};

const std::string preffname = "zsrsrv.pref";
const std::string rsrcdname = "resources";
const std::string exedir = util::dirname(util::exepath());
prefs userp{util::pathjoin({exedir, preffname})};
Volmgr volumes{};

void prefsetup(prefs &p)
{
	p.addpref<std::string>("basedir", "Directory for ZSR files", "");
	p.addpref<int>("port", "Server port", 2234);
	p.addpref<int>("results", "Number of search results to display", 20);
	p.addpref<int>("preview", "Search result preview length", 400);
	p.read();
}

std::vector<std::string> docsplit(const std::string &doc, const std::string &delim = "%")
{
	std::vector<std::string> ret{};
	std::vector<std::string> lines = util::strsplit(doc, '\n');
	std::stringstream buf{};
	for (const std::string &line : lines)
	{
		if (line == delim)
		{
			ret.push_back(buf.str());
			buf.str(std::string{});
		}
		else buf << line << "\n";
	}
	ret.push_back(buf.str());
	return ret;
}

std::string token_replace(const std::string &in, const std::unordered_map<std::string, std::string> &tokens)
{
	const std::string percent_token{"#__TOKEN_PERCENT#"};
	std::stringstream ret{};
	std::string str = std::regex_replace(in, std::regex{"%%"}, percent_token); // Hacky!
	std::regex tokenre{"%[A-Z]+"};
	std::string::size_type lastend = 0;
	for (std::sregex_iterator iter{str.begin(), str.end(), tokenre}; iter != std::sregex_iterator{}; iter++)
	{
		std::string tok = util::utf8lower(iter->str().substr(1));
		ret << str.substr(lastend, iter->position() - lastend);
		if (tokens.count(tok)) ret << tokens.at(tok);
		lastend = iter->position() + iter->length();
	}
	ret << str.substr(lastend);
	return std::regex_replace(ret.str(), std::regex{percent_token}, "%"); // Doesn't need to use regex (here or above)
}

http::doc error(const std::string &header, const std::string &body)
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "error.html"})};
	ret.content(token_replace(ret.content(), {{"header", header}, {"message", body}}));
	return ret;
}

http::doc loadcat(const std::string &name)
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "home.html"})};
	std::vector<std::string> sects = docsplit(ret.content());
	std::stringstream buf{};
	std::unordered_set<std::string> volnames = volumes.load(name);
	std::set<std::string> volsort{volnames.begin(), volnames.end()};
	for (const std::string &vol : volsort)
		buf << token_replace(sects[4], volumes.get(vol).tokens());
	ret.content(buf.str());
	return ret;
}

http::doc unloadcat(const std::string &name)
{
	volumes.unload(name);
	return http::doc{};
}

http::doc home()
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "home.html"})};
	std::stringstream buf{};
	std::vector<std::string> sects = docsplit(ret.content());
	if (sects.size() < 8) return error("Resource Error", "Not enough sections in HTML template at html/home.html");
	buf << sects[0] << sects[3] << loadcat("").content() << sects[5];
	for (const std::string &cat : volumes.categories())
	{
		if (volumes.loaded(cat))
		{
			buf << token_replace(sects[2], volumes.tokens(cat));
			buf << sects[3];
			buf << loadcat(cat).content();
		}
		else buf << token_replace(sects[1], volumes.tokens(cat)) << sects[3];
		buf << sects[5] << token_replace(sects[6], volumes.tokens(cat));
	}
	buf << sects[7];
	ret.content(buf.str());
	return ret;
}

http::doc search(Volume &vol, const std::string &query)
{
	/*std::unordered_map<std::string, std::string> tokens = vol.tokens();
	tokens["query"] = query;
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "search.html"})};
	std::vector<std::string> sects = docsplit(ret.content());
	if (sects.size() < 3) return error("Resource Error", "Not enough sections in HTML template at html/search.html");
	std::stringstream buf{};
	buf << token_replace(sects[0], tokens);
	try
	{
		for (const Result &res : vol.search(query, userp.get<int>("results"), userp.get<int>("preview")))
		{
			std::unordered_map<std::string, std::string> qtoks = tokens;
			qtoks["url"] = res.url;
			qtoks["match"] = util::t2s(res.relevance);
			qtoks["title"] = res.title;
			qtoks["preview"] = res.preview;
			buf << token_replace(sects[1], qtoks);
		}
	}
	catch (Volume::error &e) { return error(e.header(), e.body()); }
	buf << token_replace(sects[2], tokens);
	ret.content(buf.str());
	return ret;*/

	std::string match = vol.quicksearch(query);
	if (match != "") return http::redirect(http::mkpath({"view", vol.id(), match}));
	else return http::redirect(http::mkpath({"titles", vol.id(), query}));
}

http::doc complete(Volume &vol, const std::string &query)
{
	constexpr int limit = 40;
	std::unordered_map<std::string, std::string> res = vol.complete(query);
	std::vector<std::string> names{};
	names.reserve(res.size());
	for (const std::pair<const std::string, std::string> &pair : res) names.push_back(pair.first);
	std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) { return a.size() < b.size(); });
	if (names.size() > limit) names.resize(limit);
	nlohmann::json ret{};
	for (const std::string &name : names) ret.push_back({{"title", name}, {"url", "/view/" + vol.id() + "/" + res[name]}});
	ret.push_back(std::unordered_map<std::string, std::string>{{"title", "<b>See all</b>"}, {"url", "/titles/" + vol.id() + "/" + query}});
	return http::doc{"text/plain", ret.dump()};
}

http::doc titles(Volume &vol, const std::string &query)
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "titles.html"})};
	std::stringstream buf{};
	std::vector<std::string> sects = docsplit(ret.content());
	if (sects.size() < 3) return error("Resource Error", "Not enough sections in HTML template at html/titles.html");
	std::unordered_map<std::string, std::string> titletokens = vol.tokens();
	titletokens["query"] = query;
	buf << token_replace(sects[0], titletokens);
	for (const std::pair<const std::string, std::string> &pair : vol.complete(query)) buf << token_replace(sects[1], {{"title", pair.first}, {"url", "/view/" + vol.id() + "/" + pair.second}}); // TODO Sort
	buf << token_replace(sects[2], titletokens);
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
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "view.html"})};
	ret.content(token_replace(ret.content(), vol.tokens(path)));
	return ret;
}

http::doc content(Volume &vol, const std::string &path)
{
	if (! path.size()) return http::redirect(http::mkpath({"content", vol.id(), vol.info("home")}));
	try { return vol.get(path); }
	catch (Volume::error &e) { return error(e.header(), e.body()); }
}

http::doc pref()
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "mainpref.html"})};
	std::vector<std::string> sects = docsplit(ret.content());
	if (sects.size() < 3) return error("Resource Error", "Not enough sections in HTML template at html/mainpref.html");
	std::stringstream buf{};
	buf << sects[0];
	for (const std::string &prefname : userp.list())
	{
		buf << token_replace(sects[1], {{"name", userp.desc(prefname)}, {"key", prefname}, {"value", userp.getstr(prefname)}});
	}
	buf << sects[2];
	ret.content(buf.str());
	return ret;
}

http::doc rsrc(const std::string &path)
{
	if (! path.size()) return error("Bad Request", "Missing path to resource to retrieve");
	try { return http::doc{util::pathjoin({exedir, rsrcdname, path}), {{"Cache-control", "max-age=640000"}}}; }
	catch (std::runtime_error &e) { return error("Not Found", "The requested resource could not be found"); }
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
		for (const std::pair<const std::string, std::string> &kvpair : args)
		{
			try { userp.setstr(kvpair.first, kvpair.second); }
			catch (std::out_of_range &e) { continue; }
		}
		userp.write();
		return http::redirect("/");
	}
	else if (verb == "add")
	{
		if (! args.count("home") || ! args.count("location") || ! args.count("id")) return error("Couldn't create volume", "A required parameter is missing.");
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
		try { Volume::create(srcdir, userp.get<std::string>("basedir"), id, info); }
		catch(std::runtime_error &e) { return error("Couldn't create volume", e.what()); }
		volumes.refresh();
		return http::redirect("/");
	}
	else if (verb == "quit") exit(0);
	/*else if (verb == "debug")
	{
		std::stringstream buf{};
		for (const std::pair<const std::string, std::string> &arg : args) buf << arg.first << " = " << arg.second << "\n";
		return http::doc{"text/plain", buf.str()};
	}*/
	else return error("Bad Request", "Unknown action “" + verb + "”");
}

http::doc urlhandle(const std::string &url, const std::string &querystr)
{
	//std::cout << url << "\n";
	std::vector<std::string> path = util::strsplit(url, '/');
	if (path.size() && path[0] == "") path.erase(path.begin());
	for (std::string &elem : path) elem = util::urldecode(elem);
	std::unordered_map<std::string, std::string> query{};
	if (querystr.size() > 0) for (const std::string &qu : util::strsplit(querystr, '&'))
	{
		std::string::size_type idx = qu.find("=");
		if (idx == qu.npos) query[util::urldecode(qu)] = "";
		else query[util::urldecode(qu.substr(0, idx))] = util::urldecode(qu.substr(idx + 1));
	}
	try
	{
		if (path.size() == 0) return home();
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
			return http::doc{"text/plain", volumes.load_external(path[1])};
		}
		else if (path[0] == "pref") return pref();
		else if (path[0] == "rsrc") return rsrc(util::strjoin(path, '/', 1));
		else if (path[0] == "add") return http::doc{util::pathjoin({exedir, rsrcdname, "html", "add.html"})};
		else if (path[0] == "action")
		{
			if (path.size() < 2) return error("Bad Request", "No action selected");
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
	prefsetup(userp);
	volumes.init(userp.get<std::string>("basedir"));
	http::server{userp.get<int>("port"), urlhandle}.serve();
	return 0;
}

