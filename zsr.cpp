#include "zsr.h"

namespace zsr
{
	void writer::writestring(const std::string &s, std::ostream &out)
	{
		uint16_t len = s.size();
		serialize(out, len);
		out.write(reinterpret_cast<const char *>(&s[0]), len);
	}

	filecount writer::recursive_process(const std::string &path, filecount parent, std::ofstream &contout, std::ofstream &idxout) // TODO Needs a little refactoring
	{
		constexpr offset ptrfill{0};
		std::string fullpath = util::resolve(std::string{getenv("PWD")}, path);
		struct stat s;
		node::ntype type = node::ntype::reg;
		DIR *dir = nullptr;
		std::ifstream in{};
		std::string fulltarget{};
		try
		{
			if (lstat(path.c_str(), &s) != 0) throw std::runtime_error{"Couldn't lstat " + path};
			if ((s.st_mode & S_IFMT) == S_IFLNK)
			{
				if (linkpol_ == linkpolicy::skip) return 0;
				fulltarget = util::linktarget(fullpath);
				struct stat ls;
				if (stat(path.c_str(), &ls) != 0) throw std::runtime_error{"Broken symbolic link " + path};
				if (util::is_under(fullroot_, fulltarget) && linkpol_ == linkpolicy::process) type = node::ntype::link; // Only treat as link if destination is inside the tree
				else s = ls;
			}
			if ((s.st_mode & S_IFMT) == S_IFDIR)
			{
				dir = opendir(path.c_str());
				if (! dir) throw std::runtime_error{"Couldn't open directory " + path};
				type = node::ntype::dir;
			}
			if ((s.st_mode & S_IFMT) == S_IFREG)
			{
				in.open(path);
				if (! in) throw std::runtime_error{"Couldn't open file " + path};
			}
		}
		catch (std::runtime_error &e)
		{
			std::cout << "\r\033[K" << e.what() << "\n";
			return 0;
		}
		filecount id = nfile_++;
		logb(id << " " << path);
		//std::cout << "File " << id << " path " << path << " parent " << parent << "\n";
		if (linkpol_ == linkpolicy::process) links_.handle_dest(fullpath, id);
		offset mypos = contout.tellp();
		serialize(idxout, mypos);
		serialize(contout, parent);
		serialize(contout, type);
		writestring(util::basename(path), contout);
		if (type == node::ntype::reg)
		{
			offset fullsize = static_cast<offset>(util::fsize(path));
			const std::vector<std::string> metad = metagen_(filenode(*this, id, path));
			if (metad.size() != nodemeta_.size()) throw std::runtime_error{"Number of generated metadata does not match number of file metadata keys"};
			for (const std::string &val : metad) writestring(val, contout);
			serialize(contout, fullsize);
			std::streampos sizepos = contout.tellp();
			serialize(contout, ptrfill); // Placeholder for length
			lzma::wrbuf compressor{in};
			contout << &compressor;
			if (! contout) throw std::runtime_error{"Bad output stream when writing archive content for file " + path};
			offset len = static_cast<offset>(contout.tellp() - sizepos - sizeof(offset));
			std::streampos end = contout.tellp();
			contout.seekp(sizepos);
			serialize(contout, len);
			contout.seekp(end);
		}
		if (type == node::ntype::link)
		{
			links_.handle_src(fullpath, contout.tellp());
			constexpr filecount fcfill{0}; // To fill in later
			serialize(contout, fcfill);
		}
		if (type == node::ntype::dir)
		{
			diskmap::writer<std::string, filecount> children{};
			std::streampos childstart = contout.tellp();
			size_t maxnchild = 0;
			struct dirent *ent;
			while ((ent = readdir(dir)))
			{
				if (! (linkpol_ == linkpolicy::skip && ent->d_type == DT_LNK)) maxnchild++;
			}
			rewinddir(dir);
			maxnchild -= 2;
			contout.seekp(children.hdrsize + children.recsize * maxnchild, std::ios::cur);
			struct dirent *file;
			while ((file = readdir(dir)))
			{
				std::string fname{file->d_name};
				if (fname == "." || fname == "..") continue;
				filecount childid = recursive_process(path + util::pathsep + fname, id, contout, idxout);
				if (childid != 0) children.add(fname, childid);
			}
			if (closedir(dir)) throw std::runtime_error{"Couldn't close " + path + ": " + strerror(errno)};
			std::streampos end = contout.tellp();
			contout.seekp(childstart);
			children.write(contout);
			contout.seekp(end);
		}
		return id;
	}

	void writer::linkmgr::add(const std::string &src, const std::string &dest)
	{
		links_.push_back(linkinfo{});
		linkinfo *inserted = &links_.back();
		by_src_.insert(std::make_pair(src, inserted));
		by_dest_.insert(std::make_pair(dest, inserted));
	}

