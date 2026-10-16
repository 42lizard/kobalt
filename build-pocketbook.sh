#!/bin/sh
# Build the standalone PocketBook package from this checkout.
set -eu
root=$(CDPATH= cd "$(dirname "$0")" && pwd)
exec python3 "$root/tools/build_pocketbook.py" "$@"
