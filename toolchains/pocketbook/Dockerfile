# syntax=docker/dockerfile:1
FROM ghcr.io/fstanis/pocketbook-sdk-qt6-builder@sha256:1878e90fd4c9b8e736cd49b61f79cbd6fba354f6c12686a3a7c35696f2e2eb06

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl python3 pkg-config ninja-build patch qemu-user 7zip \
    libpng-dev:armel qt6-base-dev qt6-sensors-dev libqt6sql6-sqlite libpng-dev \
    && rm -rf /var/lib/apt/lists/*

# Pin both the CMake helpers and their SDK header/library inputs.
RUN git clone https://github.com/fstanis/pocketbook-sdk-qt6.git /opt/pocketbook-sdk-qt6 \
    && git -C /opt/pocketbook-sdk-qt6 checkout 754e436c7c24b4e2695154e2efffbf7447b4130e \
    && sdk=/opt/pocketbook-sdk-qt6/sdk/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/local \
    && mkdir -p "$sdk/include" "$sdk/lib" \
    && for file in include/inkview.h include/hwconfig.h lib/libinkview.so lib/libhwconfig.so; do \
        curl -fL --retry 3 "https://raw.githubusercontent.com/pocketbook/SDK_6.3.0/dd8de463733a2bc0d53d1214b64734586fac427e/SDK-B288/usr/arm-obreey-linux-gnueabi/sysroot/usr/local/$file" -o "$sdk/$file"; \
       done
ENV POCKETBOOK_QT_SDK=/opt/pocketbook-sdk-qt6
WORKDIR /work
