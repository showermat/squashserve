#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <stdexcept>
#include <xapian.h>
#include "util.h"

std::string html_title(const std::string &content, const std::string &def, const std::regex &process)
{
	std::regex titlere{"<title>(.*?)</title>"};
	std::smatch match{};
	if (! std::regex_search(content, match, titlere)) return def;
	std::string ret = match[1];
	if (! std::regex_search(ret, match, process)) return ret;
	return match[1];
}

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
	std::regex process{};
	if (args.size() >= 3) process = std::regex{args[2]};
	std::string root = args[1];
	Xapian::WritableDatabase db{util::pathjoin({root, metadir, "index"}), Xapian::DB_CREATE_OR_OVERWRITE};
	Xapian::TermGenerator termg{};
	termg.set_stemmer(Xapian::Stem{language});
	termg.set_stemming_strategy(Xapian::TermGenerator::STEM_SOME);
	termg.set_database(db);
	int i = 0;
	for (const std::string &path : util::recursive_ls(root))
	{
		std::string relpath = path.substr(root.size());
		std::string type = util::mimetype(path);
		if (type.substr(0, type.find("/")) != "text" || relpath.substr(0, 6) == metadir + "/") continue;
		std::cout << "\r\033[K" << i++ << "  " << relpath << std::flush;
		std::ifstream in{path};
		if (! in) throw std::runtime_error{"Couldn't open " + path + " for reading"};
		std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}}; // FIXME Possibly slow?
		std::string title = util::basename(path);
		if (type == "text/html") title = html_title(content, title, process);
		//std::cout << title << "\n";
		Xapian::Document doc{};
		doc.set_data(relpath);
		doc.add_value(1, title);
		termg.set_document(doc);
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
