#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64
#include <iostream>
#include <fuse.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <mutex>
#include "zsr.h"

std::unique_ptr<zsr::archive> ar{};
std::unordered_map<std::string, int> openf{};
std::mutex readlock{}, openlock{};

void help_exit()
{
	std::cerr << "Usage: zsrmnt archive.zsr mountpoint\n"; // TODO Improve
	exit(1);
}

int fs_open(const char *path, struct fuse_file_info *info)
{
	if ((info->flags & 3) != O_RDONLY) return -EACCES;
	std::lock_guard<std::mutex> openguard{openlock};
	openf[std::string{path}]++;
	return 0;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	if (! ar->check(std::string(path))) return -ENOENT;
	try
	{
		std::lock_guard<std::mutex> readguard{readlock};
		std::istream read{ar->get(std::string{path}).open()};
		read.seekg(offset);
		read.read(buf, size);
		return read.gcount();
	}
	catch (std::runtime_error &e) { return -EIO; }
}

int fs_release(const char *path, struct fuse_file_info *info)
{
	const std::string pathstr{path};
	std::lock_guard<std::mutex> closeguard{openlock};
	openf[pathstr]--;
	if (openf[pathstr] == 0)
	{
		ar->close(pathstr);
		openf.erase(pathstr);
	}
	return 0;
}

int fs_readlink(const char *path, char *buf, size_t size)
{
	zsr::iterator n = ar->get(std::string{path});
	if (n.type() != zsr::node::ntype::link) return -EINVAL;
	strncpy(buf, n.dest().c_str(), size - 1);
	buf[size - 1] = 0;
	return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	std::vector<dirent> ls{};
	try
	{
		for (const std::pair<const std::string, zsr::filecount> &child : ar->get(std::string{path}).children())
		{
			dirent dir;
			strncpy(dir.d_name, child.first.c_str(), 256); // TODO Path name length limit?
			zsr::node::ntype type = ar->index(child.second).type();
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
		zsr::iterator n = ar->get(std::string{path});
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

struct fuse_operations ops;

int main(int argc, char **argv)
{
	std::vector<std::string> args = util::argvec(argc, argv);
	argc--; argv++;
	if (args.size() < 2) help_exit();
	ar.reset(new zsr::archive{std::ifstream{args[1]}});
	ops.open = fs_open;
	ops.read = fs_read;
	ops.readlink = fs_readlink;
	ops.readdir = fs_readdir;
	ops.getattr = fs_getattr;
	ops.release = fs_release;
	return fuse_main(argc, argv, &ops, nullptr);
}

