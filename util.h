#ifndef UTIL_H
#define UTIL_H
#include <string>
#include <set>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <experimental/optional>

//#ifdef DEBUG
//#define assert(test, msg) if (! (test)) throw std::runtime_error{msg}
//#define debug_print(msg) std::cerr << msg << "\n"
//#else
//#define assert(test, msg) 
//#define debug_print(msg) 
//#endif

template <typename T> using optional = std::experimental::optional<T>;

namespace util
{
	const char pathsep = '/';

	std::vector<std::string> strsplit(const std::string &str, char delim);

	std::string strjoin(const std::vector<std::string> &list, char delim, unsigned int start = 0, unsigned int end = 0);

	std::string pathjoin(const std::vector<std::string> &list);

	std::vector<std::string> argvec(int argc, char **argv);

	std::string asciilower(std::string str);

	template <typename T> std::string t2s(const T &t)
	{
		std::stringstream ret{};
		ret << t;
		return ret.str();
	}

	template <typename T> T s2t(const std::string &s)
	{
		std::stringstream ss{};
		ss << s;
		T ret;
		ss >> ret;
		return ret;
	}

	class timer
	{
	private:
		std::chrono::time_point<std::chrono::system_clock> last;
	public:
		void reset() { last = std::chrono::system_clock::now(); }
		double check(bool reset = false)
		{
			std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
			std::chrono::duration<double> elapsed = now - last;
			if (reset) this->reset();
			return elapsed.count();
		};
		timer() : last{} { reset(); }
	};

	std::string basename(std::string path, char sep = '/');

	std::string dirname(std::string path, char sep = '/');

	std::string exepath();


	bool fexists(const std::string &path);

	bool isdir(const std::string &path);

	std::set<std::string> ls(const std::string &dir, const std::string &test = "");

	std::vector<std::string> recursive_ls(const std::string &base, const std::string &test = "");

	int fast_atoi(const char *s);

	int fast_atoi(const std::string &s);

	std::string ext2mime(const std::string &path);

	std::string mimetype(const std::string &path, const std::vector<char> &data);

	std::string mimetype(const std::string &path);

	std::string urlencode(const std::string &str);

	std::string urldecode(const std::string &str);
}

#endif

