#ifndef ZSR_COMPRESS_H
#define ZSR_COMPRESS_H
#include <streambuf>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <lzma.h>
#include "../util/util.h"

namespace lzma
{
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
		lzma_stream lzma_;
		std::vector<char> buf_, inbuf_;
		lzma_action action_;
		pos_type pos_;
		virtual std::streamsize fill() = 0;
		virtual std::streamsize load();
	public:
		buf_base() : file_{}, lzma_{}, buf_{}, inbuf_{} { }
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
		std::streamsize fill();
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
		std::streampos ptr_, start_, size_, decomp_;
		std::streamsize fill();
	public:
		rdbuf() : buf_base{}, ptr_{}, start_{}, size_{}, decomp_{} { };
		rdbuf(std::istream &file, std::streampos start, std::streampos size, std::streampos decomp);
		rdbuf(const rdbuf &orig) = delete;
		rdbuf(rdbuf &&orig) : buf_base{std::move(orig)}, ptr_{orig.ptr_}, start_{orig.start_}, size_{orig.size_}, decomp_{orig.decomp_} { }
		pos_type seekpos(pos_type target, std::ios_base::openmode which);
		pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which);
		void init(std::istream &file, std::streampos start, std::streampos size, std::streampos decomp);
		void reset() { init(*file_, start_, size_, decomp_); }
	};

	class memrdbuf : public std::streambuf
	{
	private:
		const char *source_;
		std::vector<char> buf_;
		lzma_stream lzma_;
		lzma_action action_;
		const pos_type size_, start_, len_;
		pos_type pos_; // The offset of the first byte in the buffer relative to the beginning of the entire file (not start_)
		void reset();
		std::streamsize load();
		pos_type ff_to(std::streambuf::pos_type target);
	public:
		memrdbuf(const char *source, std::streampos size, std::streampos start, std::streampos len);
		memrdbuf(const memrdbuf &orig) = delete;
		memrdbuf(memrdbuf &&orig) : std::streambuf{std::move(orig)}, source_{orig.source_}, buf_{std::move(orig.buf_)},
			lzma_{orig.lzma_}, action_{orig.action_}, size_{orig.size_}, start_{orig.start_}, len_{orig.len_}, pos_{orig.pos_}
			{ orig.lzma_.internal = nullptr; }
		int_type underflow();
		pos_type seekpos(pos_type target, std::ios_base::openmode which);
		pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which);
	};
}

#endif
