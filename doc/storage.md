# Storage compatibility

Kobalt separates saved reading data from disposable rendering caches. Application releases do not rename or reset these files. Hosts choose the storage directory; `storage::Paths` owns the shared filenames. JSON uses UTF-8. Writes use `QSaveFile` to replace a complete file atomically.

## Saved reading data

`reader-settings.json` holds global preferences. Book state uses a host-selected prefix followed by the SHA-256 of the UTF-8 book identity and `.json`. Its `schema` is `1`. The `book` and `fingerprint` fields identify the publication. It stores the canonical reading position, book-specific text preferences and native progress reconciliation fields. Globally owned layout preferences remain in global settings. Marking a book unread resets its position and book-specific preferences, but keeps annotations.

Bookmarks and annotations share `annotations/<book-key>/`. `book.json` has `version: 1`, `identity`, `title` and `fingerprint`. Book keys come from `BookmarkStore::bookKey`: native book identities remain stable across downloads; sideloaded books use their content fingerprint. A changed file can adopt annotations from the previous record of the same path.

`bookmarks.json` has `version: 1` and a `bookmarks` array. Each bookmark stores `position`, `chapter`, `excerpt` and `created`. Positions are canonical CREngine XPointers, not page numbers. Dates are UTC ISO 8601 strings.

`highlights.json` has `version: 1` and a `highlights` array. Each entry stores `id`, `start`, `end`, `text`, `chapter` and `created`. Optional `color` defaults to `gray`; optional `style` defaults to `shaded`. Supported styles are `shaded`, `underline` and `dotted`. Unknown color and style tokens survive edits and use a display fallback. Optional `sentence_start` and `sentence_end` objects preserve endpoint provenance through `enabled` and `chosen_start` or `chosen_end`.

An optional `note` string holds plain text for a future note editor. Missing notes mean empty text. Notes preserve Unicode and line breaks and are limited to 65,536 UTF-16 code units. Passage edits and appearance changes preserve the note. Beta 4 does not expose a note editor. Adding this optional field does not change format version 1.

Each book supports up to 2,000 bookmarks and 2,000 annotations. Bookmark files are limited to 1 MiB and highlight files to 8 MiB. An annotation excerpt is limited to 262,144 UTF-16 code units. Malformed or unsupported annotation files block subsequent saves rather than replacing the original data with an empty list.

Add compatible optional fields within version 1. A change to required fields, their meaning or location needs an explicit format version and migration. Do not tie data versions to release names. This is the current format contract, not a promise that older builds preserve arbitrary unknown fields. Book-state readers currently accept known fields without enforcing a future-schema write barrier; downgrade safety for a future incompatible schema needs implementation before that schema ships.

## Disposable caches

`.cache/` belongs exclusively to CREngine. Never put settings or annotations there: the engine removes files it does not recognize. It stores parsed books in an engine-specific binary format, with a 128 MiB budget. It is not an interchange or backup format.

The cache checksum includes an explicit Kobalt parser format version and the document DOM version. Kobo and Android build policies include the pinned rendering dependency manifest, dependency recipe and patches. App version strings and UI source changes do not invalidate parsed books. Bump the format version in `documentCacheChecksum` when a parser or preprocessing change makes existing cached documents unsafe to reuse. Density, user CSS and bold-reading variants retain their existing policy checks. Changing typography can still require pagination without reparsing the publication. A dependency or format change can intentionally cause one new parse.

`reopen-layout.json` is a separate bounded, version-1 cache of publication inspection results. Its policy follows the build's layout policy. It can be discarded without losing reading data. Bump its format version when inspection verdict rules change incompatibly, independently of the parsed-document format.
