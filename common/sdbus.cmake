set(SDBUS_CODEGEN OFF CACHE BOOL "Should we build the sdbus-c++-xml2cpp executable")

set(CODEGEN "False")
if (SDBUS_CODEGEN)
	set(CODEGEN "True")
endif()

#static linking to LGPL is okay since we provide the source code to this project, users could build and relink
#conan_cmake_configure(
#	REQUIRES sdbus-cpp/0.8.3
#	GENERATORS cmake_find_package
#	OPTIONS
#		sdbus-cpp:shared=False
#		sdbus-cpp:with_code_gen=${CODEGEN}
#	IMPORTS "bin, *.dll -> ./lib"
#	IMPORTS "bin, sdbus-c++-xml2cpp -> ./"
#	IMPORTS "lib, *.dylib* -> ./lib"
#)
#conan_cmake_autodetect(settings)
#conan_cmake_install(
#	PATH_OR_REFERENCE .
#	BUILD missing
#	#REMOTE cycling-public
#	SETTINGS ${settings}
#)
find_package(sdbus-c++ 0.8.3 REQUIRED)
set(SDBUS_LIBS ${SDBusCpp_LIBRARIES})
include_directories(${SDBusCpp_INCLUDE_DIRS})
