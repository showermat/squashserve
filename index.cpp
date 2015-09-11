#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <xapian.h>
#include "util.h"

const std::string metadir = "_meta";
const std::string language = "english";

int main(int argc, char **argv)
{
	std::vector<std::string> args = util::argvec(argc, argv);
	if (args.size() < 2)
	{
		std::cerr << "Missing required argument: root of file tree to index\n";
		return 1;
	}
	std::string root = args[1];
	Xapian::WritableDatabase db{util::pathjoin({root, metadir, "index"}), Xapian::DB_CREATE_OR_OPEN};
	Xapian::TermGenerator termg{};
	termg.set_stemmer(Xapian::Stem{language});
	termg.set_stemming_strategy(Xapian::TermGenerator::STEM_SOME);
	termg.set_database(db);
	int i = 0;
	for (const std::string &path : util::recursive_ls(root))
	{
		std::string relpath = path.substr(root.size());
		std::string type = util::mimetype(path);
		type = type.substr(0, type.find("/"));
		if (type != "text" || relpath.substr(0, 6) == metadir + "/") continue;
		Xapian::Document doc{};
		doc.set_data(relpath);
		termg.set_document(doc);
		std::ifstream in{path};
		if (! in) throw std::runtime_error{"Couldn't open " + path + " for reading"};
		std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}}; // FIXME Possibly slow?
		termg.index_text_without_positions(content);
		db.add_document(doc);
		std::cout << "\r\033[K" << i++ << "  " << relpath << std::flush;
	}
	std::cout << "\n";
	return 0;
}
