#include "zsr.h"

namespace zsr
{
	void writer::writestring(const std::string &s, std::ostream &out)
	{
		uint16_t len = s.size();
		out.write(reinterpret_cast<const char *>(&len), sizeof(uint16_t));
		out.write(reinterpret_cast<const char *>(&s[0]), len);
	}

	filecount writer::recursive_process(const std::string &path, filecount parent, std::ofstream &contout, std::ofstream &idxout) // TODO Needs a little refactoring
	{
		constexpr offset ptrfill{0};
		struct stat s;
		if (lstat(path.c_str(), &s) != 0) throw std::runtime_error{"Couldn't lstat " + path};
		node::ntype type = node::ntype::reg;
		DIR *dir = nullptr;
		std::ifstream in{};
		std::string fulltarget{};
		if ((s.st_mode & S_IFMT) == S_IFLNK)
		{
			fulltarget = util::linktarget(util::resolve(std::string{getenv("PWD")}, path));
			if (fulltarget.substr(0, fullroot_.size()) == fullroot_ && (fulltarget.size() == fullroot_.size() || fulltarget[fullroot_.size()] == util::pathsep))
				type = node::ntype::link; // Only treat as link if destination is inside the tree
			else if (stat(path.c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path};
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
		filecount id = nfile_++;
		logb(id << " " << path);
		//std::cout << "File " << id << " path " << path << " parent " << parent << "\n";
		std::streampos mypos = idxout.tellp();
		idxout.write(reinterpret_cast<const char *>(&ptrfill), sizeof(offset)); // Offset to next node
		idxout.write(reinterpret_cast<const char *>(&parent), sizeof(filecount));
		idxout.write(reinterpret_cast<const char *>(&type), sizeof(node::ntype));
		writestring(util::basename(path), idxout);
		//std::cerr << "ID " << id << " type " << static_cast<int>(type) << " parent " << parent << " name " << name << "\n";
		if (type == node::ntype::reg)
		{
			offset start = contout.tellp();
			lzma::wrbuf compressor{in};
			contout << &compressor;
			offset len = static_cast<offset>(contout.tellp()) - start;
			if (! contout) throw std::runtime_error{"Bad output stream when writing archive content for file " + path};
			idxout.write(reinterpret_cast<const char *>(&start), sizeof(offset));
			idxout.write(reinterpret_cast<const char *>(&len), sizeof(offset));
			offset fullsize = static_cast<offset>(util::fsize(path));
			idxout.write(reinterpret_cast<const char *>(&fullsize), sizeof(offset));
			const std::vector<std::string> metad = metagen_(filenode(*this, id, path));
			if (metad.size() != nodemeta_.size()) throw std::runtime_error{"Number of generated metadata does not match number of file metadata keys"};
			for (const std::string &val : metad) writestring(val, idxout);
		}
		if (type == node::ntype::link)
		{
			constexpr filecount fcfill{0}; // To fill in later
			//links_[util::relreduce(fullroot_, fulltarget)] = idxout.tellp(); // TODO links
			idxout.write(reinterpret_cast<const char *>(&fcfill), sizeof(filecount));
		}
		if (type == node::ntype::dir)
		{
			diskmap::writer<std::string, filecount> children{};
			std::streampos childstart = idxout.tellp();
			size_t nchild = 0;
			while (readdir(dir)) nchild++;
			rewinddir(dir);
			nchild -= 2;
			std::streampos nextpos = idxout.tellp() + static_cast<std::streampos>(children.hdrsize + children.recsize * nchild);
			idxout.seekp(mypos);
			offset nextoff = nextpos - mypos - sizeof(offset);
			idxout.write(reinterpret_cast<const char *>(&nextoff), sizeof(offset));
			idxout.seekp(nextpos);
			struct dirent *file;
			while ((file = readdir(dir)))
			{
				std::string fname{file->d_name};
				if (fname == "." || fname == "..") continue;
				//cur->add_child(recursive_add(path + util::pathsep + fname, cur, metagen));
				filecount childid = recursive_process(path + util::pathsep + fname, id, contout, idxout);
				children.add(fname, childid);
				//children.add(fname, recursive_process(path + util::pathsep + fname, id, contout, idxout));
			}
			if (closedir(dir)) throw std::runtime_error{"Couldn't close " + path + ": " + strerror(errno)};
			std::streampos end = idxout.tellp();
			idxout.seekp(childstart);
			children.write(idxout);
			idxout.seekp(end);
		}
		else
		{
			std::streampos nextpos = idxout.tellp();
			idxout.seekp(mypos);
			offset nextoff = nextpos - mypos - sizeof(offset);
			idxout.write(reinterpret_cast<const char *>(&nextoff), sizeof(offset));
			idxout.seekp(nextpos);
		}
		return id;
	}

	void writer::write_body(const std::string &contname, const std::string &idxname)
	{
		loga("Writing archive body");
		contf_ = contname;
		idxf_ = idxname;
		std::ofstream contout{contname}, idxout{idxname};
		if (! contout) throw std::runtime_error{"Couldn't open " + contname + " for writing"};
		if (! idxout) throw std::runtime_error{"Couldn't open " + idxname + " for writing"};
		nfile_ = 0;
		recursive_process(root_, 0, contout, idxout);
		loga("Wrote " << nfile_ << " entries");
		//loga("Resolving symbolic links");
		//loga(links_.size() << " links resolved");
	}

	void writer::write_header(const std::string &tmpfname)
	{
		headf_ = tmpfname;
		std::ofstream out{tmpfname};
		if (! out) throw std::runtime_error{"Couldn't open " + tmpfname + " for writing"};
		std::string fileheader = archive::magic_number + std::string(sizeof(offset), '\0');
		out.write(fileheader.c_str(), fileheader.size());
		uint8_t msize = volmeta_.size();
		out.write(reinterpret_cast<char *>(&msize), sizeof(uint8_t));
		for (const std::pair<const std::string, std::string> &pair : volmeta_)
		{
			writestring(pair.first, out);
			writestring(pair.second, out);
		}
		msize = nodemeta_.size();
		out.write(reinterpret_cast<char *>(&msize), sizeof(uint8_t));
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
		out.write(reinterpret_cast<char *>(&idxstart), sizeof(offset)); // FIXME Endianness problems?
		out.seekp(0, std::ios_base::end);
		out.write(reinterpret_cast<char *>(&nfile_), sizeof(filecount));
		out << index.rdbuf();
		if (userdata_) out << userdata_->rdbuf();
		loga("Done writing archive");
		for (const std::string &file : {headf_, contf_, idxf_}) util::rm(file);
	}

	void writer::write(std::ofstream &out)
	{
		write_header();
		write_body();
		combine(out);
	}

	std::string node::nodeinfo::readstring()
	{
		uint16_t len;
		in_.read(reinterpret_cast<char *>(&len), sizeof(uint16_t));
		std::string ret{};
		ret.resize(len);
		in_.read(reinterpret_cast<char *>(&ret[0]), len);
		return ret;
	}

	node::nodeinfo::nodeinfo(std::istream &in, std::function<std::string(const filecount &)> &revcheck, uint8_t nmeta) : in_{in}, nmeta_{nmeta}, revcheck_{revcheck}
	{
		in.read(reinterpret_cast<char *>(&parent), sizeof(filecount));
		in.read(reinterpret_cast<char *>(&type), sizeof(ntype));
		name = readstring();
		if (type == ntype::dir)
		{
			childrenstart_ = in.tellg();
			return;
		}
		if (type == ntype::link)
		{
			in.read(reinterpret_cast<char *>(&redirect), sizeof(filecount));
			return;
		}
		in.read(reinterpret_cast<char *>(&start), sizeof(offset));
		in.read(reinterpret_cast<char *>(&len), sizeof(offset));
		in.read(reinterpret_cast<char *>(&fullsize), sizeof(offset));
		metastart_ = in.tellg();
	}

	std::vector<std::string> node::nodeinfo::meta()
	{
		std::vector<std::string> ret{};
		if (metastart_ == 0) return ret;
		in_.seekg(metastart_);
		for (uint8_t i = 0; i < nmeta_; i++) ret.push_back(readstring());
		return ret;
	}
	
	node &node::follow(unsigned int depth)
	{
		constexpr unsigned int maxdepth = 255;
		nodeinfo inf = readinfo();
		if (inf.type != ntype::link) return *this;
		if (depth > maxdepth) throw std::runtime_error{"Links exceed maximum depth"};
		return container_.getnode(inf.redirect).follow(depth + 1);
	}
	
	void node::debug_treeprint(std::string prefix)
	{
		std::cout << prefix << name() << "\n"; // TODO
		//for (const std::pair<const size_t, filecount> &child : *children_) container_.getnode(child.second).debug_treeprint(prefix + "    ");
	}

	node::node(archive &container) : container_{container}
	{
		std::istream &in = container_.in();
		offset next;
		in.read(reinterpret_cast<char *>(&next), sizeof(offset));
		start_ = in.tellg();
		in.seekg(start_ + next);
		//std::cerr << "Node id " << id() << " type " << static_cast<int>(type_) << " parent " << parent_ << " name " << name_ << "\n";
		if (! in) throw badzsr{"Premature end of file while creating node"};
	}

	filecount node::id() const
	{
		if (container_.size() == 0) return 0;
		return this - &container_.getnode(0);
	}

	node *node::parent()
	{
		if (id() == 0) return nullptr;
		return &container_.getnode(readinfo().parent);
	}

	node::nodeinfo node::readinfo()
	{
		std::istream &in = container_.in();
		std::streampos oldg = in.tellg();
		in.seekg(start_);
		nodeinfo ret{in, container_.revcheck, static_cast<uint8_t>(container_.node_meta_.size())};
		in.seekg(oldg);
		return ret;
	}

	/*void node::resolve()
	{
		nodeinfo inf = readinfo();
		if (id() > 0)
		{
			node &parent = container_.getnode(inf.parent);
			if (! parent.children_) parent.children_.reset(new std::unordered_map<size_t, filecount>{});
			size_t key = std::hash<std::string>{}(inf.name);
			if (parent.children_->count(key)) throw std::runtime_error{"Internal error: collision in child map for node " + path()}; // FIXME Unlikely to cause problems...
			(*parent.children_)[key] = id();
		}
		//children_.insert(std::unique_ptr<node>{n});
	}*/

	std::streambuf *node::content()
	{
		nodeinfo inf = readinfo();
		if (inf.type == ntype::link) return follow().content();
		filecount myid = id();
		if (! container_.open_.count(myid)) container_.open_[myid].init(container_.in(), container_.datastart_ + inf.start, inf.len, inf.fullsize);
		return &container_.open_[myid];
	}

	std::string node::path()
	{
		node *par = parent();
		if (par == nullptr) return "";
		std::string ppath = par->path();
		if (ppath != "") ppath += "/";
		return ppath + name();
	}
	
	std::unordered_map<std::string, filecount> node::children()
	{
		nodeinfo inf = readinfo();
		if (inf.type == ntype::link) return follow().children();
		if (inf.type != ntype::dir) throw std::runtime_error{"Tried to get child list of non-directory"};
		std::unordered_map<std::string, filecount> ret{};
		//for (const std::pair<const size_t, filecount> &child : *children_) ret[container_.index(child.second).name()] = child.second;
		diskmap::map<std::string, filecount> children = inf.children();
		for (size_t idx = 0; idx < children.size(); idx++)
		{
			filecount childid = children[idx];
			ret[container_.index(childid).name()] = childid;
		}
		return ret;
	}

	node *node::getchild(const std::string &name)
	{
		nodeinfo inf = readinfo();
		if (inf.type == ntype::link) return follow().getchild(name);
		if (inf.type != ntype::dir) throw std::runtime_error{"Tried to get child of non-directory"};
		//size_t key = std::hash<std::string>{}(name);
		//if (! children_->count(key)) return nullptr;
		diskmap::map<std::string, filecount> children = inf.children();
		std::pair<bool, filecount> childid = children.get(name);
		if (! childid.first) return nullptr;
		return &container_.getnode(childid.second);
		//std::unique_ptr<node_base> testobj{new test_node{name}}; // Ew.
		//if (! children_.count(testobj)) return nullptr;
		//return &**children_.find(testobj);
	}
	
	void node::close()
	{
		container_.open_.erase(follow().id());
	}

	void node::extract(const std::string &path)
	{
		nodeinfo inf = readinfo();
		std::string fullpath = util::pathjoin({path, inf.name});
		if (inf.type == ntype::dir)
		{
			if (mkdir(fullpath.c_str(), 0755) != 0 && errno != EEXIST) throw std::runtime_error{"Could not create directory " + fullpath};
			//for (const std::pair<const size_t, filecount> &child : *children_) container_.getnode(child.second).extract(fullpath); // FIXME FIXME FIXME
		}
		else if (inf.type == ntype::reg)
		{
			std::ofstream out{fullpath};
			if (! out) throw std::runtime_error{"Could not create file " + fullpath};
			out << content();
			out.close();
			close();
		}
		else if (inf.type == ntype::link)
		{
			symlink(util::relreduce(util::dirname(path), follow().path()).c_str(), fullpath.c_str());
		}
	}

	node &iterator::getnode() const
	{
		if (idx > ar.size()) throw std::runtime_error("Tried to access invalid node");
		return ar.index_[idx];
	}

	std::string iterator::meta(const std::string &key) const
	{
		return getnode().meta(ar.metaidx(key));
	}

	std::unordered_map<std::string, filecount> iterator::children() const
	{
		if (! isdir()) throw std::runtime_error{"Can't list contents of non-directory"};
		return getnode().children();
	}

	std::streambuf *iterator::open()
	{
		node &n = getnode();
		if (! n.isreg()) throw std::runtime_error{"Tried to get content of directory " + path()};
		return n.content();
	}
	
	iterator::operator bool() const
	{
		return idx >= 0 && idx < ar.size();
	}

	const std::string archive::magic_number{"!ZSR"};

	std::ifstream archive::default_istream_{};
	
	node *archive::getnode(const std::string &path, bool except)
	{
		if (! index_.size())
		{
			if (except) throw std::runtime_error{"Tried to get node from empty archive"};
			return nullptr;
		}
		node *n = &index_[0];
		for (std::string &item : util::strsplit(path, util::pathsep))
		{
			if (item == "") continue;
			n = n->getchild(item);
			if (! n)
			{
				if (except) throw std::runtime_error{"Tried to access nonexistent path " + path};
				return nullptr;
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

	void archive::extract(const std::string &member, const std::string &dest)
	{
		if (! util::isdir(dest) && mkdir(dest.c_str(), 0750) < 0) throw std::runtime_error{"Couldn't access or create destination directory " + dest}; // TODO Abstract mkdir
		node *memptr = getnode(member);
		if (! memptr) throw std::runtime_error{"Member " + member + " does not exist in this archive"};
		memptr->extract(dest);
	}

	bool archive::check(const std::string &path)
	{
		node *n = getnode(path);
		if (! n) return false;
		if (n->isdir()) return false;
		return true;
	}

	archive::archive(std::ifstream &&in) : in_{std::move(in)}, index_{}, archive_meta_{}, node_meta_{}, open_{},  userdbuf_{}, userd_{&*userdbuf_}
	{
		if (! in_) throw badzsr{"Couldn't open archive input stream"};
		//in_.seekg(0, std::ios_base::end);
		//std::ifstream::pos_type fsize = in_.tellg();
		in_.seekg(0);
		std::string magic(magic_number.size(), '\0');
		in_.read(reinterpret_cast<char *>(&magic[0]), magic_number.size());
		if (magic != magic_number) throw badzsr{"File identifier incorrect"};
		offset idxstart{};
		in_.read(reinterpret_cast<char *>(&idxstart), sizeof(offset));
		if (! in) throw badzsr{"Premature end of file in header"};
		uint8_t nmeta;
		in_.read(reinterpret_cast<char *>(&nmeta), sizeof(uint8_t));
		for (uint8_t i = 0; i < nmeta; i++)
		{
			uint16_t ksize, vsize;
			std::string k{}, v{};
			in_.read(reinterpret_cast<char *>(&ksize), sizeof(uint16_t));
			k.resize(ksize);
			in_.read(reinterpret_cast<char *>(&k[0]), ksize);
			in_.read(reinterpret_cast<char *>(&vsize), sizeof(uint16_t));
			v.resize(vsize);
			in_.read(reinterpret_cast<char *>(&v[0]), vsize);
			archive_meta_[k] = v;
		}
		in_.read(reinterpret_cast<char *>(&nmeta), sizeof(uint8_t));
		for (uint8_t i = 0; i < nmeta; i++)
		{
			uint16_t msize;
			std::string mval{};
			in_.read(reinterpret_cast<char *>(&msize), sizeof(uint16_t));
			mval.resize(msize);
			in_.read(reinterpret_cast<char *>(&mval[0]), msize);
			node_meta_.push_back(mval);
		}
		datastart_ = in_.tellg();
		in_.seekg(idxstart);
		filecount nfile{};
		in_.read(reinterpret_cast<char *>(&nfile), sizeof(filecount));
		while (index_.size() < nfile) index_.emplace_back(*this); // Tricky syntax: this constructs a node with this as its container and adds it to the archive
		//for (node &n : index_) n.resolve();
		std::streampos userdstart = in_.tellg();
		in_.seekg(0, std::ios_base::end);
		userdbuf_.reset(new util::rangebuf{in_, userdstart, in_.tellg() - userdstart});
		userd_.rdbuf(&*userdbuf_);
	}

}

