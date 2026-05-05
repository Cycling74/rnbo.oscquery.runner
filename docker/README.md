# Docker based cross compile for RNBO Runner

Here we build for Linux aarch64 (64-bit rpi+) and armv7 gnueabihf (32-bit rpi+)

## Build docker image

```shell
docker build --platform=linux/amd64 -t xnor/rnbo-runner-xpile:0.1 .
```

Share to docker hub

```shell
docker push xnor/rnbo-runner-xpile:0.1
```

## Using docker image

If you haven't pulled or built locally

```shell
docker pull xnor/rnbo-runner-xpile:0.1
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
    xnor/rnbo-runner-xpile:0.1 bash
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
    -DSUPPORT_COMPILE=Off \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=armhf \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/armv7-unknown-linux-gnueabihf-gcc11_4.cmake \
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
    xnor/rnbo-runner-xpile:0.1 bash
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

