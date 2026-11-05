# P4 L2 cache geometry and the pinned bootloader on every board

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-10-18 · **Last changed:** 2026-10-26
- **Boards:** L2 work is P4 only; the bootloader pin covers every device env (P4 rev v1.x, P4 rev v3, S3)

## Context

v1.1.0-beta.2 moved to Arduino core 3.3.12 for the esp-hosted 2.12.13 radio
fixes (issue #4). It installed on every P4, died at entry with `Illegal
instruction` at PC 0x4ff20d2c, rolled back and re-offered itself. ELF diff:
lib-builder #395 (2026-09-02) cut the P4's L2 cache from 256 KB to 128 KB,
moving `.iram0.text` from 0x4ff40000 to 0x4ff20000. Every fielded bootloader
(core 3.3.9 and earlier) still maps that window as cache, and OTA never
replaces the bootloader. The app loaded into the cache.

## Decision

All P4 envs:

1. `tools/pio_p4_l2_compat.py` links the app at the 256 KB-cache layout, so
   it loads under either bootloader.
2. `src/platform_esp32/l2_cache_compat.cpp` reserves the 128 KB window the
   libs' heap table would hand out (rev3 0x4ff20000-0x4ff40000, rev v1.x
   0x4ff80000-0x4ffa0000) and, at the CORE init stage, gives it back to the
   cache as 256 KB / 8 ways / 128 B lines, beta.1's geometry.
3. The switch is `IRAM_ATTR __attribute__((noinline))`; the boot log's `l2:`
   line prints the read-back geometry and the function's address with an
   IRAM / NOT IRAM verdict. A build that says NOT IRAM must not ship.

Bootloader pin (decided 2026-10-18; S3 too since 2026-10-19): no published
image carries a bootloader the fleet does not run. `tools/pio_flash_layout.py`
swaps the core's bootloader for `firmware_blobs/p4_bootloader_{rev3,rev1}.bin`
/ `s3_bootloader.bin` (byte-for-byte the v1.1.0-beta.1 ones, core 3.3.9 /
IDF 5.5.4) in both `FLASH_EXTRA_IMAGES` (factory image, web flasher) and
`UPLOADERFLAGS` (`make flash`). A new bootloader is a deliberate web-flash-only
release (see the flash-epoch record).

## Evidence

Home graph, scale streaming, frames per minute over 3 clean minutes:

| Geometry | rev v3 P4-5 | rev v1.x P4-5 | LVGL ms/frame (rev3) |
|---|---|---|---|
| 128 KB / 64 B (libs' default, beta.3) | ~875 | ~908 | 48 |
| 256 KB / 64 B (beta.4) | ~1,000 | n/a | 40.6 |
| 256 KB / 128 B (beta.1; beta.6 on) | 1,194-1,205 | 1,060-1,066 | 34.7 |

beta.1's own platform forced to 128 KB / 64 B gave ~895: the regression was
all cache configuration (2026-10-18, 2026-10-21). Figures from `2cb7bbf`,
`2594d44` and `177aa16` (which also gives the rev v1.x 36.9 ms/frame at
256 KB / 128 B); the 48 ms is from the working notes of 2026-10-18.

Boot loop, X-8 rev v3.2, 2026-10-26: a USB-flashed build looped at the
bootloader's 9 s watchdog with nothing printed while CI's image of the same
source booted; about half of a dozen layouts failed. ROM-UART markers placed
the hang right after the line-size write: GCC had inlined the `IRAM_ATTR`
function into the `ESP_SYSTEM_INIT_FN` entry, flash code, so the fetch
between the geometry write and the invalidate went through a garbage L2. Two
layouts booted with `noinline`; the same two looped without.

## Alternatives and dead ends

- Core 3.3.11 (256 KB, OTA-safe) for an IDF match: rejected;
  it lacks the radio fixes.
- Reclaiming the reserved window at runtime: Store access fault from
  `setup()`, bench 2026-10-18.
- 64 B lines "to be safe": the libs read the line size from the register at
  runtime everywhere but the startup mode-set, misaligned syncs are refused,
  and the L2 only fronts PSRAM. 128 B is what the core itself shipped Feb to
  Sep 2026 (lib-builder #350). The ROM offers only 64 and 128.
- Suspending the L2 around the write: the size write never returns.

## Consequences

- Images v1.1.0-beta.4 to v1.2.0-beta.2 ran the switch from flash and booted
  by layout luck; being fixed bytes, they keep booting. (The commit message
  of `8375920` and CLAUDE.md say beta.3, but beta.3 only re-linked and
  reserved; the switch, `2cb7bbf`, first shipped in beta.4.)
- Before ANY platform bump: diff `.iram0.text` and heap regions of old vs
  new ELF, and prove a real app-only OTA on a bench unit running the PINNED
  bootloader.
- Core 3.3.12 dropped `CONFIG_ESP_HOSTED_USE_MEMPOOL`: internal free at boot
  rose 189,208 to 272,176 B, but the SDIO pools now allocate under traffic.
- beta.2 and beta.3 were withdrawn from the release list.

## History

- 2026-10-18: 256 KB link + reserve (`6cf7a3f`, beta.3); 256 KB / 64 B
  restore on rev3 (`2cb7bbf`, beta.4); bootloader pin, rev v1.x restore
  (`2594d44`).
- 2026-10-19: S3 pinned; pin reaches `make flash` (`f4d2639`, `0962214`).
- 2026-10-21: 128 B lines restored (`177aa16`, beta.6).
- 2026-10-26: noinline + `l2:` readback (`8375920`, v1.2.0-beta.3).

## Sources

Issue #4; `6cf7a3f`, `2cb7bbf`, `2594d44`, `177aa16`, `8375920`, `f4d2639`,
`0962214`; `tools/pio_p4_l2_compat.py`, `tools/pio_flash_layout.py`,
`src/platform_esp32/l2_cache_compat.cpp`, `firmware_blobs/`.
