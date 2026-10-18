# ESP32-P4 only: flash/package a PINNED bootloader, not the core's.
#
# OTA never replaces the bootloader, so every unit keeps the one it was last
# web-flashed with. Core 3.3.12 changed the P4 bootloader's memory map (L2
# cache 256 KB -> 128 KB, issue #4); fielded units all run the earlier one.
# Owner's call (2026-10-18): never ship a different bootloader by accident —
# every image we publish carries the one the fleet already has, so a web-
# flashed unit and an OTA'd unit are the same machine. Moving the fleet to a
# new bootloader is a deliberate release with a "web flash required" notice.
#
# The blobs are byte-for-byte the bootloaders inside the v1.1.0-beta.1 factory
# images (core 3.3.9 / IDF 5.5.4; identical across boards per silicon):
#   firmware_blobs/p4_bootloader_rev3.bin  chip_variant esp32p4     sha256 f78eeca3…
#   firmware_blobs/p4_bootloader_rev1.bin  chip_variant esp32p4_es  sha256 de4d671b…
# Each platform bump: prove the new app still boots on these (CLAUDE.md).
#
# Mechanism: the core puts its bootloader image in FLASH_EXTRA_IMAGES at
# 0x2000; firmware.factory.bin (CI artifact, web flasher) and `pio run -t
# upload` are both assembled from that list. Swap the entry for our blob.
import os

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

_BLOBS = {
    "esp32p4": "p4_bootloader_rev3.bin",
    "esp32p4_es": "p4_bootloader_rev1.bin",
}


def _setup():
    board = env.BoardConfig()  # noqa: F821
    if board.get("build.mcu", "") != "esp32p4":
        return
    variant = board.get("build.chip_variant", "esp32p4")
    blob = os.path.join(env.subst("$PROJECT_DIR"), "firmware_blobs", _BLOBS[variant])  # noqa: F821
    if not os.path.isfile(blob):
        env.Exit("p4_bootloader_pin: missing %s" % blob)  # noqa: F821
    images = list(env.get("FLASH_EXTRA_IMAGES", []))  # noqa: F821
    hits = 0
    for i, (offset, path) in enumerate(images):
        if offset == "0x2000":
            images[i] = (offset, blob)
            hits += 1
    if hits != 1:
        env.Exit("p4_bootloader_pin: expected one bootloader entry at 0x2000, "  # noqa: F821
                 "found %d — re-check before shipping" % hits)
    env.Replace(FLASH_EXTRA_IMAGES=images)  # noqa: F821
    print("p4_bootloader_pin: bootloader = %s" % os.path.basename(blob))


_setup()
