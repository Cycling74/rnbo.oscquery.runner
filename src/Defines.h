#pragma once

#include <ossia-cpp/ossia-cpp98.hpp>
#include <functional>

#define XSTR(s) STR(s)
#define STR(s) #s

//a callback function that lets you safely alter the tree of nodes, passes your root
typedef std::function<void(std::function<void(opp::node)>)> NodeBuilder;


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
};

enum class DataFileCommandStatus : unsigned int {
	Received = 0,
	Completed = 1
};

enum class DataFileCommandError : unsigned int {
	Unknown = 0,
	WriteFailed = 1,
	DecodeFailed = 2,
	InvalidRequestObject = 3,
	DeleteFailed = 4
};
