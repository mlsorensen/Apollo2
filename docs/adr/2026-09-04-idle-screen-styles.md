# Idle screen: Lion / Apollo / Alternate / Dim / Off, with a per-board dim floor

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-09-04 · **Last changed:** 2026-09-17
- **Boards:** all; the dim floor differs per board family

## Context

Until 2026-09-04 the screen timeout only dimmed the backlight. A real
screensaver followed, and three weeks of use on the 4.3C, the X-7
and the P4-5 shaped what it shows and how dark it gets. Two things kept
coming back: a 5 % backlight that read as a switched-off panel on the S3
boards, and style names that did not say what they did.

## Decision

Settings > Apollo > Display has "Screen timeout" (Off / 1 / 5 / 15 / 30 min)
and, while it is on, "Idle screen":

| style | backlight | screen |
|---|---|---|
| Lion | per-board floor | the lion bounces on black, colour change at each wall |
| Apollo | per-board floor | the Apollo drawing, same motion |
| Alternate | per-board floor | starts on a coin flip (tick parity), the other artwork takes over every 3 min, keeping position, direction and colour |
| Dim | per-board floor | the live UI stays visible under a transparent cover that swallows the waking tap |
| Off | off | nothing |

The floor is `kSaverDimPercent` in each board block: 10 on the S3 boards,
5 on the P4s. Both drawings are 6/10 of the screen height. A shot in
progress never counts as idle. Boards whose backlight cannot dim show the
drawings and Dim at full brightness; Off is the go-dark choice there.

## Evidence

- 2026-09-09, 4.3C: Dim rendered fully black. The hard-coded 5 % sits below
  the usable range of the IO-extension's inverted-duty PWM, and the saver
  was only glimpsed on wake when the backlight returned. The P4's LEDC path
  is linear, so 5 % is merely very dim there and was left as is, untested at
  the low end (`c3feeef`).
- 2026-09-17, 4.3C: the Apollo artwork at 6/10 height on the top layer ran
  at 13 fps / 60 ms per frame; as its own screen 25 to 27 fps / 28 ms. That
  measurement and the choice to load the saver as its own screen are in
  `2026-09-09-screensaver-frame-cost.md`.

## Alternatives and dead ends

- "Logo / Blank" (`3a328a6`, 2026-09-04): Blank was a true backlight off,
  so there was no dim-without-logo. Dim was inserted on 2026-09-10
  (`8d0962b`) under a NEW NVS key `ssstyle2` (old 1 -> new 2) because the
  style index is persisted and inserting at 1 would have flipped Blank
  users to Dim.
- Dim as an opaque black cover minus the lion (2026-09-10 to 12): a black
  screen burning backlight, no better than Off; replaced by the transparent
  cover over the live UI (`ec537ac`).
- The row names "Screen dim" and "Screensaver": renamed "Screen timeout"
  (it is a timeout, not a brightness) and "Idle screen" (a choice of what
  shows, one option literally called Dim).
- Random as the name of the alternating style: shipped as Alternate in
  v0.14.1 (the notes called it Random until release).

## Consequences

- The artwork lives in `ssart` (0 Lion, 1 Apollo, 2 Alternate), read only
  when the style is Logo; Dim and Off leave it alone so the choice survives
  a detour, and a rolled-back build simply bounces the lion.
- A modal on the top layer is parked by the artwork saver and restored on
  wake (found with the schedule's first-visit notice, `194dd92`).
- Screenshot: `../img/manual/device-display.png`.

## History

- 2026-09-04: bouncing lion, Logo / Blank, 5 % backlight (`3a328a6`).
- 2026-09-09: per-board floor, 1-minute timeout, shot gating (`c3feeef`,
  v0.12.0).
- 2026-09-10: Logo / Dim / Off under `ssstyle2`; rows renamed (`8d0962b`).
- 2026-09-12: Dim shows the live UI (`ec537ac`, v0.13.1).
- 2026-09-17: Lion / Apollo / Alternate, `ssart`, 6/10 height, saver on
  its own screen (`3f83449`, v0.14.1); Alternate swap HW-tested on the 4.3C.

## Sources

Commits `3a328a6`, `c3feeef`, `8d0962b`, `ec537ac`, `3f83449`;
`include/platform_esp32/board_config.h` (`kSaverDimPercent`),
`src/ui/app.cpp` (`kSaverSwapMs`); MANUAL.md Settings > Apollo > Display.
