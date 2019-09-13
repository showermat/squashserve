#include "Volume.h"

const std::string Volume::metadir{"_meta"};
const std::string Volume::default_icon{"/rsrc/img/volume.svg"};

namespace htmlutil
{
	std::string strings(const std::string &content)
	{
		//std::regex bodyre{"(<body[^>]*>([\\w\\W]*)</body>)"}; // This doesn't work because of group length limit in GNU C++ regex library
		std::regex bodyopen{"<body[^>]*>", std::regex_constants::icase};
		std::regex bodyclose{"</body>", std::regex_constants::icase};
		std::smatch bodymatch{};
		if (! std::regex_search(content, bodymatch, bodyopen)) return util::to_htmlent(content);
		std::string::size_type startidx = bodymatch.position() + bodymatch.length();
		if (! std::regex_search(content, bodymatch, bodyclose)) return util::to_htmlent(content);
		std::string::size_type endidx = bodymatch.position();
		if (endidx < startidx) return content;
		std::string body = content.substr(startidx, endidx - startidx);
		body = std::regex_replace(body, std::regex{"<style>[\\w\\W]*</style>", std::regex_constants::icase}, ""); // FIXME Will wipe out anything between two script or style blocks
		body = std::regex_replace(body, std::regex{"<script>[\\w\\W]*</script>", std::regex_constants::icase}, "");
		body = std::regex_replace(body, std::regex{"<[^>]+>"}, "");
		body = std::regex_replace(body, std::regex{"[\\s\\n]+"}, " ");
		return body;
	}

	std::string words(const std::string &content) // Sloooooooow
	{
		std::string input = strings(content);
		std::regex spacere{"[\\s 　]+"};
		std::sregex_token_iterator iter{input.begin(), input.end(), spacere, -1};
		std::unordered_set<std::string> words{};
		for (; iter != std::sregex_token_iterator{}; iter++)
		{
			std::string word = std::regex_replace((std::string) *iter, std::regex{"^\\W*(\\w.*?\\w)\\W*$"}, "$1");
			if (word.size() < 4 || word.size() > 30) continue;
			//if (! std::regex_match(word, std::regex{"^[a-zA-Z0-9'-_]+$"})) continue;
			words.insert(util::utf8lower(word));
		}
		std::ostringstream ret{};
		for (const std::string &word : words) ret << word << " ";
		return ret.str();
	}
}

#ifdef ZSR_USE_XAPIAN
const std::string Volwriter::Xapwriter::xaptmpd{"search.dir.tmp"};
const std::string Volwriter::Xapwriter::xaptmpf{"search.zsr.tmp"};

std::string Volwriter::Xapwriter::getlang(const std::unordered_map<std::string, std::string> &meta)
{
	const std::string deflang = "english";
	const std::vector<std::string> supvec = util::strsplit(Xapian::Stem::get_available_languages(), ' ');
	const std::set<std::string> supported{supvec.begin(), supvec.end()};
	const std::unordered_map<std::string, std::string> iso639_1{{"hy", "armenian"}, {"eu", "basque"}, {"ca", "catalan"}, {"da", "danish"}, {"nl", "dutch"}, {"en", "english"}, {"fi", "finnish"},
		{"fr", "french"}, {"de", "german"}, {"hu", "hungarian"}, {"it", "italian"}, {"nb", "norwegian"}, {"nn", "norwegian"}, {"no", "norwegian"}, {"pt", "portugese"}, {"ro", "romanian"},
		{"ru", "russian"}, {"es", "spanish"}, {"sv", "swedish"}, {"tr", "turkish"}}, iso639_2{{"eng", "english"}, {"arm", "armenian"}, {"hye", "armenian"}, {"baq", "basque"}, {"eus", "basque"},
		{"cat", "catalan"}, {"dan", "danish"}, {"dut", "dutch"}, {"nld", "dutch"}, {"fin", "finnish"}, {"fre", "french"}, {"fra", "french"}, {"ger", "german"}, {"deu", "german"},
		{"hun", "hungarian"}, {"ita", "italian"}, {"nno", "norwegian"}, {"nob", "norwegian"}, {"nor", "norwegian"}, {"por", "portugese"}, {"rum", "romanian"}, {"ron", "romanian"},
		{"rus", "russian"}, {"spa", "spanish"}, {"swe", "swedish"}, {"tur", "turkish"}};
	if (! meta.count("language")) return deflang;
	std::string lang = util::utf8lower(meta.at("language"));
	if (supported.count(lang)) return lang;
	if (iso639_1.count(lang) && supported.count(iso639_1.at(lang))) return iso639_1.at(lang);
	if (iso639_2.count(lang) && supported.count(iso639_2.at(lang))) return iso639_2.at(lang);
	return deflang;
}

