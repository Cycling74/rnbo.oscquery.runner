#pragma once

#include <functional>
#include <inttypes.h>
#include <map>

#define XSTR(s) STR(s)
#define STR(s) #s

namespace ossia {
	namespace net {
		class generic_device;
		class node_base;
		class parameter_base;
	}
}

//a callback function that lets you safely alter the tree of nodes, passes your root
typedef std::function<void(std::function<void(ossia::net::node_base*)>)> NodeBuilder;

enum class CompileLoadStatus : unsigned int {
	Received = 0,
	Compiled = 1,
	Loaded = 2,
	Cancelled = 3
};

enum class CompileLoadError : unsigned int {
	Unknown = 0,
	SourceWriteFailed = 1,
	CompileFailed = 2,
	LibraryNotFound = 3,
	InvalidRequestObject = 4,
	AudioNotActive = 5,
	VersionMismatch = 6,
	SourceFileDoesNotExist = 7,
	DecodeFailed = 8
};

enum class FileCommandStatus : unsigned int {
	Received = 0,
	Completed = 1
};

enum class FileCommandError : unsigned int {
	Unknown = 0,
	WriteFailed = 1,
	DecodeFailed = 2,
	InvalidRequestObject = 3,
	DeleteFailed = 4,
	ReadFailed = 5,
};

enum class PackageCommandError : unsigned int {
	Unknown = 0,
	WriteFailed = 1,
	NotFound = 2,
};

enum class DBCommandStatus : unsigned int {
	Completed = 0,
	BadParams = 1,
	NotFound = 2,
	Unknown = 3,
};

enum class ListenerCommandStatus : unsigned int {
	Received = 0,
	Completed = 1,
	Failed = 2
};

enum class InstallProgramStatus : unsigned int {
	Received = 0,
	Completed = 1
};

enum class InstallProgramError : unsigned int {
	Unknown = 0,
	InvalidRequestObject = 1,
	NotEnabled = 2,
};

struct ProgramChange {
	uint8_t chan = 0;
	uint8_t prog = 0;
};

static const std::map<std::string, int> config_midi_channel_values = {
	{"omni", 0},
	{"1", 1},
	{"2", 2},
	{"3", 3},
	{"4", 4},
	{"5", 5},
	{"6", 6},
	{"7", 7},
	{"8", 8},
	{"9", 9},
	{"10", 10},
	{"11", 11},
	{"12", 12},
	{"13", 13},
	{"14", 14},
	{"15", 15},
	{"16", 16},
	{"none", 17} //17 will never be valid
};
