## v1.0-beta.3

### What's changed

- Added Automatic brightness for Kobo devices with an ambient light sensor. Manual brightness adjustments turn it off and stop a pending brightness animation from overwriting the chosen level. Cancelled gestures restore the original level and setting.

- Publisher Fonts uses Libron for serif, Legible Next for sans-serif and IBM Plex Mono for monospace when the book has no embedded fonts. The default reading font remains a separate book override.

- Added hyphenation dictionaries for Estonian, Finnish, Georgian, Hungarian, Latvian, Lithuanian, Polish, Slovak, Slovenian and Ukrainian.

- Page buttons now page Chapters, Bookmarks, search results and licenses instead of being ignored. This can be managed via the "Hide non-linear fragments" setting.

- Long chapters in the Stats chart fill the chart height without a visible line through the bars when chapters are very long relative to other chapters.

- Kobo devices with an orientation sensor now offer Portrait, Landscape and Both. Portrait lock is the default, and rejected orientations no longer trigger page layout changes. Devices without a sensor keep manual rotation.

- Save changed reading progress to Nickel every 30 seconds while reading and before sleep, so the sleep screen can show the current progress.

- Save reading progress before USB export, while preserving newer synced progress and allowing USB to continue if saving fails.

- Show the book-loading screen at 0% in Kobalt's interface fonts, before publication inspection and full asset validation. This makes Kobalt feel faster to start and feels less like it's frozen.

- Opening the font and layout panels is now significantly faster.

- Fixed excessive view redrawing throughout Kobalt, which in practice means you see the screen flash less frequently.

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
