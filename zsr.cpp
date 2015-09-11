#include <iostream>
#include <streambuf>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <lzma.h>
#include "zsr.h"

namespace zsr
{
	std::vector<char> compress(std::vector<char> input)
	{
		std::vector<char> ret{};
		ret.resize(input.size() * 1.25 + 128);
		size_t len = 0;
		if (lzma_easy_buffer_encode(compression, LZMA_CHECK_CRC32, nullptr, reinterpret_cast<uint8_t *>(&input[0]), input.size(), reinterpret_cast<uint8_t *>(&ret[0]), &len, ret.size()) != LZMA_OK) throw std::runtime_error{"Compression failed"};
		ret.resize(len);
		//std::cerr << input.size() << " -> " << ret.size() << "\n";
		return ret;
	}

	std::vector<char> decompress(std::vector<char> input)
	{
		int avail = 8192;
		std::vector<char> ret{};
		lzma_stream stream = LZMA_STREAM_INIT;
		ret.resize(avail);
		size_t len = 0;
		if (lzma_stream_decoder(&stream, memlimit, LZMA_CONCATENATED) != LZMA_OK) throw compress_error{"Stream decoder setup failed"};
		stream.next_in = reinterpret_cast<uint8_t *>(&input[0]);
		stream.avail_in = input.size();
		stream.next_out = reinterpret_cast<uint8_t *>(&ret[0]);
		stream.avail_out = ret.size();
		while (true)
		{
			lzma_ret retval = lzma_code(&stream, stream.avail_in == 0 ? LZMA_FINISH : LZMA_RUN);
			if (retval == LZMA_STREAM_END)
			{
				len += avail - stream.avail_out;
				if (stream.avail_in != 0) throw compress_error{"Unprocessed input remaining in stream"};
				ret.resize(len);
				lzma_end(&stream);
				return ret;
			}
			if (retval != LZMA_OK) throw compress_error{"Decompression failed"};
			if (stream.avail_out == 0)
			{
				len += avail - stream.avail_out;
				ret.resize(ret.size() * 2);
				stream.next_out = reinterpret_cast<uint8_t *>(&ret[0] + len);
				stream.avail_out = avail = ret.size() - len;
			}
		}
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
			//std::cerr << path() << "\n";
			start_ = out.tellp();
			std::vector<char> zipbuf = compress(content());
			len_ = zipbuf.size();
			out.write(&zipbuf[0], zipbuf.size());
		}
	}

	void node::write_index(std::ostream &out)
	{
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
			if (mkdir(fullpath.c_str(), 0755) != 0 && errno != EEXIST) throw std::runtime_error{"Could not create directory " + fullpath}; // TODO Ignore if dir already exists
			for (std::pair<const std::string, std::unique_ptr<node>> &child : children_) child.second->extract(fullpath);
		}
		else
		{
			std::vector<char> data = content();
			std::ofstream out{fullpath};
			if (! out) throw std::runtime_error{"Could not create file " + fullpath};
			out.write(&data[0], data.size());
			out.close();
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
		if (lstat(path().c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path()};
		if ((s.st_mode & S_IFMT) == S_IFDIR) return true;
		return false;
	}

	std::vector<char> node_tree::content()
	{
		if (isdir()) throw std::runtime_error{"Tried to get content of directory " + path()};
		std::ifstream in{path()};
		if (! in) throw std::runtime_error{"Couldn't open " + path() + " for reading"};
		std::vector<char> ret{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}}; // FIXME Possibly slow?
		return ret;
	}

	node_file::node_file(archive_file &container, node_file *last) : node{0, nullptr, ""}, container_{container}
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
	}

	std::vector<char> node_file::content()
	{
		if (isdir()) throw std::runtime_error{"Tried to get content of directory " + path()};
		std::istream &in = container_.in();
		std::vector<char> zipbuf(len_);
		in.seekg(start_);
		in.read(reinterpret_cast<char *>(&zipbuf[0]), len_);
		return decompress(zipbuf);
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
		std::string fileheader = magic_number + std::string(sizeof(node::offset), '\0');
		out.write(fileheader.c_str(), fileheader.size());
		root_->write_content(out);
		node::offset index_start = out.tellp();
		root_->write_index(out);
		out.seekp(magic_number.size());
		out.write(reinterpret_cast<char *>(&index_start), sizeof(node::offset));
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

	void archive_base::extract(const std::string &dest, const std::string &subdir)
	{
		if (! util::isdir(dest) && mkdir(dest.c_str(), 0777) < 0) throw std::runtime_error{"Couldn't access or create destination directory " + dest};
		getnode(subdir)->extract(dest);
	}

	bool archive_base::check(const std::string &path) const
	{
		node *n = getnode(path);
		if (! n) return false;
		if (n->isdir()) return false;
		return true;
	}

	bool archive_base::checkdir(const std::string &path) const
	{
		node *n = getnode(path);
		if (! n) return false;
		return true;
	}

	std::vector<char> archive_base::get(const std::string &path) const
	{
		node *n = getnode(path);
		if (! n) throw std::runtime_error{"Tried to get content of nonexistent path " + path};
		if (n->isdir()) throw std::runtime_error{"Tried to get content of directory " + path};
		return n->content();
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
		if (lstat(path.c_str(), &s) != 0) throw std::runtime_error{"Couldn't stat " + path};
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
		root_ = std::unique_ptr<node>{recursive_add(root, nullptr)};
		// TODO Verify that the root is a dir...
	}
}

