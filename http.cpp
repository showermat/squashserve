#include <fstream>
#include <regex>
#include "util.h"
#include "http.h"

namespace http
{
	
	doc::doc(const std::string path) : type{util::mimetype(path)}, content{""}
	{
		std::ifstream in{path};
		if (! in) throw std::runtime_error{"Couldn't open " + path + " for reading"};
		std::string buf{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}}; // FIXME Probably slow
		content = std::move(buf);
	}

	doc redirect(const std::string &url)
	{
		return doc{"text/html", "<html><head><meta http-equiv=\"refresh\" content=\"0;url=" + url + "\"></head></html>"};
	}

	std::string mkpath(const std::vector<std::string> &items)
	{
		return "/" + util::strjoin(items, '/');
	}

	std::string title(const std::string &content)
	{
		std::regex titlere{"<title>(.*?)</title>"};
		std::smatch match{};
		if (! std::regex_search(content, match, titlere)) return "";
		return match[1];
	}

	std::string strings(const std::string &content)
	{
		//std::regex bodyre{"(<body[^>]*>([\\w\\W]*)</body>)"}; // This doesn't work because of group length limit in C++ regex library
		std::regex bodyopen{"<body[^>]*>"};
		std::regex bodyclose{"</body>"};
		std::smatch bodymatch{};
		if (! std::regex_search(content, bodymatch, bodyopen)) return content; // TODO Need to escape HTML special characters
		std::string::size_type startidx = bodymatch.position() + bodymatch.length();
		if (! std::regex_search(content, bodymatch, bodyclose)) return content;
		std::string::size_type endidx = bodymatch.position();
		if (endidx < startidx) return content;
		std::string body = content.substr(startidx, endidx - startidx);
		body = std::regex_replace(body, std::regex{"<style>[\\w\\W]*</style>"}, ""); // TODO Will wipe out anything between two script or style blocks
		body = std::regex_replace(body, std::regex{"<script>[\\w\\W]*</script>"}, "");
		body = std::regex_replace(body, std::regex{"<[^>]+>"}, "");
		body = std::regex_replace(body, std::regex{"[\\s\\n]+"}, " ");
		return body;
	}

	int server::handle(struct mg_connection *conn, enum mg_event ev)
	{
		switch (ev)
		{
		case MG_AUTH:
			return MG_TRUE;
		case MG_REQUEST:
		{
			const doc d = static_cast<server *>(conn->server_param)->callback(std::string{conn->uri}, conn->query_string ? std::string{conn->query_string} : std::string{});
			mg_send_header(conn, "Content-type", d.type.c_str());
			mg_send_data(conn, d.content.data(), d.content.size());
			return MG_TRUE;
		}
		default:
			return MG_FALSE;
		}
	}

	server::server(int port, std::function<doc(std::string, std::string)> handler) : mgserver{nullptr}, callback{handler}
	{
		mgserver = mg_create_server(this, handle);
		const char *errmsg;
		errmsg = mg_set_option(mgserver, "listening_port", util::t2s(port).c_str());
		if (errmsg) throw std::runtime_error{std::string{errmsg}};
	}

	void server::serve(int timeout)
	{
		while (true) mg_poll_server(mgserver, timeout);
	}

	server::~server()
	{
		mg_destroy_server(&mgserver);
	}
}

