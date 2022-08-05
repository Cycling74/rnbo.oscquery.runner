cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

#find RNBO
find_path(
	RNBO_DIR
	NAMES "RNBO.cpp"
	PATHS
		${CMAKE_CURRENT_SOURCE_DIR}/../../src/cpp/
		${CMAKE_CURRENT_SOURCE_DIR}/../rnbo/
	NO_DEFAULT_PATH
	DOC "Location of the RNBO C++ source code"
)

include("${RNBO_DIR}/version.cmake")
if (NOT RNBO_VERSION)
	message(FATAL_ERROR "RNBO_VERSION not set, must have not found version.cmake in RNBO_DIR")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/common/conan.cmake")

set(BUILTIN_PATCHER_PATH "" CACHE FILEPATH "path to a rnbo export to build into this runner, useful for debugging")
mark_as_advanced(BUILTIN_PATCHER_PATH)

#libossia and sdbus require c++17
set(CMAKE_CXX_STANDARD 17)

if (UNIX AND NOT APPLE)
	set(LINUX TRUE)
	option(WITH_DBUS "include dbus interface that allows for remote update" ON)
endif()

project(rnbooscquery LANGUAGES CXX)
set(PROJECT_VERSION "${RNBO_VERSION}")
set(PROJECT_APP "rnbooscquery")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(PROJECT_SRC
	src/main.cpp
	src/Config.cpp
	src/Controller.cpp
	src/Instance.cpp
	src/EventHandler.cpp
	src/JackAudio.cpp
	src/PatcherFactory.cpp
	common/RunnerUpdateState.cpp
	${RNBO_DIR}/RNBO.cpp
)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

#dbus on linux
if (WITH_DBUS)

	find_package(sdbus-c++ 0.8.3 REQUIRED)
	#handle sdbus for linux, common between this and the update lib
	#include("${CMAKE_CURRENT_SOURCE_DIR}/common/sdbus.cmake")
	add_definitions(-DRNBO_USE_DBUS)
	list(APPEND PROJECT_SRC src/RnboUpdateServiceProxy.cpp)
endif()

if (BUILTIN_PATCHER_PATH)
	add_definitions(-DRNBO_OSCQUERY_BUILTIN_PATCHER)
	list(APPEND PROJECT_SRC ${BUILTIN_PATCHER_PATH})
endif()

conan_cmake_configure(
	REQUIRES cpp-optparse/cci.20171104
	GENERATORS cmake_find_package
)
conan_cmake_autodetect(settings)
conan_cmake_install(
	PATH_OR_REFERENCE .
	BUILD missing
	SETTINGS ${settings}
)

conan_cmake_configure(
	REQUIRES boost/1.75.0
	GENERATORS cmake_find_package
	OPTIONS
		boost:shared=False
		boost:without_stacktrace=True
)
conan_cmake_install(
	PATH_OR_REFERENCE .
	BUILD missing
	SETTINGS ${settings}
)
find_package(
	Boost 1.75
	COMPONENTS filesystem system REQUIRED
)

conan_cmake_configure(
	REQUIRES libossia/xnor_conan-multiplex-thread-safe_b2c9019afdefeee0ed@xnor/testing
	GENERATORS cmake_find_package
	OPTIONS libossia:shared=False
)
conan_cmake_install(
	PATH_OR_REFERENCE .
	BUILD missing
	REMOTE cycling-public
	SETTINGS ${settings}
)

conan_cmake_configure(
	REQUIRES base64/0.4.0
	GENERATORS cmake_find_package
)
conan_cmake_install(
	PATH_OR_REFERENCE .
	BUILD missing
	SETTINGS ${settings}
)

find_library(JACK_LIB "jack" REQUIRED)
find_library(JACKSERVER_LIB "jackserver" REQUIRED)
find_library(SNDFILE_LIB "sndfile" REQUIRED)
find_path(SNDFILE_INCLUDE_DIR sndfile.hh REQUIRED)

find_package(cpp-optparse REQUIRED)
find_package(libossia REQUIRED)
find_package(base64 REQUIRED)

include_directories(
	./3rdparty/
	./common/
	${RNBO_DIR}
	"${RNBO_DIR}/common"
	"${RNBO_DIR}/src"
	"${RNBO_DIR}/src/3rdparty"
	${Boost_INCLUDE_DIRS}
	${cpp-optparse_INCLUDE_DIRS}
	${libossia_INCLUDE_DIRS}
	${base64_INCLUDE_DIRS}
	${SNDFILE_INCLUDE_DIR}
)

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

add_definitions(-DRNBO_VERSION="${RNBO_VERSION}")
add_definitions(-DRNBO_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}")
add_definitions(-DRNBO_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}")

if (CMAKE_BUILD_TYPE STREQUAL Debug)
	add_definitions(-DDEBUG)
endif()

if (UNIX)
	add_definitions(-DRNBO_DYLIB_PREFIX="lib")
	add_definitions(-DRNBO_CONFIG_DIR="~/.config/rnbo/")
	add_definitions(-DRNBO_CACHE_BASE_DIR="~/.local/share/rnbo/")
	add_definitions(-DRNBO_SO_BUILD_DIR="~/.local/share/rnbo/sobuild/")
	if (APPLE)
		add_definitions(-DRNBO_DYLIB_SUFFIX="${RNBO_VERSION}.dylib")
	else() # for Linux, BSD, Solaris, Minix
		add_definitions(-DRNBO_DYLIB_SUFFIX="${RNBO_VERSION}.so")
	endif()
