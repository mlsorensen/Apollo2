# The web flasher's engine is vendored and rebuilt with a newer esptool-js

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-29 · **Last changed:** 2026-09-29
- **Boards:** all; the failure it fixes is specific to rev v3 ESP32-P4 silicon

## Context

esptool-js up to 0.6 identifies a chip by reading the "magic" register at
0x40001000 right after sync, and the rev v3 ESP32-P4 ROM goes silent on that
read ([reproduced with esptool](#evidence)). esptool.py and esptool-js 0.7.0
identify a P4 with `GET_SECURITY_INFO` instead, but esp-web-tools 10.4.0 and
its main branch pin `^0.6.0`, so the unpkg bundle cannot flash rev v3
silicon, and rev v3 boards ship to buyers under the same SKU as rev v1.x
([why not wait upstream](#alternatives-and-dead-ends)).

## Decision

`tools/build_esp_web_tools.sh` (`make site-vendor`, and the `pages` job on
every deploy) clones esp-web-tools at a pinned tag, forces esptool-js to a
pinned exact version, builds it, and drops `dist/web` into the git-ignored
`site/vendor/esp-web-tools/`. The page loads that bundle, never unpkg
([why a rebuild, not a CDN swap or a committed file](#alternatives-and-dead-ends)).

| Pin | Value |
|---|---|
| esp-web-tools tag | 10.4.0 |
| esptool-js (forced exact) | 0.7.0 |

Bump the two pins together, check esp-web-tools still builds, and re-prove a
rev v3 P4 from Chrome before deploying ([bench proof](#evidence)). A
site-only hot deploy is: build, then commit `index.html` plus `vendor/` on
`gh-pages`; a maintainer pushes it, since the assistant's push is refused as
a bypass of the release gate.

## Consequences

- The site deploy needs node, npm and git (it already needed node for the
  embedded web app).
- Rev v3 P4 boards are flashable from a browser, which the four-board matrix
  depends on: the flasher is the only install path a buyer is pointed to,
  and the flasher's "Chip revision" radio is what picks the right image.

---

## Details

### Evidence

Symptom: every web flash of the X-8 (rev v3.2 P4) died right after
"Connecting..." with "Serial data stream stopped: Possible serial noise or
corruption", in Chrome and Firefox alike, after the erase and confirm
dialogs, while `make flash` through esptool.py worked on the same board.
The battery, the port-open reboot and the dialog delay were all suspected
first and were red herrings.

The message is esptool-js's wording for a 3 s read timeout: the ROM answered
the SYNC and went silent on the very next command, the chip-detect read of
the "magic" register at 0x40001000 that esptool-js <= 0.6 sends first.
Reproduced with esptool on the X-8 itself, 2026-09-29:

| Command | Result |
|---|---|
| `read-mem 0x40001000` | Serial data stream stopped |
| `read-mem 0x5012d044` (control) | 0xb2d5dfc5, the ROM is fine |

esptool.py never reads that register on a P4 (`USES_MAGIC_VALUE = False`; it
identifies the chip with `GET_SECURITY_INFO`). esptool-js 0.7.0 (released
2026-09-21) does the same. Bench proof: the X-8 flashed v1.0.0-beta.5 from
Chrome through the rebuilt bundle (bench, 2026-09-29).

### Alternatives and dead ends

- Waiting for esp-web-tools to bump its dependency: the rev v3 boards were
  already shipping to buyers under the same SKU as rev v1.x.
- Shipping the rebuilt bundle as a committed file: rejected, generated
  output belongs in the build graph (see the repo hygiene record); the pages
  job builds it on every deploy and `make site-vendor` does locally.
- Pointing the page at a different CDN build of esptool-js: esp-web-tools
  bundles its own copy, so only a rebuild changes what runs.

### History

- 2026-09-29: root-caused, vendored, bench-proven (`8bae197`); shipped in
  v1.0.0-beta.6 (`f5dcaa6`). The same day a second, unrelated flasher bug
  was found: the page's script had died at load since the beta-channel
  change (`bcd7be1`), so Detect and the version list had been dead for 17
  days without a report.

### Sources

`8bae197`, `bcd7be1`, `f5dcaa6`; `tools/build_esp_web_tools.sh`,
`site/index.html`, `.github/workflows/firmware-release.yml` (`pages` job).
