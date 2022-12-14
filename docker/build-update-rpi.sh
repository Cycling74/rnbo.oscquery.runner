#!/usr/bin/env bash
cd /build/examples/rnbo.oscquery.runner/update/
mkdir build-rpi && cd build-rpi
cp ../../config/conan-rpi-xcompile ~/.conan/profiles/default && \
	PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE=../../config/rpi-xpile-toolchain.cmake \
	..  && make && cpack
