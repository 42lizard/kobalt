# Qt6 cross toolchain

The `qt6` build uses this local ARM hard-float SDK for Kobo firmware 5 and 6. It was made to match firmware 5.18.270971. The SDK contains Kobo's Qt 6.5.2 headers and libraries, a GCC 11 cross compiler and CMake and qmake wrappers. Its Ubuntu 22.04 armhf packages supply the device library headers.

The [Dockerfile](Dockerfile), [Qt patches and source pin](../source), [notice](../source/NOTICE) and [license texts](../source/LICENSES) are included in this repository. Building Kobalt does not require a simulator checkout. The root project license does not replace those terms.

## Build and reuse

From the repository root, run:

```sh
CONTAINER_ENGINE=docker python3 tools/build.py --jobs 2
```

The build helper checks the image fingerprint and builds the SDK only when required. It stages the public SDK inputs under the paths expected by the Docker recipe. Do not run the recipe directly against the repository root. Compiler and dependency build outputs stay under `build/`; source downloads stay under `.cache/`.

The local image name is `localhost/kobalt-toolchain-qt6:dev`. The helper checks its host architecture and `kobalt.recipe` label before reuse. Docker layer caching preserves expensive compiler and Qt build steps when their inputs have not changed. `check.sh` verifies ARM EABI5, VFP arguments, NEON, Qt feature headers and a plugin with AUTOMOC and private Qt headers. The image also has `qemu-arm`, which `tools/checks.py` uses to run the ARM checks against this Qt.

## SDK paths

The SDK installs under `/tc/arm-kobo-linux-gnueabihf`, on the image's `PATH`:

- `bin/arm-kobo-linux-gnueabihf-*`: compiler and binutils. Wrappers supply ARMv7, Thumb, NEON and hard-float flags.
- `bin/qmake6`, `moc`, `uic`, `rcc`, `qt-cmake`: host tools configured for the device.
- `bin/arm-kobo-linux-gnueabihf-pkg-config`: device package metadata.
- `qt6/`: Qt 6.5.2 headers, libraries, CMake packages and mkspecs.
- `toolchain.cmake`: Qt's CMake toolchain, selected through `CMAKE_TOOLCHAIN_FILE`.
- `kobo.cmake`: compiler toolchain without Qt configuration.
- `check.sh` and `manifest.txt`: SDK checks and installed package versions.

On firmware 5 and 6, Qt lives in `/usr/lib` and Nickel in `/usr/local/Kobo`. A mod needs no extra rpath for Qt. The compiler is GCC 11.4; the pinned firmware uses GCC 11.5 with the same libstdc++ ABI.

The Docker recipe builds qtbase only. The reader build adds the pinned Qt Sensors module separately. The SDK image does not include the complete corresponding source for every installed binary. Local build checks do not establish readiness to redistribute the image.
