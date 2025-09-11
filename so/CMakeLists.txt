cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(RNBOso LANGUAGES CXX)

#libossia requires 20 and sdbus c++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

#https://stackoverflow.com/questions/24467404/dlclose-doesnt-really-unload-shared-object-no-matter-how-many-times-it-is-call
#https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html
# if your program relies on reinitialization of a DSO via "dlclose" and "dlopen", you can use -fno-gnu-unique
if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-gnu-unique")
endif()

#default to path setup for cloud compiler, the runner appliaction will set this explicitly
set(RNBO_CPP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../ CACHE FILEPATH "Location of the RNBO C++ source code")
set(LIBRARY_NAME_PREFIX "RNBOso" CACHE STRING "A string to use when constructing the library file name")
set(RNBO_CLASS_FILE ${RNBO_CPP_DIR}/../../codegen/xam/test/test.cpp CACHE FILEPATH "The RNBO generated class file to encapsulate in this library")

include_directories(
	${RNBO_CPP_DIR}
	"${RNBO_CPP_DIR}/common"
	"${RNBO_CPP_DIR}/src"
)

include(${RNBO_CPP_DIR}/version.cmake)
include(${RNBO_CPP_DIR}/cmake/CCache.cmake)

add_library(RNBOso SHARED
	${RNBO_CLASS_FILE}
)
set_target_properties(RNBOso PROPERTIES OUTPUT_NAME "${LIBRARY_NAME_PREFIX}.${RNBO_VERSION}")
target_link_libraries(RNBOso -latomic)

#for the cloud compiler
install(TARGETS RNBOso DESTINATION .)
