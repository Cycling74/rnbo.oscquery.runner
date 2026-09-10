# Docker based cross compile for RNBO Runner

Here we build for Linux aarch64 (64-bit rpi+) and armv7 gnueabihf (32-bit rpi+)

## Build docker image

```shell
docker build --platform=linux/amd64 -t xnor/rnbo-runner-xpile:0.3 .
```

Share to docker hub

```shell
docker push xnor/rnbo-runner-xpile:0.3
```

## Using docker image

If you haven't pulled or built locally

```shell
docker pull xnor/rnbo-runner-xpile:0.3
```

### RNBO Runner

Should be able to share .so with rpi and move

From the top level rnbooscquery directory, start up docker
Make sure to update the `/rnbo` mount to match the location of your rnbo c++ library directory

```shell
docker run -it \
    --platform linux/amd64 \
    -v $(pwd):/build \
    -v ~/dev/rnbo.core/src/cpp/:/rnbo \
    -v $(pwd)/docker/conan:/home/build/.conan \
    xnor/rnbo-runner-xpile:0.3 bash
```

64-bit rpi

```shell
mkdir -p /build/build-rpi64
cd /build/build-rpi64/
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DRNBO_DIR=/rnbo/ \
    -DSUPPORT_COMPILE=Off \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    ..  && make -j8 && cpack
```

32-bit rpi

```shell
mkdir -p /build/build-rpi32
cd /build/build-rpi32/
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DRNBO_DIR=/rnbo/ \
    -DSUPPORT_COMPILE=On \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=armhf \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/armv7-unknown-linux-gnueabihf-gcc12.cmake \
    ..  && make -j8 && cpack
```

Move

```shell
mkdir -p /build/build-move
cd /build/build-move/
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DRNBO_DIR=/rnbo/ \
    -DWITH_DBUS=Off \
    -DSUPPORT_COMPILE=Off \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    -DCMAKE_BUILD_RPATH=/data/UserData/rnbo/lib/ \
    -DUSE_SNDFILE_CONAN=On \
    -DWITH_JACKSERVER=Off \
    ..  && make -j8
```

rnbo-update-service

64-bit rpi

```shell
mkdir -p /build/update/build-rpi64
cd /build/update/build-rpi64/
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    ..  && make -j8 && cpack
```

32-bit rpi

```shell
mkdir -p /build/update/build-rpi32
cd /build/update/build-rpi32/
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=armhf \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/armv7-unknown-linux-gnueabihf-gcc11_4.cmake \
    ..  && make -j8 && cpack
```

jack transport link

```shell
docker run -it \
    --platform linux/amd64 \
    -v $(pwd):/build \
    xnor/rnbo-runner-xpile:0.3 bash
```

64-bit rpi

```shell
mkdir -p /build/build-rpi64
cd /build/build-rpi64/
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    ..  && make -j8 && cpack
```

32-bit rpi

```shell
mkdir -p /build/build-rpi32
cd /build/build-rpi32/
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/armv7-unknown-linux-gnueabihf-gcc11_4.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=armhf \
    ..  && make -j8 && cpack
```

Move

```shell
mkdir -p /build/build-move
cd /build/build-move/
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_BUILD_RPATH=/data/UserData/rnbo/lib/ \
    ..  && make -j8

```


## Prebuilding dependencies for the rpi builds

CI builds every conan dependency from source on each run, because most of them
have no binaries on `cycling-public` for our arm configurations. Pushing those
binaries once makes later builds download them instead.

`docker/rpi-deps.py` does the whole thing: it configures both rpi targets, lists
the `Release` arm packages that came out, and prints the upload command for each
one the remote does not have. It uploads nothing itself.

Both targets are covered in one pass. Note the 32-bit profile declares
`arch=armv7hf` while some packages already on `cycling-public` were built with
an older profile that used `armv7`, so the default filter accepts `armv7`,
`armv7hf` and `armv8`.

Start the container with the repo mounted and a *persistent* conan home, so the
cache survives the container. `docker/conan` already holds the profiles and is
gitignored apart from `profiles/`:

