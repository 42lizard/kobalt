#!/usr/bin/env bash
# Register an additional EPUB reader while preserving the existing default.
set -euo pipefail
[[ $# == 1 ]] || { echo "Usage: $0 MOUNTED_POCKETBOOK" >&2; exit 64; }
mount=$1
for path in applications/kobalt.app system/bin/kobalt.app; do test -f "$mount/$path"; done
config="$mount/system/config/extensions.cfg"
[[ ! -L $config ]]
mkdir -p "$(dirname "$config")"
before=$(mktemp)
after=$(mktemp "$(dirname "$config")/.extensions-kobalt.XXXXXX")
trap 'rm -f "$before" "$after"' EXIT
if [[ -f $config ]]; then cp "$config" "$before"; fi
awk '
BEGIN { count=0 }
/^epub:/ {
    count++
    line=$0; sub(/\r$/, "", line)
    n=split(line, fields, ":")
    if(n!=5 || fields[4]=="" || fields[4] ~ /^,|,$|,,/) { bad=1; exit 1 }
    if(fields[4] !~ /(^|,)kobalt\.app(,|$)/) fields[4]=fields[4] ",kobalt.app"
    print fields[1] ":" fields[2] ":" fields[3] ":" fields[4] ":" fields[5]
    next
}
{ print }
END {
    if(bad || count>1) exit 1
    if(count==0) print "epub:@EPUB_file:1:eink-reader_with_blink.app,eink-reader_with_epub2.app,kobalt.app:ICON_EPUB"
}' "$before" > "$after"
if cmp -s "$before" "$after"; then exit; fi
if [[ -f $config ]]; then
    cmp -s "$config" "$before"
    if [[ ! -e $config.before-kobalt ]]; then cp -p "$config" "$config.before-kobalt"; fi
    chmod --reference="$config" "$after"
else
    [[ ! -e $config ]]
    chmod 644 "$after"
fi
mv "$after" "$config"
echo 'Kobalt is available under Open with; the existing default is preserved.'
