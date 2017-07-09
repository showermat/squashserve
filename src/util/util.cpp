#include "util.h"
#include "mime.h"
#include "htmlent.h"

namespace util
{
	const int ftw_nopenfd = 256;

	std::vector<std::string> strsplit(const std::string &str, const std::string &delim)
	{
		std::vector<std::string> ret{};
		std::ostringstream cur{};
		std::size_t i = 0;
		while (i < str.size())
		{
			if (i <= str.size() - delim.size() && str.substr(i, delim.size()) == delim)
			{
				ret.push_back(cur.str());
				cur.str(std::string{});
				i += delim.size();
			}
			else
			{
				cur << str[i];
				i += 1;
			}
		}
		ret.push_back(cur.str());
		return ret;
	}

	std::vector<std::string> strsplit(const std::string &str, char delim)
	{
		return strsplit(str, std::string(1, delim));
	}

	std::string strjoin(const std::vector<std::string> &list, char delim, unsigned int start, unsigned int end)
	{
		if (! list.size()) return "";
		std::string ret{};
		int totsize = list.size() - 1;
		for (const std::string &item : list) totsize += item.size();
		ret.reserve(totsize);
		if (start >= list.size()) return ret;
		std::vector<std::string>::const_iterator iter = list.cbegin() + start;
		ret = *iter++;
		for (; iter != list.cend() - end && iter != list.cend(); iter++) ret += delim + *iter;
		return ret;
	}

	std::string pathjoin(const std::vector<std::string> &list)
	{
		return strjoin(list, pathsep);
	}

	std::vector<std::string> argvec(int argc, char **argv)
	{
		std::vector<std::string> ret{};
		for (int i = 0; i < argc; i++) ret.push_back(std::string{argv[i]});
		return ret;
	}

	std::pair<std::unordered_map<std::string, std::vector<std::string>>, std::vector<std::string>> argmap(unsigned int argc, char **argv, const std::string &valid, bool stop)
	{
		std::unordered_map<char, bool> valid_flags{};
		unsigned int i;
		for (i = 0; i < valid.size(); i++)
		{
			if (valid_flags.count(valid[i])) throw std::runtime_error{"Flag \"" + valid[i] + std::string{"\" specified twice"}};
			if (i < valid.size() - 1 && valid[i + 1] == ':') valid_flags[valid[i]] = true;
			else valid_flags[valid[i]] = false;
		}
		std::vector<std::string> args{};
		std::unordered_map<std::string, std::vector<std::string>> flags{};
		for (i = 1; i < argc; i++)
		{
			std::string arg{argv[i]};
			if (arg == "--")
			{
				i++;
				break;
			}
			else if (arg.size() <= 1 || arg[0] != '-')
			{
				if (stop) break;
				else args.push_back(arg);
			}
			else if (arg.size() > 2 && arg.substr(0, 2) == "--") args.push_back(arg);
			else
			{
				for (unsigned int j = 1; j < arg.size(); j++)
				{
					if (! valid_flags.count(arg[j])) throw std::runtime_error{"Invalid command-line flag \"" + std::string(1, arg[j]) + "\""};
					std::string flag(1, arg[j]);
					if (! flags.count(flag)) flags[flag] = {};
					if (! valid_flags[arg[j]]) flags[flag].push_back("");
					else
					{
						if (j < arg.size() - 1)
						{
							flags[flag].push_back(arg.substr(j + 1));
							break;
						}
						if (i == argc - 1) throw std::runtime_error{"Missing argument for command-line flag \"" + arg + "\""};
						flags[flag].push_back(std::string{argv[i + 1]});
						i++;
						break;
					}
				}
			}
		}
		for (; i < argc; i++) args.push_back(std::string{argv[i]});
		return std::make_pair(flags, args);
	}

	std::string gsub(const std::string &in, const std::string &find, const std::string &replace) // TODO Could be more efficient
	{
		std::ostringstream ret{};
		for (size_t idx = 0; idx + find.size() <= in.size();)
		{
			if (in.substr(idx, find.size()) == find)
			{
				ret << replace;
				idx += find.size();
			}
			else
			{
				ret << in[idx];
				idx += 1;
			}
		}
		return ret.str();
	}

