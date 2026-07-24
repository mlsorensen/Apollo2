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
  `Disconnected`, `Connecting...`, `Ready` (powered on), or `Standby`.
- **BREW / STEAM** — live boiler temperatures. On larger screens each has a
  **−/+** stepper that edits the set‑point directly; the small grey number is
  the current target. Edits are written to the machine as you tap.
- **Power button** — `Standby` / `Turn on` when connected. When the machine is
  configured but disconnected it becomes **Connect** and starts the Bluetooth
  link.

### SCALE card (when a scale is paired)

- **Status** — `Connected`, `Connecting...`, or `Sleeping` (sleep‑capable
  scales such as the Acaia Umbra with the link switched off). A small battery
  icon shows the scale's own battery where the scale reports it.
- **WEIGHT** — live reading. During a finished shot's review it freezes at the
  net shot weight so lifting the cup doesn't wipe the number you care about.
- **TARGET** — the brew‑by‑weight stop target in grams (**−/+** on larger
  screens; also under **Settings → Scale → Settings → Target**).
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
    paddle now.
  - **Manual**: nothing armed. A wired board still relays the paddle and times
    the shot; otherwise the scale's own timer is shown.
  - While a finished shot is frozen for review the pill reads **Reset** —
    tap it to dismiss the review and return to live monitoring.
  - The pill is disabled while a shot is running or settling.
- **Tare** — zeroes the scale. (Tares are sent twice under the hood; some
  scales drop single tare commands.)
- **Disconnect / Connect** — drops or re‑establishes the scale link, e.g. to
  save the scale's battery.

### Flow graph

Plots the live flow rate (or weight) from the scale.

- Tap the **g/s** chip to switch between flow rate (g/s) and weight (g).
- During a shot the graph restarts and follows the shot; when the shot ends it
  freezes for review (see **Review hold**) and then resumes.
- The graph style (oscilloscope sweep vs scrolling) and smoothing are set under
  **Settings → Scale → Settings**.

### Shot lifecycle notes (all modes)

- A run shorter than **8 seconds** never becomes a "shot" — it's treated as a
  flush/rinse and discarded silently. Shot detect additionally requires a few
  grams of net gain.
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

### Settings

- **Auto connect** *(default on)* — connect to the saved machine automatically
  at power‑up. Turn off if another controller needs the Micra's single
  Bluetooth slot.
- **Wired paddle** *(paddle‑capable boards; default off)* — tell the firmware
  the paddle harness is physically wired. This enables the **Auto shot** mode
  and the auto‑flush. Off, the board behaves like an unwired one (Shot detect
  / Manual only). Flipping it mid‑shot cancels the shot.
- **Auto flush** *(wired paddle boards; Off / 3 s / 6 s; default Off)* — after
  a finished shot, when the scale sees the cup lift off, the firmware waits
  (see **Flush delay**) and then runs the group for this long to rinse the
  puck's surface. Any paddle activity, a new shot, or the machine being in
  standby cancels it.
- **Flush delay** *(shown while Auto flush is on; 3 / 6 / 9 / 15 s; default
  3 s)* — the pause between the cup coming off and the flush running.
- **Brew → Temperature** — coffee boiler set‑point stepper (0.1 °C steps;
  long‑press for 0.5 °C).
- **Steam Boiler → Enable** — steam boiler on/off.
- **Steam Boiler → Temperature** — one of the Micra's three steam levels
  (shown as Level 1–3 with the temperature underneath).

---

## Settings → Scale

### Bluetooth

- **Scan** — searches for supported scales (Bookoo Themis, Acaia Umbra /
  Lunar / Pyxis / Prochef). Wake the scale first — most sleep their Bluetooth
  quickly.
- **Saved scale row** — **Connect / Disconnect** and **Forget** (no token
  needed for scales).

### Settings

- **Target** — brew‑by‑weight stop target, 5–120 g. Also editable from Home on
  larger screens.
- **Review hold** *(5–120 s, default 30 s)* — how long a finished shot's frozen
  graph and weight linger before auto‑dismissing. **Reset** dismisses early.
- **Smoothing** *(Off / Light / Medium / Strong, default Light)* — smoothing on
  the shot graph's line. Purely visual; detection and auto‑stop use the raw
  stream.
- **Drop negative g/s** *(default on)* — clamps negative flow readings (cup
  bumps, scale noise) to zero on the graph.
- **Oscilloscope graph** *(default on)* — the shot graph sweeps left→right and
  wraps, oscilloscope style. Off, it scrolls continuously instead.

---

## Settings → Device

- **Brightness** *(dimmable boards)* — backlight level.
- **Screen dim** *(Off / 15 min / 30 min, default 30 min)* — after this idle
  time the screen dims to 5 % (or switches off where the backlight can't dim);
  any touch restores it.
- **Hour / Minute** — set the clock by hand. (With Wi‑Fi + NTP the clock sets
  itself; boards with an RTC keep time through power‑off.)
- **24‑hour** *(default on)* — clock format.
- **Fahrenheit** *(default off)* — display unit for temperatures. Set‑points
  are still stored in Celsius.
- **Theme** — tap to cycle the color scheme: Midnight, Graphite, Espresso,
  Nord, Solarized, Plum, Forest, Rose, Mono (black & white), Contrast (high
  contrast), Ferrari, Sunset, Citrus.
- **Performance overlay** *(default off)* — LVGL FPS/CPU overlay, for
  debugging.
- **Button sounds** *(boards with a speaker; default on)* — click on button
  presses.

### WiFi

- **Enable** *(default off)* — join your home network. Used only for NTP time
  sync; all machine/scale control is local Bluetooth.
- **Status** — `Off`, `Connecting`, or `Connected` with the IP address.
- **Set up WiFi** — starts the device's own access point (`Micra-Setup`) and
  setup page for entering credentials (same page as token entry).
- **Forget** — clears the saved network.
- **Timezone** — city picker (handles daylight saving).
- **Auto time (NTP)** *(default on)* — sync the clock while connected.

### Root page

- **Restart display** — soft reboot. Escape hatch for the rare RGB‑panel boot
  glitch where the image comes up shifted by a few pixels.

---

## Stats

- **Brew / Boiler** — temperature history graphs. Tap **+/−** to zoom the time
  window; the set‑point is drawn as a reference line; gaps mean the machine was
  disconnected.
- **Info** — device details: our firmware version + git revision, uptime,
  battery/USB state with a runtime estimate, and the machine's Device
  Information (manufacturer, model, serial, firmware) read over Bluetooth.

---

## Brew‑by‑weight quick reference

| | Auto shot | Shot detect | Manual |
|---|---|---|---|
| Needs paddle wiring | **Yes** | No | No |
| Shot start | Paddle flip | Detected from weight | Paddle flip (wired) / — |
| Shot stop | **Automatic** at target − learned overshoot | You flip the paddle (pill flashes **Stop** at the right moment) | You flip the paddle |
| Auto‑tare at start | Yes | No (delta‑based) | No |
| Overshoot learning | Yes | No | No |
| Review graph + frozen weight | Yes | Yes | No |
| Auto flush eligible | Yes | Yes (wired boards) | No |

Wiring the paddle harness is described per board in the
[README's hardware section](README.md#supported-hardware).
