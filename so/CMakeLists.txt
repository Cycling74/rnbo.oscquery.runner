cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

#TODO get from some global config
set (PROJECT_VERSION "0.9.0")

project(RNBOso
	VERSION ${PROJECT_VERSION}
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_definitions(-DRNBO_USE_FLOAT32)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

set(RNBO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/cpp/ CACHE FILEPATH "Location of the RNBO C++ source code")
set(LIBRARY_NAME_PREFIX "RNBOso" CACHE STRING "A string to use when constructing the library file name")
set(RNBO_CLASS_FILE ${RNBO_DIR}/../../codegen/xam/test/test.cpp CACHE STRING "The RNBO generated class file to encapsulate in this library")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
	# Support Unix Makefiles and Ninja
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

include_directories(
	${RNBO_DIR}
	"${RNBO_DIR}/common"
	"${RNBO_DIR}/src"
)

add_library(RNBOso SHARED
	${RNBO_CLASS_FILE}
	#until we get a host logger exposed
	ConsoleLogger.cpp
)
set_target_properties(RNBOso PROPERTIES OUTPUT_NAME "${LIBRARY_NAME_PREFIX}.${PROJECT_VERSION}")
