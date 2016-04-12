#ifndef VOLUME_H
#define VOLUME_H
#include <string>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <set>
#include <random>
#include <regex>
#include "lib/radix_tree.hpp" // Thanks to github/ytakano
#include "util.h"
#include "zsr.h"
#include "http.h"

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
	//std::string lang_{"english"};
	std::string id_;
	std::unique_ptr<zsr::archive> archive_;
	std::unordered_map<std::string, std::string> info_;
	std::string dbfname_;
	bool indexed_;
	std::unique_ptr<radix_tree<std::string, std::set<zsr::index>>> titles_;
public:
	static Volume newvol(const std::string fname) { return Volume{fname}; }
	static Volume create(const std::string &srcdir, const std::string &destdir, const std::string &id, const std::unordered_map<std::string, std::string> &info);
	Volume(const std::string &fname);
	Volume(const Volume &orig) = delete;
	Volume(Volume &&orig) : id_{orig.id_}, archive_{std::move(orig.archive_)}, info_{orig.info_}, dbfname_{orig.dbfname_}, indexed_{orig.indexed_}, titles_{std::move(orig.titles_)} { orig.indexed_ = false; }
	const std::string &id() const { return id_; }
	const zsr::archive &archive() const { return *archive_; }
	http::doc get(std::string path);
	std::string shuffle() const;
	bool indexed() { return indexed_; }
	std::vector<Result> search(const std::string &qstr, int nres, int prevlen);
	std::unordered_map<std::string, std::string> complete(const std::string &qstr);
	std::string info(const std::string &key) const;
	std::unordered_map<std::string, std::string> tokens(optional<std::string> member);
	virtual ~Volume();
};

#endif

