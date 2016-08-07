#include "Volume.h"

const std::string Volume::metadir{"_meta"};
const std::string Volume::default_icon{"/rsrc/img/volume.svg"};
std::default_random_engine dre{std::random_device{}()};

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

Volume::Volume(const std::string &fname) : id_{}, archive_{new zsr::archive{std::move(std::ifstream{fname})}}, info_{}, dbfname_{}, indexed_{false}, titles_{archive_->userdata()} // FIXME Unsafe assumption about initialization order of archive_ and titles_
{
	id_ = util::basename(fname).substr(0, util::basename(fname).size() - 4);
	info_ = archive_->gmeta();
	if (info_.count("home") == 0) throw zsr::badzsr{"Missing home location in info file"};
	// TODO Check whether there are metadata and set indexed_ appropriately
}

Volume::~Volume()
{
	//if (util::isdir(dbfname_)) std::experimental::filesystem::remove_all(std::experimental::filesystem::path{dbfname_});
	if (indexed_ && util::isdir(dbfname_)) util::rm_recursive(dbfname_);
}

http::doc Volume::get(std::string path)
{
	if (! archive_->check(path)) throw error{"Not Found", "The requested path " + path + " was not found in this volume"};
	std::ostringstream contentss{};
	contentss << archive_->get(path).open(); // TODO What if it's a really big file?  Can we set up the infrastructure for multiple calls to Mongoose printf?
	std::string content = contentss.str();
	archive_->reap();
	return http::doc{util::mimetype(path, content), content};
}

std::string Volume::shuffle() const
{
	std::vector<zsr::filecount> files{};
	files.reserve(archive_->size());
	for (zsr::iterator n = archive_->index(); n; n++) if (! n.isdir() && n.meta("title") != "") files.push_back(n.id());
	std::uniform_int_distribution<int> dist{0, static_cast<int>(files.size() - 1)};
	return archive_->index(files[dist(dre)]).path();
}

std::vector<Result> Volume::search(const std::string &query, int nres, int prevlen)
{
	throw error{"Search Failed", "Content search is currently not implemented"};
	/*if (! indexed()) throw error{"Search Failed", "This volume is not indexed, so it cannot be searched"};
	Xapian::Enquire enq{index_};
	Xapian::QueryParser parse{};
	parse.set_database(index_);
	parse.set_stemmer(Xapian::Stem{lang_});
	parse.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
	enq.set_query(parse.parse_query(util::utf8lower(query)));
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
	return ret;*/
}

std::unordered_map<std::string, std::string> Volume::complete(const std::string &qstr)
{
	std::unordered_map<std::string, std::string> ret;
	for (const zsr::filecount &idx : titles_.search(util::utf8lower(qstr)))
	{
		zsr::iterator n = archive_->index(idx);
		ret[n.meta("title")] = n.path();
	}
	return ret;
}

std::string Volume::info(const std::string &key) const
{
	if (! info_.count(key)) throw std::runtime_error{"Requested nonexistent info \"" + key + "\" from volume " + id_};
	return info_.at(key);
}

std::unordered_map<std::string, std::string> Volume::tokens(std::string member)
{
	std::unordered_map<std::string, std::string> ret = info_;
	if (archive_->check(util::pathjoin({metadir, "favicon.png"}))) ret["icon"] = http::mkpath({"content", id(), metadir, "favicon.png"});
	else ret["icon"] = default_icon;
	if (member != "")
	{
		ret["member"] = member;
		if (archive_->check(member)) ret["title"] = util::to_htmlent(archive_->get(member).meta("title"));
	}
	ret["search"] = "";
	ret["id"] = id();
	ret["live"] = "";
	if (info_.count("origin") && member != "")
	{
		std::vector<std::string> params = util::strsplit(info_.at("origin"), ';');
		if (params.size() == 3) ret["live"] = params[0] + "/" +  std::regex_replace(member, std::regex{params[1]}, params[2]);
		else if (params.size() >= 1) ret["live"] = params[0] + "/" + member;
	}
	return ret;
}

void Volmgr::refresh()
{
	if (! dir_.size()) throw std::runtime_error{"Tried to refresh uninitialized volume list"};
	catorder_.clear();
	for (const std::string &file : util::ls(dir_, "\\.zsr$")) // Pass through excpetions
	{
		if (util::isdir(util::pathjoin({dir_, file}))) continue;
		std::string volid = file.substr(0, file.size() - 4);
		mapping_[volid] = "";
	}
	std::ifstream catin{util::pathjoin({dir_, "categories.txt"})};
	if (! catin) return;
	std::string line{};
	unsigned int linen = 0;
	while (std::getline(catin, line))
	{
		linen++;
		if (line == "") continue;
		std::vector<std::string> tok = util::strsplit(line, ':');
		if (tok.size() != 3)
		{
			std::cerr << "Couldn't parse category file line " << linen << "\n";
			continue;
		}
		categories_[tok[0]] = std::make_pair(tok[1], false);
		std::vector<std::string> conts = util::strsplit(tok[2], ' ');
		int cnt = 0;
		for (const std::string &cont : conts) if (mapping_.count(cont))
		{
			mapping_[cont] = tok[0];
			cnt++;
		}
		if (cnt) catorder_.push_back(tok[0]);
	}

}

std::vector<std::string> &Volmgr::categories()
{
	return catorder_;
}

std::unordered_map<std::string, std::string> Volmgr::tokens(const std::string &cat)
{
	if (! categories_.count(cat)) return {{"id", ""}, {"name", ""}};
	return {{"id", cat}, {"name", util::to_htmlent(categories_[cat].first)}};
}

std::unordered_set<std::string> Volmgr::load(const std::string &cat)
{
	std::unordered_set<std::string> ret{};
	if (categories_.count(cat)) categories_[cat].second = true;
	for (const std::pair<const std::string, std::string> &vol : mapping_) if (vol.second == cat)
	{
		try
		{
			volumes_.emplace(vol.first, Volume{util::pathjoin({dir_, vol.first + ".zsr"})});
			ret.insert(vol.first);
		}
		catch (zsr::badzsr &e) { std::cerr << vol.first << ": " << e.what() << "\n"; }
	}
	return ret;
}

bool Volmgr::loaded(const std::string &cat)
{
	if (categories_.count(cat)) return categories_[cat].second;
	return false;
}

void Volmgr::unload(const std::string &cat)
{
	if (categories_.count(cat)) categories_[cat].second = false;
	for (const std::pair<const std::string, std::string> &vol : mapping_)
		if (vol.second == cat) volumes_.erase(vol.first);
}

bool Volmgr::check(const std::string &name)
{
	return mapping_.count(name);
}

Volume &Volmgr::get(const std::string &name)
{
	if (! check(name)) throw std::runtime_error{"No such volume found"};
	if (! volumes_.count(name)) load(mapping_[name]);
	return volumes_.at(name);
}