void Volwriter::Xapwriter::xapian_rethrow(Xapian::Error &e)
{
	std::string syserr{};
	if (e.get_error_string()) syserr = ": " + std::string{e.get_error_string()};
	throw std::runtime_error{"Xapian error: " + e.get_msg() + syserr};
}

void Volwriter::Xapwriter::init(const std::string &lang)
{
	db = Xapian::WritableDatabase{xaptmpd};
	indexer.set_stemmer(Xapian::Stem(lang));
}

void Volwriter::Xapwriter::add(const std::string &content, const std::string &title, zsr::filecount id) try
{
	Xapian::Document doc{};
	doc.add_value(1, std::string{reinterpret_cast<char *>(&id), sizeof(id)});
	indexer.set_document(doc);
	indexer.index_text_without_positions(htmlutil::strings(content));
	//for (const std::string &word : util::strsplit(htmlutil::strings(content), ' ')) if (word.size() > 0 && word.size() < 32) doc.add_term(word);
	db.add_document(doc);
}
catch (Xapian::Error &e) { xapian_rethrow(e); }

void Volwriter::Xapwriter::write(std::ostream &out) try
{
	db.commit();
	db.close();
	Xapian::Database{xaptmpd}.compact(xaptmpf, Xapian::DBCOMPACT_SINGLE_FILE); // | Xapian::Compactor::FULLER); // Adding FULLER compaction triggers a bug in glass "File too large"
	std::ifstream in{xaptmpf};
	in.exceptions(std::ios_base::badbit);
	out << in.rdbuf();
	util::rm_recursive(xaptmpd);
	util::rm(xaptmpf);
}
catch (Xapian::Error &e) { xapian_rethrow(e); }
#endif

const std::string &Volwriter::lua_preamble{fileinclude::loaded_file("vollib.lua")};

const std::string Volwriter::default_metagen{"function meta(path, ftype) return default_meta(path, ftype) end"};

const std::string Volwriter::default_metanames{"metanames = {\"title\", \"type\"}"};

std::vector<std::string> Volwriter::meta(const zsr::filenode &n)
{
	int ftype = 0;
	if ((n.stat().st_mode & S_IFMT) == S_IFDIR) ftype = 1;
	else if ((n.stat().st_mode & S_IFMT) == S_IFREG) ftype = 2;
	else if ((n.stat().st_mode & S_IFMT) == S_IFLNK) ftype = 3;
	std::unordered_map<std::string, std::string> values = info.calltbl("meta", n.path(), ftype).tomap<std::string, std::string>();
	if (ftype == 2 || ftype == 3)
	{
		if (values.count("title") && values.at("title") != "")
		{
			searchwriter.add(values.at("title"), n.id());
#ifdef ZSR_USE_XAPIAN
			xap.add(content, values.at("title"), n.id());
#endif
		}
	}
	std::vector<std::string> ret{};
	if (ftype != 2) return ret; // The return value is currently only used if the file is a regular file...we may update this to include links
	if (! values.count("type")) values["type"] = util::mimetype(n.path());
	for (const std::string &item : metanames) ret.push_back(values.count(item) ? values.at(item) : "");
	return ret;
}

Volwriter::Volwriter(const std::string &srcdir, zsr::writer::linkpolicy linkpol, bool dbg) : debug{dbg}, indir{srcdir}, archwriter{indir, linkpol, dbg}, searchwriter{}, info{}, volmeta{}, metanames{}
#ifdef ZSR_USE_XAPIAN
, xap{}
#endif
{
	info.loadstr(lua_preamble);
	std::function<std::string(std::string, std::string, std::string)> iconv = [](std::string in, std::string from, std::string to) {
		try { return util::conv(in, from, to); }
		catch (std::runtime_error &e) { std::cout << "\n" << e.what() << "\n"; return std::string{}; }
	};
	info.expose(iconv, "iconv");
	std::function<std::string(std::string)> mimetype = [](std::string path) {
		return util::mimetype(path);
	};
	info.expose(mimetype, "mimetype");
	std::function<std::string(std::string)> html_title = [](std::string path) {
		std::string ret = html::doc::read_file(path).title();
		if (ret.size() > 0) return ret;
		ret = util::basename(path);
		std::string::size_type extpos = ret.rfind(".");
		if (extpos != std::string::npos) ret = ret.substr(0, extpos);
		return ret;
	};
	info.expose(html_title, "html_title");
	std::function<std::string(std::string)> html_encoding = [](std::string path) {
		return html::doc::read_file(path).encoding();
	};
	info.expose(html_encoding, "html_encoding");
	info.load(util::pathjoin({indir, Volume::metadir, "info.lua"}));
	if (! info.exists("meta")) info.loadstr(default_metagen);
	if (! info.exists("metanames")) info.loadstr(default_metanames);
	volmeta = info.table_iter("params").tomap<std::string, std::string>();
	archwriter.volume_meta(volmeta);
	std::function<std::vector<std::string>(const zsr::filenode &)> meta_callback = [this](const zsr::filenode &n) { return this->meta(n); };
	metanames = info.table_iter("metanames").tovec<std::string>();
	archwriter.node_meta(metanames, meta_callback);
#ifdef ZSR_USE_XAPIAN
	if (! volmeta.count("stem_lang")) volmeta["stem_lang"] = Xapwriter::getlang(volmeta);
	xap.init(volmeta["stem_lang"]);
#endif
}

