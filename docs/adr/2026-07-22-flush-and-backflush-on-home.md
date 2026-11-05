# Flush and backflush: one duration everywhere, the sequence in the controller, Standby | Flush on Home

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-07-22 · **Last changed:** 2026-11-03
- **Boards:** paddle-wired boards (the drive line exists); Flush on screens 4.3" and larger

## Context

With a drive line to the paddle, Apollo can run water through the group on
its own. Three uses arrived within four days: a post-shot auto-flush to
rinse the puck's surface, a manual Flush, and a backflush cleaning cycle.
The first backflush used a fixed 4 s pulse, and on the bench machine the
pulses never engaged the pump: every pulse starts a fresh brew, so the
machine preinfuses every pulse, and a pulse shorter than the preinfusion
time runs at line pressure only. Preinfusion is not readable over the local
link, so Apollo cannot detect or disable it.

## Decision

- **One duration drives every timed group run** (`core::flush_run_ms`): the
  Auto flush setting (Off / 3 / 6 / 9 / 15 s, widened so a pulse can outlast
  preinfusion) sets the auto-flush, the Home Flush (3 s when Auto flush is
  Off) and each backflush pulse; the backflush off phase is a fixed 3 s and
  the prompt states the pulse length this machine will use.
- **Settings > Micra > Cleaning** holds everything that deliberately runs
  water: Auto flush, Flush delay (5 / 10 / 15 s, default 5 s, shown while
  Auto flush is on; 3 / 6 / 9 s with a 3 s default until v1.0.2, when 3 s
  proved too short to get the cup clear and a hand out of the way) and
  Backflush cleaning. Cleaning is a task you come to do, not a setting to
  scroll past, and this shortens the Micra page.
- **Home MICRA card: Standby | Flush**, mirroring the SCALE card's
  Disconnect | Tare. Tap again to stop early; it reads Stop while an
  auto-flush runs and stops that. Hidden where there is no drive line
  (Standby keeps the row) and on the compact tier (its row is already
  Power + Tare).
- **Backflush** is a full-screen mode: fit the blind filter, Go, 10 cycles
  with a live counter and countdown, Cancel stays, Back leaves.
- **The sequence lives in `BrewController::poll_clean`, not the UI**, so a
  screen teardown can never orphan a running group. `CleanMode` owns the
  drive line exclusively (no shot machinery, no auto-flush while it runs);
  `can_clean()` refuses on a standby machine (a closed line there is the
  wake switch, not water) and mid-shot. A physical paddle edge during
  cleaning cancels it and is consumed, so it does not also start a shot.
  The human outranks the automation on the drive line.
- **Auto-flush** arms on review entry, starts after the cup comes off
  (a >= 30 g drop within 60 s), waits the Flush delay, runs for the
  duration; any paddle activity, a new shot, a lost relay or a machine in
  standby cancels it.
- Stats > Info gained an Uptime row the same day (2026-07-22).

## Evidence

- 2026-07-26, bench machine: the 4 s pulse "ran at line pressure, still
  water" (`99d33af`); the Auto-flush-length pulse was confirmed against the
  real preinfusion setting afterwards ("tested, works well", per the working
  notes; the commit itself says "Not HW-tested"). Not separately confirmed:
  the paddle-takeover cancel mid-sequence and the aborted-vs-complete
  wording.

## Alternatives and dead ends

- A fixed 4 s on / 4 s off backflush: useless on a preinfusing machine.
- Reading or disabling preinfusion over BLE: not possible, the local link
  exposes mode, capabilities, tank, boilers and smart standby only.
- A separate cleaning duration setting: rejected for one duration.

## Consequences

- `BrewSnapshot::relay` (paddle hardware and the wired setting) is the "a
  drive line exists" flag, not `paddle_wired`.
- Cleaning counts as activity for auto-standby (2026-11-02) and is cancelled
  by the paddle breaker (2026-11-03).
- Screenshots: `../img/manual/micra-cleaning.png`, `../img/manual/backflush.png`,
  `../img/manual/clean-lock.png`, `../img/manual/home-micra.png`.

## History

- 2026-07-22: auto-flush Off / 3 / 6 s, Uptime row (`94a8008`).
- 2026-07-24: Flush delay (`ded5d27`).
- 2026-07-26: backflush + Home Flush (`aba47cc`); one duration, Cleaning
  page, action-only power button (`99d33af`); HW-verified
  afterwards, per the working notes.
- 2026-10-05: the auto-flush ignores the paddle while it runs; Home Stop
  ends it (`e5ff37f`).
- 2026-10-16: Flush delay 5 / 10 / 15 s, default 5 s (`4f76db3`, v1.0.2).
- 2026-11-02/03 — cleaning as activity (`7a9cdf7`); breaker cancels
  cleaning (`f81dc59`).

## Sources

Commits above; `include/core/brew.h` (`kBackflushCycles`, `kBackflushOffMs`,
`kManualFlushDefaultS`, `kFlushDelayDefaultS`, `flush_run_ms`);
`include/core/brew_controller.h` (`kFlushCupDropG`, `kFlushWatchMs`);
`src/ui/app.cpp` (`kFlushChoices`, `kFlushDelayChoices`); MANUAL.md
"Cleaning", "MICRA card".
