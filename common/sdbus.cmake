set(SDBUS_DIR CACHE FILEPATH "Set the location of the directory of built libraries libs/ and include files includes/")
mark_as_advanced(SDBUS_DIR)

if (NOT SDBUS_DIR)
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
		REMOTE cycling-jfrog
		SETTINGS ${settings}
	)
	find_package(sdbus-c++ REQUIRED)
	set(SDBUS_LIBS ${SDBusCpp_LIBRARIES})
	include_directories(${SDBusCpp_INCLUDE_DIRS})
else()
	#should have sdbus-c++ in the include dir
	include_directories("${SDBUS_DIR}/include")
	set(SDBUS_LIBS
		-lrt
		#TODO most these should be avaialble on linux, besides libsdbus-c++, could simply copy that one into the docker image and then use cmake to find the rest
		${SDBUS_DIR}/libs/libsdbus-c++.a
		${SDBUS_DIR}/libs/libsystemd.a
		${SDBUS_DIR}/libs/libcap.a
		${SDBUS_DIR}/libs/libmount.a
		${SDBUS_DIR}/libs/libblkid.a
		${SDBUS_DIR}/libs/libselinux.a
		${SDBUS_DIR}/libs/libsepol.a
		${SDBUS_DIR}/libs/libpcre2-posix.a
		${SDBUS_DIR}/libs/libpcre2-8.a
		${SDBUS_DIR}/libs/libpcre2-16.a
		${SDBUS_DIR}/libs/libpcre2-32.a
		${SDBUS_DIR}/libs/libz.a
		${SDBUS_DIR}/libs/libbz2.a
		${SDBUS_DIR}/libs/liblz4.a
		${SDBUS_DIR}/libs/liblzma.a
		${SDBUS_DIR}/libs/libzstd.a
		)
endif()
