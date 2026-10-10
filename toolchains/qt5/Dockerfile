# Keep Kobo's Qt5 and C/C++ runtime ABI, but use a compiler with C++17 syntax.
FROM --platform=linux/amd64 ghcr.io/pgaskin/nickeltc@sha256:3296eb400d270cfd26f5773ab09625d4469f85680b3a191951dfb78d8ade7840 AS nickel
FROM docker.io/library/ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake ninja-build make patch pkg-config python3 qemu-user \
    gcc-11-arm-linux-gnueabihf g++-11-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
    && rm -rf /var/lib/apt/lists/*
COPY --from=nickel /tc/arm-nickel-linux-gnueabihf/arm-nickel-linux-gnueabihf /tc/qt5
# The SDK's moc/rcc/uic are x86-64. Keep their own loader and libraries so the
# same SDK also runs on ARM64 hosts without rebuilding Qt's code generators.
COPY --from=nickel /lib/x86_64-linux-gnu /tc/qt5-host/lib/x86_64-linux-gnu
COPY --from=nickel /lib64 /tc/qt5-host/lib64
COPY --from=nickel /usr/lib/x86_64-linux-gnu/libstdc++.so.6* /tc/qt5-host/usr/lib/x86_64-linux-gnu/
COPY toolchains/qt5/toolchain.cmake toolchains/qt5/wrappers.py toolchains/qt5/cxx-compat.h /tc/qt5/
RUN python3 /tc/qt5/wrappers.py
ENV PATH=/tc/qt5/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    CMAKE_TOOLCHAIN_FILE=/tc/qt5/toolchain.cmake LC_ALL=C.UTF-8
CMD ["/bin/bash"]
