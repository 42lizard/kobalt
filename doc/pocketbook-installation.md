# PocketBook release installation

Download `Kobalt-PB743K3.zip` and verify it against `SHA256SUMS`. Supported build target: InkPad Color 3, firmware U743k3.6.10.2854. Back up the previous installation and merge the ZIP's `applications/` and `system/` directories into the mounted device root, preserving books and configuration. Eject safely and restart the reader.

Launching Kobalt opens its own library. To retain the native library's Open with integration, use `platforms/pocketbook/register-reader.sh` from the source archive. See [PocketBook instructions](pocketbook.md) for registration, removal, saved data and limitations. The package contains no firmware or Qt runtime libraries. Physical-device acceptance remains pending.
