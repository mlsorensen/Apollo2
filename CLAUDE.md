# Claude Code project notes

Firmware for a La Marzocco Micra BLE remote on Waveshare ESP32 touch boards.
Read README.md "Developer documentation" first — the architecture section is
accurate and the layering rules there are hard rules:

- `include/core/` + `src/core/` must stay free of LVGL/Arduino/NimBLE/SDL.
  The BLE protocol code (Micra link, scales) lives in `src/core/` over the
  `core::ble::ICentral` port; only transports (`nimble_central.cpp`) are
  per-platform. New platforms (Pi/BlueZ, Pico/btstack) implement ICentral,
  never touch protocol code.
- The UI depends only on `core::` interfaces. It compiles unchanged on-device
  and in the host simulator (`make sim` → renders/*.png — the fastest way to
  check UI work).
- DOCS SYNC RULE: when features, settings, screens, or boards change, update
  in the same change: MANUAL.md prose + its "Where everything lives" tree +
  TOCs, docs/HARDWARE.md, README.md, the screenshots, and the docs/adr/
  record(s) the change touches. The user-facing docs cover only the RELEASED
  boards (see "Support matrix") — a change to an internal-only board updates
  the code and its ADR, and must NOT add that board to README/HARDWARE/site.
  Refresh the affected docs/img/*.png from renders/ and re-run `make
  docs-img` (tools/annotate_docs.py + docs/img/manual/manifest.json;
  re-measure callout boxes if the layout moved — the script warns when a
  render's size changes).
- Board differences live in `include/platform_esp32/board_config.h` blocks +
  feature macros; driver code never hardcodes pins.

## Where knowledge lives: the decision log is `docs/adr/`

This file holds the rules a session must know before it touches code, and
where things live. The WHY — mechanisms, bench evidence, measurements, dead
ends, history — lives in `docs/adr/` (index by area and date in `docs/adr/README.md`). Each section below links its records (`→`).

- **Reading.** Before answering a "why is it built this way" question, or
  changing a subsystem, open `docs/adr/README.md`'s index and read the
  record(s) for that topic. They are the reasoning of record; this file is
  the summary. Don't re-derive or re-walk a dead end a record already lists.
- **Writing.** A finding, a decision, a root cause, a measurement or a dead
  end goes into the ADR, not into this file. Memory keeps only session state
  (what is in flight, where to resume, bench units' whereabouts) and points
  at the record for the design. Follow the
  README's Conventions and Template exactly:
  - One record per TOPIC. A solved issue UPDATES the record(s) whose
    decision it changes — present-tense body, a dated `- YYYY-MM-DD:` History
    bullet naming the issue/commit, **Last changed** — and every sibling that
    states the old behaviour. `grep -l` the folder first. A new file
    (`YYYY-MM-DD-<topic>.md`, the date it was first decided) only for a topic
    no record covers; say so in the commit message.
  - Body (Context / Decision / Consequences) = the current design and the
    reasons that still bind it; evidence, alternatives, history and sources
    go under `## Details`, linked from the body by anchor. History keeps only
    turns that changed the design or explain a surviving rule.
  - Team voice ("we measured", "a maintainer approves", "the bench X 8\"") —
    never "the owner", no diary phrasing. No LAN addresses, home paths,
    credentials, or vendor source references.
  - Run `make adr-index` after any header change (it fails on a malformed
    header or a filename date that disagrees with First decided).
- **This file changes only** when a new binding rule, file location or
  owner decision appears — one or two lines plus the `→` link. Never paste
  rationale here.
- **Mid-debug findings are hypotheses** (global rule): an ADR records what
  the bench proved. An unproven theory goes in as `Status: Open` with what
  is unproven said plainly, or waits.

## Web flasher engine is vendored

- `site/index.html` loads esp-web-tools from the git-ignored
  `site/vendor/esp-web-tools/` (built by `tools/build_esp_web_tools.sh` =
  `make site-vendor`, and by the pages job), never from unpkg: the stock
  bundle's esptool-js cannot flash rev v3 P4 silicon.
- Bump the script's two pins (esp-web-tools tag, esptool-js exact) together,
  and re-prove a rev v3 P4 from Chrome before deploying.
- Site-only hot deploy = build, then commit `index.html` + `vendor/` on
  gh-pages; the OWNER pushes it (the assistant's push is refused as a
  CI-gate bypass).
- → docs/adr/2026-09-29-web-flasher-vendored.md

## OTA self-update

- Two parts: a notify-only CHECK (`update_check.*`) and an INSTALL in its own
  early-boot mode (`install_mode.*`). "Install now" sets
  `Config::pending_install` and reboots; `install_mode::run()` runs at the
  very start of `setup()`, before the full display/BLE/app. Don't move the
  install into the running app (the P4's hosted radio and DSI panel can't
  take it). DSI uses the light `install_panel_*` in display.cpp; RGB uses
  the normal Display; the SPI 2-inch is a no-op. Proving ground: the
  `esp32-p4-dltest` env (`src/dltest/`) — keep it.
  → docs/adr/2026-09-07-ota-install-boot-mode.md
- Artifact: `<site>/<tag>/firmware/app/<kUpdateSlug>.bin` (app-only). The
  `parts/` files are NOT OTA images. `kUpdateSlug` (board_config.h) must
  equal the CI matrix `board`.
- Check: `esp_http_client` + `crt_bundle_attach` (the Arduino
  NetworkClientSecure wrapper can't reach the cert bundle);
  `use_psram_for_tls()` keeps TLS out of internal RAM. Cadence
  `Config::update_check_mode` Off / On boot / Daily / Hourly; the repeating
  ones fire only under the screensaver.
- Rollback: `verifyRollbackLater()` → true, image marked valid after 60 s of
  healthy `loop()` (main.cpp). So bootloader and partition-table changes
  CANNOT ship via OTA — app slot only.
- BETA CHANNEL: a beta NEVER goes in `releases.json` (format frozen; old
  firmware reads `X.Y.Z-beta.N` as `X.Y.Z` and installs it). Beta-channel
  devices (`Config::beta_channel`, NVS `updbeta`) fetch `releases-beta.json`
  instead (404 → stable list). Ordering lives in `core::semver_newer()`
  (include/core/semver.h — don't fork it) AND `order_key()` in the
  workflow's pages job; change both together. Only `-beta.N` is
  understood. Promoting a beta is a REBUILD at the real tag, never a copy
  of its artifacts. → docs/adr/2026-09-12-beta-channel-two-indexes.md
- NVS RULE: a release may only ADD keys to the `micra` namespace — never
  change the meaning, type or value range of a key an older build reads
  (stored enums index fixed arrays; rollback runs old builds unattended).
  New semantics = new key + fallback read of the old one.
  → docs/adr/2026-09-07-nvs-add-only-settings-compat.md
- P4 16 MB BOUNDARY: never let a P4 app partition or its image cross
  0x1000000, and never cache-map (`esp_partition_mmap` / `spi_flash_mmap`)
  anything above 16 MB on the P4 — use `esp_partition_read` (the coredump
  partition at 0x1ff0000 is read that way in web_ui.cpp; never call
  `esp_core_dump_get_summary()`/`get_panic_reason()` there). All P4 envs use
  `boards/p4_ota_under16.csv` via the base env.
  → docs/adr/2026-09-08-p4-app-slots-under-16mb.md

- **P4 L2 cache (issue #4).** OTA never replaces the bootloader, so an app
  must load under the one a unit was last WEB-flashed with. All P4 envs link
  at the 256 KB-cache layout (`tools/pio_p4_l2_compat.py`) and
  `src/platform_esp32/l2_cache_compat.cpp` reserves the 128 KB window and
  gives it back to the cache as 256 KB / 128 B lines at the CORE init stage.
  The window is NOT reclaimable as RAM. The switch MUST run from IRAM: keep
  `l2_cache_256k()` `IRAM_ATTR __attribute__((noinline))` (GCC once inlined
  it into flash code). **A build whose boot `l2:` line says NOT IRAM must not
  ship.** Never suspend the L2 around the mode write (it hangs). Keep 128 B
  lines (the ROM offers only 64/128).
  → docs/adr/2026-10-18-p4-l2-cache-bootloader-pin.md
- **Bootloader pin, every board (owner's call):** never publish a bootloader
  the fleet doesn't run. `tools/pio_flash_layout.py` (every device env but
  dltest) swaps in `firmware_blobs/p4_bootloader_{rev3,rev1}.bin` /
  `s3_bootloader.bin` for factory images, the web flasher AND `make flash`.
  **Before ANY platform bump:** diff `.iram0.text`/heap regions of old vs new
  ELF and prove a real app-only OTA on a bench unit running the PINNED
  bootloader before releasing.
  → docs/adr/2026-10-18-p4-l2-cache-bootloader-pin.md
- **Flash epoch = the web-flash lever.** `custom_flash_epoch` in
  platformio.ini (S3 and P4 base envs, inherited) -> `fw::kFlashEpoch`.
  **Bump it in the commit that changes what lies under the app** (a pinned
  bootloader, a partition table); the release workflow's `meta` job fails a
  layout change without a bump, or an epoch going down. Device side:
  `update_check.cpp` fetches `<tag>/meta.json` and, when the epoch is above
  ours, shows "must be installed from a computer" with no Install button.
  The index files stay flat tag lists (old firmware and the flasher read
  them). Tools: `tools/flash_layout.py`, `tools/release_meta.py`,
  `core/release_meta.h` (+ `tools/meta_epoch.cpp`).
  → docs/adr/2026-10-18-flash-epoch-web-flash-lever.md
- **C6 auto-update** (`c6_update.cpp`): every P4 boot reflashes an older C6
  slave from the committed, embedded `firmware_blobs/c6_slave.bin`. Bump the
  blob whenever the pinned platform's esp-hosted host version changes.
  → docs/adr/2026-07-09-p4-hosted-radio.md
- **`esp32-p4-dltest`** (`src/dltest/`): the standalone download/install
  proving ground; keep it. Not shipped, no pinned bootloader.
  → docs/adr/2026-09-07-ota-install-boot-mode.md
- **`esp32-p4-linktest`** (+ `-rev3`; `src/linktest/main.cpp`,
  `make build-linktest` / `flash-linktest`): internal P4-5 bench tester for
  Apollo Link boards (both senses, SHOT/CTRL, guided meter PASS/FAIL, serial
  `drv/ctrl/pulse/status/test/pass/fail/abort`). Boots CTRL OFF on purpose
  (Link in copper), the opposite of shipping firmware. Reuses
  display/touch/system (+ io_extension, log_ring) only; no NVS, no radio.
  → docs/adr/2026-09-16-apollo-link-bridge.md

## Two scales + the radio arbiter (v1.2)

**Slots and roles** → docs/adr/2026-10-24-two-scale-slots-roles-over-links.md
- ESPRESSO slot = every shot role (auto-stop, shot-start tare, shot detect, auto-flush cup-off, graph); BENCH = weight readout + its own Tare, never refused mid-shot.
- Every save is Scan → tap → "Use <name> as which scale?". No auto-slotting, ever (owner's call).
- Consumers read "the espresso scale" through `core::ScaleRoles` / `RoleScale` (include/core/scale_roles.h), never a link directly. `ScaleProvisioner::link_for(slot)` / `slot_of(link)` are the only role↔link lookups.
- Swap roles flips the role pointers AND exchanges the stored keys under ONE lock, `ScaleProvisioner::swap_mu_` (issue #6). Every per-link persister (learned type, battery blob) goes through `persist_for(link, fn)`. It is the OUTERMOST lock: call a persister holding nothing; never call back into the provisioner or the UI under it.
- NVS: espresso keys keep their names (`smac`/`sname`/`sprtnr`/`sprpend`/`satype`/`sbatest`); bench = same suffixed `2`; plus `benchhome` (0 card / 1 beside graph). ADD-only.
- The shot park follows the ROLE (after a swap the bench role sits on radio slot kScaleEspresso); the espresso role is never shot-parked.
- One Umbra partner at a time, either slot: `save_scale` refuses a second, the UI saves it plain + toasts. Max 3 connections: Micra + two scales, or Micra + inbound partner Umbra + one scale.

**Home and Settings** → docs/adr/2026-10-24-two-scales-home-and-settings.md
- `build_home_tab(..., scale_enabled, bench_enabled, bench_mode, ...)`: mode 0 = ESPRESSO | BENCH switch in the SCALE card (`show_bench_side`), mode 1 = bench card beside the graph; bench-only takes the SCALE slot. Both modes on every board.
- Graph ring `bench_weights`: no data = `kFlowNoData` (pen up), never 0. `flow_mode` 2 / `bench_overlay` / `flow_ylabels_r`. Theme role `bench` (amber).
- Settings > Scale: Bluetooth (two rows + Swap), Shot settings (Target / Review hold / Detect lead-in only), Device settings (Espresso/Bench picker), Display (incl. Bench scale on Home).
- Sim: `FakeScale::set_bench`, poses 1 (BENCH side) / 2 (save modal); renders `home_bench_*`, `scale_bt_bench`, `scale_bt_modal`, `scale_display*`, `scale_device_pick`.
- Home cards share rows: see the Row alignment rule.

**Radio arbiter** → docs/adr/2026-10-21-radio-arbiter-and-address-type.md
- `core::RadioArbiter` (include/core/radio_arbiter.h) is the ONLY thing that cancels a connect, and only the current holder; core link code never calls `ble_.cancel_connect()` (NimBLE's cancel is global).
- No task of its own: links poll it. `poll()` grants kIdle / kDirectConnect / kFinderChunk / kUserScan (preempts anything); `release()` with kConnected / kFailed / kPreempted (not a failure) / kScanDone.
- A connection is never handed across tasks: a finder sighting becomes a direct connect on the slot's own task.
- Learned address type (`satype`, Micra `matype`) is compared with the STORED type, not the try's.
- `make test-radio` — keep it green.

**Parking** → docs/adr/2026-09-08-scale-connects-parked-while-idle.md
- Per-slot bitmask of `ParkSource` (saver / portal / shot) on the arbiter. Scales park for saver + portal; bench role also for shot (phase != kIdle); the Micra for the portal only.
- A partner Umbra dozes on saver/portal only — yielding the radio must never doze it.

## Static IP

- Set ONLY on the WiFi setup portal (`TokenSetup::handle_wifi` validates with
  the page's regex AND `IPAddress::fromString`; a bad set is refused whole).
  No on-device editor (owner's call): re-run Set up WiFi, `handle_root`
  pre-fills it. `Network::start_station` applies `WiFi.config()` before
  `begin()`, all-zero to return to DHCP.
- NVS (ADDs): `ipstatic` + `ipaddr`/`ipmask`/`ipgw`/`ipdns`; they travel in
  the backup's WiFi opt-out group. `INetwork::static_ip()` feeds the Info row.
- → docs/adr/2026-07-01-wifi-ntp-and-static-ip.md

## Touch controller diagnostics

- `touch.cpp` health counters in `/api/summary`'s `touch` block; log lines
  `touch: controller not answering` / `answering again`.
- `GET /api/touch` = live probe, run ON THE LVGL TASK between polls (never a
  second task's register read; S3s run it inline from `loop()`). `?reset=1`
  pulses TP_RST and re-probes; a no-op where `kTouchRst = -1` and no expander
  bit (P4-5, X-7, X-10.1, the 2-inch).
- BEFORE any reset, fetch `/api/touch`: `ack=false`/empty product = stopped
  answering; `ack=true`, status bit7 never set = not sensing. No auto-reset,
  on purpose — it would destroy that evidence.
- `gt911_find` writes the address only on a hit; never make a probe forget it.
  A timed-out probe can still have run (`done=false`): re-fetch.
- → docs/adr/2026-10-26-touch-controller-health-probe.md

## SD mount policy + Settings > Apollo > Storage

- The slot is probed ONCE per boot (`ShotStore::mount_early()` in setup(),
  BEFORE `hostedInitBLE()`), then only by the Storage page's Mount button (a
  writer-task job). The 5 s cardless retry loop must NOT come back: on the P4
  a cardless probe rewrites the SDMMC clock divider the C6 radio shares.
- The SD side never deinitialises the shared host (`sd_host_deinit_noop`).
- A mounted card's 5 s CMD13 ping stays. A Mount tap does NOT tear the radio
  down (owner's call).
- Reopen issue #4 on any `sdio_rx_get_buffer` coredump. `GET /coredump` is
  safe on the P4 — use it.
- → docs/adr/2026-07-25-sd-slot-shares-the-radio-host.md,
  docs/adr/2026-09-12-backup-and-storage-ux.md

## Settings backup to the card

- `Settings > Apollo > Backup` ⇄ `/Apollo2/settings.txt`. Port
  `core::ISettingsBackup`, implemented by the device `ShotStore` as job kinds
  on its writer task — no second mount, task or mutex. No card slot =
  `nullptr` port, no page.
- `config_backup.cpp` ENUMERATES NVS — never hand-list keys.
- RESTORE IS A REPLACE (`nvs_erase_all`, apply, full reboot via
  `App::set_reboot_handler`), never a merge.
- Per-unit keys never leave the board (`is_unit_key`: `padsense`,
  `paddrive`, `otainst`, `sprpend`/`sprpend2`, `lastunix`, `_init`); the
  knobs, `otainst` and `lastunix` are held across the erase, and so is
  `padwire` although it travels in the file (since v1.4.0, issue #11: a
  pre-v1.4 file has no line for it). A new per-unit key needs BOTH lists.
- WiFi group (`ssid`/`wifipass`/`wifi_en` + static-IP keys) and `token` are
  opt-out switches; `mac`/`name` always travel.
- A backup from NEWER firmware is refused via `core::semver_newer` (shared
  with update_check.cpp — do not fork it).
- → docs/adr/2026-09-07-nvs-add-only-settings-compat.md,
  docs/adr/2026-09-12-backup-and-storage-ux.md

## Failed-allocation flight recorder

- `platform::crash_record` (crash_record.*) hooks the heap's
  failed-allocation callback; the census goes to RTC memory (<1 KB,
  static_assert) and prints as `memfail:` lines at the next boot. IDF keeps
  ONE such callback — never register a second.
- Dev serial: `memfail` exercises it, `reboot` soft-resets (RTC survives; a
  power cycle does not).
- → docs/adr/2026-07-30-diagnostics-that-survive-the-morning.md

## Scheduled on/standby (v1.0, issue #3)

`Settings > Micra > Schedule`. Engine `core::ScheduleEngine`
(`include/core/schedule.h`, `src/core/schedule.cpp`), owned by `ui::App`, fed
from the 500 ms `App::refresh()`. `make test-schedule`
(`tools/schedule_test.cpp`) must stay green.

Engine rules (owner's decisions, don't drift):
- Times are ONE-SHOT edges, never a held period.
- Fires only if the link is Connected inside `kScheduleFireWindowMin` (2 min);
  otherwise dropped, no catch-up. A window closing unfired logs
  `schedule: ... MISSED` with its reason.
- Each slot latches by minute-of-week; an edit pre-latches anything
  in-window (dragging a time across "now" never switches the machine).
- "On" when already on is a no-op.
- "Off" during a shot, review, cleaning or held drive line DEFERS until idle
  for `kStandbyGraceMs` (2 min), then fires if still on.
- Nothing fires unless `App::ntp_ready()` (WiFi + NTP enabled + a real sync
  THIS boot) and the date is real. Warm-up lead may wrap into the previous
  evening.
- `MachineSnapshot::brewing` is NEVER set by the link — use `ShotPhase`.
- Micra reconnects are NOT parked under the screensaver (portal only).
- Auto-standby (`tick_auto_standby`, `ScheduleAction::AutoStandby`): counts
  from the END of the last activity (a shot, `ScheduleInputs::busy` =
  cleaning || drive line held), never from turn-on; a scheduled "on" or
  standby by anyone disarms it; fires only Connected + On + kIdle; needs no
  clock and not the master switch; needs a shot source. Range 10..240 in
  10-min steps, default 30 (`sanitize_auto_standby_min`).

Page (owner's calls): two lv_menu pages; switches one per line, NO group
headings, "Auto-standby when idle" FIRST (never under the master "Enable
schedules"); no gate explanation line — controls grey silently (per-widget
40 % opa, `sync_schedule_gate`); day chips are select-only.

NVS keys (all ADDs): `schen`, `schsame`, `schwarm`, `schwarmm`, `schwarn`,
`schasb`, `schasbm`, `schasbt`, `schall`, `schd0`..`schd6` (packed by
`core::pack_day`; layout is stored — don't change it).
→ docs/adr/2026-09-22-schedule-engine-rules.md
→ docs/adr/2026-09-22-schedule-page-design.md

## Memory budget rule (owner, 2026-09-10)

**Never introduce more RAM use without review and a full explanation.** No
task-stack increase, no new task, no larger buffer, no new static/heap
allocation on any device env — internal, DMA-capable or PSRAM — without
first stating the cost (bytes, which pool, which boards), why the cheaper
alternative doesn't work, and getting the owner's OK. Applies to fixes as
much as features. Restructure first: move the work to a task that already
has the room, log from `loop()` instead of a small task, reuse a buffer.
Measure `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA`, not the 8-bit pool.
→ docs/adr/2026-09-10-memory-budget-rule.md

## Row alignment rule (owner, 2026-11-01)

**Cards that share a row share their lines — never break that without
raising it first, like RAM use.** On Home's large layouts MICRA, SCALE and
BENCH share: header, caption line, value line, stepper line (or an EMPTY
slot that keeps its height), action row — each at the same y on every card.
- New content goes INTO an existing line, never between two.
- Decoration that wraps text (the Record outline) is a layout-ignoring
  object fitted behind the labels, never a box in the flow.
- Only the bench card beside the graph is exempt.
- Moving a line, adding a row or breaking rank: ask first, build on an OK.
- Verify by LOOKING: crop the card from the sim render at 2-3x and read the
  detail log's `home: scale card ... overflow=N` / `home: bench card ...
  clipped=N` lines (measured after the App's show/hide pass). "It looks
  fine" is not the test.
→ docs/adr/2026-11-01-home-cards-share-rows.md

## Release checklist (every tag, no exceptions)

1. `include/version.h` — bump `fw::kVersion` to the tag (minus the `v`).
   CI static_asserts it (and the slug) at compile time, but bump it first.
2. `CHANGELOG.md` — real notes under `## vX.Y.Z`: `### Features` then
   `### Fixes` (omit an empty list); a bolded lead-in plus one or two
   sentences per bullet, ~50 words max, what the user sees. **NEVER invent a
   version heading for unreleased work** — it goes under `## Unreleased`,
   renamed by the release commit.
3. `make sim` green locally. Device images are CI's job — no local
   `make build-release` sweep. Exception: a platform, bootloader or
   partition-table change runs `make build-all` locally and bumps the env's
   `custom_flash_epoch` (the `meta` job fails otherwise).
4. Commit, push main, then `gh workflow run firmware-release.yml -f
   tag=vX.Y.Z` (CI creates tag + Release; never build release binaries
   locally). **Stable tags from main ONLY.** Betas may be cut from a branch
   (`--ref feat/v1.x -f tag=vX.Y.Z-beta.N`); never rebase or squash a
   branch after tagging it — merge main INTO it. A branch line's notes go
   under ONE `## vX.Y.Z` heading.
5. **Approve the deploy** ("Review deployments" on the run page). Nothing
   reaches gh-pages or Releases until then; applies to betas too. Rotate the
   `RELEASE_TOKEN` PAT before it expires.

**Beta:** same steps, tag `vX.Y.Z-beta.N`, `kVersion` = `X.Y.Z-beta.N`;
published as a pre-release, never in `releases.json`. **Promote** = on the
same commit set `kVersion` to `X.Y.Z`, roll the notes up under `## vX.Y.Z`,
run at the stable tag from main — a rebuild, never a copy.
→ docs/adr/2026-07-22-release-process-and-publish-gate.md
→ docs/adr/2026-09-12-beta-channel-two-indexes.md

## Git conventions

- Commit as `marcus@turboio.com` (repo-local config).
- Do NOT add "Co-Authored-By: Claude" or any other AI-signature trailer.

## Support matrix (READ BEFORE TOUCHING ANY BOARD LIST)

FOUR boards are released and advertised. Nothing else is, on purpose.

The P4-4.3 stays RELEASED: it was de-listed on 2026-09-10 and brought back
the next day because units were already in use (fielded units self-update
from its slugs). Don't de-list it again without asking.

| env | product | role | slug |
|---|---|---|---|
| `esp32-p4-micra-5` | P4-WIFI6-Touch-LCD-5 | mount-on-Micra, the default pick | `p4-wifi6-touch-lcd-5` |
| `esp32-p4-micra-43` | P4-WIFI6-Touch-LCD-4.3 | mount-on-Micra, smaller glass | `p4-wifi6-touch-lcd-4.3` |
| `esp32-s3-micra-4-3c` | S3-Touch-LCD-4.3C BOX | mount-on-Micra, nothing to build | `s3-touch-lcd-4.3c` |
| `esp32-p4-micra-x-8` | P4-WIFI6-Touch-LCD-X 8" | counter-top companion | `p4-wifi6-touch-lcd-x-8` |

- Positioning the docs keep: P4-5 = the recommendation; P4-4.3 = the same
  board with smaller glass (choose on screen size and price, nothing else);
  4.3C = the only finished box with on-board optos, the slower part; X 8" =
  counter-top.
- Every OTHER env (`esp32-s3-micra` 2", `-7b`, `-4-3b`, `-x-7`, `-x-10-1`)
  is INTERNAL-ONLY: builds, maintained, absent from the release matrix, the
  flasher and every user-facing doc. **Do not add one back to README.md,
  docs/HARDWARE.md, site/index.html or the CI matrix without asking.**
  Nothing is deleted to de-advertise a board. (The X 7" is the owner's desk
  unit.)
- Lists that must stay in sync: `.github/workflows/firmware-release.yml`
  matrix, `site/index.html` (cards *and* `BOARD_PATTERNS`), README.md
  ("Which board?", 3D-print table, flash/`pio run` lists), docs/HARDWARE.md,
  and the `[RELEASED]`/`[INTERNAL-ONLY]` markers in platformio.ini +
  board_config.h.
- OTA on de-listed boards is knowingly broken (install 404s, rolls back) —
  do not "fix" it: no `kOtaPublished` flag, no UI gating.
- P4 silicon: rev v1.x and rev v3 split the IMAGES, not the products. Each
  P4-5 / P4-4.3 has a `-rev3` env (`BOARD_P4_SILICON_REV3`) and slug
  `<plain-slug>-rev3`; the X 8" is rev3-only. The PLAIN slug means rev v1.x
  forever — never rename it (fielded units update from it). `kName` is the
  same for both. Never surface the revision as a separate product; the
  flasher's "Chip revision" radio + Detect (`REV=`) and `make flash-p4-5` /
  `flash-p4-4-3` (esptool reads the revision) pick the image. Picking wrong
  is recoverable — don't over-engineer guard-rails.
→ docs/adr/2026-09-10-supported-board-matrix.md
→ docs/adr/2026-07-17-p4-silicon-revisions.md

## Boards / build

- Targets are `<chip>-<panel>`. Released: `build-p4-5`, `build-p4-4-3`, their
  `-rev3` siblings, `build-s3-4-3c`, `build-p4-x-8`. Internal-only: `make
  build` (2-inch S3), `build-s3-7b`, `build-s3-4-3b`, `build-p4-x-7`,
  `build-p4-x-10-1`. Each has a `flash-*` twin that auto-detects the port;
  old names (`build-p4`, `flash-7b`, ...) are aliases. Sweeps: `make
  build-release` (released envs + sim) and `make build-all` (every env + sim).
- While iterating: `make sim` plus at most the ONE env on the bench. `make
  build-all` passes before a PUSH or RELEASE of a platform change, not per
  edit, as one invocation. Never build an env that may be being flashed.
  → docs/adr/2026-07-25-build-discipline.md
- The History page is a GENERATED, git-ignored header
  (`include/platform_esp32/webapp_dist.h`, from `tools/webapp/`). Device
  `make` targets rebuild it and need **node**; bare `pio run` does not, so run
  `make webapp` first. Never commit it or hand-edit `tools/webapp/dist/`.
  → docs/adr/2026-07-28-repo-hygiene.md

### Per board: what to build and flash

| env | glass / controller | silicon (`chip_variant`) | notes |
|---|---|---|---|
| `esp32-p4-micra-43` (+`-rev3`) | 4.3" ST7701 | v1.x `esp32p4_es` / `-rev3` `esp32p4` | released; the P4-5 image hangs silently at display init on it |
| `esp32-p4-micra-5` (+`-rev3`) | 5" HX8394 | as the 4.3 | released; `extends` the -43 env; daily-driver board |
| `esp32-p4-micra-x-8` | 8" JD9365 | `esp32p4` only (bench unit rev v3.2) | released; rotates 270 in the flush (`kDsiRotate270`) |
| `esp32-p4-micra-x-7` | 7" ILI9881C | `esp32p4_es` (bench unit rev v1.3) | internal (desk unit); 180 at the panel |
| `esp32-p4-micra-x-10-1` | 10.1" JD9365_10 | `esp32p4` (assumed) | internal, never powered (owner's call: stays out of README, flasher, matrix) |
| `esp32-s3-micra-4-3c` | 4.3" RGB | S3 | released; config-only on the 4.3B wiring + CH32V003 expander |

- P4 silicon is per UNIT, not per size or SKU, and one image cannot boot both
  generations (wrong variant = a boot loop before any log: Illegal
  instruction at the bootloader on v1.x, `CHIP_LP_WDT_RESET` on v3).
  `esptool chip-id` a new unit first; `make flash-p4-5` / `flash-p4-4-3`
  pick the rev sibling themselves. Never "simplify" a json's `chip_variant`.
  → docs/adr/2026-07-17-p4-silicon-revisions.md
- DSI glass differences are a table (`BOARD_DSI_PANEL_*` in display.cpp) plus
  block constants; rev3 keys the PHY clock on `BOARD_P4_SILICON_REV3`. On the
  ILI9881C only page-0 0x36 writes are MADCTL (page 1 has a GIP 0x36). X-8:
  GPIO23 = TP_RST, GPIO33 = TP_INT, no backlight enable. The X-7/X-10.1
  "BL_EN = 23" is a known misread left as verified — fix it only with one on
  the bench. → docs/adr/2026-07-09-p4-dsi-panels.md
- P4 radio: `esp-nimble-cpp` with `lib_compat_mode = off` (never
  NimBLE-Arduino); `hostedInitBLE()` before `NimBLEDevice::init()`. The C6
  slave auto-updates from the committed `firmware_blobs/c6_slave.bin`
  (c6_update.cpp, NVS `c6try`); bump the blob with the platform's esp-hosted
  version. `APOLLO_SKIP_C6_UPDATE` (no env sets it;
  `PLATFORMIO_BUILD_FLAGS="-D APOLLO_SKIP_C6_UPDATE" pio run -e ... -t
  upload`, which wipes every env's build dir) leaves a factory slave alone —
  the X-8's factory dump + RESTORE.md live in ~/firmware-backups/apollo/.
  Waveshare's FactoryOnly image is the known-good reference for C6/wiring.
  → docs/adr/2026-07-09-p4-hosted-radio.md
- S3: NimBLE init before WiFi in `setup()`; never script DTR/RTS on the
  USB-CDC port (strands it in download mode; physical RST recovers).
  → docs/adr/2026-07-18-s3-boot-order-and-serial-reset-trap.md
- P4 battery units power-cycle on USB plug/unplug (hardware, not firmware).
  USB-vs-battery is voltage-only (`kUsbPowerVolts`) on every board; audio,
  battery and the 4.3C expander are config-only (`board_config.h`).
  → docs/adr/2026-07-23-p4-usb-plug-power-cycle.md,
  docs/adr/2026-07-18-board-peripherals-audio-battery-expander.md

### Paddle

- Pins live in board_config.h only. 4.3C: DO0/EXIO6 drive (active-LOW),
  DI0/EXIO0 sense to **GND, not DI COM**; keep paddle.cpp's direction-mask
  (0xDE) re-assert in `begin()` and every ~64th poll. P4: GPIO52 drive
  (active-HIGH) into one PC817 module, GPIO51 sense (pull-up); Link set DRV
  GPIO3 / SENSE GPIO5 / CTRL GPIO4, the same on the X boards. GPIO2, 7/8,
  37/38 are off-limits; GPIO50 is free. Never a dual opto module board.
  → docs/adr/2026-07-18-paddle-output-path-by-board.md
- DIY cable sense wire: 1 kΩ in series, then 100 nF on the pin side; never a
  capacitor alone. The dead-pad cause is UNPROVEN — treat theories as
  hypotheses for the bench. → docs/adr/2026-07-31-paddle-sense-protection.md
- Apollo Link: CTRL HIGH from `Paddle::begin()` for as long as firmware runs
  (input = paddle in copper). Paddle wiring Auto/Link/Cable =
  `core::PaddleWiring` on `IPaddle`, NVS `padwire` (in backups since
  v1.4.0, unlike the `padsense`/`paddrive` pad remaps), P4 only; main.cpp
  sets it before `begin()`.
  Rev 0.7 is the last rev (there is no 0.8); D3 must be UNIDIRECTIONAL.
  → docs/adr/2026-09-16-apollo-link-bridge.md
- Paddle breaker (core/brew_controller): every drive goes through
  `set_drive()`; trips on 10 rejected pulses or 4 sub-500 ms accepted pulses
  per 60 s. NEVER add a rate rule on accepted edges (a backflush is 10+
  edges). Reset is manual; nothing persists. Keep `make test-paddle` green.
  → docs/adr/2026-11-03-paddle-breaker.md
