# Changelog

Firmware releases. The section matching a release tag becomes that GitHub
Release's notes (see `.github/workflows/firmware-release.yml`), so keep the
heading format `## vX.Y.Z` exactly — write for someone using the machine, not
for someone reading the diff.

## v0.4.0

### Features

- **Backflush cleaning** *(boards with the paddle harness)* — a guided,
  full-screen cleaning mode under **Settings → Micra → Cleaning**. Fit the
  blind filter, tap **Go**, and the device pulses the group 10 times with a
  live cycle counter and countdown. **Cancel** stops it and stays put,
  **Back** stops it and leaves, and flipping the physical paddle stops it too.
  - Pulse length follows your **Auto flush** setting. If your machine
    preinfuses, note that every pulse restarts preinfusion — set Auto flush
    long enough that each pulse outlasts it, or turn preinfusion off while
    backflushing.
- **Flush button on Home** *(paddle boards, 4.3" and larger)* — the Micra
  card's single button is now **Standby | Flush**, mirroring the scale card's
  Disconnect | Tare. Flush runs the group for a quick rinse; tap again to stop
  early.
- **Delete individual shots** — open a shot from **Stats → History** and tap
  the trash button. The shot is removed from the SD card and the headline
  stats recalculate without it, so dial-in and practice shots no longer skew
  your accuracy numbers.
- **Auto flush is now Off / 3 / 6 / 9 / 15 s** (was Off / 3 / 6). This single
  duration drives every timed run of the group: the post-shot auto-flush, the
  new Flush button, and each backflush pulse.

### Improvements

- **Settings → Device is split into Display, Time & date, and WiFi.** The old
  single page had grown long enough that reading it meant scrolling past most
  of it; each sub-page now fits on screen.
- **Setting the clock is faster** — Time (hour, minute) and Date (month, day,
  year) are now dropdown pickers on one row each, replacing six separate
  −/+ rows. **Timezone** moved here from the WiFi page, where it never really
  belonged: it governs how every time is displayed, not just NTP.
- **Flush settings live with cleaning** — Auto flush and Flush delay moved to
  the new **Micra → Cleaning** page alongside Backflush, which also shortens
  the Micra settings page.
- **The Micra button on Home shows the action, not the state.** It reads
  Standby / Turn On / Connect and is simply disabled when unavailable; the
  connection state was already spelled out in the card's status line, and the
  old wording no longer fit beside Flush.
- The manual now says what it should about storage: **a small SD card is more
  than enough.** A shot is a few tens of KB, so even a 1 GB card holds years
  of espresso — and small cards are usually already FAT32.

## v0.3.0

- Full shot history: every finished shot is recorded to the SD card as plain
  CSV under `/Apollo2`, with an on-device History view (totals, lifetime and
  30-day accuracy, month filter, per-shot cards) and a built-in web page for
  browsing and downloading from a phone or computer.
- Fixed the RGB panel's ghosted/shifted raster on the S3 4.3C.
- Expanded the manual: SD formatting (FAT32 only), hot-swap behavior, the
  on-card data layout.

## v0.2.0

- Three-way shot mode: Auto shot / Shot detect / Manual, so wired rigs can use
  the weight-stream detector too.
- Auto-flush delay became a setting (3 / 6 / 9 / 15 s).
- Fixed Bluetooth scan contention — scanning while the other device was
  connecting used to fail outright.
- Five new themes (Mono, Contrast, Ferrari, Sunset, Citrus), 3D-printable case
  and stand files, and the first version of the user manual.

## v0.1.1

- Wrong-board protection in the web flasher: the firmware answers a serial
  identify query, so the page detects and auto-selects the right board.

## v0.1.0

- First public release: brew-by-weight with paddle control, unwired shot
  detection, scale support (Bookoo Themis, Acaia Umbra / Lunar), temperature
  history, and the web flasher.
