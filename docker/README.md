## To setup docker:
* grab your rpi libraries etc
```shell
rsync -vR --progress -rl --delete-after --safe-links ${RPI_HOST}:/"{lib,usr,opt/vc/lib}" rpi-rootfs
rm rpi-rootfs/usr/lib/systemd/system/rnbo.service
```
* build docker image:
```shell
docker build -f Dockerfile -t rnbo.rpi-xpile:0.1 .
```

cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH CONAN_CMAKE_FIND_ROOT_PATH=/rootfs/ CONAN_CMAKE_SYSROOT=/rootfs/ cmake -DRNBO_DIR=../cpp/ -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake ..


```shell
docker run -it -v $(pwd):/build rnbo.rpi-xpile:0.1
docker run -it -v $(pwd):/build rnbo.rpi-xpile:0.1 /bin/bash
```

```shell
mkdir -p ~/.conan/profiles
cd /build/examples/RNBOOSCQueryRunner/runner/
mkdir build-rpi && cd build-rpi
cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH cmake -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake -DCONAN_PROFILE=rpi .. && make
```
