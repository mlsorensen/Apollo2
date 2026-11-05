# Connection UX: no startup scan, a saved machine, discovery in Settings, Home says the state

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-06-28 · **Last changed:** 2026-11-04
- **Boards:** all

## Context

On the first day the remote scanned for the machine at every boot and Home
showed a dead "Offline" button when nothing was connected. The rule
set on 2026-06-28: no BLE scan on normal startup, persist the chosen
machine and connect to it directly, put discovery in Settings, and make Home
show the connection state rather than only machine data. The radio
mechanics (connect by address, reconnect, arbitration) are other records;
this one is what the screens do.

## Decision

- **No startup scan.** The machine's address and name are saved in NVS and
  the link dials them directly. The "Auto connect" switch (NVS `autoconn`,
  added 2026-07-18 defaulting off, default on since 2026-07-23) exists for
  the case where another controller needs the Micra's single Bluetooth
  slot.
- **Discovery lives in Settings > Micra > Bluetooth:** Scan lists machines,
  tapping one saves it; the saved row has Setup (until a token is stored),
  Connect / Disconnect and Forget (clears address, name and token). The same
  pattern was reused for scales, with the slot modal added in v1.2.0.
- **Home shows the state** on the MICRA card's own status line: Not set up,
  Setup unfinished, Disconnected, Connecting..., Heating, Ready, Standby.
  The Power button is ACTION-ONLY: Standby / Turn On when connected,
  Connect when configured but disconnected, "Pair Micra" (jumps to the
  Bluetooth page) when setup is incomplete, disabled when no action can be
  taken. It never duplicates the status line.
- **A tap is acknowledged.** After Standby / Turn On the button disables and
  reads "Working..." until the REPORTED power changes, the link leaves
  Connected, or a deadline passes. The deadline for a tap is 12 s (8 s from
  2026-08-10, raised 2026-11-01 to cover a stalled request's 5 s watchdog
  and re-dial, `App::toggle_power`); a command sent by the schedule shows
  the same "Working..." but keeps the 8 s deadline
  (`App::apply_scheduled_power`). It is a deadline, not a hold: a swallowed
  command can never lock the button.
- **Token entry** is a WiFi setup page (SoftAP portal) rather than an
  on-screen keyboard, the token being 64 hex characters; the link verifies
  the token with a read before declaring Connected.
- **First boot** shows a welcome modal on every boot until a Micra is
  paired: Set up WiFi / Pair Micra / Pair scale, then Later (this boot) and
  Don't show again (persisted); a step already done drops its button. WiFi
  is framed as the better first step (clock, updates, schedule).
- **Scale cards** read "Connecting..." with the button disabled while a
  connect is in flight; a scale whose driver declares no settings (the Varia
  Aku) gets "No device settings available for this scale" on the Device
  settings page instead of a blank page, with the Connect row hidden.

## Evidence

- 2026-08-10: the mode answer trails a command by 1 to 3 s (1 s poll plus a
  stale read-back), so Standby / Turn on looked dead and invited a second,
  state-undoing tap (`14391e7`).
- 2026-09-10: users found a greyed "Connect" confusing and "Token needed"
  leaked the auth token's existence at people who cannot act on it
  (`8d0962b`).

## Alternatives and dead ends

- An on-screen keyboard for the token: rejected, too long to type.
- A disabled "Connect" naming an action the user cannot take: replaced by
  an enabled "Set up" (2026-09-10), then "Pair Micra" (2026-09-22).
- Status words in the button ("Connecting...", "Set up in Settings"):
  removed 2026-07-26 when Flush took half the row; the card's status line
  already said it. The one exception survives in the code: a layout with no
  status text (the compact scale layout's dot-only header) still reads
  "Connecting..." on the button (`has_status_text` in `home_tab.cpp`).

## Consequences

- `core::Link` {Unconfigured, NeedsToken, Disconnected, Connecting,
  Connected} drives both the status line and the button.
- Screenshots: `../img/manual/home-micra.png`, `../img/manual/home-noscale.png`,
  `../img/manual/micra-bluetooth.png`, `../img/manual/token-modal.png`,
  `../img/manual/welcome-modal.png`, `../img/manual/scale-device-settings.png`.

## History

- 2026-06-28: connection manager, Settings scan/save/forget, token via the
  WiFi page, NeedsToken (`90a04da`, `d29f04e`, `5528c19`, `046a087`).
- 2026-07-01: offline button becomes Connect (`4fbaef6`).
- 2026-07-18: Auto connect setting, default off (`280bedf`).
- 2026-07-23: Auto connect defaults on (`4fffc83`).
- 2026-07-26: action-only power button (`99d33af`).
- 2026-08-10: "Working..." with an 8 s deadline (`14391e7`).
- 2026-09-10: "Set up"; status wording (`8d0962b`).
- 2026-09-22: welcome modal, "Pair Micra" (`a3e5332`).
- 2026-10-24: scale cards' "Connecting..." (`42e8d00`).
- 2026-11-01: tap deadline 8 -> 12 s (`5c9bb70`); the scheduled command's
  stays 8 s.
- 2026-11-04: Device settings "none" text (`62218cc`).

## Sources

Commits above; `src/ui/home_tab.cpp` (status switch), `src/ui/app.cpp`
(`toggle_power`, `apply_scheduled_power`, `open_welcome_modal`),
`src/platform_esp32/config.cpp` (`autoconn`); MANUAL.md "MICRA card",
"Bluetooth (machine)".
