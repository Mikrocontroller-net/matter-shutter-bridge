#!/usr/bin/env bash
# Installs the pinned ESP-IDF and esp-matter versions into $ESP_ROOT (default ~/esp).
# Safe to rerun: existing checkouts are reused. Paths must not contain spaces.
#
# After it finishes, each shell needs:  source tools/env.sh
set -euo pipefail

ESP_ROOT="${ESP_ROOT:-$HOME/esp}"
IDF_REF="v6.0.2"                                            # recommended by esp-matter 1.6.1
MATTER_REF="4c9dd7aa80b3ddfa64c814bdfabe76c60944cc75"       # esp-matter release/v1.6.1
IDF_TARGETS="esp32s3"

case "$ESP_ROOT" in *" "*) echo "ESP_ROOT must not contain spaces" >&2; exit 1 ;; esac

# ESP-IDF 6 expects these from the system on macOS.
for tool in cmake ninja; do
  command -v "$tool" >/dev/null || { echo "Missing $tool: brew install cmake ninja" >&2; exit 1; }
done

# ESP-IDF and the Matter SDK's Python tooling are not tested on the newest
# Python; prefer 3.13 when it is installed.
if command -v python3.13 >/dev/null; then
  shim="$ESP_ROOT/.python-shim"
  mkdir -p "$shim"
  ln -sf "$(command -v python3.13)" "$shim/python3"
  ln -sf "$(command -v python3.13)" "$shim/python"
  export PATH="$shim:$PATH"
fi
echo "Using $(python3 --version)"

mkdir -p "$ESP_ROOT"
cd "$ESP_ROOT"

if [ ! -d esp-idf ]; then
  git clone --branch "$IDF_REF" --depth 1 --recursive --shallow-submodules \
    https://github.com/espressif/esp-idf.git esp-idf
fi
(cd esp-idf && ./install.sh "$IDF_TARGETS")

# shellcheck disable=SC1091
source esp-idf/export.sh >/dev/null

if [ ! -d esp-matter ]; then
  git init -q esp-matter
  git -C esp-matter remote add origin https://github.com/espressif/esp-matter.git
  git -C esp-matter fetch -q --depth 1 origin "$MATTER_REF"
  git -C esp-matter checkout -q FETCH_HEAD
fi
cd esp-matter
git submodule update --init --depth 1
(cd connectedhomeip/connectedhomeip && ./scripts/checkout_submodules.py --platform esp32 darwin --shallow)
./install.sh

echo
echo "Toolchain ready: ESP-IDF $IDF_REF, esp-matter $MATTER_REF in $ESP_ROOT"
