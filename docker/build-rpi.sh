#!/usr/bin/env bash
cd /build/examples/RNBOOSCQueryRunner/
mkdir build-rpi && cd build-rpi
cp ../config/conan-rpi-xcompile ~/.conan/profiles/rpi && \
	PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake -DRNBO_DIR=/build/src/cpp/ \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE=../config/rpi-xpile-toolchain.cmake \
	-DCONAN_PROFILE=rpi \
	..  && make && cpack
