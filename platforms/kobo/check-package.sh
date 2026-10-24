#!/bin/sh
# Run in the prepared Kobo SDK. Check the installed library's tar metadata.
set -eu
[ "$#" -eq 2 ] || { echo 'Usage: check-package.sh PACKAGE qt5|qt6' >&2; exit 2; }
package=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
case "$2" in
    qt5) installer=KoboRoot.tgz; member=usr/local/Kobo/imageformats/libkobalt.so ;;
    qt6) installer=Kobo.tgz; member=imageformats/libkobalt.so ;;
    *) echo 'Expected qt5 or qt6' >&2; exit 2 ;;
esac
scratch=$(mktemp -d "${package%/*}/package-check.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
(cd "$scratch" && cmake -E tar xf "$package" ".kobo/$installer")
mv "$scratch/.kobo/$installer" "$scratch/library.tgz"
[ "$(tar -tzf "$scratch/library.tgz")" = "$member" ] || { echo 'Unexpected system archive members' >&2; exit 1; }
metadata=$(tar --numeric-owner -tvzf "$scratch/library.tgz" | awk '{print $1 " " $2}')
[ "$metadata" = '-rwxr-xr-x 0/0' ] || { echo "Invalid library permissions or ownership: $metadata" >&2; exit 1; }
echo "PASS: $installer contains only $member, mode 0755, owner 0:0"
