# Connection UX: no startup scan, a saved machine, discovery in Settings, Home says the state

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-06-28 · **Last changed:** 2026-11-07
- **Boards:** all

## Context

The Micra has a single Bluetooth slot and a 64-hex-character pairing token,
and the local link carries machine data, not a discovery flow. A scan at
every boot costs time and radio, and a Home that shows only machine data
leaves the user guessing when nothing is connected. The radio mechanics are
other records; this one is what the screens do.

## Decision

- **No startup scan.** Address and name are saved in NVS and the link dials
  them directly. "Auto connect" (NVS `autoconn`, default on,
  [history](#history)) is for when another controller needs the slot.
- **Discovery lives in Settings > Micra > Bluetooth:** Scan lists machines,
  tapping one saves it; the saved row has Setup (until a token is stored),
  Connect / Disconnect and Forget (clears address, name and token). Scales
  reuse the pattern plus a slot modal
  (`2026-10-24-two-scales-home-and-settings.md`).
- **Home shows the state** on the MICRA card's status line: Not set up,
  Setup unfinished, Disconnected, Connecting..., Heating, Ready, Standby.
  The Power button is action-only: Standby / Turn On, Connect, "Pair Micra"
  (jumps to the Bluetooth page), or disabled; it never repeats the status
  line ([why](#alternatives-and-dead-ends)).
- **A tap is acknowledged** ([why](#evidence)): after Standby / Turn On the
  button disables and reads "Working..." until the REPORTED power changes,
  the link leaves Connected, or a 12 s deadline passes ([how
  sized](#history)). One sender, `App::send_power`, arms it for the tap,
  the schedule and auto-standby alike. A deadline, not a hold, so a
  swallowed command can never lock the button.
- **Token entry** is a WiFi setup page (SoftAP portal), not an on-screen
  keyboard ([why](#alternatives-and-dead-ends)); the link verifies the token
  with a read before declaring Connected.
- **First boot** shows a welcome modal every boot until a Micra is paired:
  Set up WiFi / Pair Micra / Pair scale, Later (this boot), Don't show again
  (persisted); a step already done drops its button. WiFi is framed as the
  better first step (clock, updates, schedule).
- **Scale cards** read "Connecting..." with the button disabled while a
  connect is in flight; a driver with no settings (the Varia Aku) gets
  "No device settings available for this scale" instead of a blank page,
  Connect row hidden.

## Consequences

- `core::Link` {Unconfigured, NeedsToken, Disconnected, Connecting,
  Connected} drives both the status line and the button
  (`App::toggle_power`, `App::apply_scheduled_power` hold the deadlines).
- Screenshots: `../img/manual/home-micra.png`, `../img/manual/home-noscale.png`,
  `../img/manual/micra-bluetooth.png`, `../img/manual/token-modal.png`,
  `../img/manual/welcome-modal.png`, `../img/manual/scale-device-settings.png`.

---

## Details

### Evidence

- 2026-08-10: the mode answer trails a command by 1 to 3 s (1 s poll plus a
  stale read-back), so Standby / Turn on looked dead and invited a second,
  state-undoing tap (`14391e7`). This is why a tap is acknowledged with
  "Working...".
- 2026-09-10: users found a greyed "Connect" confusing and "Token needed"
  leaked the auth token's existence at people who cannot act on it
  (`8d0962b`).

### Alternatives and dead ends

- An on-screen keyboard for the token: rejected, 64 hex characters are too
  long to type.
- A disabled "Connect" naming an action the user cannot take: replaced by
  an enabled "Set up" (2026-09-10), then "Pair Micra" (2026-09-22).
- Status words in the button ("Connecting...", "Set up in Settings"):
  removed 2026-07-26 when Flush took half the row; the card's status line
  already said it. The one exception survives in the code: a layout with no
  status text (the compact scale layout's dot-only header) still reads
  "Connecting..." on the button (`has_status_text` in `home_tab.cpp`).
- A per-boot scan and a dead "Offline" button on Home: the first day's
  behaviour, replaced on 2026-06-28 by the saved machine and the status
  line.

### History

- 2026-06-28: connection manager, Settings scan/save/forget, token via the
  WiFi page, NeedsToken; no scan on normal startup (`90a04da`, `d29f04e`,
  `5528c19`, `046a087`).
- 2026-07-01: offline button becomes Connect (`4fbaef6`).
- 2026-07-18: Auto connect setting, default off (`280bedf`).
- 2026-07-23: Auto connect defaults on (`4fffc83`).
- 2026-07-26: action-only power button (`99d33af`).
- 2026-08-10: "Working..." with an 8 s deadline (`14391e7`).
- 2026-09-10: "Set up"; status wording (`8d0962b`).
- 2026-09-22: welcome modal, "Pair Micra" (`a3e5332`).
- 2026-10-24: scale cards' "Connecting..." (`42e8d00`); the scale slot
  modal shipped in v1.2.0.
- 2026-11-01: tap deadline 8 -> 12 s (`5c9bb70`), to cover a stalled
  request's 5 s watchdog and re-dial; the scheduled command's stays 8 s.
- 2026-11-04: Device settings "none" text (`62218cc`).
- 2026-11-07: the scheduled command's 8 s deadline folded into the tap's
  12 s behind one sender (issue #9): a scheduled command landing during a
  masked re-dial could re-enable the button before the machine reported.

### Sources

Commits above; `src/ui/home_tab.cpp` (status switch), `src/ui/app.cpp`
(`toggle_power`, `apply_scheduled_power`, `open_welcome_modal`),
`src/platform_esp32/config.cpp` (`autoconn`); MANUAL.md "MICRA card",
"Bluetooth (machine)".
