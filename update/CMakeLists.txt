cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

project(rnbo-update-service LANGUAGES CXX)
set(PROJECT_VERSION "0.1.1")
set(CMAKE_CXX_STANDARD 17)
set(PROJECT_APP "rnbo-update-service")

enable_testing()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)

if (UNIX AND NOT APPLE)
	set(LINUX TRUE)
else()
	message(FATAL_ERROR "only linux is suppored")
endif()

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

include("${CMAKE_CURRENT_SOURCE_DIR}/../common/sdbus.cmake")

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}/../common/"
)

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

add_executable(${PROJECT_APP}
	src/main.cpp
	src/Validation.cpp
	src/RnboUpdateService.cpp
	../common/RunnerUpdateState.cpp
)

target_link_libraries(${PROJECT_APP}
	${SDBUS_LIBS}
	-lpthread
	-latomic
)

if (LINUX)
	install(DIRECTORY DESTINATION bin)
	install(TARGETS ${PROJECT_APP} DESTINATION bin)

	install(FILES
		${CMAKE_CURRENT_SOURCE_DIR}/config/rnbo-update-service.service
		DESTINATION lib/systemd/system/
		PERMISSIONS
			OWNER_WRITE OWNER_READ
			GROUP_READ
			WORLD_READ
	)

	install(FILES
		${CMAKE_CURRENT_SOURCE_DIR}/config/com.cycling74.rnbo.conf
		DESTINATION share/dbus-1/system.d/
		PERMISSIONS
			OWNER_WRITE OWNER_READ
			GROUP_READ
			WORLD_READ
	)

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

		SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.28), libsystemd0")

		SET(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
		SET(CPACK_DEBIAN_PACKAGE_SECTION "admin")

		set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
			"${CMAKE_CURRENT_SOURCE_DIR}/debian/prerm;${CMAKE_CURRENT_SOURCE_DIR}/debian/postrm;${CMAKE_CURRENT_SOURCE_DIR}/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/debian/conffiles")

		INCLUDE(CPack)

	ENDIF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
endif()
