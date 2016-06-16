#include "zsr.h"

namespace zsr
{
	std::ifstream archive::default_istream_{};

	node_base *node_base::follow(int depth)
	{
		if (! redirect_) return this;
		if (depth > maxdepth) throw std::runtime_error{"Links exceed maximum depth"};
		return redirect_->follow(depth + 1);
	}

	offset node_base::index_size() const
	{
		offset ret = 2 * sizeof(filecount) + 2 * sizeof(offset) + 2 + name_.size() + 1;
		for (const std::string &s : mdata_) ret += 2 + std::min(static_cast<int>(s.size()), (1 << 16) - 1);
		return ret;
	}

	void node_base::add_child(node_base *n)
	{
		children_[n->name_] = std::unique_ptr<node_base>{n};
		//children_.insert(std::unique_ptr<node_base>{n});
	}

	node_base *node_base::get_child(const std::string &name) const
	{
		if (! children_.count(name)) return nullptr;
		return &*children_.at(name);
		//std::unique_ptr<node_base> testobj{new test_node{name}}; // Ew.
		//if (! children_.count(testobj)) return nullptr;
		//return &**children_.find(testobj);
	}

	void node_base::write_content(std::ostream &out)
	{
		if (isdir()) return;
		std::istream stream{content()};
		lzma::wrbuf compressor{stream};
		start_ = out.tellp();
		out << &compressor;
		if (! out) throw std::runtime_error{"Bad output stream when writing archive content for file " + path()};
		len_ = static_cast<offset>(out.tellp()) - start_;
		//log("Wrote content for node " << path() << ", start " << start_ << ", length " << len_);
		close();
	}

	void node_base::write_index(std::ostream &out)
	{
		//log("Writing index for node " << path());
		if (! isdir() && start_ == 0) throw std::runtime_error{"Content must be written before index"};
		//out.write(reinterpret_cast<const char *>(&id_), sizeof(filecount));
		filecount parentid = parent_ == nullptr ? 0 : parent_->id();
		out.write(reinterpret_cast<const char *>(&parentid), sizeof(filecount));
		out.write(reinterpret_cast<const char *>(&start_), sizeof(offset));
		out.write(reinterpret_cast<const char *>(&len_), sizeof(offset));
		size_t fullsize = size(); // FIXME This is not portable; use an explicitly sized type
		out.write(reinterpret_cast<const char *>(&fullsize), sizeof(size_t));
		uint16_t namelen = name_.size();
		out.write(reinterpret_cast<char *>(&namelen), sizeof(uint16_t));
		out.write(reinterpret_cast<const char *>(&name_[0]), name_.size());
		if (! isdir()) for (const std::string &s : mdata_)
		{
			uint16_t datalen = s.size();
			out.write(reinterpret_cast<const char *>(&datalen), sizeof(uint16_t));
			out.write(reinterpret_cast<const char *>(&s[0]), s.size());
		}
		start_ = 0;
	}

	void node_base::extract(const std::string &path)
	{
		std::string fullpath = util::pathjoin({path, name()});
		if (isdir())
		{
			if (mkdir(fullpath.c_str(), 0755) != 0 && errno != EEXIST) throw std::runtime_error{"Could not create directory " + fullpath};
			for (const std::pair<const std::string, std::unique_ptr<node_base>> &child : children_) child.second->extract(fullpath);
		}
		else
		{
			std::ofstream out{fullpath};
			if (! out) throw std::runtime_error{"Could not create file " + fullpath};
			out << content();
			out.close();
			close();
		}
	}

	void node_base::debug_treeprint(std::string prefix) const
	{
		std::cout << prefix << name_ << "\n";
		for (const std::pair<const std::string, std::unique_ptr<node_base>> &child : children_) child.second->debug_treeprint(prefix + "    ");
	}

	node_tree::node_tree(archive_tree &container, node_base *parent, std::string path) : node_base{container.size(), parent, util::basename(path), (unsigned int) container.nodemeta().size()}, container_{container}, stream_{} { }

