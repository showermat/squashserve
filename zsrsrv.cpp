#include <iostream>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <future>
#include <stdexcept>
#include "util.h"
#include "prefs.h"
#include "zsr.h"
#include "Volume.h"

/* TODO:
 * Cleanup on exit!  Install a handler to make sure all destructors (esp. prefs, Volume) are called
 * Make compress/extract multithreaded
 * Support skipping directly to exact matches of a search term
 *
 * FIXME:
 * Pressing enter in titlebar does not trigger hashchange and bring you back to the last anchor
 * Links dynamically added to pages with JavaScript are not bound by the onclick handler that keeps them in the iframe
 * Can we instruct the client to cache the toolbar images so it doesn't reload them every time?
 */

class handle_error : public std::runtime_error
{
public:
	handle_error(const std::string &msg) : runtime_error{msg} { }
};

std::string preffname = "zsrsrv.pref";
std::string rsrcdname = "resources";
std::string exedir = util::dirname(util::exepath());
prefs userp{util::pathjoin({exedir, preffname})};
std::map<std::string, Volume> volumes{};

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

std::string token_replace(const std::string &in, const std::map<std::string, std::string> &tokens)
{
	const std::string percent_token{"#__TOKEN_PERCENT#"};
	std::stringstream ret{};
	std::string str = std::regex_replace(in, std::regex{"%%"}, percent_token); // Hacky!
	std::regex tokenre{"%[A-Z]+"};
	std::string::size_type lastend = 0;
	for (std::sregex_iterator iter{str.begin(), str.end(), tokenre}; iter != std::sregex_iterator{}; iter++)
	{
		std::string tok = util::asciilower(iter->str().substr(1));
		ret << str.substr(lastend, iter->position() - lastend);
		if (tokens.count(tok)) ret << tokens.at(tok);
		else ret << iter->str();
		lastend = iter->position() + iter->length();
	}
	ret << str.substr(lastend);
	return std::regex_replace(ret.str(), std::regex{percent_token}, "%"); // Doesn't need to use regex (here or above)
}

http::doc error(const std::string &header, const std::string &body)
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "error.html"})};
	ret.content = token_replace(ret.content, {{"header", header}, {"message", body}});
	return ret;
}

http::doc home(std::map<std::string, Volume> &vols)
{
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "home.html"})};
	std::stringstream buf{};
	std::vector<std::string> sects = docsplit(ret.content);
	if (sects.size() < 3) return error("Resource Error", "Not enough sections in HTML template at html/home.html");
	buf << sects[0];
	for (std::pair<const std::string, Volume> &vol : vols) buf << token_replace(sects[1], vol.second.tokens({})); // TODO Sort volume list by title
	buf << sects[2];
	ret.content = buf.str();
	return ret;
}

http::doc search(Volume &vol, const std::string &query)
{
	std::map<std::string, std::string> tokens = vol.tokens({});
	tokens["query"] = query;
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "search.html"})};
	std::vector<std::string> sects = docsplit(ret.content);
	if (sects.size() < 3) return error("Resource Error", "Not enough sections in HTML template at html/search.html");
	std::stringstream buf{};
	buf << token_replace(sects[0], tokens);
	try
	{
		for (const Result &res : vol.search(query, userp.get<int>("results"), userp.get<int>("preview")))
		{
			std::map<std::string, std::string> qtoks = tokens;
			qtoks["url"] = res.url;
			qtoks["match"] = util::t2s(res.relevance);
			qtoks["title"] = res.title;
			qtoks["preview"] = res.preview;
			buf << token_replace(sects[1], qtoks);
		}
	}
	catch (Xapian::InvalidArgumentError &e) { return error("Search Failed", "This volume is not indexed, so it cannot be searched"); }
	catch (Xapian::Error &e) { return error("Search Failed", e.get_msg()); }
	buf << token_replace(sects[2], tokens);
	ret.content = buf.str();
	return ret;
}

http::doc view(Volume &vol, const std::string &path)
{
	if (! path.size()) return http::redirect(http::mkpath({"view", vol.id(), vol.info("home")}));
	http::doc ret{util::pathjoin({exedir, rsrcdname, "html", "view.html"})};
	ret.content = token_replace(ret.content, vol.tokens(path));
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
	std::vector<std::string> sects = docsplit(ret.content);
	if (sects.size() < 3) return error("Resource Error", "Not enough sections in HTML template at html/mainpref.html");
	std::stringstream buf{};
	buf << sects[0];
	for (const std::string &prefname : userp.list())
	{
		buf << token_replace(sects[1], {{"name", userp.desc(prefname)}, {"key", prefname}, {"value", userp.getstr(prefname)}});
	}
	buf << sects[2];
	ret.content = buf.str();
	return ret;
}

