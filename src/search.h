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
#include "zsr/zsr.h"

namespace rsearch
{
	typedef uint32_t treesize;

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
		const char *base_;
		std::unordered_map<std::string, zsr::offset> children(const char *&ptr); // Expects pointer to be at beginning of child section
		std::unordered_set<zsr::filecount> values(const char *&ptr); // Expects pointer to be at beginning of value section -- call children() first!
		std::unordered_set<zsr::filecount> subtree_closure(zsr::offset nodepos);
		zsr::offset nodefind(const std::string &query);
		void debug_print(zsr::offset off = 0, std::string prefix = "");
	public:
		disktree() : base_{nullptr} { }
		disktree(const std::string_view &in) : base_{&in[0]} { }
		void init(const std::string_view &in) { base_ = &in[0]; }
		std::unordered_set<zsr::filecount> search(const std::string &query);
		std::unordered_set<zsr::filecount> exact_search(const std::string &query);
	};
}

#endif

