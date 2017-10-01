#include <iostream>
#include <iomanip>
#include "zsr/zsr.h"
#include "zsr/writer.h"
#include "util/lua.h"

void help_exit()
{
	std::cerr << "Usage:\n    zsrutil c src dest.zsr [info.lua]\n    zsrutil x src [member]\n    zsrutil i src [member]\n    zsrutil l src [member]\n"; // TODO Improve
	exit(1);
}

int main(int argc, char **argv) try
{
	std::vector<std::string> args = util::argvec(argc, argv);
	if (args.size() < 2) help_exit();
	if (args[1] == "c")
	{
		if (args.size() < 4) help_exit();
		zsr::writer ar{args[2]};
		lua::exec info{};
		if (args.size() > 4)
		{
			info.load(args[4]);
			ar.volume_meta(info.table_iter("params").tomap<std::string, std::string>());
			ar.node_meta(info.table_iter("metanames").tovec<std::string>(), [&info](const zsr::filenode &file) { return info.calltbl("meta", file.path()).tomap<std::string, std::string>(); });
		}
		std::ofstream out{args[3]};
		ar.write(out);
	}
	else if (args[1] == "x")
	{
		if (args.size() < 3) help_exit();
		const zsr::archive ar{args[2]};
		if (args.size() < 4) ar.get("/").extract(".");
		else ar.get(args[3]).extract(".");
	}
	else if (args[1] == "i")
	{
		if (args.size() < 3) help_exit();
		const zsr::archive ar{args[2]};
		unsigned int maxwidth = 0;
		if (args.size() > 3)
		{
			zsr::node n = ar.get(args[3]);
			for (const std::string &key : ar.nodemeta()) if (key.size() > maxwidth) maxwidth = key.size();
			for (const std::string &key : ar.nodemeta()) if (n.meta(key).size()) std::cout << std::setw(maxwidth) << key << ":  " << n.meta(key) << "\n";
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
		const zsr::archive ar{args[2]};
		//for (const std::pair<const std::string, zsr::filecount> &child : ar.get(args.size() > 3 ? args[3] : "").children())
		for (zsr::iterator children = ar.get(args.size() > 3 ? args[3] : "").children(); children; children++)
		{
			zsr::node child = children.get();
			std::cout << child.name();
			zsr::node::ntype type = child.type();
			if (type == zsr::node::ntype::dir) std::cout << "/";
			else if (type == zsr::node::ntype::link) std::cout << " -> " << child.dest();
			std::cout << "\n";
		}
	}
	else help_exit();
	return 0;
}
catch (std::exception &e)
{
	std::cerr << "Error: " << e.what() << "\n";
	return 1;
}
