#define CATCH_CONFIG_MAIN

#include "catch.hpp"
#include "Validation.h"

namespace {
	std::vector<std::string> valid = {
		"0.10.0-jb-eventide.1",
		"0.10.0-jb-eventide.0",
		"0.10.0-dev.0",
		"0.9.0-xnor-rpi-ossia.6",
		"0.9.0-xnor-rpi-ossia.5",
		"0.9.0-xnor-rpi-ossia.4",
		"0.9.0-fde-js-cdn.0",
		"0.9.0-bb-rpi-ossia-cli.1",
		"0.9.0-dev.32",
		"0.9.0-dev.31",
		"0.9.0-dev.30",
		"0.9.0-alpha.0"
	};
	std::vector<std::string> invalid = {
		"0.10.0-jb-eventide.1 & rm -rf /",
		"rm -rf /",
		"&& chown",
		"0.9.0&&",
		"&0.9.0-alpha.0",
		"0.9&.0-alpha.0",
		"0.9|.0-alpha.0",
		"0.9.0-alpha.0 || echo \"true\"",
		"0.9.0 || echo \"true\"",
		"0.9.0&&rm -rf /",
	};
}

TEST_CASE("Version validation", "[validation]") {
	for (auto& v: valid) {
		DYNAMIC_SECTION("valid " + v) {
			REQUIRE(validation::version(v));
		}
	}
	for (auto& v: invalid) {
		DYNAMIC_SECTION("invalid " + v) {
			REQUIRE(!validation::version(v));
		}
	}
}
