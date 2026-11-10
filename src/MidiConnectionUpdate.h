#pragma once
#include <array>
#include <string>

// Refine one baseline cable after resolving a legacy alias or USB path fallback.
// Scope the change to the cable: identical aliases can resolve to different devices.
struct MidiConnectionUpdate {
	using Key = std::array<std::string, 4>;
	Key before, after;
};
