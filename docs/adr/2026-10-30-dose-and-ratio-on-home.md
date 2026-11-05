# Dose and brew ratio: Record is the bench weight block, the chip builds in, out, ratio

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-10-30 · **Last changed:** 2026-11-02
- **Boards:** every board with the large Home layout; needs an espresso scale (the ratio needs a yield) and, for weighing, a bench scale

## Context

v1.2.0 added a bench scale as a weight readout. The next step
(sketched 2026-10-24, designed 2026-10-30 from sim mockups) was to record the
dose from that scale and show the brew ratio, without a mode switch at
Record time and without a second UI for it. Constraints: the X 8" swap-mode
header has no spare width, a tared cup and an untared portafilter must both
work, and nothing may add a line to the Home cards (see
`2026-11-01-home-cards-share-rows.md`).

## Decision

- **Record = the bench weight block.** Dormant it is the ordinary weight and
  caption. When the reading has held still ~1 s and the net dose is
  dose-sized, weight and caption become one outlined amber block whose
  caption reads "Record 18.3 g" (the dose it would store). Tapped: a deeper
  fill and "✓ 18.3 g recorded" until the weight changes. Disconnect and Tare
  stay in every layout. The block is sized to the body, never the body to
  the block.
- **Candidate gate and containers.** Dose = the STEP from the previous
  stable plateau to the current one (so the scale's zero never matters),
  minus the heaviest stored container not heavier than the reading. Capture
  refuses a container under 50 g ("too light to tell apart from a dose, tare
  it instead") and one within the dose range of another stored container.
  A reading equal to a container is the empty container, 0 g.
- **The chip in the graph** builds up in words, no arrow glyph (in testing
  "›" read as greater-than): "18.2 g in ×", then at shot start "--.- g out",
  live during the shot, latched at review as "36.4 g out  1:2.0" with the
  ratio in bench amber. It sits ON TOP of the legend in a left-flush column;
  only its × is a control.
- **Walk-away clearing**, silent: screensaver start, a scale link down for
  30 s (counted only once it has been seen up since the Record), Micra to
  standby, or 60 min. Re-Record replaces.
- **Storage:** `ShotSummary.dose_g` and `dose_src` (recorded / default) as
  trailing CSV columns; History shows the ratio.
- **Opt-in** since 2026-11-02: "Record dose" is off until turned on, and
  "Default dose" is its own switch with 5 to 30 g controls that exist only
  while it is on.

| constant | value |
|---|---|
| dose range offered | 5 to 30 g |
| container floor | 50 g |
| stillness | 0.2 g band for 1,000 ms |
| link-lost grace | 30 s |
| backstop | 60 min |
| containers | 2 |

## Evidence

- Realistic-kit sweep (`tools/dose_test.cpp`, 2026-10-30, requested):
  four kits x doses 8 to 28 g, tared and untared, milk 30 to 400 g: 0 wrong
  doses; 73 look-alike offers (milk landing 5 to 50 g above a container)
  which weight alone cannot separate and which the stillness gate hides
  while pouring. The step rule and the 30 g gate cut them to 58 over more
  cases (`8a7bcb5`).
- X-8 soak 2026-10-31: the raw-reading rule offered "Record 48.3 g" (never a
  dose; the biggest baskets take ~28 g) and demanded a tare first. Both
  fixed by the step rule and `kDoseMaxG` 50 -> 30.
- RAM: +16 B static for the plateaus (X-8 build 94,864 B static after
  `3df12b0`).

## Alternatives and dead ends

- Candidate A, a pill in the SCALE card header: fits at 800 px, no room in
  the X 8" swap-mode header. Rejected 2026-10-30.
- Outline the number only: clipped the caption beside the graph.
- A terse unit-less chip beside the graph (`8a7bcb5`): replaced the next day
  by stacking the legend's two entries so the full chip fits (`bcc5eb1`).
- A dot on the dose chip: dropped, the legend's amber dot already names the
  bench series (`f492fa2`).
- Confirmation toasts for Record / Capture / clear: dropped, the block and
  chip are the feedback; refusals still toast.
- A "− Off +" stepper as the default-dose control: we rejected a
  bottom notch that is a different mode; replaced by a switch (`0d3084a`).

## Consequences

- Three containers became two (2026-11-01, "two is plenty"); a `dosec2`
  from beta.1 is left unread.
- `dosen` default flipped to false in beta.2 with no migration: a beta.1
  unit that never toggled it sees the feature off and turns it on once by
  hand (the beta.1 units were all bench units). `dosedef` keeps its beta.1
  meaning (0 / absent = off); the switch's memory is the added `dosedeflast`.
- Screenshots: `../img/manual/home-dose-recorded.png`,
  `../img/manual/scale-shot-settings.png`, `../img/manual/shot-card.png`.

## History

- 2026-10-24: future dosing workflow sketched with the bench scale: bench
  slot a full IScale, `dose_g` as an appended CSV column, no rework allowed.
- 2026-10-30: design settled on mockups; built `4d1d5d6`, reviews `c2ca58c`.
- 2026-10-31: step rule, 30 g gate, live ratio, History cell (`8a7bcb5`).
- 2026-11-01: default dose, provenance, two containers, chip above a live
  legend (`3df12b0`); shipped in v1.3.0-beta.1 (release commit `9f8379e`,
  2026-11-01; the tag sits at `71e90bd`, 2026-11-02).
- 2026-11-02: Record dose opt-in, Default dose its own switch (`0d3084a`,
  v1.3.0-beta.2).

## Sources

Commits `4d1d5d6`, `c2ca58c`, `8a7bcb5`, `bcc5eb1`, `3df12b0`, `f492fa2`,
`0d3084a`; `include/core/dose.h` (the constants in the table),
`include/core/dose_settings.h`; `src/platform_esp32/config.cpp` (`dosen`,
`dosec0`/`dosec1`, `dosedef`, `dosedeflast`); MANUAL.md "Recording your
dose".
