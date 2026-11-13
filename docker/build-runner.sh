#!/usr/bin/env bash
# Build the runner .deb inside the xnor/rnbo-runner-xpile container.
# Used by .github/workflows/build.yml and by any build against an RNBO source tree.
#
#   RUNNER_SRC              path to this repo inside the container   (default /build)
#   RUNNER_ARCH             armhf | arm64                             (default armhf)
#   RUNNER_BUILD_JOBS       parallel make jobs                        (default: nproc)
#   INCLUDE_RUNNER_VERSION  On | Off (unset: the CMakeLists.txt default)
#
# RNBO comes from conan if RNBO_CONAN_VERSION is set, otherwise from a source tree:
#   RNBO_CONAN_VERSION      e.g. 1.4.6
#   RNBO_CONAN_TAG          e.g. c74/stable (unset: the CMakeLists.txt default)
#   RNBO_DIR                path to the RNBO C++ sources              (default /rnbo)
#
# To get RNBO from a conan remote other than the public one, also set:
#   C74_CONAN_REMOTE_URL, and C74_CONAN_USER + C74_CONAN_PASSWORD if it needs a login
#
# The package ends up in $RUNNER_SRC/build-rpi-$RUNNER_ARCH/
set -euo pipefail

RUNNER_SRC=${RUNNER_SRC:-/build}
RUNNER_ARCH=${RUNNER_ARCH:-armhf}
JOBS=${RUNNER_BUILD_JOBS:-$(nproc)}

case "$RUNNER_ARCH" in
	armhf) TOOLCHAIN=armv7-unknown-linux-gnueabihf-gcc12; SUPPORT_COMPILE=On ;;
	arm64) TOOLCHAIN=aarch64-unknown-linux-gcc11_4;       SUPPORT_COMPILE=Off ;;
	*) echo "unknown RUNNER_ARCH '$RUNNER_ARCH', expected armhf or arm64" >&2; exit 1 ;;
esac

export CONAN_NON_INTERACTIVE=1

CMAKE_ARGS=(
	-DCMAKE_BUILD_TYPE=Release
	-DSUPPORT_COMPILE=$SUPPORT_COMPILE
	-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="$RUNNER_ARCH"
	-DCMAKE_TOOLCHAIN_FILE="/home/build/cmake/toolchains/$TOOLCHAIN.cmake"
)

if [ -n "${RNBO_CONAN_VERSION:-}" ]; then
	CMAKE_ARGS+=(-DRNBO_CONAN_VERSION="$RNBO_CONAN_VERSION")
	if [ -n "${RNBO_CONAN_TAG:-}" ]; then
		CMAKE_ARGS+=(-DRNBO_CONAN_TAG="$RNBO_CONAN_TAG")
	fi
	if [ -n "${C74_CONAN_REMOTE_URL:-}" ]; then
		conan remote add -f cycling "$C74_CONAN_REMOTE_URL"
		if [ -n "${C74_CONAN_USER:-}" ] && [ -n "${C74_CONAN_PASSWORD:-}" ]; then
			conan user -r cycling -p "$C74_CONAN_PASSWORD" "$C74_CONAN_USER"
		fi
	fi
else
	CMAKE_ARGS+=(-DRNBO_DIR="${RNBO_DIR:-/rnbo}")
fi

if [ -n "${INCLUDE_RUNNER_VERSION:-}" ]; then
	CMAKE_ARGS+=(-DINCLUDE_RUNNER_VERSION="$INCLUDE_RUNNER_VERSION")
fi

BUILD_DIR="$RUNNER_SRC/build-rpi-$RUNNER_ARCH"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "${CMAKE_ARGS[@]}" "$RUNNER_SRC"
make -j"$JOBS"
cpack
