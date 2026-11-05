# Flash epochs and meta.json: how a release declares itself web-flash-only

- **Status:** Open (device gate and CI job proven; no release has yet raised an epoch, so the WEB FLASH REQUIRED notes path has only run against a bench build)
- **Area:** ota-release
- **First decided:** 2026-10-18 · **Last changed:** 2026-10-21
- **Boards:** all device envs

## Context

OTA replaces the app slot only. A release that changes what lies UNDER the
app, the bootloader or the partition table, cannot be installed over the air
on a unit with the old one; the under-16 MB P4 table and the L2 cache fix
both needed a one-time web flash ([when](#history)). Without a lever such a
release installs, dies, rolls back and re-offers itself forever, and nothing
in the pipeline notices the change.

## Decision

A per-board integer, the flash epoch, names the bootloader + partition-table
generation a unit was last web-flashed with. Everything is epoch 1.

- `custom_flash_epoch` in `platformio.ini` (per chip-family base env,
  inherited, overridable) becomes `fw::kFlashEpoch` through a PRE-script
  define ([why PRE](#evidence)); a device build without it fails (`#error`
  in `version.h`). Bump it in the commit that changes what lies under the
  app.
- The workflow's `meta` job (ungated, before approval) describes every
  board's bootloader and partition table from its factory image
  (`tools/flash_layout.py describe`), compares with the newest earlier
  release carrying a `<tag>/meta.json` (`tools/release_meta.py`), and FAILS
  the run on a change without a bump or an epoch going down. It publishes
  this release's `meta.json` ([format and budget](#ev-meta-format)).
- A release that raises an epoch leads its notes and `notes.txt` with
  `**WEB FLASH REQUIRED** (<boards>)`, for firmware from before the lever
  ([why that line is not the whole lever](#alternatives-and-dead-ends)).
- Device (`update_check.cpp`): once a newer version's image is served, fetch
  its `meta.json`. 404 = a pre-meta release = epoch 1. No answer, or our slug
  missing = not offered this round. Epoch above `kFlashEpoch` =
  `UpdateInfo::web_flash`: the modal says "must be installed from a computer"
  and has NO Install button ([seen on the bench](#evidence)).
- The index files stay flat tag lists ([why](#alternatives-and-dead-ends)).
  Bootloader IDF version and epoch are logged at boot and in `/api/summary`
  as evidence only; the epoch is the gate.

## Consequences

- A pinned-bootloader or partition-table change is the files plus the
  epoch bump; forgetting the bump fails CI, not the fleet.
- Firmware older than v1.1.0-beta.5 has no gate: on an epoch-raising
  release it installs, rolls back and re-prompts; the notes' first line is
  its only help.
- One small extra request per offered version.

---

## Details

### Evidence

<a id="ev-meta-format"></a>
`<tag>/meta.json` is `{"epoch":{slug:N,...},"layout":{...}}`, the epoch map
first and ending within 448 B (`DEVICE_META_BUDGET`, under the device's
512 B fetch, `core::kMaxMetaBytes`); the `meta` job reads it back with the
device's own parser (`core/release_meta.h` via `tools/meta_epoch.cpp`).

| Date | Unit | Result |
|---|---|---|
| 2026-10-19 | rev v1.3 P4-5, build with `custom_flash_epoch = 0` | booted reporting epoch 1: the define came from a POST script and never reached the sources (`ebdd41c` fixed it) |
| 2026-10-19 | same unit, fixed build | logs `v1.1.0-beta.5 needs flash epoch 1 (this unit: 0) - web flash only`; check-task stack high-water 2,820 of 6,144 B |
| 2026-10-19 | CI, v1.1.0-beta.5 | first `meta` job run; `meta.json` lists epoch 1 for every slug. Its images were built before the define fix and compiled the fallback, also 1 |
| 2026-10-21 | same P4-5, still epoch 0 | modal reads "must be installed from a computer", no Install button |

### Alternatives and dead ends

- A marker line in `notes.txt` as the only flag (the first sketch): kept
  as the human-readable half; older firmware can only display it and newer
  firmware needs a per-slug value.
- Changing the index format: refused, every fielded device parses it.
- Reasoning from the bootloader's version string at runtime: logged, but a
  version does not say whether a given app loads under it; the epoch does.

### History

- 2026-09-08: the under-16 MB P4 partition table, the first change under
  the app that needed a web flash (see that record).
- 2026-10-18: the L2 cache break, the second; levers decided the same day.
- 2026-10-19: built (`f4d2639`) and shipped in v1.1.0-beta.5 (`46bc62e`);
  the pre-script define fix (`ebdd41c`) landed an hour after that tag and
  shipped in v1.1.0-beta.6.
- 2026-10-21: device path confirmed on the epoch-0 bench unit.

### Sources

`f4d2639`, `ebdd41c`, `46bc62e`; `.github/workflows/firmware-release.yml`
(`meta` job), `tools/release_meta.py`, `tools/flash_layout.py`,
`tools/meta_epoch.cpp`, `include/core/release_meta.h`, `include/version.h`,
`src/platform_esp32/update_check.cpp`.
