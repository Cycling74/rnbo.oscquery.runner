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

set(CONAN_PROFILE "default" CACHE STRING "The profile to use for building conan deps, this is useful for cross compiling")

set(LIBRARY_NAME_PREFIX "RNBOso" CACHE STRING "A string to use when constructing the library file name")
set(RNBO_CLASS_FILE "" CACHE FILEPATH "The RNBO generated class file to encapsulate in this library")

set(RNBO_VERSION "" CACHE STRING "The version of librnbo to link against")
set(RNBO_CONAN_TAG "c74/testing" CACHE STRING "Tag to use if getting RNBO from Conan")
set(RNBO_LINK_SHARED True CACHE BOOL "Should we link to a shared library for rnbo (otherwise static)")

set(RNBO_INCLUDE_DIR "/usr/include/rnbo/" CACHE FILEPATH "The location of the rnbo headers")
set(USE_CONAN True CACHE BOOL "Optionally use conan for RNBO")

if (NOT RNBO_VERSION)
	message(FATAL_ERROR "You must provide a RNBO_VERSION")
endif()

add_library(RNBOso SHARED
	${RNBO_CLASS_FILE}
)

if (USE_CONAN)
	include("${CMAKE_CURRENT_LIST_DIR}/conan.cmake")
	#find librnbo on the cloud compiler or during dev
	conan_cmake_configure(
		REQUIRES librnbo/${RNBO_VERSION}@${RNBO_CONAN_TAG}
		GENERATORS cmake_find_package
			OPTIONS
			librnbo:shared=${RNBO_LINK_SHARED}
	)
	conan_cmake_install(
		PATH_OR_REFERENCE .
		BUILD missing
		SETTINGS_HOST build_type=${CMAKE_BUILD_TYPE}
		SETTINGS_BUILD build_type=${CMAKE_BUILD_TYPE}
		PROFILE_HOST ${CONAN_PROFILE}
		PROFILE_BUILD default
	)
	find_package(librnbo REQUIRED)
	include_directories(${librnbo_INCLUDE_DIRS})
	target_link_libraries(RNBOso librnbo::librnbo)
else()
	include_directories(
		${RNBO_INCLUDE_DIR}
		"${RNBO_INCLUDE_DIR}/common"
		"${RNBO_INCLUDE_DIR}/src"
	)

	#	if (RNBO_LINK_SHARED)
	#		target_link_libraries(RNBOso -lrnbo.${RNBO_VERSION})
	#	else()
	#		target_link_libraries(RNBOso librnbo.a)
	#	endif()
endif()

set_target_properties(RNBOso PROPERTIES OUTPUT_NAME "${LIBRARY_NAME_PREFIX}.${RNBO_VERSION}")

#for the cloud compiler
install(TARGETS RNBOso DESTINATION .)

