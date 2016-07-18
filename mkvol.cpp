#include <regex>
#include <functional>
#include <iostream>
#include <vector>
#include "util.h"
#include "zsr.h"
#include "search.h"

void help_exit()
{
	std::cerr << "Usage:\n    mkvol srcdir dest.zsr\n"; // TODO Improve
	exit(1);
}

std::string html_title(const std::string &content, const std::string &def, const std::regex &process = std::regex{}) // TODO Allow regex processing of title with last argument
{
	std::regex titlere{"<title>(.*?)</title>"};
	std::smatch match{};
	if (! std::regex_search(content, match, titlere)) return def;
	std::string ret = util::from_htmlent(match[1]);
	if (! std::regex_search(ret, match, process)) return ret;
	return match[1];
}

rsearch::disktree_writer searchwriter{};

std::vector<std::string> meta(const zsr::writer::filenode &n)
{
	std::string path = n.path();
	if (util::mimetype(path) != "text/html") return {""};
	std::ostringstream content{};
	content << std::ifstream{path}.rdbuf();
	std::string title = html_title(content.str(), util::basename(path));
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
	std::vector<std::string> args = util::argvec(argc, argv);
	if (args.size() < 3) help_exit();
	std::ofstream out{args[2]};
	zsr::writer archwriter{args[1]};
	archwriter.volume_meta(gmeta(args[1]));
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

