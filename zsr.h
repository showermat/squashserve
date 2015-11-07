#ifndef ZSR_H
#define ZSR_H
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include "util.h"

namespace zsr
{
	const int compression = 6;
	const int memlimit = 1 << 30; // 1 GB

	class archive;
	class archive_tree;
	class archive_file;

	std::vector<char> compress(std::vector<char> input);

	std::vector<char> decompress(std::vector<char> input);

	class badzsr : public std::runtime_error
	{
	public:
		badzsr(std::string msg) : runtime_error{msg} { }
	};

	class compress_error : public std::runtime_error
	{
	public:
		compress_error(std::string msg) : runtime_error{msg} { }
	};

	class node
	{
	public:
		typedef uint64_t index;
		typedef uint64_t offset;
	protected:
		index id_;
		node *parent_;
		std::string name_;
		offset start_, len_;
		std::map<std::string, std::unique_ptr<node>> children_;
		node(index id, node *parent, const std::string &name) : id_{id}, parent_{parent}, name_{name}, start_{0}, len_{0}, children_{} { }
	public:
		node(const node &orig) = delete;
		index id() const { return id_; }
		std::string name() const { return name_; }
		node *parent() const { return parent_; }
		offset index_size() const { return 2 * sizeof(index) + 2 * sizeof(offset) + 2 + name_.size(); }
		void debug_treeprint(std::string prefix = "") const; // TODO Debug remove
		virtual bool isdir() const = 0;
		virtual std::vector<char> content() = 0;
		virtual std::string path() const = 0;
		void add_child(node *n);
		node *get_child(const std::string &name) const;
		void write_content(std::ostream &out);
		void write_index(std::ostream &out);
		void extract(const std::string &path);
		size_t hash() const { return static_cast<size_t>(id_); }
	};

	class node_tree : public node
	{
	private:
		std::string dirpath_;
		archive_tree &container_;
	public:
		node_tree(index id, node *parent, std::string path, archive_tree &container) : node{id, parent, util::basename(path)}, dirpath_{util::dirname(path)}, container_{container} { } // FIXME I believe this makes dirpath_ an absolute path, which will make problems if we then try to extract it.  Path should be relative to the archive root!
		bool isdir() const;
		std::vector<char> content();
		std::string path() const { return dirpath_ + util::pathsep + name(); }
	};

	class node_file : public node
	{
	private:
		archive_file &container_;
	public:
		node_file(archive_file &container, node_file *last);
		bool isdir() const { return start_ == 0; }
		std::vector<char> content();
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
	public:
		archive_base(const archive_base &orig) = delete;
		archive_base(archive_base &&orig) : next_id_{orig.next_id_}, root_{std::move(orig.root_)} { orig.root_ = nullptr; }
		void write(std::ostream &out);
		void extract(const std::string &dest, const std::string &subdir = "");
		bool check(const std::string &path) const;
		bool isdir(const std::string &path) const;
		void debug_treeprint() { root_->debug_treeprint(); }
		std::vector<char> get(const std::string &path) const;
	};

	class archive_tree : public archive_base
	{
	private:
		std::string basedir_;
		node_tree *recursive_add(const std::string &path, node_tree *parent);
	public:
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
		void extract(const std::string &dest, const std::string &subdir = "") { impl_->extract(dest, subdir); }
		std::vector<char> get(const std::string &path) { return impl_->get(path); }
	};
}

#endif

