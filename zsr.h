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
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "util.h"
#include "compress.h"

namespace zsr
{
	class archive;
	class archive_tree;
	class archive_file;

	void logging(bool on);

	void log(const std::string &msg, std::ostream &out = std::cerr);

	class badzsr : public std::runtime_error
	{
	public:
		badzsr(std::string msg) : runtime_error{msg} { }
	};

	class node
	{
	public:
		typedef uint64_t index; // Constrains the maximum number of files in an archive
		typedef uint64_t offset; // Constrains the size of the archive
	protected:
		index id_;
		node *parent_;
		std::string name_;
		offset start_, len_;
		std::unordered_map<std::string, std::unique_ptr<node>> children_;
		node(index id, node *parent, const std::string &name) : id_{id}, parent_{parent}, name_{name}, start_{0}, len_{0}, children_{} { }
	public:
		node(const node &orig) = delete;
		index id() const { return id_; }
		std::string name() const { return name_; }
		node *parent() const { return parent_; }
		offset index_size() const { return 2 * sizeof(index) + 2 * sizeof(offset) + 2 + name_.size(); }
		//virtual offset size() const = 0;
		void debug_treeprint(std::string prefix = "") const; // TODO Debug remove
		virtual bool isdir() const = 0;
		virtual std::streambuf *content() = 0;
		virtual std::string path() const = 0;
		void add_child(node *n);
		node *get_child(const std::string &name) const;
		void write_content(std::ostream &out);
		void write_index(std::ostream &out);
		virtual void close() { }
		void extract(const std::string &path);
		size_t hash() const { return static_cast<size_t>(id_); }
	};

	class node_tree : public node
	{
	private:
		archive_tree &container_;
		std::ifstream stream_;
	public:
		node_tree(index id, node *parent, std::string path, archive_tree &container) : node{id, parent, util::basename(path)}, container_{container}, stream_{} {}
		bool isdir() const;
		std::streambuf *content();
		void close() { stream_.close(); }
		std::string path() const;
	};

	class node_file : public node
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

	class archive_base
	{
	public:
		static const std::string magic_number;
	protected:
		node::index next_id_ = 1;
		std::unique_ptr<node> root_;
		archive_base() : root_{nullptr} { }
		node *getnode(const std::string &path) const;
		std::set<node *> open_;
	public:
		archive_base(const archive_base &orig) = delete;
		archive_base(archive_base &&orig) : next_id_{orig.next_id_}, root_{std::move(orig.root_)} { orig.root_ = nullptr; }
		void write(std::ostream &out);
		void extract(const std::string &member = "", const std::string &dest = ".");
		bool check(const std::string &path) const;
		bool isdir(const std::string &path) const;
		void debug_treeprint() { root_->debug_treeprint(); }
		std::streambuf *open(const std::string &path);
		void reap();
	};

	class archive_tree : public archive_base
	{
	private:
		std::string basedir_;
		node_tree *recursive_add(const std::string &path, node_tree *parent);
	public:
		std::string basedir() const { return basedir_; }
		archive_tree(const std::string &root);
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
		archive(const std::string &root) : impl_{new archive_tree{root}} { }
		archive(std::ifstream &&in) : impl_{new archive_file{std::move(in)}} { }
		archive(archive &&orig) : impl_{std::move(orig.impl_)} { }
		bool check(const std::string &path) const { return impl_->check(path); }
		bool isdir(const std::string &path) const { return impl_->isdir(path); }
		void write(std::ostream &out) { impl_->write(out); }
		void extract(const std::string &member = "", const std::string &dest = ".") { impl_->extract(member, dest); }
		std::streambuf *open(const std::string &path) { return impl_->open(path); }
		void reap() { impl_->reap(); }
	};
}

#endif