http::doc rsrc(const std::string &path)
{
	if (! path.size()) return error("Bad Request", "Missing path to resource to retrieve");
	try { return http::doc{util::pathjoin({exedir, rsrcdname, path})}; }
	catch (std::runtime_error &e) { return error("Not Found", "The resource you requested could not be found"); }
}

http::doc add()
{
	return http::doc{util::pathjoin({exedir, rsrcdname, "html", "add.html"})};
}

std::map<std::string, Volume> buildlist()
{
	std::map<std::string, Volume> ret{};
	std::set<std::string> contents{};
	const std::string basedir = userp.get<std::string>("basedir");
	try { contents = util::ls(basedir, "\\.zsr$"); }
	catch (std::runtime_error &e) { return ret; }
	std::vector<std::future<Volume>> loadthreads{};
	for (const std::string &fname : contents)
	{
		loadthreads.push_back(std::async(std::launch::async, Volume::newvol, util::pathjoin({basedir, fname})));
	}
	for (std::future<Volume> &f : loadthreads)
	{
		try
		{
			Volume v = f.get();
			std::cout << v.id() << "\n";
			ret.insert(std::make_pair(v.id(), std::move(v)));
		}
		catch (zsr::badzsr &e)
		{
			std::cout << "FILE_NAME_HERE" << ": " << e.what() << "\n"; // TODO
		}
	}
	std::cout << "Done loading volumes\n";
	return ret;
}

http::doc action(const std::string &verb, const std::map<std::string, std::string> &args)
{
	if (verb == "refresh")
	{
		volumes = buildlist();
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
		return http::redirect("/"); // TODO Implement
	}
	else if (verb == "quit")
	{
		exit(0);
	}
	else if (verb == "debug")
	{
		std::stringstream buf{};
		for (const std::pair<const std::string, std::string> &arg : args) buf << arg.first << "\t" << arg.second << "\n";
		return http::doc{"text/plain", buf.str()};
	}
	else return error("Bad Request", "Unknown action “" + verb + "”");
}

http::doc urlhandle(const std::string &url, const std::string &querystr)
{
	std::vector<std::string> path = util::strsplit(url, '/');
	for (std::string &elem : path) elem = util::urldecode(elem);
	std::map<std::string, std::string> query{};
	if (querystr.size() > 0)
	{
		for (const std::string &qu : util::strsplit(querystr, '&'))
		{
			std::string::size_type idx = qu.find("=");
			if (idx == qu.npos) query[util::urldecode(qu)] = "";
			else query[util::urldecode(qu.substr(0, idx))] = util::urldecode(qu.substr(idx + 1));
		}
	}
	try
	{
		if (path.size() == 0) return home(volumes);
		if (path[0] == "search" || path[0] == "view" || path[0] == "content")
		{
			if (path.size() < 2) return error("Bad Request", "Missing volume ID");
			if (volumes.count(path[1]) == 0) return error("Not Found", "No volume with ID “" + path[1] + "” exists");
			if (path[0] == "search") return search(volumes.at(path[1]), util::strjoin(path, '/', 2));
			else if (path[0] == "view") return view(volumes.at(path[1]), util::strjoin(path, '/', 2) + (querystr.size() ? ("?" + querystr) : ""));
			else return content(volumes.at(path[1]), util::strjoin(path, '/', 2));
		}
		else if (path[0] == "favicon.ico") return http::doc{util::pathjoin({exedir, rsrcdname, "img", "favicon.png"})};
		else if (path[0] == "pref") return pref();
		else if (path[0] == "rsrc") return rsrc(util::strjoin(path, '/', 1));
		else if (path[0] == "add") return add();
		else if (path[0] == "action")
		{
			if (path.size() < 2) return error("Bad Request", "No action selected");
			return action(path[1], query);
		}
		else throw handle_error{"Unknown action “" + path[0] + "”"};
	}
	catch (handle_error &e)
	{
		return error("Error", e.what());
	}
}

int main(int argc, char **argv)
{
	//std::ios_base::sync_with_stdio(false);
	prefsetup(userp);
	volumes = buildlist();
	http::server{userp.get<int>("port"), urlhandle}.serve();
	return 0;
}