void Volwriter::write(std::ofstream &out)
{
	loga("Started");
	std::string searchfname{"titles-" + util::t2s(::getpid()) + ".zsr.tmp"};
	std::fstream searchf{searchfname, std::ios_base::in | std::ios_base::out | std::ios_base::trunc};
	if (! searchf) throw std::runtime_error{"Couldn't open " + searchfname + " for writing"};
	searchf.exceptions(std::ios_base::badbit);
	if (! debug) util::rm(searchfname);
	archwriter.write_header();
	archwriter.write_body();
#ifdef ZSR_USE_XAPIAN
	zsr::offset xapstart{0};
	zsr::serialize(searchf, xapstart);
#endif
	searchwriter.write(searchf);
#ifdef ZSR_USE_XAPIAN
	xapstart = searchf.tellp();
	loga("Writing search index");
	xap.write(searchf);
	searchf.seekp(0);
	zsr::serialize(searchf, xapstart);
#endif
	searchf.seekg(0);
	archwriter.userdata(searchf);
	archwriter.combine(out);
}

void Volume::create(const std::string &srcdir, const std::string &destdir, const std::string &id, const std::unordered_map<std::string, std::string> &info)
{
	//std::cout << "src = " << srcdir << "\ndest = " << destdir << "\nid = " << id << "\n";
	//for (const std::pair<const std::string, std::string> &pair : info) std::cout << "  " << pair.first << " = " << pair.second << "\n";
	const std::regex validmeta{"^[A-Za-z_]+$"};
	if (! util::isdir(srcdir)) throw std::runtime_error{"Source directory " + srcdir + " is not accessible"};
	if (! util::isdir(destdir)) throw std::runtime_error{"Destination directory " + destdir + " is not accessible"};
	if (! util::isdir(util::pathjoin({srcdir, metadir})))
	{
		util::mkdir(util::pathjoin({srcdir, metadir}));
		std::string infofname{util::pathjoin({srcdir, metadir, "info.lua"})};
		std::ofstream infofile{infofname};
		if (! infofile) throw std::runtime_error{"Could not open information file " + infofname + " for writing"};
		infofile.exceptions(std::ios_base::badbit);
		infofile << "params = {\n";
		for (const std::pair<const std::string, std::string> &item : info)
		{
			if (! std::regex_match(item.first, validmeta)) throw std::runtime_error{"Metadata name “" + item.first + "” contains invalid characters"};
			infofile << "\t" << item.first << " = \"" << util::gsub(item.second, "\"", "\\\"") << "\",\n";
		}
		infofile << "}\n";
		if (info.count("favicon")) util::cp(info.at("favicon"), util::pathjoin({srcdir, metadir, "favicon.png"}));
	}
	std::string outpath = util::pathjoin({destdir, id + ".zsr"});
	std::ofstream out{outpath};
	if (! out) throw std::runtime_error{"Could not open output file " + outpath};
	out.exceptions(std::ios_base::badbit);
	Volwriter{srcdir, zsr::writer::linkpolicy::process}.write(out);
}

