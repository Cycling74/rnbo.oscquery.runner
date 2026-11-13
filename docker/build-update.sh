#!/usr/bin/env bash
# Build the rnbo-update-service .deb inside the xnor/rnbo-runner-xpile container.
# Used by .github/workflows/build-update.yml.
#
#   RUNNER_SRC         path to this repo inside the container   (default /build)
#   RUNNER_ARCH        armhf | arm64                             (default armhf)
#   RUNNER_BUILD_JOBS  parallel make jobs                        (default: nproc)
#
# The package ends up in $RUNNER_SRC/build-rpi-$RUNNER_ARCH-update/
set -euo pipefail

RUNNER_SRC=${RUNNER_SRC:-/build}
RUNNER_ARCH=${RUNNER_ARCH:-armhf}
JOBS=${RUNNER_BUILD_JOBS:-$(nproc)}

case "$RUNNER_ARCH" in
	armhf) TOOLCHAIN=armv7-unknown-linux-gnueabihf-gcc12 ;;
	arm64) TOOLCHAIN=aarch64-unknown-linux-gcc11_4 ;;
	*) echo "unknown RUNNER_ARCH '$RUNNER_ARCH', expected armhf or arm64" >&2; exit 1 ;;
esac

export CONAN_NON_INTERACTIVE=1

BUILD_DIR="$RUNNER_SRC/build-rpi-$RUNNER_ARCH-update"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="$RUNNER_ARCH" \
	-DCMAKE_TOOLCHAIN_FILE="/home/build/cmake/toolchains/$TOOLCHAIN.cmake" \
	"$RUNNER_SRC/update"
make -j"$JOBS"
cpack
