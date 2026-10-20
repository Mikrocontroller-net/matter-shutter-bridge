# Source this in each shell before building: source tools/env.sh
ESP_ROOT="${ESP_ROOT:-$HOME/esp}"
export ESP_MATTER_PATH="$ESP_ROOT/esp-matter"
if [ -d "$ESP_ROOT/.python-shim" ]; then
  export PATH="$ESP_ROOT/.python-shim:$PATH"
fi
# shellcheck disable=SC1091
source "$ESP_ROOT/esp-idf/export.sh" >/dev/null
# shellcheck disable=SC1091
source "$ESP_ROOT/esp-matter/export.sh" >/dev/null
export IDF_CCACHE_ENABLE=1
