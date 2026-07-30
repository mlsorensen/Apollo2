# Changelog

Firmware releases. The section matching a release tag becomes that GitHub
Release's notes (see `.github/workflows/firmware-release.yml`), so keep the
heading format `## vX.Y.Z` exactly — write for someone using the machine, not
for someone reading the diff.

## Unreleased

### Features

- **A diagnostic log you can read after the fact.** The device now keeps its
  recent diagnostics (the same messages the USB serial console prints, plus
  the system's own) in memory — 64 KB, typically hours of normal use — with
  each line stamped with the time, or seconds-since-boot until the clock is
  set. Read it on the device (**Stats → Info → Diagnostic log**) or, with
  Wi-Fi connected, as plain text at `http://<device IP>/log` (the web page's
  header has a **Log** link too). So when the paddle "did nothing" this
  morning, the evidence is still there after breakfast — no serial cable at
  the moment of failure required. The log doesn't survive a reboot.

### Fixes

- **The standby/turn-on button reflects the machine's state faster.** The
  Micra's Bluetooth interface answers state queries with a lag right after a
  mode change, so the old "read back immediately after the command" approach
  reliably saw the previous state and the screen then waited out a slow 3 s
  poll — occasionally 4 s of nothing after a tap. The state poll now simply
  runs every second, so the button settles within about a second of the
  machine actually switching.

## v0.5.2

### Fixes

- **Upgrading with the web flasher no longer wipes your settings.** Installing
  a new version cleared the paired machine, the Wi-Fi credentials and every
  setting — even when "Erase device" was left unchecked — and there was no way
  to avoid it. The published image was a single blob covering the whole start
  of the flash, and the unused space it padded along the way happened to be the
  partition those settings live in, so writing it scrubbed them every time. The
  flasher now installs the same firmware in pieces that step around that
  partition, so an upgrade keeps everything and a deliberate "Erase device"
  still starts you clean. Flashing the attached `.bin` by hand with `esptool`
  is still a full image, and still clears settings — that's what it's for.

## v0.5.1

### Features

- **History shows how each shot was run.** Every recorded shot now carries a
  tag — a bolt for an **Auto** shot (the wired paddle stopped it at weight) or
  an eye for a **Detect**ed one (inferred from the scale alone) — in its own
  column in the shot list, on the shot card, and in the web app's table. Useful
  when you switch modes: an auto shot that missed its target and a detected one
  that was never going to stop at all are different stories, and the list used
  to tell them identically. Nothing to enable, and it applies to shots you have
  already recorded — the mode was being saved to the SD card all along, it just
  wasn't shown in the list.

### Fixes

- **The web page can no longer lag behind the firmware it ships with.** The
  History page a board serves was baked in from a pre-built file kept in the
  repo, so it was only ever as current as the last time someone remembered to
  regenerate it by hand — a page could go out with an older version of itself
  than the firmware around it. It is now rebuilt from source as part of every
  firmware build and every release, so what you flash is what you get.

## v0.5.0

### Features

- **The machine chimes when it's up to temperature** *(boards with a speaker —
  the 4.3C and the P4s)*. Switch it on, walk away, and a short rising chime
  tells you it's ready. It marks the *end of a warm-up*, so it sounds **once**
  and then stays quiet through all the small dips and reheats that hold
  temperature. It re-arms when a new warm-up is genuinely coming: the machine
  goes to standby (or off, or disconnects), the steam boiler is switched on or
  off, or a boiler falls 10 °C below its set point. With the steam boiler off
  the chime follows the coffee boiler alone; with it on, both have to be there.
  - **Settings → Micra → Controls → Chime volume** — Off / 25 / 50 / 75 / 100 %,
    default 50 %. Each tap plays a note at the new level, so you can set it by
    ear.

### Improvements

- **Settings → Micra → Settings is now Settings → Micra → Controls** — a
  "Settings" page inside Settings read poorly, and the page is the machine's
  controls: brew temperature, steam boiler, wired paddle.
- **Auto connect moved to Settings → Micra → Bluetooth**, next to the saved
  machine it applies to, where a scan can no longer push it off the screen.

### Fixes

- **"Ready" is recognised properly again.** The machine's own thermostat works
  to about 2 °C — a steam boiler set to 128 °C routinely settles at 127 °C and
  stops heating there — but the display insisted on 0.75 °C and so could show
  **Heating** indefinitely on a machine that was long since ready. Anything
  within 2 °C of the set point now counts as ready. (This is also what the new
  chime waits for, so on affected machines it would never have fired at all.)
- **Opening a shot in the web app is fast** — a fraction of a second instead of
  seven or eight, on the same data.
- **Loading the web page no longer starves the device.** Serving it briefly
  consumed most of the free internal memory, which showed up as display
  glitches and dropped Bluetooth traffic while a page was open.
- **The display stops falling back to slow software copies** on the P4 5" while
  Wi-Fi and Bluetooth are busy, so the interface keeps its frame rate during a
  web page load.
- **A Micra sitting connected in standby no longer floods the serial log**, which
  was costing enough time per frame to visibly stall the interface.

## v0.4.1

A bug-fix release — no new features.

### Fixes

- **Acaia scales no longer get stuck "connected" with no weight.** A link could
  sit connected and acking, and never deliver a reading. Several separate
  causes: liveness was judged on any Bluetooth traffic (which a scale keeps
  sending even when its weight events were never registered), the event
  registration itself was subtly malformed, incoming frames were mis-parsed the
  moment a fixed-length one arrived, and the handshake could be written before
  the scale was ready to hear it. The link now watches the *weight* stream
  specifically, re-handshakes when it stalls, and reconnects if it stays silent.
  Connecting is also faster to first reading.
- **Acaia weights could read 100× high** on scales that report their decimal
  place as 0.
- **The graph smoothing setting now affects the live graph**, not just the
  frozen shot review. Previously the sweep you watch while pulling a shot
  stayed unsmoothed at every setting. *Strong* is now a wider filter than
  before; Off, Light and Medium behave as they did.
- **Shots freeze for review when the drips actually stop**, instead of after a
  fixed 3 seconds — so the final weight is a settled one. This also makes the
  learned overshoot compensation more accurate, since that's the number it
  grades itself against.
- **The stop hint (unwired mode) learns its own timing.** It shares nothing
  with the wired auto-stop now, because it also has to cover your reaction time
  between seeing the hint and flipping the paddle; one shared value had the two
  modes pulling against each other on a wired machine.
- **The 2-inch board no longer fails to start with "display init failed"** — it
  was the only board taking its drawing buffer from the same scarce internal
  memory that Wi-Fi and Bluetooth need, and the request had grown too large to
  fit.
- **An empty SD slot no longer floods the serial log.** Boards without a card
  were logging a mount failure every 5 seconds, burying everything else.

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
