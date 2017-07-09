#include "template.h"

namespace templ
{
	std::vector<std::string> split(const std::string &in, unsigned int sects, const std::string &id, const std::string &sep)
	{
		std::vector<std::string> ret = util::strsplit(in, "\n" + sep + "\n");
		if (sects > 0 && ret.size() != sects) throw std::runtime_error{"Expected " + util::t2s(sects) + " sections in template " + id + ", but got " + util::t2s(ret.size())};
		return ret;
	}

	std::vector<std::string> split(const std::string &in, const std::string &sep)
	{
		return split(in, 0, "", sep);
	}

	bool test(const std::string &expr, const std::unordered_map<std::string, std::string> &vars)
	{
		const static std::regex re_equal{"^(\\w+)=(.+)\\?$"}, re_exist{"^(\\w+)\\?$"};
		std::smatch match{};
		if (std::regex_match(expr, match, re_equal)) return vars.count(match[1]) && vars.at(match[1]) == match[2];
		if (std::regex_match(expr, match, re_exist)) return vars.count(match[1]);
		throw std::runtime_error{"Invalid test expression " + expr};
	}

	std::string eval(const std::string &expr, const std::unordered_map<std::string, std::string> &vars)
	{
		const static std::regex re_ternequal{"^(\\w+)=(.+?)\\?(.*?):(.*)$"}, re_ternary{"^(\\w+)\\?(.*?):(.*)$"}, re_sub{"^(\\w+)$"};
		std::smatch match{};
		if (std::regex_match(expr, match, re_ternequal))
			return (vars.count(match[1]) && vars.at(match[1]) == match[2]) ? match[3] : match[4];
		if (std::regex_match(expr, match, re_ternary))
			return (vars.count(match[1])) ? match[2] : match[3];
		if (std::regex_match(expr, match, re_sub))
			return (vars.count(match[1])) ? vars.at(match[1]) : "";
		throw std::runtime_error{"Invalid evaluation expression " + expr};
	}

	std::string render(const std::string &in, const std::unordered_map<std::string, std::string> &vars)
	{
		const static std::regex re_token{"\\{\\{(.*?)\\}\\}"};
		std::ostringstream ret{};
		int count = 0, nsilent = 0;
		std::vector<bool> echo{};
		for (std::sregex_token_iterator iter{in.begin(), in.end(), re_token, {-1, 1}}; iter != std::sregex_token_iterator{}; iter++)
		{
			const std::string &cur = iter->str();
			std::string res = "";
			if (count++ % 2 == 0) res = cur;
			else
			{
				if (! cur.size()) continue;
				if (cur[0] == '#')
				{
					echo.push_back(test(cur.substr(1), vars));
					nsilent += echo.back() ? 0 : 1;
				}
				else if (cur[0] == '/') // Do we want to restrict what comes after the slash?
				{
					if (! echo.size()) throw std::runtime_error{"Unmatched end of block in template render"};
					nsilent -= echo.back() ? 0 : 1;
					echo.pop_back();
				}
				else res = eval(cur, vars);
			}
			if (! nsilent) ret << res;
		}
		return ret.str();
	}
}

