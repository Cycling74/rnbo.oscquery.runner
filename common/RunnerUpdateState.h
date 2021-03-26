#pragma once

#include <stdint.h>
#include <vector>
#include <string>

enum class RunnerUpdateState : uint32_t {
	Idle = 0,
	Active = 1,
	Failed = 2
};

namespace runner_update {
	bool from(uint32_t in, RunnerUpdateState& out);
	std::string into(const RunnerUpdateState& s);
	std::vector<std::string> all();
}
