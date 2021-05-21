#static linking to LGPL is okay since we provide the source code to this project, users could build and relink
conan_cmake_configure(
	REQUIRES sdbus-cpp/0.8.3
	GENERATORS cmake_find_package
	OPTIONS sdbus-cpp:shared=False
	IMPORTS "bin, *.dll -> ./lib"
	IMPORTS "bin, sdbus-c++-xml2cpp -> ./"
	IMPORTS "lib, *.dylib* -> ./lib"
)
conan_cmake_autodetect(settings)
conan_cmake_install(
	PATH_OR_REFERENCE .
	BUILD missing
	#REMOTE cycling-public
	SETTINGS ${settings}
)
find_package(sdbus-c++ REQUIRED)
set(SDBUS_LIBS ${SDBusCpp_LIBRARIES})
include_directories(${SDBusCpp_INCLUDE_DIRS})
