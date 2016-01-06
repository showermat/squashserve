#include "zsr.h"

namespace zsr
{
	bool logging_ = false;

	void logging(bool on) { logging_ = on; }

	void log(const std::string &msg, std::ostream &out)
	{
		if (! logging_) return;
		out << util::timestr() << ":  " << msg << "\n";
	}

	void node::add_child(node *n)
	{
		children_[n->name_] = std::unique_ptr<node>{n};
	}

	node *node::get_child(const std::string &name) const
	{
		if (! children_.count(name)) return nullptr;
		return &*children_.at(name);
	}

	void node::write_content(std::ostream &out)
	{
		if (isdir()) for (std::pair<const std::string, std::unique_ptr<node>> &child : children_) child.second->write_content(out);
		else
		{
			std::istream stream{content()};
			lzma::wrbuf compressor{stream};
			start_ = out.tellp();
			out << &compressor;
			if (! out) throw std::runtime_error{"Bad output stream when writing archive content for file " + path()};
			len_ = static_cast<offset>(out.tellp()) - start_;
			log("Wrote content for node " + path() + ", start " + util::t2s(start_) + ", length " + util::t2s(len_));
			close();
		}
	}

	void node::write_index(std::ostream &out)
	{
		//log("Writing index for node " + path());
		if (! isdir() && start_ == 0) throw std::runtime_error{"Content must be written before index"};
		out.write(reinterpret_cast<const char *>(&id_), sizeof(index));
		index parentid = parent_ == nullptr ? 0 : parent_->id();
		out.write(reinterpret_cast<const char *>(&parentid), sizeof(index));
		out.write(reinterpret_cast<const char *>(&start_), sizeof(offset));
		out.write(reinterpret_cast<const char *>(&len_), sizeof(offset));
		uint16_t namelen = name_.size();
		out.write(reinterpret_cast<char *>(&namelen), sizeof(uint16_t));
		out.write(reinterpret_cast<const char *>(&name_[0]), name_.size());
		if (isdir()) for (std::pair<const std::string, std::unique_ptr<node>> &child : children_) child.second->write_index(out);
		start_ = 0;
	}

