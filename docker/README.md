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

*TODO* Fix DBUS

64-bit rpi

```shell
mkdir -p /build/build-rpi64
cd /build/build-rpi64/
cmake -DRNBO_DIR=/build/rnbo/ \
    -DCMAKE_BUILD_TYPE=Release \
    -DRNBO_DIR=/rnbo/ \
    -DWITH_DBUS=Off \
    -DSUPPORT_COMPILE=Off \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    ..  && make -j8 && cpack
```

32-bit rpi

```shell
mkdir -p /build/build-rpi32
cd /build/build-rpi32/
cmake -DRNBO_DIR=/build/rnbo/ \
    -DCMAKE_BUILD_TYPE=Release \
    -DRNBO_DIR=/rnbo/ \
    -DWITH_DBUS=Off \
    -DSUPPORT_COMPILE=Off \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=armhf \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/armv7-unknown-linux-gnueabihf-gcc11_4.cmake \
    ..  && make -j8 && cpack
```

Move

```shell
mkdir -p /build/build-move
cd /build/build-move/
cmake -DRNBO_DIR=/build/rnbo/ \
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


# OLD

## To setup docker:
* grab your rpi libraries etc

```shell
rsync -vR --progress -rl --delete-after --safe-links pi@raspberrypi.local:/"{lib,usr,opt/vc/lib}" rpi-rootfs
rm rpi-rootfs/usr/lib/arm-linux-gnueabihf/libpthread.a rpi-rootfs/usr/lib/arm-linux-gnueabihf/libdl.a
rm rpi-rootfs/usr/lib/systemd/system/rnbo*
rm -r rpi-rootfs/usr/bin rpi-rootfs/usr/sbin
```


* build docker image:

```shell
docker build -f Dockerfile.bookworm -t xnor/rpi-bookworm-audio-xpile:0.3 .
```

cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH CONAN_CMAKE_FIND_ROOT_PATH=/rootfs/ CONAN_CMAKE_SYSROOT=/rootfs/ cmake -DRNBO_DIR=../cpp/ -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake ..


```shell
docker run -it --platform linux/amd64 -v $(pwd):/build -v ~/Documents/rnbo-docker-rpi-bookworm-conan-1.61/:/home/runner/.conan/ xnor/rpi-bookworm-audio-xpile:0.3 /bin/bash
```

```shell
mkdir -p ~/.conan/profiles
cd /build/examples/rnbo.oscquery.runner/runner/
mkdir build-rpi && cd build-rpi
cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH cmake -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake -DCONAN_PROFILE=rpi .. && make
```

## share to docker hub
```shell
docker push xnor/rpi-bookworm-audio-xpile:0.3
```

# RPI 5

* start from the slim 64-bit bookworm image
* install the development libraries from README-rpi.md
* grab the sysroot

```shell
rsync -vR --progress -rl --delete-after --safe-links pi5:/"{lib,usr,opt/vc/lib}" rpi-rootfs
rm rpi-rootfs/usr/lib/aarch64-linux-gnu/libpthread.a rpi-rootfs/usr/lib/aarch64-linux-gnu/libdl.a
```

* build docker image:

```shell
docker build -f Dockerfile.bookworm64bit -t xnor/rpi-bookwork64-audio-xpile:0.3 .
```

# 64-bit build

