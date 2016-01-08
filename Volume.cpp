#include <regex>
//#include <experimental/filesystem>
#include "Volume.h"

Volume::Volume(const std::string &fname) : id_{}, /*archive_{std::move(std::ifstream{fname})},*/ archive_{new zsr::archive_file{std::move(std::ifstream{fname})}}, info_{}, dbfname_{}, index_{}
{
	id_ = util::basename(fname).substr(0, util::basename(fname).size() - 4); // TODO Better way of getting file id
	std::ostringstream infoss{};
	infoss << archive_->open(util::pathjoin({metadir, "info.txt"}));
	std::string info = infoss.str();
	archive_->reap();
	for (const std::string &line : util::strsplit(info, '\n'))
	{
		if (line.size() == 0 || line[0] == '#') continue;
		unsigned int splitloc = line.find(":");
		if (splitloc == line.npos) continue;
		info_[line.substr(0, splitloc)] = line.substr(splitloc + 1);
	}
	if (info_.count("home") == 0) throw zsr::badzsr{"Missing home location in info file"};
	if (archive_->isdir(util::pathjoin({metadir, "index"})))
	{
		dbfname_ = "/tmp/zsridx_" + id();
		archive_->extract(util::pathjoin({metadir, "index"}), dbfname_);
		try { index_ = Xapian::Database{util::pathjoin({dbfname_, "index"})}; }
		catch (Xapian::DatabaseCorruptError &e) { } // If the index is no good, we'll make do without
	}
}

//Volume::Volume(Volume &&orig) : id_{orig.id_}, archive_{std::move(orig.archive_)}, info_{orig.info_}, dbfname_{orig.dbfname_}, index_{std::move(orig.index_)} { }

Volume::~Volume()
{
	// TODO Remove directory tree rooted at dbfname
	//if (util::isdir(dbfname_)) std::experimental::filesystem::remove_all(std::experimental::filesystem::path{dbfname});
}

bool Volume::check(const std::string &path) const
{
	return archive_->check(path);
}

http::doc Volume::get(std::string path)
{
	if (! check(path)) throw error{"Not Found", "The requested path " + path + " was not found in this volume"};
	std::ostringstream contentss{};
	contentss << archive_->open(path);
	std::string content = contentss.str();
	archive_->reap();
	return http::doc{util::mimetype(path, content), content};
}

std::vector<Result> Volume::search(const std::string &query, int nres, int prevlen)
{
	Xapian::Enquire enq{index_};
	Xapian::QueryParser parse{};
	parse.set_database(index_);
	parse.set_stemmer(Xapian::Stem{lang_});
	parse.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
	enq.set_query(parse.parse_query(query));
	Xapian::MSet matches = enq.get_mset(0, nres);
	std::vector<Result> ret{};
	for (Xapian::MSetIterator iter = matches.begin(); iter != matches.end(); iter++)
	{
		Result r;
		r.relevance = iter.get_percent();
		std::string relpath = iter.get_document().get_data();
		r.url = http::mkpath({"view", id(), relpath});
		std::ostringstream raw{};
		raw << archive_->open(relpath);
		std::string content = raw.str();
		r.title = http::title(content, util::basename(relpath));
		r.preview = http::strings(content).substr(0, prevlen);
		ret.push_back(r);
	}
	archive_->reap();
	return ret;
}

std::string Volume::info(const std::string &key) const
{
	// TODO Check presence?
	return info_.at(key);
}

std::map<std::string, std::string> Volume::tokens(optional<std::string> member)
{
	std::map<std::string, std::string> ret = info_;
	if (check(util::pathjoin({metadir, "favicon.png"}))) ret["icon"] = http::mkpath({"content", id(), metadir, "favicon.png"});
	else ret["icon"] = "/rsrc/img/volume.svg"; // TODO Don't hardcode
	if (member)
	{
		ret["view"] = http::mkpath({"view", id(), *member});
		ret["content"] = http::mkpath({"content", id(), *member});
	}
	else
	{
		ret["view"] = http::mkpath({"view", id()});
		ret["content"] = http::mkpath({"content", id()});
	}
	ret["search"] = "";
	ret["prefs"] = http::mkpath({"pref", id()});
	ret["home"] = http::mkpath({"view", id(), info("home")});
	ret["id"] = id();
	ret["live"] = "";
	if (info_.count("origin") && member)
	{
		std::vector<std::string> params = util::strsplit(info_.at("origin"), ';');
		if (params.size() == 3) ret["live"] = params[0] + "/" +  std::regex_replace(*member, std::regex{params[1]}, params[2]);
		else if (params.size() >= 1) ret["live"] = params[0] + "/" + *member;
	}
	return ret;
}

