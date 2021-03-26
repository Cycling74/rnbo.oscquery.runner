#include "RunnerUpdateState.h"

namespace runner_update {
	bool from(uint32_t in, RunnerUpdateState& out) {
		if (in < 3) {
			out = static_cast<RunnerUpdateState>(in);
			return true;
		}
		return false;
	}

	std::string into(const RunnerUpdateState& s) {
		switch (s) {
			case RunnerUpdateState::Idle:
				return "idle";
			case RunnerUpdateState::Active:
				return "active";
			case RunnerUpdateState::Failed:
				return "failed";
			default:
				return "";
		}
	}

	std::vector<std::string> all() {
		return {
			into(RunnerUpdateState::Idle),
			into(RunnerUpdateState::Active),
			into(RunnerUpdateState::Failed),
		};
	}
}
