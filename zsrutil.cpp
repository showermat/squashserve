#include <iostream>
#include <iomanip>
#include "zsr.h"

void help_exit()
{
	std::cerr << "Usage:\n    zsrutil c src dest.zsr\n    zsrutil x src [member]\n    zsrutil i src [member]\n    zsrutil l src [member]"; // TODO Improve
	exit(1);
}

int main(int argc, char **argv)
{
	std::vector<std::string> args = util::argvec(argc, argv);
	if (args.size() < 2) help_exit();
	if (args[1] == "c")
	{
		if (args.size() < 4) help_exit();
		zsr::writer ar{args[2]};
		std::ofstream out{args[3]};
		ar.write(out);
	}
	else if (args[1] == "x")
	{
		if (args.size() < 3) help_exit();
		zsr::archive ar{std::ifstream{args[2]}};
		if (args.size() < 4) ar.extract();
		else ar.extract(args[3]);
	}
	else if (args[1] == "i")
	{
		if (args.size() < 3) help_exit();
		zsr::archive ar{std::ifstream{args[2]}};
		unsigned int maxwidth = 0;
		if (args.size() > 3)
		{
			zsr::iterator n = ar.get(args[3]);
			for (const std::string &key : ar.nodemeta()) if (key.size() > maxwidth) maxwidth = key.size();
			for (const std::string &key : ar.nodemeta()) std::cout << std::setw(maxwidth) << key << ":  " << n.meta(key) << "\n";
			return 0;
		}
		std::cout << "Archive metadata:\n";
		for (const std::pair<const std::string, std::string> &pair : ar.gmeta()) if (pair.first.size() > maxwidth) maxwidth = pair.first.size();
		for (const std::pair<const std::string, std::string> &pair : ar.gmeta()) std::cout << "    " << std::setw(maxwidth) << pair.first << ":  " << pair.second << "\n";
		std::cout << "Node metadata:\n";
		for (const std::string &key : ar.nodemeta()) std::cout << "    " << key << "\n";
	}
	else if (args[1] == "l")
	{
		if (args.size() < 3) help_exit();
		zsr::archive ar{std::ifstream{args[2]}};
		for (const std::pair<const std::string, zsr::filecount> &child : ar.get(args.size() > 3 ? args[3] : "").children())
		{
			std::cout << child.first;
			zsr::node::ntype type = ar.index(child.second).type();
			if (type == zsr::node::ntype::dir) std::cout << "/";
			else if (type == zsr::node::ntype::link) std::cout << " -> " << ar.index(child.second).dest();
			std::cout << "\n";
		}
	}
	else help_exit();
	return 0;
}