Volume::Volume(const std::string &fname, const std::string &id) : id_{id}, archive_{new zsr::archive{fname}}, info_{}, titles_{}
#ifdef ZSR_USE_XAPIAN
, index_{}, xapfd_{-1}
#endif
{
	if (! id_.size()) id_ = util::basename(fname).substr(0, util::basename(fname).size() - 4);
	titles_.init(archive_->userdata());
#ifdef ZSR_USE_XAPIAN
	zsr::offset xapstart;
	zsr::deserialize(userd, xapstart);
	titles_.init(userd, sizeof(zsr::offset));
	xapstart += dynamic_cast<util::rangebuf *>(userd.rdbuf())->offset();
	xapfd_ = ::open(fname.c_str(), O_RDONLY);
	if (xapfd_ == -1) throw std::runtime_error{"Couldn't open archive for search"};
	::lseek(xapfd_, xapstart, SEEK_SET);
	index_ = Xapian::Database{xapfd_};
#endif
	info_ = archive_->gmeta();
	if (info_.count("home") == 0) throw zsr::badzsr{"Missing home location"};
}

std::pair<std::string, std::string> Volume::get(std::string path)
{
	if (! archive_->check(path)) throw error{"Not Found", "The requested path " + path + " was not found in this volume"};
	std::ostringstream contentss{};
	zsr::node file = archive_->get(path);
	contentss << file.content().rdbuf(); // TODO What if it's a really big file?  Can we set up the infrastructure for multiple calls to Mongoose printf?
	std::string content = contentss.str();
	return std::make_pair(file.meta("type"), content);
}

std::string Volume::shuffle() const
{
	constexpr int tries = 32;
	for (int i = 0; i < tries; i++)
	{
		zsr::node n = archive_->index(util::randint<zsr::filecount>(0, archive_->size() - 1));
		if (! n.isreg() || n.meta("title") == "") continue;
		return n.path();
	}
	std::vector<zsr::filecount> files{};
	files.reserve(archive_->size());
	for (zsr::filecount i = 0; i < archive_->size(); i++)
	{
		zsr::node n = archive_->index(i);
		if (! n.isdir() && n.meta("title") != "") files.push_back(n.id());
	}
	if (files.size() == 0) return "";
	return archive_->index(files[util::randint<zsr::filecount>(0, files.size() - 1)]).path();
}

std::vector<Result> Volume::search(const std::string &query, int nres, int prevlen)
{
#ifdef ZSR_USE_XAPIAN
	Xapian::Enquire enq{index_};
	Xapian::QueryParser parse{};
	parse.set_database(index_);
	parse.set_stemmer(Xapian::Stem{info_["stem_lang"]});
	parse.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
	enq.set_query(parse.parse_query(util::utf8lower(query)));
	Xapian::MSet matches = enq.get_mset(0, nres);
	std::vector<Result> ret{};
	for (Xapian::MSetIterator iter = matches.begin(); iter != matches.end(); iter++)
	{
		Result r;
		r.relevance = iter.get_percent();
		std::string idenc = iter.get_document().get_value(1);
		zsr::filecount *id = reinterpret_cast<zsr::filecount *>(&idenc[0]);
		zsr::node node = archive_->index(*id);
		r.url = node.path();
		std::ostringstream raw{};
		raw << node.content();
		std::string content = raw.str();
		r.title = node.meta("title");
		r.preview = htmlutil::strings(content).substr(0, prevlen); // TODO Find and highlight search terms and display most relevant portions
		ret.push_back(r);
	}
	return ret;
#else
	throw error{"Search Failed", "Content search is currently not implemented"};
#endif

}

int Volume::completions(const std::string &query)
{
	if (! query.size()) return 0;
	std::vector<std::string> words = util::strsplit(util::utf8lower(query), ' ');
	std::unordered_set<zsr::filecount> matches = titles_.search(words.back());
	words.pop_back();
	for (const std::string &word : words) matches = util::intersection(matches, titles_.search(word));
	return matches.size();
}

std::unordered_map<std::string, std::string> Volume::complete(const std::string &query, zsr::filecount start, zsr::filecount max)
{
	// FIXME The ordering given here is suboptimal.  All the search backend can sort by is the tree path, which is lower-case and
	// potentially not the same as the article title (for example, if the file is a link).  So that's kind of useless.  It's
	// unfortunately easier for now to just fetch all the articles and sort them by file index -- if you're going to do a half-
	// baked solution, do one that doesn't take much effort.  Then we select out the slice of these for the current page, and rely
	// on the hash function for unordered_map to give the same ordering every time within the page.  I'm not sure if there's a way
	// to do this that's both better and not really difficult.
	std::unordered_map<std::string, std::string> ret{};
	if (! query.size()) return ret;
	std::vector<std::string> words = util::strsplit(util::utf8lower(query), ' ');
	std::unordered_set<zsr::filecount> matches = titles_.search(words.back());
	words.pop_back();
	for (const std::string &word : words) matches = util::intersection(matches, titles_.search(word));
	std::vector<zsr::filecount> sorted{matches.begin(), matches.end()};
	std::sort(sorted.begin(), sorted.end());
	std::vector<zsr::node> nodes{};
	for (zsr::filecount i = start; i < sorted.size() && (max == 0 || i < start + max); i++) nodes.push_back(archive_->index(sorted[i]));
	std::vector<zsr::node> linksfirst{};
	for (const zsr::node &n : nodes) if (n.type() == zsr::node::ntype::link) linksfirst.push_back(n);
	for (const zsr::node &n : nodes) if (n.type() != zsr::node::ntype::link) linksfirst.push_back(n);
	zsr::filecount cnt = 0;
	for (const zsr::node &n : linksfirst)
	{
		if (max > 0 && cnt++ >= max) break;
		ret[n.meta("title")] = n.path();
	}
	return ret;
}

