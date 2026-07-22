#!/usr/bin/env bash
# Build + flash the firmware for a connected board.
#
# Board selection (which PlatformIO env to flash):
#   1. explicit:  tools/flash.sh s3-7b   (canonical <chip>-<panel> name, an old
#                 alias like 7b/p4, or a full env name) / BOARD=s3-7b
#   2. auto:      if our firmware is already running, read its boot banner over
#                 serial and match the board; otherwise fall back to the default.
# The S3 boards share the same MCU, so they can't be told apart over USB
# *before* our firmware is on them — hence the banner probe + a default.
#
# The serial port is auto-detected (single board) or taken from $PORT / -p.

board_to_env() {
  case "$1" in
    s3-2|2|2inch|lcd2|s3-lcd-2|esp32-s3-micra)            echo "esp32-s3-micra" ;;
    s3-7b|7|7b|lcd7|s3-lcd-7|7inch|esp32-s3-micra-7b)     echo "esp32-s3-micra-7b" ;;
    s3-4-3b|4|43|4-3b|4.3b|43b|lcd43|esp32-s3-micra-4-3b) echo "esp32-s3-micra-4-3b" ;;
    s3-4-3c|4-3c|4.3c|43c|esp32-s3-micra-4-3c)            echo "esp32-s3-micra-4-3c" ;;
    p4-4-3|p4|p4-43|p4-wifi6|esp32-p4-micra-43)           echo "esp32-p4-micra-43" ;;
    p4-5|p45|esp32-p4-micra-5)                            echo "esp32-p4-micra-5" ;;
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
    if b"P4-WIFI6-Touch-LCD-5" in buf: print("esp32-p4-micra-5"); break  # before the generic P4 match
    if b"P4-WIFI6" in buf: print("esp32-p4-micra-43");  break  # before LCD-4: its banner has "LCD-4.3" too
    if b"LCD-7" in buf:   print("esp32-s3-micra-7b");   break
    if b"LCD-4.3C" in buf: print("esp32-s3-micra-4-3c"); break  # before the generic LCD-4 match
    if b"LCD-4" in buf:   print("esp32-s3-micra-4-3b"); break
    if b"LCD-2" in buf:   print("esp32-s3-micra");      break
s.close()
PY
}

BOARD="${1:-${BOARD:-}}"
PORT="${PORT:-$(detect_port)}"

ENV=""
if [ -n "$BOARD" ]; then
  ENV="$(board_to_env "$BOARD")"
  if [ -z "$ENV" ]; then
    echo "flash: unknown board '$BOARD' (use s3-2 | s3-7b | s3-4-3b | s3-4-3c | p4-4-3 | p4-5)" >&2
    exit 2
  fi
elif [ -n "$PORT" ]; then
  ENV="$(probe_env "$PORT")"
  [ -n "$ENV" ] && echo "flash: detected $ENV on $PORT" >&2
fi
if [ -z "$ENV" ]; then
  echo "flash: couldn't tell which board this is (the two share an MCU, and no" >&2
  echo "       running Micra firmware was detected to read its banner)." >&2
  echo "       Re-run with the board so we don't flash the wrong build:" >&2
  echo "         make flash-s3-7b     (S3 7\" 1024x600)" >&2
  echo "         make flash-s3-4-3b   (S3 4.3B 800x480)" >&2
  echo "         make flash-s3-4-3c   (S3 4.3C 800x480, dimmable)" >&2
  echo "         make flash-s3-2      (S3 2\" 320x240)" >&2
  echo "         make flash-p4-4-3    (P4-WIFI6 4.3\" 800x480)" >&2
  echo "         make flash-p4-5      (P4-WIFI6 5\" 1280x720)" >&2
  exit 2
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
