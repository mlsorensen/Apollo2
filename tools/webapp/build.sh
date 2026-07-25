#!/bin/sh
# Build the web app and regenerate the embedded firmware asset.
# Needs node/npm on PATH (any recent LTS). The generated header is COMMITTED,
# so firmware builds never require node — run this only when the app changes.
set -e
cd "$(dirname "$0")"
npm install --no-audit --no-fund
npm run build
gzip -9 -c dist/index.html > dist/index.html.gz
python3 gen_header.py dist/index.html.gz ../../include/platform_esp32/webapp_dist.h
echo "wrote include/platform_esp32/webapp_dist.h"
