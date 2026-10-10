#!/bin/sh
# Run the full native suite in the same Linux environment on either CI service.
set -eu

if [ -n "${CONTAINER_ENGINE:-}" ]; then
    native_engine=$CONTAINER_ENGINE
elif command -v podman >/dev/null 2>&1; then
    native_engine=podman
else
    native_engine=docker
fi

exec "$native_engine" run --rm --volume "$PWD:/source" --workdir /source ubuntu:24.04 sh -eu -c '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends build-essential clang libclang-rt-18-dev llvm cmake ninja-build python3 python3-venv git ca-certificates pkg-config patch zlib1g-dev libfontconfig1-dev autoconf automake libtool util-linux
    python3 -m venv /tmp/kobalt-format
    /tmp/kobalt-format/bin/python -m pip install clang-format==18.1.8
    python3 tools/format_cpp.py --check --binary /tmp/kobalt-format/bin/clang-format
    export CC=clang CXX=clang++
    export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/source
    exec setpriv --reuid="$1" --regid="$2" --clear-groups \
        python3 tools/run_tests.py --coverage --jobs 2 -- --output-junit native.xml --test-output-size-passed 10000000
' sh "$(id -u)" "$(id -g)"
