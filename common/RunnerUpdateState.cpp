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
				return "Idle";
			case RunnerUpdateState::Active:
				return "Active";
			case RunnerUpdateState::Failed:
				return "Failed";
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
