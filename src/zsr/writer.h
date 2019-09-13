#ifndef ZSR_WRITER_H
#define ZSR_WRITER_H
#include <list>
#include <tuple>
#include <unistd.h>
#include "../util/util.h"
#include "common.h"

namespace zsr
{
	class filenode
	{
	public:
		enum class ntype : char {unk = 0, dir = 1, reg = 2, link = 3};
	private:
		const filecount id_;
		const std::string path_;
		const struct stat &stat_;
	public:
		filenode(const filecount id, const std::string &path, const struct stat &s) : id_{id}, path_{path}, stat_{s} { }
		filecount id() const { return id_; }
		const std::string &path() const { return path_; }
		const struct stat &stat() const { return stat_; }
	};

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

	class writer
	{
	public:
		enum class linkpolicy { process, follow, skip };
	private:
		const std::string root_, fullroot_;
		bool debug_;
		linkpolicy linkpol_;
		std::unordered_map<std::string, std::string> volmeta_;
		std::vector<std::string> nodemeta_;
		std::function<std::vector<std::string>(const filenode &)> metagen_;
		std::istream *userdata_;
		std::fstream headf_, contf_, idxf_;
		filecount nfile_;
		linkmgr links_;
		std::string randext_;
		void writestring(const std::string &s, std::ostream &out);
		filecount recursive_process(const std::string &path, filecount parent, std::fstream &contout, std::fstream &idxout);
	public:
		writer(const std::string &root, linkpolicy links = linkpolicy::process, bool debug = false) : root_{root},
			fullroot_{util::realpath(util::resolve(std::string{getenv("PWD")}, root_))}, debug_{debug}, linkpol_{links}, volmeta_{},
			nodemeta_{}, metagen_{[](const filenode &n) { return std::vector<std::string>{}; }}, userdata_{nullptr}, nfile_{},
			links_{fullroot_}, randext_{"-" + util::t2s(::getpid()) + ".zsr.tmp"} { }
		void userdata(std::istream &data) { userdata_ = &data; userdata_->exceptions(std::ios::badbit); }
		void volume_meta(const std::unordered_map<std::string, std::string> data) { volmeta_ = data; }
		void node_meta(const std::vector<std::string> keys, std::function<std::vector<std::string>(const filenode &)> generator) { nodemeta_ = keys; metagen_ = generator; }
		void node_meta(const std::vector<std::string> keys, std::function<std::unordered_map<std::string, std::string>(const filenode &)> generator);
		void write_body(std::string contname = "", std::string idxname = "");
		void write_header(std::string tmpfname = "");
		void combine(std::ofstream &out);
		void write(std::ofstream &out);
	};
}

#endif
