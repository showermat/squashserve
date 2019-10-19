#include "http.h"

namespace http
{
	doc::doc(const std::string &path, const std::unordered_map<std::string, std::string> &headers) : type_{util::mimetype(path)}, content_{}, headers_{headers}
	{
		std::ifstream in{path};
		if (! in) throw std::runtime_error{"Couldn't open " + path + " for reading"};
		in.exceptions(std::ios_base::badbit);
		std::ostringstream buf{};
		buf << in.rdbuf();
		content_ = buf.str();
	}

	const std::unordered_map<std::string, std::string> &doc::headers()
	{
		if (! headers_.count("Content-Type")) headers_["Content-Type"] = type_;
		if (! headers_.count("Content-Length")) headers_["Content-Length"] = util::t2s(content_.size());
		return headers_;
	}

	doc redirect(const std::string &url)
	{
		doc ret{"text/html", "<html><head><meta charset=\"utf8\"><title>Moved</title></head><body>This content has moved to <a href=\"" + url + "\">" + url + "</a>.</body></html>"};
		ret.status(302);
		ret.header("Location", url);
		return ret;
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
		{405, {"Method Not Allowed", "Only GET requests are permitted by this server"}},
		{500, {"Internal Server Error", "An error occurred while processing your request"}}
	};

	const std::string error::msg_template = "<html><head><title>%s</title></head><body><h1>%s</h1><p>%s.</p></body></html>";

	void error::send(onion_response *res)
	{
		if (! messages.count(status_)) status_ = 500;
		const std::pair<const std::string, std::string> &msg = messages.at(status_);
		const char *title = msg.first.c_str();
		int bodylen = 71 + 2 * msg.first.size() + msg.second.size();
		onion_response_set_code(res, status_);
		onion_response_set_header(res, "Content-Type", "text/html");
		onion_response_set_header(res, "Content-Length", util::t2s(bodylen).c_str());
		onion_response_write_headers(res);
		onion_response_printf(res, msg_template.c_str(), title, title, msg.second.c_str());
	}

	void add_to_map(void *data, char *k, char *v, int flags)
	{
		static_cast<std::unordered_map<std::string, std::string> *>(data)->insert(std::pair<std::string, std::string>{std::string{k}, std::string{v}});
	}

	onion_connection_status server::handle(void *data, onion_request *req, onion_response *res) try
	{
		server *srv = static_cast<server *>(data);
		std::string method = onion_request_methods[(onion_request_get_flags(req) & 0x0F)];
		uint32_t remoteip = util::str2ip(std::string{onion_request_get_client_description(req)}); // I don't think the "description" is guaranteed to be an IP
		if (! srv->filter.check(remoteip)) throw error{403};
		if (method != "GET") throw error{405};
		std::unordered_map<std::string, std::string> query{};
		onion_dict_preorder(onion_request_get_query_dict(req), reinterpret_cast<void *>(add_to_map), &query);
		doc d = srv->callback(std::string{onion_request_get_fullpath(req)}, query, remoteip);
		onion_response_set_code(res, d.status());
		for (const std::pair<const std::string, std::string> &header : d.headers()) onion_response_set_header(res, header.first.c_str(), header.second.c_str());
		onion_response_write_headers(res);
		onion_response_write(res, d.content().data(), d.size());
		return OCS_PROCESSED;
	}
	catch (error &e) { e.send(res); return OCS_PROCESSED; }
	catch (std::exception &e) { error{500}.send(res); return OCS_PROCESSED; }

	server::server(const std::string &addr, uint16_t port, std::function<doc(std::string, std::unordered_map<std::string, std::string>, uint32_t)> handler, const std::string &accept) : o{onion_new(O_THREADED)}, callback{handler}, filter{accept}
	{
		invoke_handle = onion_handler_new(handle, static_cast<void *>(this), free_handler);
		onion_set_root_handler(o, invoke_handle);
		onion_set_hostname(o, addr.c_str());
		onion_set_port(o, "2235");
	}

	void server::serve(int timeout)
	{
		onion_listen(o);
	}

	server::~server()
	{
		onion_free(o);
	}
}
