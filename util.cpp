#include "util.h"
#include "mime.h"
#include "htmlent.h"

namespace util
{
	const int ftw_nopenfd = 256;

	std::vector<std::string> strsplit(const std::string &str, char delim)
	{
		std::vector<std::string> ret{};
		std::string cur{};
		for (std::string::const_iterator iter = str.cbegin(); iter != str.cend(); iter++)
		{
			if (*iter == delim)
			{
				if (! cur.size()) continue;
				ret.push_back(cur);
				cur.clear();
			}
			else cur += *iter;
		}
		if (cur.size()) ret.push_back(cur);
		return ret;
	}

	std::string strjoin(const std::vector<std::string> &list, char delim, unsigned int start, unsigned int end)
	{
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

	std::string conv(const std::string &in, const std::string &from, const std::string &to)
	{
		iconv_t cd = iconv_open(to.c_str(), from.c_str());
		if (cd == (iconv_t) -1) throw std::runtime_error{"Can't convert from " + from + " to " + to};
		std::stringstream ret{};
		std::string buf(256, '\0');
		size_t nin = in.size(), nout = buf.size();
		char *inaddr = const_cast<char *>(&in[0]); // TODO Valid use?
		char *outaddr = &buf[0];
		while (nin > 0)
		{
			size_t status = iconv(cd, &inaddr, &nin, &outaddr, &nout);
			if (status == (size_t) -1)
			{
				if (errno == E2BIG)
				{
					ret << buf.substr(0, buf.size() - nout);
					nout = buf.size();
					outaddr = &buf[0];
				}
				else throw std::runtime_error{"Couldn't convert string: " + std::string{strerror(errno)}};
			}
		}
		buf.resize(outaddr - &buf[0]);
		ret << buf;
		iconv_close(cd);
		return ret.str();
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
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert{};
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

	std::string basename(std::string path, char sep)
	{
		while (path.back() == sep) path = path.substr(0, path.size() - 1);
		return path.substr(path.rfind(sep) + 1);
	}

	std::string dirname(std::string path, char sep)
	{
		while (path.back() == sep) path = path.substr(0, path.size() - 1);
		return path.substr(0, path.rfind(sep));
	}

	void rm(const std::string &path)
	{
		if (unlink(path.c_str())) throw std::runtime_error{"Could not remove " + path + ": " + std::string{strerror(errno)}};
	}

	int ftw_pred_rm(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
	{
		if (typeflag == FTW_F || typeflag == FTW_SL || typeflag == FTW_SLN) return unlink(fpath) ? errno : 0;
		if (typeflag == FTW_DP) return rmdir(fpath) ? errno : 0;
		if (typeflag == FTW_DNR || typeflag == FTW_NS) return EACCES;
		return ENOSYS;
	}

	void rm_recursive(const std::string &path)
	{
		int ret = nftw(path.c_str(), &ftw_pred_rm, ftw_nopenfd, FTW_MOUNT | FTW_PHYS | FTW_DEPTH);
		if (ret) throw std::runtime_error{std::string{strerror(ret)}};
	}
	
	std::string exepath()
	{
		std::string ret(2048, '\0');
		int len = readlink("/proc/self/exe", &ret[0], ret.size());
		ret.resize(len);
		return ret;
	}

	std::size_t fsize(const std::string &path)
	{
		struct stat statbuf;
		if (stat(path.c_str(), &statbuf) < 0) throw std::runtime_error{"Couldn't stat file " + path};
		return static_cast<std::size_t>(statbuf.st_size);
	}

	std::string timestr(const std::string &fmt, std::time_t time)
	{
		std::stringstream ss{};
		ss << std::put_time(std::localtime(&time), fmt.c_str());
		return ss.str();
	}

	bool fexists(const std::string &path)
	{
		return access(path.c_str(), F_OK);
	}

	bool isdir(const std::string &path)
	{
		DIR *d = opendir(path.c_str());
		if (! d) return false;
		closedir(d);
		return true;
	}

	std::unordered_set<std::string> ls(const std::string &dir, const std::string &test)
	{
		std::unordered_set<std::string> ret{};
		std::regex *testre = nullptr;
		if (test != "") testre = new std::regex{test};
		DIR *d = opendir(dir.c_str());
		if (! d) throw std::runtime_error{"Couldn't open directory " + dir};
		struct dirent *file;
		while ((file = readdir(d)))
		{
			std::string fname{file->d_name};
			if (fname == "." || fname == "..") continue;
			if (testre && ! std::regex_search(fname, *testre)) continue;
			ret.insert(fname);
		}
		closedir(d);
		delete testre;
		return ret;
	}

	static std::vector<std::string> files{};
	static const std::regex *ftwtest;

	int ftw_pred_ls(const char *path, const struct stat *info, int type)
	{
		if (type == FTW_F && (! ftwtest || std::regex_search(path, *ftwtest))) files.push_back(path);
		return 0;
	}

	std::vector<std::string> recursive_ls(const std::string &base, const std::string &test) // NOT THREADSAFE
	{
		files.clear();
		std::regex ftwtest_base{};
		if (test == "") ftwtest = nullptr;
		else { ftwtest_base = std::regex{test}; ftwtest = &ftwtest_base; }
		glob_t globbuf;
		if (glob(base.c_str(), GLOB_NOSORT | GLOB_BRACE | GLOB_NOCHECK, nullptr, &globbuf)) throw std::runtime_error{"Glob failed on pattern " + base};
		for (unsigned int i = 0; i < globbuf.gl_pathc; i++) ftw(globbuf.gl_pathv[i], &ftw_pred_ls, ftw_nopenfd);
		globfree(&globbuf);
		return files;
	}

	/*std::vector<std::string> recursive_ls(const std::string &path, const std::string &ext)
	{
		std::vector<std::string> ret{};
		DIR *dir = opendir(path.c_str());
		if (! dir) throw std::runtime_error{"Could not open " + path + ": " + strerror(errno)};
		struct dirent *ent;
		while (ent = readdir(dir))
		{
			std::string name{ent->d_name};
			int type = ent->d_type;
			std::string newpath{path + "/" + name};
			if (type == DT_REG)
			{
				const std::vector<std::string> &tok = util::strsplit(name, '.');
				if (ext != "" && tok[tok.size() - 1] == ext) ret.push_back(newpath);
			}
			else if (type == DT_DIR)
			{
				const std::vector<std::string> &rec = recursive_ls(newpath, ext);
				ret.insert(ret.end(), rec.begin(), rec.end());
			}
		}
		closedir(dir);
		return ret;
	}*/

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
		magic_t myt = magic_open(MAGIC_ERROR | MAGIC_MIME);
		magic_load(myt, nullptr);
		const char *type = magic_buffer(myt, &data[0], data.size());
		if (type == nullptr)
		{
			magic_close(myt);
			return "application/octet-stream";
		}
		std::string ret{type};
		magic_close(myt);
		return ret;
	}

	std::string mimetype(const std::string &path)
	{
		std::string extmime = ext2mime(path);
		if (extmime != "") return extmime;
		magic_t myt = magic_open(MAGIC_ERROR | MAGIC_MIME);
		magic_load(myt, nullptr);
		const char *type = magic_file(myt, path.c_str());
		if (type == nullptr)
		{
			magic_close(myt);
			return "application/octet-stream";
		}
		std::string ret{type};
		magic_close(myt);
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

	std::string urldecode(const std::string &str)
	{
		std::ostringstream ret{};
		unsigned int i = 0;
		while (i != str.size())
		{
			if (str[i] == '%' && i + 2 < str.size())
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
		static std::map<wchar_t, std::wstring> basic_ent{{L'"', L"quot"}, {L'&', L"amp"}, {L'\'', L"apos"}, {L'<', L"lt"}, {L'>', L"gt"}};
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert{};
		std::basic_ostringstream<wchar_t> ret{};
		for (const wchar_t &c : convert.from_bytes(str))
		{
			if (basic_ent.count(c)) ret << L"&" << basic_ent[c] << L";";
			//else if (c <= L'~') ret << c; // What about control characters?
			//else ret << L"&#" << static_cast<wint_t>(c) << L";";
			else ret << c;
		}
		return convert.to_bytes(ret.str());
	}
}
