#!/usr/bin/env bash
cd /build/examples/RNBOOSCQueryRunner/update/
mkdir build-rpi && cd build-rpi
cp ../../config/conan-rpi-xcompile ~/.conan/profiles/rpi && \
	PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DSDBUS_DIR=/sdbus-0.8.3 \
	-DCMAKE_TOOLCHAIN_FILE=../../config/rpi-xpile-toolchain.cmake \
	..  && make && cpack
