#include "http.h"

namespace http
{
	doc::doc(const std::string &path, const std::unordered_map<std::string, std::string> &headers) : type_{util::mimetype(path)}, content_{}, headers_{headers}
	{
		std::ifstream in{path};
		if (! in) throw std::runtime_error{"Couldn't open " + path + " for reading"};
		std::ostringstream buf{};
		buf << in.rdbuf();
		content_ = buf.str();
	}

	const std::unordered_map<std::string, std::string> &doc::headers()
	{
		if (! headers_.count("Content-type")) headers_["Content-type"] = type_;
		if (! headers_.count("Content-length")) headers_["Content-length"] = util::t2s(content_.size());
		return headers_;
	}

	doc redirect(const std::string &url)
	{
		return doc{"text/html", "<html><head><meta charset=\"utf8\"><meta http-equiv=\"refresh\" content=\"0;url=" + url + "\"></head></html>"};
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

	ipfilter::ipfilter(const std::string &accept)
	{
		if (accept == "") whitelist[0] = 0;
		else for (const std::string &straddr : util::strsplit(accept, ','))
		{
			std::vector<std::string> components = util::strsplit(straddr, '/');
			if (components.size() > 2) throw std::runtime_error{"Malformed CIDR specification " + straddr};
			uint32_t addr = util::str2ip(components[0]);
			int masksize = 32;
			if (components.size() == 2) masksize = util::s2t<int>(components[1]);
			if (masksize < 0) masksize = 0;
			uint32_t mask = ~0;
			if (masksize < 32) mask = ~((1 << (32 - masksize)) - 1);
			whitelist[addr] = mask;
		}
	}

	bool ipfilter::check(uint32_t addr)
	{
		for (const std::pair<const uint32_t, int> &pair : whitelist) if ((addr & pair.second) == (pair.first & pair.second)) return true;
		return false;
	}

	const std::unordered_map<int, std::pair<std::string, std::string>> error::messages{
		{400, {"Bad Request", "The request you made is not valid"}},
		{403, {"Forbidden", "You are not authorized to view this page"}},
		{405, {"Method Not Allowed", "Only GET requests are supported by this server"}},
		{500, {"Internal Server Error", "An error occurred while processing your request"}}
	};

	const std::string error::msg_template = "HTTP/1.1 %d %s\r\nContent-type: text/html\r\nContent-length: %d\r\n\r\n<html><head><title>%s</title></head><body><h1>%s</h1><p>%s.</p></body></html>";

	void error::send(struct mg_connection *conn)
	{
		if (! messages.count(status_)) status_ = 500;
		const std::pair<const std::string, std::string> &msg = messages.at(status_);
		const char *title = msg.first.c_str();
		int bodylen = 71 + 2 * msg.first.size() + msg.second.size();
		mg_printf(conn, msg_template.c_str(), status_, title, bodylen, title, title, msg.second.c_str());
	}

	void server::handle(struct mg_connection *conn, int ev, void *data) try
	{
		if (ev != MG_EV_HTTP_REQUEST) return;
		server *srv = static_cast<server *>(conn->mgr->user_data);
		uint32_t remoteip = conn->sa.sin.sin_addr.s_addr;
		if (! srv->filter.check(remoteip)) throw error{403};
		struct http_message *msg = static_cast<struct http_message *>(data);
		if (std::string{msg->method.p, msg->method.len} != "GET") throw error{405};
		doc d = srv->callback(std::string{msg->uri.p, msg->uri.len}, std::string{msg->query_string.p, msg->query_string.len}, remoteip);
		std::ostringstream head{};
		head << "HTTP/1.1 200 OK\r\n";
		for (const std::pair<const std::string, std::string> &header : d.headers()) head << header.first << ": " << header.second << "\r\n";
		head << "\r\n";
		mg_printf(conn, "%s", head.str().c_str());
		mg_send(conn, d.content().data(), d.size());
	}
	catch (error &e) { e.send(conn); }
	catch (std::exception &e) { error{500}.send(conn); }

	server::server(uint16_t port, std::function<doc(std::string, std::string, uint32_t)> handler, const std::string &accept) : mgr{}, callback{handler}, filter{accept}
	{
		mg_mgr_init(&mgr, this);
		mg_connection *conn = mg_bind(&mgr, util::t2s(port).c_str(), handle);
		if (! conn) throw std::runtime_error{"Unable to create socket connection"};
		mg_set_protocol_http_websocket(conn);
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

