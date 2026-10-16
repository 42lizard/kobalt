# PocketBook build and installation

Experimental target: **PocketBook InkPad Color 3 (PB743K3)** with firmware
**U743k3.6.10.2854**, using its Qt 6.8.2 and InkView runtime. Other firmware and
models have not been validated. This checkout contains the complete application
build, SDK recipe, firmware preparation, package builder and validation tools;
the PocketBook monorepo is not required.

## Build from a standalone checkout

Install Python 3.11+ and start Docker or Podman. Clone the fork's port branch:

```sh
git clone --branch pocketbook-port https://github.com/42lizard/kobalt.git
cd kobalt
```

Download the [official PB743K3 firmware](https://download.pocketbook-int.com/fw/Yitoa/743k3/user/20260105/sw_20260105_U743k3_6.10.2854_user.zip)
and extract `SWUPDATE.BIN` on the computer. It is a local build input: do not
copy it to the reader. The builder verifies its exact SHA-256 before use.

```sh
./build-pocketbook.sh --firmware /absolute/path/SWUPDATE.BIN --jobs 4 --test
```

The wrapper works from any current directory. `CONTAINER_ENGINE=docker` or
`CONTAINER_ENGINE=podman` selects an engine explicitly; otherwise the shared
Kobalt detector chooses a running engine. The build uses the container host's
native architecture and an ARM soft-float cross-compiler. `toolchains/pocketbook/Dockerfile`
pins the SDK base image, CMake helpers and InkView SDK revision. The first run
needs network access and several GiB for the SDK, dependencies and firmware
extraction. Subsequent runs verify and reuse firmware inputs, dependency builds
and the SDK image. No other checkout or externally installed SDK is needed.

When migrating an existing monorepo build, move its old `build/pocketbook`
directory aside once before running this command. CMake caches contain absolute
paths from the former `/workspace/external/kobalt` container mount; this workflow
uses `/work`. A fresh clone needs no migration. Keep `SWUPDATE.BIN` outside that
output directory.

Outputs:

- `build/pocketbook/Kobalt-PB743K3.zip`: installable application package.
- `build/pocketbook/pocketbook.json`: source commit, dirty flag, source hash,
  firmware hash, SDK image ID, package checksum and runtime-test status.
- `build/pocketbook/arm-runtime-test/`: optional ARM workflow screenshots.

The build requires committed, clean source by default. Use `--allow-dirty` for
local development; the record marks that result as modified. A failed build
does not replace the previous package. The build always checks firmware imports
and runs the package, registration and build-tool tests. `--test` additionally
runs the host EPUB workflow and the real ARM workflow with no configured IP
network. Only that test mode grants the container `SYS_ADMIN`, to create an
isolated network namespace; the installed reader needs no elevated rights.

Firmware validation checks ARM softfp, `/lib/ld-linux.so.3`, absence of build
RPATHs, direct library availability and exact strong imported symbol versions.
ARM emulation uses the firmware's Qt/libc and offscreen QPA with stubbed InkView
hardware calls. Its public workflow opens an EPUB, waits for a completed page,
turns a page, closes and restores the same content on reopening. These checks
do not establish native QPA, touch, refresh or task-manager behavior on hardware.

For host regression tests inside the built SDK:

```sh
docker run --rm -v "$PWD:/work" -w /work kobalt-pocketbook-sdk:local \
  python3 pocketbook/build.py --host --regression --jobs 4
```

The lower-level `pocketbook/build.py`, `prepare_firmware.py`,
`check_firmware.py` and `check_arm_runtime.py` remain available for diagnosis.
The ARM runner accepts `--book PATH` to use a local EPUB instead of its fixture.
All generated output and firmware inputs belong in ignored `build/` directories.

## Install

Connect the PB743K3 in USB storage mode. Back up the previous application files
before updating. Extract the ZIP on the computer and merge its paths into the
device root, preserving existing books, configuration and other applications:

```text
applications/kobalt.app
system/bin/kobalt.app
system/kobalt/bin/kobalt
system/kobalt/assets/
system/kobalt/notices/
```

The package includes the upstream fonts and their notices. It contains no Qt
libraries, firmware, Kobo installation files, books or user state. Both launcher
files are required: the native library resolves handlers under `system/bin`.

Register the additional reader from this checkout:

```sh
python3 pocketbook/register_reader.py /Volumes/PB743K3
```

The helper preserves the current default and other EPUB readers. It backs up
an existing `extensions.cfg` once and is safe to repeat. Safely eject the device,
disconnect USB and fully restart after replacing a running reader. In the native
library, long-press an unprotected EPUB and choose **Open with → Kobalt**.
Starting Kobalt without a book opens the native library; there is no separate
library or desktop file picker.

Positions, bookmarks and preferences remain in `system/kobalt/config`. Logs are
in `system/kobalt/app.log` and `system/kobalt/diagnostics/`. Native progress and
annotation interoperability, Readest sync, automatic sensor rotation, frontlight
and dictionary integration are not implemented. See [reader behavior](../docs/reader.md#experimental-pocketbook-host).

To remove Kobalt, delete its two launchers and its `bin`, `assets` and `notices`
directories. Keep `config` to retain reading state. Remove only `kobalt.app` from
the EPUB association in `system/config/extensions.cfg`; do not blindly restore
an old backup over later association changes.

## Source and validation

The prior build `6de0711` has confirmed startup and EPUB opening on the device.
The rebased upstream-synchronized reader `1a17e2f` has passed 107 host test groups
and the exact-firmware offline ARM workflow, including two device books. It has
been installed, but physical acceptance of that synchronized version is pending.

The standalone buildchain was validated from an independent clone on an ARM64
Docker host, mounting only this checkout and the firmware file. The complete
build/package command, host reading workflow, offline ARM workflow, ten Python
build/package/registration checks and all 108 host regression groups passed.
The SDK base provides AMD64 and ARM64 images; AMD64 and Podman builds have not
been exercised. Kobo build entry points are unchanged; no new Kobo cross-build
or physical PocketBook acceptance run was performed for the tooling move.

This buildchain does not publish a release or install onto a device automatically.
The combined package includes GPL/AGPL dependencies. Use Kobalt's
[corresponding-source procedure](../docs/development.md#ci-and-tagged-releases)
when distributing binaries; the SDK recipe and firmware requirements must stay
documented with the source. Firmware is not part of the distributable package.
