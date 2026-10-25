#!/usr/bin/env bash
# Pinned official firmware is a local link/test input; it is never redistributed.
set -euo pipefail
sevenzip=$(command -v 7zz || command -v 7z)
root=$(cd "$(dirname "$0")/../.." && pwd)
source=${1:-$root/build/firmware/SWUPDATE.BIN}
expected=0ca8343a41e2497e0d038ed8f105be2f4d7f60ce9747e8ada24b164c5d5fd444
mkdir -p "$root/build/firmware"
if [[ ! -f $source ]]; then
    archive="$root/build/firmware/PB743K3.zip"
    curl -fL --retry 3 https://download.pocketbook-int.com/fw/Yitoa/743k3/user/20260105/sw_20260105_U743k3_6.10.2854_user.zip -o "$archive.part"
    echo "769fb0ed98446fb134f6baf177504e1ab2e9552b2944f2a4ec55fab059d305cf  $archive.part" | sha256sum -c -
    mv "$archive.part" "$archive"
    "$sevenzip" e -y "$archive" "-o$(dirname "$source")" SWUPDATE.BIN
fi
echo "$expected  $source" | sha256sum -c -
output="$root/build/pocketbook/firmware"
mkdir -p "$output"
cd "$output"
recipe=$(sha256sum "$root/platforms/pocketbook/firmware.sh" | cut -d' ' -f1)
if [[ -f recipe.sha && $(cat recipe.sha) == "$recipe" ]] && sha256sum --quiet -c inputs.sha256; then
    exit 0
fi
rm -f recipe.sha
inflate() {
    # gzip reports trailing bytes after this embedded member; 7-Zip validates the decoded filesystem.
    set +e
    dd if="$source" iflag=skip_bytes skip="$1" bs=1M status=none | gzip -dc > "$2"
    status=("${PIPESTATUS[@]}")
    set -e
    [[ ${status[1]} == 0 || ${status[1]} == 2 ]]
    [[ -s $2 ]]
    "$sevenzip" l "$2" > /dev/null
}
inflate 15336448 rootfs.raw
inflate 20579328 ebrmain.raw
"$sevenzip" e -y ebrmain.raw -oebrmain cramfs.img > /dev/null
"$sevenzip" x -y rootfs.raw -orootfs 'lib/*' 'usr/lib/*' > /dev/null
"$sevenzip" x -y ebrmain/cramfs.img -oruntime 'lib/*' > /dev/null
"$sevenzip" x -y ebrmain.raw -oebrmain 'plugins/platforms/libqoffscreen.so' 'plugins/tls/*' 'plugins/sqldrivers/libqsqlite.so' > /dev/null
mkdir -p rootfs/usr/lib
find rootfs/lib rootfs/usr/lib runtime/lib ebrmain/plugins -type f -print0 | sort -z | xargs -0 sha256sum > inputs.sha256
printf '%s\n' "$recipe" > recipe.sha
