cmake_minimum_required(VERSION 3.0)
project(
	rnbo-remote-update-tests
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 11)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/catch2"
)

add_executable(rnbo-remote-update-tests main.cpp)

enable_testing()
add_test(NAME rnbo-remote-update-tests COMMAND rnbo-remote-update-tests)
