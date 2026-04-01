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
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
    build-essential cmake \
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
    && rm -rf /var/lib/apt/lists/*

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
RUN pip3 install conan==1.62.0

WORKDIR /home/build

# Copy conan profiles, cmake toolchains and scripts
COPY --chown=build:build conan/profiles/ /home/build/.conan/profiles/
COPY --chown=build:build cmake/ /home/build/cmake/
COPY --chown=build:build scripts/ /home/build/scripts/

# Set cross-compilation environment
ENV CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc
ENV CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER=arm-linux-gnueabihf-gcc
ENV PATH="${PATH}:/home/build/.local/bin:/home/build/scripts"
