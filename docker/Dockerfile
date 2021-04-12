#Dockerfile for setting up environment for cross compiling for raspbian rpi

#derived from https://bitbucket.org/mitchallen/pi-cross-compile/src/master/Dockerfile
FROM debian:buster-slim
MAINTAINER Alex Norman "xnor@cylcing74.com"

RUN DEBIAN_FRONTEND=noninteractive apt-get -y update && apt-get upgrade -y
RUN DEBIAN_FRONTEND=noninteractive \
	apt-get -y --no-install-recommends install \
	git wget build-essential pkg-config \
	python3-pip python3-setuptools \
	libssl-dev \
	ca-certificates
RUN pip3 install conan
RUN cd /tmp/ \
	&& wget https://github.com/Pro/raspi-toolchain/releases/latest/download/raspi-toolchain.tar.gz \
	&& tar xfz raspi-toolchain.tar.gz --strip-components=1 -C /opt
RUN mkdir -p local/src/ && cd local/src \
	&& wget https://github.com/Kitware/CMake/releases/download/v3.19.3/cmake-3.19.3.tar.gz \
	&& tar xf cmake-3.19.3.tar.gz && cd cmake-3.19.3 && ./bootstrap && make && make install
COPY rpi-rootfs /rpi-rootfs
RUN mkdir -p /root/.conan/profiles
COPY sdbus-0.8.3 /sdbus-0.8.3
COPY conan-rpi-xcompile /root/.conan/profiles/default

#cache conan
RUN conan remote add cycling https://api.bintray.com/conan/xnor/cycling74
RUN conan install -r cycling libossia/1.2.1@xnor/testing
RUN conan install -r cycling cpp-optparse/cci.20171104@
RUN conan install -r cycling base64/0.4.0@
RUN conan install -r cycling sdbus-cpp/0.8.3@

CMD ["/bin/bash", "/build/examples/RNBOOSCQueryRunner/docker/build-rpi.sh"]
