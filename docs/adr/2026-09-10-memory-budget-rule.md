# No new RAM use on a device build without its cost stated and an explicit OK

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-09-10 · **Last changed:** 2026-10-26
- **Boards:** all; the S3 boards are the ones a few KB from starvation

## Context

The S3 boards run WiFi, NimBLE, the web server, LVGL and the RGB panel's
bounce buffers out of ~512 KB of internal RAM. By 2026-07-25 the 4.3C had hit
systemic exhaustion (`BLE_INIT malloc failed`, a web-server task that
silently never started), and through September the same shortage resurfaced
as unrelated symptoms: `esp-aes: Failed to allocate memory` mid-TLS breaking
the update check, `/log` not loading, the setup AP refusing associations, a
90 s network blackout after boot while a retry loop held ~30 KB.

The firmware was also measuring the wrong pool. Every logged heap figure was
`MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`; the hardware AES engine, the WiFi
driver and the LCD draw from the narrower `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA`.
On the P4 the plain-internal figure misleads the same way (2026-07-27, P4-5,
web page loading with WiFi + BLE + display up):

| pool | free | largest |
|---|---|---|
| `MALLOC_CAP_INTERNAL` | 45,460 | 31,732 |
| `MALLOC_CAP_INTERNAL \| MALLOC_CAP_DMA` | 10,000 | 1,140 |

On 2026-09-10 a stack bump (rgb_resync 1536 -> 3072) to cure a canary panic
was stopped in review: no change to memory configuration without approval
and a full explanation.

## Decision

Never add RAM use on a device env (a task stack increase, a new task, a
buffer, a static or heap allocation, in internal, DMA-capable or PSRAM)
without first stating the cost in bytes, the pool, and the boards, why the
zero-memory alternative does not work, and getting an explicit OK.
Restructure first: log from `loop()` instead of a small task, reuse an
existing buffer, shrink something else. Applies to fixes as much as features.

Measure the DMA-capable pool, not the 8-bit one; the heap telltale and the
`DSI: DMA sync missed` line report both.

## Evidence

The reclaim work that preceded the rule, all measured on hardware:

| date | change | result |
|---|---|---|
| 2026-07-25 | web/SD task stacks 12 -> 8 KB, ShotRecord staging to PSRAM, task creates checked | ~22 KB internal recovered (4.3C) |
| 2026-09-09 | check task preallocated static, 12 -> 8 KB (peak ~4 KB); a largest-free floor before a fetch, 8 KB by judgement then 6 KB from measurement the same day (`c3feeef`: fetches succeed from a ~7.2 KB plateau, three runs from 7,156-7,668 completed in ~1 s) | boot check completes in ~1 s, board stays pingable |
| 2026-09-10 | RGB bounce buffer x12 -> x10; seven stacks sized from high-water marks | ~26 KB nominal; DMA largest 2,932 -> 21,492 idle; setup AP starts with 26,568 free where 15,136 had failed |

Stacks from the 2026-09-10 census (S3 4.3C, live Micra + scale + mounted
card):

| task | before | after | peak |
|---|---|---|---|
| loopTask | 16384 | 12288 | 9940 |
| micra_link | 8192 | 4096 | 2364 |
| scale_link | 8192 | 4096 | 2356 |
| rgb_resync | 4096 | 1536 | 788 |
| shot_store | 8192 | 6144 (5120 after the P4 census) | 2872 S3 / 3100 P4 |
| updchk | 8192 (12288 before `7760dfe`) | 6144 (`kCheckStackBytes`) | 4036 |
| snd_play | 3584 | 2048 | 1380 |

## Alternatives and dead ends

- `WiFi.useStaticBuffers(true)`: removes late fragmentation but reserves the
  whole RX/TX pool up front, ~14 KB of contiguous internal (largest 21,492 ->
  7,156) and blocked update checks outright (2026-09-09).
- Bumping `CONFIG_ESP_IPC_TASK_STACK_SIZE` or any sdkconfig value: the
  Arduino core is prebuilt, and `custom_sdkconfig` forces the hybrid build
  that fails on the S3.
- Fixing the rgb_resync canary by growing its stack: the verbose print was
  moved out of the task into `loop()` instead, no memory added (`4380721`).

## Consequences

- Every later feature states its cost in the commit (schedule engine ~80 B
  .bss; touch diagnostics 120 B .bss plus two stack buffers; platform bump
  +1.7 KB P4 / +6.1 KB S3 static, approved 2026-10-18).
- Stack cuts apply to every board while P4 peaks were measured later;
  accepted because canary detection names the task on a bad cut.
- The failed-allocation hook and the DMA-pool telltale exist because
  attribution from a vendor tag to "which pool, how much" cost an afternoon
  (see the diagnostics record).

## History

- 2026-07-25: first internal-RAM diet (`5b9fd38`).
- 2026-07-27: P4 DMA-capable pool measured separately; the misleading
  plain-internal figure documented.
- 2026-09-08: v0.11.2: BLE scanning parked under the screensaver and the
  daily check gated on that idle window, so the TLS fetch runs with ~40 KB
  freed (`12f0c09`).
- 2026-09-09: check task static and 8 KB; NTP-sync gate (`7760dfe`).
- 2026-09-10: ~26 KB reclaimed, both pools reported (`4b69e95`, v0.12.2);
  P4 census follow-up (`969bc1b`); the rule written into CLAUDE.md
  (`4380721`).
- 2026-10-26: touch diagnostics shipped with the cost itemised, the
  pattern every change now follows.

## Sources

Commits `5b9fd38`, `12f0c09`, `7760dfe`, `c3feeef`, `4b69e95`, `969bc1b`,
`4380721`, `99f66fd`, `de3c6d8`; tags v0.11.2, v0.12.2; CLAUDE.md "Memory
budget rule"; `src/platform_esp32/update_check.cpp` (`kCheckStackBytes`,
`kMinInternalLargest`, both 6 KB today).
