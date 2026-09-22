#!/usr/bin/env bash
# Build + flash the firmware for a connected board.
#
# Every env is flashable here, released or not — this is a dev tool. (Only
# p4-5, p4-4-3, s3-4-3c and p4-x-8 are published/advertised; see CLAUDE.md "Support
# matrix".)
#
# Board selection (which PlatformIO env to flash):
#   1. explicit:  tools/flash.sh p4-5    (canonical <chip>-<panel> name, an old
#                 alias like 7b/p4, or a full env name) / BOARD=p4-5
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
    p4-4-3-rev3|p4-43-rev3|esp32-p4-micra-43-rev3)        echo "esp32-p4-micra-43-rev3" ;;
    p4-5-rev3|p45-rev3|esp32-p4-micra-5-rev3)             echo "esp32-p4-micra-5-rev3" ;;
    p4-x-7|p4x7|esp32-p4-micra-x-7)                       echo "esp32-p4-micra-x-7" ;;
    p4-x-8|p4x8|esp32-p4-micra-x-8)                       echo "esp32-p4-micra-x-8" ;;
    p4-x-10-1|p4-x-10|p4x101|esp32-p4-micra-x-10-1)       echo "esp32-p4-micra-x-10-1" ;;
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

# Probe a running board for the env: its boot banner ("Micra remote — <board
# name>") names the product, and the "id?" reply's REV= field names the
# silicon (efuse major*100+minor; >= 300 = rev v3.0+), which picks the -rev3
# image for the two P4 boards that ship in both generations. The probe asks
# "id?" itself, so a board that is already up (no reset on open) answers too.
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
import re
deadline = time.time() + 3.0
buf = b""
env = None
while time.time() < deadline:
    buf += s.read(256)
    if env is None:
        if b"P4-WIFI6-Touch-LCD-X-10.1" in buf: env = "esp32-p4-micra-x-10-1"  # X boards before the generic P4 match
        elif b"P4-WIFI6-Touch-LCD-X-7" in buf: env = "esp32-p4-micra-x-7"
        elif b"P4-WIFI6-Touch-LCD-X-8" in buf: env = "esp32-p4-micra-x-8"
        elif b"P4-WIFI6-Touch-LCD-5" in buf: env = "esp32-p4-micra-5"  # before the generic P4 match
        elif b"P4-WIFI6" in buf: env = "esp32-p4-micra-43"  # before LCD-4: its banner has "LCD-4.3" too
        elif b"LCD-7" in buf:   env = "esp32-s3-micra-7b"
        elif b"LCD-4.3C" in buf: env = "esp32-s3-micra-4-3c"  # before the generic LCD-4 match
        elif b"LCD-4" in buf:   env = "esp32-s3-micra-4-3b"
        elif b"LCD-2" in buf:   env = "esp32-s3-micra"
        if env is not None and env not in ("esp32-p4-micra-5", "esp32-p4-micra-43"):
            break  # only the 4.3 and 5 come in two silicon revisions
        if env is not None:
            try: s.write(b"id?\n")
            except Exception: pass
    else:
        m = re.search(rb"REV=(\d+)", buf)
        if m:
            if int(m.group(1)) >= 300: env += "-rev3"
            break
if env: print(env)
s.close()
PY
}

BOARD="${1:-${BOARD:-}}"
PORT="${PORT:-$(detect_port)}"