	bool writer::linkmgr::walk_add(const std::string &path, const struct stat *st, void *arg)
	{
		linkmgr *caller = (linkmgr *) arg;
		if ((st->st_mode & S_IFMT) == S_IFLNK)
		{
			std::string target = util::linktarget(path);
			if (! util::fexists(target)) return false;
			if (util::is_under(caller->root_, target)) caller->add(path, target);
			else return true;
			return false;
		}
		return true;
	}

	void writer::linkmgr::search()
	{
		util::fswalk(root_, walk_add, (void *) this, false);
	}

	void writer::linkmgr::handle_src(const std::string &path, std::streampos destpos)
	{
		std::unordered_map<std::string, linkinfo *>::iterator iter = by_src_.find(path);
		if (iter == by_src_.end()) throw std::runtime_error{"Couldn't find " + path + " in link table"};
		iter->second->destpos = destpos;
	}

	void writer::linkmgr::handle_dest(const std::string &path, filecount id)
	{
		std::pair<std::unordered_map<std::string, linkinfo *>::iterator, std::unordered_map<std::string, linkinfo *>::iterator> range = by_dest_.equal_range(path);
		for (std::unordered_map<std::string, linkinfo *>::iterator iter = range.first; iter != range.second; iter++)
		{
			iter->second->destid = id;
			iter->second->resolved = true;
		}
	}
	void writer::linkmgr::write(std::ostream &out)
	{
		size_t total = by_src_.size();
		size_t done = 0;
		for (const std::pair<const std::string, linkinfo *> &link : by_src_)
		{
			logb(++done << "/" << total);
			if (! link.second->resolved) throw std::runtime_error{"Link " + link.first + " was not resolved"};
			out.seekp(link.second->destpos);
			serialize(out, link.second->destid);
		}
	}

	void writer::write_body(const std::string &contname, const std::string &idxname)
	{
		if (linkpol_ == linkpolicy::process)
		{
			loga("Finding links");
			links_.search();
			logb(links_.size() << " links found");
		}
		loga("Writing archive body");
		contf_ = contname;
		idxf_ = idxname;
		std::ofstream contout{contname}, idxout{idxname};
		if (! contout) throw std::runtime_error{"Couldn't open " + contname + " for writing"};
		if (! idxout) throw std::runtime_error{"Couldn't open " + idxname + " for writing"};
		nfile_ = 0;
		recursive_process(root_, 0, contout, idxout);
		loga("Wrote " << nfile_ << " entries");
		if (linkpol_ == linkpolicy::process)
		{
			loga("Writing links");
			links_.write(contout);
		}
	}

	void writer::write_header(const std::string &tmpfname)
	{
		headf_ = tmpfname;
		std::ofstream out{tmpfname};
		if (! out) throw std::runtime_error{"Couldn't open " + tmpfname + " for writing"};
		std::string fileheader = archive::magic_number + std::string(sizeof(offset), '\0');
		out.write(fileheader.c_str(), fileheader.size());
		uint8_t msize = volmeta_.size();
		serialize(out, msize);
		for (const std::pair<const std::string, std::string> &pair : volmeta_)
		{
			writestring(pair.first, out);
			writestring(pair.second, out);
		}
		msize = nodemeta_.size();
		serialize(out, msize);
		for (const std::string &mkey : nodemeta_) writestring(mkey, out);
	}

	void writer::combine(std::ofstream &out)
	{
		loga("Combining archive components");
		if (! out) throw std::runtime_error{"Could not open archive output file"};
		std::ifstream header{headf_};
		std::ifstream content{contf_};
		std::ifstream index{idxf_};
		out << header.rdbuf();
		out << content.rdbuf();
		offset idxstart = static_cast<offset>(out.tellp());
		out.seekp(archive::magic_number.size());
		serialize(out, idxstart); // FIXME Endianness problems?
		out.seekp(0, std::ios_base::end);
		serialize(out, nfile_);
		out << index.rdbuf();
		if (userdata_) out << userdata_->rdbuf();
		loga("Done writing archive");
		for (const std::string &file : {headf_, contf_, idxf_}) util::rm(file);
	}

	void writer::write(std::ofstream &out)
	{
		//std::function<void(int)> sighdl = [this](int sig) { for (const std::string &file : {headf_, contf_, idxf_}) util::rm(file); };
		//struct sigaction act, oldact;
		//act.sa_flags = 0;
		//::sigemptyset(&act.sa_mask);
		//act.sa_handler = *sighdl.target<void(*)(int)>();
		//if (! ::sigaction(SIGINT, &act, &oldact));
		write_header();
		write_body();
		combine(out);
		//::sigaction(SIGINT, &oldact, nullptr);
	}

