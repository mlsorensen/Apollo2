# Diagnostics that survive until someone looks: the log ring, a bounded /log, and a failed-allocation flight recorder in RTC memory

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-30 · **Last changed:** 2026-11-07
- **Boards:** all

## Context

Evidence evaporates before anyone looks: a machine that misbehaves at 06:30
has rebooted or scrolled its serial away before a cable is attached. Three
limits apply: the IDF's own `ESP_LOGE` lines from an empty SD slot bury
everything else ([volume](#ev-ring)); on the RGB boards the web server is
pumped from the main loop, so a long `/log` response freezes the display
and, once polled, changes what is measured ([measured](#ev-log)); and a core
dump holds task stacks only (`CONFIG_ESP_COREDUMP_CAPTURE_DRAM` is off in
the prebuilt core and cannot be turned on), so a heap-starvation crash
leaves no record of the pool ([the crash that showed it](#history)).

## Decision

- A timestamped in-RAM log ring fed by `core::logf` and an `esp_log` vprintf
  hook (IDF and NimBLE tags included), shown on-device (Stats > Info) and
  served at `/log`. A Detailed log switch sizes it ([since](#history)):
  512 KB in PSRAM with detail (about two days of per-minute telemetry),
  128 KB without (events only, weeks); 12 KB internal fallback.
- `/log` serves the last 4 KB with a header pointing at `?full=1`; serving
  less is the only lever ([why not chunking](#alternatives-and-dead-ends)).
- Vendor SD log tags (`sdmmc_common`, `vfs_fat_sdmmc`) pass only while a
  failure is news: once the error settles at `ESP_ERR_TIMEOUT` (nothing on
  the bus) they go quiet until it changes. The firmware's own line says
  plainly that an empty slot is normal.
- `platform::crash_record` registers the heap's failed-allocation callback
  and writes a census of the refusing pool (requested size, caps and
  function, task and core, totals, used blocks by exact size, the largest
  free holes) into an `RTC_NOINIT` record under 1 KB; the next boot prints
  it as `memfail:` lines into the ring. A 2 ms tripwire takes the same
  census when the DMA pool's low-water mark steps down 4 KB or sets any new
  low under 8 KB, catching the transient consumer still holding its memory
  ([verified](#ev-recorder)).

## Consequences

- The recorder's live log line runs only with >2 KB of stack headroom (it
  runs on the failing task's stack); the RTC record is written regardless.
  A power cycle clears RTC memory, a software or panic reset does not. Both
  RTC records are `static_assert`ed to 1 KB (the S3 has 8 KB of RTC slow
  RAM).
- Two dev serial commands exercise it (`poll_serial_id` in
  `src/device/main.cpp`): `memfail` forces an impossible allocation
  (`crash_record::self_test`), `reboot` soft-resets so the record survives.
- Attribution is by size signature only (`HEAP_TASK_TRACKING` is
  unavailable): big fixed-size buffers are named, small generic blocks are
  not.
- `/coredump` serves the stored dump; its P4 read-path fix is the
  ota-release area's 16 MB boundary record.

---

## Details

### Evidence

| date | measurement |
|---|---|
| <a id="ev-ring"></a>2026-07-27 | cardless retry: two IDF error lines per 5 s attempt, ~24 lines/min |
| <a id="ev-log"></a>2026-09-09 | a single `/log` of a full 64 KB ring blocked the S3 main loop >10 s; an 8,782 B response took ~0.30 s |
| 2026-09-10 | ring fills at ~329 B/min on a 4.3C (~3.3 h retention); the SD heartbeat was ~30% of all volume; after the cleanup ~190 B/min (~5.5 h) |
| <a id="ev-recorder"></a>2026-09-12 | recorder HW-verified on the 4.3C: two forced failures + `reboot` print the full census; it surfaced a DMA low-water of 596 B that boot |
| 2026-09-12 | tripwire with a 16-byte peek identified 2,432 B blocks as WiFi RX buffers (802.11 MSDU 2,304 + 8): 4 during a TLS check, 8-12 during BLE connects |
| 2026-10-16 | the recorder named the X 8" crash in one line: `sdio_read` asking for 612,864 B with 50 KB largest free, i.e. a corrupted length, not starvation |

### Alternatives and dead ends

- Pumping `lv_timer_handler()` between `/log` chunks (the pattern the C6
  update uses): on RGB/S3 boards serving while LVGL renders starved the
  panel's bounce buffer and the image jumped (seen on the 4.3C). The
  main-loop freeze is protective, so the handler was never moved.
- A second "ALLOC FAILED" hook in main.cpp: IDF keeps one callback, and the
  later registration silently replaced the recorder (fixed 2026-09-12).
- Shipping the task-stack census: ~112 B/min into the ring, half the
  retention the cleanup had won; it is a dev flag (`DEV_TASK_CENSUS`).
- Per-source log caps (the first ten touch presses of a boot, `1762b99`):
  called arbitrary in review; a verbosity level was the right shape and
  arrived as the Detailed log switch. The touch cap itself is still a
  constant in `touch.cpp` (`kMaxLoggedPresses`), not yet a consequence of
  the switch.

### History

- 2026-07-27: the IDF's SD error lines found burying everything else; SD
  vendor tags slaved to the failure's novelty (`2bea7f7`).
- 2026-07-30: 64 KB log ring, on-device viewer, `/log` (`8aab008`).
- 2026-09-09: fetching `/log` found freezing the S3's display for seconds
  and, once polled, changing the thing being measured.
- 2026-09-10: `/log` bounded to 4 KB; SD heartbeat backs off to 10 min once
  "no card" is established (`1762b99`, v0.12.1).
- 2026-09-12: the X-7's `sdio_rx_get_buffer` assert left a core dump with
  no heap evidence, the limit that motivated the recorder; flight recorder
  (`38e51f4`), hardened for small-stack tasks and the P4 (`5452562`),
  verified and de-duplicated (`eb9e0c5`), tripwire floor and peek
  (`19e4efa`); shipped in v0.14.0.
- 2026-10-16: the SD heartbeat itself ended with the probe-once mount policy
  (see the SD/radio record).
- 2026-10-31: Detailed log switch, ring 512 / 128 KB (`f7499ad`).
- 2026-11-07: folded in from the project's working notes (CLAUDE.md).

### Sources

Commits `2bea7f7`, `8aab008`, `1762b99`, `38e51f4`, `5452562`, `eb9e0c5`,
`19e4efa`, `f7499ad`; tags v0.12.1, v0.14.0; `src/platform_esp32/
crash_record.cpp`, `web_ui.cpp`, `include/platform_esp32/web_ui.h`;
CLAUDE.md "Failed-allocation flight recorder".
