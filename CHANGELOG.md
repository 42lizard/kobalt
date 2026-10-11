## v1.0-beta.2

### Fixed

- Fixed left alignment and justification moving publisher-centered text to the left.

- Fixed reversed landscape touch targets on affected Clara devices with firmware 4, using NickelRotate’s coordinate correction.

- Fixed opening and saved chapter lookup for sideloaded filenames containing `#`, `?` or literal percent escapes on firmware 4. If files fail to open, an explanation is logged.

- Fixed opening DRM-free EPUBs from SD storage, including KoboCloud books under `/mnt/sd/kobocloud/`.

- Fixed opening DRM-free EPUBs downloaded through Kobo's built-in Dropbox and Google Drive integrations. Protected books still open in the built-in reader.

## v1.0-beta.1

### What's New

First public Kobo beta, using Qt5 on firmware 4.38 and later.
