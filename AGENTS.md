# Working on Kobalt public source

Read [README.md](README.md) before changing code. Follow [Building Kobalt](docs/development.md) for build commands, tests and source boundaries. [Shared reader controls](docs/development.md#shared-reader-controls) is the canonical component guidance. Keep reader behavior documentation current in [Reader behavior](docs/reader.md).

## README edits

The README must be edited manually.

## Release version

When preparing a release, update the root `VERSION` file to the release tag, such as `v1.0-beta.2`, in the same change as its `CHANGELOG.md` entry. Check that both match before tagging. Untagged builds use `VERSION`; tagged CI builds use the triggering tag.

## Acknowledgements

Update [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) when adding or replacing a dependency or asset. Include upstream links and the original authors. Credit Nico Verbruggen for his projects and adaptations, while keeping the original upstream authors credited. Keep maintenance instructions in this file, not in the acknowledgements document.