else()
	message(FATAL_ERROR "platform not supported (yet)")
endif()

SET(BUILD_SCRIPT_NAME "rnbo-compile-so")

#bring over SO build files here so we can package
FILE(GLOB SO_BUILD_FILES ${CMAKE_CURRENT_SOURCE_DIR}/so/*)
list(FILTER SO_BUILD_FILES EXCLUDE REGEX ${BUILD_SCRIPT_NAME})
FILE(COPY ${SO_BUILD_FILES} DESTINATION share/rnbo/so/)
FILE(COPY
	"${CMAKE_CURRENT_SOURCE_DIR}/so/${BUILD_SCRIPT_NAME}"
	DESTINATION bin/
	FILE_PERMISSIONS
		OWNER_EXECUTE OWNER_WRITE OWNER_READ
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)

#package rnbo c++ directory
FILE(GLOB RNBO_CPP_FILES ${RNBO_DIR}/*)
FILE(COPY ${RNBO_CPP_FILES} DESTINATION src/rnbo)

add_executable(${PROJECT_APP} ${PROJECT_SRC})

target_link_libraries(${PROJECT_APP}
	${Boost_LIBRARIES}
	${libossia_LIBRARIES}
	${base64_LIBRARIES}
	${cpp-optparse_LIBRARIES}
	${JACK_LIB}
	${JACKSERVER_LIB}
	${SNDFILE_LIB}
	${SDBUS_LIBS}
)

if (LINUX)
	target_link_libraries(${PROJECT_APP}
		-ldl
		-latomic
		-lpthread
	)
endif()

### INSTALL
#make sure dirs exist
install(DIRECTORY DESTINATION share)
install(DIRECTORY DESTINATION src)
install(DIRECTORY DESTINATION bin)
install(DIRECTORY DESTINATION lib)

install(TARGETS ${PROJECT_APP} DESTINATION bin)

#libraries
#install(DIRECTORY DESTINATION lib)
#FILE(GLOB INSTALL_LIBS ${CMAKE_BINARY_DIR}/lib/*)
#install(FILES ${INSTALL_LIBS} TYPE LIB)

#build script
INSTALL(FILES
	${CMAKE_BINARY_DIR}/bin/${BUILD_SCRIPT_NAME}
	DESTINATION bin
	PERMISSIONS
		OWNER_EXECUTE OWNER_WRITE OWNER_READ
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)

#shared, src
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/rnbo DESTINATION share)
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/rnbo DESTINATION src)

if (LINUX)
	install(FILES
		${CMAKE_CURRENT_SOURCE_DIR}/config/rnbooscquery.service.local
		RENAME rnbooscquery.service
		DESTINATION lib/systemd/system/
		COMPONENT local
	)

	install(FILES
		${CMAKE_CURRENT_SOURCE_DIR}/config/rnbooscquery.service.usr
		RENAME rnbooscquery.service
		DESTINATION lib/systemd/system/
		COMPONENT usr
		EXCLUDE_FROM_ALL
	)

	#setup deb build
	IF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
		INCLUDE(InstallRequiredSystemLibraries)

		SET(CPACK_GENERATOR "DEB")

		SET(CPACK_PACKAGE_DESCRIPTION "rnbo runner using OSCQuery for communication")
		SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "An application that runs code generated by RNBO.")
		SET(CPACK_PACKAGE_VENDOR "Cycling '74")
		SET(CPACK_PACKAGE_CONTACT "xnor@cycling74.com")

		SET(CPACK_PACKAGE_VERSION_MAJOR "${RNBO_VERSION_MAJOR}")
		SET(CPACK_PACKAGE_VERSION_MINOR "${RNBO_VERSION_MINOR}")
		SET(CPACK_PACKAGE_VERSION_PATCH "${RNBO_VERSION_PATCH}")

		#rnbo version might have a 'prerelease' or 'channel' version in it
		SET(CPACK_DEBIAN_PACKAGE_VERSION "${RNBO_VERSION}")

		SET(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${CPACK_DEBIAN_PACKAGE_VERSION}")
		SET(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")

		SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libjack-jackd2-0 (>= 1.9.10+20150825) | libjack-0.125, libsndfile1 (>= 1.0.20), libc6 (>= 2.28), libavahi-compat-libdnssd1 (>= 0.7), ruby (>= 1:2.5), cmake (>= 3.13.4), g++ (>= 4:8.3), libsystemd0, libavahi-client-dev")

		#packages that are also installed by default but don't have to be
		SET(CPACK_DEBIAN_PACKAGE_RECOMMENDS "rnbo-update-service (>= 0.1.1)")
		SET(CPACK_DEBIAN_PACKAGE_BREAKS "rnbo-update-service (= 0.1.0)")

		#packages that are suggested
		SET(CPACK_DEBIAN_PACKAGE_SUGGESTS "jack_transport_link")

		SET(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
		SET(CPACK_DEBIAN_PACKAGE_SECTION "sound")

		#remove the local component from the deb as it references /usr/ not /usr/local/
		get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
		message(STATUS "components ${CPACK_COMPONENTS_ALL}")
		list(REMOVE_ITEM CPACK_COMPONENTS_ALL "local")
		SET(CPACK_DEB_COMPONENT_INSTALL ON)
		SET(CPACK_COMPONENTS_GROUPING "ALL_COMPONENTS_IN_ONE")

		set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
			"${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm;${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm;${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/conffiles")

		INCLUDE(CPack)

	ENDIF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
endif()
