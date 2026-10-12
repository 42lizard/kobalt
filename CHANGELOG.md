## v1.0-beta.3

### What's changed

- Update slider rows and the font sample in the Text and Layout panels with partial refreshes on MediaTek, so they no longer flash.

- Restore the heavy rule below the Settings panel.

- Extend the active tab's fill to the rule above the navigation bar while a panel is open.

- Page buttons now page Chapters, Bookmarks, search results and licenses instead of being ignored.

- Long chapters in the Stats chart fill the chart height without a notch.

- Show rotation labels beside the icons when they fit, and icons only on narrower screens. Use white unselected split buttons with thin borders and black selected buttons. Add space between rotation icons and labels.

- Kobo devices with an orientation sensor now offer Portrait, Landscape and Both. Portrait lock is the default, and rejected orientations no longer trigger page layout changes. Devices without a sensor keep manual rotation.

- Reduce unnecessary layout-progress and Stats redraws. Layout progress advances across phases and updates at most once per 200 ms between phase boundaries. On MediaTek, an isolated progress-bar update uses partial mode in light mode over a text-only page. Avoid repainting unchanged appearance panels, Chapters and the font picker. Merge adjacent widget updates with the same refresh policy. Keep separated panel damage regions separate. Open the main reading bars together in one screen-spanning update. Keep existing waveforms, update modes and closing cleanup. Position Text, Layout and Stats before showing them. Keep the current book frame until an appearance setting or viewport changes. Localize link highlights, header and footer changes, and panel pagination. Batch opening progress within the current event-loop pass. Keep toolbar rules in place when panels open, and skip unchanged brightness notices.

- Save changed reading progress to Nickel every 30 seconds while reading and before sleep, so the sleep screen can show the current progress.

- Save reading progress before USB export, while preserving newer synced progress and allowing USB to continue if saving fails.

- Fixed unchanged header and footer text flashing during ordinary page turns on MTK devices. These turns now use partial update mode.

- Use whole-screen coverage for ordinary page turns on older and unknown hardware, keeping the existing update mode and waveform.

- Show the book-loading screen at 0% in Kobalt's interface fonts, before publication inspection and full asset validation.

- Open appearance panels immediately over the current page, prepare their preview after painting, and preserve the top bar when switching panels. Load only visible font picker samples and avoid layout work for outline thickness. Keep the Text and Layout controls and Overflow usable while the preview opens or renders, so repeated drags and drafts opened in Overflow keep their latest values. Show a thickness change and its font sample in the same screen update.

- Show both reading bars together in one screen-spanning text update on MediaTek. Combine compatible search keyboard key damage into one rectangle and keep an already visible keyboard in place. Refresh a slider and its value as one rectangle.

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
