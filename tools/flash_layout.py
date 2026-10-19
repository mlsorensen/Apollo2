#!/usr/bin/env python3
"""What a web flash puts UNDER the app: the bootloader and the partition table.

OTA replaces only the app, so these two are fixed on a unit from its last web
flash. A release that changes either one for a board can't be installed over
the air on that board: it needs a web flash, and kFlashEpoch (platformio.ini
`custom_flash_epoch`, per env) must go up so the device stops offering the
OTA. The release workflow enforces that with this script.

    flash_layout.py describe <pio-build-dir>
        -> JSON on stdout: {epoch, bootloader: {offset, sha256, idf, size},
                            partitions_sha256}
           read from firmware.factory.bin, i.e. exactly what gets flashed
           (esptool may patch the bootloader header's flash params while
           merging, so the factory image is the ground truth, not the blob).
    flash_layout.py extract <factory.bin> <offset> <out.bin>
        -> cut one bootloader image out of a factory image, byte-for-byte
           (how firmware_blobs/*_bootloader*.bin were made).
"""

import hashlib
import json
import os
import re
import struct
import sys

PARTITION_TABLE_OFFSET = 0x8000  # both chips; the build's partitions.bin lands here


def image_len(b: bytes, off: int) -> int:
    """Length of the ESP app/bootloader image starting at `off`."""
    if b[off] != 0xE9:
        raise ValueError("no ESP image magic at 0x%x" % off)
    nseg = b[off + 1]
    hash_appended = b[off + 23] == 1
    p = off + 24
    for _ in range(nseg):
        _, seg_len = struct.unpack_from("<II", b, p)
        p += 8 + seg_len
    p += 1                      # checksum byte...
    p += (-(p - off)) % 16      # ...lands on the last byte of a 16-byte block
    if hash_appended:
        p += 32
    return p - off


def bootloader_idf(img: bytes) -> str:
    m = re.search(rb"v\d+\.\d+(\.\d+)?[-\w.]*", img)
    return m.group().decode() if m else ""


def describe(build_dir: str) -> dict:
    meta = json.load(open(os.path.join(build_dir, "flash_layout.json")))
    factory = open(os.path.join(build_dir, "firmware.factory.bin"), "rb").read()
    off = int(meta["bootloader_offset"], 16)
    n = image_len(factory, off)
    bl = factory[off:off + n]
    table = open(os.path.join(build_dir, "partitions.bin"), "rb").read()
    flashed_table = factory[PARTITION_TABLE_OFFSET:PARTITION_TABLE_OFFSET + len(table)]
    if flashed_table != table:
        raise SystemExit("flash_layout: partitions.bin is not what the factory image carries")
    return {
        "epoch": meta["epoch"],
        "bootloader": {"offset": meta["bootloader_offset"], "size": n,
                       "sha256": hashlib.sha256(bl).hexdigest(),
                       "idf": bootloader_idf(bl)},
        "partitions_sha256": hashlib.sha256(table).hexdigest(),
    }


def main(argv) -> int:
    if len(argv) == 3 and argv[1] == "describe":
        json.dump(describe(argv[2]), sys.stdout, indent=2)
        print()
        return 0
    if len(argv) == 5 and argv[1] == "extract":
        b = open(argv[2], "rb").read()
        off = int(argv[3], 16)
        img = b[off:off + image_len(b, off)]
        open(argv[4], "wb").write(img)
        print("%d bytes, sha256 %s, %s" % (len(img), hashlib.sha256(img).hexdigest(),
                                          bootloader_idf(img)))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
