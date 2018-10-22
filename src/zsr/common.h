#ifndef ZSR_COMMON_H
#define ZSR_COMMON_H
#include "compress.h"
#include "diskmap.h"

#define VERBOSE

#ifdef VERBOSE
const std::string clrln{"\r\033[K"};
#define loga(msg) std::cout << clrln << util::timestr() << ": " << msg << std::endl
#define logb(msg) std::cout << clrln << msg << std::flush
#else
#define loga(msg)
#define logb(msg)
#endif

namespace zsr
{
	const std::string magic_number = "!ZSR";
	constexpr uint16_t version = 1;

	typedef uint64_t filecount; // Constrains the maximum number of files in an archive
	typedef uint64_t offset; // Constrains the size of the archive and individual files

	inline void boundcheck(const char *target, const char *bound)
	{
		//std::cerr << "Read to " << (size_t) target << " with bound " << (size_t) bound << " = " << (bound ? bound - target : 0) << "\n";
		if (bound && target > bound) throw std::runtime_error{"Tried to read " + util::t2s(target - bound) + " byte(s) past end of memory block"};
	}

	template <typename T> inline void serialize(std::ostream &out, const T &t) { out.write(reinterpret_cast<const char *>(&t), sizeof(t)); }
	template <> inline void serialize<std::string>(std::ostream &out, const std::string &t) { out.write(reinterpret_cast<const char *>(&t[0]), t.size()); }
	template <typename T> inline const T deser(const char *&ptr, const char *bound = nullptr)
	{
		boundcheck(ptr + sizeof(T), bound);
		const T *ret = reinterpret_cast<const T *>(ptr);
		ptr += sizeof(T);
		return *ret;
	}
	inline const std::string_view deser_string(const char *&ptr, uint16_t len, const char *bound = nullptr)
	{
		boundcheck(ptr + len, bound);
		std::string_view ret{ptr, len};
		ptr += len;
		return ret;
	}
	template <> inline const std::string_view deser<std::string_view>(const char *&ptr, const char *bound)
	{
		uint16_t len = deser<uint16_t>(ptr, bound);
		return deser_string(ptr, len, bound);
	}
}

#endif
