# P4 L2 cache geometry and the pinned bootloader on every board

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-10-18 · **Last changed:** 2026-11-07
- **Boards:** L2 work is P4 only; the bootloader pin covers every device env (P4 rev v1.x, P4 rev v3, S3)

## Context

Arduino core 3.3.12, needed for its esp-hosted radio fixes, cuts the P4's L2
cache from 256 KB to 128 KB and moves `.iram0.text` from 0x4ff40000 to
0x4ff20000. Every fielded bootloader (core 3.3.9 and earlier) maps that
window as cache, and OTA never replaces the bootloader, so an app linked at
the new layout loads into the cache and dies at entry
([the break](#ev-break)). The smaller cache also costs the graph about a
quarter of its frame rate ([measured](#ev-geometry)).

## Decision

All P4 envs:

1. `tools/pio_p4_l2_compat.py` links the app at the 256 KB-cache layout, so
   it loads under either bootloader.
2. `src/platform_esp32/l2_cache_compat.cpp` reserves the 128 KB window the
   libs' heap table would hand out (rev3 0x4ff20000-0x4ff40000, rev v1.x
   0x4ff80000-0x4ffa0000) and, at the CORE init stage, gives it back to the
   cache as 256 KB / 8 ways / 128 B lines, the geometry the core shipped
   before the cut ([frame rates by geometry](#ev-geometry); [why not 64 B
   lines or a reclaimed window](#alternatives-and-dead-ends)).
3. The switch is `IRAM_ATTR __attribute__((noinline))`; the boot log's `l2:`
   line prints the read-back geometry and the function's address with an
   IRAM / NOT IRAM verdict. A build that says NOT IRAM must not ship
   ([the boot loop that taught this](#ev-bootloop)).

Bootloader pin, every device env except the standalone `esp32-p4-dltest`
proof firmware: no published image carries a bootloader
the fleet does not run. `tools/pio_flash_layout.py` swaps the core's
bootloader for `firmware_blobs/p4_bootloader_{rev3,rev1}.bin` /
`s3_bootloader.bin` (byte-for-byte the v1.1.0-beta.1 ones, core 3.3.9 /
IDF 5.5.4; cut from those images with `tools/flash_layout.py extract`)
in both `FLASH_EXTRA_IMAGES` (factory image, web flasher) and
`UPLOADERFLAGS` (`make flash`). A new bootloader is a deliberate
web-flash-only release (see the flash-epoch record).

## Consequences

- Images v1.1.0-beta.4 to v1.2.0-beta.2 ran the switch from flash and booted
  by layout luck; being fixed bytes, they keep booting
  ([which builds](#history)).
- Before ANY platform bump: diff `.iram0.text` and heap regions of old vs
  new ELF, and prove a real app-only OTA on a bench unit running the PINNED
  bootloader.
- Core 3.3.12 dropped `CONFIG_ESP_HOSTED_USE_MEMPOOL`: internal free at boot
  rose 189,208 to 272,176 B, but the SDIO pools now allocate under traffic.
- beta.2 and beta.3 were withdrawn from the release list.

---

## Details

### Design detail

The switch (`l2_cache_256k()`) follows the libs' own startup order, plus a
write-back first because PSRAM may already hold dirty lines: interrupts
off, `Cache_WriteBack_All` (L1 D-cache and L2), `Cache_Set_L2_Cache_Mode`
(256 KB, 8 ways, 128 B), `Cache_Invalidate_All` (L2). Everything it calls
is ROM except `Cache_WriteBack_All`, the libs' IRAM patch. The ROM's
`Cache_Set_L2_Cache_Mode` is two register writes and ignores its ways
argument; the enum is passed anyway. It runs as an `ESP_SYSTEM_INIT_FN` at
the CORE stage, on core 0 with the other core still parked.

### Evidence

<a id="ev-break"></a>
The break: v1.1.0-beta.2 moved to Arduino core 3.3.12 for the esp-hosted
2.12.13 radio fixes (issue #4). It installed on every P4, died at entry with
`Illegal instruction` at PC 0x4ff20d2c, rolled back and re-offered itself.
ELF diff: lib-builder #395 (2026-09-02) cut the P4's L2 cache from 256 KB to
128 KB, moving `.iram0.text` from 0x4ff40000 to 0x4ff20000. The app had
loaded into what the fielded bootloader maps as cache.

<a id="ev-geometry"></a>
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

<a id="ev-bootloop"></a>
Boot loop, X-8 rev v3.2, 2026-10-26: a USB-flashed build looped at the
bootloader's 9 s watchdog with nothing printed while CI's image of the same
source booted; about half of a dozen layouts failed. ROM-UART markers placed
the hang right after the line-size write: GCC had inlined the `IRAM_ATTR`
function into the `ESP_SYSTEM_INIT_FN` entry, flash code, so the fetch
between the geometry write and the invalidate went through a garbage L2. Two
layouts booted with `noinline`; the same two looped without.

### Alternatives and dead ends

- Core 3.3.11 (256 KB, OTA-safe) for an IDF match: rejected;
  it lacks the radio fixes.
- Reclaiming the reserved window at runtime: Store access fault from
  `setup()`, bench 2026-10-18.
- 64 B lines "to be safe": the libs read the line size from the register at
  runtime everywhere but the startup mode-set, misaligned syncs are refused
  rather than rounded, and the L2 only fronts PSRAM and flash (internal-SRAM
  DMA, the radio's SDIO pool included, sits behind the L1 D-cache, whose
  line stays 64 B). 128 B is what the core itself shipped Feb to Sep 2026
  (lib-builder #350, an ESP-SR override; #395 dropped it as a cleanup, not
  a fix). The ROM offers only 64 and 128.
- Suspending the L2 around the write: the size write never returns.

### History

- 2026-10-18: 256 KB link + reserve (`6cf7a3f`, beta.3); 256 KB / 64 B
  restore on rev3 (`2cb7bbf`, beta.4); bootloader pin for the P4s, rev v1.x
  restore (`2594d44`).
- 2026-10-19: S3 bootloader pinned too (`f4d2639`), so beta.4's S3 web
  flash had carried the core's unpinned IDF 5.5.5 bootloader (bench units
  only, harmless). The pin reaches `make flash` (`0962214`): until then a
  USB upload wrote the core's IDF 5.5.5 bootloader while the factory image
  carried the pin.
- 2026-10-21: 128 B lines restored (`177aa16`, beta.6).
- 2026-10-26: noinline + `l2:` readback (`8375920`, v1.2.0-beta.3). The
  commit message of `8375920` and CLAUDE.md say the from-flash switch dates
  from beta.3, but beta.3 only re-linked and reserved; the switch,
  `2cb7bbf`, first shipped in beta.4, so the affected images are beta.4 to
  v1.2.0-beta.2.
- 2026-11-07: folded in from the project's working notes (CLAUDE.md).

### Sources

Issue #4; `6cf7a3f`, `2cb7bbf`, `2594d44`, `177aa16`, `8375920`, `f4d2639`,
`0962214`; `tools/pio_p4_l2_compat.py`, `tools/pio_flash_layout.py`,
`src/platform_esp32/l2_cache_compat.cpp`, `firmware_blobs/`.
