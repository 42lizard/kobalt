# PocketBook build and installation

Experimental target: **PocketBook InkPad Color 3 (PB743K3)**, firmware **U743k3.6.10.2854**, Qt 6.8.2 and InkView. Other models and firmware versions are unvalidated. The application builds from this checkout alone; no parallel PocketBook repository is needed.

## Build

Install Git and Docker, then run:

```sh
git clone --branch pocketbook-port https://github.com/42lizard/kobalt.git
cd kobalt
./build-pocketbook.sh --test
```

The wrapper builds the pinned SDK recipe in `platforms/pocketbook/sdk/Dockerfile`, downloads and verifies the official firmware, configures CMake presets, and builds the application dependencies and reader. No Qt module is compiled. Firmware is extracted only under ignored `build/`; no firmware or Qt library is packaged. The first build requires network access and several GiB of disk space. Later runs reuse verified sources and incremental outputs. Linux and Docker are the validated host; the SDK also supplies ARM64 images. `CONTAINER_ENGINE` and `KOBALT_POCKETBOOK_IMAGE` select an engine or SDK image. Podman is unvalidated.

Pass `--firmware /absolute/path/SWUPDATE.BIN` to use a predownloaded copy of the pinned firmware, and `--jobs N` to select compiler parallelism.

The package is `build/pocketbook/Kobalt-PB743K3.zip`. Build metadata is `build/pocketbook/pocketbook.json`. Running without `--test` skips PocketBook host/QEMU workflows; native regression checks still run. Release drafts require the complete test run.

CMake definitions and presets are shared with the native development project under `platforms/desktop/`. The `pocketbook-host` preset selects the PocketBook host with native Qt; `pocketbook` selects the ARM softfp toolchain and verified firmware Qt libraries, including Sql. The firmware has no Qt Sensors module, so the ARM target uses manual rotation. Application, SDK and firmware responsibilities remain separate.

## Install

Back up an existing installation, connect the device in USB storage mode and merge the ZIP's `applications/` and `system/` directories into its root. Preserve other applications, books and configuration. Both `applications/kobalt.app` and `system/bin/kobalt.app` are required. Register the additional native-library handler from Linux:

```sh
bash platforms/pocketbook/register-reader.sh /path/to/mounted/PB743K3
```

This preserves the current EPUB default, backs up an existing `system/config/extensions.cfg` once, and is safe to repeat. Safely eject, disconnect USB and fully restart after replacing a running reader. In the native library, long-press an unprotected EPUB and select **Open with → Kobalt**. Returning closes Kobalt and restores the native library.

Launching Kobalt directly opens its own shared library. Add local folders or individual EPUBs, browse covers, sort books and resume reading. Returning from a book opened here shows Kobalt's library. Removing a book removes its index entry; it does not delete the EPUB. Home exits the app after saving and draining engine work.

## Reading and saved data

The port uses upstream beta.4's shared reader, including annotations, bookmarks, typography, navigation, dictionaries and reading statistics. The shared SQLite profile migrates older saved data while retaining migration backups; see [Storage compatibility](storage.md). Keep `system/kobalt/config/` when updating. Logs remain in `system/kobalt/app.log` and `system/kobalt/diagnostics/`.

Use **Settings → Dictionaries** to download Kobalt dictionaries over system Wi-Fi. Downloads are checksum-verified and installed dictionaries work offline. Hold a word to look it up; the selected dictionary is saved per book. Dictionary files and attribution remain under `system/kobalt/config/dictionaries/`. Native PocketBook dictionaries are separate.

Native progress/annotation interoperability, Readest synchronization, frontlight control and automatic sensor rotation remain unavailable. Physical-device acceptance of beta.4pb, touch, downloads and e-paper refresh is pending.

To uninstall, remove the two launchers and `system/kobalt/{bin,assets,notices}`. Keep `config` to retain saved data. Remove only `kobalt.app` from the EPUB handler list; do not restore an old association backup over later changes.

## Validation and releases

`--test` runs the upstream native regression suite, PocketBook host integration and offline ARM reading/dictionary/library workflows against the actual firmware libraries under QEMU. SQLite migration and annotation database checks also run on ARM using the firmware SQLite plugin. Screenshots cover light/dark mode at 300/212 PPI. Firmware ABI checks verify direct libraries, strong imports, ARM softfp, loader and absence of build rpaths. Host checks and firmware emulation do not establish physical-device behavior.

GitHub Actions builds PocketBook only. Branch builds retain one-day downloadable artifacts. Tags ending in `pb`, including `v1.0-beta.4pb`, create a draft release with the ZIP, corresponding application/dependency sources, metadata and SHA-256 checksums. No Kobo or Android release is built. Published releases cannot be overwritten by this workflow.
