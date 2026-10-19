# Every device env: flash/package a PINNED bootloader (never the core's), and
# compile in this env's flash epoch.
#
# OTA never replaces the bootloader or the partition table, so every unit keeps
# the ones it was last web-flashed with. Core 3.3.12 changed the P4
# bootloader's memory map (L2 cache 256 KB -> 128 KB, issue #4) and broke OTA
# onto every fielded P4. Owner's call (2026-10-18, extended to the S3
# 2026-10-19): never ship a different bootloader by accident — every image we
# publish carries the one the fleet already has, so a web-flashed unit and an
# OTA'd unit are the same machine. "It happened to work on the S3" is not a
# guarantee the next core keeps.
#
# The blobs are byte-for-byte the bootloaders inside the v1.1.0-beta.1 factory
# images (core 3.3.9 / IDF 5.5.4; cut with `tools/flash_layout.py extract`):
#   firmware_blobs/p4_bootloader_rev3.bin  esp32p4     21568 B  sha256 f78eeca3…
#   firmware_blobs/p4_bootloader_rev1.bin  esp32p4_es  21392 B  sha256 de4d671b…
#   firmware_blobs/s3_bootloader.bin       esp32s3     19984 B  sha256 b41be55a…
#                                          (qio, 16 MB: every S3 env)
#
# FLASH EPOCH (`custom_flash_epoch`, set on the base env of each chip family
# and inherited; an env may override): bump it in the commit that changes
# what lies under the app for that env — a new pinned bootloader, a partition
# table — i.e. whenever a unit can only take the release by web flash. The
# device compares it with the epoch the release's meta.json lists for its
# slug and offers "install from a computer" instead of Install when it is
# behind. The release workflow fails a run whose bootloader/partition hashes
# changed for a slug without its epoch going up (tools/flash_layout.py).
#
# Mechanism: the core puts its bootloader image in FLASH_EXTRA_IMAGES (0x2000
# on the P4, 0x0 on the S3); firmware.factory.bin (CI artifact, web flasher)
# and `pio run -t upload` are both assembled from that list. Swap the entry
# for our blob. flash_layout.json in the build dir hands the epoch and the
# offset to tools/flash_layout.py.
import json
import os

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

# (mcu, chip_variant) -> (offset, blob)
_PINS = {
    ("esp32p4", "esp32p4"): ("0x2000", "p4_bootloader_rev3.bin"),
    ("esp32p4", "esp32p4_es"): ("0x2000", "p4_bootloader_rev1.bin"),
    ("esp32s3", "esp32s3"): ("0x0000", "s3_bootloader.bin"),
}


def _setup():
    board = env.BoardConfig()  # noqa: F821
    mcu = board.get("build.mcu", "")
    variant = board.get("build.chip_variant", "") or mcu
    if (mcu, variant) not in _PINS:
        env.Exit("pio_flash_layout: no pinned bootloader for %s/%s" % (mcu, variant))  # noqa: F821
    offset, name = _PINS[(mcu, variant)]
    blob = os.path.join(env.subst("$PROJECT_DIR"), "firmware_blobs", name)  # noqa: F821
    if not os.path.isfile(blob):
        env.Exit("pio_flash_layout: missing %s" % blob)  # noqa: F821
    images = list(env.get("FLASH_EXTRA_IMAGES", []))  # noqa: F821
    hits = 0
    for i, (off, _path) in enumerate(images):
        if int(off, 16) == int(offset, 16):
            images[i] = (off, blob)
            hits += 1
    if hits != 1:
        env.Exit("pio_flash_layout: expected one bootloader entry at %s, "  # noqa: F821
                 "found %d — re-check before shipping" % (offset, hits))
    env.Replace(FLASH_EXTRA_IMAGES=images)  # noqa: F821

    try:
        epoch = int(env.GetProjectOption("custom_flash_epoch"))  # noqa: F821
    except Exception:
        env.Exit("pio_flash_layout: set custom_flash_epoch on this env")  # noqa: F821
    env.Append(CPPDEFINES=[("APOLLO_FLASH_EPOCH", epoch)])  # noqa: F821

    build_dir = env.subst("$BUILD_DIR")  # noqa: F821
    os.makedirs(build_dir, exist_ok=True)
    with open(os.path.join(build_dir, "flash_layout.json"), "w") as f:
        json.dump({"epoch": epoch, "bootloader_offset": offset,
                   "bootloader_blob": name}, f)
    print("pio_flash_layout: bootloader = %s, flash epoch %d" % (name, epoch))


_setup()
