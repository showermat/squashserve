#ifndef HTTP_H
#define HTTP_H
#include <string>
#include <functional>
#include <unordered_map>
#include <sstream>
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
		doc(const std::string path, const std::unordered_map<std::string, std::string> &headers = {});
		void content(const std::string &value) { content_ = value; }
		const std::string &content() const { return content_; }
		size_t size() const { return content_.size(); }
		void header(std::string key, std::string value) { headers_[key] = value; }
		const std::unordered_map<std::string, std::string> &headers();
	};

	class server
	{
	private:
		struct mg_mgr mgr;
		std::function<doc(std::string, std::string)> callback;
		static void handle(struct mg_connection *conn, int ev, void *data);
	public:
		server(int port, std::function<doc(std::string, std::string)> handler);
		void serve(int timeout = 1000);
		virtual ~server();
	};
	
	doc redirect(const std::string &url);
	std::string mkpath(const std::vector<std::string> &items);
	std::string title(const std::string &content, const std::string def = "");
	std::string strings(const std::string &content);
}

#endif

