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

	std::string title(const std::string &content, const std::string def)
	{
		std::regex titlere{"<title>(.*?)</title>"};
		std::smatch match{};
		if (! std::regex_search(content, match, titlere)) return def;
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

	void server::handle(struct mg_connection *conn, int ev, void *data)
	{
		switch (ev)
		{
		case MG_EV_HTTP_REQUEST:
			struct http_message *msg = static_cast<struct http_message *>(data);
			const doc d = static_cast<server *>(conn->mgr->user_data)->callback(std::string{msg->uri.p, msg->uri.len}, std::string{msg->query_string.p, msg->query_string.len});
			mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-type: %s\r\nContent-length: %lu\r\n\r\n", d.type.c_str(), d.content.size());
			mg_send(conn, d.content.data(), d.content.size());
			//mg_send_header(conn, "Content-type", d.type.c_str());
			//mg_send_data(conn, d.content.data(), d.content.size());
		}
	}

	server::server(int port, std::function<doc(std::string, std::string)> handler) : mgr{}, callback{handler}
	{
		mg_mgr_init(&mgr, this);
		mg_connection *conn = mg_bind(&mgr, util::t2s(port).c_str(), handle);
		mg_set_protocol_http_websocket(conn);
		//mgserver = mg_create_server(this, handle);
		//const char *errmsg;
		//errmsg = mg_set_option(mgserver, "listening_port", util::t2s(port).c_str());
		//if (errmsg) throw std::runtime_error{std::string{errmsg}};
	}

	void server::serve(int timeout)
	{
		while (true) mg_mgr_poll(&mgr, timeout);
	}

	server::~server()
	{
		mg_mgr_free(&mgr);
	}
}

