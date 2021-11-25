#Dockerfile for setting up environment for cross compiling for raspbian rpi

#derived from https://bitbucket.org/mitchallen/pi-cross-compile/src/master/Dockerfile
FROM debian:buster-slim
MAINTAINER Alex Norman "xnor@cycling.com"

ENV DEBIAN_FRONTEND=noninteractive

#initial setup
RUN apt-get -y update && apt-get upgrade -y
RUN apt-get -y --no-install-recommends install \
	git gpg wget build-essential pkg-config \
	python3-pip python3-setuptools \
	libssl-dev \
	ca-certificates

#conan setup
RUN pip3 install conan
RUN mkdir -p /root/.conan/profiles
COPY conan-rpi-xcompile /root/.conan/profiles/default

#toolchain
RUN cd /tmp/ \
	&& wget https://github.com/Pro/raspi-toolchain/releases/latest/download/raspi-toolchain.tar.gz \
	&& tar xfz raspi-toolchain.tar.gz --strip-components=1 -C /opt

#cmake
RUN mkdir -p local/src/ && cd local/src \
	&& wget https://github.com/Kitware/CMake/releases/download/v3.19.3/cmake-3.19.3.tar.gz \
	&& tar xf cmake-3.19.3.tar.gz && cd cmake-3.19.3 && ./bootstrap && make && make install

COPY rpi-rootfs /rpi-rootfs

CMD ["/bin/bash", "/build/examples/RNBOOSCQueryRunner/docker/build-rpi.sh"]
