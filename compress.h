#include <streambuf>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <lzma.h>
#include "util.h"

namespace lzma
{
	const int compression = 6;
	const int memlimit = 1 * 1024 * 1024 * 1024;
	const int chunksize = 32 * 1024;

	void compress(std::istream &in, std::ostream &out);

	void decompress(std::istream &in, std::ostream &out);

	class compress_error : public std::runtime_error
	{
	public:
		compress_error(std::string msg) : runtime_error{msg} { }
	};

	class buf_base : public std::streambuf
	{
	protected:
		std::istream *file_;
		std::streampos ptr_;
		lzma_stream lzma_;
		std::vector<char> buf_, inbuf_;
		lzma_action action_;
		virtual std::streamsize load() = 0;
	public:
		buf_base() : file_{}, ptr_{}, lzma_{}, buf_{} { }
		buf_base(const buf_base &orig) = delete;
		buf_base(buf_base &&orig);
		virtual void init(std::istream &file);
		virtual void reset() = 0;
		int_type underflow();
		virtual ~buf_base() { if (lzma_.internal) lzma_end(&lzma_); }
	};

	class wrbuf : public buf_base
	{
	private:
		std::streamsize load();
	public:
		wrbuf() : buf_base{} { };
		wrbuf(std::istream &file) : buf_base{} { init(file); }
		wrbuf(const wrbuf &orig) = delete;
		wrbuf(wrbuf &&orig) : buf_base{std::move(orig)} { }
		void init(std::istream &file);
		void reset() { init(*file_); }
	};

	class rdbuf : public buf_base
	{
	private:
		std::streampos start_, size_;
		std::streamsize load();
	public:
		rdbuf() : buf_base{}, start_{}, size_{} { };
		rdbuf(std::istream &file, std::streampos start, std::streampos size);
		rdbuf(const rdbuf &orig) = delete;
		rdbuf(rdbuf &&orig) : buf_base{std::move(orig)}, start_{orig.start_}, size_{orig.size_} { }
		void init(std::istream &file, std::streampos start, std::streampos size);
		void reset() { init(*file_, start_, size_); }
	};
}

