#include <iostream>
#include <fstream>
#include <string>
#include <regex>
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
	Xapian::WritableDatabase db{util::pathjoin({root, metadir, "index"}), Xapian::DB_CREATE_OR_OVERWRITE};
	Xapian::TermGenerator termg{};
	termg.set_stemmer(Xapian::Stem{language});
	termg.set_stemming_strategy(Xapian::TermGenerator::STEM_SOME);
	termg.set_database(db);
	int i = 1;
	for (const std::string &path : util::recursive_ls(root))
	{
		std::string relpath = path.substr(root.size());
		std::string type = util::mimetype(path);
		if (type.substr(0, type.find("/")) != "text" || relpath.substr(0, 6) == metadir + "/") continue;
		std::cout << "\r\033[K" << i++ << "  " << relpath << std::flush;
		Xapian::Document doc{};
		doc.set_data(relpath);
		termg.set_document(doc);
		std::ifstream in{path};
		if (! in) throw std::runtime_error{"Couldn't open " + path + " for reading"};
		std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}}; // FIXME Possibly slow?
		if (type == "text/html")
		{
			content = std::regex_replace(content, std::regex{"<[^>]{0,2000}>"}, " ");
			content = std::regex_replace(content, std::regex{"&[a-zA-Z]{1,100};"}, " ");
		}
		content = std::regex_replace(content, std::regex{"[^a-zA-Z0-9_\'-]{1,2000}"}, " "); // Only support (most) English for now since Xapian is dumb with languages anyway
		content = std::regex_replace(content, std::regex{"\\b[0-9-]{1,100}\\b"}, " "); // Not perfect -- hyphen-space is not a word boundary
		content = std::regex_replace(content, std::regex{"\\b\\S{1,2}\\b"}, "");
		termg.index_text_without_positions(content);
		db.add_document(doc);
	}
	std::cout << "\n";
	return 0;
}
