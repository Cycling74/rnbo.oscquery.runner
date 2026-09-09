#!/usr/bin/env python3
"""Report which locally built conan packages are missing from a remote and print
the conan upload commands that would push them.

Uploads nothing. Run it inside the xpile container after build-rpi-deps.sh, so
the conan cache holds the rpi packages:

    /build/docker/conan-upload-plan.py
    /build/docker/conan-upload-plan.py --arch armv8          # 64-bit only
    /build/docker/conan-upload-plan.py --os Macos --arch x86_64

Then authenticate and run the printed commands:

    conan user -r cycling-public -p "$C74_CONAN_PASSWORD" "$C74_CONAN_USER"
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

DEFAULT_ARCHS = ["armv7", "armv8"]


def search_json(ref, remote=None):
    """conan search as parsed json, or None when the ref is unknown there."""
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    cmd = ["conan", "search", ref, "--json", path]
    if remote:
        cmd += ["-r", remote]
    try:
        subprocess.run(cmd, capture_output=True, text=True)
        with open(path) as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return None
    finally:
        if os.path.exists(path):
            os.unlink(path)
    if data.get("error"):
        return None
    return data


def packages(data):
    if not data:
        return []
    out = []
    for result in data.get("results") or []:
        for item in result.get("items") or []:
            for pkg in item.get("packages") or []:
                out.append(pkg)
    return out


def local_refs():
    out = subprocess.run(["conan", "search", "*", "--raw"],
                         capture_output=True, text=True)
    refs = []
    for line in out.stdout.splitlines():
        line = line.strip()
        # skip blank lines and conan's chatter
        if not line or "/" not in line or line.startswith(("There are", "Existing")):
            continue
        refs.append(line)
    return refs


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", default="cycling-public")
    ap.add_argument("--arch", action="append", help="repeatable (default: %s)"
                    % ", ".join(DEFAULT_ARCHS))
    ap.add_argument("--os", dest="os_", default="Linux")
    ap.add_argument("--build-type", default="Release")
    ap.add_argument("--ref", action="append",
                    help="limit to these refs (default: everything in the cache)")
    args = ap.parse_args()
    archs = args.arch or DEFAULT_ARCHS

    refs = args.ref or local_refs()
    if not refs:
        print("nothing in the local conan cache", file=sys.stderr)
        return 1

    commands, skipped = [], []
    print("# wanted: os=%s arch=%s build_type=%s  remote=%s"
          % (args.os_, "|".join(archs), args.build_type, args.remote))
    print()
    for ref in sorted(refs):
        query = ref if "@" in ref else ref + "@"
        mine = packages(search_json(query))
        wanted = [p for p in mine
                  if p.get("settings", {}).get("os") == args.os_
                  and p.get("settings", {}).get("arch") in archs
                  and p.get("settings", {}).get("build_type") == args.build_type]
        if not wanted:
            continue
        theirs = {p["id"] for p in packages(search_json(query, remote=args.remote))}
        for pkg in wanted:
            s = pkg.get("settings", {})
            tag = "%s %s/%s" % (s.get("arch"), s.get("compiler"), s.get("compiler.version"))
            if pkg["id"] in theirs:
                skipped.append("%s  %s  (already on %s)" % (ref, tag, args.remote))
            else:
                print("# %s  %s" % (ref, tag))
                commands.append("conan upload '%s:%s' -r %s --check -c"
                                % (query, pkg["id"], args.remote))
                print(commands[-1])
                print()

    print("# %d package(s) to upload, %d already present" % (len(commands), len(skipped)))
    for line in skipped:
        print("#   %s" % line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
