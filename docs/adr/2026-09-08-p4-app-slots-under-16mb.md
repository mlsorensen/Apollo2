# P4 flash layout: both app slots stay entirely below the 16 MB boundary

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-08 · **Last changed:** 2026-09-30
- **Boards:** every ESP32-P4 env (32 MB flash). S3 boards have 16 MB flash and are unaffected.

## Context

The P4 boards carry 32 MB of flash, and reads through the cache mapping (the
bootloader's boot-time verify, IDF's `esp_image_verify`, `esp_partition_mmap`)
go wrong above 0x1000000, consistent with a 3-byte flash address wrapping at
16 MB; driver reads and the ROM loader see the right bytes
([the single-variable proof](#ev-boundary)). The stock `default_32MB.csv`
puts app1 at 0xc90000 (12.56 MB), so a 3.76 MB image there ends about 160 KB
past the boundary and is rejected at boot although it was written
byte-perfect.

## Decision

`boards/p4_ota_under16.csv`, set on the base P4 env and inherited by every
P4 env:

| Partition | Offset | Size | Note |
|---|---|---|---|
| nvs | 0x9000 | 0x5000 | |
| otadata | 0xe000 | 0x2000 | |
| app0 | 0x10000 | 7 MB | |
| app1 | 0x710000 | 7 MB | ends 0xe10000 = 14.06 MB |
| spiffs | 0xe10000 | 0x11e0000 | |
| coredump | 0x1ff0000 | 64 KB | above 16 MB, see Consequences |

Rules: never let a P4 app partition or its image cross 16 MB, and never
cache-map (`esp_partition_mmap` / `spi_flash_mmap`) anything above 16 MB on
the P4 ([the coredump fault](#ev-coredump)). Driver reads
(`esp_partition_read`) of that region are fine. No app-side workaround
remains ([what was built and reverted](#alternatives-and-dead-ends)).

## Consequences

- A partition-table change cannot ship over the air, so fielded P4 units
  need a one-time web flash of v0.11.0 or later; OTA works permanently after
  that. App-only OTA images are byte-identical whichever table a unit has;
  the fix is in the factory image.
- `GET /coredump` reads the coredump partition's SHA note via
  `esp_partition_read` in `web_ui.cpp`, never through
  `esp_core_dump_get_summary()`, which mmaps it and faults
  ([how that was found](#ev-coredump)). The partition stays put; moving it
  is another table change.
- esptool `read-flash` at 115200 baud is the definitive view of a P4's flash
  (higher rates fail on its USB-CDC).

---

## Details

### Evidence

<a id="ev-boundary"></a>
The first on-device OTA of v0.11.0 (3,763,920 B) on the P4-5 downloaded and
wrote cleanly, then failed every time: the app-side verify reported a
different bad checksum per attempt, and with that bypassed the second-stage
bootloader rejected the slot with `invalid segment length 0x69666957`
("WiFi" in little-endian, a data string where a segment header should be)
and fell back to app0. The S3 4.3C installed the same-shaped image fine.

Single-variable proof on the P4-5, 2026-09-08: the SAME v0.11.0 image through
the SAME standard `esp_ota` path booted from app1 once app1 moved under
16 MB (`image self-verified OK`, `running from app1 (pending verify)`,
`confirmed healthy`).

| Image at app1 | End address | Result |
|---|---|---|
| v0.10.0, 2,501,984 B, stock table | 0xef2d60 (14.95 MB) | booted |
| v0.11.0, 3,763,920 B, stock table | 0x1026ed0 (16.15 MB) | rejected, rollback |
| v0.11.0 debug build at app0 | ~3.65 MB | booted |
| v0.11.0 at app1 of the new table | ~10.6 MB | booted |

The WRITE was perfect: the PSRAM buffer's SHA-256 matched the published
`.sha256`, `esp_partition_read` readback matched, and an esptool `read-flash`
through the ROM loader (bypassing cache, mmap, app and bootloader) was
byte-identical to the published image. Only reads through the cache mapping,
the bootloader's verify and IDF's `esp_image_verify`, went wrong, consistent
with a 3-byte flash address wrapping at 16 MB in that path.

<a id="ev-coredump"></a>
Corollary, 2026-09-30, P4-5 (rev v1.3) on v1.0.0-beta.6: `GET /coredump`
panicked every P4 because `esp_core_dump_get_summary()` mmaps the coredump
partition at 0x1ff0000 and the first load faults (3/3, and the new dump
overwrote the wanted one). Fixed the same day (`e77f033`): `web_ui.cpp`
reads the SHA note via `esp_partition_read`; a 26,084 B dump over HTTP
matched the ROM-loader read.

### Alternatives and dead ends

- "Random, size-driven, P4-only download or write corruption" (the morning
  of 2026-09-08): the per-attempt checksum differences looked like PSRAM or
  DMA coherency trouble. Wrong; the bytes in flash were right.
- App-side workarounds built during the hunt (PSRAM-to-internal bounce before
  `esp_ota_write`, self-verify via `esp_partition_read`, writing otadata
  directly, a cache flush): all reverted before the re-cut. Every earlier
  symptom was the same boundary in a different read path.
- A bootloader with 4-byte addressing: also a full reflash, riskier, no gain.

### History

- 2026-09-08: corruption theory formed and discarded within the day;
  boundary proven; v0.11.0 re-cut with the new table (`f8637cc`).
- 2026-09-30: coredump mmap fault root-caused and fixed (`e77f033`).

### Sources

`f8637cc`, `e77f033`; `boards/p4_ota_under16.csv`, `platformio.ini`,
`src/platform_esp32/web_ui.cpp`.
