cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

#TODO get from some global config
set (PROJECT_VERSION "0.9.0")

project(RNBOOSCQueryRunner
	VERSION ${PROJECT_VERSION}
	LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 11)
set(PROJECT_APP "rnbooscquery")

add_definitions(-DRNBO_USE_FLOAT32)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

set(CONAN_PROFILE default CACHE STRING "The name of the profile to use for building conan dependencies, defaults to 'default'")

# Download automatically, you can also just copy the conan.cmake file
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
	message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
	file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/master/conan.cmake"
		"${CMAKE_BINARY_DIR}/conan.cmake")
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_check(VERSION 1.29.0 REQUIRED)

#add our public remote, has libossia
if (NOT EXISTS ${CMAKE_BINARY_DIR}/conan_successful)
    execute_process(
			COMMAND conan remote add cycling-bintray https://api.bintray.com/conan/xnor/cycling74
			# COMMAND_ECHO STDOUT
			ERROR_QUIET # errors are okay: the entries might already exist
    )

    message(STATUS "")
    message(STATUS "Bootstrapping conan.")
    message(STATUS "")
else()
    file(REMOVE ${CMAKE_BINARY_DIR}/conan_successful)
endif()

conan_cmake_run(REQUIRES cpp-optparse/cci.20171104
	BASIC_SETUP
	CMAKE_TARGETS
	BUILD missing
	PROFILE ${CONAN_PROFILE}
)

conan_cmake_run(REQUIRES libossia/1.2.1@xnor/testing
	BASIC_SETUP
	CMAKE_TARGETS
	KEEP_RPATHS
	BUILD missing
	PROFILE ${CONAN_PROFILE}
	IMPORTS "bin, *.dll -> ./bin"
	IMPORTS "lib, *.dylib* -> ./lib"
	IMPORTS "lib, *.so* -> ./lib"
)

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
	set(ConanSettings compiler.libcxx=libstdc++11)
endif()

conan_cmake_run(REQUIRES base64/0.4.0
	BASIC_SETUP
	CMAKE_TARGETS
	BUILD missing
	PROFILE ${CONAN_PROFILE}
	SETTINGS ${ConanSettings}
)

find_library(JACK_LIB "jack" REQUIRED)
find_library(SNDFILE_LIB "sndfile" REQUIRED)
find_path(SNDFILE_INCLUDE_DIR sndfile.hh REQUIRED)

#allow for boost to be installed on system
#if not, use conan
find_package(Boost 1.67
	COMPONENTS
	filesystem
)

#set variables so that we can use them later,
#for conan BOOST_INCLUDE will be empty but CONAN_INCLUDE_DIRS will have what we need
if(Boost_FOUND)
	set(BOOST_LIB Boost::filesystem)
	set(BOOST_INCLUDE ${Boost_INCLUDE_DIRS})
else()
	conan_cmake_run(REQUIRES boost/1.75.0
				 BASIC_SETUP
				 CMAKE_TARGETS
				 BUILD missing
				 PROFILE ${CONAN_PROFILE}
	)
	set(BOOST_LIB CONAN_PKG::boost)
endif()

find_path(
	RNBO_DIR
	NAMES "RNBO.cpp"
	PATHS
		${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/
		~/local/src/rnbo/
		${CMAKE_CURRENT_SOURCE_DIR}/../rnbo/
	NO_DEFAULT_PATH
	DOC "Location of the RNBO C++ source code"
)

include_directories(
	./3rdparty/
	${RNBO_DIR}
	"${RNBO_DIR}/common"
	"${RNBO_DIR}/src"
	"${RNBO_DIR}/src/3rdparty"
	${CONAN_INCLUDE_DIRS}
	${SNDFILE_INCLUDE_DIR}
	${BOOST_INCLUDE}
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
		set(PLATFORM_LIBS "-ldl -latomic -lpthread")
	endif()
else()
	message(FATAL_ERROR "platform not supported (yet)")
endif()

SET(BUILD_SCRIPT_NAME "rnbo-compile-so")

#bring over SO build files here so we can package
FILE(GLOB SO_BUILD_FILES ${CMAKE_CURRENT_SOURCE_DIR}/so/*)
list(FILTER SO_BUILD_FILES EXCLUDE REGEX ${BUILD_SCRIPT_NAME})
FILE(COPY ${SO_BUILD_FILES} DESTINATION share/rnbo/so/)
FILE(COPY "${CMAKE_CURRENT_SOURCE_DIR}/so/${BUILD_SCRIPT_NAME}"
	DESTINATION bin/
	FILE_PERMISSIONS
		OWNER_EXECUTE OWNER_WRITE OWNER_READ
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)

#package rnbo c++ directory
FILE(GLOB RNBO_CPP_FILES ${RNBO_DIR}/*)
FILE(COPY ${RNBO_CPP_FILES} DESTINATION src/rnbo)

add_executable(${PROJECT_APP}
	src/main.cpp
	src/Config.cpp
	src/Controller.cpp
	src/Instance.cpp
	src/EventHandler.cpp
	src/JackAudio.cpp
	src/PatcherFactory.cpp
	src/ValueCallbackHelper.cpp
	${RNBO_DIR}/RNBO.cpp
)

target_link_libraries(${PROJECT_APP}
	CONAN_PKG::libossia
	CONAN_PKG::base64
	CONAN_PKG::cpp-optparse
	${BOOST_LIB}
	${JACK_LIB}
	${SNDFILE_LIB}
	${PLATFORM_LIBS}
)

# add rpath so we can run from the build directory
if (APPLE)
	add_custom_command(TARGET ${PROJECT_APP}
		POST_BUILD COMMAND
		${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path/../lib"
		$<TARGET_FILE:${PROJECT_APP}>)
endif()

#build an archive so we could install this elsewhere
add_custom_command(
	TARGET ${PROJECT_APP}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E tar czf ${CMAKE_CURRENT_BINARY_DIR}/rnbo-oscquery-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}.tar.gz --
	bin lib share src
)

### INSTALL
#make sure dirs exist
install(DIRECTORY DESTINATION share)
install(DIRECTORY DESTINATION src)
install(DIRECTORY DESTINATION lib)
install(DIRECTORY DESTINATION bin)

install(TARGETS ${PROJECT_APP} DESTINATION bin)

#libraries
FILE(GLOB INSTALL_LIBS ${CMAKE_BINARY_DIR}/lib/*)
install(FILES ${INSTALL_LIBS} TYPE LIB)

#build script
INSTALL(FILES ${CMAKE_BINARY_DIR}/bin/${BUILD_SCRIPT_NAME}
	DESTINATION bin
	PERMISSIONS
		OWNER_EXECUTE OWNER_WRITE OWNER_READ
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)
#shared, src
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/rnbo DESTINATION share)
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/rnbo DESTINATION src)
