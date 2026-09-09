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
binaries once makes later builds download them instead. Three steps: build,
identify, upload.

### 1. Build

Start the container with the repo mounted and a *persistent* conan home, so the
cache survives the container. `docker/conan` is already gitignored apart from
`profiles/`:

```shell
docker run -it \
    --platform linux/amd64 \
    -v $(pwd):/build \
    -v $(pwd)/docker/conan:/home/build/.conan \
    xnor/rnbo-runner-xpile:0.3 \
    /build/docker/build-rpi-deps.sh 1.4.5
```

That configures both rpi targets with the same flags CI uses and leaves the
resulting packages in the cache. It only configures: every conan install happens
at cmake configure time, so the runner itself never has to compile.

The flags have to match CI exactly. Package ids are derived from settings and
options, so a dependency built with different ones produces an id CI will not
match, and it will rebuild from source anyway.

### 2. Identify

From a shell in the same container, with the same mounts:

```shell
/build/docker/conan-upload-plan.py
```

It lists every `Release` package in the cache for `armv7` and `armv8`, checks
each against the remote, and prints the upload command for the ones missing.
It uploads nothing. Useful flags: `--arch armv8` for one target, `--remote`,
`--ref` to limit to a package, `--os`/`--build-type` for other configurations.

### 3. Upload

Authenticate, then run the commands it printed:

```shell
conan user -r cycling-public -p "$C74_CONAN_PASSWORD" "$C74_CONAN_USER"
conan upload 'boost/1.86.0@:8df48fb69d6cf4f688675634989c1f9b04d6bad7' -r cycling-public --check -c
```

Add `--skip-upload` to any of them to rehearse: it runs the checks and the
compression but sends nothing.

### Things worth knowing

Uploading a package uploads its recipe too if the remote does not have it, and
that has a consequence. Conan 1.x binds a recipe to the remote it came from and
then looks for binaries **only there**. Once `boost/1.86.0` exists on
`cycling-public`, every build resolves the recipe from there rather than
conancenter, so any configuration whose binary is *not* on `cycling-public`
gets built from source instead of downloaded from conancenter. If you push the
arm binaries, consider pushing the ones for the machines you develop on too.

A package only helps if its id matches what the consumer asks for. Anything that
changes a dependency's settings or options -- a compiler version bump in the
image, a changed option in `CMakeLists.txt` -- produces a new id, and that
package needs building and pushing again.
