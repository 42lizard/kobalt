#!/bin/bash
# Tag jobs assemble a draft only after every package and source job succeeds.
set -euo pipefail
version=${KOBALT_RELEASE_VERSION:?}
[[ ${GITHUB_REF:-} == "refs/tags/$version" ]] || { echo 'Draft releases require the matching tag push' >&2; exit 2; }
for name in Kobalt-qt5.zip Kobalt-qt6.zip Kobalt-android-arm64.apk Kobalt-sources.tar.gz; do
    [[ -s build/release/$name ]] || { echo "Missing release artifact: $name" >&2; exit 1; }
done
(cd build/release && shasum -a 256 Kobalt-* build-*.json > SHA256SUMS)
if gh release view "$version" --json isDraft --jq .isDraft > build/release/draft-state.txt 2>build/release/draft-query.log; then
    [[ $(cat build/release/draft-state.txt) == true ]] || { echo 'Refusing to replace a published release' >&2; exit 1; }
else
    # A failed lookup may also be authentication or network failure. Creation
    # checks the tag server-side and must succeed before any upload occurs.
    prerelease=()
    [[ $version != *-* ]] || prerelease=(--prerelease)
    gh release create "$version" --verify-tag --draft "${prerelease[@]}" --title "Kobalt $version" --generate-notes
fi
rm -f build/release/draft-state.txt build/release/draft-query.log
gh release upload "$version" build/release/Kobalt-* build/release/build-*.json build/release/SHA256SUMS --clobber
