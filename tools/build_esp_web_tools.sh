#!/usr/bin/env bash
# Vendor esp-web-tools (the browser flasher's engine) into site/vendor/, built
# against a NEWER esptool-js than any esp-web-tools release ships with.
#
# WHY (2026-09-29, bench-proven on a rev v3.2 ESP32-P4): esptool-js <= 0.6
# identifies the chip by reading the "magic" register at 0x40001000 first,
# and on rev v3 P4 silicon that read makes the ROM go silent -- the flasher
# dies right after "Connecting..." with "Serial data stream stopped" (a read
# timeout, despite the wording). esptool.py never reads it on a P4
# (USES_MAGIC_VALUE = False; it uses GET_SECURITY_INFO), and esptool-js 0.7.0
# (2026-09-21) does the same, but esp-web-tools 10.4.0 -- and its main
# branch -- still pin esptool-js ^0.6.0. So we build esp-web-tools ourselves
# with esptool-js pinned, and site/index.html loads the result instead of
# unpkg. Output is generated (git-ignored); the pages job runs this and
# copies site/vendor into the deploy. Needs node + npm + git.
#
# Bump the two pins together after checking esp-web-tools still builds
# (script/build == tsc + rollup) and that a rev v3 P4 flashes from Chrome.
set -euo pipefail

EWT_TAG="${EWT_TAG:-10.4.0}"          # esp-web-tools release tag
ESPTOOL_JS="${ESPTOOL_JS:-0.7.0}"     # esptool-js version to force (exact)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/site/vendor/esp-web-tools"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/esp-web-tools.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

echo "esp-web-tools ${EWT_TAG} + esptool-js ${ESPTOOL_JS} -> site/vendor/esp-web-tools/"
git clone -q --depth 1 --branch "$EWT_TAG" https://github.com/esphome/esp-web-tools.git "$WORK/ewt"
cd "$WORK/ewt"
npm ci --no-audit --no-fund --loglevel=error
npm install --no-audit --no-fund --loglevel=error --save-exact "esptool-js@${ESPTOOL_JS}"
# script/build does the same three things but needs jq for the version stamp.
echo "export const version = \"${EWT_TAG}+esptool-js-${ESPTOOL_JS}\";" > src/version.ts
rm -rf dist
NODE_ENV=production npm exec -- tsc
NODE_ENV=production npm exec -- rollup -c >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT"
cp -R dist/web/. "$OUT/"
# A note beside the files, for anyone who finds the directory in a deploy.
cat > "$OUT/VENDORED.txt" <<NOTE
esp-web-tools ${EWT_TAG} (https://github.com/esphome/esp-web-tools, Apache-2.0)
rebuilt with esptool-js ${ESPTOOL_JS} by tools/build_esp_web_tools.sh.
See that script for why.
NOTE
echo "wrote $(ls "$OUT" | wc -l | tr -d ' ') files to site/vendor/esp-web-tools/"