	std::string conv(const std::string &in, const std::string &from, const std::string &to)
	{
		iconv_t cd = ::iconv_open(to.c_str(), from.c_str());
		if (cd == (iconv_t) -1) throw std::runtime_error{"Can't convert from " + from + " to " + to};
		std::stringstream ret{};
		std::string buf(256, '\0');
		size_t nin = in.size(), nout = buf.size();
		char *inaddr = const_cast<char *>(&in[0]); // TODO Valid use?
		char *outaddr = &buf[0];
		while (nin > 0)
		{
			size_t status = ::iconv(cd, &inaddr, &nin, &outaddr, &nout);
			if (status == (size_t) -1)
			{
				if (errno == E2BIG)
				{
					ret << buf.substr(0, buf.size() - nout);
					nout = buf.size();
					outaddr = &buf[0];
				}
				else throw std::runtime_error{"Couldn't convert string: " + std::string{::strerror(errno)}};
			}
		}
		buf.resize(outaddr - &buf[0]);
		ret << buf;
		::iconv_close(cd);
		return ret.str();
	}

	std::string env_or(const std::string &var, const std::string &def)
	{
		char *val = ::getenv(var.c_str());
		if (val) return std::string{val};
		return def;
	}

	std::string alnumonly(const std::string &str)
	{
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert{};
		std::basic_ostringstream<wchar_t> ss{};
		for (const wchar_t &c : convert.from_bytes(str)) if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_') ss << c;
		return convert.to_bytes(ss.str());
	}

	std::string utf8lower(const std::string &str)
	{
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert{"�", L"�"};
		std::basic_ostringstream<wchar_t> ss{};
		for (const wchar_t &c : convert.from_bytes(str)) ss << std::tolower(c, std::locale{ucslocale});
		return convert.to_bytes(ss.str());
	}

	template <> bool s2t<bool>(const std::string &s)
	{
		if (! s.size()) return false;
		if (s[0] == 'y' || s[0] == 'Y' || s[0] == 't' || s[0] == 'T') return true;
		return false;
	}

	std::string basename(std::string path, char sep) // TODO Rewrite
	{
		while (path.back() == sep) path = path.substr(0, path.size() - 1);
		return path.substr(path.rfind(sep) + 1);
	}

	std::string dirname(std::string path)
	{
		return normalize(path + "/..");
	}

	void rm(const std::string &path, bool isdir)
	{
		int fail;
		if (! isdir)
		{
			fail = ::unlink(path.c_str());
			if (! fail) return;
			if (fail && errno != EISDIR) throw std::runtime_error{"Could not unlink " + path + ": " + std::string{::strerror(errno)}};
		}
		fail = ::rmdir(path.c_str());
		if (fail) throw std::runtime_error{"Could not rmdir " + path + ": " + std::string{::strerror(errno)}};
	}

	bool rm_pred(const std::string &path, const struct stat *st, void *arg) // FIXME Need to add DEPTH option to fswalk
	{
		rm(path);
		return true;
	}

	void rm_recursive(const std::string &path)
	{
		fswalk(path, rm_pred, nullptr, fswopt::depth);
	}

	void mkdir(const std::string &path, int mode, bool skipexist)
	{
		if (::mkdir(path.c_str(), mode) < 0)
		{
			if (errno == EEXIST && skipexist) return;
			throw std::runtime_error{"Could not create directory " + path + ": " + std::string{::strerror(errno)}};
		}
	}

	void cp(const std::string &src, const std::string &dest)
	{
		std::ifstream in{src};
		if (! in) throw std::runtime_error{"Couldn't open " + src + " for reading"};
		std::ofstream out{dest};
		if (! out) throw std::runtime_error{"Couldn't open " + dest + " for writing"};
		out << in.rdbuf();
		if (! out) throw std::runtime_error{"Copy failed"};
	}

	std::string exepath()
	{
		std::string ret(2048, '\0');
		int len = ::readlink("/proc/self/exe", &ret[0], ret.size());
		ret.resize(len);
		return ret;
	}

