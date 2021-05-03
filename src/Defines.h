#pragma once

#include <functional>

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
};

enum class CompileLoadError : unsigned int {
	Unknown = 0,
	SourceWriteFailed = 1,
	CompileFailed = 2,
	LibraryNotFound = 3,
	InvalidRequestObject = 4,
	AudioNotActive = 5,
	VersionMismatch = 6,
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
	DeleteFailed = 4
};

enum class ListenerCommandStatus : unsigned int {
	Received = 0,
	Completed = 1
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
