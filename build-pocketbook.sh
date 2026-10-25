#!/usr/bin/env bash
# Build only PocketBook; SDK production and application CMake remain separate.
set -euo pipefail
root=$(cd "$(dirname "$0")" && pwd)
cd "$root"
inside=false
if [[ ${1:-} == --inside ]]; then inside=true; shift; fi
tests=false
firmware=""
jobs=${JOBS:-4}
while [[ $# -gt 0 ]]; do
    case $1 in
        --test) tests=true; shift ;;
        --firmware) firmware=${2:?Expected firmware path}; shift 2 ;;
        --jobs) jobs=${2:?Expected compiler job count}; shift 2 ;;
        *) echo "Usage: $0 [--test] [--firmware SWUPDATE.BIN] [--jobs N]" >&2; exit 64 ;;
    esac
done
[[ $jobs =~ ^[1-9][0-9]*$ ]]
if $inside; then
    commit=$(git rev-parse HEAD)
    dirty=false
    if [[ -n $(git status --porcelain) ]]; then dirty=true; fi
    source_hash=$(git diff HEAD --binary | sha256sum)
    release_version=$(bash platforms/ci/version.sh)
    test "$release_version" = "$(cat VERSION)"
    if [[ -n ${KOBALT_RELEASE_VERSION:-} ]] && $dirty; then echo "Tagged builds require clean sources" >&2; exit 1; fi
    cd platforms/desktop
    cmake --preset pocketbook-host -DCMAKE_TOOLCHAIN_FILE=
    cmake --build --preset pocketbook-host-tests --parallel "$jobs" --target kobalt-tests pocketbook-test pocketbook-fixtures
    ctest --preset pocketbook-host
    cd ../..
    bash platforms/pocketbook/firmware.sh "${firmware:-$root/build/firmware/SWUPDATE.BIN}"
    cd platforms/desktop
    cmake --preset pocketbook
    cmake --build --preset pocketbook-tests --parallel "$jobs" --target Kobalt pocketbook-test kobalt-profile-storage-tests kobalt-annotation-database-tests pocketbook-package
    cd ../..
    bash platforms/pocketbook/check-firmware.sh build/pocketbook/arm-beta4/Kobalt build/pocketbook/firmware/runtime/lib build/pocketbook/firmware/rootfs/lib build/pocketbook/firmware/rootfs/usr/lib
    if $tests; then bash platforms/pocketbook/test.sh; fi
    mkdir -p build/pocketbook
    [[ $(git rev-parse HEAD) == "$commit" && $(git diff HEAD --binary | sha256sum) == "$source_hash" ]]
    if ! $dirty; then [[ -z $(git status --porcelain) ]]; fi
    jq -n --argjson dirty "$dirty" --arg package_sha256 "$(sha256sum build/pocketbook/Kobalt-PB743K3.zip | cut -d' ' -f1)" --arg version "$(cat VERSION)" --arg commit "$commit" --arg upstream "435e922" --arg firmware "0ca8343a41e2497e0d038ed8f105be2f4d7f60ce9747e8ada24b164c5d5fd444" --argjson runtime_tests "$tests" '{version:$version,commit:$commit,dirty:$dirty,package_sha256:$package_sha256,upstream:$upstream,firmware_sha256:$firmware,runtime_tests:$runtime_tests}' > build/pocketbook/pocketbook.json
    exit
fi
engine=${CONTAINER_ENGINE:-docker}
image=${KOBALT_POCKETBOOK_IMAGE:-kobalt-pocketbook-sdk:local}
"$engine" build -t "$image" -f platforms/pocketbook/sdk/Dockerfile .
# Linked worktrees need their own Git common directory at its absolute location.
common=$(git rev-parse --path-format=absolute --git-common-dir)
arguments=(--jobs "$jobs")
if $tests; then arguments+=(--test); fi
mounts=()
if [[ -n $firmware ]]; then
    firmware=$(realpath "$firmware")
    test -f "$firmware"
    mounts=(-v "$firmware:/input/SWUPDATE.BIN:ro")
    arguments+=(--firmware /input/SWUPDATE.BIN)
fi
"$engine" run --rm --cap-add SYS_ADMIN "${mounts[@]}" \
    -v "$root:/work" -v "$common:$common:ro" -w /work \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/work \
    -e KOBALT_RELEASE_VERSION "${image}" bash build-pocketbook.sh --inside "${arguments[@]}"
