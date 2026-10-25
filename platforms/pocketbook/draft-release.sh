#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
tag=$1 repository=$2
[[ $tag == v*pb && $(cat VERSION) == "$tag" ]]
[[ $(git rev-parse HEAD) == "$(git rev-parse "$tag^{commit}")" ]]
cd build/release
jq -e --arg tag "$tag" --arg commit "$(git rev-parse HEAD)" '.version == $tag and .commit == $commit and .runtime_tests == true and .dirty == false' pocketbook.json > /dev/null
echo "$(jq -r .package_sha256 pocketbook.json)  Kobalt-PB743K3.zip" | sha256sum -c -
for asset in Kobalt-PB743K3.zip Kobalt-sources.tar.gz; do test -s "$asset"; done
cmp <(tar -xOf Kobalt-sources.tar.gz "Kobalt-$tag-sources/SOURCE.txt") <(printf '%s\n' "Application commit: $(git rev-parse HEAD)" "Version: $tag")
sha256sum Kobalt-PB743K3.zip Kobalt-sources.tar.gz pocketbook.json > SHA256SUMS
awk -v tag="$tag" '$0 == "## "tag {found=1;next} found && /^## / {exit} found {print}' "$root/doc/CHANGELOG.md" > notes.md
test -s notes.md
if gh release view "$tag" --repo "$repository" --json isDraft > release.json 2>/dev/null; then
    jq -e '.isDraft == true' release.json > /dev/null
    gh release edit "$tag" --repo "$repository" --prerelease --title "Kobalt $tag — PocketBook" --notes-file notes.md
else
    gh release create "$tag" --repo "$repository" --verify-tag --draft --prerelease --title "Kobalt $tag — PocketBook" --notes-file notes.md
fi
gh release upload "$tag" --repo "$repository" --clobber Kobalt-PB743K3.zip Kobalt-sources.tar.gz pocketbook.json SHA256SUMS
rm -f notes.md release.json
