## v1.0-beta.2

### Fixed

- Fixed opening EPUBs with repeated identical manifest items. Conflicting duplicate IDs remain unsupported.
- Fixed opening EPUBs with standard IDPF-obfuscated embedded TrueType or OpenType fonts.

- Fixed left alignment and justification moving publisher-centered text to the left.
- Fixed reversed landscape touch targets on affected Clara devices with firmware 4, using NickelRotate’s coordinate correction.
- Fixed opening and saved chapter lookup for sideloaded filenames containing `#`, `?` or literal percent escapes on firmware 4. If files fail to open, an explanation is logged.
- Fixed opening DRM-free local EPUBs regardless of their folder, including hidden folders, SD storage, Dropbox, Google Drive and third-party sync services. Protected books still open in the built-in reader.
- Fixed missing Chinese, Japanese, Korean and other bundled fallback characters in chapter titles and interface controls. CJK fallback text and CFF font previews use Kobalt’s font renderer to avoid crashes in the firmware font driver.
- Load private interface fallback fonts when text first needs them, including the CJK collection. This is more memory efficient.

## v1.0-beta.1

### What's New

First public Kobo beta, using Qt5 on firmware 4.38 and later.
