#ifndef ZSR_SEARCH_H
#define ZSR_SEARCH_H
#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <unordered_map>
#include <deque>
#include <locale>
#include "../lib/radix_tree/radix_tree.hpp" // Thanks to github/ytakano
#include "zsr.h"

namespace rsearch
{
	typedef uint32_t treesize;

	typedef uint16_t namesize;

	class disktree_writer
	{
	private:
		radix_tree<std::string, std::unordered_set<zsr::filecount>> stree_;
	public:
		disktree_writer() : stree_{} { }
		void add(const std::string &title, zsr::filecount id);
		//void build(zsr::archive &ar);
		void write(std::ostream &out);
	};

	class disktree
	{
	private:
		std::istream *in_;
		zsr::offset start_;
		std::unordered_map<std::string, zsr::offset> children(); // Expects get pointer to be at beginning of child section
		std::unordered_set<zsr::filecount> values(); // Expects get pointer to be at beginning of value section -- call children() first!
		std::unordered_set<zsr::filecount> subtree_closure(zsr::offset nodepos);
		zsr::offset nodefind(const std::string &query);
		void debug_print(zsr::offset off = 0, std::string prefix = "");
	public:
		disktree() : in_{nullptr}, start_{0} { }
		disktree(std::istream &in, std::streampos start = 0) : in_{&in}, start_{static_cast<zsr::offset>(start)} { }
		void init(std::istream &in, std::streampos start = 0) { in_ = &in; start_ = start; }
		std::unordered_set<zsr::filecount> search(const std::string &query);
		std::unordered_set<zsr::filecount> exact_search(const std::string &query);
	};
}

#endif

