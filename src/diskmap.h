#ifndef ZSR_DISKMAP_H
#define ZSR_DISKMAP_H
#include <map>
#include <fstream>
#include <functional>
#include <optional>
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
		const char *base_;
		size_t size_;
		std::function<K(const V &)> check_;
		std::pair<size_t, V> readent(const char *&ptr)
		{
			std::pair<size_t, V> ret = std::make_pair(*reinterpret_cast<const size_t *>(ptr), *reinterpret_cast<const V *>(ptr + sizeof(size_t)));
			ptr += skipsize;
			return ret;
		}
		const char *binsearch(const char *start, size_t len, size_t qhash)
		{
			if (len <= 0) return 0;
			const char *inptr = start + (len / 2) * skipsize;
			size_t testhash = readent(inptr).first;
			if (qhash == testhash) return inptr - skipsize;
			if (qhash < testhash) return binsearch(start, len / 2, qhash);
			return binsearch(inptr, len / 2 - (len + 1) % 2, qhash);
		}
	public:
		map(const char *in, std::function<K(const V &)> check) : check_{check}
		{
			size_ = *reinterpret_cast<const size_t *>(in);
			base_ = in + sizeof(size_t);
		}
		size_t size() { return size_; }
		std::optional<V> get(const K &query)
		{
			size_t qhash = std::hash<K>{}(query);
			const char *inptr = binsearch(base_, size_, qhash);
			while (inptr >= base_ && readent(inptr).first == qhash) inptr -= 2 * skipsize;
			if (inptr < base_) inptr = base_;
			while (inptr < base_ + size_ * skipsize)
			{
				std::pair<size_t, V> ent = readent(inptr);
				//std::cerr << qhash << " - " << ent.first << "\n";
				if (ent.first != qhash) return std::nullopt;
				if (check_(ent.second) == query) return ent.second;
			}
			return std::nullopt;
		}
		V operator[](size_t idx) // TODO Implement an actual iterator
		{
			if (idx >= size_) throw std::runtime_error{"Out of bounds"}; // TODO Throw a range_error
			const char *inptr = base_ + idx * skipsize;
			return readent(inptr).second;
		}
	};
}

#endif

