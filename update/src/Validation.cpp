#include <regex>

namespace {
	//https://www.debian.org/doc/debian-policy/ch-controlfields.html#version
	const std::regex VERSION_VALID_REGEX(R"X(^(?:\d+:)?\d[[:alnum:]\.\+\-~]*?(?:-[[:alnum:]\+\.~]+)?$)X");
}

namespace validation {
	bool version(const std::string& version) {
		return std::regex_match(version, VERSION_VALID_REGEX);
	}
}
