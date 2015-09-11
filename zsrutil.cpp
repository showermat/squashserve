#include <iostream>
#include "zsr.h"

void help_exit()
{
	std::cerr << "Usage: zsrutil c|x src dest [member]\n"; // TODO Improve
	exit(1);
}

int main(int argc, char **argv)
{
	std::vector<std::string> args = util::argvec(argc, argv);
	if (args.size() < 2) help_exit();
	if (args[1] == "c")
	{
		if (args.size() < 4) help_exit();
		zsr::archive ar{args[2]};
		std::ofstream out{args[3]};
		ar.write(out);
	}
	else if (args[1] == "x")
	{
		if (args.size() < 4) help_exit();
		zsr::archive ar{std::ifstream{args[2]}};
		if (args.size() < 5) ar.extract(args[3]);
		else ar.extract(args[3], args[4]);
	}
	return 0;
}

