cmake_minimum_required(VERSION 3.17)
project(rnbo-file-tests LANGUAGES CXX)
# Standalone: cmake -S test -B build/file-tests && cmake --build build/file-tests
# Set RNBO_TEST_OTHER_FS and RNBO_TEST_SMALL_FS (a separate tmpfs smaller than
# 2 MiB) when running ctest to include the real EXDEV and disk-full checks.
set(CMAKE_CXX_STANDARD 17)
find_package(Boost REQUIRED COMPONENTS filesystem)
add_executable(rnbo-file-tests FileUtil.cpp ../src/FileUtil.cpp)
target_include_directories(rnbo-file-tests PRIVATE ../src ../3rdparty/catch2)
target_link_libraries(rnbo-file-tests PRIVATE Boost::filesystem)
enable_testing()
add_test(NAME rnbo-file-tests COMMAND rnbo-file-tests)
