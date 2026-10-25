#!/usr/bin/env bash
# Develops on the bridge over Wi-Fi instead of USB (see main/remote.h).
#
#   tools/remote.sh flash          build, upload, and wait for the new firmware
#   tools/remote.sh cmd <line>     run one console line, e.g. matter esp status
#   tools/remote.sh log [-f]       print the kept log; -f keeps following it
#
# BRIDGE_HOST (default shutter-bridge) and BRIDGE_TOKEN come from the
# environment or from .bridge-remote in the repository root (gitignored, same
# variable syntax). `matter esp remote` on the USB console shows both.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -f "$root/.bridge-remote" ]]; then source "$root/.bridge-remote"; fi
host="${BRIDGE_HOST:-shutter-bridge}"
token="${BRIDGE_TOKEN:-}"
if [[ -z "$token" ]]; then
  echo "BRIDGE_TOKEN is not set; 'matter esp remote' on the USB console shows it" >&2
  exit 1
fi

# api <path> [curl options...]: one authorized request; the body goes to stdout.
api() {
  local path="$1"
  shift
  curl --silent --show-error --fail-with-body --max-time "${TIMEOUT:-30}" \
    -H "Authorization: Bearer $token" "$@" "http://$host$path"
}

# header <file> <name>: a header's value from a curl -D dump.
header() {
  tr -d '\r' <"$1" | awk -F': ' -v k="$2" 'tolower($1) == tolower(k) { print $2 }'
}

flash() {
  source "$root/tools/env.sh" >/dev/null 2>&1
  local out
  if ! out=$(cd "$root" && idf.py build 2>&1); then
    echo "$out" >&2
    exit 1
  fi
  local bin="$root/build/matter-shutter-bridge.bin"
  local want
  want=$(shasum -a 256 "$root/build/matter-shutter-bridge.elf" | cut -c1-8)
  echo "uploading $(wc -c <"$bin" | tr -d ' ') bytes (firmware $want) to $host"
  TIMEOUT=300 api /ota --data-binary @"$bin" -H "Content-Type: application/octet-stream"

  # New firmware answers once Wi-Fi is up, which also confirms it. Firmware
  # that does not get there is rolled back within 60 s, and the old one
  # answers instead.
  local dump
  dump=$(mktemp)
  trap 'rm -f "$dump"' RETURN
  sleep 5
  for _ in $(seq 60); do
    if TIMEOUT=3 api "/log" -o /dev/null -D "$dump" 2>/dev/null; then
      local running
      running=$(header "$dump" X-Firmware)
      if [[ "$running" == "$want "* ]]; then
        echo "running $running"
        return 0
      fi
      echo "the device runs $running, not $want: the update was rolled back" >&2
      exit 1
    fi
    sleep 2
  done
  echo "no answer from $host for 2 minutes; check the USB console" >&2
  exit 1
}

log() {
  local follow=""
  if [[ "${1:-}" == "-f" ]]; then follow=1; fi
  local dump since=""
  dump=$(mktemp)
  trap 'rm -f "$dump"' RETURN
  while :; do
    if ! api "/log${since:+?since=$since}" -D "$dump"; then
      if [[ -z "$follow" ]]; then exit 1; fi
      sleep 2
      continue
    fi
    local start next
    start=$(header "$dump" X-Log-Start)
    next=$(header "$dump" X-Log-Next)
    if [[ -n "$since" && "$start" -lt "$since" ]]; then
      echo "--- device restarted ---"
    elif [[ -n "$since" && "$start" -gt "$since" ]]; then
      echo "--- $((start - since)) bytes lost ---"
    fi
    since="$next"
    if [[ -z "$follow" ]]; then break; fi
    sleep 1
  done
}

case "${1:-}" in
  flash) flash ;;
  cmd) shift; api /console --data-binary "$*" ;;
  log) shift; log "$@" ;;
  *)
    sed -n '2,10s/^# \{0,1\}//p' "$0" >&2
    exit 2
    ;;
esac
