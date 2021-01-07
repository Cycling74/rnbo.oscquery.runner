cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

#TODO get from some global config
set (PROJECT_VERSION "0.8.0")

project(RNBOOSCQueryRunner
	VERSION ${PROJECT_VERSION}
	LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 17)
set(PROJECT_APP "rnbooscquery")
add_definitions(-DRNBO_USE_FLOAT32)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

# Download automatically, you can also just copy the conan.cmake file
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
	message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
	file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/master/conan.cmake"
		"${CMAKE_BINARY_DIR}/conan.cmake")
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)

#conan_cmake_run(REQUIRES libsndfile/1.0.30
#	BASIC_SETUP
#	BUILD missing
#)
conan_cmake_run(REQUIRES cpp-optparse/cci.20171104
	BASIC_SETUP
	BUILD missing
)

find_library(JACK_LIB "jack" REQUIRED)
find_library(SNDFILE_LIB "sndfile" REQUIRED)

# TODO conan ossia
#TODO remove alex specific path
find_library(OSSIA_LIB "ossia"
	HINTS "~/local/src/libossia/build/src/"
)
if (NOT OSSIA_LIB)
	message(FATAL_ERROR "ossia not found")
endif()
message(STATUS "OSSIA_LIB ${OSSIA_LIB}")

find_path(
	RNBO_DIR
	NAMES "RNBO.cpp"
	PATHS
		${CMAKE_CURRENT_SOURCE_DIR}/../../../src/cpp/
		~/local/src/rnbo/
	NO_DEFAULT_PATH
	DOC "Location of the RNBO C++ source code"
)

include_directories(
	./3rdparty/
	${RNBO_DIR}
	"${RNBO_DIR}/common"
	"${RNBO_DIR}/src"
	"${RNBO_DIR}/src/3rdparty"
	#TODO remove alex specific paths
	"/usr/local/include"
)

add_definitions(-DRNBO_VERSION="${CMAKE_PROJECT_VERSION}")
add_definitions(-DRNBO_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}")
add_definitions(-DRNBO_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}")

if (UNIX)
	add_definitions(-DRNBO_DYLIB_PREFIX="lib")
	add_definitions(-DRNBO_CONFIG_DIR="~/.config/rnbo/")
	add_definitions(-DRNBO_CACHE_BASE_DIR="~/.local/share/rnbo/")
	add_definitions(-DRNBO_SO_BUILD_DIR="~/.local/share/rnbo/sobuild/")
	if (APPLE)
		add_definitions(-DRNBO_DYLIB_SUFFIX="${CMAKE_PROJECT_VERSION}.dylib")
	else() # for Linux, BSD, Solaris, Minix
		add_definitions(-DRNBO_DYLIB_SUFFIX="${CMAKE_PROJECT_VERSION}.so")
		set(PLATFORM_LIBS "-ldl -latomic -lpthread -lstdc++fs")
	endif()
else()
	message(FATAL_ERROR "platform not supported (yet)")
endif()

add_executable(${PROJECT_APP}
	src/main.cpp
	src/Config.cpp
	src/Controller.cpp
	src/Instance.cpp
	src/EventHandler.cpp
	src/JackAudio.cpp
	src/PatcherFactory.cpp
	${RNBO_DIR}/RNBO.cpp
)
target_link_libraries(${PROJECT_APP} ${CONAN_LIBS} ${JACK_LIB} ${SNDFILE_LIB} ${OSSIA_LIB} ${PLATFORM_LIBS})
