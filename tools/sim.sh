#!/usr/bin/env bash
# Build the host simulator and render one UI frame to a PNG, then open it.
#
# Usage:
#   tools/sim.sh                  # -> renders/home.png
#   tools/sim.sh renders/foo.png  # custom output path
set -euo pipefail

cd "$(dirname "$0")/.."

OUT="${1:-renders/home.png}"
PIO="${PIO:-$(command -v pio || echo /opt/homebrew/bin/pio)}"

"$PIO" run -e sim
.pio/build/sim/program "$OUT"

# Best-effort preview (macOS: Preview.app; Linux: xdg-open).
( open "$OUT" || xdg-open "$OUT" ) >/dev/null 2>&1 || true
