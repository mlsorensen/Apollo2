# Changelog

Firmware releases. The section matching a release tag becomes that GitHub
Release's notes (see `.github/workflows/firmware-release.yml`), so keep the
heading format `## vX.Y.Z` exactly — write for someone using the machine, not
for someone reading the diff.

## v0.13.1

### Changes

- **The Dim idle screen keeps the screen on.** It now does what it did before
  the lion arrived: the backlight drops to the low level and the normal screen
  stays visible, so you can still read the machine's state from across the
  room. (It had been blanking the screen while leaving the backlight on, which
  gained nothing over Off.) The first touch still only wakes it. The dim level
  is set per board and will be tuned as each one is checked.

## v0.13.0

### Changes

- **The ESP32-P4-WIFI6-Touch-LCD-4.3 is supported again.** It's built,
  published and offered by the [web flasher](https://mlsorensen.github.io/Apollo2/)
  alongside the LCD-5, the S3-4.3C BOX and the X 8" box. It had been dropped in
  v0.12.2; if you're running one, updates work normally again from this release
  on. It is the **same board as the LCD-5** — same processor, radio, audio,
  battery, SD card and paddle wiring — with a 4.3" 800×480 screen instead of a
  5" 1280×720 one. Choose between the two on screen size and a small price
  difference; nothing else about them differs.
- **Finer control over the warm-up chime volume.** The four fixed levels (25 /
  50 / 75 / 100 %) are replaced by a slider with **–** and **+** buttons either
  side: drag to get close, tap to nudge 5 % at a time, and either way it plays a
  note at the new level so you can set it by ear. Just as importantly the scale
  is now even *to the ear* — before, everything above about 40 % sounded much
  the same and the quiet end jumped in big steps, because the setting was
  applied directly as signal strength rather than as loudness. **Your existing
  setting is carried over at the same volume**, so nothing changes how your
  machine sounds: if it was on 50 % it now reads 80 % and is exactly as loud as
  before.
- **Removed "Restart display" from Settings.** It was an escape hatch for the
  rare display glitch that was root-caused and fixed in v0.12.3, so there is
  nothing left for it to fix. The automatic re-alignment after every boot is
  unchanged.

## v0.12.3

### Fixes

- **The 4.3C's ghosted screen is fixed.** Since the first S3 boards, the
  display would occasionally latch into a corrupted state: roughly every tenth
  row flickering with content from the row band above, the picture sitting a
  few pixels high with the top wrapping to the bottom, sometimes from the
  moment it powered on, and only a restart or luck would clear it. The cause is
  a bookkeeping bug in the display driver Apollo is built on: whenever
  something holds up the panel's refill interrupt for more than half a
  millisecond (writing settings to flash is enough), the driver loses track of
  which of its two line buffers to fill next and never recovers. Apollo now
  corrects that every frame, so the same event costs one barely visible blip
  instead of a stuck screen. Verified on hardware both ways.
- **Settings > Restart display no longer reboots the 4.3C.** In v0.12.2 the
  button crashed the board instead of restarting the display (a side effect of
  the memory reclaim). It restarts the panel again, and with the fix above you
  should rarely need it.

- **The screensaver is fast again on the P4 boards.** v0.12.0 sped the
  bouncing lion up on the 4.3C but slowed it down on the P4-5 and the X boxes
  (about 12 fps at 93% CPU on the 5", from 30 fps at 75% before). The logo is
  now pre-rendered at each screen's own size, so every board draws it directly
  instead of scaling it — smoother on all of them, and the P4s are back above
  where v0.11 left them.

## v0.12.2

### Changes

- **Fixed the 4.3C gradually losing its network features.** On a machine with a
  scale paired and an SD card in, the remote could reach a state where checking
  for updates failed, the log page wouldn't load, and the WiFi/token setup portal
  would advertise but refuse to let a phone join it — all at once, and only
  clearing on a restart. The board had run out of a specific kind of internal
  memory that the radio, the display and encrypted connections all draw from.
  Apollo now uses roughly 26 KB less of it, which restores all three.
- **Fixed the "Check for updates" setting rendering in a smaller font** than the
  buttons above it in Settings > Device > WiFi.
- **The supported board line-up is now three boards.** Firmware images are
  built and published for the **ESP32-P4-WIFI6-Touch-LCD-5**, the
  **ESP32-S3-Touch-LCD-4.3C / 4.3C-BOX**, and the
  **ESP32-P4-WIFI6-Touch-LCD-X 8" box** — and those are what the
  [web flasher](https://mlsorensen.github.io/Apollo2/) offers. The P4-5 is the
  recommendation for mounting on the machine; the 4.3C is there for anyone who
  would rather not 3D-print a case or make up a paddle cable (it's the only
  supported board that's a finished box *and* has the opto-isolators built in);
  the X 8" is the counter-top companion.
- **Other boards are no longer published.** The S3 2", 4.3B and 7B, the bare
  P4 4.3, and the X 7" and 10.1" have been dropped from releases and from the
  web flasher. Nothing was removed from the source: every one of them still
  builds from this repo exactly as before, and their pin/panel definitions,
  build environments and printable shells are all still here. If you run one,
  **stay on v0.12.1 or build from source** — and note that your device will
  keep announcing new versions it cannot install, because the update check
  doesn't know which boards have published images. Declining the update, or
  turning the notices off under Settings > Apollo > WiFi, is the right move.
  A failed install is harmless: the device rolls back to the running firmware.
- **New printable backplate for the X 8" box**, so it mounts to the same
  counter-top stand as the other boards — see `hardware/3d-prints/`.

## v0.12.1

### Changes

- **Fixed "Check for updates" disappearing.** The button on Stats > Info and the
  **Check for updates** setting (Settings > Device > WiFi) would both vanish
  until the next restart, seemingly at random. They were being dropped whenever
  the screen layout rebuilt — which happens when you change the theme, or when a
  scale connects or disconnects. Automatic checks were still running the whole
  time; only the controls were missing.
- **The Micra's greyed-out "Connect" button is now a "Set up" button.** When a
  machine isn't paired yet, the button did nothing and didn't say what to do
  instead. Tapping it now takes you straight to Settings > Micra > Bluetooth. The
  status text is plainer too: **Not set up** (no machine chosen) and **Setup
  unfinished** (chosen, but pairing never completed) replace "Set up in Settings"
  and "Token needed".
- **Added a Dim option to the idle screen**, so there are now three: **Logo**
  (dimmed backlight with the bouncing lion), **Dim** (same dimmed backlight, but
  a plain black screen), and **Off** (backlight fully off). If you were using
  "Blank" before, you're now on "Off" — the same behaviour, clearer name.
- **Renamed two Display settings to match what they do.** "Screen dim" is now
  **Screen timeout** (it sets the idle time, not the brightness), and
  "Screensaver" is now **Idle screen**.
- **Added an Hourly update check** alongside Daily. Like Daily, it only runs
  while the machine is idle and the idle screen is up, so it never interrupts
  you.
- **The log page no longer freezes the display for as long.** Opening it showed
  the entire log, which locks the screen while it's sent. It now shows the most
  recent portion by default, with a link to the full log when you need it.
- **The machine keeps more log history.** Two messages were repeating far more
  than they needed to — the "no SD card" note and a line for every screen tap —
  which pushed older entries out. Quieting them means a problem from earlier in
  the day is more likely to still be recorded.

## v0.12.0

### Changes

- **Fixed the S3 boards falling off the network after every boot.** For about
  90 seconds after starting up, an S3 remote would get an IP address and then
  be unreachable — no clock sync, no update check, and the web page wouldn't
  load — before recovering on its own. The cause was the automatic update check
  starting before the network was actually usable: it then tied up memory the
  WiFi radio needed to receive anything, so it blocked the very connection it
  was waiting for. Apollo now waits for a confirmed internet clock sync before
  checking, so the check runs against a working connection and finishes in about
  a second. (This is what v0.11.4 was meant to fix; that change addressed a
  different theory and didn't solve it. The P4 boards were never affected.)
- **Fixed only the first update check of each session working.** After the check
  that runs at startup, every later check — including the daily one — was being
  refused for lack of memory, silently. The limit it was measured against was
  set too conservatively; it's now based on what a check actually uses.
- **Fixed the Logo screensaver going completely black on the 4.3C.** The dimmed
  backlight was being set below what that panel can show, so an idle machine
  looked switched off rather than asleep — and tapping it seemed to do nothing.
  The dim level is now set per board.
- **The screensaver is far smoother and uses much less power.** It was redrawing
  the shot graph hidden underneath it on every single frame. It now leaves the
  hidden graph alone, which makes the bouncing logo about four times faster and
  frees up processing for everything else. Shot recording is unaffected — a shot
  that starts while the screen is asleep is still captured in full.
- **Added a 1-minute option to Screen dim** (Settings > Display), alongside the
  existing 5, 15 and 30. A shot in progress never counts as idle, so the
  screensaver won't cover a live shot however short the timeout is; the countdown
  restarts when the shot finishes.

## v0.11.4

### Changes

- **Fixed unreliable WiFi on the S3 boards.** The S3's WiFi was going to sleep
  between beacons and dropping incoming packets even with a strong signal — which
  showed up as spotty connectivity, the clock rarely syncing over the internet,
  and "Check for updates" failing with "couldn't check". Apollo now keeps the
  S3's WiFi receiver awake while connected, so time sync and update checks work
  reliably. (The P4 boards were never affected.)

## v0.11.3

### Changes

- **More reliable clock sync, and you can see it.** After connecting to WiFi,
  Apollo now keeps retrying the time sync every ~30 s until it actually lands
  (instead of possibly waiting up to an hour on a missed first try), then settles
  to an hourly refresh. **Stats → Info** shows an honest **NTP synced** line —
  "2m ago", or "not since boot" if it hasn't synced yet — so you can tell at a
  glance whether the clock is really coming from the internet.

## v0.11.2

### Changes

- **Quieter, steadier update checks.** While the screensaver is on (machine
  idle), Apollo now pauses its Bluetooth reconnect attempts — no more pointless
  retrying when nobody's using the machine — and it runs the once-a-day update
  check in that idle window, when memory is freest, so the check is reliable and
  never gets in the way during use. An already-connected machine or scale stays
  connected. The startup check also stops retrying within about a minute so it
  can't linger.

## v0.11.1

### Changes

- **The update notice now tells you how to turn it off.** The "Update available"
  screen adds a line pointing to **Settings → Apollo → WiFi** (set **Check for
  updates** to **Off**) if you'd rather not be prompted.

## v0.11.0

### Changes

- **Apollo can update itself, right on the device.** With WiFi on, it checks
  the releases site and shows a notice when a newer firmware is out — with the
  version and release notes, and **Install now / Remind me later / Skip this
  version**. Install now downloads the new firmware and installs it: a progress
  bar, then a short "screen goes dark ~20 seconds, don't power off" phase, then
  it restarts on the new version. Your settings, paired machine and shot
  history are kept, and a firmware that won't boot rolls back on its own. The
  [web flasher](https://mlsorensen.github.io/Apollo2/) is still there for
  recovery and for the P4 boards' first flash.
- **Choose how it checks: Off / On boot / Daily.** Under **Settings → Apollo →
  WiFi → Check for updates**. "On boot" checks once each startup; "Daily" also
  re-checks about once a day while the machine stays on. You can always check
  by hand from **Stats → Info → Check for updates**; if it can't reach the
  server it says so instead of claiming you're up to date.
- **Steadier update checks.** A brief Wi-Fi/DNS hiccup during a check now
  retries instead of showing a spurious "couldn't reach the server" or a blank
  release-notes box.
- **Faster, more reliable Wi-Fi on the P4 boards.** On first boot Apollo
  updates the board's Wi-Fi co-processor to the matching firmware (a brief
  one-time "Updating Wi-Fi co-processor" screen), which fixes a stale-factory
  quirk that slowed every connection. One-time and automatic.
- **Settings → Device is now Settings → Apollo** — "Device" was ambiguous
  next to the machine and the scale; the page is about this device, Apollo.

## v0.10.0

### Changes

- **Screensaver enhanced** — the idle screen can now show a bouncing image
  over the dimmed display, or blank the screen entirely. Pick under
  **Settings → Device → Display → Screensaver** (Logo / Blank). **Screen
  dim** also gains a 5‑minute option.

- **X‑series 8" image fixed.** The previous 8" image was built for the older
  ESP32‑P4 silicon revision and could not boot the rev 3.0+ chips these boxes
  actually ship with (the board looked dead — no screen, no serial). The 8"
  image now targets rev 3.0+ silicon and mounts the picture the right way up
  for the 8" enclosure. (The 7" image is unchanged — 7" units in the wild are
  the older revision.)

## v0.9.0

### Changes

- **Two new boards: the Waveshare ESP32‑P4‑WIFI6‑Touch‑LCD‑X finished boxes
  (7" and 8").** All‑in‑one enclosed displays — no shell to print — with the
  same fast ESP32‑P4 internals as the 4.3/5, bigger screens (7" 1280×720,
  8" 1280×800), and the same optional Auto‑shot paddle wiring on GPIO 51/52.
  Each size takes its own image from the web flasher. The 7" is verified on
  hardware; the 8" is untested until one is on the bench.

- **Bookoo Themis Ultra works** — it speaks the same protocol as the Themis
  Mini and is recognized automatically (weight, tare, battery, beep and
  auto‑off). The **Beep** row on both scales now offers Off / 1–3, matching
  the Bookoo app: the out‑of‑range levels the wire accepts actually play
  *quieter* than level 3 (previously we offered 1–4).

- **Scan-to-set-up.** The token and Wi‑Fi setup screens now show a QR code:
  scan it with your phone's camera to join the `Micra-Setup` network, and the
  setup page opens by itself (the device now answers as a captive portal, like
  a hotel Wi‑Fi sign-in page). Joining manually and opening
  `http://192.168.4.1` still works exactly as before.

## v0.8.0

### Changes

- **Token setup is now always manual entry.** The old "read the token from the
  machine in pairing mode" path never actually worked on current machine
  firmware — that Bluetooth characteristic holds a pairing *seed*, not a readable
  token — so it has been removed. After you pick your machine, enter its token
  (get it with the **LM Token** app) over the `Micra-Setup` Wi‑Fi page. Existing
  tokens and settings are untouched.

### Fixes

- **`Micra-Setup` Wi‑Fi wouldn't accept connections while on your home Wi‑Fi.**
  Opening the token / Wi‑Fi setup page while the device was connected (and serving
  the History web app) left the `Micra-Setup` network visible but impossible to
  join. The device now cleanly drops the home connection before starting the
  access point, and reconnects when the setup page closes.
- **Switching shot mode now cancels an in-progress shot.** Changing the shot mode
  (Manual / Auto shot / Shot detect) while a shot timer is running resets it to
  idle and drops the drive line, so a timer can't be left ticking under the wrong
  mode.

### Features

- **"Machine is in pairing mode" prompt.** If the machine is left in
  configuration/pairing mode, the device now tells you to restart it instead of
  silently looping on a failed connection.

## v0.7.1

### Fixes

- **Spontaneous reboot on SD‑card hiccups (P4 boards).** A mount retry or an
  unmounted/removed SD card could — after enough cycles — tear down the SDMMC
  peripheral the WiFi/BLE co‑processor shares (an IDF slot‑refcount
  over‑decrement), crashing the radio and rebooting the device. Diagnosed
  from a captured coredump; the SD driver can no longer deinitialize the
  shared host under any circumstances.
- **Nuisance ready chimes.** The warm‑up chime now sounds at most **once per
  turn‑on**. Heavy steaming, refills, or steam‑boiler toggles no longer
  re‑arm it mid‑session; standby/off/disconnect still does, so the next
  warm‑up chimes as before.

### Features

- **Crash dumps are downloadable.** If the firmware crashes, the automatic
  post‑mortem snapshot it saves to flash is now surfaced: the boot log says
  `crash dump stored`, and a **Crash dump** link appears on the web page
  (`http://⟨device IP⟩/coredump`; `?erase=1` clears it) to download it as a
  file to attach to a bug report. The filename is a fingerprint of the build
  that crashed — correct even if the device was upgraded since — and each
  release now ships a debug‑symbols archive whose `.appsha` files match
  fingerprints to the right decoding `.elf`.

## v0.7.0

### Features

- **Scale device settings.** Settings stored on the scale itself — beep,
  auto-off, and more — are now adjustable from the remote, on a new
  **Settings → Scale → Device settings** page. The rows adapt to the
  connected scale:
  - **Bookoo Themis** — beep level (Off / 1–4) and auto-off (5–30 min).
  - **Acaia Umbra** — beep on/off and auto-sleep timer, plus **Unit**
    (g / oz).
  - **Acaia Lunar / Pyxis** — beep toggle (the row shows the volume stored
    on the scale), auto-sleep, **Unit** (g / oz), and the current weighing
    mode (shown for reference — Acaia's protocol can't change modes
    remotely).
  - Rows show the scale's live values while connected, with a **Connect**
    shortcut right on the page when it isn't. Switching an Acaia to ounces
    only changes the scale's own display: Apollo keeps working in grams, so
    brew-by-weight targets stay correct.
  - The old Scale → Settings page split into **Shot settings** and
    **Device settings**, so neither page needs scrolling.
- **Varia Aku support.** The Aku (Micro/Mini/Pro) joins Bookoo and Acaia as
  a supported scale: live weight, tare, and a battery readout — including
  its battery frames, which aren't in any public protocol documentation.
  Its gauge reports in 20% steps, so the battery icon turns red at its
  final step before empty (each scale model now sets its own low-battery
  threshold).

### Fixes

- **Tare and Disconnect lock during a shot.** Both scale-card buttons (and
  the new device-settings rows) disable while a shot is running or
  settling — a mid-shot tare would corrupt the auto-stop math — and
  re-enable the moment the shot ends, however it ends.
- **Scale settings changes stick.** Acaia scales silently drop Bluetooth
  writes that arrive in quick bursts; setting changes are now paced the way
  Acaia's own app paces them, confirmed against the scale's reported state,
  and retried when the scale ignores one.
- **Scale scanning is more reliable.** The scan window doubled to 10
  seconds — scales that advertise infrequently (the Aku) could slip through
  the old window while the radio was also servicing the Micra connection.

## v0.6.2

### Features

- **A choice of ready chimes.** New **Chime melody** setting
  (**Settings → Micra → Controls**) with a handful of tunes to choose from —
  tap to cycle through them and hear each one — plus a **Random** option
  that plays a different tune each time the machine finishes warming up.
  The existing Chime volume setting keeps controlling the level, and `Off`
  is right there in the cycle.

## v0.6.1

### Fixes

- **The power button acknowledges the tap instantly.** The Micra reports a
  mode change a couple of seconds after the command, so tapping
  `Standby`/`Turn on` looked like nothing happened — and invited a second,
  state-undoing tap. The button now immediately disables and reads
  `Working...` until the machine reports the change (or the link drops, or
  8 seconds pass — it can never stay locked).

## v0.6.0

### Features

- **Shot detect shows the real shot the moment it's detected.** Detection
  confirms a few seconds after the first drip; until now the screen kept the
  generic live graph and the raw scale weight (cup included) for the whole
  shot, and only the end-of-shot review showed the aligned picture. Now, the
  instant a shot is detected, the graph switches to the shot plot back-filled
  from history — the ramp you already poured, from the estimated start — and
  the weight readout drops to shot grams, matching what a wired shot shows.
- **No more accidental untracked shots.** In Auto shot and Shot detect modes,
  flipping the paddle while no scale is connected now leaves the machine off
  and shows a message ("connect the scale or switch to Manual mode") instead
  of silently running a shot with no timing, tracking, or auto-stop. Waking a
  standby machine with a paddle flip is unaffected, and Manual mode plus the
  Flush button still cover deliberate untracked runs.
- **Four new themes matching the Micra body colors.** Like the existing
  Ferrari theme for the red machine, each runs its machine's paint through
  the whole scheme: **Modena** (yellow), **Gulf** (light blue, with the
  livery's orange as the heating color), **Monaco** (navy), and **Ivory**
  (creme). Settings → Device → Display → Theme.
- **The ready chime carries better.** The warm-up chime is now the "Buddy
  Holly" intro riff — all of it above 650 Hz, where the small speaker actually
  has output; the old arpeggio's low notes faded a room away.
- **New "Detect lead-in" setting** (**Settings → Scale → Settings**, 0–10 s,
  default 3 s): how far before the first drip your shot actually starts —
  the paddle flip, then preinfusion water working through the puck. It's
  added when back-dating a detected shot, so the timer and graph line up
  with the moment you flipped the paddle. Adjust it to match your machine's
  preinfusion setting as needed; detected shot durations now include it.

## v0.5.3

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
