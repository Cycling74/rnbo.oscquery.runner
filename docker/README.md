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

should be able to share .so with rpi and move

```
docker run -it -v $(pwd):/build -v ~/dev/rnbo.core/src/cpp/:/rnbo -v $(pwd)/conan:/home/build/.conan -v $(pwd)/cmake:/home/build/cmake --platform linux/amd64 rnbo.move.takeover:0.3 bash
mkdir -p /build/build-rpi /build/build-move
cd /build/build-rpi/
cmake -DRNBO_DIR=/build/rnbo/ \
    -DCMAKE_BUILD_TYPE=Release \
    -DRNBO_DIR=/rnbo/ \
    -DWITH_DBUS=Off \
    -DSUPPORT_COMPILE=Off \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    ..  && make -j8 && cpack
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

```
 docker run -it -v $(pwd):/build -v ~/dev/rnbo.core/src/cpp/:/rnbo -v /Users/xnor/dev/rnbo.move.takeover/rnbo.oscquery.runner/conan:/home/build/.conan -v /Users/xnor/dev/rnbo.move.takeover/rnbo.oscquery.runner/cmake:/home/build/cmake --platform linux/amd64 rnbo.move.takeover:0.3 bash
cmake \
    -DCMAKE_TOOLCHAIN_FILE=/home/build/cmake/toolchains/aarch64-unknown-linux-gcc11_4.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64 \
    -DCMAKE_BUILD_RPATH=/data/UserData/rnbo/lib/ \
    ..  && make -j8

```
