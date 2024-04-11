set(CMAKE_CROSSCOMPILING TRUE)
SET(CMAKE_SYSROOT "/rpi-rootfs")
set(CMAKE_SYSTEM_NAME "Linux")
set(CMAKE_SYSTEM_PROCESSOR "armv7")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
set(CONAN_PROFILE "host" CACHE STRING "the conan profile to use")

set(TOOLCHAIN_HOST "arm-linux-gnueabihf")

set(COMMON_FLAGS "-I${CMAKE_SYSROOT}/usr/include ")
set(LIB_DIRS
	"${CMAKE_SYSROOT}/lib/${TOOLCHAIN_HOST}"
	"${CMAKE_SYSROOT}/usr/lib/${TOOLCHAIN_HOST}"
	"${CMAKE_SYSROOT}/usr/lib"
)
FOREACH(LIB ${LIB_DIRS})
	set(COMMON_FLAGS "${COMMON_FLAGS} -L${LIB} -Wl,-rpath-link,${LIB}")
ENDFOREACH()

set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${CMAKE_SYSROOT}/usr/lib/${TOOLCHAIN_HOST}")

set(CMAKE_C_FLAGS "-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard ${COMMON_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "" FORCE)

unset(ENV{LD_LIBRARY_PATH})
set(ENV{HOST} ${TOOLCHAIN_HOST})
set(ENV{CC} "${TOOLCHAIN_HOST}-gcc-12")
set(ENV{CXX} "${TOOLCHAIN_HOST}-g++-12")
set(ENV{LD} "${TOOLCHAIN_HOST}-ld")
set(ENV{AR} "${TOOLCHAIN_HOST}-ar")
set(ENV{RANLIB} "${TOOLCHAIN_HOST}-ranlib")
set(ENV{STRIP} "${TOOLCHAIN_HOST}-strip")
set(ENV{NM} "${TOOLCHAIN_HOST}-nm")

set(ENV{CFLAGS} ${CMAKE_C_FLAGS})
set(ENV{CXXFLAGS} ${CMAKE_CXX_FLAGS})
set(ENV{ASFLAGS} "-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard --sysroot=${CMAKE_SYSROOT}")
set(ENV{LDFLAGS} "--sysroot=${CMAKE_SYSROOT} -L${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf/")

#pkg config setup
#https://stackoverflow.com/questions/9221236/pkg-config-fails-to-find-package-under-sysroot-directory
#https://autotools.io/pkgconfig/cross-compiling.html
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/lib/${TOOLCHAIN_HOST}/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})


set(CMAKE_FIND_ROOT_PATH "${CMAKE_INSTALL_PREFIX};${CMAKE_PREFIX_PATH};${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_INSTALL_PREFIX};${CMAKE_PREFIX_PATH};${CMAKE_SYSROOT}")

# search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_COMPILER $ENV{CC})
set(CMAKE_CXX_COMPILER $ENV{CXX})
set(CMAKE_AR $ENV{AR})
set(CMAKE_RANLIB $ENV{RANLIB})