	size_t fsize(const std::string &path)
	{
		struct stat statbuf;
		if (::stat(path.c_str(), &statbuf) < 0) throw std::runtime_error{"Couldn't stat file " + path + ": " + std::string{::strerror(errno)}};
		return static_cast<size_t>(statbuf.st_size);
	}

	std::string timestr(const std::string &fmt, std::time_t time)
	{
		std::stringstream ss{};
		ss << std::put_time(std::localtime(&time), fmt.c_str());
		return ss.str();
	}

	bool fexists(const std::string &path)
	{
		return ! ::access(path.c_str(), F_OK);
	}

	bool isdir(const std::string &path)
	{
		DIR *d = ::opendir(path.c_str());
		if (! d) return false;
		::closedir(d);
		return true;
	}

	bool isfile(const std::string &path)
	{
		return fexists(path) && ! isdir(path);
	}

	std::unordered_set<std::string> ls(const std::string &dir, const std::string &test)
	{
		std::unordered_set<std::string> ret{};
		std::regex *testre = nullptr;
		if (test != "") testre = new std::regex{test};
		DIR *d = ::opendir(dir.c_str());
		if (! d) throw std::runtime_error{"Couldn't open directory " + dir};
		struct dirent *file;
		while ((file = ::readdir(d)))
		{
			std::string fname{file->d_name};
			if (fname == "." || fname == "..") continue;
			if (testre && ! std::regex_search(fname, *testre)) continue;
			ret.insert(fname);
		}
		::closedir(d);
		delete testre;
		return ret;
	}

	void fswalk(const std::string &path, const std::function<bool(const std::string &, const struct stat *, void *)> &fn, void *userd, int flags)
	{
		int statret;
		struct stat sb;
		bool follow = flags & fswopt::follow, depth = flags & fswopt::depth;
		if (follow) statret = ::stat(path.c_str(), &sb);
		else statret = ::lstat(path.c_str(), &sb);
		if (statret) return; //throw std::runtime_error{"Couldn't stat " + path + ": " + std::string{strerror(errno)}}; // TODO Error handling?
		if (! depth && ! fn(path, &sb, userd)) return;
		if (! follow && (sb.st_mode & S_IFMT) == S_IFLNK && ::stat(path.c_str(), &sb)) return; // Re-process links as their destinations
		if ((sb.st_mode & S_IFMT) == S_IFDIR)
		{
			DIR *d = ::opendir(path.c_str());
			if (!d ) return; // TODO
			struct dirent *child;
			while ((child = ::readdir(d)))
			{
				std::string childname{child->d_name};
				if (childname != "." && childname != "..") fswalk(path + pathsep + childname, fn, userd, flags);
			}
			::closedir(d); // TODO Catch exceptions so that the directory is always closed
		}
		if (depth && ! fn(path, &sb, userd)) return;
	}

	bool ls_pred(const std::string &path, const struct stat *st, void *arg)
	{
		((std::unordered_set<std::string> *) arg)->insert(path);
		return true;
	}

	std::unordered_set<std::string> recursive_ls(const std::string &base, const std::string &test) // TODO test not currently being considered
	{
		std::unordered_set<std::string> ret{};
		fswalk(base, ls_pred, (void *) &ret, true);
		return ret;
	}

	std::string realpath(const std::string &path)
	{
		char *cret = ::realpath(path.c_str(), nullptr);
		if (! cret) return ""; // FIXME Error handling for incorrect input
		std::string ret{cret};
		::free(cret);
		return ret;
	}

	std::string normalize(const std::string &path)
	{
		if (! path.size()) return ".";
		std::vector<std::string> patharr = strsplit(path, pathsep);
		std::vector<std::string> ret{};
		if (patharr[0] == "") ret.push_back("");
		for (const std::string &elem : patharr)
		{
			if (elem == "" || elem == ".") continue;
			else if (elem == "..")
			{
				if (! ret.size() || ret.back() == "..") ret.push_back("..");
				else if (ret.size() == 1 && ret[0] == "") continue;
				else ret.pop_back();
			}
			else ret.push_back(elem);
		}
		if (! ret.size()) return ".";
		if (ret.size() == 1 && ret[0] == "") return "/";
		return strjoin(ret, pathsep);
	}

