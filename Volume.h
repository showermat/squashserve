#ifndef VOLUME_H
#define VOLUME_H
#include <string>
#include <map>
#include <memory>
#include <sstream>
#include <xapian.h>
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
private:
	std::string metadir{"_meta"}; // FIXME Should be a pref, or at least static
	std::string lang_{"english"};
	std::string id_;
	//zsr::archive_file archive_;
	std::unique_ptr<zsr::archive_file> archive_;
	std::map<std::string, std::string> info_;
	std::string dbfname_;
	Xapian::Database index_;
public:
	static Volume newvol(const std::string fname) { return Volume{fname}; }
	Volume(const std::string &fname);
	Volume(const Volume &orig) = delete;
	Volume(Volume &&orig) : id_{orig.id_}, archive_{std::move(orig.archive_)}, info_{orig.info_}, dbfname_{""}, index_{orig.index_} { }
	const std::string &id() const { return id_; }
	const zsr::archive_file &archive() const { return *archive_; }
	bool check(const std::string &path) const;
	http::doc get(std::string path);
	std::vector<Result> search(const std::string &qstr, int nres, int prevlen);
	std::string info(const std::string &key) const;
	std::map<std::string, std::string> tokens(optional<std::string> member);
	virtual ~Volume();
};

#endif

