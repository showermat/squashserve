#ifndef ZSR_DISKMAP_H
#define ZSR_DISKMAP_H
#include <map>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace diskmap
{
	template<typename K, typename V> class writer
	{
	private:
		std::map<size_t, V> items_;
	public:
		static constexpr size_t hdrsize = sizeof(size_t);
		static constexpr size_t recsize = sizeof(size_t) + sizeof(V);
		void add(const K &k, const V &v) { items_[std::hash<K>{}(k)] = v; }
		void write(std::ostream &out)
		{
			size_t size = items_.size();
			out.write(reinterpret_cast<char *>(&size), sizeof(size_t));
			for (const std::pair<size_t, V> &item : items_)
			{
				out.write(reinterpret_cast<const char *>(&item.first), sizeof(size_t));
				out.write(reinterpret_cast<const char *>(&item.second), sizeof(V));
			}
		}
	};

	template<typename K, typename V> class map
	{
	private:
		static constexpr size_t skipsize = sizeof(size_t) + sizeof(V);
		std::istream &in_;
		std::streampos start_;
		size_t size_;
		std::function<K(const V &)> check_;
		K safecheck(const V &v)
		{
			std::streampos oldpos = in_.tellg();
			K ret = check_(v);
			if (oldpos != in_.tellg()) in_.seekg(oldpos);
			return ret;
		}
		std::pair<size_t, V> readent()
		{
			std::pair<size_t, V> ret{};
			in_.read(reinterpret_cast<char *>(&ret.first), sizeof(size_t));
			in_.read(reinterpret_cast<char *>(&ret.second), sizeof(size_t));
			return ret;
		}
		std::streampos binsearch(std::streampos start, size_t len, size_t qhash)
		{
			if (len <= 0) return 0;
			in_.seekg(start + static_cast<std::streampos>((len / 2) * skipsize));
			size_t testhash = readent().first;
			if (qhash == testhash) return in_.tellg() - static_cast<std::streampos>(skipsize);
			if (qhash < testhash) return binsearch(start, len / 2, qhash);
			return binsearch(in_.tellg(), len / 2 - (len + 1) % 2, qhash);
		}
	public:
		map(std::istream &in, std::function<K(const V &)> check) : in_{in}, check_{check}
		{
			in_.read(reinterpret_cast<char *>(&size_), sizeof(size_t));
			start_ = in_.tellg();
		}
		size_t size() { return size_; }
		std::pair<bool, V> get(const K &query) // Pending the existence of std::optional...
		{
			size_t qhash = std::hash<K>{}(query);
			in_.seekg(binsearch(start_, size_, qhash));
			while (in_.tellg() >= start_ && readent().first == qhash) in_.seekg(-2 * static_cast<int>(skipsize), std::ios::cur);
			if (in_.tellg() < start_) in_.seekg(start_);
			while (in_.tellg() < start_ + static_cast<std::streampos>(size_ * skipsize))
			{
				std::pair<size_t, V> ent = readent();
				//std::cerr << qhash << " - " << ent.first << "\n";
				if (ent.first != qhash) return std::make_pair(false, V{});
				if (safecheck(ent.second) == query) return std::make_pair(true, ent.second);
			}
			return std::make_pair(false, V{});
		}
		V operator[](size_t idx) // TODO Implement an actual iterator
		{
			if (idx >= size_) throw std::runtime_error{"Out of bounds"}; // TODO Throw a range_error
			in_.seekg(start_ + static_cast<std::streampos>(idx * skipsize));
			return readent().second;
		}
	};
}

#endif

