#include "compress.h"

namespace lzma
{
	void code(lzma_stream &stream, std::istream &in, std::ostream &out)
	{
		std::vector<char> inbuf(chunksize, 0), outbuf(chunksize, 0);
		while (true)
		{
			stream.next_in = reinterpret_cast<uint8_t *>(&inbuf[0]);
			in.read(&inbuf[0], inbuf.size());
			std::streamsize insize = in.gcount();
			stream.avail_in = insize;
			stream.total_in = 0;
			do
			{
				stream.total_out = 0;
				stream.next_out = reinterpret_cast<uint8_t *>(&outbuf[0]);
				stream.avail_out = outbuf.size();
				//std::cerr << stream.avail_in << ", " << stream.total_in << ", " << stream.avail_out << ", " << stream.total_out << "";
				lzma_ret retval = lzma_code(&stream, insize == 0 ? LZMA_FINISH : LZMA_RUN);
				//std::cerr << " => " << stream.avail_in << ", " << stream.total_in << ", " << stream.avail_out << ", " << stream.total_out << "\n";
				if (retval == LZMA_STREAM_END)
				{
					if (stream.avail_in != 0) throw compress_error{"Unprocessed input remaining in stream"};
					out.write(&outbuf[0], stream.total_out);
					lzma_end(&stream);
					return;
				}
				if (retval != LZMA_OK) throw compress_error{"(De)compression failed"};
				//std::cout << "Retrieved " << stream.total_in << " bytes and output " << stream.total_out << " bytes\n";
				out.write(&outbuf[0], stream.total_out);
			}
			while (stream.total_out > 0);
		}
	}
	
	void compress(std::istream &in, std::ostream &out)
	{
		lzma_stream stream = LZMA_STREAM_INIT;
		if (lzma_easy_encoder(&stream, compression, LZMA_CHECK_CRC64) != LZMA_OK) throw compress_error{"Stream encoder setup failed"};
		code(stream, in, out);
	}

	void decompress(std::istream &in, std::ostream &out)
	{
		lzma_stream stream = LZMA_STREAM_INIT;
		if (lzma_stream_decoder(&stream, memlimit, LZMA_CONCATENATED) != LZMA_OK) throw compress_error{"Stream decoder setup failed"};
		code(stream, in, out);
	}

	void buf_base::init(std::istream &file)
	{
		file_ = &file;
		ptr_ = 0;
		buf_.resize(chunksize);
		inbuf_.resize(chunksize);
		action_ = LZMA_RUN;
		lzma_ = LZMA_STREAM_INIT;
		char *end = &buf_[0] + buf_.size();
		setg(end, end, end);
	}

	buf_base::buf_base(buf_base &&orig) : file_{orig.file_}, ptr_{orig.ptr_}, lzma_{orig.lzma_}, buf_{std::move(orig.buf_)}, inbuf_{std::move(orig.inbuf_)}, action_{orig.action_}
	{
		orig.lzma_.internal = nullptr;
	}

	std::streambuf::int_type buf_base::underflow()
	{
		std::vector<char> input(chunksize, 0);
		if (gptr() >= egptr() && load() == 0) return traits_type::eof();
		return traits_type::to_int_type(*gptr());
	}

	void wrbuf::init(std::istream &file)
	{
		buf_base::init(file);
		if (lzma_easy_encoder(&lzma_, compression, LZMA_CHECK_CRC64) != LZMA_OK) throw compress_error{"Stream encoder setup failed"};
	}

	std::streamsize wrbuf::load() try
	{
		lzma_.next_out = reinterpret_cast<uint8_t *>(&buf_[0]);
		if (lzma_.total_out > 0)
		{
			lzma_.avail_out = buf_.size();
			lzma_.total_out = 0;
			lzma_ret retval = lzma_code(&lzma_, action_);
			if (retval != LZMA_OK && retval != LZMA_STREAM_END) throw compress_error{"Compression failed with liblzma error " + util::t2s(retval)};
			if (lzma_.total_out > 0)
			{
				char *start = &buf_[0], *end = start + lzma_.total_out;
				setg(start, start, end);
				//std::cout << "Compressed " << lzma_.total_out << " bytes\n";
				return lzma_.total_out;
			}
		}
		lzma_.next_out = reinterpret_cast<uint8_t *>(&buf_[0]);
		lzma_.total_out = 0;
		while (lzma_.total_out == 0)
		{
			lzma_.total_in = 0;
			lzma_.next_in = reinterpret_cast<uint8_t *>(&inbuf_[0]);
			file_->read(&inbuf_[0], inbuf_.size());
			std::streamsize insize = file_->gcount();
			if (insize == 0) action_ = LZMA_FINISH;
			ptr_ += insize;
			lzma_.avail_in = insize;
			lzma_.avail_out = buf_.size();
			lzma_ret retval = lzma_code(&lzma_, action_);
			//std::cout << "Got " << lzma_.total_in << " bytes, compressed to " << lzma_.total_out << "\n";
			if (retval == LZMA_STREAM_END) break;
			else if (retval != LZMA_OK) throw compress_error{"Compression failed with liblzma error " + util::t2s(retval)};
		}
		char *start = &buf_[0], *end = start + lzma_.total_out;
		setg(start, start, end);
		return lzma_.total_out;
	}
	catch (std::exception &e) { std::cerr << "Error: " << e.what() << "\n"; return 0; }
	
	void rdbuf::init(std::istream &file, std::streampos start, std::streampos size)
	{
		buf_base::init(file);
		start_ = start;
		size_ = size;
		if (lzma_stream_decoder(&lzma_, memlimit, LZMA_CONCATENATED) != LZMA_OK) throw compress_error{"Stream decoder setup failed"};
	}

	rdbuf::rdbuf(std::istream &file, std::streampos start, std::streampos size) : buf_base{}
	{
		init(file, start, size);
	}

	std::streamsize rdbuf::load() try
	{
		lzma_.next_out = reinterpret_cast<uint8_t *>(&buf_[0]);
		lzma_.avail_out = buf_.size();
		if (lzma_.total_out == buf_.size())
		{
			lzma_.total_out = 0;
			lzma_ret retval = lzma_code(&lzma_, action_);
			if (retval == LZMA_STREAM_END);
			else if (retval != LZMA_OK) throw compress_error{"Decompression failed with liblzma error " + util::t2s(retval)};
			if (lzma_.total_out > 0)
			{
				char *start = &buf_[0], *end = start + lzma_.total_out;
				setg(start, start, end);
				return lzma_.total_out;
			}
		}
		lzma_.total_in = lzma_.total_out = 0;
		lzma_.next_in = reinterpret_cast<uint8_t *>(&inbuf_[0]);
		file_->seekg(start_ + ptr_);
		file_->read(&inbuf_[0], static_cast<std::streamsize>(inbuf_.size()) < size_ - ptr_ ? inbuf_.size() : size_ - ptr_);
		std::streamsize insize = file_->gcount();
		ptr_ += insize;
		lzma_.avail_in = insize;
		if (insize == 0) action_ = LZMA_FINISH;
		lzma_ret retval = lzma_code(&lzma_, action_);
		if (retval == LZMA_STREAM_END);
		else if (retval != LZMA_OK) throw compress_error{"Decompression failed with liblzma error " + util::t2s(retval)};
		char *start = &buf_[0], *end = start + lzma_.total_out;
		setg(start, start, end);
		return lzma_.total_out;
	}
	catch (std::exception &e) { std::cerr << "Error: " << e.what() << "\n"; return 0; }
}

