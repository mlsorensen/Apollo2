# The P4's SD slot and its C6 radio share one SDMMC controller: probe once, never deinit, recover from bad reads

- **Status:** Open (the mount policy and the driver update stand; why the C6's length reads go bad on the X 8" is unknown)
- **Area:** platform
- **First decided:** 2026-07-25 · **Last changed:** 2026-11-07
- **Boards:** all P4 boards (SD on SDMMC slot 0, the ESP32-C6 radio on slot 1); the S3 boards have a native radio and are unaffected

## Context

On the P4 the SD card (slot 0) and the esp-hosted SDIO link to the ESP32-C6
radio (slot 1) ride one SDMMC peripheral: one host init and refcount, and
one chip-wide clock divider that IDF rewrites on every slot change.
Whatever the SD side does to that host, the radio feels, and every such
fault surfaces as the same radio-driver assert (`sdio_rx_get_buffer` in
`sdio_read`) presenting as a spontaneous reboot, so distinct causes look
like one bug ([the faults](#ev-faults)).

## Decision

1. The SD side never deinitialises the shared host. `ensure_bus()` brings
   the LDO rail, host and slot up once; the host config's `deinit_p` is a
   no-op (`sd_host_deinit_noop`), so every IDF mount/unmount cleanup path
   leaves the host alone ([the refcount mechanism](#ev-refcount); [why not
   a pre-init](#alternatives-and-dead-ends)).
2. The slot is probed once per boot, synchronously in `setup()` before
   `hostedInitBLE()` (`ShotStore::mount_early()`), and afterwards only from
   the Storage page's Mount button. The 5 s cardless retry loop is gone and
   must not come back ([the shared divider](#ev-divider); [why not a slower
   poll](#alternatives-and-dead-ends)). A mounted card keeps a 5 s CMD13
   liveness ping, which at 40 MHz shares the C6's divider and never moves
   the clock (why the P4-5 with a card never crashed).
3. The platform runs esp-hosted 2.12.13 (pioarduino 55.03.312-1, core
   3.3.12), whose RX path drops and re-reads a bad length instead of
   asserting ([driver history](#ev-driver); [soak](#ev-soak)).

## Consequences

- `MediumState` gained `kRemoved` (pulled while mounted; stays unmounted
  until Mount or a restart) and `kUnmounted` (by request); `StorageInfo`
  carries the CID vendor/name (`card`), CSD capacity (`card_bytes`), the
  boot-sector filesystem type and a `busy` flag that greys the buttons. Mount
  and Unmount are writer-task jobs. A card inserted after
  boot is mounted from Settings > Apollo > Storage, one ~25 ms exposure on
  the user's say-so; the radio is not torn down for it.
- Unproven: why the X 8"'s length reads go bad (SDIO margin at 40 MHz, a
  slave-side bug, something else touching the host). The driver recovers
  rather than prevents, and a persistent garbage length would now spin
  (radio wedged, no reboot) instead of crashing; never seen, never verified.
  Reopen the tracking issue ([which](#sources)) on any `sdio_rx_get_buffer`
  coredump.

---

## Details

### Evidence

<a id="ev-faults"></a>

| date | unit | finding |
|---|---|---|
| 2026-07-25 | P4-4.3, no card | boot loop every ~12 s: each failed mount's cleanup deinitialised the host under the radio. `ensure_bus()` fixed it; 2+ min cardless clean |
| <a id="ev-refcount"></a>2026-08-23 | P4-5 | two reboots; coredump: `sdio_write` asserting on a deleted queue. IDF's `sdmmc_host_init_slot` skips its refcount on a back-to-back re-init while deinit still decrements, draining to zero and deleting the radio's queues. No-op deinit fixed it |
| 2026-10-16 | X 8", no card | one silent `software (3)` reset, one panic at 24 min and 2.5 h uptime. `memfail:` named a 612 KB RX request with 50 KB largest free: a corrupted frame length, not starvation. Coredump: `shot_store` inside `sdmmc_card_init` of the cardless probe |
| <a id="ev-divider"></a>2026-10-16 | pinned IDF source | a 400 kHz probe needs host_div 10, the C6 at 40 MHz needs 4; the P4's divider is chip-wide (`HP_SYS_CLKRST`) and `sdmmc_host_start_command` rewrites it on every slot change, flipping the radio's clock for ~25 ms per probe |
| 2026-10-17 | X 8" on the probe-once build | panic at 17.6 h (742 KB length), `shot_store` idle in `xQueueReceive`. SD was not the sole cause |
| 2026-10-18 | 4-board soak, 1.1.0-beta.1 | only the X 8" crashed (6 h, 141 KB length), again with SD idle; it was also the only board with the Micra connected |
| <a id="ev-driver"></a>2026-10-18 | esp-hosted history | 2.12.8 runs SDIO RX in streaming mode and skips the length bound check, so a bad `PKT_LEN` reaches the allocator. 2.12.10 drops the read instead of asserting; 2.12.11 stops SDIO deinit tearing down the shared host; 2.12.12 re-reads the length and restarts on an all-ones value |
| <a id="ev-soak"></a>2026-10-19 to 2026-10-21 | 4 boards on 1.1.0-beta.5 | ~46 h, ~184 board-hours: 0 reboots, 0 coredumps, 0 `RX buffer alloc failed`, 0 `PKT_LEN` re-reads |

### Alternatives and dead ends

- Pre-initialising the host so the mount helper "sees already initialised"
  (2026-07-25): stale by IDF 5.5, where `sdmmc_host_init` returns ESP_OK on
  double init and the helper believes it owns the host. Superseded by the
  no-op deinit.
- Backing the cardless probe off to 5 min, or a 10 min safety poll: both
  still move the shared clock; we chose probe-at-boot plus an
  explicit Mount.
- Card-detect: the X 8" has none (socket pins 9-12 are shield, 51 kΩ
  pull-ups on CMD/D0-D3), so presence can only be learned by asking the
  card. A Mount tap is one ~25 ms exposure on the user's say-so; the radio
  is not torn down for it (a deliberate choice).

### History

- 2026-07-25: `ensure_bus()`, first no-deinit fix (`f40f014`).
- 2026-08-23: refcount mechanism decoded; `sd_host_deinit_noop` (`384756b`).
- 2026-10-16: clock-divider root cause; probe once + Storage page
  (`8946ad6`, v1.0.3-beta.1).
- 2026-10-17: post-fix crash with SD idle; "SD as sole cause" dropped.
- 2026-10-18: platform bump to esp-hosted 2.12.13 + matching C6 slave
  (`99f66fd`); issue #4 opened. The bump's bootloader fallout is the
  ota-release area's L2-cache record.
- 2026-10-23: v1.1.0 ships both fixes; #4 closed.
- 2026-11-07: folded in from the project's working notes (CLAUDE.md).

### Sources

Commits `f40f014`, `384756b`, `8946ad6`, `99f66fd`; tags v1.0.3-beta.1,
v1.1.0-beta.1 .. beta.5, v1.1.0; issue #4 (the tracking issue to reopen on
a new `sdio_rx_get_buffer` coredump); `src/platform_esp32/shot_store.cpp`;
CLAUDE.md "SD mount policy".
