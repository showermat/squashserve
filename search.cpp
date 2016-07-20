#include "search.h"

namespace rsearch
{
	const zsr::offset ptrfill{0};

	void debug_treeprint(radix_tree_node<std::string, std::set<zsr::filecount>> *n, std::string prefix = "") // Debug
	{
		if (! n) return;
		std::cout << prefix << n->m_key << " -> ";
		if (n->m_children.count("") && n->m_children[""]->m_value) for (const zsr::filecount &i : n->m_children[""]->m_value->second) std::cout << i << " ";
		std::cout << "\n";
		for (const std::pair<const std::string, radix_tree_node<std::string, std::set<zsr::filecount>> *> child : n->m_children) if (child.first != "") debug_treeprint(child.second, prefix + "  ");
	}

	void disktree::debug_print(zsr::offset off, std::string prefix) // Debug
	{
		if (! in_) throw std::runtime_error{"Bad stream"};
		in_.seekg(off);
		const std::unordered_map<std::string, zsr::offset> curchild = children();
		const std::set<zsr::filecount> myval = values();
		for (const zsr::filecount &val : myval) std::cout << val << " ";
		std::cout << "\n";
		for (const std::pair<const std::string, zsr::offset> &child : curchild)
		{
			std::cout << prefix << child.first << " -> ";
			debug_print(child.second, prefix + "  ");
		}
	}

	void recursive_treewrite(std::ostream &out, radix_tree_node<std::string, std::set<zsr::filecount>> *n, zsr::offset treestart)
	{
		static int nwritten = 0;
		logb(++nwritten);
		treesize nval = 0;
		if (n->m_children.count("") && n->m_children[""]->m_value) nval = n->m_children[""]->m_value->second.size();
		treesize nchild = 0;
		for (const std::pair<const std::string, radix_tree_node<std::string, std::set<zsr::filecount>> *> child : n->m_children) if (child.first != "" && child.second) nchild++; // TODO Probably a faster way?
		out.write(reinterpret_cast<const char *>(&nchild), sizeof(treesize));
		std::deque<zsr::offset> childpos{};
		for (const std::pair<const std::string, radix_tree_node<std::string, std::set<zsr::filecount>> *> child : n->m_children)
		{
			if (child.first == "" || ! child.second) continue;
			treesize namelen = child.first.size();
			out.write(reinterpret_cast<const char *>(&namelen), sizeof(treesize));
			out.write(&child.first[0], namelen);
			childpos.push_back(static_cast<zsr::offset>(out.tellp()));
			out.write(reinterpret_cast<const char *>(&ptrfill), sizeof(zsr::offset));
		}
		out.write(reinterpret_cast<const char *>(&nval), sizeof(treesize));
		if (n->m_children.count("") && n->m_children[""]->m_value)
			for (const zsr::filecount &i : n->m_children[""]->m_value->second) out.write(reinterpret_cast<const char *>(&i), sizeof(zsr::filecount));
		for (const std::pair<const std::string, radix_tree_node<std::string, std::set<zsr::filecount>> *> child : n->m_children)
		{
			if (child.first == "" || ! child.second) continue;
			zsr::offset childstart = static_cast<zsr::offset>(out.tellp()) - treestart;
			out.seekp(childpos.front());
			out.write(reinterpret_cast<const char *>(&childstart), sizeof(zsr::offset));
			out.seekp(0, std::ios_base::end);
			recursive_treewrite(out, child.second, treestart);
			childpos.pop_front();
		}
	}

	void disktree_writer::write(std::ostream &out)
	{
		loga("Writing search index");
		recursive_treewrite(out, stree_.m_root, static_cast<zsr::offset>(out.tellp()));
	}

	void disktree_writer::add(const std::string &title, zsr::filecount id)
	{
		std::function<bool(char)> alnum = [](char c) { return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122); };
		std::function<bool(char)> space = [](char c) { return c == ' ' || c == '\t' || c == '\n'; };
		if (title.size() == 0) return;
		std::string lctitle = util::asciilower(title); // TODO Generalize to Unicode
		for (std::string::size_type i = 0; i < lctitle.size(); i++)
			if (i == 0 || ((! alnum(lctitle[i - 1]) && alnum(lctitle[i])) || (space(lctitle[i - 1]) && ! space(lctitle[i]))))
				stree_[lctitle.substr(i)].insert(id); // TODO Adapt this for general Unicode
	}

	//void disktree_writer::build(zsr::archive &ar)
	//{
		//for (zsr::iterator n = ar.index(); n; n++) if (! n.isdir()) add(n, n.meta("title"));
	//}

	std::unordered_map<std::string, zsr::offset> disktree::children()
	{
		std::unordered_map<std::string, zsr::offset> ret{};
		treesize nchild;
		in_.read(reinterpret_cast<char *>(&nchild), sizeof(treesize));
		for (treesize i = 0; i < nchild; i++)
		{
			treesize namelen;
			in_.read(reinterpret_cast<char *>(&namelen), sizeof(treesize));
			std::string name{};
			name.resize(namelen);
			in_.read(reinterpret_cast<char *>(&name[0]), namelen);
			zsr::offset loc;
			in_.read(reinterpret_cast<char *>(&loc), sizeof(zsr::offset));
			ret[name] = loc;
		}
		return ret;
	}

	std::set<zsr::filecount> disktree::values()
	{
		std::set<zsr::filecount> ret{};
		treesize nval;
		in_.read(reinterpret_cast<char *>(&nval), sizeof(treesize));
		for (treesize i = 0; i < nval; i++)
		{
			zsr::filecount curval;
			in_.read(reinterpret_cast<char *>(&curval), sizeof(zsr::filecount));
			ret.insert(curval);
		}
		return ret;
	}

	std::set<zsr::filecount> disktree::subtree_closure(zsr::offset nodepos)
	{
		std::set<zsr::filecount> ret{};
		in_.seekg(nodepos);
		if (! in_) throw std::runtime_error{"Tried to seek outside of file"};
		const std::unordered_map<std::string, zsr::offset> curchild = children();
		const std::set<zsr::filecount> myval = values();
		ret.insert(myval.cbegin(), myval.cend());
		for (const std::pair<const std::string, zsr::offset> &child : curchild)
		{
			if (child.second == nodepos) throw std::runtime_error{"Loop detected in search tree"};
			//std::cerr << "  Child " << child.second << "\n";
			const std::set<zsr::filecount> curval = subtree_closure(child.second);
			ret.insert(curval.cbegin(), curval.cend());
		}
		return ret;
	}

	std::set<zsr::filecount> disktree::search(const std::string &query)
	{
		zsr::offset curnode = 0;
		std::string::size_type idx = 0;
		while (idx < query.size())
		{
			in_.seekg(curnode);
			curnode = 0;
			for (const std::pair<const std::string, zsr::offset> &child : children())
			{
				std::string::size_type minlen = std::min(query.size() - idx, child.first.size());
				if (query.substr(idx, minlen) == child.first.substr(0, minlen))
				{
					curnode = child.second;
					idx += minlen;
					break;
				}
			}
			if (curnode == 0) return std::set<zsr::filecount>{}; // No matches possible
		}
		return subtree_closure(curnode);
	}
}

