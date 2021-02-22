cmake_minimum_required(VERSION 3.13 FATAL_ERROR) #upgrade to 17 if we use conan

project(rnbo-update-service LANGUAGES CXX)
set(PROJECT_VERSION "0.1")
set(CMAKE_CXX_STANDARD 11)
set(PROJECT_APP "rnbo-update-service")

if (UNIX AND NOT APPLE)
	set(LINUX TRUE)
else()
	message(FATAL_ERROR "only linux is suppored")
endif()

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif()

find_package(Boost 1.62
	COMPONENTS filesystem system
	REQUIRED
)

find_package(PkgConfig)
pkg_check_modules(DBUS_CPP dbus-cpp
	REQUIRED
)
pkg_check_modules(DBUS dbus-1
	REQUIRED
)

link_directories(${DBUS_CPP_LIBRARY_DIRS} ${DBUS_LIBRARY_DIRS})

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/../common/"
	${BOOST_INCLUDE}
	${DBUS_CPP_INCLUDE_DIRS}
	${DBUS_INCLUDE_DIRS}
)

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()


if (LINUX)
	set(PLATFORM_LIBS "-ldl -latomic -lpthread")
endif()

add_executable(${PROJECT_APP}
	src/main.cpp
)

target_link_libraries(${PROJECT_APP}
	${BOOST_LIB}
	${PLATFORM_LIBS}
	${DBUS_CPP_LIBRARIES}
	${DBUS_LIBRARIES}
)

### INSTALL
#make sure dirs exist
install(DIRECTORY DESTINATION bin)
install(TARGETS ${PROJECT_APP} DESTINATION bin)


if (LINUX)
	#install(FILES
	#	${CMAKE_CURRENT_SOURCE_DIR}/config/rnbooscquery.service.local
	#	RENAME rnbooscquery.service
	#	DESTINATION lib/systemd/system/
	#	COMPONENT local
	#)

	#install(FILES
	#	${CMAKE_CURRENT_SOURCE_DIR}/config/rnbooscquery.service.usr
	#	RENAME rnbooscquery.service
	#	DESTINATION lib/systemd/system/
	#	COMPONENT usr
	#	EXCLUDE_FROM_ALL
	#)

	#setup deb build
	IF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
		INCLUDE(InstallRequiredSystemLibraries)

		SET(CPACK_GENERATOR "DEB")

		SET(CPACK_PACKAGE_DESCRIPTION "This service communicates via dbus to let the rnbooscquery runner program specify versions to update it to.")
		SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Description: A service for managing the version of the rnbooscquery runner.")
		SET(CPACK_PACKAGE_VENDOR "Cycling '74")
		SET(CPACK_PACKAGE_CONTACT "xnor@cycling74.com")

		SET(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")

		SET(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${CPACK_DEBIAN_PACKAGE_VERSION}")
		SET(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")

		SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.28), libdbus-cpp5 (>= 5.0.0)")

		SET(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
		SET(CPACK_DEBIAN_PACKAGE_SECTION "admin")

		#set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
		#	"${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm;${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm;${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/conffiles")

		INCLUDE(CPack)

	ENDIF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
endif()
