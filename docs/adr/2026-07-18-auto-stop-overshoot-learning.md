# Auto-stop fires at target minus a learned overshoot, taught only by target-triggered stops

- **Status:** Accepted (a Reset action is planned, issue #7, v1.4.0)
- **Area:** radio
- **First decided:** 2026-07-18 · **Last changed:** 2026-11-04
- **Boards:** all with a paddle drive (the hint variant on every board)

## Context

The Micra exposes no brew start/stop over BLE, so brew-by-weight means
Apollo drives the paddle line itself and cuts it on weight. The cup keeps
filling after the cut (drip tail plus relay and scale latency), so the stop
must come early by an amount that depends on grind, basket and flow. The
pattern was inherited from an earlier relay controller: trigger at
target minus overshoot and adapt the overshoot from each shot's settled
final weight.

## Decision

`core::BrewController` (59999de, 2026-07-18):

- Auto-stop when `weight - start >= target - overshoot_g_`.
- At the settle freeze, `err = settled - target` (positive = overshot) and
  `learn_overshoot`: absorb a fraction, clamp, persist on a move worth
  storing. Only TARGET-TRIGGERED stops teach. The relay cut
  (`auto_stopped_`) teaches `overshoot_g_`; a Detect-mode shot where the
  stop HINT fired teaches a separate `hint_overshoot_g_`, which also contains
  the user's reaction time (0d099bd, 2026-07-27). A shot the user simply
  chose to stop, blown past or not, teaches nothing in either mode.
- The settle ENDS when the drips stop, not on a fixed timer, because the
  weight at its end is what the learner grades against; the window also
  requires fresh samples so a stalled stream cannot read as "settled".
- The learner is independent of History: it runs in the brew controller at
  the freeze. Deleting a shot from History does not undo what it taught.

| Constant | Value |
|---|---|
| Default overshoot (NVS `ovshoot`), hint overshoot (NVS `hintovs`) | 2.0 g each |
| Learn rate (`kOvershootLearnRate`) | 0.5 of the error per shot |
| Clamp (`kOvershootMaxG`) | 0 to 8 g |
| NVS dead-band (`kOvershootPersistEpsG`) | 0.05 g, measured against what is on flash |
| Settle: ceiling / floor / window / calm windows / rate | 3 s / 1 s / 500 ms / 2 / 0.2 g/s |
| Minimum shot before settle or learning (`kMinShotMs`) | 8 s |
| Stop hint lead (`kStopHintLeadS`) | 0.25 s of current flow |

## Evidence

- Observed 2026-11-03/04: after a bean change slowed shots from ~35 s to
  ~42 s, a 52 g target landed 48.9, 47.1 and 49.7 g. Half the error per shot
  means a drip tail that moved by a few grams takes three or four shots to
  chase, and a learner tracking tail variance shows up as short shots in the
  meantime. The learned value is invisible and there is no way to clear it
  short of a settings restore.
- 0.2 g/s over a 500 ms window is 0.1 g, one LSB on a 0.1 g scale, so a
  single calm window is noise-dominated; two consecutive ones make the test
  mean "settled".

## Alternatives and dead ends

- A flow term in the auto-stop itself (as the hint has): we confirmed
  the relay stop is target minus the learned offset only.
- One shared overshoot for relay and hint: the two fought each other on a
  wired rig running Detect.
- Writing the estimate to NVS every shot: converged, it jitters by
  hundredths of a gram, finer than the scale resolves.
- A fixed 3 s settle: too short learns from a still-filling cup, too long
  waits on a scale that stopped moving.

## Consequences

- Fail-open rule from the original design: if the scale drops mid-shot the
  line is never stranded (a 4 s blind grace, then the shot ends).
- Issue #7 (2026-11-04, target v1.4.0): a "Learned drip: N g" line and a
  Reset button under Shot settings, resetting both estimates to 2.0 g through
  the existing persisters (values only, no new NVS key).

## History

- 2026-07-18: brew-by-weight shipped on the 4.3C, 50 %/shot, 0 to 8 g clamp (59999de).
- 2026-07-21: 8 s minimum-shot gate made universal (6e6319d).
- 2026-07-27: drip-stop settle, separate hint overshoot, NVS dead-band (0d099bd).
- 2026-11-04: short shots after a bean change observed; issue #7 opened.

## Sources

- Commits 59999de, 6e6319d, 0d099bd; issue #7.
- `include/core/brew_controller.h`, `src/core/brew_controller.cpp`, `src/platform_esp32/config.cpp`.
