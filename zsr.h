#ifndef ZSR_H
#define ZSR_H
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <tuple>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "util.h"
#include "compress.h"

namespace zsr
{
	class archive_base;
	class archive_tree;
	class archive_file;

	void logging(bool on);

	void log(const std::string &msg, std::ostream &out = std::cerr);

	class badzsr : public std::runtime_error
	{
	public:
		badzsr(std::string msg) : runtime_error{msg} { }
	};

	class node_base
	{
	public:
		typedef uint64_t index; // Constrains the maximum number of files in an archive
		typedef uint64_t offset; // Constrains the size of the archive
	protected:
		index id_;
		node_base *parent_;
		std::string name_;
		offset start_, len_;
		std::unordered_map<std::string, std::unique_ptr<node_base>> children_;
		std::vector<std::string> mdata_;
		node_base(index id, node_base *parent, const std::string &name, unsigned int nmeta) : id_{id}, parent_{parent}, name_{name}, start_{0}, len_{0}, children_{}, mdata_(nmeta, "") { }
	public:
		node_base(const node_base &orig) = delete;
		index id() const { return id_; }
		std::string name() const { return name_; }
		node_base *parent() const { return parent_; }
		offset index_size() const;
		//virtual offset size() const = 0;
		void debug_treeprint(std::string prefix = "") const; // TODO Debug remove
		virtual bool isdir() const = 0;
		virtual std::streambuf *content() = 0;
		virtual std::string path() const = 0;
		void add_child(node_base *n);
		void addmeta() { mdata_.push_back(""); }
		void delmeta(int idx) { mdata_.erase(mdata_.begin() + idx); }
		std::string meta(unsigned int key) const { return mdata_.at(key); }
		void meta(unsigned int key, const std::string &val) { mdata_.at(key) = val; }
		void setmeta(const std::vector<std::string> &mdata) { mdata_ = mdata; }
		node_base *get_child(const std::string &name) const;
		void write_content(std::ostream &out);
		void write_index(std::ostream &out);
		virtual void close() { }
		void extract(const std::string &path);
		//size_t hash() const { return static_cast<size_t>(id_); }
	};

	class node_tree : public node_base
	{
	private:
		archive_tree &container_;
		std::ifstream stream_;
	public:
		node_tree(archive_tree &container, node_base *parent, std::string path);
		bool isdir() const;
		std::streambuf *content();
		void close() { stream_.close(); }
		std::string path() const;
	};

	class node_file : public node_base
	{
	private:
		archive_file &container_;
		std::unique_ptr<lzma::rdbuf> stream_;
	public:
		node_file(archive_file &container, node_file *last);
		bool isdir() const { return start_ == 0; }
		std::streambuf *content();
		void close() { stream_.reset(); }
		std::string path() const;
	};

	class node
	{
	private:
		archive_base *ar;
		node_base::index idx;
		const node_base *getnode() const;
	public:
		node (archive_base *a, node_base::index i) : ar{a}, idx{i} { }
		std::string name() const { return getnode()->name(); }
		std::string path() const { return getnode()->path(); }
		node_base::index index() const { if (getnode()->id() != idx) throw std::runtime_error{"Inconsistent index"}; return getnode()->id(); }
		bool isdir() const { return getnode()->isdir(); }
		std::string meta(const std::string &key) const;
		void operator ++(int i) { idx++; }
		void operator --(int i) { idx--; }
		void operator +=(int i) { idx += i; }
		void operator -=(int i) { idx -= i; }
		bool operator ==(const node &other) const { return ar == other.ar && idx == other.idx; }
		operator bool() const;
	};

