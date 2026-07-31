#!/usr/bin/env bash
# One-shot per-unit paddle GPIO override. Writes "padsense=<N>" or
# "paddrive=<N>" down the serial port; the firmware persists it in NVS
# (survives reflashes AND web-flasher upgrades) and applies it at the next
# boot. This is the repair knob for a unit with a damaged pad — the board
# config stays canonical; the quirk travels with the hardware.
#
#   tools/padpin.sh sense 50     move this unit's paddle sense to GPIO50
#   tools/padpin.sh drive 49     move this unit's paddle drive to GPIO49
#   tools/padpin.sh sense -1     revert this unit to the board default
#
# The port is auto-detected (same globs as flash.sh) or taken from $PORT/arg 3.
set -euo pipefail

WHICH="${1:?usage: padpin.sh <sense|drive> <gpio|-1> [port]}"
PIN="${2:?usage: padpin.sh <sense|drive> <gpio|-1> [port]}"
case "$WHICH" in
  sense|drive) ;;
  *) echo "padpin: first argument must be 'sense' or 'drive'" >&2; exit 1 ;;
esac
PORT="${3:-${PORT:-}}"
if [ -z "$PORT" ]; then
  for p in /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* \
           /dev/cu.SLAB_USBtoUART* /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$p" ] && PORT="$p" && break
  done
fi
if [ -z "$PORT" ]; then
  echo "padpin: no serial port found (plug the board in, or set PORT=...)" >&2
  exit 1
fi

# No new dependencies: prefer PlatformIO's own venv python, which bundles
# pyserial (it's what `pio device monitor` runs on and this project already
# requires pio). Fall back to any python3 that happens to have pyserial.
PY=""
for cand in "$HOME/.platformio/penv/bin/python" python3; do
  if "$cand" -c 'import serial' >/dev/null 2>&1; then PY="$cand"; break; fi
done
if [ -z "$PY" ]; then
  echo "padpin: no python with pyserial found. PlatformIO provides one" >&2
  echo "  (~/.platformio/penv/bin/python); or: python3 -m pip install pyserial" >&2
  exit 1
fi

echo "padpin: port=$PORT pad$WHICH=$PIN" >&2
"$PY" - "$PORT" "pad$WHICH" "$PIN" <<'PY'
import sys, time
import serial  # provided by the interpreter chosen above

# Open WITHOUT asserting DTR/RTS: on these boards those are reset controls
# (USB-Serial-JTAG resets on DTR/RTS patterns; the S3 can even be stranded in
# download mode — see the repo's serial notes). Setting them low before open
# keeps the running firmware running.
s = serial.Serial()
s.port = sys.argv[1]
s.baudrate = 115200
s.timeout = 0.5
s.dtr = False
s.rts = False
s.open()

# Send the command repeatedly for a while: even if the open still managed to
# reboot the board, it will hear one of these once setup() finishes (~5s).
cmd = f"{sys.argv[2]}={sys.argv[3]}\n".encode()
key = sys.argv[2] + ":"
deadline = time.time() + 15.0
next_send = 0.0
buf = b""
ok = False
while time.time() < deadline:
    if time.time() >= next_send:
        s.write(cmd)
        next_send = time.time() + 1.5
    buf += s.read(256)
    if key.encode() in buf:
        for line in buf.decode(errors="replace").splitlines():
            if key in line:
                print(line.strip())
        ok = True
        break
s.close()
if not ok:
    print("padpin: no confirmation from the device (older firmware without "
          "the command, or another program holding the port?)", file=sys.stderr)
    sys.exit(1)
PY