```shell
docker run -it \
    --platform linux/amd64 \
    -v $(pwd):/build \
    -v $(pwd)/docker/conan:/home/build/.conan \
    xnor/rnbo-runner-xpile:0.3 \
    /build/docker/rpi-deps.py 1.4.5
```

Pass the rnbo version you are building against; add the conan tag as a second
argument if it is not `c74/stable`. It only configures, because every conan
install happens at cmake configure time, so the runner itself never has to
compile for the cache to fill.

Dependency builds are capped at half the cores. Left alone conan uses every core
it can see, and Docker Desktop reports every host CPU to the container while
giving the VM far less memory than that implies: 12 CPUs against 8.8 GiB here.
ossia's `osc_factory.cpp` wants multiple GB by itself, so twelve concurrent
compilers exhaust memory and one gets killed. That surfaces as a truncated
diagnostic followed by `Error 2` with no `error:` line anywhere, which reads
like a source problem but is not one. Use `--jobs` or `RPI_DEPS_JOBS` to change
the cap.

The output ends with something like:

```
=== packages for os=Linux arch=armv7|armv8 build_type=Release

  boost/1.86.0        armv8  gcc/11.4   MISSING
  zlib/1.3            armv8  gcc/11.4   on cycling-public

=== 1 to upload. authenticate, then run these:

conan user <your-username> -r cycling-public -p

conan upload 'boost/1.86.0@:8df48fb6...' -r cycling-public --check -c
```

Run those in the same container. `-p` with no value prompts for the password
rather than leaving it in your shell history, and `--skip-upload` on any upload
rehearses it: the checks and compression run, nothing is sent. Push a small
package first to confirm you have write permission before the big ones.

Or let the script do all of them. Authenticate once, then re-run with
`--upload`:

```shell
conan user <your-username> -r cycling-public -p
/build/docker/rpi-deps.py --no-build --upload
```

`--dry-run` in place of `--upload` rehearses the whole set without sending
anything. `--all` switches to conan's `--all`, one command per reference rather
than one per package; that uploads every package the cache holds for a
reference, not only the missing ones, which is equivalent here because the cache
was built by this script and holds nothing else.

If a package reads `on cycling-public (not indexed)`, it is there and will be
used, but `conan search` cannot see it. Artifactory keeps a per-recipe search
index and does not always refresh it when packages are added to a recipe it
already knows -- which is exactly what happens when conan reports `Recipe is up
to date, upload skipped`. The script confirms by asking for the package's
conaninfo, which goes by path rather than through the index, so it will not
tell you to re-upload something that is already published. Either way it refuses to start when no user is set for the remote,
and stops at the first failure rather than grinding through the rest, saying
how many got through. Conan runs non-interactively throughout, so a missing or
rejected login fails immediately instead of waiting at a prompt.

Each command uploads the recipe as well as the binary, so a package the remote
has never seen arrives complete. Two commands for the same package (one per
arch) re-check the recipe on the second run, which is harmless.

Afterwards re-run with `--no-build` to confirm the uploads landed; anything that
worked flips from `MISSING` to `on cycling-public`. Other flags: `--arch armv8`
for a single target, plus `--remote`, `--ref`, `--os` and `--build-type`.

### Things worth knowing

The flags the script passes mirror `.github/workflows/build.yml`, and they have
to. Package ids follow settings and options, so a dependency built with
different ones produces an id CI will not match, and it rebuilds from source
anyway. The same applies over time: a compiler bump in this image gives every
package a new id, and they all need building and pushing again.

Uploading a package uploads its recipe too if the remote does not have it, and
that has a consequence. Conan 1.x binds a recipe to the remote it came from and
then looks for binaries **only there**. Once `boost/1.86.0` exists on
`cycling-public`, every build resolves the recipe from there rather than
conancenter, so any configuration whose binary is *not* on `cycling-public` gets
built from source instead of downloaded from conancenter. If you push the arm
binaries, consider pushing the ones for the machines you develop on too.
