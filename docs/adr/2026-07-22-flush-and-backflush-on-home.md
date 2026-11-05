# Flush and backflush: one duration everywhere, the sequence in the controller, Standby | Flush on Home

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-07-22 · **Last changed:** 2026-11-03
- **Boards:** paddle-wired boards (the drive line exists); Flush on screens 4.3" and larger

## Context

With a drive line to the paddle, Apollo can run water through the group on
its own: a post-shot auto-flush, a manual Flush and a backflush cycle all
want it. Every pulse on the line starts a fresh brew, so the machine
preinfuses every pulse and a pulse shorter than the preinfusion time runs
at line pressure only ([seen on the bench](#evidence)); preinfusion is not
readable over the local link.

## Decision

- **One duration drives every timed group run** (`core::flush_run_ms`,
  [why one](#alternatives-and-dead-ends)): the Auto flush setting (Off / 3 /
  6 / 9 / 15 s, wide enough to outlast preinfusion) sets the auto-flush, the
  Home Flush (3 s when Auto flush is Off) and each backflush pulse; the
  backflush off phase is a fixed 3 s and the prompt states the pulse length.
- **Settings > Micra > Cleaning** holds everything that deliberately runs
  water: Auto flush, Flush delay (5 / 10 / 15 s, default 5 s, shown while
  Auto flush is on; [why not 3 s](#evidence)) and Backflush cleaning
  ([why its own page](#alternatives-and-dead-ends)).
- **Home MICRA card: Standby | Flush**, mirroring the SCALE card's
  Disconnect | Tare. Tap again to stop early; it reads Stop while an
  auto-flush runs. Hidden where there is no drive line (Standby keeps the
  row) and on the compact tier (its row is already Power + Tare).
- **Backflush** is a full-screen mode: fit the blind filter, Go, 10 cycles
  with a live counter and countdown, Cancel stays, Back leaves.
- **The sequence lives in `BrewController::poll_clean`, not the UI**, so a
  screen teardown can never orphan a running group. `CleanMode` owns the
  drive line exclusively; `can_clean()` refuses on a standby machine (a
  closed line there is the wake switch) and mid-shot. A paddle edge during
  cleaning cancels it and is consumed: the human outranks the automation.
- **Auto-flush** arms on review entry, starts after the cup comes off
  (a >= 30 g drop within 60 s), waits the Flush delay, runs for the
  duration; paddle activity, a new shot, a lost relay or standby cancels it.

## Consequences

- `BrewSnapshot::relay` (paddle hardware and the wired setting) is the "a
  drive line exists" flag, not `paddle_wired`.
- Cleaning counts as activity for auto-standby and is cancelled by the
  paddle breaker ([since](#history)).
- Screenshots: `../img/manual/micra-cleaning.png`, `../img/manual/backflush.png`,
  `../img/manual/clean-lock.png`, `../img/manual/home-micra.png`.

---

## Details

### Evidence

- 2026-07-26, bench machine: the first backflush used a fixed 4 s pulse,
  and the pulses never engaged the pump; the 4 s pulse "ran at line
  pressure, still water" (`99d33af`). The Auto-flush-length pulse was
  confirmed against the real preinfusion setting afterwards ("tested, works
  well", per the working notes; the commit itself says "Not HW-tested").
  Not separately confirmed: the paddle-takeover cancel mid-sequence and the
  aborted-vs-complete wording.
- v1.0.2: a 3 s Flush delay proved too short to get the cup clear and a
  hand out of the way; the choices became 5 / 10 / 15 s.
- The three uses arrived within four days (2026-07-22 to 2026-07-26).

### Alternatives and dead ends

- A fixed 4 s on / 4 s off backflush: useless on a preinfusing machine.
- Reading or disabling preinfusion over BLE: not possible, the local link
  exposes mode, capabilities, tank, boilers and smart standby only.
- A separate cleaning duration setting: rejected for one duration.
- Cleaning rows inline on the Micra page: rejected; cleaning is a task you
  come to do, not a setting to scroll past, and the sub-page shortens the
  Micra page.
- The sequence in the UI: rejected, a screen teardown could orphan a running
  group; no shot machinery and no auto-flush run while `CleanMode` holds
  the line.

### History

- 2026-07-22: auto-flush Off / 3 / 6 s; Stats > Info gained its Uptime row
  the same day (`94a8008`).
- 2026-07-24: Flush delay, 3 / 6 / 9 s with a 3 s default (`ded5d27`).
- 2026-07-26: backflush + Home Flush (`aba47cc`); one duration, Cleaning
  page, action-only power button (`99d33af`); HW-verified afterwards, per
  the working notes.
- 2026-10-05: the auto-flush ignores the paddle while it runs; Home Stop
  ends it (`e5ff37f`).
- 2026-10-16: Flush delay 5 / 10 / 15 s, default 5 s (`4f76db3`, v1.0.2).
- 2026-11-02: cleaning counts as activity for auto-standby (`7a9cdf7`).
- 2026-11-03: the paddle breaker cancels cleaning (`f81dc59`).

### Sources

Commits above; `include/core/brew.h` (`kBackflushCycles`, `kBackflushOffMs`,
`kManualFlushDefaultS`, `kFlushDelayDefaultS`, `flush_run_ms`);
`include/core/brew_controller.h` (`kFlushCupDropG`, `kFlushWatchMs`);
`src/ui/app.cpp` (`kFlushChoices`, `kFlushDelayChoices`); MANUAL.md
"Cleaning", "MICRA card".
