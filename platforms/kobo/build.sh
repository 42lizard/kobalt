#!/bin/bash
set -euo pipefail
if [[ $# -ne 1 || ! "$1" =~ ^qt[56]$ ]]; then
    echo "Usage: bash platforms/kobo/build.sh qt5|qt6" >&2
    exit 2
fi
target=$1
root=$(cd "$(dirname "$0")/../.." && pwd)
jobs=${JOBS:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "JOBS must be positive" >&2; exit 2; }
image=$(sh "$root/platforms/sdk-image.sh" "$target")
engine=${CONTAINER_ENGINE:-docker}
sdk_id=$("$engine" image inspect "$image" --format '{{.Id}}')
commit=$(git -C "$root" rev-parse --short HEAD)
modified=OFF
if [[ -n $(git -C "$root" status --porcelain) ]]; then modified=ON; fi
"$engine" run --rm --pull=never --user "$(id -u):$(id -g)" -e HOME=/tmp -e KOBALT_RELEASE_VERSION \
    --volume "$root:/work" \
    --workdir /work/platforms/kobo "$image" bash -euc '
        started=$SECONDS
        cmake --preset "$1" \
            -DKOBALT_SDK_ID="$5" -DKOBALT_DEPENDENCY_JOBS="$2" -DKOBALT_COMMIT="$3" -DKOBALT_COMMIT_MODIFIED="$4"
        echo "Configure: $((SECONDS - started)) seconds"
        started=$SECONDS
        cmake --build --preset "$1" --parallel "$2"
        cmake --build --preset "$1" --target check-package
        echo "Build and package: $((SECONDS - started)) seconds"
    ' kobalt-build "$target" "$jobs" "$commit" "$modified" "$sdk_id"
