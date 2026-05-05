#!/bin/bash
# check-glibc.sh — verify a binary's GLIBC requirements don't exceed a given version
#
# Usage: check-glibc.sh <binary> <max-version>
# Example: check-glibc.sh ./rnbooscquery 2.17

set -euo pipefail

usage() {
    echo "Usage: $(basename "$0") <binary> <max-glibc-version>"
    echo "  Example: $(basename "$0") ./rnbooscquery 2.17"
    exit 1
}

[[ $# -ne 2 ]] && usage

BINARY="$1"
MAX_VERSION="$2"

if [[ ! -f "$BINARY" ]]; then
    echo "Error: file not found: $BINARY" >&2
    exit 1
fi

# Compare two x.y version strings; returns 0 if $1 > $2
version_gt() {
    local a_major a_minor b_major b_minor
    IFS='.' read -r a_major a_minor <<< "$1"
    IFS='.' read -r b_major b_minor <<< "$2"
    a_minor=${a_minor:-0}
    b_minor=${b_minor:-0}
    if   [[ "$a_major" -gt "$b_major" ]]; then return 0
    elif [[ "$a_major" -eq "$b_major" && "$a_minor" -gt "$b_minor" ]]; then return 0
    else return 1
    fi
}

# readelf works on foreign-arch ELFs without a cross-specific binary
VERSIONS=$(readelf -sW "$BINARY" 2>/dev/null \
    | grep -oP '(?<=@GLIBC_)[\d.]+' \
    | sort -Vu)

if [[ -z "$VERSIONS" ]]; then
    echo "No GLIBC version requirements found in $BINARY"
    exit 0
fi

FAILED=0
MAX_FOUND=""

while IFS= read -r ver; do
    if [[ -z "$MAX_FOUND" ]] || version_gt "$ver" "$MAX_FOUND"; then
        MAX_FOUND="$ver"
    fi
    if version_gt "$ver" "$MAX_VERSION"; then
        echo "FAIL  GLIBC_$ver  (exceeds max GLIBC_$MAX_VERSION)"
        FAILED=1
    fi
done <<< "$VERSIONS"

echo ""
echo "Binary : $BINARY"
echo "Max allowed : GLIBC_$MAX_VERSION"
echo "Max required: GLIBC_$MAX_FOUND"
echo "All versions: $(echo "$VERSIONS" | tr '\n' ' ')"
echo ""

if [[ $FAILED -eq 0 ]]; then
    echo "OK — all GLIBC requirements within GLIBC_$MAX_VERSION"
    exit 0
else
    echo "FAILED — binary requires GLIBC symbols above GLIBC_$MAX_VERSION"
    exit 1
fi
