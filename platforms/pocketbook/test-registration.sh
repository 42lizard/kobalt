#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage"/{applications,system/bin,system/config}
touch "$stage/applications/kobalt.app" "$stage/system/bin/kobalt.app"
printf 'pdf:unchanged\nepub:@EPUB_file:1:custom.app,stock.app:ICON_EPUB\n' > "$stage/original"
cp "$stage/original" "$stage/system/config/extensions.cfg"
bash "$root/platforms/pocketbook/register-reader.sh" "$stage"
grep -Fx 'epub:@EPUB_file:1:custom.app,stock.app,kobalt.app:ICON_EPUB' "$stage/system/config/extensions.cfg"
cmp "$stage/original" "$stage/system/config/extensions.cfg.before-kobalt"
cp "$stage/system/config/extensions.cfg" "$stage/registered"
bash "$root/platforms/pocketbook/register-reader.sh" "$stage"
cmp "$stage/registered" "$stage/system/config/extensions.cfg"
cat "$stage/original" "$stage/original" > "$stage/duplicate"
cp "$stage/duplicate" "$stage/system/config/extensions.cfg"
if bash "$root/platforms/pocketbook/register-reader.sh" "$stage"; then exit 1; fi
cmp "$stage/duplicate" "$stage/system/config/extensions.cfg"