	writer::~writer()
	{
		for (const std::string &file : {headf_, contf_, idxf_}) if (util::fexists(file)) util::rm(file);
	}

	std::string node::readstring()
	{
		uint16_t len;
		deserialize(container_.in_, len);
		std::string ret{};
		ret.resize(len);
		container_.in_.read(reinterpret_cast<char *>(&ret[0]), len);
		return ret;
	}

	node::node(archive &container, offset idx): container_{container}, id_{idx}, revcheck_{container_.revcheck}
	{
		std::istream &in = container_.in_;
		uint8_t nmeta = static_cast<uint8_t>(container_.node_meta_.size());
		in.seekg(container_.idxstart_ + idx * sizeof(offset));
		offset start;
		deserialize(in, start);
		in.seekg(container_.datastart_ + start);
		deserialize(in, parent_);
		deserialize(in, type_);
		name_ = readstring();
		if (type_ == ntype::link) deserialize(in, redirect_);
		else if (type_ == ntype::reg)
		{
			for (uint8_t i = 0; i < nmeta; i++) meta_.push_back(readstring());
			deserialize(in, fullsize_);
			deserialize(in, len_);
		}
		datastart_ = in.tellg();
		//std::cerr << "Node " << id_ << ": " << name_ << "@" << std::hex << start << "[" << std::dec << len_ << "]" << "\n";
	}

	diskmap::map<std::string, filecount> node::childmap()
	{
		if (type_ == ntype::link) return follow().childmap();
		if (type_ != ntype::dir) throw std::runtime_error{"Tried to get child of non-directory"};
		container_.in_.seekg(datastart_);
		return diskmap::map<std::string, filecount>{container_.in_, revcheck_};
	}

	node node::follow(unsigned int limit, unsigned int depth)
	{
		constexpr unsigned int maxdepth = 255;
		if (type_ != ntype::link) return *this;
		if (limit && depth >= limit) return *this;
		if (! limit && depth > maxdepth) throw std::runtime_error{"Links exceed maximum depth"};
		return node{container_, redirect_}.follow(limit, depth + 1);
	}

	std::unique_ptr<node> node::parent()
	{
		if (id_ == 0) return std::unique_ptr<node>{};
		return std::unique_ptr<node>{new node{container_, parent_}};
	}

	std::string node::path()
	{
		if (id_ == 0) return "";
		std::string ppath = parent()->path();
		if (ppath != "") ppath += "/";
		return ppath + name();
	}

	std::string node::meta(const std::string &key)
	{
		return follow().meta_[container_.metaidx(key)];
	}

	std::unordered_map<std::string, filecount> node::children()
	{
		std::unordered_map<std::string, filecount> ret{};
		diskmap::map<std::string, filecount> cont = childmap();
		for (filecount i = 0; i < cont.size(); i++)
		{
			filecount fileid = cont[i];
			ret[node{container_, fileid}.name()] = fileid;
		}
		return ret;
	}

	std::unique_ptr<node> node::getchild(const std::string &name)
	{
		std::pair<bool, filecount> childid = childmap().get(name);
		if (! childid.first) return std::unique_ptr<node>{};
		return std::unique_ptr<node>{new node{container_, childid.second}};
	}

	std::streambuf *node::content()
	{
		if (type_ == ntype::link) return follow().content();
		if (type_ != ntype::reg) throw std::runtime_error{"Tried to get content of non-regular file"};
		if (! container_.open_.count(id_)) container_.open_[id_].init(container_.in_, datastart_, len_, fullsize_);
		return &container_.open_[id_];
	}

	void node::close()
	{
		container_.open_.erase(follow().id_);
	}

	void node::extract(const std::string &location)
	{
		std::string fullpath = util::pathjoin({location, name_});
		if (type_ == ntype::dir)
		{
			util::mkdir(fullpath, 0755, true);
			diskmap::map<std::string, filecount> cont = childmap();
			//std::cerr << "Node " << id_ << " name " << name_ << ": " << cont.size() << " children\n";
			for (filecount i = 0; i < cont.size(); i++) node{container_, cont[i]}.extract(fullpath);
		}
		else if (type_ == ntype::reg)
		{
			std::ofstream out{fullpath};
			if (! out) throw std::runtime_error{"Could not create file " + fullpath};
			out << content();
			out.close();
			close();
		}
		else if (type_ == ntype::link)
		{
			symlink(dest().c_str(), fullpath.c_str());
		}
	}

