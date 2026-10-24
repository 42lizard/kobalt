#!/bin/bash
# Keep the audited Android runtime and its notices tied to the prepared SDK.
set -euo pipefail
[[ $# == 1 ]] || { echo 'Expected an APK path' >&2; exit 2; }
root=$(cd "$(dirname "$0")/../.." && pwd)
manifest="$root/dependencies/android-runtime.json"
[[ $(sh "$root/platforms/sdk-image.sh" android) == $(jq -r .sdk_image "$manifest") ]] || {
    echo 'Android SDK changed: review runtime notices and source pins' >&2; exit 1;
}
while IFS=$'\t' read -r name expected; do
    actual=$(shasum -a 256 "$root/assets/notices/$name" | awk '{print $1}')
    [[ $actual == "$expected" ]] || { echo "Notice checksum mismatch: $name" >&2; exit 1; }
done < <(jq -r '.notices | to_entries[] | [.key,.value] | @tsv' "$manifest")
for module in qtbase qtsensors; do
    revision=$(jq -r ".${module}_revision" "$manifest")
    [[ $(jq -r --arg name "$module-android" '.[$name].url' "$root/dependencies/release-sources.json") == "https://codeload.github.com/qt/$module/tar.gz/$revision" ]] || {
        echo "Qt source pin mismatch: $module" >&2; exit 1;
    }
done
actual=$(unzip -Z1 "$1" | sed -n 's|^lib/arm64-v8a/\([^/]*\.so\)$|\1|p' | LC_ALL=C sort)
expected=$(jq -r '.native_libraries[]' "$manifest" | LC_ALL=C sort)
[[ $actual == "$expected" ]] || { echo 'APK runtime changed: review notices and source inputs' >&2; exit 1; }
echo 'PASS: Android runtime inventory, notices and Qt source pins'
