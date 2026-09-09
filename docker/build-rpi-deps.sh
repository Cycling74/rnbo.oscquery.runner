#!/usr/bin/env bash
# Populate the conan cache with every dependency both rpi targets need, so the
# packages can then be pushed to the remote with conan-upload-plan.py and future
# builds download them instead of rebuilding.
#
# Run inside the xpile container with the repo mounted and a persistent conan
# home, so the cache survives the container:
#
#   docker run -it --platform linux/amd64 \
#       -v $(pwd):/build \
#       -v $(pwd)/docker/conan:/home/build/.conan \
#       xnor/rnbo-runner-xpile:0.3 \
#       /build/docker/build-rpi-deps.sh 1.4.5
#
# This only configures. Every conan install happens at cmake configure time, so
# the runner itself never needs to compile for the cache to be populated.
set -euo pipefail

RNBO_CONAN_VERSION="${1:-${RNBO_CONAN_VERSION:-}}"
RNBO_CONAN_TAG="${2:-${RNBO_CONAN_TAG:-c74/stable}}"

if [ -z "$RNBO_CONAN_VERSION" ]; then
	echo "usage: $0 <rnbo-version> [rnbo-conan-tag]" >&2
	echo "   eg: $0 1.4.5" >&2
	exit 1
fi

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAINS=/home/build/cmake/toolchains

# flags mirror .github/workflows/build.yml. they have to: package ids follow the
# settings and options, so anything that differs here produces packages that the
# CI build will not match and will rebuild anyway.
configure() {
	local dir="$1" toolchain="$2" arch="$3" support_compile="$4"
	echo
	echo "=== configuring ${dir} (${arch})"
	mkdir -p "${REPO}/${dir}"
	cd "${REPO}/${dir}"
	cmake \
		-DRNBO_CONAN_VERSION="${RNBO_CONAN_VERSION}" \
		-DRNBO_CONAN_TAG="${RNBO_CONAN_TAG}" \
		-DCMAKE_BUILD_TYPE=Release \
		-DSUPPORT_COMPILE="${support_compile}" \
		-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAINS}/${toolchain}" \
		-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="${arch}" \
		"${REPO}"
}

configure build-rpi32 armv7-unknown-linux-gnueabihf-gcc12.cmake armhf On
configure build-rpi64 aarch64-unknown-linux-gcc11_4.cmake arm64 Off

echo
echo "cache populated. now run: ${REPO}/docker/conan-upload-plan.py"