	std::string resolve(const std::string &base, const std::string &path)
	{
		if (path.size() && path[0] == pathsep) return normalize(path);
		return normalize(base + pathsep + path);
	}

	std::string linktarget(std::string path) // TODO Not handling errors
	{
		path = realpath(dirname(path)) + pathsep + basename(path);
		struct stat s;
		if (::lstat(path.c_str(), &s) != 0) return path;
		if ((s.st_mode & S_IFMT) != S_IFLNK) return path;
		std::string dir{dirname(path.c_str())};
		std::string ret{};
		ret.resize(s.st_size + 1);
		::readlink(path.c_str(), &ret[0], ret.size());
		ret.resize(ret.size() - 1);
		return resolve(dir, ret);
	}

	std::string relreduce(std::string base, std::string target)
	{
		base = normalize(base);
		target = normalize(target);
		if (base == ".") return target;
		if (base[0] == pathsep && target[0] != pathsep) return target;
		if (base[0] != pathsep && target[0] == pathsep) return target;
		std::vector<std::string> basearr = strsplit(base, pathsep);
		std::vector<std::string> targetarr = strsplit(target, pathsep);
		std::vector<std::string> ret{};
		std::vector<std::string>::size_type i, j;
		for (i = 0; i < basearr.size() && i < targetarr.size() && basearr[i] == targetarr[i]; i++);
		for (j = i; j < basearr.size(); j++) ret.push_back("..");
		for (j = i; j < targetarr.size(); j++) if (targetarr[j] != ".") ret.push_back(targetarr[j]);
		return strjoin(ret, pathsep);
	}

	bool is_under(std::string parent, std::string child)
	{
		parent = realpath(parent);
		child = realpath(child);
		if (parent == "/" and child[0] == '/') return true;
		if (child.substr(0, parent.size()) != parent) return false;
		if (child.size() == parent.size() || child[parent.size()] == util::pathsep) return true;
		return false;
	}

	int fast_atoi(const char *s)
	{
		int ret = 0;
		while (*s) ret = ret * 10 + *s++ - '0';
		return ret;
	}

	int fast_atoi(const std::string &s)
	{
		int ret = 0;
		for (std::string::const_iterator i = s.begin(); i != s.end(); i++) ret = ret * 10 + *i - '0';
		return ret;
	}

	std::string ext2mime(const std::string &path)
	{
		std::string::size_type idx = path.rfind(".");
		if (idx == path.npos) return "";
		std::string ext = utf8lower(path.substr(idx + 1));
		if (! mime_types.count(ext)) return "";
		return mime_types.at(ext);
	}

	std::string mimetype(const std::string &path, const std::string &data)
	{
		std::string extmime = ext2mime(path);
		if (extmime != "") return extmime;
		magic_t myt = ::magic_open(MAGIC_ERROR | MAGIC_MIME_TYPE);
		::magic_load(myt, nullptr);
		const char *type = ::magic_buffer(myt, &data[0], data.size());
		if (type == nullptr)
		{
			::magic_close(myt);
			return "application/octet-stream";
		}
		std::string ret{type};
		::magic_close(myt);
		return ret;
	}

	std::string mimetype(const std::string &path)
	{
		std::string extmime = ext2mime(path);
		if (extmime != "") return extmime;
		magic_t myt = ::magic_open(MAGIC_ERROR | MAGIC_MIME_TYPE);
		::magic_load(myt, nullptr);
		const char *type = ::magic_file(myt, path.c_str());
		if (type == nullptr)
		{
			::magic_close(myt);
			return "application/octet-stream";
		}
		std::string ret{type};
		::magic_close(myt);
		return ret;
	}

	std::string urlencode(const std::string &str)
	{
		std::stringstream ret{};
		unsigned int i = 0;
		while (i != str.size())
		{
			if ((str[i] >= 48 && str[i] <= 57) || (str[i] >= 65 && str[i] <= 90) || (str[i] >= 97 && str[i] <= 122)) ret << str[i];
			else ret << "%" << std::uppercase << std::hex << (int) str[i];
			i++;
		}
		return ret.str();
	}

