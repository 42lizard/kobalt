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
case "$target" in
    # Qt5 v0.2.3, including the native cross-strip fix.
    qt5) image=ghcr.io/nicoverbruggen/kobuild@sha256:c5c2c0e4f1ebd5268ea631c89354976ef090fc571e162c294644677c5aedad05 ;;
    # Qt6 v0.2.2, including Sensors. Pin the published multi-architecture image.
    qt6) image=ghcr.io/nicoverbruggen/kobuild@sha256:b939301323e1bed0bf6b0bee43b8791db2efdf01231d4245c1f972d57433644a ;;
esac
engine=${CONTAINER_ENGINE:-docker}
sdk_id=$("$engine" image inspect "$image" --format '{{.Id}}')
commit=$(git -C "$root" rev-parse --short HEAD)
modified=OFF
if [[ -n $(git -C "$root" status --porcelain) ]]; then modified=ON; fi
"$engine" run --rm --pull=never --user "$(id -u):$(id -g)" -e HOME=/tmp \
    --volume "$root:/work" \
    --workdir /work/platforms/kobo "$image" bash -euc '
        started=$SECONDS
        cmake --preset "$1" \
            -DKOBALT_SDK_ID="$5" -DKOBALT_DEPENDENCY_JOBS="$2" -DKOBALT_COMMIT="$3" -DKOBALT_COMMIT_MODIFIED="$4"
        echo "Configure: $((SECONDS - started)) seconds"
        started=$SECONDS
        cmake --build --preset "$1" --parallel "$2"
        echo "Build and package: $((SECONDS - started)) seconds"
    ' kobalt-build "$target" "$jobs" "$commit" "$modified" "$sdk_id"
