## To setup docker:
* grab your rpi libraries etc

```shell
rsync -vR --progress -rl --delete-after --safe-links pi@c74rpi.local:/"{lib,usr,opt/vc/lib}" rpi-rootfs
rm rpi-rootfs/usr/lib/arm-linux-gnueabihf/libpthread.a rpi-rootfs/usr/lib/arm-linux-gnueabihf/libdl.a
rm rpi-rootfs/usr/lib/systemd/system/rnbo*
rm rpi-rootfs/usr/bin
```


* build docker image:

```shell
docker build -f Dockerfile -t xnor/rpi-bookworm-audio-xpile:0.1 .
```

cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH CONAN_CMAKE_FIND_ROOT_PATH=/rootfs/ CONAN_CMAKE_SYSROOT=/rootfs/ cmake -DRNBO_DIR=../cpp/ -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake ..


```shell
docker run -it -v $(pwd):/build -v ~/Documents/rnbo-docker-rpi-bookworm-conan-1.61/:/root/.conan/ xnor/rpi-bookworm-audio-xpile:0.1 /bin/bash
docker run -it -u root -v $(pwd):/build -v ~/Documents/rnbo-docker-rpi-bookworm-conan-1.61/:/root/.conan/ xnor/rpi-bookworm-audio-xpile:0.1 /bin/bash
```

```shell
mkdir -p ~/.conan/profiles
cd /build/examples/rnbo.oscquery.runner/runner/
mkdir build-rpi && cd build-rpi
cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH cmake -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake -DCONAN_PROFILE=rpi .. && make
```

## share to docker hub
```shell
docker push xnor/rpi-bookworm-audio-xpile:0.1
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
docker build -f Dockerfile.bookworm64bit -t xnor/rpi-bookwork64-audio-xpile:0.1 .
```
