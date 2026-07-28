#!/usr/bin/env python3
"""Split a PlatformIO factory image into web-flasher parts that SKIP user data.

`firmware.factory.bin` is one blob covering 0x0 through the end of the app,
with the gaps between partitions padded with 0xFF. Flashing it at offset 0 is
simple, but it writes those pad bytes over everything in between — including
the `nvs` partition, which is where the firmware keeps the paired machine, the
Wi-Fi credentials and every setting. An upgrade therefore looked like a factory
reset, whether or not the user asked to erase.

So: emit the same image as several parts with the nvs range(s) cut out. Bytes
that used to be written still are, at the same offsets; only user data is left
alone. The nvs location comes from the build's own partition table, because it
is not the same on every board, and neither is the bootloader offset (0x0 on
ESP32-S3, 0x2000 on ESP32-P4) — nothing here is hardcoded.

    stage_flash_parts.py <pio-build-dir> <out-dir>

Writes part files plus `parts.json` ([{path, offset}, ...], ascending) for the
manifest generator.
"""

import json
import os
import sys

# esp_partition_info_t: magic u16, type u8, subtype u8, offset u32, size u32,
# label[16], flags u32 — 32 bytes per entry, little-endian.
ENTRY = 32
MAGIC = b"\xaa\x50"
TYPE_DATA = 0x01
SUBTYPE_NVS = 0x02


def nvs_ranges(partition_table: bytes):
    """Every (start, end) of an nvs partition in the table."""
    out = []
    for i in range(0, len(partition_table) - ENTRY + 1, ENTRY):
        e = partition_table[i:i + ENTRY]
        if e[:2] != MAGIC:
            break  # md5 entry or padding: the table is over
        ptype, subtype = e[2], e[3]
        offset = int.from_bytes(e[4:8], "little")
        size = int.from_bytes(e[8:12], "little")
        if ptype == TYPE_DATA and subtype == SUBTYPE_NVS:
            out.append((offset, offset + size))
    return sorted(out)


def main(build_dir: str, out_dir: str) -> int:
    factory = open(os.path.join(build_dir, "firmware.factory.bin"), "rb").read()
    table = open(os.path.join(build_dir, "partitions.bin"), "rb").read()

    holes = [(s, e) for s, e in nvs_ranges(table) if s < len(factory)]
    if not holes:
        print("stage_flash_parts: no nvs partition inside the image — "
              "refusing to publish a blind full-flash image", file=sys.stderr)
        return 1

    os.makedirs(out_dir, exist_ok=True)
    parts, cursor, n = [], 0, 0
    for start, end in holes + [(len(factory), len(factory))]:
        chunk = factory[cursor:min(start, len(factory))]
        if chunk:
            name = f"part{n}-0x{cursor:06x}.bin"
            with open(os.path.join(out_dir, name), "wb") as f:
                f.write(chunk)
            parts.append({"path": name, "offset": cursor})
            n += 1
        cursor = max(cursor, min(end, len(factory)))

    with open(os.path.join(out_dir, "parts.json"), "w") as f:
        json.dump(parts, f, indent=2)

    kept = sum(len(open(os.path.join(out_dir, p["path"]), "rb").read()) for p in parts)
    print(f"stage_flash_parts: {len(factory)} bytes -> {len(parts)} part(s), "
          f"{kept} written, preserving " +
          ", ".join(f"0x{s:x}-0x{e:x}" for s, e in holes))
    for p in parts:
        print(f"  {p['path']} @ 0x{p['offset']:x}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1], sys.argv[2]))
