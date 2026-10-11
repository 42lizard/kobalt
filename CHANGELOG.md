## v1.0-beta.3

### What's changed

- Save changed reading progress to Nickel every 30 seconds while reading and before sleep, so the sleep screen can show the current progress.

- Save reading progress before USB export, while preserving newer synced progress and allowing USB to continue if saving fails.

- Fixed unchanged header and footer text flashing during ordinary page turns on MTK devices. These turns now use partial update mode.

- Use whole-screen coverage for ordinary page turns on older and unknown hardware, keeping the existing update mode and waveform.

- Avoid cleanup flashes when opening reading controls over text-only pages in light mode. Closing the controls by tapping the page or title retains cleanup. Dark-mode controls use cleanup when opening and closing.

## v1.0-beta.2

### What's changed

- Fixed opening and appearance previews for EPUBs whose structural CSS selectors require styles to be recalculated after parsing. Publisher image spacing and centered text are preserved.

- Fixed opening EPUBs with repeated identical manifest items. Conflicting duplicate IDs remain unsupported.

- Fixed opening EPUBs with standard IDPF-obfuscated embedded TrueType or OpenType fonts.

- Added evenly spaced line-spacing slider marks with snapping at 100%, 110%, 120%, 130%, 140%, 150%, 175%, 200%, 250% and 300%. Plus and minus still adjust spacing by one percentage point.

- Fixed line spacing being applied twice. 150% now means 1.5 times the font size. Existing book settings are converted to preserve their approximate spacing, with 122% becoming 150% and a maximum of 300%.

- Fixed left alignment and justification moving publisher-centered text to the left.

- Fixed reversed landscape touch targets on affected Clara devices with firmware 4, using NickelRotate’s coordinate correction.

- Fixed opening and saved chapter lookup for sideloaded filenames containing `#`, `?` or literal percent escapes on firmware 4. If files fail to open, an explanation is logged.

- Fixed opening DRM-free local EPUBs regardless of their folder, including hidden folders, SD storage, Dropbox, Google Drive and third-party sync services. Protected books still open in the built-in reader.

- Fixed missing Chinese, Japanese, Korean and other bundled fallback characters in chapter titles and interface controls. CJK fallback text and CFF font previews use Kobalt’s font renderer to avoid crashes in the firmware font driver.

- Load private interface fallback fonts when text first needs them, including the CJK collection. This is more memory efficient.

- Fixed Kobalt remaining inactive when Nickel's initialization callback was missed.

## v1.0-beta.1

### What's New

First public Kobo beta, using Qt5 on firmware 4.38 and later.