	bool node_tree::isdir() const
	{
		struct stat s;
		if (stat(path().c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path()};
		if ((s.st_mode & S_IFMT) == S_IFDIR) return true;
		return false;
	}

	std::streambuf *node_tree::content()
	{
		stream_.reset(new std::ifstream{});
		stream_->open(path());
		if (! *stream_) throw std::runtime_error{"Couldn't open " + path()};
		return stream_->rdbuf();
	}

	std::string node_tree::path() const
	{
		return (parent_ ? parent_->path() + util::pathsep + name() : container_.basedir());
	}
	
	size_t node_tree::size() const
	{
		struct stat statbuf;
		if (stat(path().c_str(), &statbuf) < 0) throw std::runtime_error{"Couldn't stat file " + path()};
		return statbuf.st_size;
	}

	node_file::node_file(archive_file &container, node_file *last) : node_base{container.size(), nullptr, "", (unsigned int) container.nodemeta().size()}, container_{container}, stream_{nullptr}
	{
		std::istream &in = container_.in();
		in.read(reinterpret_cast<char *>(&parent_), sizeof(filecount));
		in.read(reinterpret_cast<char *>(&start_), sizeof(offset));
		in.read(reinterpret_cast<char *>(&len_), sizeof(offset));
		in.read(reinterpret_cast<char *>(&fullsize_), sizeof(size_t));
		uint16_t namelen;
		in.read(reinterpret_cast<char *>(&namelen), sizeof(uint16_t));
		name_.resize(namelen);
		in.read(&name_[0], namelen);
		if (start_ > 0) for (unsigned int i = 0; i < mdata_.size(); i++)
		{
			uint16_t datalen;
			in.read(reinterpret_cast<char *>(&datalen), sizeof(uint16_t));
			std::string s{};
			s.resize(datalen);
			in.read(reinterpret_cast<char *>(&s[0]), datalen);
			mdata_[i] = s;
		}
		if (! in) throw badzsr{"Premature end of file while creating node_base"};
	}

	void node_file::resolve()
	{
		if (id_ == 0) parent_ = nullptr;
		else
		{
			filecount parentid = reinterpret_cast<filecount>(parent_);
			if (parentid > container_.size()) throw std::runtime_error{"Couldn't resolve parent ID " + util::t2s(parentid) + " to pointer"};
			parent_ = container_.index_.at(parentid); // FIXME Requires friend-classing
			parent_->add_child(this);
		}
		if (start_ == 0 && len_ != 0) // Link
		{
			filecount destid = reinterpret_cast<filecount>(len_);
			if (destid > container_.size()) throw std::runtime_error{"Couldn't resolve parent ID " + util::t2s(destid) + " to pointer"};
			redirect_ = container_.index_.at(destid);
		}
		else redirect_ = nullptr;
	}

	std::streambuf *node_file::content()
	{
		if (stream_) return &*stream_;
		stream_.reset(new lzma::rdbuf{});
		stream_->init(container_.in(), start_, len_, fullsize_);
		return &*stream_;
	}

	std::string node_file::path() const
	{
		if (parent() == nullptr) return "";
		std::string ppath = parent()->path();
		if (ppath != "") ppath += "/";
		return ppath + name();
	}

	node_base *node::getnode() const
	{
		if (idx > ar->size()) throw std::runtime_error("Tried to access invalid node");
		return ar->index_[idx];
	}

	std::string node::meta(const std::string &key) const
	{
		return getnode()->meta(ar->metaidx(key));
	}

	void node::meta(const std::string &key, const std::string &val)
	{
		getnode()->meta(ar->metaidx(key), val);
	}

	std::unordered_map<std::string, filecount> node::children() const
	{
		if (! isdir()) throw std::runtime_error{"Can't list contents of non-directory"};
		std::unordered_map<std::string, filecount> ret{};
		for (const std::pair<const std::string, std::unique_ptr<node_base>> &child : getnode()->children()) ret[child.second->name()] = child.second->id();
		return ret;
	}

	std::streambuf *node::open()
	{
		if (isdir()) throw std::runtime_error{"Tried to get content of directory " + path()};
		node_base *n = getnode();
		ar->open_.insert(n);
		return n->content();
	}
	
	node::operator bool() const
	{
		return idx < ar->size();
	}

	const std::string archive_base::magic_number{"!ZSR"};

	void archive_base::write(std::ostream &out)
	{
		std::string fileheader = magic_number + std::string(sizeof(offset), '\0');
		out.write(fileheader.c_str(), fileheader.size());
		uint8_t msize = archive_meta_.size();
		out.write(reinterpret_cast<char *>(&msize), sizeof(uint8_t));
		for (const std::pair<const std::string, std::string> &pair : archive_meta_)
		{
			uint16_t size = pair.first.size();
			out.write(reinterpret_cast<const char *>(&size), sizeof(uint16_t));
			out.write(pair.first.c_str(), size);
			size = pair.second.size();
			out.write(reinterpret_cast<const char *>(&size), sizeof(uint16_t));
			out.write(pair.second.c_str(), size);
		}
		msize = node_meta_.size();
		out.write(reinterpret_cast<char *>(&msize), sizeof(uint8_t));
		for (const std::string &mkey : node_meta_)
		{
			uint16_t ksize = mkey.size();
			out.write(reinterpret_cast<const char *>(&ksize), sizeof(uint16_t));
			out.write(mkey.c_str(), mkey.size());
		}
		loga("Writing archive content");
		int nwritten = 0;
		for (node_base *n : index_)
		{
			logb(++nwritten << " " << n->path());
			n->write_content(out);
		}
		offset index_start = out.tellp();
		filecount nfile = index_.size();
		out.write(reinterpret_cast<char *>(&nfile), sizeof(filecount));
		loga("Writing file index");
		nwritten = 0;
		for (node_base *n : index_)
		{
			logb(++nwritten << " " << n->path());
			n->write_index(out);
		}
		out.seekp(magic_number.size());
		out.write(reinterpret_cast<char *>(&index_start), sizeof(offset)); // FIXME Endianness problems?
		out.seekp(0, std::ios_base::end);
		//if (userdata()) out << userdata().rdbuf(); // FIXME Removed from constructor for now
		//log("Archive writing finished");
	}

	node_base *archive_base::getnode(const std::string &path, bool except) const
	{
		node_base *n = &*root_;
		if (! n)
		{
			if (except) throw std::runtime_error{"Tried to get node from empty archive"};
			return nullptr;
		}
		for (std::string &item : util::strsplit(path, util::pathsep))
		{
			n = n->get_child(item);
			if (! n)
			{
				if (except) throw std::runtime_error{"Tried to access nonexistent path " + path};
				return nullptr;
			}
		}
		return n;
	}

	unsigned int archive_base::metaidx(const std::string &key) const
	{
		std::vector<std::string>::const_iterator iter = std::find(node_meta_.cbegin(), node_meta_.cend(), key);
		if (iter == node_meta_.cend()) throw std::runtime_error{"Tried to retrieve nonexistent metadata key \"" + key + "\""};
		return iter - node_meta_.cbegin();
	}

	void archive_base::extract(const std::string &member, const std::string &dest)
	{
		if (! util::isdir(dest) && mkdir(dest.c_str(), 0777) < 0) throw std::runtime_error{"Couldn't access or create destination directory " + dest};
		node_base *memptr = getnode(member);
		if (! memptr) throw std::runtime_error{"Member " + member + " does not exist in this archive"};
		memptr->extract(dest);
	}

	void archive_base::addmeta(const std::string &key)
	{
		if (node_meta_.size() >= 256) throw std::runtime_error{"Tried to add too many metadata keys"}; // TODO Don't hardcode
		std::vector<std::string>::iterator iter = std::find(node_meta_.begin(), node_meta_.end(), key);
		if (iter != node_meta_.end()) throw std::runtime_error{"Tried to add duplicate metadata key \"" + key + "\""};
		node_meta_.push_back(key);
		for (node_base *n : index_) n->addmeta();
	}

	void archive_base::delmeta(const std::string &key)
	{
		unsigned int idx = metaidx(key);
		node_meta_.erase(node_meta_.begin() + idx);
		for (node_base *n : index_) n->delmeta(idx);
	}

	bool archive_base::check(const std::string &path) const
	{
		node_base *n = getnode(path);
		if (! n) return false;
		if (n->isdir()) return false;
		return true;
	}

	void archive_base::close(const std::string &path)
	{
		for (std::unordered_set<node_base *>::iterator iter = open_.begin(); iter != open_.end(); ) if ((*iter)->path() == path)
		{
			(*iter)->close();
			iter = open_.erase(iter);
		}
		else iter++;
	}

	void archive_base::reap()
	{
		for (node_base *n : open_) n->close();
		open_.clear();
	}

	archive_file::archive_file(std::ifstream &&in) : in_{std::move(in)}, userdbuf_{}, userd_{&*userdbuf_}
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
		in_.seekg(idxstart);
		filecount nfile{};
		in_.read(reinterpret_cast<char *>(&nfile), sizeof(filecount));
		root_ = std::unique_ptr<node_base>{new node_file{*this, nullptr}};
		node_file *last = dynamic_cast<node_file *>(&*root_);
		index_.push_back(last);
		while (index_.size() < nfile)
		{
			node_file *n = new node_file{*this, last};
			index_.push_back(n);
			last = n;
		}
		for (node_base *n : index_) dynamic_cast<node_file *>(n)->resolve();
		std::streampos userdstart = in_.tellg();
		in_.seekg(0, std::ios_base::end);
		userdbuf_.reset(new util::rangebuf{in_, userdstart, in_.tellg() - userdstart});
		userd_.rdbuf(&*userdbuf_);
	}

