## To setup docker:
* grab your rpi libraries etc
```shell
rsync -vR --progress -rl --delete-after --safe-links ${RPI_HOST}:/"{lib,usr,opt/vc/lib}" rpi-rootfs
```
* build docker image:
```shell
docker build -f Dockerfile -t xnor/rpi-xpile .
```

cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH CONAN_CMAKE_FIND_ROOT_PATH=/rootfs/ CONAN_CMAKE_SYSROOT=/rootfs/ cmake -DRNBO_DIR=../cpp/ -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake ..


```shel
docker run -it -v /Users/xnor/dev/max.maxcore/:/build  xnor/rpi-xpile /bin/bash
```

```shell
mkdir -p ~/.conan/profiles
cd /build/examples/RNBOOSCQueryRunner/runner/
mkdir build-rpi && cd build-rpi
cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH CONAN_CMAKE_FIND_ROOT_PATH=/rpi-rootfs/ CONAN_CMAKE_SYSROOT=/rpi-rootfs/ cmake -DRNBO_DIR=../cpp/ -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake -DCONAN_PROFILE=rpi .. && make
```