	node iterator::getnode() const
	{
		if (idx >= ar.size()) throw std::runtime_error("Tried to access invalid node (" + util::t2s(idx) + " ≥ " + util::t2s(ar.size()) + ")");
		return node{ar, idx};
	}

	std::string iterator::meta(const std::string &key) const
	{
		return getnode().meta(key);
	}

	std::unordered_map<std::string, filecount> iterator::children() const
	{
		if (! isdir()) throw std::runtime_error{"Can't list contents of non-directory"};
		return getnode().children();
	}

	std::streambuf *iterator::open()
	{
		node n = getnode();
		if (! n.isreg()) throw std::runtime_error{"Tried to get content of directory " + path()};
		return n.content();
	}

	iterator::operator bool() const
	{
		return idx >= 0 && idx < ar.size();
	}

	const std::string archive::magic_number{"!ZSR"};

	std::ifstream archive::default_istream_{};

	std::unique_ptr<node> archive::getnode(const std::string &path, bool except)
	{
		if (! size_)
		{
			if (except) throw std::runtime_error{"Tried to get node from empty archive"};
			return std::unique_ptr<node>{};
		}
		std::unique_ptr<node> n{new node{*this, 0}};
		//std::cerr << ">>>> " << this << " " << idxstart_ << " " << datastart_ << " " << n->name() << "|\n";
		for (std::string &item : util::strsplit(path, util::pathsep))
		{
			if (! n->isdir())
			{
				if (except) throw std::runtime_error{"Tried to get child of non-directory " + path};
				return std::unique_ptr<node>{};
			}
			if (item == "" || item == ".") continue;
			if (item == "..")
			{
				if (n->id() > 0) n = n->parent();
				continue;
			}
			n = n->getchild(item);
			if (! n)
			{
				if (except) throw std::runtime_error{"Tried to access nonexistent path " + path};
				return std::unique_ptr<node>{};
			}
		}
		return n;
	}

	unsigned int archive::metaidx(const std::string &key) const
	{
		std::vector<std::string>::const_iterator iter = std::find(node_meta_.cbegin(), node_meta_.cend(), key);
		if (iter == node_meta_.cend()) throw std::runtime_error{"Tried to retrieve nonexistent metadata key \"" + key + "\""};
		return iter - node_meta_.cbegin();
	}

	void archive::extract(const std::string &path, const std::string &dest)
	{
		if (! util::isdir(dest)) util::mkdir(dest, 0750);
		std::unique_ptr<node> member = getnode(path);
		if (! member) throw std::runtime_error{"Member " + path + " does not exist in this archive"};
		member->extract(dest);
	}

	bool archive::check(const std::string &path)
	{
		std::unique_ptr<node> n = getnode(path);
		if (! n) return false;
		if (n->isdir()) return false;
		return true;
	}

	archive::archive(std::ifstream &&in) : in_{std::move(in)}, archive_meta_{}, node_meta_{}, open_{},  userdbuf_{}, userd_{&*userdbuf_}
	{
		if (! in_) throw badzsr{"Couldn't open archive input stream"};
		in_.seekg(0);
		std::string magic(magic_number.size(), '\0');
		in_.read(reinterpret_cast<char *>(&magic[0]), magic_number.size());
		if (magic != magic_number) throw badzsr{"File identifier incorrect"};
		offset idxstart{};
		deserialize(in_, idxstart);
		if (! in_) throw badzsr{"Premature end of file in header"};
		uint8_t nmeta;
		deserialize(in_, nmeta);
		for (uint8_t i = 0; i < nmeta; i++)
		{
			uint16_t ksize, vsize;
			std::string k{}, v{};
			deserialize(in_, ksize);
			k.resize(ksize);
			in_.read(reinterpret_cast<char *>(&k[0]), ksize);
			deserialize(in_, vsize);
			v.resize(vsize);
			in_.read(reinterpret_cast<char *>(&v[0]), vsize);
			archive_meta_[k] = v;
		}
		deserialize(in_, nmeta);
		for (uint8_t i = 0; i < nmeta; i++)
		{
			uint16_t msize;
			std::string mval{};
			deserialize(in_, msize);
			mval.resize(msize);
			in_.read(reinterpret_cast<char *>(&mval[0]), msize);
			node_meta_.push_back(mval);
		}
		datastart_ = in_.tellg();
		in_.seekg(idxstart);
		deserialize(in_, size_);
		idxstart_ = in_.tellg();
		offset test;
		deserialize(in_, test);
		std::streampos userdstart = idxstart_ + static_cast<std::streampos>(size_ * sizeof(filecount));
		in_.seekg(0, std::ios_base::end);
		userdbuf_.reset(new util::rangebuf{in_, userdstart, in_.tellg() - userdstart});
		userd_.rdbuf(&*userdbuf_);
	}
}