	std::string urldecode(const std::string &str, bool plus)
	{
		std::ostringstream ret{};
		unsigned int i = 0;
		while (i != str.size())
		{
			if (plus && str[i] == '+') ret << ' ';
			else if (str[i] == '%' && i + 2 < str.size())
			{
				int c;
				std::stringstream buf{};
				buf << str.substr(i + 1, 2); // TODO Error checking
				buf >> std::hex >> c;
				ret << (char) c;
				i += 2;
			}
			else ret << str[i];
			i++;
		}
		return ret.str();
	}

	rangebuf::rangebuf(std::istream &src, std::streampos start, std::streampos size): src_{src}, buf_(blocksize), start_{start}, size_{size}, pos_{0}
	{
		setg(&buf_[0], &buf_[0], &buf_[0]);
	}

	std::streamsize rangebuf::fill()
	{
		src_.seekg(start_ + pos_);
		size_t readsize = std::min((std::streamoff) buf_.size(), size_ - pos_);
		src_.read(&buf_[0], readsize);
		setg(&buf_[0], &buf_[0], &buf_[0] + src_.gcount());
		return src_.gcount();
	}

	std::streambuf::int_type rangebuf::underflow()
	{
		pos_ += egptr() - eback();
		if (fill() == 0) return traits_type::eof();
		return traits_type::to_int_type(*gptr());
	}

	std::streambuf::pos_type rangebuf::seekpos(pos_type target, std::ios_base::openmode which)
	{
		if (target < 0) target = 0;
		if (target > size_) target = size_;
		if (target >= pos_ && target < pos_ + egptr() - eback()) setg(eback(), eback() + target - pos_, egptr());
		else
		{
			pos_ = target;
			fill();
		}
		return pos_ + gptr() - eback();
	}

