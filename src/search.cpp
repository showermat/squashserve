#include "search.h"

namespace rsearch
{
	const zsr::offset ptrfill{0};

	void debug_treeprint(radix_tree_node<std::string, std::unordered_set<zsr::filecount>> *n, std::string prefix = "") // Debug
	{
		if (! n) return;
		std::cout << prefix << n->m_key << " -> ";
		if (n->m_children.count("") && n->m_children[""]->m_value) for (const zsr::filecount &i : n->m_children[""]->m_value->second) std::cout << i << " ";
		std::cout << "\n";
		for (const std::pair<const std::string, radix_tree_node<std::string, std::unordered_set<zsr::filecount>> *> child : n->m_children) if (child.first != "") debug_treeprint(child.second, prefix + "  ");
	}

	void disktree::debug_print(zsr::offset off, std::string prefix) // Debug
	{
		if (! *in_) throw std::runtime_error{"Bad stream"};
		in_->seekg(off);
		const std::unordered_map<std::string, zsr::offset> curchild = children();
		const std::unordered_set<zsr::filecount> myval = values();
		for (const zsr::filecount &val : myval) std::cout << val << " ";
		std::cout << "\n";
		for (const std::pair<const std::string, zsr::offset> &child : curchild)
		{
			std::cout << prefix << child.first << " -> ";
			debug_print(child.second, prefix + "  ");
		}
	}

	void recursive_treewrite(std::ostream &out, radix_tree_node<std::string, std::unordered_set<zsr::filecount>> *n, zsr::offset treestart)
	{
		static int nwritten = 0;
		logb(++nwritten);
		treesize nval = 0;
		if (n->m_children.count("") && n->m_children[""]->m_value) nval = n->m_children[""]->m_value->second.size();
		treesize nchild = 0;
		for (const std::pair<const std::string, radix_tree_node<std::string, std::unordered_set<zsr::filecount>> *> child : n->m_children) if (child.first != "" && child.second) nchild++; // TODO Probably a faster way?
		zsr::serialize(out, nchild);
		std::deque<zsr::offset> childpos{};
		for (const std::pair<const std::string, radix_tree_node<std::string, std::unordered_set<zsr::filecount>> *> child : n->m_children)
		{
			if (child.first == "" || ! child.second) continue;
			treesize namelen = child.first.size();
			zsr::serialize(out, namelen);
			out.write(&child.first[0], namelen);
			childpos.push_back(static_cast<zsr::offset>(out.tellp()));
			zsr::serialize(out, ptrfill);
		}
		zsr::serialize(out, nval);
		if (n->m_children.count("") && n->m_children[""]->m_value)
			for (const zsr::filecount &i : n->m_children[""]->m_value->second) zsr::serialize(out, i);
		for (const std::pair<const std::string, radix_tree_node<std::string, std::unordered_set<zsr::filecount>> *> child : n->m_children)
		{
			if (child.first == "" || ! child.second) continue;
			zsr::offset childstart = static_cast<zsr::offset>(out.tellp()) - treestart;
			out.seekp(childpos.front());
			zsr::serialize(out, childstart);
			out.seekp(0, std::ios_base::end);
			recursive_treewrite(out, child.second, treestart);
			childpos.pop_front();
		}
	}

	void disktree_writer::write(std::ostream &out)
	{
		loga("Writing title index");
		if (! stree_.m_root)
		{
			constexpr treesize fill{0};
			for (int i = 0; i < 2; i++) zsr::serialize(out, fill);
			return;
		}
		recursive_treewrite(out, stree_.m_root, static_cast<zsr::offset>(out.tellp()));
	}

	void disktree_writer::add(const std::string &title, zsr::filecount id)
	{
		if (title.size() == 0) return;
		std::locale loc{util::ucslocale};
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert{};
		std::basic_ostringstream<wchar_t> ss{};
		try { for (const wchar_t &c : convert.from_bytes(title)) ss << std::tolower(c, loc); }
		catch (std::range_error &e) { std::cerr << clrln << "Could not decode title \"" << title << "\" as UTF-8\n"; return; }
		std::wstring lctitle = ss.str();
		for (std::wstring::size_type i = 0; i < lctitle.size(); i++)
			if (i == 0 ||
				(std::isspace(lctitle[i - 1], loc) && ! std::isspace(lctitle[i], loc)) ||
				(! std::isalnum(lctitle[i - 1], loc) && std::isalnum(lctitle[i], loc))) // TODO Index starting from new words in camel case
					stree_[convert.to_bytes(lctitle.substr(i))].insert(id);
	}

	std::unordered_map<std::string, zsr::offset> disktree::children()
	{
		std::unordered_map<std::string, zsr::offset> ret{};
		treesize nchild;
		zsr::deserialize(*in_, nchild);
		for (treesize i = 0; i < nchild; i++)
		{
			treesize namelen;
			zsr::deserialize(*in_, namelen);
			std::string name{};
			name.resize(namelen);
			in_->read(reinterpret_cast<char *>(&name[0]), namelen);
			zsr::offset loc;
			zsr::deserialize(*in_, loc);
			ret[name] = loc;
		}
		return ret;
	}

	std::unordered_set<zsr::filecount> disktree::values()
	{
		std::unordered_set<zsr::filecount> ret{};
		treesize nval;
		zsr::deserialize(*in_, nval);
		for (treesize i = 0; i < nval; i++)
		{
			zsr::filecount curval;
			zsr::deserialize(*in_, curval);
			ret.insert(curval);
		}
		return ret;
	}

	std::unordered_set<zsr::filecount> disktree::subtree_closure(zsr::offset nodepos)
	{
		std::unordered_set<zsr::filecount> ret{};
		in_->seekg(start_ + nodepos);
		if (! *in_) throw std::runtime_error{"Tried to seek outside of file"};
		const std::unordered_map<std::string, zsr::offset> curchild = children();
		const std::unordered_set<zsr::filecount> myval = values();
		ret.insert(myval.cbegin(), myval.cend());
		for (const std::pair<const std::string, zsr::offset> &child : curchild)
		{
			if (child.second == nodepos) throw std::runtime_error{"Loop detected in search tree"};
			const std::unordered_set<zsr::filecount> curval = subtree_closure(child.second);
			ret.insert(curval.cbegin(), curval.cend());
		}
		return ret;
	}

	zsr::offset disktree::nodefind(const std::string &query)
	{
		zsr::offset curnode = 0;
		std::string::size_type idx = 0;
		while (idx < query.size())
		{
			in_->seekg(start_ + curnode);
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
			if (curnode == 0) return 0;
		}
		return curnode;
	}

	std::unordered_set<zsr::filecount> disktree::search(const std::string &query)
	{
		zsr::offset top = nodefind(query);
		if (top == 0) return std::unordered_set<zsr::filecount>{};
		return subtree_closure(top);
	}

	std::unordered_set<zsr::filecount> disktree::exact_search(const std::string &query)
	{
		zsr::offset top = nodefind(query);
		if (top == 0) return std::unordered_set<zsr::filecount>{};
		in_->seekg(start_ + top);
		if (! *in_) throw std::runtime_error{"Tried to seek outside of file"};
		children();
		return values();
	}
}
