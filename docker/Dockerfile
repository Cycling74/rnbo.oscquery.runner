#Dockerfile for setting up environment for cross compiling for raspbian rpi

#derived from https://bitbucket.org/mitchallen/pi-cross-compile/src/master/Dockerfile
FROM debian:bullseye-slim
MAINTAINER Alex Norman "xnor@cycling.com"

ENV DEBIAN_FRONTEND=noninteractive

#initial setup
RUN apt-get -y update && apt-get upgrade -y
RUN apt-get -y --no-install-recommends install \
	git gpg wget build-essential pkg-config \
	python3-pip python3-setuptools \
	libssl-dev \
	cmake \
	gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf \
	ca-certificates

#conan setup
RUN pip3 install conan==1.58.0
RUN mkdir -p /root/.conan/profiles
COPY conan-rpi-xcompile /root/.conan/profiles/default

COPY rpi-rootfs /rpi-rootfs

CMD ["/bin/bash", "/build/examples/rnbo.oscquery.runner/docker/build-rpi.sh"]