	std::streambuf::pos_type rangebuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which)
	{
		if (dir == std::ios_base::beg) return seekpos(off, which);
		if (dir == std::ios_base::end) return seekpos(size_ + off, which);
		return seekpos(pos_ + gptr() - eback(), which);
	}

	std::streampos streamsize(std::istream &stream)
	{
		if (! stream) return 0;
		std::streampos reset = stream.tellg();
		stream.seekg(0, std::ios_base::end);
		std::streampos ret = stream.tellg() + static_cast<std::streampos>(1);
		stream.seekg(reset);
		return ret;
	}

	std::string from_htmlent(const std::string &str) // TODO Support numeric Unicode entities as well
	{
		std::ostringstream ret{}, curent{};
		bool entproc = false;
		std::string::const_iterator iter = str.cbegin();
		while (iter != str.cend())
		{
			if (entproc && *iter == ';')
			{
				if (htmlent.count(curent.str())) ret << htmlent.at(curent.str());
				else ret << '&' << curent.str() << ';';
				entproc = false;
				curent.str("");
				curent.clear();
			}
			else if (entproc && *iter == '&')
			{
				ret << '&' << curent.str();
				curent.str("");
				curent.clear();
			}
			else if (entproc && (*iter < 65 || *iter > 122 || (*iter < 97 && *iter > 90)))
			{
				ret << '&' << curent.str() << *iter;
				entproc = false;
				curent.str("");
				curent.clear();
			}
			else if (entproc) curent << *iter;
			else if (*iter == '&') entproc = true;
			else ret << *iter;
			iter++;
		}
		if (entproc) ret << '&' << curent.str();
		return ret.str();
	}

	std::string to_htmlent(const std::string &str)
	{
		static std::map<char, std::string> basic_ent{{'"', "quot"}, {'&', "amp"}, {'\'', "apos"}, {'<', "lt"}, {'>', "gt"}};
		std::ostringstream ret{};
		for (const char &c : str)
		{
			if (basic_ent.count(c)) ret << "&" << basic_ent[c] << ";";
			//else if (c <= L'~') ret << c; // What about control characters?
			//else ret << L"&#" << static_cast<wint_t>(c) << L";";
			else ret << c;
		}
		return ret.str();
	}

	uint32_t str2ip(const std::string &in) // NOTE Endian-specific!  Not portable between architectures
	{
		char ret[4];
		std::vector<std::string> octets = strsplit(in, '.');
		if (octets.size() != 4) throw std::runtime_error{"Wrong number of octets in IPv4 address " + in};
		for (int i = 0; i < 4; i++)
		{
			uint8_t octet = static_cast<uint8_t>(s2t<int>(octets[i])); // TODO Error checking
			ret[i] = octet;
		}
		return *reinterpret_cast<uint32_t *>(ret);
	}

	std::string ip2str(uint32_t in)
	{
		uint8_t *octets = reinterpret_cast<uint8_t *>(&in);
		std::stringstream ret{};
		for (int i = 0; i < 4; i++)
		{
			if (i > 0) ret << ".";
			ret << static_cast<int>(octets[i]);
		}
		return ret.str();
	}

	void *mmap_guard::open(const std::string &fname)
	{
		close();
		struct stat st;
		if (::stat(fname.c_str(), &st)) throw std::runtime_error{"Could not stat " + fname + ": " + std::string{::strerror(errno)}};
		fsize = st.st_size;
		fd = ::open(fname.c_str(), O_RDONLY);
		if (fd < 0) throw std::runtime_error{"Could not open " + fname + ": " + std::string{::strerror(errno)}};
		map = ::mmap(nullptr, fsize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
		if (map == MAP_FAILED)
		{
			::close(fd); // Ignore errors?
			map = nullptr;
			throw std::runtime_error{"Could not mmap " + fname + ": " + std::string{::strerror(errno)}};
		}
		return map;
	}

	void mmap_guard::close()
	{
		if (! map) return;
		if (::munmap(map, fsize)) throw std::runtime_error{"Munmap failed: " + std::string{::strerror(errno)}};
		if (::close(fd)) throw std::runtime_error{"Close failed: " + std::string{::strerror(errno)}};
	}

	int system(const std::string &bin, const std::vector<std::string> &args)
	{
		std::vector<const char *>cargs{};
		cargs.push_back(bin.c_str());
		for (const std::string &arg : args) cargs.push_back(arg.c_str());
		cargs.push_back(0);
		pid_t pid = fork();
		if (pid == -1) throw std::runtime_error{"Failed to fork"};
		if (pid == 0) execvp(bin.c_str(), (char **) &cargs[0]);
		int ret;
		wait(&ret);
		return ret;
	}

	std::pair<int, std::string> sysio(const std::string &bin, const std::vector<std::string> &args, const std::string &in, bool readout)
	{
		std::vector<const char *>cargs{};
		cargs.push_back(bin.c_str());
		for (const std::string &arg : args) cargs.push_back(arg.c_str());
		cargs.push_back(0);
		int infd[2], outfd[2];
		if (pipe(infd) || pipe(outfd)) throw std::runtime_error{"Failed to open pipe: " + std::string{strerror(errno)}};
		pid_t pid = fork();
		if (pid == -1) throw std::runtime_error{"Failed to fork: " + std::string{strerror(errno)}}; // TODO Close pipes
		if (pid == 0)
		{
			dup2(infd[0], 0); dup2(outfd[1], 1);
			close(infd[0]); close(infd[1]);
			close(outfd[0]); close(outfd[1]);
			if (! readout) close(1);
			execvp(bin.c_str(), (char **) &cargs[0]);
		}
		close(infd[0]); close(outfd[1]);
		if (write(infd[1], &in[0], in.size()) != (int) in.size()) throw std::runtime_error{"Failed to write all input"}; // FIXME Call write multiple times
		close(infd[1]);
		int ret; wait(&ret);
		std::string out, buf;
		buf.resize(256);
		while (true)
		{
			int count = read(outfd[0], &buf[0], buf.size());
			if (count == 0) break;
			if (count < 0) throw std::runtime_error{"Failed to read all output: " + std::string{strerror(errno)}};
			out += buf.substr(0, count);
		}
		close(outfd[0]);
		return std::make_pair(ret, out);
	}
}

