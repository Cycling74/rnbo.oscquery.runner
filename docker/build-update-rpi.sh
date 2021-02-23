#!/usr/bin/env bash
cd /build/examples/RNBOOSCQueryRunner/update/
rm -rf build-rpi
mkdir build-rpi && cd build-rpi
PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE=../../config/rpi-xpile-toolchain.cmake \
	..  && make && cpack
