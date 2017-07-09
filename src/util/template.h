#ifndef UTIL_TEMPLATE_H
#define UTIL_TEMPLATE_H
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include "util.h"

namespace templ
{
	std::vector<std::string> split(const std::string &in, unsigned int sects, const std::string &id, const std::string &sep = "%");
	std::vector<std::string> split(const std::string &in, const std::string &sep = "%");
	std::string render(const std::string &in, const std::unordered_map<std::string, std::string> &vars);
}

#endif

