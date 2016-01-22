#include "Volume.h"

const std::string Volume::metadir{"_meta"};
const std::string Volume::default_icon{"/rsrc/img/volume.svg"};

Volume Volume::create(const std::string &srcdir, const std::string &destdir, const std::string &id, const std::unordered_map<std::string, std::string> &info)
{
	//std::cout << "src = " << srcdir << "\ndest = " << destdir << "\nid = " << id << "\n";
	//for (const std::pair<const std::string, std::string> &pair : info) std::cout << "  " << pair.first << " = " << pair.second << "\n";
	// Check that srcdir and destdir exist, id is alphanumeric plus underscore, and id.zsr does not yet exist in destdir
	// Create _meta if it does not exist, create info.txt, copy the favicon there if it exists
	// If the tree is not indexed yet, index it
	// Compress the tree to the destination
	// Load the volume
	throw std::runtime_error{"This functionality is not implemented yet"};
}

Volume::Volume(const std::string &fname) : id_{}, archive_{new zsr::archive_file{std::move(std::ifstream{fname})}}, info_{}, dbfname_{}, indexed_{false}, index_{}, titles_{}
{
	id_ = util::basename(fname).substr(0, util::basename(fname).size() - 4);
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
		try
		{
			index_ = Xapian::Database{util::pathjoin({dbfname_, "index"})};
			indexed_ = true;
		}
		catch (Xapian::DatabaseCorruptError &e) { } // If the index is no good, we'll make do without
	}
	if (indexed_)
	{
		titles_ = std::unique_ptr<radix_tree<std::string, std::set<Xapian::docid>>>{new radix_tree<std::string, std::set<Xapian::docid>>{}};
		std::function<bool(char)> alnum = [](char c) { return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122); };
		for (Xapian::ValueIterator iter = index_.valuestream_begin(1); iter != index_.valuestream_end(1); iter++)
		{
			std::string title_lc = util::asciilower(*iter);
			for (std::string::size_type i = 0; i < title_lc.size(); i++) if (i == 0 || ! alnum(title_lc[i - 1]))
			{
				(*titles_)[title_lc.substr(i)].insert(iter.get_docid());
			}
		}
	}
}

Volume::~Volume()
{
	//if (util::isdir(dbfname_)) std::experimental::filesystem::remove_all(std::experimental::filesystem::path{dbfname_});
	if (indexed_ && util::isdir(dbfname_)) util::rm_recursive(dbfname_);
}

bool Volume::check(const std::string &path) const
{
	return archive_->check(path);
}

http::doc Volume::get(std::string path)
{
	if (! check(path)) throw error{"Not Found", "The requested path " + path + " was not found in this volume"};
	std::ostringstream contentss{};
	contentss << archive_->open(path); // TODO What if it's a really big file?  Can we set up the infrastructure for multiple calls to Mongoose printf?
	std::string content = contentss.str();
	archive_->reap();
	return http::doc{util::mimetype(path, content), content};
}

std::vector<Result> Volume::search(const std::string &query, int nres, int prevlen)
{
	if (! indexed()) throw error{"Search Failed", "This volume is not indexed, so it cannot be searched"};
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

std::unordered_map<std::string, std::string> Volume::complete(const std::string &qstr)
{
	std::unordered_map<std::string, std::string> ret;
	if (! indexed_) return ret;
	std::vector<radix_tree<std::string, std::set<Xapian::docid>>::iterator> res;
	titles_->prefix_match(util::asciilower(qstr), res);
	for (const radix_tree<std::string, std::set<Xapian::docid>>::iterator &iter : res) for (Xapian::docid docid : iter->second)
	{
		Xapian::Document doc = index_.get_document(docid);
		ret[doc.get_value(1)] = doc.get_data();
	}
	return ret;
}

std::string Volume::info(const std::string &key) const
{
	if (! info_.count(key)) throw std::runtime_error{"Requested nonexistent info \"" + key + "\" from volume " + id_};
	return info_.at(key);
}

std::unordered_map<std::string, std::string> Volume::tokens(optional<std::string> member)
{
	std::unordered_map<std::string, std::string> ret = info_;
	if (check(util::pathjoin({metadir, "favicon.png"}))) ret["icon"] = http::mkpath({"content", id(), metadir, "favicon.png"});
	else ret["icon"] = default_icon;
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

