# Apollo 2 user manual

Everything the firmware can do, screen by screen and setting by setting. For
flashing and first‑time pairing, start with the [README](README.md#getting-started).

Some rows only appear where the hardware supports them — a switch you don't see
(for example **Wired paddle** on a board without paddle wiring, or
**Brightness** on a board whose backlight can't dim) simply doesn't apply to
your board.

---

## Home

The Home screen adapts to the screen size and to whether a scale is paired.

### MICRA card

- **Status** — `Set up in Settings` (nothing paired yet), `Token needed`,
  `Disconnected`, `Connecting...`, `Heating` (powered on, boilers still coming
  up to temperature — the dot pulses amber), `Ready` (at temperature), or
  `Standby`. The Micra itself doesn't report a warm‑up state; `Heating` is
  inferred from the live boiler temperatures vs their set‑points. A boiler
  within **2 °C** of its set‑point counts as ready, because the machine's own
  thermostat works to about that tolerance — a steam boiler set to 128 °C
  routinely settles at 127 °C and stops heating there. Boards with a speaker
  can also chime when it turns `Ready` — see
  [Settings → Micra → Controls](#controls).
- **BREW / STEAM** — live boiler temperatures. On larger screens each has a
  **−/+** stepper that edits the set‑point directly; the small grey number is
  the current target. Edits are written to the machine as you tap.
- **Power button** — `Standby` / `Turn on` when connected. After a tap it
  briefly reads `Working...` (disabled) until the machine reports the change —
  the Micra's answer trails the command by a couple of seconds, and the button
  would otherwise look like it did nothing. When the machine is configured but
  disconnected it becomes **Connect** and starts the Bluetooth link.
- **Flush** *(paddle‑wired boards, screens 4.3" and larger)* — runs the group
  for a quick rinse, for the same time as **Auto flush** (3 s when Auto flush
  is Off). Tap again while it runs to stop early. Greyed out while a shot is
  in flight or the machine is in standby.

### SCALE card (when a scale is paired)

- **Status** — `Connected`, `Connecting...`, or `Sleeping` (sleep‑capable
  scales such as the Acaia Umbra with the link switched off). A small battery
  icon shows the scale's own battery where the scale reports it.
- **WEIGHT** — live reading. During a finished shot's review it freezes at the
  net shot weight so lifting the cup doesn't wipe the number you care about.
- **TARGET** — the brew‑by‑weight stop target in grams (**−/+** on larger
  screens; also under **Settings → Scale → Shot settings → Target**).
- **TIMER** — the shot timer. Runs from the ESP's own clock whenever the
  firmware is the shot authority (Auto shot or Shot detect, and every wired
  shot); in Manual mode without a wire it falls back to the scale's built‑in
  timer.
- **Shot mode pill** (under the timer) — tap to cycle the shot mode:
  - **Auto shot** *(only with a wired paddle — see below)*: flipping the
    Micra's paddle starts the shot (auto‑tare, timer, graph), and the firmware
    opens the paddle circuit by itself when the weight reaches
    *target − overshoot*. The overshoot compensation is learned from each
    auto‑stopped shot's settled weight, so accuracy improves over the first
    few shots.
  - **Shot detect**: no wiring needed — the shot is detected from the weight
    stream alone (start when flow sustains, stop when flow ceases). There is
    no auto‑stop; instead the pill flashes **Stop** when the shot reaches the
    point where the auto‑stop would have fired, telling you to flip the
    paddle now. Detection confirms a few seconds after the first drip; the
    moment it does, the timer, weight and graph snap to the full shot —
    back‑dated to the estimated start, including the **Detect lead‑in**
    (see **Settings → Scale → Shot settings**).
  - **Manual**: nothing armed. A wired board still relays the paddle and times
    the shot; otherwise the scale's own timer is shown.
  - While a finished shot is frozen for review the pill reads **Reset** —
    tap it to dismiss the review and return to live monitoring.
  - The pill is disabled while a shot is running or settling.
- **Tare** — zeroes the scale. (Tares are sent twice under the hood; some
  scales drop single tare commands.)
- **Disconnect / Connect** — drops or re‑establishes the scale link, e.g. to
  save the scale's battery.
- Like the shot‑mode pill, **Tare** and **Disconnect** are disabled while a
  shot is running or settling — either would disturb the weight the stop math
  runs on. They re‑enable the moment the shot is over, however it ends.

### Flow graph

Plots the live flow rate (or weight) from the scale.

- Tap the **g/s** chip to switch between flow rate (g/s) and weight (g).
- During a shot the graph restarts and follows the shot; when the shot ends it
  freezes for review (see **Review hold**) and then resumes.
- The graph style (oscilloscope sweep vs scrolling) and smoothing are set under
  **Settings → Scale → Shot settings**.

### Shot lifecycle notes (all modes)

- A run shorter than **8 seconds** never becomes a "shot" — it's treated as a
  flush/rinse and discarded silently. Shot detect additionally requires a few
  grams of net gain.
- **Auto shot and Shot detect refuse to start without the scale.** Flipping
  the paddle while the scale isn't connected leaves the machine off and pops a
  message — connect the scale (or wake it) or switch to **Manual** mode. This
  catches the "flipped the paddle, then noticed the scale was never set up"
  shot. Waking a standby machine with a paddle flip still works (no water
  moves), and deliberate untracked runs remain available via Manual mode or
  the **Flush** button.
- On wired boards, flipping the paddle ON **while a review is frozen** is
  swallowed (the machine does not start) and the **Reset** pill flashes —
  dismiss the review first, then start the next shot. This prevents an
  expected auto shot from silently running as a manual one.

---

## Settings → Micra

### Bluetooth

- **Scan** — searches for La Marzocco machines. Scanning takes priority over
  in‑progress background connections (a scale stuck in `Connecting...` no
  longer blocks it). If nothing is found the status line says so — make sure
  the machine is powered on and in range, then scan again.
- **Saved machine row** — shows the paired machine with:
  - **Setup** (only until a token is stored) — starts the token entry flow
    (see the [README](README.md#3-enter-a-token-manually-only-if-step-2-didnt-auto-connect)).
  - **Connect / Disconnect** — manual link control. The Micra accepts only one
    Bluetooth client, so Disconnect frees it for another remote or the phone
    app.
  - **Forget** — clears the machine, its name, and its token.
- **Auto connect** *(default on)* — connect to the saved machine automatically
  at power‑up. Turn off if another controller needs the Micra's single
  Bluetooth slot.

### Controls

- **Wired paddle** *(paddle‑capable boards; default off)* — tell the firmware
  the paddle harness is physically wired. This enables the **Auto shot** mode
  and the auto‑flush. Off, the board behaves like an unwired one (Shot detect
  / Manual only). Flipping it mid‑shot cancels the shot.
- **Chime melody** *(boards with a speaker; Off / Blue / Pink / Human /
  Gold / White / Autumn / Random, default Blue)* — which tune announces the end of a warm‑up. Each tap
  cycles to the next melody and auditions it; **Random** picks a different
  tune each time the machine warms up; `Off` silences the chime regardless of
  the volume below.
- **Chime volume** *(boards with a speaker; Off / 25 / 50 / 75 / 100 %, default
  50 %)* — the warm‑up chime's level, so you can walk away and be called back.
  Each tap cycles to the next level **and plays a note at it**, so you can set
  it by ear; `Off` silences it.

  The chime marks the *end of a warm‑up*, not every moment the boilers are at
  temperature, so it sounds **once** and then stays quiet through all the small
  dips and reheats that hold the temperature. It re‑arms when a new warm‑up is
  genuinely coming: the machine goes to standby (or off, or disconnects), the
  steam boiler is switched on or off, or a boiler falls 10 °C below its set
  point. With the steam boiler **off** the chime follows the coffee boiler
  alone; with it **on**, both boilers have to be there.
- **Brew → Temperature** — coffee boiler set‑point stepper (0.1 °C steps;
  long‑press for 0.5 °C).
- **Steam Boiler → Enable** — steam boiler on/off.
- **Steam Boiler → Temperature** — one of the Micra's three steam levels
  (shown as Level 1–3 with the temperature underneath).

### Cleaning *(paddle‑capable boards)*

Everything that deliberately runs water through the group. The whole page
needs the paddle harness, since that's what drives it.

- **Auto flush** *(Off / 3 / 6 / 9 / 15 s; default Off)* — after a finished
  shot, when the scale sees the cup lift off, the firmware waits (see **Flush
  delay**) and then runs the group for this long to rinse the puck's surface.
  Any paddle activity, a new shot, or the machine being in standby cancels it.
  **This one duration drives every timed group run**: the auto‑flush, the Home
  **Flush** button, and each backflush pulse. With Auto flush Off the other two
  fall back to 3 s.
- **Flush delay** *(shown while Auto flush is on; 3 / 6 / 9 / 15 s; default
  3 s)* — the pause between the cup coming off and the flush running.
- **Backflush cleaning** — opens a full‑screen cleaning mode. Fit the blind
  filter with detergent, tap **Go**, and the device pulses the group **10
  times: ⟨Auto flush⟩ seconds on, 3 seconds off**, showing the cycle count, a
  countdown, and the total up front. **Cancel** stops the sequence and stays on
  the screen so you can run it again; **Back** stops it and leaves. Flipping
  the physical paddle also stops it — you always outrank the automation. When
  it finishes, rinse the basket and run it again with plain water to clear the
  detergent. Greyed out unless the machine is on and no shot is in flight.
  - **Preinfusion/prebrew matters here.** Each pulse starts a fresh brew, so
    the machine preinfuses *every time*. A pulse shorter than your preinfusion
    never engages the pump, so it runs at line pressure only — water still
    flows and it still rinses, just with far less force than a pump‑pressure
    backflush. If that's not what you want, either raise **Auto flush** until
    the pulse comfortably outlasts preinfusion, or turn preinfusion off while
    you backflush.

---

## Settings → Scale

### Bluetooth

- **Scan** — searches for supported scales (Bookoo Themis, Acaia Umbra /
  Lunar / Prochef / Pyxis, Varia Aku — Pyxis and Aku untested). Wake the scale first — most
  sleep their Bluetooth quickly.
- **Saved scale row** — **Connect / Disconnect** and **Forget** (no token
  needed for scales).

### Shot settings

- **Target** — brew‑by‑weight stop target, 5–120 g. Also editable from Home on
  larger screens.
- **Review hold** *(5–120 s, default 30 s)* — how long a finished shot's frozen
  graph and weight linger before auto‑dismissing. **Reset** dismisses early.
- **Detect lead‑in** *(0–10 s, default 3 s)* — Shot detect mode only. The
  detector notices a shot at the first weight rise, but the shot started
  earlier: you flipped the paddle, then preinfusion water worked through the
  puck before the first drop landed. This offset is added when back‑dating the
  shot start, so the timer and graph line up with the moment you actually
  started the shot. Adjust it to match your machine's preinfusion setting as
  needed (roughly your typical paddle‑to‑first‑drip time); 0 starts the shot
  at the first drip.
- **Smoothing** *(Off / Light / Medium / Strong, default Light)* — smoothing on
  the shot graph's line. Purely visual; detection and auto‑stop use the raw
  stream.
- **Drop negative g/s** *(default on)* — clamps negative flow readings (cup
  bumps, scale noise) to zero on the graph.
- **Oscilloscope graph** *(default on)* — the shot graph sweeps left→right and
  wraps, oscilloscope style. Off, it scrolls continuously instead.

### Device settings

Settings stored on the scale itself, adjusted over Bluetooth. Which rows
appear — and their value choices — depends on the scale model:

- **Bookoo Themis** — **Beep** (buzzer level Off / 1–4) and **Auto‑off**
  (5–30 min). The scale also accepts a level 5, but its firmware plays it
  *quieter* than 4, so it isn't offered; a 5 set from the Bookoo app shows
  as `--`.
- **Acaia Umbra** — **Beep** (On / Off; also silences its tare/timer chirps),
  **Auto sleep** (Off / 1–30 min), and **Unit** (g / oz). The Acaia app can
  additionally set *power‑off* timers; those aren't offered here (a
  powered‑off scale can't be woken over Bluetooth) and show as `--` if set
  elsewhere.
- **Acaia Lunar / Pyxis** — **Beep** (volume Off / 1–3 — the Acaia app only
  offers on/off, but the scale itself stores a volume), **Auto sleep**
  (Off / 5–60 min), **Unit** (g / oz), and **Mode** — the scale's current
  weighing mode, shown for reference only (the Acaia protocol has no way to
  change mode remotely; use the scale's buttons).

**Unit** only changes what the *scale's own display* shows — Apollo always
works and displays in grams (an oz‑mode Acaia streams ounces over Bluetooth;
the firmware converts them back, so brew‑by‑weight targets stay correct).
Changed values may take a moment to be confirmed by the scale; if a change
doesn't stick, the row snaps back to what the scale reports.
- **Varia Aku** — none (its protocol only streams weight).

The scale owns these values: the rows show what it reports, so they read `--`
until the scale is connected (a **Connect** prompt appears right in the group
when it isn't — no scale paired yet, the group points you to Bluetooth
instead). Tap a value to advance to the next choice; the write goes to the
scale immediately and sticks like any change made from the scale's own
buttons. Like Tare, these rows are disabled while a shot is running.

### Per‑scale nuances & recommended daily workflow

The supported scales handle power and sleep differently, so the smoothest
end‑of‑day / morning routine differs by model. In every case, if the Micra has
an auto‑on schedule, the scale step below is the *only* thing you do — the
machine is already warming up when you arrive.

- **Acaia Lunar** — stays running for as long as Bluetooth is connected (it
  won't auto‑sleep on you mid‑connection). Recommended routine: just **power
  the scale off** when you're done and **power it on** in the morning — Apollo
  reconnects to it automatically.
- **Acaia Umbra** — can be used exactly like the Lunar, but it also has a real
  sleep mode that Apollo can wake it from. Recommended routine: tap
  **Disconnect** on Apollo's scale card when you're done (the Umbra won't
  sleep *while connected*, so disconnecting is what lets it doze — the card
  then shows `Sleeping`), and tap **Connect** in the morning, which wakes the
  Umbra over Bluetooth. The Umbra's battery lasts about a **week** in sleep
  mode — and once it's flat, Connect naturally can't wake it, so put it on a
  charger if it's been sitting.
- **Bookoo Themis** — has a sleep mode, but it genuinely powers down: Apollo
  **cannot** wake it with Connect. Treat it like the Lunar: leave Apollo
  connecting, **turn the scale on** in the morning and you're good; when
  you're done, just walk away — the scale sleeps on its own from inactivity.

---

## Settings → Device

Three short sub‑pages — **Display**, **Time & date**, and **WiFi** — so each
page fits on screen with little to no scrolling.

### Display

- **Brightness** *(dimmable boards)* — backlight level.
- **Screen dim** *(Off / 15 min / 30 min, default 30 min)* — after this idle
  time the screen dims to 5 % (or switches off where the backlight can't dim);
  any touch restores it.
- **Theme** — tap to cycle the color scheme: Midnight, Graphite, Espresso,
  Nord, Solarized, Plum, Forest, Rose, Mono (black & white), Contrast (high
  contrast), Ferrari, Sunset, Citrus, Modena, Gulf, Monaco, Ivory. The last
  four (plus Ferrari) are companions to the Micra body colors — the machine's
  paint as the accent: Ferrari red, Modena the yellow Micra, Gulf the light
  blue, Monaco the navy, Ivory the creme.
- **Fahrenheit** *(default off)* — display unit for temperatures. Set‑points
  are still stored in Celsius.
- **Button sounds** *(boards with a speaker; default on)* — click on button
  presses. (The other sound, the warm‑up chime, is under
  [Settings → Micra → Controls](#controls) since it's about the machine.) With
  button sounds off *and* the chime volume `Off`, the audio hardware stays
  powered down until the next restart.
- **Performance overlay** *(default off)* — LVGL FPS/CPU overlay, for
  debugging.

### Time & date

- **Time** — hour and minute pickers, to set the clock by hand. (With Wi‑Fi +
  NTP the clock sets itself on every boot; alternatively, boards with an RTC
  and an optional coin cell installed keep time through a power‑off.)
- **Date** — month / day / year pickers. NTP fills the date in automatically;
  without NTP a real date is needed for features that stamp records (shot
  history). Setting only the time leaves the date unset.
- **24‑hour** *(default on)* — clock format.
- **Timezone** — city picker (handles daylight saving). Governs how all
  times display and how NTP time is converted.

### WiFi

- **Enable** *(default off)* — join your home network. Used only for NTP time
  sync; all machine/scale control is local Bluetooth.
- **Status** — `Off`, `Connecting`, or `Connected` with the IP address.
- **Set up WiFi** — starts the device's own access point (`Micra-Setup`) and
  setup page for entering credentials (same page as token entry).
- **Forget** — clears the saved network.
- **Auto time (NTP)** *(default on)* — sync the clock over WiFi while
  connected. (The timezone it applies is set under **Time & date**.)

### Root page

- **Restart display** — escape hatch for the rare RGB‑panel glitch where the
  image comes up (or drifts) shifted by a few pixels. On RGB boards this
  re‑aligns the panel in place — you'll see a single one‑frame hop, then a
  clean image; nothing is lost and nothing reboots. Other boards do a soft
  reboot. The firmware also re‑aligns itself once shortly after every boot.
- **Lock display for cleaning** — disables touch for 30 seconds so you can
  wipe the screen. A full‑screen countdown shows the time remaining; the lock
  ends on its own (touching the screen does nothing until then).

---

## Stats

- **Brew / Boiler** — temperature history graphs. Tap **+/−** to zoom the time
  window; the set‑point is drawn as a reference line; gaps mean the machine was
  disconnected.
- **History** — the shot log (boards with an SD‑card slot). Headline cards show
  lifetime totals: shots recorded, lifetime accuracy and 30‑day accuracy
  (actual vs target weight). Below, the shot list with a calendar filter —
  **All** plus a button for each month that has shots, so an old month is one
  tap away instead of a long scroll. Each row shows the signed miss against
  target — `36.2/36g (+0.2)` — green within 2 g, amber beyond, and how the
  shot was run: a bolt for an **Auto** shot (the paddle harness stopped it at
  weight) or an eye for a **Detect**ed one (inferred from the scale). Only
  those two modes record — a fully manual shot has no armed start/stop, so
  nothing is saved. Tap a shot for a full‑screen card with its
  result/target/diff, time, average flow, and the weight + flow graph.
  - **SD card**: any size — a **small card is more than enough**. A shot is
    a few tens of KB, so even a 1 GB card holds decades of daily espresso;
    an old card from a drawer is perfect. Use a card formatted **FAT32**
    (FAT16 also works) — small cards usually ship that way. exFAT — the
    factory format of most cards over 32 GB — and NTFS are **not**
    supported: History detects what's on an unusable card and names it
    ("This SD card is exFAT‑formatted…") instead of pretending the slot is
    empty. Reformat on a computer; the device never formats or erases a card
    on its own.
  - **Hot‑swap friendly**: no reboot needed in either direction. The device
    looks for a card every 5 seconds, so a freshly inserted card is picked
    up within ~5 s; a removed card is likewise noticed within a few seconds
    and History switches back to the insert‑a‑card guidance. If the card
    fills up, new shots stop being saved and History says so.
  - **What's recorded and when**: recorded shots are viewable immediately —
    no clock needed. Recording NEW shots additionally needs a real date/time
    (NTP, or Settings → Device) — until the clock is set the footer warns
    that shots aren't being saved. Records live on the card under `/Apollo2`
    — an index CSV plus one samples CSV per shot — so the whole history can
    be read on any computer (graphs are drawn from the data by the device
    and the web page; nothing is pre‑rendered — the CSVs are the database).
  - **Delete a shot**: open its card and tap the red trash button, then
    confirm. The shot's files are removed from the card and the headline
    stats recalculate without it — handy for throwing out dial‑in or
    practice shots. This one *is* destructive (unlike Reset stats below).
  - **Reset stats**: tap any headline card. Non‑destructive — the three
    headline numbers restart from now (the Total card shows "Since ⟨date⟩")
    while every recorded shot stays on the card. Undo by deleting
    `Apollo2/stats_since.txt` on a computer.
  - **Browse from your phone or computer**: with WiFi connected, open
    `http://⟨device IP⟩/` (the IP is on **Stats → Info**). The device serves
    its own web page — the same stats, month filters, and shot list, with a
    live graph per shot, a **Download card** button that saves any shot as a
    PNG image (composed in your browser from the raw data), and CSV
    downloads — all styled to match whatever theme the device is currently
    using. The same built‑in web server also carries the setup pages during
    pairing/WiFi setup, so nothing conflicts.
- **Info** — device details: our firmware version + git revision, uptime,
  battery/USB state with a runtime estimate, and the machine's Device
  Information (manufacturer, model, serial, firmware) read over Bluetooth.
  - **Diagnostic log** — tap **View** for the recent diagnostic log (the same
    messages the USB serial console prints), each line stamped with the time
    (or seconds‑since‑boot before the clock is set). The log lives in RAM:
    it survives as long as the device stays powered and holds the last
    64 KB — typically hours of normal operation — so "the paddle did nothing
    this morning" can be diagnosed after the fact without a serial cable.
    It does not survive a reboot. The on‑screen viewer shows the newest few
    KB; with WiFi connected, `http://⟨device IP⟩/log` serves the whole
    thing as plain text (there's also a **Log** link on the web page's
    header) — easy to copy into a bug report.

---

## Brew‑by‑weight quick reference

| | Auto shot | Shot detect | Manual |
|---|---|---|---|
| Needs paddle wiring | **Yes** | No | No |
| Shot start | Paddle flip | Detected from weight (back‑dated by **Detect lead‑in**) | Paddle flip (wired) / — |
| Shot stop | **Automatic** at target − learned overshoot | You flip the paddle (pill flashes **Stop** at the right moment) | You flip the paddle |
| Auto‑tare at start | Yes | No (delta‑based) | No |
| Overshoot learning | Yes | No | No |
| Review graph + frozen weight | Yes | Yes | No |
| Auto flush eligible | Yes | Yes (wired boards) | No |

Wiring the paddle harness — parts, photos of the Micra's paddle loom, and
per‑board terminals — is covered in the [wiring guide](docs/WIRING.md).

### Paddle pin protection & per‑unit remapping

- **Suggested: a 1 kΩ resistor in line with the paddle's sense wire** (at the
  screw terminal, between the board pin and the switch). It protects the
  input by limiting current from static or stray voltage on the wire —
  one cheap part, no effect on normal operation. The
  [wiring guide](docs/WIRING.md) shows a no‑solder way to add it with a
  4‑slot screw‑terminal block.
- **If a paddle pin ever does fail** (symptom: the paddle stops registering
  or behaves erratically while everything meters fine — the diagnostic log's
  `Paddle: raw sense ...` lines will show a line stuck or drifting on its
  own), the pins are **remappable per unit** without a custom firmware
  build: move the wire to a free GPIO on the header, connect the device over
  USB, and run

  ```
  make padsense PIN=50      # sense wire moved to GPIO50 (example)
  make paddrive PIN=49      # drive wire, same idea
  make padsense PIN=-1      # revert to the board default
  ```

  (or type `padsense=50` into any serial monitor). The setting is stored in
  the device's settings memory — it survives reflashes and web‑flasher
  upgrades, so stock releases keep working on a remapped unit — and takes
  effect on the next boot, which logs it: `Paddle: sense GPIO50 (NVS
  override)`. The board's default pins stay unchanged for everyone else.
