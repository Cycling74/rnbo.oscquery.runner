#include "Util.h"

#include <vector>
#include <iostream>
#include <chrono>
#include <iterator>
#include <algorithm>

namespace runner {
	std::string render_time_template(const std::string& tmpl) {
		const size_t size = 2 * (tmpl.size() + 1); //just pad it out a bunch
		std::vector<char> filename(size);

		std::time_t time = std::time({});
		if (std::strftime(filename.data(), size, tmpl.c_str(), std::gmtime(&time)) == 0) {
			std::cerr << "failed to render to time template " << tmpl << std::endl;
			filename[size - 1] = '\0';
		}

		return std::string(filename.data());
	}

	std::string sanitizeName(std::string n) {
		n.erase(std::remove_if(n.begin(), n.end(),
					[](unsigned char x) {
					return !(std::isalnum(x) || x == '-' || x == '_' || x == '.');
					}), n.end());
		return n;
	}

	std::string targetid() {
		static const std::string name = sanitizeName(rnbo_system_processor + "-" + rnbo_system_name + "-" + rnbo_compiler_id + "-" + rnbo_compiler_version);
		return name;
	}
}
