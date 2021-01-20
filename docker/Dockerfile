#derived from https://bitbucket.org/mitchallen/pi-cross-compile/src/master/Dockerfile
FROM debian:buster-slim
MAINTAINER Alex Norman "xnor@cylcing74.com"

# USAGE: docker run -it -v ~/raspberry/hello:/build mitchallen/pi-cross-compile

#LABEL com.mitchallen.pi-cross-compile="{\"Description\":\"Cross Compile for Raspberry Pi\",\"Usage\":\"docker run -it -v ~/myprojects/mybuild:/build mitchallen/pi-cross-compile\",\"Version\":\"0.1.0\"}"

RUN apt-get update && apt-get upgrade -y
RUN DEBIAN_FRONTEND=noninteractive \
	apt-get -y --no-install-recommends install \
	git wget build-essential \
	python3-pip python3-setuptools \
	libavahi-compat-libdnssd-dev libsndfile1-dev libssl-dev libjack-jackd2-dev
RUN DEBIAN_FRONTEND=noninteractive \
	apt-get -y install --reinstall ca-certificates
RUN pip3 install conan
RUN git clone --progress --verbose https://github.com/raspberrypi/tools.git --depth=1 pitools
RUN mkdir -p local/src/ && cd local/src \
	&& wget https://github.com/Kitware/CMake/releases/download/v3.19.3/cmake-3.19.3.tar.gz \
	&& tar xf cmake-3.19.3.tar.gz && cd cmake-3.19.3 && ./bootstrap && make && make install

ENV BUILD_FOLDER /build
WORKDIR ${BUILD_FOLDER}
