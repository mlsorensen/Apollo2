#!/usr/bin/env bash
# Build + flash the firmware for a connected board.
#
# Board selection (which PlatformIO env to flash):
#   1. explicit:  tools/flash.sh 7b      (or 2inch / an env name) / BOARD=7b
#   2. auto:      if our firmware is already running, read its boot banner over
#                 serial and match the board; otherwise fall back to the default.
# The two boards share the same ESP32-S3, so they can't be told apart over USB
# *before* our firmware is on them — hence the banner probe + a default.
#
# The serial port is auto-detected (single board) or taken from $PORT / -p.

DEFAULT_BOARD="2inch"   # used when nothing is specified and no banner is seen

board_to_env() {
  case "$1" in
    2|2inch|lcd2|s3-lcd-2|esp32-s3-micra)        echo "esp32-s3-micra" ;;
    7|7b|lcd7|s3-lcd-7|7inch|esp32-s3-micra-7b)  echo "esp32-s3-micra-7b" ;;
    *) echo "" ;;
  esac
}

detect_port() {
  local p
  for p in /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* \
           /dev/cu.SLAB_USBtoUART* /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$p" ] && { printf '%s' "$p"; return 0; }
  done
  return 0  # none found -> empty (PlatformIO can still try to auto-detect)
}

# Probe a running board's boot banner ("Micra remote — <board name>") for the env.
probe_env() {
  local port="$1"
  command -v python3 >/dev/null 2>&1 || return 0
  python3 - "$port" <<'PY' 2>/dev/null
import sys, time
try:
    import serial
except Exception:
    sys.exit(0)
try:
    s = serial.Serial(sys.argv[1], 115200, timeout=0.5)
except Exception:
    sys.exit(0)
deadline = time.time() + 3.0
buf = b""
while time.time() < deadline:
    buf += s.read(256)
    if b"LCD-7" in buf:   print("esp32-s3-micra-7b"); break
    if b"LCD-2" in buf:   print("esp32-s3-micra");    break
s.close()
PY
}

BOARD="${1:-${BOARD:-}}"
PORT="${PORT:-$(detect_port)}"

ENV=""
if [ -n "$BOARD" ]; then
  ENV="$(board_to_env "$BOARD")"
  if [ -z "$ENV" ]; then
    echo "flash: unknown board '$BOARD' (use 2inch | 7b)" >&2
    exit 2
  fi
elif [ -n "$PORT" ]; then
  ENV="$(probe_env "$PORT")"
  [ -n "$ENV" ] && echo "flash: detected $ENV on $PORT" >&2
fi
if [ -z "$ENV" ]; then
  ENV="$(board_to_env "$DEFAULT_BOARD")"
  echo "flash: defaulting to $ENV (pass 2inch|7b to override)" >&2
fi

if [ -z "$PORT" ]; then
  echo "flash: no serial port found." >&2
  echo "  - use a DATA USB-C cable (not charge-only) and the board's UART/USB port" >&2
  echo "  - a CH34x UART bridge needs the macOS WCH driver" >&2
  echo "  - for a first flash, try download mode: hold BOOT, tap RESET, release BOOT" >&2
  echo "  letting PlatformIO try to auto-detect anyway..." >&2
fi

echo "flash: env=$ENV port=${PORT:-auto}" >&2
exec pio run -e "$ENV" -t upload ${PORT:+--upload-port "$PORT"}
