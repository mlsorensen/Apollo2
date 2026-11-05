# No "Start anyway" override for a paddle flip refused without a scale

- **Status:** Dropped (the override); the replacement, Manual mode as the way out, is Accepted
- **Area:** hardware
- **First decided:** 2026-08-24 · **Last changed:** 2026-10-01
- **Boards:** every board with a wired paddle

## Context

Since 2026-08-09 (`4110485`) a paddle ON edge is refused while the armed
shot mode is Auto shot or Shot detect and no scale is connected: the
machine stays off and a toast says why. Because the paddle reaches the
machine only through Apollo, a refusal means the physical paddle cannot
reach the machine at all, and powering Apollo off does not help (the
drive is then open). That blocked a manual pull with the scale away, and
the machine's own paddle-driven pairing sequence (paddle left, power on,
wait, paddle right), which stalled a token-provisioning test until the
unit was put in Manual mode.

## Decision

Do not add a one-shot pass-through. A "Start anyway" modal was built on
2026-08-24 and reverted in the same session (no commit survives; the
working notes of that day record it): it left the mode as Auto or Shot
detect, so the shot it let through was never tracked or stopped, a timer
running forever. Manual mode already covers pulling without a scale, so
the refusal toast leads with that instead: "Shot not started: Auto shot
needs the scale. Switch to Manual mode to pull without one, or connect a
scale." (Shot detect names itself the same way; with a bench scale saved
it says "espresso scale".) Switching shot mode cancels any in-flight timer
(`87c5187`).

On 2026-10-01 (`74eef72`, found on the first Apollo Link bench test) the
sharper case went away: with NO scale paired the effective mode is Manual
whatever is stored, since nothing could detect or stop a shot and the
Home mode pill is not even rendered. The stored mode returns the moment a
scale is paired. A known-but-disconnected scale keeps the chosen mode and
its guard.

## Evidence

No measurements; a UX decision. The bench observation of 2026-10-01: a
unit with no scale paired and Shot detect stored refused every flip with
no way out on screen.

## Alternatives and dead ends

- The one-shot override (built, reverted 2026-08-24): untracked run.
- An override that also flips the mode to Manual for that pull: the condition under which the override could return; not built.

## Consequences

- Refusals stay the default so brew-by-weight protects normal use. The wired path refuses in Auto shot (`poll_wired`), the pass-through relay in Shot detect (`poll_unwired`); both tick `scale_refuse_seq`.
- Standby wake flips are exempt from the refusal and OFF edges always relay.

## History

- 2026-08-09: no-scale refusal with a toast (`4110485`).
- 2026-08-24: override built and reverted; toast reworded; mode switch cancels the timer (`87c5187`).
- 2026-10-01: no paired scale means Manual (`74eef72`).

## Sources

src/core/brew_controller.cpp (`scale_refuse_seq`); src/ui/app.cpp (the refusal toast); MANUAL.md on shot modes.
