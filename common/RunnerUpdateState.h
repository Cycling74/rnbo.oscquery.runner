#pragma once

#include <stdint.h>

enum class RunnerUpdateState : uint32_t {
	Idle = 0,
	Active = 1,
	Failed = 2
};

