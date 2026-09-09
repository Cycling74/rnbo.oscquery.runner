#!/usr/bin/env python3
"""Build every conan dependency the rpi targets need, list what came out, and
print the conan upload commands for whatever the remote does not have yet.

Run inside the xpile container, with the repo mounted and a persistent conan
home so the cache survives the container:

    docker run -it --platform linux/amd64 \
        -v $(pwd):/build \
        -v $(pwd)/docker/conan:/home/build/.conan \
        xnor/rnbo-runner-xpile:0.3 \
        /build/docker/rpi-deps.py 1.4.5

This uploads nothing. Authenticate and run the printed commands yourself:

    conan user <your-username> -r cycling-public -p

-p with no value prompts for the password instead of putting it in your shell
history. Re-run with --no-build afterwards to confirm the uploads landed.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

# (build dir, toolchain, deb arch, SUPPORT_COMPILE)
# these mirror .github/workflows/build.yml and have to: package ids follow the
# settings and options, so anything built with different ones produces an id the
# CI build will not match, and it will rebuild from source anyway.
TARGETS = [
    ("build-rpi32", "armv7-unknown-linux-gnueabihf-gcc12.cmake", "armhf", "On"),
    ("build-rpi64", "aarch64-unknown-linux-gcc11_4.cmake", "arm64", "Off"),
]
TOOLCHAIN_DIR = "/home/build/cmake/toolchains"
# the 32-bit profile declares armv7hf, not armv7. armv7 is here too because
# older packages already on cycling-public were built with a profile that used
# it, so both spellings show up in a populated cache.
DEFAULT_ARCHS = ["armv7", "armv7hf", "armv8"]


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build(rnbo_version, rnbo_tag):
    """Configure both targets. Every conan install happens at cmake configure
    time, so the runner itself never needs to compile to fill the cache."""
    root = repo_root()
    for directory, toolchain, arch, support_compile in TARGETS:
        path = os.path.join(root, directory)
        print("\n=== configuring %s (%s)\n" % (directory, arch), flush=True)
        os.makedirs(path, exist_ok=True)
        cmd = [
            "cmake",
            "-DRNBO_CONAN_VERSION=%s" % rnbo_version,
            "-DRNBO_CONAN_TAG=%s" % rnbo_tag,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DSUPPORT_COMPILE=%s" % support_compile,
            "-DCMAKE_TOOLCHAIN_FILE=%s/%s" % (TOOLCHAIN_DIR, toolchain),
            "-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=%s" % arch,
            root,
        ]
        result = subprocess.run(cmd, cwd=path)
        if result.returncode != 0:
            print("\nconfiguring %s failed, stopping" % directory, file=sys.stderr)
            return False
    return True


def search_json(ref, remote=None):
    """conan search as parsed json, or None when the ref is unknown there."""
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    cmd = ["conan", "search", ref, "--json", path]
    if remote:
        cmd += ["-r", remote]
    try:
        subprocess.run(cmd, capture_output=True, text=True)
        with open(path) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    finally:
        if os.path.exists(path):
            os.unlink(path)
    return None if data.get("error") else data


def packages(data):
    found = []
    for result in (data or {}).get("results") or []:
        for item in result.get("items") or []:
            found.extend(item.get("packages") or [])
    return found


def cache_refs():
    out = subprocess.run(["conan", "search", "*", "--raw"],
                         capture_output=True, text=True)
    return [line.strip() for line in out.stdout.splitlines()
            if line.strip() and "/" in line
            and not line.startswith(("There are", "Existing"))]


def plan(args):
    refs = args.ref or cache_refs()
    if not refs:
        print("nothing in the local conan cache", file=sys.stderr)
        return 1

    archs = args.arch or DEFAULT_ARCHS
    print("\n=== packages for os=%s arch=%s build_type=%s\n"
          % (args.os_, "|".join(archs), args.build_type))

    missing = []
    for ref in sorted(refs):
        query = ref if "@" in ref else ref + "@"
        mine = [p for p in packages(search_json(query))
                if p.get("settings", {}).get("os") == args.os_
                and p.get("settings", {}).get("arch") in archs
                and p.get("settings", {}).get("build_type") == args.build_type]
        if not mine:
            continue
        theirs = {p["id"] for p in packages(search_json(query, remote=args.remote))}
        for pkg in mine:
            settings = pkg.get("settings", {})
            where = "on %s" % args.remote if pkg["id"] in theirs else "MISSING"
            print("  %-58s %-6s %-10s %s"
                  % (ref, settings.get("arch"),
                     "%s/%s" % (settings.get("compiler"), settings.get("compiler.version")),
                     where))
            if pkg["id"] not in theirs:
                missing.append((query, pkg["id"]))

    if not missing:
        print("\n=== nothing to upload, %s has everything" % args.remote)
        return 0

    print("\n=== %d to upload. authenticate, then run these:\n" % len(missing))
    print("conan user <your-username> -r %s -p\n" % args.remote)
    for query, pid in missing:
        print("conan upload '%s:%s' -r %s --check -c" % (query, pid, args.remote))
    print("\n# add --skip-upload to any of them to rehearse: checks and")
    print("# compression run, nothing is sent.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rnbo_version", nargs="?",
                        help="rnbo version to build against, eg 1.4.5")
    parser.add_argument("rnbo_tag", nargs="?", default="c74/stable")
    parser.add_argument("--no-build", action="store_true",
                        help="skip the build, just list and print commands")
    parser.add_argument("--remote", default="cycling-public")
    parser.add_argument("--arch", action="append",
                        help="repeatable (default: %s)" % ", ".join(DEFAULT_ARCHS))
    parser.add_argument("--os", dest="os_", default="Linux")
    parser.add_argument("--build-type", default="Release")
    parser.add_argument("--ref", action="append",
                        help="limit to these refs (default: everything in the cache)")
    args = parser.parse_args()

    if not args.no_build:
        if not args.rnbo_version:
            parser.error("rnbo_version is required unless --no-build is given")
        if not build(args.rnbo_version, args.rnbo_tag):
            return 1
    return plan(args)


if __name__ == "__main__":
    sys.exit(main())
