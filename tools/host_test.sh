#!/usr/bin/env bash
# Builds and runs the host unit tests in build-host/.
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck disable=SC1091
source tools/env.sh
cmake -S test -B build-host -G Ninja >/dev/null
cmake --build build-host
ctest --test-dir build-host --output-on-failure
