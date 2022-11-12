#!/usr/bin/env bash
cd /build/examples/rnbo.oscquery.runner/
mkdir -p build-rpi && cd build-rpi && rm -fr *
mkdir -p ~/.conan/profiles/ && cp ../config/conan-rpi-xcompile ~/.conan/profiles/default
PATH=/opt/cross-pi-gcc/bin:$PATH \
	cmake -DRNBO_DIR=/build/src/cpp/ \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE=/build/examples/rnbo.oscquery.runner/config/rpi-xpile-toolchain.cmake \
	-DDEFAULT_CACHE_DIR=/usr/share/rnbo/cache/ \
	-DDEFAULT_CONFIG_DIR=/usr/share/rnbo/config/ \
	..  && make && cpack
