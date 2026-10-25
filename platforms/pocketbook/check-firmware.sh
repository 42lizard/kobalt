#!/usr/bin/env bash
set -euo pipefail
binary=$1; shift
readelf=arm-linux-gnueabi-readelf
$readelf -h "$binary" | grep -q 'soft-float ABI'
$readelf -h "$binary" | grep -q 'Machine:.*ARM'
$readelf -l "$binary" | grep -q '/lib/ld-linux.so.3'
if $readelf -d "$binary" | grep -Eq 'RPATH|RUNPATH'; then echo 'Embedded build path' >&2; exit 1; fi
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
: > "$stage/exports"; : > "$stage/libraries"
while IFS= read -r -d '' library; do
    if [[ $(head -c4 "$library") != $'\x7fELF' ]]; then continue; fi
    $readelf -d "$library" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' >> "$stage/libraries"
    $readelf -W --dyn-syms "$library" | awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") {print $8; if ($8 ~ /@@/) {sub(/@@.*/, "", $8); print $8}}' >> "$stage/exports"
done < <(find "$@" -maxdepth 1 -type f -print0)
sed -i 's/@@/@/g' "$stage/exports"
sort -u -o "$stage/exports" "$stage/exports"
sort -u -o "$stage/libraries" "$stage/libraries"
$readelf -d "$binary" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p' | sort -u > "$stage/needed"
$readelf -W --dyn-syms "$binary" | awk '$7 == "UND" && $5 == "GLOBAL" {print $8}' | sort -u > "$stage/imports"
comm -23 "$stage/needed" "$stage/libraries" > "$stage/missing"
comm -23 "$stage/imports" "$stage/exports" >> "$stage/missing"
if [[ -s $stage/missing ]]; then cat "$stage/missing" >&2; exit 1; fi
printf 'Firmware ABI verified: %s libraries, %s imports; ARM softfp, correct loader, no rpath.\n' "$(wc -l < "$stage/needed")" "$(wc -l < "$stage/imports")"
