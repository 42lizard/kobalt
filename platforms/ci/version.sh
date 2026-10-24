#!/bin/bash
# Print the tag-derived version, or the checked-in version for branch builds.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
version=${KOBALT_RELEASE_VERSION:-$(jq -er '.version' "$root/assets/manifest.json")}
[[ $version =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]] || { echo 'Invalid release version' >&2; exit 2; }
if [[ ${GITHUB_REF:-} == refs/tags/* ]]; then
    [[ ${GITHUB_REF#refs/tags/} == "$version" ]] || { echo 'Version does not match the triggering tag' >&2; exit 2; }
    [[ $(git -C "$root" rev-parse "refs/tags/$version^{commit}") == $(git -C "$root" rev-parse HEAD) ]] || { echo 'Tag does not identify this checkout' >&2; exit 2; }
fi
printf '%s\n' "$version"
