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
		offset mypos = contout.tellp();
		idxout.write(reinterpret_cast<const char *>(&mypos), sizeof(offset));
		contout.write(reinterpret_cast<const char *>(&parent), sizeof(filecount));
		contout.write(reinterpret_cast<const char *>(&type), sizeof(node::ntype));
		writestring(util::basename(path), contout);
		if (type == node::ntype::reg)
		{
			offset fullsize = static_cast<offset>(util::fsize(path));
			const std::vector<std::string> metad = metagen_(filenode(*this, id, path));
			if (metad.size() != nodemeta_.size()) throw std::runtime_error{"Number of generated metadata does not match number of file metadata keys"};
			for (const std::string &val : metad) writestring(val, contout);
			contout.write(reinterpret_cast<const char *>(&fullsize), sizeof(offset));
			std::streampos sizepos = contout.tellp();
			contout.write(reinterpret_cast<const char *>(&ptrfill), sizeof(offset)); // Placeholder for length
			lzma::wrbuf compressor{in};
			contout << &compressor;
			if (! contout) throw std::runtime_error{"Bad output stream when writing archive content for file " + path};
			offset len = static_cast<offset>(contout.tellp() - sizepos - sizeof(offset));
			std::streampos end = contout.tellp();
			contout.seekp(sizepos);
			contout.write(reinterpret_cast<const char *>(&len), sizeof(offset));
			contout.seekp(end);
		}
		if (type == node::ntype::link)
		{
			constexpr filecount fcfill{0}; // To fill in later
			//links_[util::relreduce(fullroot_, fulltarget)] = idxout.tellp(); // TODO links
			contout.write(reinterpret_cast<const char *>(&fcfill), sizeof(filecount));
		}
		if (type == node::ntype::dir)
		{
			diskmap::writer<std::string, filecount> children{};
			std::streampos childstart = contout.tellp();
			size_t nchild = 0;
			while (readdir(dir)) nchild++;
			rewinddir(dir);
			nchild -= 2;
			contout.seekp(children.hdrsize + children.recsize * nchild, std::ios::cur);
			struct dirent *file;
			while ((file = readdir(dir)))
			{
				std::string fname{file->d_name};
				if (fname == "." || fname == "..") continue;
				filecount childid = recursive_process(path + util::pathsep + fname, id, contout, idxout);
				children.add(fname, childid);
			}
			if (closedir(dir)) throw std::runtime_error{"Couldn't close " + path + ": " + strerror(errno)};
			std::streampos end = contout.tellp();
			contout.seekp(childstart);
			children.write(contout);
			contout.seekp(end);
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

	std::string node::readstring()
	{
		uint16_t len;
		in_.read(reinterpret_cast<char *>(&len), sizeof(uint16_t));
		std::string ret{};
		ret.resize(len);
		in_.read(reinterpret_cast<char *>(&ret[0]), len);
		return ret;
	}

	node::node(archive &container, offset idx): container_{container}, in_{container.in_}, id_{idx}, revcheck_{container_.revcheck}
	{
		uint8_t nmeta = static_cast<uint8_t>(container_.node_meta_.size());
		in_.seekg(container_.idxstart_ + idx * sizeof(offset));
		offset start;
		in_.read(reinterpret_cast<char *>(&start), sizeof(offset));
		in_.seekg(container_.datastart_ + start);
		in_.read(reinterpret_cast<char *>(&parent_), sizeof(filecount));
		in_.read(reinterpret_cast<char *>(&type_), sizeof(ntype));
		name_ = readstring();
		if (type_ == ntype::link) in_.read(reinterpret_cast<char *>(&redirect_), sizeof(filecount));
		else if (type_ == ntype::reg)
		{
			for (uint8_t i = 0; i < nmeta; i++) meta_.push_back(readstring());
			in_.read(reinterpret_cast<char *>(&fullsize_), sizeof(offset));
			in_.read(reinterpret_cast<char *>(&len_), sizeof(offset));
		}
		datastart_ = in_.tellg();
		//std::cerr << "Node " << id_ << ": " << name_ << "@" << std::hex << start << "[" << std::dec << len_ << "]" << "\n";
	}

	diskmap::map<std::string, filecount> node::childmap()
	{
		if (type_ == ntype::link) return follow().childmap();
		if (type_ != ntype::dir) throw std::runtime_error{"Tried to get child of non-directory"};
		in_.seekg(datastart_);
		return diskmap::map<std::string, filecount>{in_, revcheck_};
	}

	node node::follow(unsigned int depth)
	{
		constexpr unsigned int maxdepth = 255;
		if (type_ != ntype::link) return *this;
		if (depth > maxdepth) throw std::runtime_error{"Links exceed maximum depth"};
		return node{container_, redirect_}.follow(depth + 1);
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

	void node::extract(const std::string &path)
	{
		std::string fullpath = util::pathjoin({path, name_});
		if (type_ == ntype::dir)
		{
			if (mkdir(fullpath.c_str(), 0755) != 0 && errno != EEXIST) throw std::runtime_error{"Could not create directory " + fullpath};
			diskmap::map<std::string, filecount> cont = childmap();
			std::cerr << "Node " << id_ << " name " << name_ << ": " << cont.size() << " children\n";
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
			symlink(util::relreduce(util::dirname(path), follow().path()).c_str(), fullpath.c_str());
		}
	}

	node iterator::getnode() const
	{
		if (idx >= ar.size()) throw std::runtime_error("Tried to access invalid node");
		return node{ar, idx};
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

	void archive::extract(const std::string &path, const std::string &dest)
	{
		if (! util::isdir(dest) && mkdir(dest.c_str(), 0750) < 0) throw std::runtime_error{"Couldn't access or create destination directory " + dest}; // TODO Abstract mkdir
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
		in_.read(reinterpret_cast<char *>(&idxstart), sizeof(offset));
		if (! in_) throw badzsr{"Premature end of file in header"};
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
		in_.read(reinterpret_cast<char *>(&size_), sizeof(filecount));
		idxstart_ = in_.tellg();
		offset test;
		in_.read(reinterpret_cast<char *>(&test), sizeof(offset));
		std::streampos userdstart = idxstart_ + static_cast<std::streampos>(size_ * sizeof(filecount));
		in_.seekg(0, std::ios_base::end);
		userdbuf_.reset(new util::rangebuf{in_, userdstart, in_.tellg() - userdstart});
		userd_.rdbuf(&*userdbuf_);
	}

}

