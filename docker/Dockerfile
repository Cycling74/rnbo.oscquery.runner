#adapted from the move-everything docker file
#https://github.com/charlesvestal/move-everything/blob/main/Dockerfile

#cross-compiling for aarch64 targeting Ableton Move
FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture arm64 && \
    dpkg --add-architecture armhf && \
    echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu jammy main universe\n\
deb [arch=amd64] http://archive.ubuntu.com/ubuntu jammy-updates main universe\n\
deb [arch=amd64] http://archive.ubuntu.com/ubuntu jammy-security main universe\n\
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports jammy main universe\n\
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports jammy-updates main universe\n\
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports jammy-security main universe" > /etc/apt/sources.list

RUN apt-get update && \
    apt-get install -y \
    curl file wget \
    git vim \
    gcc-11-aarch64-linux-gnu g++-11-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    gcc-12-aarch64-linux-gnu g++-12-aarch64-linux-gnu \
    gcc-11-arm-linux-gnueabihf g++-11-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
    gcc-12-arm-linux-gnueabihf g++-12-arm-linux-gnueabihf \
    build-essential \
    python3 python3-pip python3-setuptools \
    libdbus-1-dev:arm64 libdbus-1-dev:armhf \
    libsystemd-dev:arm64 libsystemd-dev:armhf \
    libsndfile1-dev:arm64 libsndfile1-dev:armhf \
    libatomic1:arm64 libatomic1:armhf \
    libavahi-compat-libdnssd-dev:arm64 libavahi-compat-libdnssd-dev:armhf \
    libssl-dev:arm64 libssl-dev:armhf \
    libxml2-dev:arm64 libxml2-dev:armhf \
    libjack-jackd2-dev:arm64 libjack-jackd2-dev:armhf \
    libsdbus-c++-dev:arm64 libsdbus-c++-dev:armhf \
    libgmock-dev:arm64 libgmock-dev:armhf \
    google-mock:arm64 google-mock:armhf \
    libstdc++-11-dev:arm64 libstdc++-11-dev:armhf \
    libgcc-11-dev-arm64-cross libgcc-11-dev-armhf-cross \
    libstdc++-12-dev:arm64 libstdc++-12-dev:armhf \
    libgcc-12-dev-arm64-cross libgcc-12-dev-armhf-cross \
    && rm -rf /var/lib/apt/lists/*

# ubuntu jammy ships cmake 3.22, but libossia requires >= 3.25
# (cmake_minimum_required(VERSION 3.25...3.31) in its CMakeLists.txt) and CI
# builds libossia from source, so apt's cmake is too old. 3.28.1 is what the
# Move builder image carries and is known to configure this libossia.
# Deliberately staying on 3.x: cmake 4 drops support for
# cmake_minimum_required(VERSION <3.5), which older conan recipes still declare.
# This image is only ever built --platform=linux/amd64, hence the x86_64 tarball.
ARG CMAKE_VERSION=3.28.1
ARG CMAKE_SHA256=f76398c24362ad87bad1a3d6f1e8f4377632b5b1c360c4ba1fd7cd205fd9d8d4
RUN curl -fsSL -o /tmp/cmake.tar.gz \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" && \
    echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum -c - && \
    tar -xzf /tmp/cmake.tar.gz -C /usr/local --strip-components=1 && \
    rm /tmp/cmake.tar.gz && \
    cmake --version

# Install rust system-wide
ENV RUSTUP_HOME=/usr/local/rustup
ENV CARGO_HOME=/usr/local/cargo
ENV PATH="/usr/local/cargo/bin:${PATH}"
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --target aarch64-unknown-linux-gnu --target armv7-unknown-linux-gnueabihf && \
    chmod -R a+rX /usr/local/rustup /usr/local/cargo

### Setup user and system
RUN useradd -ms /bin/bash build
RUN mkdir -p /home/build
RUN chown -R build:build /home/build

USER build

#install conan
RUN pip3 install conan==1.64.0

WORKDIR /home/build

# Copy conan profiles, cmake toolchains and scripts
COPY --chown=build:build conan/profiles/ /home/build/.conan/profiles/
COPY --chown=build:build cmake/ /home/build/cmake/
COPY --chown=build:build scripts/ /home/build/scripts/

# Set cross-compilation environment
ENV CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc
ENV CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER=arm-linux-gnueabihf-gcc
ENV PATH="${PATH}:/home/build/.local/bin:/home/build/scripts"