	node_tree *archive_tree::recursive_add(const std::string &path, node_tree *parent, std::function<std::unordered_map<std::string, std::string>(const node &)> metagen)
	{
		struct stat s;
		if (stat(path.c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path};
		node_tree *cur = new node_tree{*this, parent, path};
		index_.push_back(cur);
		logb(index_.size() << " " << path);
		if ((s.st_mode & S_IFMT) != S_IFDIR)
		{
			std::unordered_map<std::string, std::string> meta = metagen(node(this, index_.size() - 1));
			for (const std::pair<const std::string, std::string> &pair : meta)
			{
				std::vector<std::string>::iterator iter = std::find(node_meta_.begin(), node_meta_.end(), pair.first);
				unsigned int idx = iter - node_meta_.begin();
				if (iter == node_meta_.end())
				{
					addmeta(pair.first);
					idx = node_meta_.size() - 1;
				}
				cur->meta(idx, pair.second);
			}
		}
		else
		{
			DIR *dir = opendir(path.c_str());
			if (! dir) return cur; // TODO How to handle inaccessible directories?
			struct dirent *file;
			while ((file = readdir(dir)))
			{
				std::string fname{file->d_name};
				if (fname == "." || fname == "..") continue;
				cur->add_child(recursive_add(path + util::pathsep + fname, cur, metagen));
			}
			closedir(dir);
		}
		return cur;
	}

	archive_tree::archive_tree(const std::string &root, std::istream &userdata, const std::unordered_map<std::string, std::string> &gmeta, std::function<std::unordered_map<std::string, std::string>(const node &)> metagen) : archive_base{}, basedir_{root}, userd_{userdata}
	{
		archive_meta_ = gmeta;
		loga("Loading source tree");
		root_ = std::unique_ptr<node_base>{recursive_add(root, nullptr, metagen)};
		loga(index_.size() << " files loaded");
	}
}

