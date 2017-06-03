#ifndef ZSR_VOLUME_H
#define ZSR_VOLUME_H
#include <string>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <set>
#include <regex>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "util/util.h"
#include "util/lua.h"
#include "zsr.h"
#include "search.h"
#include "build/fileinclude.h"
#ifdef ZSR_USE_XAPIAN
#include <xapian.h>
#endif

namespace htmlutil
{
	std::string strings(const std::string &content);

	std::string words(const std::string &content);

	std::string title(const std::string &content, const std::string &def, const std::string &encoding = "", const std::regex &process = std::regex{});
}

class Volwriter
{
private:
#ifdef ZSR_USE_XAPIAN
	class Xapwriter
	{
	private:
		const static std::string xaptmpd;
		const static std::string xaptmpf;
		Xapian::WritableDatabase db;
		Xapian::TermGenerator indexer;
	public:
		static std::string getlang(const std::unordered_map<std::string, std::string> &meta);
		void xapian_rethrow(Xapian::Error &e);
		Xapwriter() : db{}, indexer{} { }
		void init(const std::string &lang = "english");
		void add(const std::string &content, const std::string &title, zsr::filecount id);
		void write(std::ostream &out);
	};
#endif
	const std::string indir;
	zsr::writer archwriter;
	rsearch::disktree_writer searchwriter;
	lua::exec info;
	std::unordered_map<std::string, std::string> volmeta;
	static const std::string &lua_preamble, default_indexer;
#ifdef ZSR_USE_XAPIAN
	Xapwriter xap;
#endif
	std::vector<std::string> meta(const zsr::writer::filenode &n);
public:
	Volwriter(const std::string &srcdir, zsr::writer::linkpolicy linkpol);
	Volwriter(const Volwriter &orig) = delete;
	Volwriter(Volwriter &&orig) = delete;
	void write(std::ofstream &out);
};

struct Result
{
	std::string url;
	std::string title;
	int relevance;
	std::string preview;
};

class Volume
{
public:
	class error : public std::runtime_error
	{
	private:
		std::string header_, body_;
	public:
		error(const std::string &header, const std::string &body) : runtime_error{body}, header_{header}, body_{body} { }
		const std::string &header() const { return header_; }
		const std::string &body() const { return body_; }
	};
	const static std::string metadir;
	const static std::string default_icon;
private:
	std::string id_;
	std::unique_ptr<zsr::archive> archive_;
	std::unordered_map<std::string, std::string> info_;
	rsearch::disktree titles_;
#ifdef ZSR_USE_XAPIAN
	Xapian::Database index_;
	int xapfd_;
#endif
public:
	static void create(const std::string &srcdir, const std::string &destdir, const std::string &id, const std::unordered_map<std::string, std::string> &info);
	Volume(const std::string &fname, const std::string &id = "");
	Volume(const Volume &orig) = delete;
	Volume(Volume &&orig) : id_{orig.id_}, archive_{std::move(orig.archive_)}, info_{std::move(orig.info_)}, titles_{std::move(orig.titles_)}
#ifdef ZSR_USE_XAPIAN
	, index_{std::move(orig.index_)}, xapfd_{orig.xapfd_} { orig.xapfd_ = -1; }
#else
	{ }
#endif
	const std::string &id() const { return id_; }
	const zsr::archive &archive() const { return *archive_; }
	std::pair<std::string, std::string> get(std::string path);
	std::string shuffle() const;
	std::vector<Result> search(const std::string &query, int nres, int prevlen);
	std::unordered_map<std::string, std::string> complete(const std::string &query, int max = 0);
	std::string quicksearch(std::string query);
	std::string info(const std::string &key) const;
	std::unordered_map<std::string, std::string> tokens(std::string member = "");
	virtual ~Volume() {
#ifdef ZSR_USE_XAPIAN
		if (xapfd_ != -1) close(xapfd_);
#endif
	}
};

class Volmgr
{
private:
	static unsigned int unique_id;
	std::string dir_;
	std::vector<std::string> catorder_;
	std::unordered_map<std::string, std::pair<std::string, bool>> categories_;
	std::unordered_map<std::string, std::string> mapping_;
	std::unordered_map<std::string, Volume> volumes_;
public:
	void refresh();
	Volmgr() : dir_{} { }
	Volmgr(const std::string &dir) : dir_{dir} { }
	void init(const std::string &dir) { dir_ = dir; refresh(); }
	Volmgr(const Volmgr &orig) = delete;
	Volmgr(Volmgr &&orig) : dir_{std::move(orig.dir_)}, catorder_{std::move(orig.catorder_)}, categories_{std::move(orig.categories_)}, mapping_{std::move(orig.mapping_)}, volumes_{std::move(orig.volumes_)} { }
	std::vector<std::string> &categories();
	std::unordered_map<std::string, std::string> tokens(const std::string &cat);
	std::unordered_set<std::string> load(const std::string &cat);
	bool loaded(const std::string &cat);
	void unload(const std::string &cat);
	bool check(const std::string &name);
	std::string load_external(const std::string &path);
	Volume &get(const std::string &name);
};

#endif

