# A cross toolchain for Kobo firmware 5.x, the Qt 6 line. It is the Qt 6
# counterpart of NickelTC (https://github.com/pgaskin/NickelTC), which covers
# firmware 3.x and 4.x with Qt 5.2.1.
#
# Kobo built firmware 5.18 with GCC 11.5.0 and glibc 2.35, a Yocto kirkstone
# set, and its Qt 6.5.2 links system ICU 70, glib 2.72, fontconfig, FreeType,
# HarfBuzz, xkbcommon, D-Bus and OpenGL ES 2. Ubuntu 22.04 ships the same
# glibc, GCC 11.4 and the same library generation for armhf, so this image
# uses Ubuntu's cross compiler and armhf multiarch packages as the sysroot
# instead of a crosstool-ng build. GCC 11.4 and 11.5 share the C++ ABI
# (GLIBCXX_3.4.29); the firmware's libstdc++ is 6.0.29 as well.
#
# Qt starts from the official 6.5.2 archive pinned in source.json. Local
# patches reproduce Kobo's Qt and SQLite changes. The build keeps the firmware
# system libraries and OpenGL ES 2 configuration because those features affect
# private Qt class layouts. Only qtbase is built here; Kobalt builds Qt Sensors
# separately from its pinned dependency manifest.
#
# Layout of the result, under /tc/arm-kobo-linux-gnueabihf:
#   bin/arm-kobo-linux-gnueabihf-*  compiler and binutils; gcc and g++ carry
#                                   the Kobo CPU flags, so no flags are needed
#   bin/qmake6, moc, uic, rcc, ...  Qt tools for the host, targeting the device
#   bin/arm-kobo-linux-gnueabihf-pkg-config
#   qt6/                            Kobo's Qt 6.5.2 built for the device
#   host/                           the host build of the Qt tools
#   kobo.cmake                      the compiler toolchain file
#   toolchain.cmake                 Qt's toolchain file; chainloads kobo.cmake
# The C library and system libraries stay where Ubuntu puts cross packages:
# /usr/arm-linux-gnueabihf and /usr/lib/arm-linux-gnueabihf.

# The digest is the multi-architecture index, so amd64 and arm64 hosts build
# the same recipe for their native host CPU. Both cross-compile ARM32 code;
# the host programs such as moc and rcc must run on the container host CPU.
FROM docker.io/library/ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc AS base
ARG DEBIAN_FRONTEND=noninteractive

# The armhf packages come from ports.ubuntu.com on every host architecture.
# The stock sources.list must be restricted to the native architecture first,
# or apt looks for armhf packages on archive.ubuntu.com and fails.
RUN set -eu; \
    arch="$(dpkg --print-architecture)"; \
    sed -i "s|^deb |deb [arch=$arch] |" /etc/apt/sources.list; \
    dpkg --add-architecture armhf; \
    for pocket in jammy jammy-updates jammy-security; do \
        echo "deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports $pocket main universe"; \
    done > /etc/apt/sources.list.d/armhf.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        bash bzip2 ca-certificates cmake coreutils curl file g++ gcc git jq \
        libdbus-1-dev make ninja-build patch pkg-config python3 unzip xz-utils zip \
        gcc-11-arm-linux-gnueabihf g++-11-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
        libc6-dev:armhf libstdc++-11-dev:armhf \
        libatspi2.0-dev:armhf libdbus-1-dev:armhf libdrm-dev:armhf libegl-dev:armhf libevdev-dev:armhf \
        libfontconfig-dev:armhf libfreetype-dev:armhf libgbm-dev:armhf libgles-dev:armhf \
        libglib2.0-dev:armhf libharfbuzz-dev:armhf libicu-dev:armhf libinput-dev:armhf \
        libmtdev-dev:armhf libpcre2-dev:armhf libpng-dev:armhf libsqlite3-dev:armhf \
        libssl-dev:armhf libudev-dev:armhf libxkbcommon-dev:armhf libzstd-dev:armhf \
        zlib1g-dev:armhf; \
    rm -rf /var/lib/apt/lists/*

# These COPY paths belong to the isolated context staged by tools/build_engine.py.
# All inputs come from this repository; no simulator files enter the image.
FROM base AS build

# Verify the upstream archive checksum and apply the firmware patches before
# either Qt build. A failed download or patch leaves no reusable source tree.
COPY runtime/qt6-source /tc/source
RUN python3 /tc/source/prepare.py /tc/src/qtbase

COPY experiments/toolchain-qt6/wrappers.sh experiments/toolchain-qt6/kobo.cmake experiments/toolchain-qt6/build-qt.sh /tc/src/
COPY experiments/toolchain-qt6/mkspecs /tc/src/mkspecs
# Compiler wrappers select ARMv7 Thumb, NEON and hard-float calling conventions.
# The pkg-config wrapper searches target library metadata instead of host Qt.
RUN /tc/src/wrappers.sh /tc/arm-kobo-linux-gnueabihf

# The Qt build takes tens of minutes. It runs at its final path because the
# generated toolchain files and qmake wrappers record absolute paths.
# build-qt.sh first builds native host tools, then cross-compiles the device
# libraries with QT_HOST_PATH pointing at those tools. Device binaries are
# not executed during this stage.
RUN /tc/src/build-qt.sh /tc/arm-kobo-linux-gnueabihf /tc/src

# Keep the installed SDK and cross-library packages, leaving Qt build trees
# and unpacked sources in the build stage. This is a development image, not a
# device filesystem or a complete corresponding-source distribution.
FROM base AS toolchain
# qemu-arm runs the ARM component checks against this Qt; see tools/checks.py.
# It is installed here so that adding it does not rebuild Qt.
RUN apt-get update && apt-get install -y --no-install-recommends qemu-user && rm -rf /var/lib/apt/lists/*
COPY --from=build /tc/arm-kobo-linux-gnueabihf /tc/arm-kobo-linux-gnueabihf
COPY experiments/toolchain-qt6/check.sh /tc/arm-kobo-linux-gnueabihf/check.sh
# CMake 3.21 and later read CMAKE_TOOLCHAIN_FILE from the environment, so a
# plain cmake call cross-compiles here.
ENV PATH=/tc/arm-kobo-linux-gnueabihf/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    CROSS_TC=arm-kobo-linux-gnueabihf \
    CMAKE_TOOLCHAIN_FILE=/tc/arm-kobo-linux-gnueabihf/toolchain.cmake \
    PKG_CONFIG=arm-kobo-linux-gnueabihf-pkg-config \
    LC_ALL=C.UTF-8
# Compile/link smoke checks cover ARM EABI/VFP/NEON, firmware feature headers,
# pkg-config, qmake, AUTOMOC and private Qt headers. They do not execute a mod
# in Nickel or establish compatibility with untested firmware versions.
RUN /tc/arm-kobo-linux-gnueabihf/check.sh && rm -rf /tmp/*
CMD ["/bin/bash"]