ENV=""
if [ -n "$BOARD" ]; then
  ENV="$(board_to_env "$BOARD")"
  if [ -z "$ENV" ]; then
    echo "flash: unknown board '$BOARD' (use p4-5 | p4-4-3 | p4-5-rev3 | p4-4-3-rev3 | s3-4-3c | p4-x-8 | s3-2 | s3-7b | s3-4-3b | p4-x-7 | p4-x-10-1)" >&2
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
  echo "         make flash-p4-5      (P4-WIFI6 5\" 1280x720)      [released]" >&2
  echo "         make flash-p4-4-3    (P4-WIFI6 4.3\" 800x480)      [released]" >&2
  echo "           (both P4s: the chip revision, v1.x or v3, is read off the chip" >&2
  echo "            and the matching image picked for you)" >&2
  echo "         make flash-s3-4-3c   (S3 4.3C 800x480, dimmable)  [released]" >&2
  echo "         make flash-p4-x-8    (P4-WIFI6 X 8\" box 1280x800) [released]" >&2
  echo "       internal-only boards (build fine, never published):" >&2
  echo "         make flash-s3-2      (S3 2\" 320x240)" >&2
  echo "         make flash-s3-4-3b   (S3 4.3B 800x480)" >&2
  echo "         make flash-s3-7b     (S3 7\" 1024x600)" >&2
  echo "         make flash-p4-x-7    (P4-WIFI6 X 7\" box 1280x720)" >&2
  echo "         make flash-p4-x-10-1 (P4-WIFI6 X 10.1\" box 1280x800)" >&2
  exit 2
fi

if [ -z "$PORT" ]; then
  echo "flash: no serial port found." >&2
  echo "  - use a DATA USB-C cable (not charge-only) and the board's UART/USB port" >&2
  echo "  - a CH34x UART bridge needs the macOS WCH driver" >&2
  echo "  - for a first flash, try download mode: hold BOOT, tap RESET, release BOOT" >&2
  echo "  letting PlatformIO try to auto-detect anyway..." >&2
fi

# The P4-5 and P4-4.3 ship with two binary-incompatible silicon generations
# (rev v1.x "es" vs rev v3.x) under one product name, and need different
# images. The ROM loader reports the revision even on a BLANK board, so for
# those two envs ask esptool and pick the right sibling ourselves -- whichever
# spelling the user typed. (P4 only: an S3 has one silicon, and its native
# USB-CDC port must not see a scripted reset dance -- see CLAUDE.md.) The
# ask is one extra ROM handshake before the flash; the flash resets anyway.
case "$ENV" in
  esp32-p4-micra-43|esp32-p4-micra-43-rev3|esp32-p4-micra-5|esp32-p4-micra-5-rev3)
    if [ -n "$PORT" ]; then
      BASE="${ENV%-rev3}"
      ESPTOOL=""
      for c in esptool esptool.py "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" \
               "$HOME/.platformio/penv/bin/esptool.py"; do
        if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then ESPTOOL="$c"; break; fi
      done
      REV=""
      if [ -n "$ESPTOOL" ]; then
        REV="$("$ESPTOOL" --port "$PORT" chip-id 2>/dev/null | sed -n 's/.*revision v\([0-9]*\)\.\([0-9]*\).*/\1.\2/p' | head -1)"
        [ -n "$REV" ] || REV="$("$ESPTOOL" --port "$PORT" chip_id 2>/dev/null | sed -n 's/.*revision v\([0-9]*\)\.\([0-9]*\).*/\1.\2/p' | head -1)"
      fi
      if [ -n "$REV" ]; then
        if [ "${REV%%.*}" -ge 3 ]; then WANT="${BASE}-rev3"; else WANT="$BASE"; fi
        if [ "$WANT" != "$ENV" ]; then
          echo "flash: the chip on $PORT is ESP32-P4 rev v$REV -> flashing $WANT instead of $ENV" >&2
          ENV="$WANT"
        else
          echo "flash: ESP32-P4 rev v$REV confirmed for $ENV" >&2
        fi
      else
        echo "flash: couldn't read the chip revision on $PORT; flashing $ENV as asked" >&2
        echo "       (a rev v3 chip needs the -rev3 image and vice versa -- the wrong one" >&2
        echo "        just won't boot until the other is flashed; nothing is damaged)" >&2
      fi
    fi
    ;;
esac

echo "flash: env=$ENV port=${PORT:-auto}" >&2
exec pio run -e "$ENV" -t upload ${PORT:+--upload-port "$PORT"}
