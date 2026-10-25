#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
host="$root/build/pocketbook/host-beta4"
arm="$root/build/pocketbook/arm-beta4"
firmware="$root/build/pocketbook/firmware"
fixtures=$(mktemp -d "$root/build/pocketbook/fixtures.XXXXXX")
trap 'rm -rf "$fixtures"' EXIT
"$host/pocketbook-fixtures" "$fixtures"
runtime="$fixtures/runtime"
mkdir "$runtime"
export KOBALT_LOOKUP_FIXTURE="$fixtures/lookup.epub" KOBALT_DICTIONARY_FIXTURE="$fixtures/dictionary-fixture.zip"
export QT_QPA_PLATFORM=offscreen QT_STYLE_OVERRIDE=Fusion QT_LOGGING_RULES='*.debug=false'
mkdir -p build/pocketbook/host-captures build/pocketbook/arm-captures
KOBALT_TEST_CAPTURE="$root/build/pocketbook/host-captures/reader.png" timeout 240 "$host/pocketbook-test"
arm-linux-gnueabi-gcc -shared -fPIC tests/pocketbook/inkview_stub.c -o "$runtime/libinkview-test.so"
run_arm() {
    timeout 240 qemu-arm -L "$firmware/rootfs" \
        -E "LD_LIBRARY_PATH=$runtime:$firmware/runtime/lib:$firmware/rootfs/lib:$firmware/rootfs/usr/lib" \
        -E "LD_PRELOAD=$runtime/libinkview-test.so" \
        -E "QT_PLUGIN_PATH=$firmware/ebrmain/plugins" "$@"
}
export KOBALT_TEST_CAPTURE="$root/build/pocketbook/arm-captures/reader.png"
# Disabling networking demonstrates that saved books and dictionaries work offline.
unshare --net true
export -f run_arm
export arm firmware runtime
unshare --net bash -c 'run_arm "$arm/pocketbook-test"'
run_arm "$arm/kobalt-profile-storage-tests"
KOBALT_TEST_EMULATOR=/usr/bin/qemu-arm QEMU_LD_PREFIX="$firmware/rootfs" run_arm "$arm/kobalt-annotation-database-tests"
