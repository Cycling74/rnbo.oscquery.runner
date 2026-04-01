set(CONAN_PROFILE "aarch64-unknown-linux-gcc11_4" CACHE STRING "the conan profile to use")

set(tools /usr/bin)
set(SYSROOT /)

set(CMAKE_CROSSCOMPILING true)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_SYSROOT ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu/)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(ENV{LD_LIBRARY_PATH})
set(ENV{CHOST} aarch64-linux-gnu)
set(ENV{AS} ${tools}/aarch64-linux-gnu-as)
set(ENV{AR} ${tools}/aarch64-linux-gnu-gcc-ar-11)
set(ENV{RANLIB} ${tools}/aarch64-linux-gnu-gcc-ranlib-11)
set(ENV{LD} ${tools}/aarch64-linux-gnu-ld)
set(ENV{STRIP} ${tools}/aarch64-linux-gnu-strip)
set(ENV{CC} ${tools}/aarch64-linux-gnu-gcc-11)
set(ENV{CXX} ${tools}/aarch64-linux-gnu-g++-11)
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")

set(CMAKE_C_COMPILER ${tools}/aarch64-linux-gnu-gcc-11)
set(CMAKE_CXX_COMPILER ${tools}/aarch64-linux-gnu-g++-11)
set(CMAKE_AR ${tools}/aarch64-linux-gnu-gcc-ar-11)
set(CMAKE_RANLIB ${tools}/aarch64-linux-gnu-gcc-ranlib-11)

set(CMAKE_C_FLAGS "-mcpu=cortex-a72 -march=armv8-a+crc -mbranch-protection=standard" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-mcpu=cortex-a72 -march=armv8-a+crc -mbranch-protection=standard" CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc" CACHE STRING "" FORCE)
