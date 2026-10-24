#!/bin/bash
# Offline release checks. Never modify the application's Git state.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
for script in platforms/ci/*.sh platforms/android/*.sh platforms/kobo/*.sh platforms/sdk-image.sh; do bash -n "$script"; done
mkdir -p build
scratch=$(mktemp -d "$root/build/release-check.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/version.cmake" <<'CMAKE'
include("$ENV{KOBALT_TEST_ROOT}/platforms/release-version.cmake")
kobalt_release_version(actual v1.0-beta.4)
if(NOT actual STREQUAL "$ENV{KOBALT_TEST_EXPECTED}")
    message(FATAL_ERROR "Unexpected version: ${actual}")
endif()
CMAKE
export KOBALT_TEST_ROOT=$root
for version in v1.0-beta.4 v1.0 v1.2.3; do
    KOBALT_RELEASE_VERSION=$version KOBALT_TEST_EXPECTED=$version cmake -P "$scratch/version.cmake"
done
for version in '../bad' '1.0' 'v1.0;bad' 'v1.0/evil'; do
    if KOBALT_RELEASE_VERSION=$version cmake -P "$scratch/version.cmake" > "$scratch/error" 2>&1; then echo 'Accepted invalid version' >&2; exit 1; fi
done
# Source manifests must have disjoint names and complete checksums.
jq -es '([.[0]|keys[]] + [.[1]|keys[]]) as $keys | ($keys|length)==($keys|unique|length) and (all(.[]|to_entries[]; (.key|test("^[a-z0-9-]+$")) and (.value.url|startswith("https://")) and (.value.sha256|test("^[a-f0-9]{64}$"))))' dependencies/manifest.json dependencies/release-sources.json > /dev/null
# Fail closed before consulting tools or producing an APK when secrets are absent.
if env -u KOBALT_ANDROID_KEYSTORE bash platforms/android/sign-apk.sh missing.apk "$scratch/output.apk" > "$scratch/error" 2>&1; then echo 'Signing accepted a missing key' >&2; exit 1; fi
[[ ! -e "$scratch/output.apk" ]]
echo 'PASS: release helper syntax, versions, source pins and missing signing key'
# Verify that the staged runtime manifest receives the tag version.
mkdir "$scratch/assets-project"
cat > "$scratch/assets-project/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.22)
project(ReleaseAssets NONE)
add_subdirectory("$ENV{KOBALT_TEST_ROOT}/assets" assets)
file(READ "${KOBALT_ASSETS}/manifest.json" manifest)
string(JSON actual GET "${manifest}" version)
if(NOT actual STREQUAL "v9.8.7-beta.6")
    message(FATAL_ERROR "Runtime manifest did not receive the release version")
endif()
CMAKE
KOBALT_RELEASE_VERSION=v9.8.7-beta.6 cmake -S "$scratch/assets-project" -B "$scratch/assets-build" > "$scratch/configure.log"
# CI runs on Linux with GNU tar, as do the package builders.
if tar --version 2>/dev/null | head -n 1 | grep -q 'GNU tar'; then
    for target in qt5 qt6; do
        if [[ $target == qt5 ]]; then member=usr/local/Kobo/imageformats/libkobalt.so; installer=KoboRoot.tgz
        else member=imageformats/libkobalt.so; installer=Kobo.tgz; fi
        mkdir -p "$scratch/package/$(dirname "$member")" "$scratch/zip/.kobo"
        printf 'fixture\n' > "$scratch/package/$member"
        for owner in 0 501; do
            tar --format=gnu --owner="$owner" --group="$owner" --mode=0755 -czf "$scratch/zip/.kobo/$installer" -C "$scratch/package" "$member"
            (cd "$scratch/zip" && cmake -E tar cf "$scratch/package-$target.zip" --format=zip .kobo)
            if bash platforms/kobo/check-package.sh "$scratch/package-$target.zip" "$target" > "$scratch/package.log" 2>&1; then
                [[ $owner == 0 ]] || { echo 'Accepted a non-root package' >&2; exit 1; }
            else
                [[ $owner != 0 ]] || { cat "$scratch/package.log"; exit 1; }
            fi
        done
        rm "$scratch/zip/.kobo/$installer"
    done
else
    echo 'SKIP: archive ownership regression needs GNU tar (runs in Linux CI)'
fi
echo 'PASS: runtime tag version and package ownership regression'
