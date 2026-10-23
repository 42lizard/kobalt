## Experimental PocketBook build

{{package_links}}

Other models and firmware versions have not been validated. Host and emulated
ARM tests do not establish physical-device touch or display behavior.

### Installation

1. Connect the reader in USB storage mode and back up any previous Kobalt installation.
2. Extract the ZIP on the computer and merge `applications/` and `system/` into
   the reader's root, preserving other applications, books and configuration.
3. To register Kobalt in **Open with**, run `pocketbook/register_reader.py` from
   the source archive against the mounted reader. It preserves the current EPUB default.
4. Eject safely, disconnect USB and fully restart after replacing a running reader.
5. In the native library, long-press an unprotected EPUB and choose **Open with → Kobalt**.

The package contains no firmware or Qt runtime libraries. The official firmware
is required only as a verified build input and is not redistributed.
See `pocketbook/README.md` in the source archive for removal and platform limitations.

[Checksums]({{checksums_url}}) · [Build information]({{build_info_url}})
