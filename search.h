#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <unordered_map>
#include <deque>
#include "lib/radix_tree.hpp" // Thanks to github/ytakano
#include "zsr.h"

namespace rsearch
{
	typedef uint32_t treesize;

	typedef uint16_t namesize;

	class disktree_writer
	{
	private:
		radix_tree<std::string, std::set<zsr::filecount>> stree_;
	public:
		disktree_writer() : stree_{} { }
		void add(const std::string &title, zsr::filecount id);
		//void build(zsr::archive &ar);
		void write(std::ostream &out);
	};

	class disktree
	{
	private:
		std::istream &in_;
		std::unordered_map<std::string, zsr::offset> children(); // Expects get pointer to be at beginning of child section
		std::set<zsr::filecount> values(); // Expects get pointer to be at beginning of value section -- call children() first!
		std::set<zsr::filecount> subtree_closure(zsr::offset nodepos);
		void debug_print(zsr::offset off = 0, std::string prefix = "");
	public:
		disktree(std::istream &in) : in_{in} { }
		std::set<zsr::filecount> search(const std::string &query);
	};
}

