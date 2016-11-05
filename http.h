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
#include "util/util.h"
#include "mongoose.h"

namespace http
{
	class doc
	{
	private:
		std::string type_;
		std::string content_;
		std::unordered_map<std::string, std::string> headers_;
	public:
		doc(): type_{"text/plain"}, content_{}, headers_{} { }
		doc(const std::string &type, const std::string &content, const std::unordered_map<std::string, std::string> &headers = {}) : type_{type}, content_{content}, headers_{headers} { }
		doc(const std::string &path, const std::unordered_map<std::string, std::string> &headers = {});
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

	class server
	{
	private:
		struct mg_mgr mgr;
		std::function<doc(std::string, std::string, uint32_t)> callback;
		ipfilter filter;
		static std::string denied_msg;
		static void handle(struct mg_connection *conn, int ev, void *data);
	public:
		server(uint16_t port, std::function<doc(std::string, std::string, uint32_t)> handler, const std::string &accept = "");
		void serve(int timeout = 1000);
		virtual ~server();
	};
	
	doc redirect(const std::string &url);
	std::string mkpath(const std::vector<std::string> &items);
	std::string title(const std::string &content, const std::string def = "");
}

#endif

