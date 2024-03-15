#!/usr/bin/env bash
cd /build/examples/rnbo.oscquery.runner/
mkdir -p build-rpi-bookworm64 && cd build-rpi-bookworm64 && rm -fr *
mkdir -p ~/.conan/profiles/ && cp ../docker/conan-rpi-bookworm64-xcompile-host ~/.conan/profiles/host && cp ../docker/conan-rpi-bookworm64-xcompile-build ~/.conan/profiles/default
PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake -DRNBO_DIR=/build/src/cpp/ \
	-DCMAKE_BUILD_TYPE=Release \
	-DCONAN_PROFILE=host \
	-DCMAKE_TOOLCHAIN_FILE=/build/examples/rnbo.oscquery.runner/config/rpi-bookworm64-xpile-toolchain.cmake \
	..  && make && cpack
