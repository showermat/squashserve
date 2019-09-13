#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "util/util.h"
#include "zsr/zsr.h"
#include "zsr/writer.h"
#include "Volume.h"

void help_exit()
{
	std::cerr <<
R"(Usage:
    mkvol [-d] [-l POL] srcdir dest.zsr
Flags:
    -l POL: Handle symbolic links according to policy POL:
        process: Encode symbolic links within the tree as links in the archive (default)
        follow: Always follow symbolic links and archive the destination
        skip: Ignore symbolic links completely
    -d: Enable debug mode: don't delete temporary files
)"; // TODO Improve
	exit(1);
}


int main(int argc, char **argv) try
{
	std::unordered_map<std::string, std::vector<std::string>> flags{};
	std::vector<std::string> args{};
	try
	{
		auto cmdln = util::argmap(argc, argv, "l:d");
		flags = cmdln.first;
		args = cmdln.second;
	}
	catch (std::runtime_error &e)
	{
		std::cerr << "Error: " << e.what() << "\n";
		help_exit();
	}
	if (args.size() < 2) help_exit();
	bool debug = false;
	if (flags.count("d")) debug = true;
	zsr::writer::linkpolicy linkpol = zsr::writer::linkpolicy::process;
	if (flags.count("l"))
	{
		if (std::unordered_set<std::string>{"process", "proc"}.count(flags["l"][0])) linkpol = zsr::writer::linkpolicy::process;
		else if (std::unordered_set<std::string>{"skip", "ignore", "ign"}.count(flags["l"][0])) linkpol = zsr::writer::linkpolicy::skip;
		else if (std::unordered_set<std::string>{"follow", "fol"}.count(flags["l"][0])) linkpol = zsr::writer::linkpolicy::follow;
		else throw std::runtime_error{"Invalid value for flag \"-l\": " + flags["l"][0]};
	}
	std::ofstream out{args[1]};
	if (! out) throw std::runtime_error{"Couldn't open output file"};
	out.exceptions(std::ios_base::badbit);
	Volwriter{args[0], linkpol, debug}.write(out);
	return 0;
}
catch (std::exception &e)
{
	std::cerr << "Error: " << e.what() << "\n";
	return 1;
}
