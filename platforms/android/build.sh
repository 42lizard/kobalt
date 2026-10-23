#!/bin/bash
set -euo pipefail
if [[ $# -ne 0 ]]; then echo "Usage: bash platforms/android/build.sh" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/../.." && pwd)
jobs=${JOBS:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "JOBS must be positive" >&2; exit 2; }
# Prepared multi-architecture Android SDK; application dependencies build locally.
image=${KOBALT_ANDROID_IMAGE:-ghcr.io/nicoverbruggen/kobuild@sha256:013a0509703e6499529a438659ea3a8d0a18d3eedaf24fdfea548840878008b3}
engine=${CONTAINER_ENGINE:-docker}
read -r arch sdk_id < <("$engine" image inspect "$image" --format '{{.Architecture}} {{.Id}}')
mkdir -p "$root/build/android-$arch" "$root/build/gradle"
commit=$(git -C "$root" rev-parse --short HEAD)
modified=OFF
if [[ -n $(git -C "$root" status --porcelain) ]]; then modified=ON; fi
"$engine" run --rm --pull=never --user "$(id -u):$(id -g)" \
    -e HOME=/tmp -e GRADLE_USER_HOME=/gradle -e ANDROID_USER_HOME="/work/build/android-$arch/user-home" -e KOBALT_ANDROID_HOST_ARCH="$arch" \
    --volume "$root:/work" \
    --volume "$root/build/gradle:/gradle" \
    --workdir /work/platforms/android "$image" bash -euc '
        exec 9>"/work/build/android-$KOBALT_ANDROID_HOST_ARCH/build.lock"
        flock -n 9 || { echo "Android output is in use by another build" >&2; exit 1; }
        started=$SECONDS
        cmake --preset android \
            -DKOBALT_SDK_ID="$4" -DKOBALT_DEPENDENCY_JOBS="$1" -DKOBALT_COMMIT="$2" -DKOBALT_COMMIT_MODIFIED="$3"
        echo "Configure: $((SECONDS - started)) seconds"
        started=$SECONDS
        cmake --build --preset android --parallel "$1"
        echo "Build and APK: $((SECONDS - started)) seconds"
    ' kobalt-android "$jobs" "$commit" "$modified" "$sdk_id"
