#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64
#include <iostream>
#include <fuse.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <attr/xattr.h>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <mutex>
#include "zsr/zsr.h"

std::unique_ptr<const zsr::archive> ar{};

void help_exit()
{
	std::cerr << "Usage: zsrmnt archive.zsr mountpoint\n"; // TODO Improve
	exit(1);
}

int fs_open(const char *path, struct fuse_file_info *info)
{
	if ((info->flags & 3) != O_RDONLY) return -EACCES;
	std::string pathstr{path};
	if (! ar->check(pathstr)) return -ENOENT;
	return 0;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	std::string pathstr{path};
	if (! ar->check(pathstr)) return -ENOENT;
	try
	{
		zsr::stream read = ar->get(pathstr).content();
		read.seekg(offset);
		read.read(buf, size);
		return read.gcount();
	}
	catch (std::runtime_error &e) { return -EIO; }
}

int fs_release(const char *path, struct fuse_file_info *info)
{
	return 0;
}

int fs_readlink(const char *path, char *buf, size_t size)
{
	const zsr::iterator n = ar->get(std::string{path});
	if (n.type() != zsr::node::ntype::link) return -EINVAL;
	strncpy(buf, n.dest().c_str(), size - 1);
	buf[size - 1] = 0; // This isn't the behvaior described in the manpage....
	return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	std::vector<dirent> ls{};
	try
	{
		//for (const std::pair<const std::string, zsr::filecount> &child : ar->get(std::string{path}).children())
		for (zsr::childiter children = ar->get(std::string{path}).children(); children; children++)
		{
			zsr::iterator child = children.get();
			dirent dir;
			strncpy(dir.d_name, child.name().c_str(), 256); // TODO Path name length limit?
			zsr::node::ntype type = child.type();
			if (type == zsr::node::ntype::reg) dir.d_type = DT_REG;
			else if (type == zsr::node::ntype::dir) dir.d_type = DT_DIR;
			else if (type == zsr::node::ntype::link) dir.d_type = DT_LNK;
			ls.push_back(dir);
		}
	}
	catch (std::runtime_error &e) { return -ENOENT; } // TODO Should specialize runtime_error in zsr to allow type-based error handling
	filler(buf, ".", nullptr, 0);
	filler(buf, "..", nullptr, 0);
	for (dirent &d : ls) if (filler(buf, d.d_name, nullptr, 0)) return -EIO;
	return 0;
}

int fs_getattr(const char *path, struct stat *stat)
{
	memset(stat, 0, sizeof(struct stat));
	stat->st_nlink = 1;
	try
	{
		const zsr::iterator n = ar->get(std::string{path});
		if (n.type() == zsr::node::ntype::dir) stat->st_mode = S_IFDIR;
		else if (n.type() == zsr::node::ntype::reg)
		{
			stat->st_mode = S_IFREG;
			stat->st_size = n.size();
		}
		else if (n.type() == zsr::node::ntype::link)
		{
			stat->st_mode = S_IFLNK;
			stat->st_size = n.dest().size();
		}
		stat->st_mode |= 00750;
	}
	catch (std::runtime_error &e) { return -ENOENT; }
	return 0;
}

//#ifdef HAVE_SETXATTR // ?
int fs_listxattr(const char *path, char *list, size_t size) // FIXME Metadata could theoretically contain nulls, causing problems
{
	std::string pathstr{path};
	std::ostringstream buf{};
	try
	{
		if (path == std::string{"/"})
		{
			for (const std::pair<std::string, std::string> &entry : ar->gmeta()) buf << "user." << entry.first << '\0';
		}
		else
		{
			const zsr::iterator n = ar->get(pathstr);
			if (n.isdir()) return 0;
			for (const std::string &key : ar->nodemeta())
			{
				std::string value = n.meta(key);
				if (value.size()) buf << "user." << key << '\0';
			}
		}
	}
	catch (std::runtime_error &e) { return -ENOENT; }
	std::string ret = buf.str();
	if (size)
	{
		if (ret.size() > size) return -ERANGE;
		memcpy(list, &ret[0], ret.size());
	}
	return ret.size();
}

int fs_getxattr(const char *path, const char *name, char *value, size_t size)
{
	std::string pathstr{path}, namestr{name}, val{};
	std::ostringstream buf{};
	if (namestr.substr(0, 5) != "user.") return -ENOATTR;
	std::string attrname = namestr.substr(5);
	try
	{
		if (pathstr == std::string{"/"})
		{
			if (! ar->gmeta().count(attrname)) return -ENOATTR;
			val = ar->gmeta().at(attrname);
		}
		else
		{
			const zsr::iterator n = ar->get(pathstr);
			try { val = n.meta(attrname); }
			catch (std::runtime_error &e) { return -ENOATTR; }
			if (! val.size()) return -ENOATTR;
		}
	}
	catch (std::runtime_error &e) { return -ENOENT; }
	if (size)
	{
		if (val.size() > size) return -ERANGE;
		memcpy(value, &val[0], val.size());
	}
	return val.size();
}

int fs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	return -EACCES;
}

int fs_removexattr(const char *path, const char *name)
{
	return -EACCES;
}

struct fuse_operations ops;

int main(int argc, char **argv)
{
	std::vector<std::string> args = util::argvec(argc, argv);
	argc--; argv++;
	if (args.size() < 2) help_exit();
	ar.reset(new zsr::archive{args[1]});
	ops.open = fs_open;
	ops.read = fs_read;
	ops.readlink = fs_readlink;
	ops.readdir = fs_readdir;
	ops.getattr = fs_getattr;
	ops.release = fs_release;
	ops.listxattr = fs_listxattr;
	ops.getxattr = fs_getxattr;
	ops.setxattr = fs_setxattr;
	ops.removexattr = fs_removexattr;
	return fuse_main(argc, argv, &ops, nullptr);
}

