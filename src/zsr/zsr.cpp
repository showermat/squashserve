#include "zsr.h"

namespace zsr
{
	node::node(const archive &container, offset idx): container_{container}, id_{idx}, revcheck_{container_.revcheck}
	{
		if (idx >= container.size()) throw std::runtime_error("Tried to access invalid node (" + util::t2s(idx) + " ≥ " + util::t2s(container.size()) + ")");
		const char *inptr = container_.idxstart_ + idx * sizeof(offset);
		uint8_t nmeta = static_cast<uint8_t>(container_.node_meta_.size());
		offset start = deser<offset>(inptr);
		inptr = container_.datastart_ + start;
		parent_ = deser<offset>(inptr);
		type_ = deser<ntype>(inptr);
		name_ = deser<std::string_view>(inptr);
		if (type_ == ntype::link) redirect_ = deser<filecount>(inptr);
		else if (type_ == ntype::reg)
		{
			for (uint8_t i = 0; i < nmeta; i++) meta_.push_back(std::string{deser<std::string_view>(inptr)});
			fullsize_ = deser<offset>(inptr);
			len_ = deser<offset>(inptr);
		}
		data_ = inptr;
		//std::cerr << "Node " << id_ << ": " << name_ << " @ " << std::hex << start << " [" << std::dec << len_ << "]\n";
	}

	diskmap::map<std::string, filecount> node::childmap() const
	{
		if (type_ == ntype::link) return follow().childmap();
		if (type_ != ntype::dir) throw std::runtime_error{"Tried to get child of non-directory"};
		return diskmap::map<std::string, filecount>{data_, revcheck_};
	}

	node node::follow(unsigned int limit, unsigned int depth) const
	{
		constexpr unsigned int maxdepth = 255;
		if (type_ != ntype::link) return *this;
		if (limit && depth >= limit) return *this;
		if (! limit && depth > maxdepth) throw std::runtime_error{"Links exceed maximum depth"};
		return node{container_, redirect_}.follow(limit, depth + 1);
	}

	std::optional<node> node::parent() const
	{
		if (id_ == 0) return std::nullopt;
		return node{container_, parent_};
	}

	std::string node::path() const
	{
		if (id_ == 0) return "";
		std::string ppath = parent()->path();
		if (ppath != "") ppath += "/";
		return ppath + name();
	}

	std::string node::meta(const std::string &key) const
	{
		// TODO We should either give links metadata or follow them when retrieving
		if (type_ != ntype::reg) throw std::runtime_error{"Tried to get metadata of a non-regular file"};
		return meta_[container_.metaidx(key)];
		//return follow().meta_[container_.metaidx(key)];
	}

	iterator node::children() const
	{
		return iterator{container_, *this};
	}

	std::optional<node> node::child(const std::string &name) const
	{
		std::optional<filecount> childid = childmap().get(name);
		if (! childid) return std::nullopt;
		return node{container_, *childid};
	}

	stream node::content() const
	{
		if (type_ == ntype::link) return follow().content();
		if (type_ != ntype::reg) throw std::runtime_error{"Tried to get content of non-regular file"};
		return stream{data_, static_cast<std::streampos>(len_), static_cast<std::streampos>(fullsize_)}; // TODO Theoretically unsafe unsigned-to-signed conversions
	}

	void node::extract(const std::string &location) const
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
			out << content().rdbuf();
			out.close();
		}
		else if (type_ == ntype::link)
		{
			symlink(dest().c_str(), fullpath.c_str());
		}
	}

	std::unordered_map<std::string, filecount> iterator::all() const
	{
		std::unordered_map<std::string, filecount> ret{};
		for (filecount i = 0; i < n.nchild(); i++) ret[ar.index(i).name()] = i;
		return ret;
	}

	node iterator::get() const
	{
		return node{ar, n.childid(idx)};
	}

	std::optional<node> archive::getnode(const std::string &path, bool except) const
	{
		if (! size_)
		{
			if (except) throw std::runtime_error{"Tried to get node from empty archive"};
			return std::nullopt;
		}
		std::optional<node> n{std::in_place, *this, 0};
		//std::cerr << ">>>> " << this << " " << idxstart_ << " " << datastart_ << " " << n->name() << "|\n";
		for (std::string &item : util::strsplit(path, util::pathsep))
		{
			if (! n->isdir())
			{
				if (except) throw std::runtime_error{"Tried to get child of non-directory " + path};
				return std::nullopt;
			}
			if (item == "" || item == ".") continue;
			if (item == "..")
			{
				if (n->id() > 0) n.emplace(n->parent().value()); // It would be nice if we could avoid these emplaces, but I'm not sure wether move assignment of nodes is possible (it is required to use `n = n->parent()`).
				continue;
			}
			std::optional<node> child = n->child(item);
			if (! child)
			{
				if (except) throw std::runtime_error{"Tried to access nonexistent path " + path};
				return std::nullopt;
			}
			n.emplace(child.value());
		}
		return n;
	}

	unsigned int archive::metaidx(const std::string &key) const
	{
		std::vector<std::string>::const_iterator iter = std::find(node_meta_.cbegin(), node_meta_.cend(), key);
		if (iter == node_meta_.cend()) throw std::runtime_error{"Tried to retrieve nonexistent metadata key \"" + key + "\""};
		return iter - node_meta_.cbegin();
	}

	bool archive::check(const std::string &path) const
	{
		std::optional<node> n = getnode(path);
		if (! n) return false;
		if (n->isdir()) return false;
		return true;
	}

	archive::archive(const std::string &path) : in_{path}, archive_meta_{}, node_meta_{}, userd_{}
	{
		base_ = static_cast<const char *>(in_.get());
		if (in_.size() < magic_number.size() + sizeof(uint16_t) + sizeof(offset)) throw badzsr{"File too small"};
		const char *inptr = base_;
		std::string_view magic = deser_string(inptr, magic_number.size());
		if (magic != magic_number) throw badzsr{"Not a ZSR file"};
		uint16_t vers = deser<uint16_t>(inptr);
		if (vers != version) throw badzsr{"ZSR version " + util::t2s(version) + " cannot read files of version " + util::t2s(vers)};
		const char *idxstart = base_ + deser<offset>(inptr);
		if (idxstart > base_ + in_.size()) throw badzsr{"File too small"};
		uint8_t nmeta = deser<uint8_t>(inptr);
		for (uint8_t i = 0; i < nmeta; i++)
		{
			std::string k = std::string{deser<std::string_view>(inptr)};
			std::string v = std::string{deser<std::string_view>(inptr)};
			archive_meta_[k] = v;
		}
		nmeta = deser<uint8_t>(inptr);
		for (uint8_t i = 0; i < nmeta; i++) node_meta_.push_back(std::string{deser<std::string_view>(inptr)});
		datastart_ = inptr;
		inptr = idxstart;
		size_ = deser<filecount>(inptr);
		idxstart_ = inptr;
		const char *userdstart = idxstart_ + size_ * sizeof(filecount);
		if (userdstart > base_ + in_.size()) throw badzsr{"File too small"};
		userd_ = std::string_view{userdstart, static_cast<std::string_view::size_type>(base_ + in_.size() - userdstart)};
	}
}
