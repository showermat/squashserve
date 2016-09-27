#include <regex>
#include <functional>
#include <iostream>
#include <vector>
#include <getopt.h>
#include "util/util.h"
#include "zsr.h"
#include "search.h"

void help_exit()
{
	std::cerr << "Usage:\n    mkvol srcdir dest.zsr\n"; // TODO Improve
	exit(1);
}

rsearch::disktree_writer searchwriter{};
std::string encoding{};
std::regex process{};

std::string html_title(const std::string &content, const std::string &def, const std::string &encoding = "", const std::regex &process = std::regex{})
{
	std::regex titlere{"<title>(.*?)</title>", std::regex_constants::ECMAScript | std::regex_constants::icase};
	std::smatch match{};
	if (! std::regex_search(content, match, titlere)) return def;
	std::string title = match[1];
	if (encoding.size())
	{
		try { title = util::conv(title, encoding, "UTF-8"); }
		catch (std::runtime_error &e)
		{
			std::cout << clrln << "Could not convert title for file " << def << "\n";
			return def;
		}
	}
	std::string ret = util::from_htmlent(title);
	if (! std::regex_search(ret, match, process)) return ret;
	return match[1];
}

std::vector<std::string> meta(const zsr::writer::filenode &n)
{
	std::string path = n.path();
	if (util::mimetype(path) != "text/html") return {""};
	std::ostringstream content{};
	content << std::ifstream{path}.rdbuf();
	std::string title = html_title(content.str(), util::basename(path), encoding, process);
	searchwriter.add(title, n.id());
	return {title};
}

std::unordered_map<std::string, std::string> gmeta(const std::string &path)
{
	std::unordered_map<std::string, std::string> ret{};
	std::ostringstream infoss{};
	infoss << std::ifstream{util::pathjoin({path, "_meta", "info.txt"})}.rdbuf();
	std::string info = infoss.str();
	for (const std::string &line : util::strsplit(info, '\n'))
	{
		if (line.size() == 0 || line[0] == '#') continue;
		unsigned int splitloc = line.find(":");
		if (splitloc == line.npos) continue;
		ret[line.substr(0, splitloc)] = line.substr(splitloc + 1);
	}
	return ret;
}

int main(int argc, char **argv)
{
	int arg;
	while ((arg = getopt(argc, argv, "")) > 0) // May come in handy at some point
	{
		switch (arg)
		{
		case '?':
		default:
			help_exit();
			break;
		}
	}
	std::vector<std::string> args = util::argvec(argc - optind, argv + optind);
	if (args.size() < 2) help_exit();
	std::ofstream out{args[1]};
	if (! out) throw std::runtime_error{"Couldn't open output file"};
	zsr::writer archwriter{args[0]};
	std::unordered_map<std::string, std::string> volmeta = gmeta(args[0]);
	if (volmeta.count("encoding")) encoding = volmeta["encoding"];
	if (volmeta.count("title_filter")) process = std::regex{volmeta["title_filter"]};
	archwriter.volume_meta(volmeta);
	archwriter.node_meta({"title"}, meta);
	archwriter.write_header();
	archwriter.write_body();
	std::string searchtmpf{"search.zsr.tmp"};
	std::ofstream searchout{searchtmpf};
	searchwriter.write(searchout);
	searchout.close();
	std::ifstream searchin{searchtmpf};
	archwriter.userdata(searchin);
	archwriter.combine(out);
	util::rm(searchtmpf);
	return 0;
}

