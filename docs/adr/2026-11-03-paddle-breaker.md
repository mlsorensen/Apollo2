# The paddle breaker: a flickering sense input opens the drive until a person resets it

- **Status:** Accepted (shipped in v1.3.0-beta.2, 2026-11-03)
- **Area:** hardware
- **First decided:** 2026-11-03 · **Last changed:** 2026-11-03
- **Boards:** every board with a wired paddle (P4-5, P4-4.3, X 8", S3-4.3C)

## Context

A failing sense pad announces itself hours ahead. On 2026-11-02 the
bench X 8" GPIO5 produced 238 accepted phantom ON edges in two hours
(120 in one five-minute window), each a shot start; only an offline scale
kept the pump off. The Micra's own cutoff (about a minute) bounds one
run; the harm is the repetition. The model we chose: a circuit breaker. It trips on a fault
signature, never in normal use, and a person resets it.

## Decision

`core::BrewController` counts two signatures in a fixed 60 s window and
trips on either:

| signature | what it is | trip at |
|---|---|---|
| rejected pulses | raw sense transitions the 75 ms glitch filter threw away (a level that changed again before 3 consecutive 25 ms polls) | 10 per window |
| short accepted pulses | an accepted ON that goes OFF again in under 500 ms (ON->OFF only; a brisk OFF->ON after a long ON is a hand reaching for the next shot) | 4 per window |

**There is deliberately NO rate rule on accepted edges**: a manual
backflush is ten ON edges in well under two minutes (`kBackflushCycles`
= 10 cycles of the flush duration on, `kBackflushOffMs` = 3 s off) and
must never trip. A hand's ~1 ms contact bounce is invisible at the 25 ms
poll, so a healthy pin counts about zero forever; a flush holds a second
or more, a shot twenty.

Tripped: every drive goes through one choke point, `set_drive()`, which
refuses to close the line; `cancel_shot()` ends whatever was running (a
shot, an auto-flush, a cleaning cycle) and opens the line; `can_clean()`
is false; `IPaddle::own(false)` drops the Link's CTRL so the paddle
reverts to the Micra in copper (on the opto cable and the 4.3C there is no
transfer switch: the paddle is dead until reset, and the modal says so).
`paddle_trip_seq` ticks and the UI shows a modal with one button, Reset.
Reset clears the counters and re-takes CTRL; continuing flicker re-trips
at once, which is the diagnosis. Nothing persists; a reboot clears it.

## Evidence

- Cost: about 20 B of .bss in BrewController (two counters, three timestamps, a flag, a sequence), no task, no heap; the modal is a transient LVGL object. X 8" build RAM unchanged at 94,408 B.
- `make test-paddle` (tools/paddle_breaker_test.cpp, plain g++) replays the X 8"'s chatter, a bouncy 10-cycle manual backflush, flush-flush-shot, three quick flicks versus four, trips mid-backflush / mid-flush / mid-shot, reset under flicker and when quiet, a trip landing on a backflush phase boundary, and boot with the paddle left ON. Keep it green.
- **First real trip, 2026-11-03:** a developer fitted the sense resistor into the X 8"'s terminal block with the board powered; the loose contacts chattered, the drive opened and the modal showed, exactly as designed (working notes and CLAUDE.md; the time of day is not recorded). The guide now says to plug the block in powered off, and that touching bare wires together during the bench test can trip it (`78cd3e3`, `24f9302`).

## Alternatives and dead ends

- A rate rule on accepted edges: rejected before building (the backflush).
- Dropping only the drive line while letting flushes and cleaning run: rejected ("stop driving the Micra at all"), hence the single choke point.
- Auto-clear or a persisted trip: not wanted; a person looks at the wiring.
- A no-flow guard (shot running, no weight arriving): out of scope.

## Consequences

- Review fixes the same day (`24f9302`): `poll_clean` returns once a trip has ended the cycle (it used to fall through and log a phantom pulse), and the short rule counts ON->OFF only.
- 4.3C: an I2C read failure on the expander used to read as "open", which under the breaker is a rejected pulse whenever the paddle is held closed; `Paddle::sensed()` now keeps the last good level (`0fec2f2`). "Open on error" bought nothing, the drive goes through the same chip.
- The breaker's modal names the wiring in use (Link: paddle in copper; cable: paddle dead until reset).

## History

- 2026-11-03: built, host test, shipped in v1.3.0-beta.2 (`f81dc59`, `24f9302`, `0fec2f2`, release `1d4199b`).

## Sources

src/core/brew_controller.cpp (`sense_paddle_edge`, `breaker_note`, `trip_breaker`, `set_drive`); include/core/brew_controller.h (`kBreaker*`); tools/paddle_breaker_test.cpp; MANUAL.md "Paddle pin protection"; the sense-protection record `2026-07-31-paddle-sense-protection.md`.
