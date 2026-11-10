#pragma once
#include <array>
#include <string>

// Refine one baseline cable after resolving a legacy alias or USB path fallback.
// Scope the change to the cable: identical aliases can resolve to different devices.
// An empty `before` adds `after`: a carried control cable came back.
struct MidiConnectionUpdate {
	using Key = std::array<std::string, 4>;
	Key before, after;
};
