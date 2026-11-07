# Dose and brew ratio: Record is the bench weight block, the chip builds in, out, ratio

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-10-30 · **Last changed:** 2026-11-07
- **Boards:** every board with the large Home layout; needs an espresso scale (the ratio needs a yield) and, for weighing, a bench scale

## Context

The bench scale on Home is a weight readout. Recording the dose from it and
showing the brew ratio must need no mode switch at Record time and no
second UI. Constraints: the X 8" swap-mode header has no spare width, a
tared cup and an untared portafilter must both work, and nothing may add a
line to the Home cards (`2026-11-01-home-cards-share-rows.md`).

## Decision

- **Record = the bench weight block.** Dormant it is the ordinary weight and
  caption. When the reading has held still ~1 s and the net dose is
  dose-sized, weight and caption become one outlined amber block captioned
  "Record 18.3 g"; tapped, a deeper fill and "✓ 18.3 g recorded" until the
  weight changes. A recorded dose LOCKS the card: nothing more is offered,
  whatever lands on the bench, until the × on the chip, the shot or a
  walk-away clear ([why](#evidence)); a staged default never locks it.
  Disconnect and Tare stay in every layout. The block is
  sized to the body, never the body to the block
  ([what lost](#alternatives-and-dead-ends)).
- **Candidate gate and containers.** Dose = the STEP from a BASE plateau to
  the current one, minus the heaviest stored container not heavier than the
  step ([why the step](#evidence)). The base is sticky: a small adjustment
  (a step of at most 30 g either way that leaves the total positive) keeps
  it, so a dose weighed in stages is offered whole and a few beans taken
  back out offer the remainder; a container-sized landing re-bases on the
  plateau just left, a lift on the new one
  ([why sticky](#evidence)). Capture refuses
  a container under 50 g or within the dose range of another; a reading
  equal to a container is the empty container, 0 g.
- **The chip in the graph** builds up in words
  ([why no arrow](#alternatives-and-dead-ends)): "18.2 g in ×", at shot
  start "--.- g out", live during the shot, latched at review as "36.4 g
  out  1:2.0" with the ratio in bench amber. It sits ON TOP of the legend in
  a left-flush column; only its × is a control.
- **Walk-away clearing**, silent: screensaver start, a scale link down for
  30 s (once it has been seen up since the Record), Micra to standby, or
  60 min. A correction is × then Record ([was re-Record replaces](#history)).
- **Storage:** `ShotSummary.dose_g` and `dose_src` (recorded / default) as
  trailing CSV columns; History shows the ratio.
- **Opt-in** ([since v1.3.0-beta.2](#history)): "Record dose" is off until
  turned on; "Default dose" is its own switch whose 5 to 30 g controls exist
  only while it is on.

| constant | value |
|---|---|
| dose range offered | 5 to 30 g |
| container floor | 50 g |
| stillness | 0.2 g band for 1,000 ms |
| link-lost grace | 30 s |
| backstop | 60 min |
| containers | 2 |

## Consequences

- Two containers ([were three](#history)); a `dosec2` written by beta.1 is
  left unread.
- `dosen` defaults to false with no migration ([why acceptable](#history));
  `dosedef` keeps its beta.1 meaning (0 / absent = off), the switch's memory
  is the added `dosedeflast`.
- Screenshots: `../img/manual/home-dose-recorded.png`,
  `../img/manual/scale-shot-settings.png`, `../img/manual/shot-card.png`.

---

## Details

### Evidence

- Realistic-kit sweep (`tools/dose_test.cpp`, 2026-10-30, requested):
  four kits x doses 8 to 28 g, tared and untared, milk 30 to 400 g: 0 wrong
  doses; 73 look-alike offers (milk landing 5 to 50 g above a container)
  which weight alone cannot separate and which the stillness gate hides
  while pouring. The step rule and the 30 g gate cut them to 58 over more
  cases (`8a7bcb5`).
- X-8 soak 2026-10-31: the raw-reading rule offered "Record 48.3 g" (never a
  dose; the biggest baskets take ~28 g) and demanded a tare first. Both
  fixed by the step rule (so the scale's zero never matters) and
  `kDoseMaxG` 50 -> 30.
- Bench 2026-11-07: beans weighed into a tared cup in two pours, 8 g then
  10 g, offered "Record 8.0 g" and then nothing: the step was measured from
  the previous plateau, so the top-up read as 2 g. Fixed by the sticky base
  (the plateau a load is measured from survives small adjustments). The
  host sweep's look-alike count is unchanged (29) and a staged milk pour now
  offers nothing once past a dose where it offered its last step before.
  Dropping the step rule for the raw reading was weighed and rejected: the
  stored containers never needed it either way, but it keeps an untared,
  unstored cup and a drifted zero working for free, and taring makes the two
  behave the same.
- Lock, 2026-11-07: with "re-Record replaces", the 1 g "same weight" band
  decided whether a top-up after a Record re-offered, and anything else
  landing (a jug, a cup) pulled an offer while a dose waited for its shot.
  A timed "recorded" (a few seconds, then offer again even for the same
  weight) was weighed; the lock won because it needs no band and no timer
  and keeps the card quiet until the shot. The band now decides only the
  caption. A correction after a Record costs the × and a second tap.
- The container floor's refusal text: "too light to tell apart from a dose,
  tare it instead".
- RAM: +16 B static for the plateaus (X-8 build 94,864 B static after
  `3df12b0`); the sticky base replaced the previous-plateau fields one for
  one.

### Alternatives and dead ends

- Candidate A, a pill in the SCALE card header: fits at 800 px, no room in
  the X 8" swap-mode header. Rejected 2026-10-30.
- Outline the number only: clipped the caption beside the graph.
- An arrow glyph ("›") between the chip's parts: in testing it read as
  greater-than; replaced by words.
- A terse unit-less chip beside the graph (`8a7bcb5`): replaced the next day
  by stacking the legend's two entries so the full chip fits (`bcc5eb1`).
- A dot on the dose chip: dropped, the legend's amber dot already names the
  bench series (`f492fa2`).
- Confirmation toasts for Record / Capture / clear: dropped, the block and
  chip are the feedback; refusals still toast.
- A "− Off +" stepper as the default-dose control: rejected, a bottom notch
  that is a different mode; replaced by a switch (`0d3084a`).

### History

- 2026-10-24: future dosing workflow sketched with the bench scale (which
  v1.2.0 added as a weight readout): bench slot a full IScale, `dose_g` as
  an appended CSV column, no rework allowed.
- 2026-10-30: design settled on sim mockups; built `4d1d5d6`, reviews
  `c2ca58c`.
- 2026-10-31: step rule, 30 g gate, live ratio, History cell (`8a7bcb5`).
- 2026-11-01: default dose, provenance, two containers ("two is plenty";
  three before), chip above a live legend (`3df12b0`); shipped in
  v1.3.0-beta.1 (release commit `9f8379e`, 2026-11-01; the tag sits at
  `71e90bd`, 2026-11-02).
- 2026-11-02: Record dose opt-in, Default dose its own switch (`0d3084a`,
  v1.3.0-beta.2); `dosen` flipped to default false with no migration: a
  beta.1 unit that never toggled it sees the feature off and turns it on
  once by hand, acceptable because the beta.1 units were all bench units.
- 2026-11-07: the base plateau made sticky across small adjustments so a
  dose weighed in stages is offered whole; a recorded dose locks the card
  until × / the shot (re-Record replaced before) (feat/v1.4, for v1.4.0).

### Sources

Commits `4d1d5d6`, `c2ca58c`, `8a7bcb5`, `bcc5eb1`, `3df12b0`, `f492fa2`,
`0d3084a`; `include/core/dose.h` (the constants in the table),
`tools/dose_test.cpp` (`test_staged_dose`),
`include/core/dose_settings.h`; `src/platform_esp32/config.cpp` (`dosen`,
`dosec0`/`dosec1`, `dosedef`, `dosedeflast`); MANUAL.md "Recording your
dose".
