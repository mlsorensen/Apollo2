# P4 radio over the on-board ESP32-C6: esp-nimble-cpp, hostedInitBLE first, a self-updating slave, and measure the DMA pool

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-09 · **Last changed:** 2026-10-18
- **Boards:** all P4 boards (P4-4.3, P4-5, X 7"/8"/10.1", both silicon revisions)

## Context

The ESP32-P4 has no radio. The Waveshare boards carry an ESP32-C6 running
Espressif's esp-hosted slave firmware, reached over SDIO; WiFi and BLE both
ride that link. NimBLE-Arduino, the S3 envs' library, does not support the
P4 ([maintainer statement](#sources)), so nothing in the S3 build applies.
The link's SDIO buffers draw on the same DMA-capable internal pool as the
display's GDMA descriptors, the I2S ring and SD-MMC.

## Decision

- BLE uses h2zero's `esp-nimble-cpp` (the same `NimBLE*` API) against the
  stock Arduino core's IDF NimBLE host, which ships NimBLE-over-hosted
  enabled; `lib_compat_mode = off` is required. It is pinned to the same
  2.5.0 API family as the S3's NimBLE-Arduino so the `ICentral` transport
  compiles against both ([why not NimBLE-Arduino](#alternatives-and-dead-ends)).
- The SDIO transport comes up through Arduino's hosted HAL,
  `hostedInitBLE()`, before `NimBLEDevice::init()`; without it the init
  aborts with the link down ([found on the first hardware](#ev-bringup)).
- The C6's slave firmware is kept matched to the host library: on every
  boot an older slave (a stale factory slave reads as 0) is reflashed from
  the embedded, committed `firmware_blobs/c6_slave.bin` over SDIO and the P4
  reboots; three failed boots give up. The blob is bumped with the
  platform's esp-hosted version (2.12.13, [since 2026-10-18](#history))
  and never fetched ([why](#alternatives-and-dead-ends)). The running image
  is marked valid before that reboot so the bootloader never reads it as a
  bootloop ([why](#ev-c6-rollback)).
- Allocation health on the P4 is read from
  `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA`, never the plain-internal figure
  ([measured on the P4-5](#ev-dma-pool)).

## Consequences

- Known consumers of the DMA-capable pool: esp-hosted SDIO buffers (WiFi
  and BLE), per-transaction GDMA descriptors, the I2S ring (1,440 B, static
  from boot: `dma_desc_num` 3 x `dma_frame_num` 120 in `sound.cpp`), SD-MMC.
  The `DSI: DMA sync missed` line prints `dma_free`/`dma_largest` beside the
  plain pair; keep it that way.
- The hosted link stalls after 60-90 s of continuous scanning ([upstream
  issue](#sources)); scans here are short and reconnects direct by address.
- The prebuilt host allows three connections: the Micra plus two scales, or
  the Micra, one inbound partner-mode Umbra and one scale. The two-scale-slot
  record (radio area) is built around that cap.
- `APOLLO_SKIP_C6_UPDATE` (a build flag no env sets) leaves a factory slave
  alone for a dump-and-restore validation build.
- The silicon-revision split (`esp32p4_es` vs `esp32p4`) and the bootloader
  pin are the hardware and ota-release areas' records.

---

## Details

### Evidence

<a id="ev-bringup"></a>
- 2026-07-17, first P4 hardware (rev v1.3 P4-4.3): `NimBLEDevice::init()`
  alone drops into the hosted vhci path with the link down and aborts
  (`card init failed`); with `hostedInitBLE()` (esp_hosted_init +
  connect_to_slave + BT controller RPC) first, link up, host init clean,
  BLE working (`a84f57b`). A `Req_GetCoprocessorFwVersion` RPC timeout at
  boot on a factory slave is harmless.

<a id="ev-dma-pool"></a>
- 2026-07-27, P4-5, web page loading with WiFi + BLE + display up
  (`a4af25b`): plain internal 45,460 free / 31,732 largest while the
  DMA-capable pool had 10,000 / 1,140. A ~2 KB GDMA descriptor list was
  failing with "largest 31,732" on the log. The same commit halved the audio
  I2S ring (4x180 -> 3x120 frames, 2,880 -> 1,440 B), which had been ~29% of
  that pool. Signatures of the pool running dry: `gdma-link:
  gdma_new_link_list: no mem` (the DSI sync falls back to a CPU copy) and
  `vhci_drv: ... malloc failed` (BLE over SDIO); they alternate because they
  share the pool.

<a id="ev-c6-rollback"></a>
- 2026-09-22 (`9a24cb5`): a C6 update on the first boot after an OTA landed
  inside the 60 s rollback window, so the bootloader read the P4's reboot as
  a bootloop and reverted. The image is now marked valid before that reboot:
  the display and hosted link coming up is what the health check stands for.
- 2026-10-18 (`99f66fd`): esp-hosted 2.12.8 -> 2.12.13 with the matching
  slave blob (byte-identical to the core's own hosted copy), for the SDIO RX
  recovery described in the SD/radio record.

### Alternatives and dead ends

- Porting NimBLE-Arduino to the P4: unsupported upstream; the API-compatible
  packaging made it unnecessary.
- Calling `NimBLEDevice::init()` alone: aborts in the vhci layer, as above.
- Fetching the slave image at update time: rejected on 2026-09-07; the blob
  is committed so the install never depends on a second download.
- Pre-allocating `esp_async_memcpy` descriptor lists at boot: not possible
  (from the working notes; not re-verified), they are allocated per
  transaction and `backlog` only caps queue depth. `a4af25b` instead tries
  the 128 KB chunk and drops to 32 KB for 2 s when its list fails.

### History

- 2026-07-09: P4-4.3 env added with esp-nimble-cpp (`400d95f`).
- 2026-07-17: hardware bring-up; `hostedInitBLE()` ordering found
  (`a84f57b`).
- 2026-07-27: DMA-capable pool measured separately on the P4-5; the miss
  line reports both pools, I2S ring halved (`a4af25b`).
- 2026-09-07: C6 auto-update shipped with the OTA work (`89e1d0e`).
- 2026-09-22: image marked valid before the C6-driven reboot (`9a24cb5`).
- 2026-10-18: slave blob and host library to 2.12.13 (`99f66fd`).

### Sources

Commits `400d95f`, `a84f57b`, `a4af25b`, `89e1d0e`, `9a24cb5`, `99f66fd`;
NimBLE-Arduino issue #906 (the maintainer's statement that the P4 is
unsupported); esp-hosted-mcu issue #180 (the scan stall after 60-90 s);
`platformio.ini` P4 base env comments; `src/device/main.cpp`;
`src/platform_esp32/c6_update.cpp`, `display.cpp` (the miss line),
`sound.cpp`; CLAUDE.md "Hosted BLE bring-up" and
"C6 auto-update".
