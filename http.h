#ifndef HTTP_H
#define HTTP_H
#include <string>
#include <functional>
#include "mongoose.h"

namespace http
{
	struct doc
	{
		std::string type;
		std::string content;
		doc(): type{"text/plain"}, content{} { }
		doc(const std::string &t, const std::string &c) : type{t}, content{c} { }
		doc(const std::string &t, const std::vector<char> &c) : type{t}, content{&c[0], c.size()} { }
		doc(const std::string path);
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
	//std::string encoding(const doc &content);
	//std::string decode(const std::string content);
	std::string title(const std::string &content, const std::string def = "");
	std::string strings(const std::string &content);
}

#endif

