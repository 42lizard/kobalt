#!/bin/sh
# Build both Kobo Qt targets through this repository's container workflow.
set -eu
root=$(CDPATH= cd "$(dirname "$0")" && pwd)
jobs=2
mini=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --jobs) [ "$#" -ge 2 ] || { echo "--jobs needs a number" >&2; exit 2; }; jobs=$2; shift 2 ;;
        --mini) mini=true; shift ;;
        --help|-h) echo "Usage: ./build-kobo.sh [--jobs NUMBER] [--mini]"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
case "$jobs" in ''|*[!0-9]*) echo "--jobs must be a positive integer" >&2; exit 2 ;; esac
[ "$jobs" -gt 0 ] || { echo "--jobs must be a positive integer" >&2; exit 2; }
export CONTAINER_ENGINE=${CONTAINER_ENGINE:-docker}
clean_commit() {
    [ "$(git -C "$1" rev-parse --show-toplevel)" = "$1" ] && [ -z "$(git -C "$1" status --porcelain)" ] || {
        echo "Checkout must be clean and committed: $1" >&2; exit 1;
    }
    git -C "$1" rev-parse HEAD
}
if [ "$mini" = false ]; then commit=$(clean_commit "$root"); fi
for target in qt5 qt6; do
    set --
    if [ "$mini" = true ]; then set -- --mini --allow-dirty; fi
    python3 "$root/tools/ci_build.py" --target "$target" --jobs "$jobs" \
        --sdk-image "ghcr.io/nicoverbruggen/kobuild:$target-v0.1.0" "$@"
    if [ "$mini" = false ]; then
        [ "$(clean_commit "$root")" = "$commit" ] || { echo "Source changed during the build" >&2; exit 1; }
    fi
done
if [ "$mini" = true ]; then
    echo "Qt5 and Qt6 mini packages: $root/build/mini/"
else
    echo "Qt5 and Qt6 packages: $root/build/release/"
fi
