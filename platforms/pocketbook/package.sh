#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
binary=$1 assets=$2 output=$3
stage=$(mktemp -d "$root/build/pocketbook/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage"/{applications,system/bin,system/kobalt/bin,system/kobalt/notices}
cp "$binary" "$stage/system/kobalt/bin/kobalt"
cp -R "$assets" "$stage/system/kobalt/assets"
cp "$root"/assets/notices/* "$root/LICENSE" "$stage/system/kobalt/notices/"
cp "$root/platforms/pocketbook/launcher.sh" "$stage/applications/kobalt.app"
printf '#!/bin/sh\nexec /mnt/ext1/applications/kobalt.app "$@"\n' > "$stage/system/bin/kobalt.app"
chmod 755 "$stage/applications/kobalt.app" "$stage/system/bin/kobalt.app" "$stage/system/kobalt/bin/kobalt"
(cd "$stage" && zip -X -q -r "$stage/package.zip" applications system)
mv "$stage/package.zip" "$output"
