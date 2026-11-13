#!/usr/bin/env bash
# Build the runner inside the xnor/rnbo-runner-xpile container: a .deb for the rpi, or a tarball for Ableton Move.
# Used by .github/workflows/build.yml and by any build against an RNBO source tree.
#
#   RUNNER_SRC              path to this repo inside the container   (default /build)
#   RUNNER_ARCH             armhf | arm64 | move                      (default armhf)
#                           move is aarch64 without dbus, jackserver and source compile, with libsndfile from conan
#                           and /data/UserData/rnbo/lib/ as the rpath, and makes a tar.gz instead of a .deb
#   RUNNER_BUILD_JOBS       parallel make jobs                        (default: nproc)
#   INCLUDE_RUNNER_VERSION  On | Off (unset: the CMakeLists.txt default)
#   RUNNER_PRERELEASE       app prerelease identifier like beta.1, makes the version 1.4.6-11~beta.1 (unset or empty: a final release)
#
# RNBO comes from conan if RNBO_CONAN_VERSION is set, otherwise from a source tree:
#   RNBO_CONAN_VERSION      e.g. 1.4.6
#   RNBO_CONAN_TAG          e.g. c74/stable (unset: the CMakeLists.txt default)
#   RNBO_DIR                path to the RNBO C++ sources              (default /rnbo)
#
# To get RNBO from a conan remote other than the public one, also set:
#   C74_CONAN_REMOTE_URL, and C74_CONAN_USER + C74_CONAN_PASSWORD if it needs a login
#
# The package ends up in $RUNNER_SRC/build-rpi-$RUNNER_ARCH/ (a .deb), for move in $RUNNER_SRC/build-move/
# as rnbooscquery_<version>_move.tar.gz holding bin/rnbooscquery, <version> being the same as the rpi .deb version.
set -euo pipefail

RUNNER_SRC=${RUNNER_SRC:-/build}
RUNNER_ARCH=${RUNNER_ARCH:-armhf}
JOBS=${RUNNER_BUILD_JOBS:-$(nproc)}

case "$RUNNER_ARCH" in
	armhf) TOOLCHAIN=armv7-unknown-linux-gnueabihf-gcc12; SUPPORT_COMPILE=On ;;
	arm64) TOOLCHAIN=aarch64-unknown-linux-gcc11_4;       SUPPORT_COMPILE=Off ;;
	move)  TOOLCHAIN=aarch64-unknown-linux-gcc11_4;       SUPPORT_COMPILE=Off ;;
	*) echo "unknown RUNNER_ARCH '$RUNNER_ARCH', expected armhf, arm64 or move" >&2; exit 1 ;;
esac

export CONAN_NON_INTERACTIVE=1

CMAKE_ARGS=(
	-DCMAKE_BUILD_TYPE=Release
	-DSUPPORT_COMPILE=$SUPPORT_COMPILE
	-DCMAKE_TOOLCHAIN_FILE="/home/build/cmake/toolchains/$TOOLCHAIN.cmake"
)

if [ "$RUNNER_ARCH" = move ]; then
	CMAKE_ARGS+=(
		-DWITH_DBUS=Off
		-DWITH_JACKSERVER=Off
		-DUSE_SNDFILE_CONAN=On
		-DCMAKE_BUILD_RPATH=/data/UserData/rnbo/lib/
	)
	BUILD_DIR="$RUNNER_SRC/build-move"
else
	CMAKE_ARGS+=(-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="$RUNNER_ARCH")
	BUILD_DIR="$RUNNER_SRC/build-rpi-$RUNNER_ARCH"
fi

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

if [ -n "${RUNNER_PRERELEASE:-}" ]; then
	CMAKE_ARGS+=(-DRUNNER_PRERELEASE="$RUNNER_PRERELEASE")
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "${CMAKE_ARGS[@]}" "$RUNNER_SRC"
make -j"$JOBS"

if [ "$RUNNER_ARCH" = move ]; then
	# the version the build used, from the RUNNER_VERSION define that is PROJECT_VERSION in CMakeLists.txt
	VERSION=$(grep -o -- '-DRUNNER_VERSION=[^ ]*' CMakeFiles/rnbooscquery.dir/flags.make | sed 's/^-DRUNNER_VERSION=//; s/[\\"]//g' | sort -u)
	if [ -z "$VERSION" ] || [ "$(printf '%s\n' "$VERSION" | wc -l)" -ne 1 ]; then
		echo "could not find the runner version in CMakeFiles/rnbooscquery.dir/flags.make" >&2
		exit 1
	fi
	rm -rf move-pkg
	mkdir -p move-pkg/bin
	cp bin/rnbooscquery move-pkg/bin/
	tar --owner=0 --group=0 -czf "rnbooscquery_${VERSION}_move.tar.gz" -C move-pkg bin
	rm -rf move-pkg
else
	cpack
fi
