cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(RNBOso LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_definitions(-DRNBO_USE_FLOAT32)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

#https://stackoverflow.com/questions/24467404/dlclose-doesnt-really-unload-shared-object-no-matter-how-many-times-it-is-call
#https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html
# if your program relies on reinitialization of a DSO via "dlclose" and "dlopen", you can use -fno-gnu-unique
if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-gnu-unique")
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

include("${RNBO_DIR}/version.cmake")

add_library(RNBOso SHARED
	${RNBO_CLASS_FILE}
	#until we get a host logger exposed
	ConsoleLogger.cpp
)
set_target_properties(RNBOso PROPERTIES OUTPUT_NAME "${LIBRARY_NAME_PREFIX}.${RNBO_VERSION}")
