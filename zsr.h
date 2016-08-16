#ifndef ZSR_H
#define ZSR_H
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
#include "lib/sparsehash/sparse_hash_map"
#include "util.h"
#include "compress.h"

#define VERBOSE

#ifdef VERBOSE
const std::string clrln{"\r\033[K"};
#define loga(msg) std::cout << clrln << util::timestr() << ": " << msg << std::endl
#define logb(msg) std::cout << clrln << msg << std::flush
#else
#define loga(msg)
#define logb(msg)
#endif

/* TODO
 * Support symlinks
 * Then, in Wikipedia, when an article redirects, just make it a symlink
 */

namespace zsr
{
	typedef uint64_t filecount; // Constrains the maximum number of files in an archive
	typedef uint64_t offset; // Constrains the size of the archive and individual files

	const int maxdepth = 255;

	class archive;
	class index;

	class badzsr : public std::runtime_error
	{
	public:
		badzsr(std::string msg) : runtime_error{msg} { }
	};

	class writer
	{
	public:
		class filenode
		{
		private:
			writer &owner_;
			const filecount id_;
			const std::string path_;
		public:
			filenode(writer &owner, const filecount id, const std::string &path) : owner_{owner}, id_{id}, path_{path} { }
			filecount id() const { return id_; }
			std::string path() const { return path_; }
		};
	private:
		const std::string root_;
		std::unordered_map<std::string, std::string> volmeta_;
		std::vector<std::string> nodemeta_;
		std::function<std::vector<std::string>(const filenode &)> metagen_;
		std::istream *userdata_;
		std::string headf_, contf_, idxf_;
		filecount nfile_;
		void writestring(const std::string &s, std::ostream &out);
		void recursive_process(const std::string &path, filecount parent, std::ofstream &contout, std::ofstream &idxout);
	public:
		writer(const std::string &root) : root_{root}, volmeta_{}, nodemeta_{}, metagen_{[](const filenode &n) { return std::vector<std::string>{}; }}, userdata_{nullptr} { }
		void userdata(std::istream &data) { userdata_ = &data; }
		void volume_meta(const std::unordered_map<std::string, std::string> data) { volmeta_ = data; }
		void node_meta(const std::vector<std::string> keys, std::function<std::vector<std::string>(const filenode &)> generator) { nodemeta_ = keys; metagen_ = generator; }
		void write_body(const std::string &contname = "content.zsr.tmp", const std::string &idxname = "index.zsr.tmp");
		void write_header(const std::string &tmpfname = "header.zsr.tmp");
		void combine(std::ofstream &out);
		void write(std::ofstream &out);
	};

	class node
	{
	public:
		enum class ntype : char {unk = 0, dir = 1, reg = 2, link = 3};
		//class sethash { public: size_t operator ()(const std::unique_ptr<node> &x) const { return std::hash<std::string>{}(x->name_); } };
	private:
		class nodeinfo
		{
		private:
			std::istream &in_;
			offset metastart_;
			uint8_t nmeta_;
			std::string readstring();
		public:
			ntype type;
			offset parent, redirect;
			std::string name;
			offset start, len;
			size_t fullsize;
			nodeinfo(std::istream &in, uint8_t nmeta);
			std::vector<std::string> meta();
		};
		offset start_;
		std::unique_ptr<std::unordered_map<size_t, filecount>> children_;
		archive &container_;
		nodeinfo readinfo();
		node &follow(int depth = 0);
		// Need to follow for isdir/isreg, content, children, add_child, addmeta, delmeta, meta, setmeta, getchild, close, extract (create a link)
		// Need to set redirect_ when creating an archive from disk
		friend class archive; // TODO
	public:
		node(archive &container);
		node(const node &orig) = delete;
		node(node &&orig) : start_{orig.start_}, children_{std::move(orig.children_)}, container_{orig.container_} { }
		filecount id() const;
		std::string name() { return readinfo().name; }
		node *parent();
		void debug_treeprint(std::string prefix = ""); // TODO Debug remove
		bool isdir() { return static_cast<bool>(children_); }
		bool isreg() { return ! children_; }
		ntype type() { return readinfo().type; }
		std::streambuf *content();
		std::string path();
		size_t size() { return readinfo().fullsize; }
		std::unordered_map<std::string, filecount> children() const;
		std::string meta(uint8_t key) { return readinfo().meta()[key]; }
		const node *getchild(const std::string &name) const;
		node *getchild(const std::string &name) { return const_cast<node *>(static_cast<const node &>(*this).getchild(name)); }
		void close();
		void extract(const std::string &path);
		void resolve();
		//size_t hash() const { return static_cast<size_t>(id()); }
		bool operator ==(node &other) { return name() == other.name(); }
	};

	class iterator
	{
	private:
		archive &ar;
		filecount idx;
		node &getnode() const;
	public:
		iterator(archive &a, filecount i) : ar{a}, idx{i} { }
		filecount id() const { return idx; }
		std::string name() const { return getnode().name(); }
		std::string path() const { return getnode().path(); }
		bool isdir() const { return getnode().isdir(); }
		std::string meta(const std::string &key) const;
		std::unordered_map<std::string, filecount> children() const;
		size_t size() const { return getnode().size(); }
		std::streambuf *open();
		void close() { getnode().close(); }
		void operator ++(int i) { idx++; }
		void operator --(int i) { idx--; }
		void operator +=(int i) { idx += i; }
		void operator -=(int i) { idx -= i; }
		bool operator ==(const iterator &other) const { return &ar == &other.ar && idx == other.idx; }
		operator bool() const;
	};

	class archive
	{
	public:
		static const std::string magic_number;
	private:
		static std::ifstream default_istream_;
		std::ifstream in_;
		std::vector<node> index_;
		offset datastart_;
		std::unordered_map<std::string, std::string> archive_meta_;
		std::vector<std::string> node_meta_;
		std::unordered_map<filecount, lzma::rdbuf> open_;
		std::unique_ptr<util::rangebuf> userdbuf_;
		std::istream userd_;
		//archive() : root_{}, index_{}, archive_meta_{}, node_meta_{}, open_{} { }
		node &getnode(filecount idx) { return index_[idx]; }
		node *getnode(const std::string &path, bool except = false);
		unsigned int metaidx(const std::string &key) const;
		friend class node; // TODO Ugh
		friend class iterator;
	public:
		archive(const archive &orig) = delete;
		archive(archive &&orig) : in_{std::move(orig.in_)}, index_{std::move(orig.index_)}, archive_meta_{std::move(orig.archive_meta_)}, node_meta_{std::move(orig.node_meta_)}, open_{std::move(orig.open_)}, userdbuf_{std::move(orig.userdbuf_)}, userd_{&*userdbuf_} { orig.in_ = std::ifstream{}; orig.userd_.rdbuf(nullptr); }
		archive(std::ifstream &&in);
		filecount size() const { return index_.size(); }
		std::unordered_map<std::string, std::string> &gmeta() { return archive_meta_; }
		std::vector<std::string> nodemeta() const { return node_meta_; }
		bool check(const std::string &path);
		void debug_treeprint() { index_[0].debug_treeprint(); }
		iterator get(const std::string &path) { return iterator{*this, getnode(path, true)->id()}; }
		iterator index(filecount idx = 0) { return iterator{*this, idx}; }
		void extract(const std::string &member = "", const std::string &dest = ".");
		std::ifstream &in() { return in_; }
		std::istream &userdata() { return userd_; }
		void close(const std::string &path) { getnode(path, true)->close(); }
		void reap() { open_.clear(); }
		virtual ~archive() { reap(); }
	};
}

#endif

