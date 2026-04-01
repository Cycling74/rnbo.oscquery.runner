set(CONAN_PROFILE "armv7-unknown-linux-gnueabihf" CACHE STRING "the conan profile to use")

set(tools /usr/bin)
set(SYSROOT /)

set(CMAKE_CROSSCOMPILING true)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_SYSROOT ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf/)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(ENV{LD_LIBRARY_PATH})
set(ENV{CHOST} arm-linux-gnueabihf)
set(ENV{AS} ${tools}/arm-linux-gnueabihf-as)
set(ENV{AR} ${tools}/arm-linux-gnueabihf-gcc-ar-11)
set(ENV{RANLIB} ${tools}/arm-linux-gnueabihf-gcc-ranlib-11)
set(ENV{LD} ${tools}/arm-linux-gnueabihf-ld)
set(ENV{STRIP} ${tools}/arm-linux-gnueabihf-strip)
set(ENV{CC} ${tools}/arm-linux-gnueabihf-gcc-11)
set(ENV{CXX} ${tools}/arm-linux-gnueabihf-g++-11)
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig")

set(CMAKE_C_COMPILER ${tools}/arm-linux-gnueabihf-gcc-11)
set(CMAKE_CXX_COMPILER ${tools}/arm-linux-gnueabihf-g++-11)
set(CMAKE_AR ${tools}/arm-linux-gnueabihf-gcc-ar-11)
set(CMAKE_RANLIB ${tools}/arm-linux-gnueabihf-gcc-ranlib-11)

set(CMAKE_C_FLAGS "-march=armv7-a -mtune=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-march=armv7-a -mtune=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc" CACHE STRING "" FORCE)
