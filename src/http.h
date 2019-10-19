#ifndef ZSR_HTTP_H
#define ZSR_HTTP_H
#include <string>
#include <sstream>
#include <functional>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <onion/onion.hpp>
#include <onion/dict.hpp>
#include "util/util.h"

namespace http
{
	class doc
	{
	private:
		int status_;
		std::string type_;
		std::string content_;
		std::unordered_map<std::string, std::string> headers_;
	public:
		doc() : type_{"text/plain"}, content_{}, headers_{} { }
		doc(const std::string &type, const std::string &content, const std::unordered_map<std::string, std::string> &headers = {}) : status_{200}, type_{type}, content_{content}, headers_{headers} { }
		doc(const std::string &path, const std::unordered_map<std::string, std::string> &headers = {});
		void status(int value) { status_ = value; }
		int status() { return status_; }
		void content(const std::string &value) { content_ = value; }
		const std::string &content() const { return content_; }
		size_t size() const { return content_.size(); }
		void header(std::string key, std::string value) { headers_[key] = value; }
		const std::unordered_map<std::string, std::string> &headers();
	};

	class ipfilter // A proper implementation of this would use a radix tree to check successive bits of the incoming address against all stored addresses -- but this project's intended purpose involves a very small accept list, so let's go with an easier implementation for now.
	{
	private:
		std::unordered_map<uint32_t, uint32_t> whitelist;
	public:
		ipfilter(const std::string &accept);
		bool check(uint32_t addr);
	};

	class error : public std::runtime_error
	{
	private:
		static const std::unordered_map<int, std::pair<std::string, std::string>> messages;
		static const std::string msg_template;
		int status_;
	public:
		error(int status) : std::runtime_error{"HTTP " + util::t2s(status_)}, status_{status} { }
		void send(onion_response *res);
	};

	class server
	{
	private:
		onion *o;
		onion_handler *invoke_handle;
		std::function<doc(std::string, std::unordered_map<std::string, std::string>, uint32_t)> callback;
		ipfilter filter;
		static onion_connection_status handle(void *data, onion_request *req, onion_response *res);
		static void free_handler(void *data) { }
		// static void log(onion_log_level level, const char *filename, int lineno, const char *fmt, ...);
		// onion_log = server::log; // To set logger
	public:
		server(const std::string &addr, uint16_t port, std::function<doc(std::string, std::unordered_map<std::string, std::string>, uint32_t)> handler, const std::string &accept = "");
		void serve(int timeout = 1000);
		virtual ~server();
	};

	doc redirect(const std::string &url);
	std::string mkpath(const std::vector<std::string> &items);
	std::string title(const std::string &content, const std::string def = "");
}

#endif
