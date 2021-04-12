## To setup docker:
* grab your rpi libraries etc
```shell
rsync -vR --progress -rl --delete-after --safe-links pi@raspberrypi.local:/"{lib,usr,opt/vc/lib}" rpi-rootfs
rm rpi-rootfs/usr/lib/systemd/system/rnbo.service
```
* copy over the sdbus-0.8.3 directory
  This was a pain, check the details below
* build docker image:
```shell
docker build -f Dockerfile -t xnor/rpi-buster-audio-xpile:0.4 .
```

cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH CONAN_CMAKE_FIND_ROOT_PATH=/rootfs/ CONAN_CMAKE_SYSROOT=/rootfs/ cmake -DRNBO_DIR=../cpp/ -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake ..


```shell
docker run -it -v $(pwd):/build xnor/rpi-buster-audio-xpile:0.4
docker run -it -v $(pwd):/build xnor/rpi-buster-audio-xpile:0.4 /bin/bash
```

```shell
mkdir -p ~/.conan/profiles
cd /build/examples/RNBOOSCQueryRunner/runner/
mkdir build-rpi && cd build-rpi
cp ../rpi-profile ~/.conan/profiles/rpi && PATH=/opt/cross-pi-gcc/bin:$PATH cmake -DCMAKE_TOOLCHAIN_FILE=../rpi-toolchain.cmake -DCONAN_PROFILE=rpi .. && make
```

## share to docker hub
```shell
docker push xnor/rpi-buster-audio-xpile:0.4
```

### SDus binaries

The sdbus conan setup doesn't seem to like just downloading the binaries I've
pushed up to bintray, so to work around that we just sidestep conan for this
target in the cross compile, pointing to a directory of include files and
pre-built libraries.

Here are the contents:

```
sdbus-0.8.3/
sdbus-0.8.3//include
sdbus-0.8.3//include/sdbus-c++
sdbus-0.8.3//include/sdbus-c++/Error.h
sdbus-0.8.3//include/sdbus-c++/ConvenienceApiClasses.inl
sdbus-0.8.3//include/sdbus-c++/TypeTraits.h
sdbus-0.8.3//include/sdbus-c++/IProxy.h
sdbus-0.8.3//include/sdbus-c++/Types.h
sdbus-0.8.3//include/sdbus-c++/ConvenienceApiClasses.h
sdbus-0.8.3//include/sdbus-c++/Flags.h
sdbus-0.8.3//include/sdbus-c++/ProxyInterfaces.h
sdbus-0.8.3//include/sdbus-c++/Message.h
sdbus-0.8.3//include/sdbus-c++/StandardInterfaces.h
sdbus-0.8.3//include/sdbus-c++/IObject.h
sdbus-0.8.3//include/sdbus-c++/AdaptorInterfaces.h
sdbus-0.8.3//include/sdbus-c++/sdbus-c++.h
sdbus-0.8.3//include/sdbus-c++/MethodResult.h
sdbus-0.8.3//include/sdbus-c++/IConnection.h
sdbus-0.8.3//libs
sdbus-0.8.3//libs/libbz2.a
sdbus-0.8.3//libs/libz.a
sdbus-0.8.3//libs/libblkid.a
sdbus-0.8.3//libs/libsdbus-c++.a
sdbus-0.8.3//libs/libpcre2-posix.a
sdbus-0.8.3//libs/libpcre2-16.a
sdbus-0.8.3//libs/liblzma.a
sdbus-0.8.3//libs/liblz4.a
sdbus-0.8.3//libs/libpcre2-8.a
sdbus-0.8.3//libs/libsepol.a
sdbus-0.8.3//libs/libmount.a
sdbus-0.8.3//libs/libpcre2-32.a
sdbus-0.8.3//libs/libzstd.a
sdbus-0.8.3//libs/libselinux.a
sdbus-0.8.3//libs/libsystemd.a
sdbus-0.8.3//libs/libcap.a
```

To get these I built the main RNBOOSCQueryRunner project on an rpi, with
`VEBOSE=1 make` to see what libraries it linked, then I copied those out
of the conan directores and made the `libs` directory that you see.
I also copied the header files `sdbus-c++` into thet `included` dir that you see.
