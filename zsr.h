#ifndef ZSR_H
#define ZSR_H
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <list>
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
#include <errno.h>
#include <string.h>
#include "util/util.h"
#include "compress.h"
#include "diskmap.h"

#define VERBOSE

#ifdef VERBOSE
const std::string clrln{"\r\033[K"};
#define loga(msg) std::cout << clrln << util::timestr() << ": " << msg << std::endl
#define logb(msg) std::cout << clrln << msg << std::flush
#else
#define loga(msg)
#define logb(msg)
#endif

namespace zsr
{
	typedef uint64_t filecount; // Constrains the maximum number of files in an archive
	typedef uint64_t offset; // Constrains the size of the archive and individual files

	//bool verbose = false;
	//const std::string clrln{"\r\033[K"};
	//inline void loga(const std::string &msg) { if (verbose) std::cout << clrln << util::timestr() << ": " << msg << std::endl; }
	//inline void logb(const std::string &msg) { if (verbose) std::cout << clrln << msg << std::flush; }

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
		enum class linkpolicy { process, follow, skip };
	private:
		class linkmgr
		{
		public:
			struct linkinfo
			{
				bool resolved;
				std::streampos destpos;
				filecount destid;
				linkinfo() : resolved{false}, destpos{}, destid{} { }
			};
		private:
			std::string root_;
			std::list<linkinfo> links_;
			std::unordered_map<std::string, linkinfo *> by_src_;
			std::unordered_multimap<std::string, linkinfo *> by_dest_;
			void add(const std::string &src, const std::string &dest);
			static bool walk_add(const std::string &path, const struct stat *st, void *dest);
		public:
			linkmgr(const std::string root) : root_{root} { }
			void search();
			void handle_src(const std::string &path, std::streampos destpos);
			void handle_dest(const std::string &path, filecount id);
			void write(std::ostream &out);
			size_t size() { return links_.size(); }
		};
		const std::string root_, fullroot_;
		linkpolicy linkpol_;
		std::unordered_map<std::string, std::string> volmeta_;
		std::vector<std::string> nodemeta_;
		std::function<std::vector<std::string>(const filenode &)> metagen_;
		std::istream *userdata_;
		std::string headf_, contf_, idxf_;
		filecount nfile_;
		linkmgr links_;
		void writestring(const std::string &s, std::ostream &out);
		filecount recursive_process(const std::string &path, filecount parent, std::ofstream &contout, std::ofstream &idxout);
	public:
		writer(const std::string &root, linkpolicy links = linkpolicy::process) : root_{root}, fullroot_{util::resolve(std::string{getenv("PWD")}, root_)}, linkpol_{links}, volmeta_{}, nodemeta_{}, metagen_{[](const filenode &n) { return std::vector<std::string>{}; }}, userdata_{nullptr}, nfile_{}, links_{fullroot_} { }
		void userdata(std::istream &data) { userdata_ = &data; }
		void volume_meta(const std::unordered_map<std::string, std::string> data) { volmeta_ = data; }
		void node_meta(const std::vector<std::string> keys, std::function<std::vector<std::string>(const filenode &)> generator) { nodemeta_ = keys; metagen_ = generator; }
		void write_body(const std::string &contname = "content.zsr.tmp", const std::string &idxname = "index.zsr.tmp");
		void write_header(const std::string &tmpfname = "header.zsr.tmp");
		void combine(std::ofstream &out);
		void write(std::ofstream &out);
		virtual ~writer();
	};

	class node
	{
	public:
		enum class ntype : char {unk = 0, dir = 1, reg = 2, link = 3};
	private:
		archive &container_;
		filecount id_;
		std::vector<std::string> meta_;
		std::function<std::string(const filecount &)> &revcheck_;
		ntype type_;
		offset parent_;
		filecount redirect_;
		std::string name_;
		offset len_;
		size_t fullsize_;
		offset datastart_;
		std::string readstring();
		diskmap::map<std::string, filecount> childmap();
		node follow(unsigned int limit = 0, unsigned int depth = 0); // Need to follow for isdir/isreg, content, children, add_child, addmeta, delmeta, meta, setmeta, getchild, close, extract (create a link)
	public:
		node(archive &container, offset idx);
		node(const node &orig) = default; // Can these both be default?
		node(node &&orig) = default;
		filecount id() const { return id_; }
		std::string name() const { return name_; }
		std::unique_ptr<node> parent(); // Pending existence of std::optional
		ntype type() const { return type_; }
		bool isdir() { return follow().type_ == ntype::dir; }
		bool isreg() { return follow().type_ == ntype::reg; }
		std::string path();
		std::string dest() { return util::relreduce(util::dirname(path()), follow(1).path()); }
		size_t size() { return follow().fullsize_; }
		std::string meta(uint8_t key) { return follow().meta_[key]; }
		std::unordered_map<std::string, filecount> children();
		std::unique_ptr<node> getchild(const std::string &name); // Pending std::optional
		std::streambuf *content();
		void close();
		void extract(const std::string &path);
	};

	class iterator // TODO Make this actually inherit from std::iterator
	{
	private:
		archive &ar;
		filecount idx;
		node getnode() const;
	public:
		iterator(archive &a, filecount i) : ar{a}, idx{i} { }
		filecount id() const { return idx; }
		std::string name() const { return getnode().name(); }
		std::string path() const { return getnode().path(); }
		bool isdir() const { return getnode().isdir(); }
		bool isreg() const { return getnode().isreg(); }
		node::ntype type() const { return getnode().type(); }
		std::string meta(const std::string &key) const;
		std::unordered_map<std::string, filecount> children() const;
		std::string dest() const { return getnode().dest(); }
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
		std::function<std::string(const filecount &)> revcheck = [this](const filecount &x) { return index(x).name(); };
		std::ifstream in_;
		offset idxstart_, datastart_;
		filecount size_;
		std::unordered_map<std::string, std::string> archive_meta_;
		std::vector<std::string> node_meta_;
		std::unordered_map<filecount, lzma::rdbuf> open_;
		std::unique_ptr<util::rangebuf> userdbuf_;
		std::istream userd_;
		node getnode(filecount idx) { return node{*this, idx}; }
		std::unique_ptr<node> getnode(const std::string &path, bool except = false); // TODO Pending std::optional
		unsigned int metaidx(const std::string &key) const;
		friend class node; // TODO Ugh
		friend class iterator;
	public:
		archive(const archive &orig) = delete;
		archive(archive &&orig) : revcheck{std::move(orig.revcheck)}, in_{std::move(orig.in_)}, idxstart_{orig.idxstart_}, datastart_{orig.datastart_}, size_{orig.size_}, archive_meta_{std::move(orig.archive_meta_)}, node_meta_{std::move(orig.node_meta_)}, open_{std::move(orig.open_)}, userdbuf_{std::move(orig.userdbuf_)}, userd_{&*userdbuf_} { orig.in_ = std::ifstream{}; orig.userd_.rdbuf(nullptr); }
		archive(std::ifstream &&in);
		filecount size() const { return size_; }
		std::unordered_map<std::string, std::string> &gmeta() { return archive_meta_; }
		std::vector<std::string> nodemeta() const { return node_meta_; }
		bool check(const std::string &path);
		iterator get(const std::string &path) { return iterator{*this, getnode(path, true)->id()}; }
		iterator index(filecount idx = 0) { return iterator{*this, idx}; }
		void extract(const std::string &member = "", const std::string &dest = ".");
		std::istream &userdata() { return userd_; }
		void close(const std::string &path) { getnode(path, true)->close(); }
		void reap() { open_.clear(); }
		virtual ~archive() { reap(); }
	};
}

#endif