	void node::extract(const std::string &path)
	{
		std::string fullpath = util::pathjoin({path, name()});
		if (isdir())
		{
			if (mkdir(fullpath.c_str(), 0755) != 0 && errno != EEXIST) throw std::runtime_error{"Could not create directory " + fullpath};
			for (std::pair<const std::string, std::unique_ptr<node>> &child : children_) child.second->extract(fullpath);
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

	void node::debug_treeprint(std::string prefix) const
	{
		std::cout << prefix << name_ << "\n";
		for (const std::pair<const std::string, std::unique_ptr<node>> &child : children_) child.second->debug_treeprint(prefix + "    ");
	}

	bool node_tree::isdir() const
	{
		struct stat s;
		if (stat(path().c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path()};
		if ((s.st_mode & S_IFMT) == S_IFDIR) return true;
		return false;
	}

	std::streambuf *node_tree::content()
	{
		stream_.open(path());
		if (! stream_) throw std::runtime_error{"Couldn't open " + path()};
		return stream_.rdbuf();
	}

	std::string node_tree::path() const
	{
		return (parent_ ? parent_->path() + util::pathsep + name() : container_.basedir());
	}

	node_file::node_file(archive_file &container, node_file *last) : node{0, nullptr, ""}, container_{container}, stream_{}
	{
		// TODO Not checking for premature end of file
		std::istream &in = container_.in();
		index parentid;
		in.read(reinterpret_cast<char *>(&id_), sizeof(index));
		in.read(reinterpret_cast<char *>(&parentid), sizeof(index));
		in.read(reinterpret_cast<char *>(&start_), sizeof(offset));
		in.read(reinterpret_cast<char *>(&len_), sizeof(offset));
		uint16_t namelen;
		in.read(reinterpret_cast<char *>(&namelen), sizeof(uint16_t));
		name_.resize(namelen);
		in.read(&name_[0], namelen);
		if (parentid == 0) parent_ = nullptr;
		else
		{
			for (node_file *n = last; n != nullptr; n = dynamic_cast<node_file *>(n->parent_)) if (n->id() == parentid) parent_ = n;
			if (parent_ == nullptr) throw badzsr{"Couldn't resolve parent ID to pointer"};
		}
		if (parent_) parent_->add_child(this);
		stream_.init(in, start_, len_);
	}

	std::string node_file::path() const
	{
		std::string ret{name()};
		for (node *n = this->parent(); n != nullptr && n->parent() != nullptr; n = n->parent()) ret = n->name() + "/" + ret;
		return ret;
	}

	const std::string archive_base::magic_number{"!ZSR"};

	void archive_base::write(std::ostream &out)
	{
		log("Base archive content writer starting");
		std::string fileheader = magic_number + std::string(sizeof(node::offset), '\0');
		out.write(fileheader.c_str(), fileheader.size());
		root_->write_content(out);
		log("Base archive content written; index writer starting");
		node::offset index_start = out.tellp();
		root_->write_index(out);
		out.seekp(magic_number.size());
		out.write(reinterpret_cast<char *>(&index_start), sizeof(node::offset)); // FIXME Endianness problems?
		log("Archive writing finished");
	}

	node *archive_base::getnode(const std::string &path) const
	{
		node *n = &*root_;
		if (! n) return nullptr;
		for (std::string &item : util::strsplit(path, util::pathsep))
		{
			n = n->get_child(item);
			if (! n) return nullptr;
		}
		return n;
	}

	void archive_base::extract(const std::string &member, const std::string &dest)
	{
		if (! util::isdir(dest) && mkdir(dest.c_str(), 0777) < 0) throw std::runtime_error{"Couldn't access or create destination directory " + dest};
		node *memptr = getnode(member);
		if (! memptr) throw std::runtime_error{"Member " + member + " does not exist in this archive"};
		memptr->extract(dest);
	}

	bool archive_base::check(const std::string &path) const
	{
		node *n = getnode(path);
		if (! n) return false;
		if (n->isdir()) return false;
		return true;
	}

	bool archive_base::isdir(const std::string &path) const
	{
		node *n = getnode(path);
		if (! n) return false;
		return true;
	}

	std::streambuf *archive_base::get(const std::string &path) const
	{
		node *n = getnode(path);
		if (! n) throw std::runtime_error{"Tried to get content of nonexistent path " + path};
		if (n->isdir()) throw std::runtime_error{"Tried to get content of directory " + path};
		return n->content(); // FIXME need to close in constructor
	}

	archive_file::archive_file(std::ifstream &&in) : in_{std::move(in)}
	{
		if (! in_) throw badzsr{"Couldn't open archive input stream"};
		// TODO Not checking for premature end of file
		in_.seekg(0, std::ios_base::end);
		std::ifstream::pos_type fsize = in_.tellg();
		in_.seekg(0);
		std::string magic(magic_number.size(), '\0');
		in_.read(reinterpret_cast<char *>(&magic[0]), magic_number.size());
		if (magic != magic_number) throw badzsr{"File identifier incorrect"};
		node::offset idxstart{};
		in_.read(reinterpret_cast<char *>(&idxstart), sizeof(node::offset));
		in_.seekg(idxstart);
		root_ = std::unique_ptr<node>{new node_file{*this, nullptr}};
		node_file *last = dynamic_cast<node_file *>(&*root_);
		while (in_.tellg() < fsize)
		{
			if (in_.tellg() < 0) throw badzsr{"Invalid position in input stream"};
			node_file *n = new node_file{*this, last};
			last = n;
		}
	}

	node_tree *archive_tree::recursive_add(const std::string &path, node_tree *parent)
	{
		struct stat s;
		if (stat(path.c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path};
		node_tree *cur = new node_tree{next_id_++, parent, path, *this};
		if ((s.st_mode & S_IFMT) == S_IFDIR)
		{
			DIR *dir = opendir(path.c_str());
			if (! dir) return cur; // TODO How to handle inaccessible directories?
			struct dirent *file;
			while ((file = readdir(dir)))
			{
				std::string fname{file->d_name};
				if (fname == "." || fname == "..") continue;
				cur->add_child(recursive_add(path + util::pathsep + fname, cur));
			}
			closedir(dir);
		}
		return cur;
	}

	archive_tree::archive_tree(const std::string &root) : archive_base{}, basedir_{root}
	{
		log("Tree archive construction starting");
		root_ = std::unique_ptr<node>{recursive_add(root, nullptr)};
		log("Recursive add finished");
	}
}

