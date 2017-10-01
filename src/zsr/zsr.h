#ifndef ZSR_ZSR_H
#define ZSR_ZSR_H
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include "common.h"

namespace zsr
{
	class archive;
	class index;
	class iterator;

	class badzsr : public std::runtime_error
	{
	public:
		badzsr(std::string msg) : runtime_error{msg} { }
	};

	class stream : public std::istream
	{
	private:
		util::imemstream in_;
		lzma::rdbuf buf_;
		size_t size_, decomp_;
	public:
		stream(const std::string_view &in, size_t decomp);
		stream(const stream &orig) = delete;
		stream(stream &&orig);
	};

	class node
	{
	public:
		enum class ntype : char {unk = 0, dir = 1, reg = 2, link = 3};
	private:
		const archive &container_;
		filecount id_;
		std::vector<std::string> meta_;
		const std::function<std::string(const filecount &)> &revcheck_;
		ntype type_;
		offset parent_;
		filecount redirect_;
		std::string name_;
		offset len_, fullsize_;
		const char *data_;
		diskmap::map<std::string, filecount> childmap() const;
		node follow(unsigned int limit = 0, unsigned int depth = 0) const; // Need to follow for isdir/isreg, content, children, add_child, addmeta, delmeta, meta, setmeta, getchild, extract (create a link)
	public:
		node(const archive &container, offset idx);
		node(const node &orig) = default;
		node(node &&orig) = default;
		filecount id() const { return id_; }
		std::string name() const { return name_; }
		std::optional<node> parent() const;
		ntype type() const { return type_; }
		std::string path() const;
		std::string dest() const { return util::relreduce(util::dirname(path()), follow(1).path()); }
		size_t size() const { return follow().fullsize_; }
		std::string meta(const std::string &key) const;
		iterator children() const;
		std::optional<node> child(const std::string &name) const;
		stream content() const;
		void extract(const std::string &path) const;
		// Internal functions
		filecount nchild() const { return childmap().size(); }
		filecount childid(filecount idx) const { return childmap()[idx]; }
		bool isdir() const { return follow().type_ == ntype::dir; }
		bool isreg() const { return follow().type_ == ntype::reg; }
	};

	class iterator
	{
	private:
		const archive &ar;
		const node n;
		filecount idx;
	public:
		iterator(const archive &a, node nd) : ar{a}, n{nd}, idx{0} { }
		std::unordered_map<std::string, filecount> all() const;
		node get() const;
		void reset() { idx = 0; }
		void operator ++(int i) { idx++; }
		void operator --(int i) { idx--; }
		void operator +=(int i) { idx += i; }
		void operator -=(int i) { idx -= i; }
		bool operator ==(const iterator &other) const { return &n == &other.n && idx == other.idx; }
		operator bool() const { return idx >= 0 && idx < n.nchild(); }
	};
	
	class archive
	{
	private:
		std::function<std::string(const filecount &)> revcheck = [this](const filecount &x) {
			const char *inptr = idxstart_ + x * sizeof(offset);
			inptr = datastart_ + deser<offset>(inptr) + sizeof(filecount) + 1;
			return std::string{deser<std::string_view>(inptr)};
		};
		util::mmap_guard in_;
		const char *base_;
		const char *idxstart_, *datastart_;
		filecount size_;
		std::unordered_map<std::string, std::string> archive_meta_;
		std::vector<std::string> node_meta_;
		std::string_view userd_;
		node getnode(filecount idx) const { return node{*this, idx}; }
		std::optional<node> getnode(const std::string &path, bool except = false) const;
		unsigned int metaidx(const std::string &key) const;
		friend class node;
	public:
		archive(const std::string &path);
		archive(const archive &orig) = delete;
		archive(archive &&orig) : revcheck{std::move(orig.revcheck)}, in_{std::move(orig.in_)}, base_{orig.base_}, idxstart_{orig.idxstart_}, datastart_{orig.datastart_}, size_{orig.size_},
			archive_meta_{std::move(orig.archive_meta_)}, node_meta_{std::move(orig.node_meta_)}, userd_{std::move(orig.userd_)} { }
		filecount size() const { return size_; }
		const std::unordered_map<std::string, std::string> &gmeta() const { return archive_meta_; }
		std::vector<std::string> nodemeta() const { return node_meta_; }
		bool check(const std::string &path) const;
		node get(const std::string &path) const { return *getnode(path, true); }
		node index(filecount idx = 0) const { return node{*this, idx}; }
		const std::string_view &userdata() const { return userd_; }
		virtual ~archive() { }
	};
}

#endif
