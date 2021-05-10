#!/usr/bin/env bash
cd /build/examples/RNBOOSCQueryRunner/
mkdir build-rpi && cd build-rpi
mkdir -p ~/.conan/profiles/
cp ../config/conan-rpi-xcompile ~/.conan/profiles/default
PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake -DRNBO_DIR=/build/src/cpp/ \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE=../config/rpi-xpile-toolchain.cmake \
	..  && make && cpack
