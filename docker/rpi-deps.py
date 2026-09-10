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

By default it uploads nothing, it just prints the commands. Authenticate and
run them yourself, or re-run with --upload and let it do all of them:

    conan user <your-username> -r cycling-public -p
    rpi-deps.py --no-build --upload

-p with no value prompts for the password instead of putting it in your shell
history. --dry-run rehearses the uploads without sending anything. Re-run with
--no-build afterwards to confirm they landed.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

# (build dir, toolchain/profile name, deb arch, SUPPORT_COMPILE)
# these mirror .github/workflows/build.yml and have to: package ids follow the
# settings and options, so anything built with different ones produces an id the
# CI build will not match, and it will rebuild from source anyway.
# the toolchain file and the conan profile deliberately share a name.
# dedicated build dirs, so this never inherits a stale CMakeCache.txt from a
# build-rpi32/64 configured by hand.
TARGETS = [
    ("build-deps-rpi32", "armv7-unknown-linux-gnueabihf-gcc12", "armhf", "On"),
    ("build-deps-rpi64", "aarch64-unknown-linux-gcc11_4", "arm64", "Off"),
]
TOOLCHAIN_DIR = "/home/build/cmake/toolchains"
# half the cores, matching the convention used elsewhere. conan otherwise hands
# every dependency build every core it can see, and libossia alone is enough to
# wedge the machine. override with RPI_DEPS_JOBS.
DEFAULT_JOBS = max(1, (os.cpu_count() or 2) // 2)
# the 32-bit profile declares armv7hf, not armv7. armv7 is here too because
# older packages already on cycling-public were built with a profile that used
# it, so both spellings show up in a populated cache.
DEFAULT_ARCHS = ["armv7", "armv7hf", "armv8"]


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cap_parallelism(jobs):
    """Conan 1 style recipes (libossia) read tools.cpu_count(), which honours
    CONAN_CPU_COUNT. Conan 2 style recipes (boost) read
    conan.tools.build.build_jobs(), which ignores it and falls back to every
    core on the machine, so that one needs the conf entry. Set both."""
    os.environ["CONAN_CPU_COUNT"] = str(jobs)
    home = os.path.join(os.environ.get("CONAN_USER_HOME", os.path.expanduser("~")),
                        ".conan")
    conf = os.path.join(home, "global.conf")
    try:
        existing = open(conf).read() if os.path.exists(conf) else ""
        if "tools.build:jobs" not in existing:
            with open(conf, "a") as handle:
                handle.write("tools.build:jobs=%d\n" % jobs)
            print("capped dependency builds at %d jobs (%s)" % (jobs, conf))
    except OSError as err:
        print("warning: could not write %s (%s); conan 2 style recipes such as "
              "boost may still build with every core" % (conf, err), file=sys.stderr)


def build(rnbo_version, rnbo_tag, jobs):
    """Configure both targets. Every conan install happens at cmake configure
    time, so the runner itself never needs to compile to fill the cache."""
    cap_parallelism(jobs)
    root = repo_root()
    for directory, profile, arch, support_compile in TARGETS:
        path = os.path.join(root, directory)
        print("\n=== configuring %s (%s)\n" % (directory, arch), flush=True)
        os.makedirs(path, exist_ok=True)
        cmd = [
            "cmake",
            "-DRNBO_CONAN_VERSION=%s" % rnbo_version,
            "-DRNBO_CONAN_TAG=%s" % rnbo_tag,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DSUPPORT_COMPILE=%s" % support_compile,
            "-DCMAKE_TOOLCHAIN_FILE=%s/%s.cmake" % (TOOLCHAIN_DIR, profile),
            # passed explicitly rather than left to the toolchain, which sets it
            # with set(... CACHE ...) and so cannot override a stale cache entry
            "-DCONAN_PROFILE=%s" % profile,
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


def on_remote(query, pid, remote):
    """Whether the remote really has this package.

    conan search can under-report. Artifactory keeps a per-recipe search index
    and does not always refresh it when packages are added to a recipe it
    already knows, which is what happens when the recipe upload is skipped as
    up to date. Asking for the package's conaninfo goes by path instead of
    through that index, so it sees packages search does not.
    """
    result = subprocess.run(
        ["conan", "get", "%s:%s" % (query, pid), "conaninfo.txt", "-r", remote],
        capture_output=True, text=True)
    return result.returncode == 0


def auth_state(remote):
    """(user_name, authenticated) for a remote, (None, False) when unknown."""
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    try:
        subprocess.run(["conan", "user", "-r", remote, "--json", path],
                       capture_output=True, text=True)
        with open(path) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None, False
    finally:
        if os.path.exists(path):
            os.unlink(path)
    for entry in data.get("remotes") or []:
        if entry.get("name") == remote:
            return entry.get("user_name"), bool(entry.get("authenticated"))
    return None, False


def upload_commands(missing, remote, use_all):
    """conan upload argv for each missing package.

    --all uploads every package the cache holds for a reference, not just the
    missing ones, which is one command per reference instead of one per
    package. Fine when the cache only holds the configurations you mean to
    publish, which is the case for a cache built by this script. Without it
    each package is named explicitly, so nothing else can ride along.
    """
    if use_all:
        refs = []
        for query, _ in missing:
            if query not in refs:
                refs.append(query)
        return [["conan", "upload", ref, "--all", "-r", remote, "--check", "-c"]
                for ref in refs]
    return [["conan", "upload", "%s:%s" % (query, pid),
             "-r", remote, "--check", "-c"] for query, pid in missing]


def run_uploads(missing, remote, dry_run, use_all):
    user, authed = auth_state(remote)
    if not user:
        print("\nno user set for %s. authenticate first:\n"
              "  conan user <your-username> -r %s -p" % (remote, remote),
              file=sys.stderr)
        return 1
    if not authed:
        print("\nnote: %s has user '%s' but no verified token; continuing anyway"
              % (remote, user))

    cmds = upload_commands(missing, remote, use_all)
    print("\n=== uploading %d package(s) to %s as '%s'%s\n"
          % (len(missing), remote, user, " (dry run)" if dry_run else ""))
    for index, cmd in enumerate(cmds, 1):
        if dry_run:
            cmd = cmd + ["--skip-upload"]
        print("[%d/%d] %s" % (index, len(cmds), " ".join(cmd)), flush=True)
        if subprocess.run(cmd).returncode != 0:
            print("\nupload failed, stopping with %d of %d done. if this is an "
                  "authentication or permission problem:\n"
                  "  conan user <your-username> -r %s -p"
                  % (index - 1, len(cmds), remote), file=sys.stderr)
            return 1
    if dry_run:
        print("\n=== dry run finished, nothing was sent")
    else:
        print("\n=== uploaded %d package(s). re-run with --no-build to confirm."
              % len(missing))
    return 0


def plan(args):
    refs = args.ref or cache_refs()
    if not refs:
        print("nothing in the local conan cache", file=sys.stderr)
        return 1

    archs = args.arch or DEFAULT_ARCHS
    print("\n=== packages for os=%s arch=%s build_type=%s\n"
          % (args.os_, "|".join(archs), args.build_type))

    missing, rows = [], []
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
            if pkg["id"] in theirs:
                where = "on %s" % args.remote
            elif on_remote(query, pkg["id"], args.remote):
                where = "on %s (not indexed)" % args.remote
            else:
                where = "MISSING"
                missing.append((query, pkg["id"]))
            rows.append((ref, settings.get("arch"),
                         "%s/%s" % (settings.get("compiler"),
                                    settings.get("compiler.version")),
                         where))

    width = max([len(r[0]) for r in rows] or [0])
    for ref, arch, compiler, where in rows:
        print("  %-*s  %-8s %-9s %s" % (width, ref, arch, compiler, where))

    if not missing:
        print("\n=== nothing to upload, %s has everything" % args.remote)
        return 0

    if args.upload or args.dry_run:
        return run_uploads(missing, args.remote, args.dry_run, args.all)

    print("\n=== %d to upload. authenticate, then run these:\n" % len(missing))
    print("conan user <your-username> -r %s -p\n" % args.remote)
    for cmd in upload_commands(missing, args.remote, args.all):
        print(" ".join("'%s'" % part if ":" in part or "@" in part else part
                       for part in cmd))
    print("\n# or let this script do it: rerun with --upload once you have")
    print("# authenticated. --dry-run rehearses without sending anything.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rnbo_version", nargs="?",
                        help="rnbo version to build against, eg 1.4.5")
    parser.add_argument("rnbo_tag", nargs="?", default="c74/stable")
    parser.add_argument("-j", "--jobs", type=int,
                        default=int(os.environ.get("RPI_DEPS_JOBS") or DEFAULT_JOBS),
                        help="parallel jobs for dependency builds (default: %d)"
                             % DEFAULT_JOBS)
    parser.add_argument("--all", action="store_true",
                        help="upload with conan's --all, one command per "
                             "reference. uploads every package the cache holds "
                             "for that reference, not only the missing ones")
    parser.add_argument("--upload", action="store_true",
                        help="actually upload the missing packages, rather than "
                             "printing the commands. authenticate first with "
                             "conan user")
    parser.add_argument("--dry-run", action="store_true",
                        help="like --upload but passes --skip-upload, so the "
                             "checks and compression run and nothing is sent")
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

    # never sit at a prompt. without this conan asks for credentials when a
    # remote needs them, which hangs a non-tty run and quietly waits for input
    # in an interactive one. we would rather it failed immediately and said so.
    os.environ["CONAN_NON_INTERACTIVE"] = "1"

    if not args.no_build:
        if not args.rnbo_version:
            parser.error("rnbo_version is required unless --no-build is given")
        if not build(args.rnbo_version, args.rnbo_tag, args.jobs):
            return 1
    return plan(args)


if __name__ == "__main__":
    sys.exit(main())
