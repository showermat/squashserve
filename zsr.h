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

/* TODO
 * Support symlinks
 * Then, in Wikipedia, when an article redirects, just make it a symlink
 */

namespace zsr
{
	typedef uint64_t index; // Constrains the maximum number of files in an archive
	typedef uint64_t offset; // Constrains the size of the archive

	class archive_base;
	class archive_tree;
	class archive_file;

	const int maxdepth = 256;

	void logging(bool on);

	void log(const std::string &msg, std::ostream &out = std::cerr);

	class badzsr : public std::runtime_error
	{
	public:
		badzsr(std::string msg) : runtime_error{msg} { }
	};

	class node_base
	{
	protected:
		index id_;
		node_base *parent_;
		std::string name_;
		offset start_, len_;
		node_base *redirect_;
		std::unordered_map<std::string, std::unique_ptr<node_base>> children_;
		std::vector<std::string> mdata_;
		node_base(index id, node_base *parent, const std::string &name, unsigned int nmeta) : id_{id}, parent_{parent}, name_{name}, start_{0}, len_{0}, redirect_{nullptr}, children_{}, mdata_(nmeta, "") { }
		node_base *follow(int depth = 0); // Need to follow for isdir, content, children, add_child, addmeta, delmeta, meta, setmeta, get_child, close, extract (create a link)
		// Need to set redirect_ when creating an archive from disk
		// When writing archive, skip metadata for links; be sure to adjust index_size appropriately
	public:
		node_base(const node_base &orig) = delete;
		index id() const { return id_; }
		std::string name() const { return name_; }
		node_base *parent() const { return parent_; }
		offset index_size() const;
		void debug_treeprint(std::string prefix = "") const; // TODO Debug remove
		virtual bool isdir() const = 0;
		virtual std::streambuf *content() = 0;
		virtual std::string path() const = 0;
		virtual size_t size() const = 0;
		const std::unordered_map<std::string, std::unique_ptr<node_base>> &children() const { return children_; }
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
		virtual size_t size() const;
	};

	class node_file : public node_base
	{
	private:
		archive_file &container_;
		std::unique_ptr<lzma::rdbuf> stream_;
		size_t fullsize_;
	public:
		node_file(archive_file &container, node_file *last);
		void resolve();
		bool isdir() const { return start_ == 0; }
		std::streambuf *content();
		void close() { stream_.reset(); }
		std::string path() const;
		virtual size_t size() const { return fullsize_; }
	};

	class node
	{
	private:
		archive_base *ar;
		index idx;
		node_base *getnode() const;
	public:
		node (archive_base *a, index i) : ar{a}, idx{i} { }
		index id() const { return idx; }
		std::string name() const { return getnode()->name(); }
		std::string path() const { return getnode()->path(); }
		bool isdir() const { return getnode()->isdir(); }
		std::string meta(const std::string &key) const;
		void meta(const std::string &key, const std::string &val);
		std::unordered_map<std::string, index> children() const;
		size_t size() const { return getnode()->size(); }
		std::streambuf *open();
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
		node_base *getnode(const std::string &path, bool except = false) const;
		unsigned int metaidx(const std::string &key) const;
		friend class node; // TODO Ugh
		friend class node_file;
	public:
		archive_base(const archive_base &orig) = delete;
		archive_base(archive_base &&orig) : root_{std::move(orig.root_)}, index_{std::move(orig.index_)}, archive_meta_{std::move(orig.archive_meta_)}, node_meta_{std::move(orig.node_meta_)}, open_{std::move(orig.open_)} { orig.root_ = nullptr; }
		void write(std::ostream &out);
		void extract(const std::string &member = "", const std::string &dest = ".");
		unsigned int size() const { return index_.size(); }
		std::unordered_map<std::string, std::string> &gmeta() { return archive_meta_; }
		std::vector<std::string> nodemeta() const { return node_meta_; }
		void addmeta(const std::string &key);
		void delmeta(const std::string &key);
		bool check(const std::string &path) const;
		void debug_treeprint() { root_->debug_treeprint(); }
		node get(const std::string &path) { return node{this, getnode(path, true)->id()}; }
		node index(index idx) { return node{this, idx}; }
		void reap();
		virtual std::istream &userdata() = 0;
	};

	class archive_tree : public archive_base
	{
	private:
		std::string basedir_;
		std::istream& userd_;
		node_tree *recursive_add(const std::string &path, node_tree *parent, std::function<std::unordered_map<std::string, std::string>(const std::string &)> metagen);
	public:
		std::string basedir() const { return basedir_; }
		archive_tree(const std::string &root, std::istream &userdata, const std::unordered_map<std::string, std::string> &gmeta, std::function<std::unordered_map<std::string, std::string>(const std::string &)> metagen);
		archive_tree(archive_tree &&orig) = default;
		std::istream &userdata() { return userd_; }
	};

	class archive_file : public archive_base
	{
	private:
		std::ifstream in_;
		std::unique_ptr<util::rangebuf> userdbuf_;
		std::istream userd_;
	public:
		archive_file(std::ifstream &&in);
		archive_file(archive_file &&orig) : archive_base{std::move(orig)}, in_{std::move(orig.in_)}, userdbuf_{std::move(orig.userdbuf_)}, userd_{&*userdbuf_} { orig.in_ = std::ifstream{}; }
		std::ifstream &in() { return in_; }
		std::istream &userdata() { return userd_; }
	};

	class archive
	{
	private:
		static std::ifstream default_istream_;
		std::unique_ptr<archive_base> impl_;
	public:
		archive(const std::string &root, std::istream &userdata = default_istream_, const std::unordered_map<std::string, std::string> &gmeta = {}, std::function<std::unordered_map<std::string, std::string>(const std::string &)> metagen = [](const std::string &s) { return std::unordered_map<std::string, std::string>{}; }) : impl_{new archive_tree{root, userdata, gmeta, metagen}} { }
		archive(std::ifstream &&in) : impl_{new archive_file{std::move(in)}} { }
		archive(archive &&orig) : impl_{std::move(orig.impl_)} { }
		bool check(const std::string &path) const { return impl_->check(path); }
		unsigned int size() const { return impl_->size(); }
		std::unordered_map<std::string, std::string> &gmeta() { return impl_->gmeta(); }
		std::vector<std::string> nodemeta() const { return impl_->nodemeta(); }
		void addmeta(const std::string &key) { impl_->addmeta(key); }
		void delmeta(const std::string &key) { impl_->delmeta(key); }
		void write(std::ostream &out) { impl_->write(out); }
		void extract(const std::string &member = "", const std::string &dest = ".") { impl_->extract(member, dest); }
		node get(const std::string &path) { return impl_->get(path); }
		node index(index idx = 0) { return impl_->index(idx); }
		std::istream &userdata() { return impl_->userdata(); }
		void reap() { impl_->reap(); }
	};
}

#endif

