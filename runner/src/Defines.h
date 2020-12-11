#pragma once

#include <ossia-cpp/ossia-cpp98.hpp>
#include <functional>

#define XSTR(s) STR(s)
#define STR(s) #s

//a callback function that lets you safely alter the tree of nodes, passes your root
typedef std::function<void(std::function<void(opp::node)>)> NodeBuilder;

