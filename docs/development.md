# Building Kobalt

Install Python 3 and start Docker or Podman. Build the firmware 4 device package with:

```sh
CONTAINER_ENGINE=docker python3 tools/build.py --target qt5 --hardware --jobs 2
```

Use `--target qt6` for firmware 5 and 6 development builds. The build downloads dependencies pinned by URL and SHA-256 in `dependencies/manifest.json` and builds the matching toolchain. Keep the toolchain recipes, dependency patches and license files with the source. `crengine-vertical-writing.patch` imports the engine changes from [m-tky/crengine at 6a56ffa0f1af](https://github.com/m-tky/crengine/tree/6a56ffa0f1afc95e9e5463c5ca6584f57022d047) onto the pinned KOReader engine, after the existing Kobalt patches. It also exposes EPUB page progression to the adapter. The patch uses GPL v2 or later. `crengine-cache-write-errors.patch` keeps CREngine running when a write to its document cache fails, instead of ending the process. The failed cache file stays marked as incomplete and is never opened. `crengine-optical-size.patch` adds the reader’s manual optical-size override in points while preserving automatic CSS sizing when the override is zero. Update `dependencies/crengine-patches.txt` when changing the engine patches. The device, host-test and Android builders use that shared ordered inventory.

The firmware 5 and 6 package is `build/kobalt/reader-hardware/Kobalt.zip`. The firmware 4 package is `build/kobalt-qt5/reader-hardware/Kobalt.zip`. See [Reader behavior](reader.md) for installation and removal.

Report bugs through the issue tracker; pull requests are not accepted.

For a binary release, provide the corresponding source and dependency sources with their notices and licenses. The dependency archives downloaded by the build are under `.cache/kobalt/`.

## Android

Build the standalone app with `python3 android/build.py` from this public checkout. Debug builds use the repository's shared debug keystore and the `com.nicoverbruggen.kobalt.debug` app ID. Release builds keep `com.nicoverbruggen.kobalt` and require an external release key for signing. It uses the shared reader components and `src/platform/android/` host services. It does not link Nickel. [Android setup](../android/README.md) includes the complete toolchain environment, debug and release builds, signing with a PKCS#12 key, APK verification and installation. Run its environment exports in the same terminal as the build commands.

## macOS

Build a local testing app on macOS 14 or later. Install the Xcode command-line tools and Python 3.9 or later. CMake, Ninja, `pkg-config` and `patch` must be on `PATH`. To keep the additional build tools inside the checkout:

```sh
python3 -m venv build/macos-tools
build/macos-tools/bin/pip install cmake ninja pkgconf
export PATH="$PWD/build/macos-tools/bin:$PATH"
python3 tools/build_macos.py --jobs 2
open build/macos/Kobalt.app
```

Kobo, desktop, Android and host-test builds enable FriBidi, libunibreak and MathML in CREngine. FriBidi and libunibreak are pinned in `dependencies/manifest.json` and built as static libraries from their release archives. The builds need `make` and a POSIX shell for those two dependencies; their archives contain the generated Autotools scripts, so Autoconf and Automake are not required.

The macOS app and host engine tests fully link their FreeType archive so font initialization and glyph operations use one FreeType build. Linux host tests also hide the archive symbols from Qt. Qt keeps its own bundled FreeType. Both Kobo targets also link the pinned private FreeType archive and hide its symbols from Nickel.

The first build compiles the pinned host Qt SDK and engine dependencies. Later builds reuse `build/tests/<host>/`, the same dependency directory as the portable tests. `--offline` uses cached dependencies and fonts only. `--cache DIR` selects a source archive cache. `--qt PATH` selects an existing host SDK with its bundled libpng.

The build runs the desktop window tests, generates the Android artwork as a Mac icon, and packages Qt, fonts and hyphenation dictionaries in `build/macos/Kobalt.app`. The app can be moved out of the checkout. It has a local ad-hoc signature, without distribution signing or notarization. It builds for the Mac's current architecture; it is not a universal binary.

The desktop host uses the shared reader interface and the Qt-only Android controller and canvas. Its file picker, screen presets, keyboard and window lifecycle live in `src/platform/desktop/`. The toolbar offers Fill window and fixed 300 PPI 6-inch and 7-inch screens based on Clara Colour and Libra Colour specifications. The previews include device frames without logos; the 7-inch frame has page buttons outside the reading viewport. Device previews scale through the canvas; window resizing does not change their engine viewport. The canvas converts touchscreen gestures to mouse input. It ignores raw trackpad finger events because trackpads already provide mouse events. It does not link Nickel or the Android runtime. The desktop window tests also run in the portable test suite.

## Build Kobo packages

Run `./build-kobo.sh` from this repository to build public packages for both Qt5 and Qt6 with the published CI SDK images. It requires Python 3.9 or later, Git and a running Docker engine, with no local Qt SDK. The script defaults to Docker; `CONTAINER_ENGINE=podman` selects Podman. The checkout must be clean and committed. The script stops after a failed target or a source change. Outputs and source/image records go to `build/release/`. Use `--jobs NUMBER` to set compiler concurrency. The script does not install or publish packages. See [CI and tagged releases](#ci-and-tagged-releases) for individual packaging commands and source bundles.

## Font assets

IBM Plex, Noto and Instrument Serif remain in `resources/fonts/` with their license notices. Release packages include the reading fonts and their notices so installation needs no additional download.

Run `python3 tools/fetch_fonts.py` before a direct CMake build. Kobo and Android build scripts also run this step. The selection includes the Core Collection with Cartisse replaced by NV Ancizar Serif, plus NV Literata from the extra collection, with Regular, Italic, Bold and Bold Italic styles. The files come from [eBook Fonts](https://github.com/nicoverbruggen/ebook-fonts) and are cached in the ignored `vendor/ebook-fonts/` directory. Use `python3 tools/fetch_fonts.py --offline` to check that the cache contains every pinned file without using the network. Android's `--offline` option applies to these downloads too.

`resources/fonts/ebook-core.json` pins the upstream commit and the SHA-256 of each font. Builds reject missing or changed files. Libron 0.31 comes from its pinned GitHub release archive. The manifest verifies the archive and each of its four TTF styles by SHA-256. Font binaries are cached in the ignored vendor directory. Offline builds require a previously fetched cache. The other fonts retain their upstream revision. Keep the manifest and font license notices in source archives; binaries include the downloaded fonts and notices.

The Noto Serif CJK Regular collection is stored with the other Noto fallback fonts in `resources/fonts/`. Its source commit and checksum are in `resources/fonts/noto-cjk.json`. It adds about 25.2 MiB before compression and is included in Kobo, Android and macOS asset bundles. Its OFL notice appears in Acknowledgments.

Noto Naskh Arabic and Noto Serif fonts for Hebrew, Armenian, Georgian, Devanagari, Bengali, Tamil and Thai are stored in `resources/fonts/`. `noto-scripts.json` records their source revision and checksums. All platforms bundle them for CREngine fallback and register them with Qt for interface text. Their OFL notices appear in Acknowledgments.

Before a release, review the latest upstream collection and update the revision, file list and checksums together. Fetch that version and validate rendering before releasing. Builds do not follow a moving branch: the same release source must reproduce the same font assets. Review the font notices when changing the collection.

## C++ style

Use clang-format 18.1.8 with the checked-in `.clang-format`. It uses four spaces, attached braces, a 120-column target and separate lines for operations. Comment text and include order stay unchanged. Use braces for new branches and loops. The formatter does not insert or remove braces. Keep public ownership and failure contracts in headers when callers need them. Extract helpers when they make the sequence easier to follow; file size alone does not require a split.

Install the formatter in an ignored local environment, then check or apply the style:

```sh
python3 -m venv build/formatter-venv
build/formatter-venv/bin/python -m pip install clang-format==18.1.8
python3 tools/format_cpp.py --check --binary build/formatter-venv/bin/clang-format
python3 tools/format_cpp.py --write --binary build/formatter-venv/bin/clang-format
```

The default action is `--check`. The tool checks C++ files under `src/` and `tests/`, including new files but excluding ignored build output and third-party source. It rejects a different formatter version. Configure editor formatting to use the same executable and `.clang-format`. Keep mechanical formatting separate from behavior changes. Both Nickel Qt copies use this style independently.

For new includes, put the paired implementation header first, then project headers, Qt headers, standard C++ headers and operating-system headers. Separate the groups with a blank line and sort each group alphabetically. Preserve required ordering for private ABI headers and macro-dependent includes. The formatter preserves include order; changing existing order requires a separate reviewed build check.

## Shared reader controls

Reader implementations live in `src/interfaces/reader/`. `window/` contains reader coordination and appearance preview ownership. `views/` contains the reading page, navigation and opening view. `panels/` contains reading tools such as chapters, bookmarks, search and dictionary lookup. `settings/` contains settings, appearance controls, dictionary management and acknowledgments. `interface.cpp` provides the public entry points, and `Sources.cmake` lists the implementations for every host.

The shared Qt Widgets controls live in `src/interfaces/reader/ui/` and build in the single `reader-widgets` target. `style/` contains metrics, scale, themes and fonts. `controls/` groups buttons, icons, rows, options, sliders, text, panels and both pager families. `dialogs/` contains cards and modal overlays. Controls own painting, spacing and interaction. Panels own their children and internal layout. Navigation supplies placement and bounds. Reading state and device policy stay in services and controllers.

`Button` configures painting separately from its icon rendition. Theme replay retains the configured glyph, rendition and orientation artwork. `CaptionButton` preserves the separate caption-row and list-action presentations. `DescribedOption` uses one indicator enum and owns its presentation measurements. `ListRow` keeps its trailing caption and reserved width in one accessory value. Its labels remain available for accessibility and stable test identities; font size changes go through the row. `SettingSlider` sets its own density-scaled height so Nickel's fixed 80-pixel slider rule cannot clip its handle inside a scaled row.

Reader icons render vector geometry at the requested pixel size. The build runs `tools/ui_icon_vectors.py` to generate Qt paths from the checksum-verified Lucide SVGs in `resources/icons/lucide/`, including the manifest's stroke overrides. The generator accepts only the shapes used by those assets and rejects unsupported SVG features. It needs Python's standard library and adds no runtime SVG dependency. `QIconEngine` draws the paths directly or creates a bitmap at the exact requested physical resolution. Action icons fit their stroked vector bounds to the configured ink size without cropping or resizing a bitmap. Custom icons use the same renderer; chapters bars keep whole-pixel alignment. Preserve device pixel ratio, theme and active colors when adding an icon. The retained PNG files are comparison references and are not bundled into the reader. A shared 512 KiB cache retains final-size rasterizations by glyph, colors, rendition, orientation artwork, density, physical size, mode and state. Ordinary integral pixel placement of black or white ink reuses those pixels. Colored ink, reduced painter opacity, fractional placement, nonuniform scaling, rotation, reflection and other composition modes keep direct vector painting to preserve edge rounding. Oversized images render without entering the cache.

Presentation setters do not emit user intent. Repeated values avoid another paint or layout update. Keep native click signals, parent ownership, context-bound connections and release paint batching. Paint events draw existing state and do not change styles or layout. Preserve the distinct full-width and embedded pager contracts.

Use the focused headers for the controls a caller needs. Keep related small classes together. Do not add forwarding headers after all callers migrate. Shared controls use public Qt APIs supported by both Kobo builds. The Qt5 toolchain uses GCC 11 with GCC 4.9.4 standard-library headers and the older Kobo runtime. C++17 language support does not provide newer library containers, so use local tagged values when a library container is unavailable.

Run `python3 tools/ui_architecture.py` to check the component boundary. It follows local includes through their dependencies. The UI may depend on public Qt, strings, presentation helpers and font assets. It must not include reader orchestration, reading state, persistence or platform implementations. Generated string headers are allowed explicitly. Private Qt headers, includes escaping the source root, unknown local headers and retired visual properties fail the check. `reader_dark`, `reader_style` and internal diagnostic/cache properties remain valid.

## Tests

The tests in `tests/` build for this computer, not for a Kobo. They test the models, storage, publication code, the CREngine adapter, the reader window and the Android controllers. From `src/platform/nickel/` they test the position codec, location helper, USB removal state model, browser handoff, window presentation and sleep/shutdown coordination. The coordination tests compile the real adapter against fake Qt power and plug managers. They verify Kobalt's decisions without verifying Nickel's private ABI. Firmware adapters, hooks and device display behavior still need separate integration checks. Run the native suite with:

```sh
python3 tools/run_tests.py --jobs 2
```

The tool first checks the reading fonts, as `tools/fetch_fonts.py` does. Add `--offline` to use cached fonts and source archives only; a missing archive is then an error. `--cache DIR` keeps the source archives somewhere other than `.cache/kobalt/`. The first run then builds the pinned Qt Base and Qt Sensors from `toolchains/qt6/source/source.json` and `dependencies/manifest.json`, without the Kobo patches, into `build/tests/<host>/qt`. Later runs reuse it. The tool also builds CREngine and its pinned dependencies for the host. It needs CMake, Ninja, `pkg-config`, `patch`, a C++17 compiler and Python 3.9 or later. It does not install system packages. To use an existing Qt 6.5 SDK, pass `--qt PATH`. That SDK must include its bundled libpng, because the engine uses the same PNG library as Qt. With `--no-engine`, Qt 6.4 is enough, such as the packages of Ubuntu 24.04.

`--no-engine` builds only the tests that need no CREngine. `--build-only` skips the test run. Arguments after `--` go to `ctest`, for example `-- -R reader-window`.

Run `python3 tools/run_tests.py --coverage --jobs 2` with Clang to measure native source coverage. It uses a separate build directory, `build/tests/<host>/tests-coverage/`, and needs `llvm-profdata` and `llvm-cov` on `PATH` or through `xcrun` on macOS. Each run writes fresh profiles and reports under `coverage/run-*/`, so previous runs cannot raise its coverage. The tool prints covered and total lines, branches, functions and regions. `run.json` preserves the test exit status, test arguments and host even if reporting fails. `summary.json` records that metadata, per-file counts, source and binary fingerprints, and LLVM warnings. `regions.json` records source regions and branches for reviewing uncovered paths. Keep the reports with the test results when comparing runs.

The percentages include instrumented public `src/` files linked into registered native test executables. They exclude test code, Qt and engine dependencies. Unbuilt source files appear separately in `unmeasured_files`; they are not counted as covered. This includes firmware-specific Nickel code and Android JNI/runtime services. Python tooling tests run in the suite, but their coverage is not part of the LLVM percentages. A filtered CTest run measures only that run's paths. Do not compare it to a full-suite result as though both exercised the same tests. Adding a platform to the test build changes the denominator; compare per-file counts as well as aggregate percentages.

To regenerate a report from one run's profiles, use `python3 tools/test_coverage.py build/tests/<host>/tests-coverage --profiles build/tests/<host>/tests-coverage/coverage/run-NAME --output build/tests/<host>/tests-coverage/coverage/run-NAME`. Use the same test binaries and source as that run. The reporter compares their fingerprints with an existing summary and refuses changed inputs. Rebuilding them can invalidate profile mappings. LLVM can also warn about hash-zero mappings for unused inline functions; inspect other mismatches before accepting a report.

The `multilingual` check covers artificial Arabic and Hebrew edge cases, thirteen language fixtures and the public-domain gallery books. It verifies glyph coverage, mixed-direction runs, mixed-script text, search, word selection, chapter navigation, reflow and saved positions. The public-domain books exercise complete literary chapters and their LTR, RTL and vertical navigation. The `line-breaking` check compares Japanese strict and loose line breaking. The `mathml` check verifies fraction layout. The international gallery uses complete public-domain chapters from pinned Wikisource transcriptions. `tests/fixtures/public-domain/catalog.json` pins their data files and checksums. Each chapter records its source revision, original author and transcription license. Artificial books remain only in regression tests. Chapter-list screenshots cover Greek, Arabic and Japanese writing modes. Use `--group international` to capture only that section.

The suite is validated on macOS. The tests run headless: on macOS with Qt's `minimal` platform and FreeType fonts, elsewhere with `offscreen`.

The app does not ship a variable font. Engine-enabled host tests stage a genuine Fraunces variable font from the pinned HarfBuzz source archive into `fixtures/variable-fonts/`. `tests/fixtures/variable-font.json` records its provenance, size, checksum and expected weight and optical-size axes. The fixture script verifies its variation tables and stages the OFL notice with it. CTest sets `KOBALT_TEST_VARIABLE_FONTS` to this directory, so variable-font rendering, weight, optical-size and persistence checks run without a private fixture. The font stays outside installed reader assets. Update its manifest and notice when changing the dependency or fixture. Direct test executable runs need that environment variable too.

`tests/model/` holds small check programs, one per area. `tests/native-ui/` holds the Qt Test reader tests and the host services that replace the device. `tests/android/` tests the Android controllers with the same host services. `tests/fixtures/generate.py` writes regression books and the pinned public-domain chapter EPUBs. Register a new test in `tests/CMakeLists.txt`, in the section that matches what it needs.

## Native screenshot tour

Run `python3 tools/native_screenshots.py` to capture the shared reader UI with CREngine. It builds the native fixture using the same tools and dependency cache as `tools/run_tests.py`. It runs headlessly and leaves an open Mac app alone. On macOS, put the build tools on `PATH` as shown above.

```sh
export PATH="$PWD/build/macos-tools/bin:$PATH"
python3 tools/native_screenshots.py
open build/screenshots/index.html
python3 tools/native_screenshots.py --list
python3 tools/native_screenshots.py --group settings --group chapters --offline
python3 tools/native_screenshots.py --group settings --group about --ppi 300 --ppi 212 --offline
python3 tools/native_screenshots.py --screen dictionary/definition --theme dark --offline
python3 tools/native_screenshots.py --include-android --offline
```

Android screenshots are opt-in. The default sweep and `--group all` skip Android startup previews. Add `--include-android` to include those native previews, or select `--group android-startup` to capture only them. For actual Android screens, start a disposable headless Android emulator and build the debug APK first. Add `--android-serial emulator-PORT` to the screenshot command. This installs the APK and replaces Kobalt’s recent-book entry on that emulator. It never operates on a physical device. It captures phone, seven-inch reader, tablet and phone landscape startup states, recent-book removal, opening errors and Android’s document picker. The captures use Alice’s Adventures in Wonderland and its real cover. Each image records its APK checksum, dimensions and capture time. Native-only sweeps retain those emulator captures until another emulator sweep replaces them. Use `python3 tools/android_screenshots.py --serial emulator-PORT --book PATH` to update only the Android captures. Both commands update the same gallery. Add `--reader-checks` to the Android-only command to check seven phone, reader and tablet profiles with cutouts, landscape and distinct densities. It checks the physical reader viewport against Android cutout insets, sends page taps, verifies saved positions and reopens the reader. Each profile adds four reader images and a record in `build/screenshots/android-reader-validation.json`. The emulator size, density, rotation, navigation overlays and cutout overlays are restored after success or failure.

The default gallery is always `build/screenshots/index.html`. Run the command again to update it, then reload the page. Each run writes a self-contained `index.html` gallery and `manifest.json`. Native PNG files go under `build/screenshots/<theme>/<group>/`; Android emulator PNG files go under `build/screenshots/android/android-device/`. The gallery embeds the screenshots, fonts, CSS and JavaScript. Copy or open that single file without a server or network connection. The gallery shows light screenshots by default. Use the theme button to switch to dark screenshots without displaying both sets together. Filter by UI group or search text, then select a screenshot for a larger view. A capture set containing only one theme shows that theme and disables the switch. The individual PNGs remain available for direct inspection. Use `--gallery` to regenerate the HTML from an existing capture set without rebuilding or recapturing. Native images also have JSON with the visible controls, selected word, page and rendering environment. The manifest records book checksums, the capture executable's checksum and the asset manifest's checksum. A partial run replaces the selected groups, themes or individual screens and retains the other screenshots. Each new screenshot records its capture time and executable checksum, so retained captures can be distinguished from fresh ones. A full sweep replaces the current gallery and removes obsolete image files. Run a full sweep to change the viewport. Captures use temporary scratch space outside the gallery. A capture failure leaves the previous gallery intact and writes `build/screenshots/capture-error.log`. Temporary files are removed after success or failure. A lock prevents two capture runs from updating the same gallery. If a run is forcibly interrupted, remove its `.capture.lock` only after confirming the process has stopped.

The default captures both themes at 1264 × 1680 pixels and 300 ppi. Repeat `--ppi` to capture more than one density, for example `--ppi 300 --ppi 212`. Both UI and reading density are set before the reader widgets are constructed. Density does not change the device pixel ratio. Metadata records UI density as `ppi`, reading density as `reading_ppi`, and the capture pixel ratio as `dpr`. The 300 ppi paths stay unchanged; other densities go under `build/screenshots/ppi-<density>/<theme>/<group>/`. Partial runs retain captures at other densities. `--theme light` or `--theme dark` selects one. `--width` and `--height` change the viewport. All captures use the shared reader interface with the device emulation. The `international-writing-modes` group uses three complete chapters of Sōseki's `吾輩は猫である`, presented vertically, horizontally and vertically again. It checks those transitions and captures one vertical page and the chapter list in both themes. Chinese and Korean vertical captures use the same pinned chapter text with writing-mode styling; they do not reproduce the source edition’s page layout. Reading pages are grouped into basic, multilingual, mathematics, CJK horizontal and CJK vertical sections. Paragraphs and ruby come from the source transcription. Mixed blocks remain covered by the artificial regression book. Groups cover reading, settings, typography, layout, chapters, links, footnotes, dictionaries, search, bookmarks, frontlight, statistics, acknowledgments, finish and sync dialogs, equations and Japanese text. `--screen GROUP/NAME` uses the names from a previous manifest. Each group opens a fresh temporary reader profile, so it does not depend on earlier groups. The typography scene `fonts-long-names` substitutes synthetic labels in the existing font cells to show two-line names and a single-word ellipsis; it does not install those named fonts. `--no-build` reuses the existing executable; use it only after building the current source.

A cached full run of 170 screenshots on the development Mac took 1 minute 48 seconds with two capture processes, compared with 3 minutes 22 seconds serially. The PNG checksums matched between runs. These times exclude compilation and downloads; they depend on the machine and capture inventory.

Capture uses two isolated processes by default. Use `--jobs 1` for serial capture or increase `--jobs` if memory and CPU allow. Each group prints its elapsed time; `manifest.json` records the capture timings. The HTML is generated once after all selected groups succeed. Use `--group` or `--screen` to shorten a review run, `--theme light` to capture one theme, and `--no-build` to skip compilation when the executable already matches the source. The first run can take much longer because it downloads books and builds dependencies. Later runs reuse both caches.

The device services report 09:00 and a full battery. Frontlight and incoming sync positions are simulated. The frontlight group captures the controls and the brightness indicator during an actual edge swipe. The swipe checks that brightness changes without moving the reader position. Dictionary captures long-press actual book text and keep the selected text highlighted. Definitions come from the pinned English monolingual StarDict archive in koreader-dicts v1.2.0, installed and indexed through the dictionary manager in a temporary profile. The archive is cached under the dependency cache and verified against the catalogue checksum. Missing-entry and service-error states remain simulated and are identified in screenshot metadata. The dense statistics capture uses Moby Dick with more than one hundred chapters. These fixtures do not test platform sync transport.

`tests/fixtures/screenshot_books.json` pins Project Gutenberg editions of Alice's Adventures in Wonderland and Calculus Made Easy by URL and SHA-256. No downloaded EPUB is part of the source. International books are generated offline from the pinned chapter transcriptions in `tests/fixtures/public-domain/`. Books are cached in `kobalt-screenshot-books` under Python's temporary directory. Use `--book-cache DIR` to keep them elsewhere. A valid cached book is reused without a request; `--offline` fails if it is absent. To reuse an existing download, copy it into that cache under the filename from the manifest. A changed download or cache entry fails checksum verification instead of silently changing the screenshots. `--dependency-cache DIR` controls build archives separately.

The gallery lists exclusions: platform screens and keyboards, transient loading indicators and platform authorization failures. Font choices use up to three pages and licenses use one example. These screenshots cover the reader's UI states, not every book, device or possible setting combination. Pixel comparisons require the same Qt build, font assets, viewport and platform. The automated tour checks that controls open and images settle; a successful run does not establish that every captured screen looks correct.

For a UI refactor, preserve the affected capture set before editing production code. `python3 tools/ui_review.py compare BEFORE AFTER --batch NAME` compares matching screen, theme, density and fixture inputs, then appends changed, missing and additional images to `build/screenshots/ui-review.html`. Its copied images and previous entries survive gallery regeneration. `python3 tools/ui_review.py journal --phase NAME --message TEXT --test passed:RESULT` appends to `build/refactor-journal/index.html`. Use `failed:` or `blocked:` for those outcomes; add `--file PATH`, `--image PATH` and `--review difference-N` when relevant. Journal images are copied to immutable assets. Both pages reload every 20 seconds without running captures or tests. The latest progress report appears at the top of both pages. Journal history appears newest first; its JSON record remains in append order. Test results have status labels, and file lists and capture metadata can be expanded. Refresh preserves the reading position and open evidence sections. Images link to the full-size capture.

`tests/native-ui/correctness_checklist.json` fixes the refactor acceptance inventory. It maps expected behavior to Qt test slots, frozen scene names in both themes at 300/212 ppi and separately assigned platform gates. The registered `ui-correctness-inventory` test checks that these references still exist. To evaluate a sweep, run `python3 tests/tooling/test_ui_correctness.py --junit BUILD/native.xml --manifest build/screenshots/manifest.json --gates BUILD/gates.json --report BUILD/correctness.json`. Generate the native evidence with CTest `--output-junit` and `--test-output-size-passed 10000000`; gate records contain `exit_code`, `log` and exact `required_lines` from the verified platform run. The `visual-parity` gate also records `native_binary_sha256` so retained captures from an older binary cannot satisfy native capture entries. The report marks every entry passed, failed or blocked and exits nonzero for incomplete acceptance. Keep reports and platform logs in ignored build output. A native pass cannot satisfy a platform gate.

Run `python3 tools/ui_review.py serve --port 0` to serve the gallery, comparison and journal on one loopback origin. The command prints the selected URLs and serves only those artifact directories. Keep the process running during review. Ctrl+C stops that server. An occupied requested port causes it to select a free port.

## CI and tagged releases

Keep release notes in `CHANGELOG.md`, newest first. Each release heading must be exactly `## <tag>`, such as `## v1.0-beta.1`. Its notes continue until the next `## ` heading. Use `## Unreleased` for work that is not ready for a tag. It never supplies notes for a release. Missing, empty or duplicate tag sections fail validation.

Use one short paragraph for a simple release. For longer notes, group them under `### What's new`, `### What's changed` and `### Technical changes`, omitting empty sections. Keep new features and changed behavior user-facing. Put internal refactors, build changes and test changes under technical changes. Group minor fixes under bug fixes and improvements. The `v1.0-beta.1` entry contains only: "First public Kobo beta, using Qt5 on firmware 4.38 and later."

On a GitHub tag push, the first CI job renders the changelog entry and installation template with `tools/draft_release.py --notes-only`. Every build and packaging job depends on this check, so invalid notes stop the workflow before a build starts. Branch pushes on GitHub and manual runs skip the notes check. Rerunning a draft replaces its notes, including hand edits. Published releases remain protected.

Edit [release-installation.md](release-installation.md) to change the installation steps included in each GitHub release. The helper combines the tagged changelog entry, this Markdown template and the source-bundle information. It fills `{{package_links}}` with a Markdown list of the device ZIPs attached to that release, so `v1.0-beta.1` lists only Qt5. For individual links, use `{{qt5_url}}`, `{{qt6_url}}`, `{{source_url}}`, `{{checksums_url}}` or `{{build_info_url}}`. It also supports `{{tag}}`, `{{repository}}`, `{{release_url}}` and `{{download_url}}`. Unknown fields, malformed fields, empty templates and links to packages excluded from the release fail validation. Do not put `{{qt6_url}}` in a template used for the first beta.

Preview the complete GitHub release text without building packages, creating tags or contacting GitHub:

```sh
python3 tools/draft_release.py --notes-only --tag v1.0-beta.1 --repository nicoverbruggen/kobalt-oss
```

Review this preview before tagging. Keep installation edits in the template, because a draft rerun replaces the generated release text.

Native tests are disabled in GitHub Actions for the initial beta. Run the full suite locally before releasing. To enable the CI gate, set the repository Actions variable `KOBALT_NATIVE_TESTS` to `true`. This setting applies to branch pushes, tags and manual runs; it does not depend on a specific release tag. When disabled, device builds and release draft creation accept the skipped test job. When enabled, a failed test job blocks both.

The optional native job checks C++ formatting and runs the full suite with CREngine and Clang coverage through `sh tools/ci_native_tests.sh`. The container builds for the host CPU in Ubuntu 24.04; it does not cross-compile for ARM or run Nickel. Linux runs the socket-interface adapter that macOS substitutes. JUnit results, the CTest log and coverage metadata are separate `native-results` artifacts, including after a test failure. Tests run as the host user so GitHub can read their output. A formatting failure stops before compilation. GitHub caches verified dependency sources. The container engine must support mounting the checkout at `/source`; the helper honors `CONTAINER_ENGINE`, otherwise preferring Podman and then Docker. The first run downloads packages, fonts and dependency sources and builds Qt; the 90-minute job timeout is not an estimate. Keep the full suite enabled for local release checks.

GitHub Actions builds both Kobo targets on pushes to `main` and on `v*` tags. Tag pushes and manual runs also collect the complete source bundle. Successful tag pushes automatically create or update a draft after release-note validation, both Kobo builds and source collection pass. The draft job also requires the native tests to pass when enabled; intentionally skipped native tests do not block it. The tag sets the version compiled into the app and recorded in its asset manifest, for example `v1.0-beta.1`. Commit the release inputs, then create and push the version tag when ready. No version edit in `CMakeLists.txt` is required for a tagged release. An untagged checkout uses the root `VERSION` file. Keep that file at the current source version; it currently records `v1.0-beta.1`. CI never publishes the draft automatically. A rerun can replace attachments on a draft, but refuses to change a published release.

For `v1.0-beta.1`, the GitHub draft contains `Kobalt-qt5.zip`, `Kobalt-source.tar.gz`, `SHA256SUMS` and `BUILD-INFO.json`. The workflow still builds Qt5 and Qt6; the native test gate runs only when enabled, and the Qt6 package remains available as a CI artifact. The draft helper applies this rule to the exact tag, includes only the selected attachments in checksums and build metadata, and removes an old Qt6 attachment when rerunning an existing draft. Other tags include both device ZIPs. Published releases remain protected.

The ZIPs include the update package and installable assets. The source bundle includes tracked source, build recipes, patches, licenses, every archive in `dependencies/manifest.json`, Qt source, SDK source recipes, the pinned eBook Fonts source repository and the downloaded reading fonts. `SOURCE-BUNDLE.json` inside the archive lists every file and its SHA-256. The build records identify the source commit and SDK image. CI rejects dirty or mismatched inputs before creating a draft.

Use the attached source bundle when distributing the binaries. GitHub's automatic source ZIP and tarball contain only repository files and omit downloaded dependencies and assets. Review third-party notices when changing dependencies; collecting source does not itself resolve license compatibility questions.

To run the packaging steps individually:

```sh
python3 tools/ci_build.py --target qt5
python3 tools/ci_build.py --target qt6
python3 tools/release_sources.py
```

Outputs go to `build/release/`. These commands require a clean checkout. `--allow-dirty` is available for local validation, and draft creation rejects those outputs. `release_sources.py --offline` uses verified cached archives and fonts only. The SDK builds still need their base container images and operating-system packages. The source bundle is not a complete offline Docker image or host toolchain.

A manual GitHub workflow run builds both packages and collects the source bundle. Manual runs never create or update drafts, even when a tag is selected. Creating drafts uses the workflow's built-in GitHub token with `contents: write`; build jobs have read-only repository access. No Android signing key or external repository credential is needed for this Kobo workflow.

The Python build helpers resolve the version in this order: `KOBALT_RELEASE_VERSION`, the version in an extracted source bundle, an exact version tag on the checkout, then the root `VERSION` file. Multiple version tags on the same commit require an explicit `KOBALT_RELEASE_VERSION`. CI passes its triggering tag explicitly. The source archive records that version in `SOURCE-BUNDLE.json`, so rebuilding it does not require Git metadata. Direct CMake users must pass `-DKOBALT_VERSION=...` to override the fallback. Android uses the same displayed version resolver; its numeric version code remains a separate setting.

Beta tags such as `v1.0-beta.1` are supported. To verify packages before tagging, use a push to `main` or a manual workflow run and inspect the run's Artifacts section. Tag pushes create drafts automatically; review their notes and attachments before publishing.

CI pulls the public `ghcr.io/nicoverbruggen/kobuild:qt5-v0.1.0` and `ghcr.io/nicoverbruggen/kobuild:qt6-v0.1.0` SDK images. Both support AMD64 and ARM64 hosts and require no registry login. The SDK contains the compiler and Kobo Qt; Kobalt still builds its application dependencies and applies its own patches. A failed pull stops the job instead of starting a local SDK build. The build record retains the actual image ID. To use a published SDK locally, pass `--sdk-image ghcr.io/nicoverbruggen/kobuild:qt6-v0.1.0` to `tools/ci_build.py`; omit it to build the SDK from this repository.

GitHub requests a 30-minute timeout per device build. This is a time limit, not a build-time estimate. The job must reach GitHub for actions, dependency downloads and artifact uploads, and GHCR for toolchain images.

GitHub uses two compiler jobs without a container CPU cap.

The native test runner supplies the pinned Fraunces variable-font fixture described in the Tests section. For direct runs of individual test binaries, set `KOBALT_TEST_VARIABLE_FONTS` to the staged fixture directory or another directory of TTF or OTF files. These checks cover the supported weight range, independent thickness, previews and reopening. Direct runs without a variable font report a skip for these integration cases. No test font is added to release bundles.
