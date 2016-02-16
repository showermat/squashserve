#include <regex>
#include <functional>
#include <iostream>
#include "zsr.h"

void help_exit()
{
	std::cerr << "Usage:\n    mkvol src dest"; // TODO Improve
	exit(1);
}

std::string html_title(const std::string &content, const std::string &def, const std::regex &process = std::regex{}) // TODO remove HTML entities
{
	std::regex titlere{"<title>(.*?)</title>"};
	std::smatch match{};
	if (! std::regex_search(content, match, titlere)) return def;
	std::string ret = match[1];
	if (! std::regex_search(ret, match, process)) return ret;
	return match[1];
}

std::unordered_map<std::string, std::string> meta(const std::string &path)
{
	if (util::mimetype(path) != "text/html") return {};
	std::ostringstream content{};
	content << std::ifstream{path}.rdbuf();
	return {{"title", html_title(content.str(), util::basename(path))}};
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
	zsr::archive ar{args[1], gmeta(args[1]), meta};
	std::ofstream out{args[2]};
	ar.write(out);
	return 0;
}