	class archive_base
	{
	public:
		static const std::string magic_number;
	protected:
		std::unique_ptr<node_base> root_;
		std::vector<node_base *> index_;
		std::unordered_map<std::string, std::string> archive_meta_;
		std::vector<std::string> node_meta_;
		std::set<node_base *> open_;
		archive_base() : root_{}, index_{}, archive_meta_{}, node_meta_{}, open_{} { }
		node_base *getnode(const std::string &path) const;
		unsigned int metaidx(const std::string &key) const;
		friend class node;
	public:
		// TODO Most of these should be replaced by a single function that returns a node and then new functions in the node... (but only const functions)
		// TODO Move value typedefs from node_base into zsr namespace
		archive_base(const archive_base &orig) = delete;
		archive_base(archive_base &&orig) : root_{std::move(orig.root_)}, index_{std::move(orig.index_)}, archive_meta_{std::move(orig.archive_meta_)}, node_meta_{std::move(orig.node_meta_)}, open_{std::move(orig.open_)} { orig.root_ = nullptr; }
		node_base *idx(unsigned int i) { if (i >= index_.size()) throw badzsr{"Tried to get node " + util::t2s(i) + " from " + util::t2s(index_.size()) + " nodes"}; return index_.at(i); }
		void write(std::ostream &out);
		void extract(const std::string &member = "", const std::string &dest = ".");
		unsigned int size() const { return index_.size(); }
		std::unordered_map<std::string, std::string> &gmeta() { return archive_meta_; }
		std::vector<std::string> nodemeta() const { return node_meta_; }
		void addmeta(const std::string &key);
		void delmeta(const std::string &key);
		std::string meta(const std::string &path, const std::string &key) const;
		void meta(const std::string &path, const std::string &key, const std::string &val);
		bool check(const std::string &path) const;
		bool isdir(const std::string &path) const;
		void debug_treeprint() { root_->debug_treeprint(); }
		std::streambuf *open(const std::string &path);
		node index(node_base::index idx = 0) { return node{this, idx}; }
		void reap();
	};

	class archive_tree : public archive_base
	{
	private:
		std::string basedir_;
		node_tree *recursive_add(const std::string &path, node_tree *parent, std::function<std::unordered_map<std::string, std::string>(const std::string &)> metagen);
	public:
		std::string basedir() const { return basedir_; }
		archive_tree(const std::string &root, const std::unordered_map<std::string, std::string> &gmeta, std::function<std::unordered_map<std::string, std::string>(const std::string &)> metagen);
		archive_tree(archive_tree &&orig) = default;
	};

	class archive_file : public archive_base
	{
	private:
		std::ifstream in_;
	public:
		archive_file(std::ifstream &&in);
		archive_file(archive_file &&orig) : archive_base{std::move(orig)}, in_{std::move(orig.in_)} { orig.in_ = std::ifstream{}; }
		std::ifstream &in() { return in_; }
	};

	class archive
	{
	private:
		std::unique_ptr<archive_base> impl_;
	public:
		archive(const std::string &root, const std::unordered_map<std::string, std::string> &gmeta = {}, std::function<std::unordered_map<std::string, std::string>(const std::string &)> metagen = [](const std::string &s) { return std::unordered_map<std::string, std::string>{}; }) : impl_{new archive_tree{root, gmeta, metagen}} { }
		archive(std::ifstream &&in) : impl_{new archive_file{std::move(in)}} { }
		archive(archive &&orig) : impl_{std::move(orig.impl_)} { }
		bool check(const std::string &path) const { return impl_->check(path); }
		bool isdir(const std::string &path) const { return impl_->isdir(path); }
		unsigned int size() const { return impl_->size(); }
		std::unordered_map<std::string, std::string> &gmeta() { return impl_->gmeta(); }
		std::vector<std::string> nodemeta() const { return impl_->nodemeta(); }
		void addmeta(const std::string &key) { impl_->addmeta(key); }
		void delmeta(const std::string &key) { impl_->delmeta(key); }
		std::string meta(const std::string &path, const std::string &key) const { return impl_->meta(path, key); }
		void meta(const std::string &path, const std::string &key, const std::string &val) { return impl_->meta(path, key, val); }
		void write(std::ostream &out) { impl_->write(out); }
		void extract(const std::string &member = "", const std::string &dest = ".") { impl_->extract(member, dest); }
		std::streambuf *open(const std::string &path) { return impl_->open(path); }
		void reap() { impl_->reap(); }
	};
}

#endif

