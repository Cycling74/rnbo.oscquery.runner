cmake_minimum_required(VERSION 3.17)
project(
	rnbo-remote-update-tests
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 11)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/../src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/catch2"
)

add_executable(rnbo-remote-update-tests
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/Validation.cpp"
	main.cpp
)

#std-only tests, no Catch2/sdbus/boost, need C++17 for std::optional
add_executable(runner-version-select-tests
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/RunnerVersionSelect.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/Validation.cpp"
	runner_version_select.cpp
)
set_target_properties(runner-version-select-tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

enable_testing()
add_test(NAME rnbo-remote-update-tests COMMAND rnbo-remote-update-tests)
add_test(NAME runner-version-select-tests COMMAND runner-version-select-tests)