std::string Volume::quicksearch(std::string query)
{
	std::function<std::list<zsr::node>(zsr::archive &, std::unordered_set<zsr::filecount>)> getnodes = [](zsr::archive &ar, std::unordered_set<zsr::filecount> idxs) {
		std::list<zsr::node> ret{};
		for (const zsr::filecount &idx: idxs) ret.push_back(ar.index(idx));
		return ret;
	};
	query = util::utf8lower(query);
	std::list<zsr::node> nodes = getnodes(*archive_, titles_.exact_search(query));
	for (const zsr::node &n : nodes) if (n.meta("title") == query) return n.path();
	for (const zsr::node &n : nodes) if (util::utf8lower(n.meta("title")) == query) return n.path();
	nodes = getnodes(*archive_, titles_.search(query));
	if (nodes.size() == 1) return nodes.begin()->path();
	for (std::list<zsr::node>::iterator iter = nodes.begin(); iter != nodes.end(); )
		if (iter->type() == zsr::node::ntype::link) iter = nodes.erase(iter);
		else iter++;
	if (nodes.size() == 1) return nodes.begin()->path();
	return "";
}

std::string Volume::info(const std::string &key) const
{
	if (! info_.count(key)) throw std::runtime_error{"Requested nonexistent info “" + key + "” from volume " + id_};
	return info_.at(key);
}

std::unordered_map<std::string, std::string> Volume::tokens(std::string member)
{
	std::unordered_map<std::string, std::string> ret = info_;
	if (archive_->check(util::pathjoin({metadir, "favicon.png"}))) ret["icon"] = "/" + util::strjoin({"content", id(), metadir, "favicon.png"}, '/');
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

unsigned int Volmgr::unique_id = 1;

void Volmgr::refresh()
{
	if (! dir_.size()) throw std::runtime_error{"Tried to refresh uninitialized volume list"};
	catorder_.clear();
	categories_.clear();
	mapping_.clear();
	volumes_.clear();
	for (const std::string &file : util::ls(dir_, "\\.zsr$")) // Pass through exceptions
	{
		if (util::isdir(util::pathjoin({dir_, file}))) continue;
		std::string volid = file.substr(0, file.size() - 4);
		mapping_[volid] = "";
	}
	std::ifstream catin{util::pathjoin({dir_, "categories.txt"})};
	if (! catin) return;
	catin.exceptions(std::ios_base::badbit);
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
	for (const std::pair<const std::string, std::string> &pair : mapping_) if (pair.second == cat)
	{
		try
		{
			if (! volumes_.count(pair.first)) volumes_.emplace(pair.first, Volume{util::pathjoin({dir_, pair.first + ".zsr"})});
			ret.insert(pair.first);
		}
		catch (zsr::badzsr &e) { std::cerr << pair.first << ": " << e.what() << "\n"; }
		catch (std::runtime_error &e) { std::cerr << pair.first << ": " << e.what() << "\n"; }
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

std::string Volmgr::load_external(const std::string &path)
{
	unsigned int volnum = unique_id++;
	std::string volid = "@ext:" + util::t2s(volnum);
	try
	{
		volumes_.emplace(volid, Volume{path, volid});
		mapping_[volid] = "";
	}
	catch (std::runtime_error &e) { throw std::runtime_error{"Could not load ZSR file " + path + ": " + e.what()}; }
	return volid;
}

Volume &Volmgr::get(const std::string &name)
{
	if (! name.size()) throw std::runtime_error{"Cannot load null volume"};
	if (! check(name)) throw std::runtime_error{"No such volume found"};
	if (! volumes_.count(name)) load(mapping_[name]);
	return volumes_.at(name);
}
