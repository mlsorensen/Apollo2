#!/bin/sh
# Build the web app and (re)generate the embedded firmware asset.
#
# Needs node/npm on PATH (any recent LTS) — a DEV DEPENDENCY of every device
# build. The generated header is NOT committed: the Makefile treats it as a
# build artifact of tools/webapp/src, so `make build`/`make flash` rebuild it
# whenever a web source changes, and CI does the same for releases. That way
# the page a board serves always matches the source in the tree.
#
# `gzip -n` drops the mtime from the gzip header so the same input yields a
# byte-identical asset on every machine.
set -e
cd "$(dirname "$0")"

if ! command -v npm >/dev/null 2>&1; then
  echo "build.sh: npm not found — the web app needs node (any recent LTS)." >&2
  echo "          macOS: brew install node   (see README, Developer documentation)" >&2
  exit 1
fi

npm install --no-audit --no-fund
npm run build
gzip -9 -n -c dist/index.html > dist/index.html.gz
python3 gen_header.py dist/index.html.gz ../../include/platform_esp32/webapp_dist.h
echo "wrote include/platform_esp32/webapp_dist.h"
