# Shot detect (unwired) mode: the weight stream drives the shot state machine

- **Status:** Open (shipped and in daily use; the detector thresholds are untuned guesses, there is no UI escape from a false detection mid-shot, and the manual stopwatch was never built)
- **Area:** radio
- **First decided:** 2026-07-19 · **Last changed:** 2026-10-17
- **Boards:** all

## Context

Brew-by-weight (2026-07-18) drives `core::BrewController` from paddle
edges and can only run on a board with the paddle harness. Users without
the harness still wanted the shot review: timer, frozen weight, the
shot-aligned graph. The Micra exposes no shot state over local BLE (the
read characteristic answers machineMode, boilers, tankStatus and the like;
no notifications), so the only shot signal on an unwired rig is the scale.

## Decision

- A pure-core `ShotDetector` fed from the scale snapshot drives the SAME
  phases (Idle -> Brewing -> Settling -> Review); the phase-keyed freeze,
  review hold and Reset work unchanged. `ShotTimer.start(t)` takes a
  retroactive stamp, so the duration is honest despite the late confirm.
- Shot mode is a tri-state `core::ShotMode` Manual / Auto / Detect
  (ded5d27, 2026-07-24; NVS `shotmd`): a wired rig can run the detector too,
  the paddle still relays pass-through, and a persisted Auto without a relay
  degrades to Detect. No scale paired means Manual whatever is stored
  (74eef72, 2026-10-01).
- Detector v1 = confirm-after-sustain. We set the sustain at 5 s
  (not 2.5) and made the 8 s minimum-shot gate universal (6e6319d).

| Constant | Value |
|---|---|
| Idle -> Candidate (`kStartFlowGps`) | 0.4 g/s |
| Candidate dies below (`kKeepFlowGps`) | 0.3 g/s |
| Confirm delay (`kSustainMs`) / net gain (`kConfirmNetG`) | 5 s / 2 g |
| Single-sample step = cup placed (`kStepAbortG`) | 5 g |
| End below (`kEndFlowGps`) for (`kEndSustainMs`) | 0.2 g/s / 2 s |
| Cup pulled (`kPullNegG`) | -5 g |
| Tolerated blackout / runaway cap | 4 s / 120 s |
| Minimum shot, all modes (`kMinShotMs`) | 8 s |
| Unwired minimum gain (`kUnwiredMinShotG`) | 5 g |
| Detect lead-in (user, NVS `leadin`) | 0 to 10 s, default 3 |

- Mid-shot handoff (4110485, 2026-08-09): at confirm the always-on sample
  ring is handed to the LIVE shot plot in place, trimmed and rebased to the
  backdated start, so graph, net weight and timer snap to the whole shot at
  detection instead of at review. "Detect lead-in" adds the preinfusion time
  to the retroactive start so t=0 is lever-on, matching wired.
- Stop-early hint: the running shot reaching target minus the hint overshoot
  minus 0.25 s of current flow (`kStopHintLeadS`) strobes the Home pill
  (~5 Hz for 3 s, one click up front) so it registers in peripheral vision.
  The relay auto-stop itself uses NO flow term.
- No manual start button when a scale is present (a product decision): detector only,
  plus Reset.

## Evidence

- Built 2026-07-21 on the 4.3C; the first test surfaced a hidden shot
  button, scan starvation while the Micra was Connecting, per-pulse beeps
  (dropped for one click), and a stuck warn fill after a flash cancel; all
  fixed the same day.
- We test unwired on the wired 4.3C by toggling the setting; the
  pass-through relays the physical paddle in both modes (8f0df4f).

## Alternatives and dead ends

- Probing the Micra's read characteristic with speculative setting names
  for a live shot state: proposed 2026-07-19, never run.
- A ~30 s retrospective look-back reclassifier: deferred as later tuning;
  the always-on ring exists to enable it.
- Auto shot without a scale: refused with a toast (4110485). A "Start
  anyway" override was considered and dropped on 2026-10-01 in favour of
  Manual mode as the way out (see the paddle-override record).

## Consequences

- The hint's overshoot is learned separately from the relay's (see the
  overshoot record); a detected shot the user simply stopped teaches nothing.
- Open: the shot button is disabled while a shot is active, so a false
  positive can only be ended by a Settings flip or by waiting; the detector
  constants and `kStopHintLeadS` are HW-tuning guesses.
- "Connected" means a weight has arrived (95f792f, 2026-10-17), so the
  detector never arms on a link without a stream.

## History

- 2026-07-19: designed.
- 2026-07-21: built: detector, wired-paddle toggle, shot-aligned review, stop hint (11046d5); 5 s sustain + 8 s gate (6e6319d); shot-fitted review window (830190c).
- 2026-07-22: pass-through in unwired mode (8f0df4f); merged (1fc9c6b), shipped in v0.4.0 (2026-07-26).
- 2026-07-24: tri-state shot mode (ded5d27).
- 2026-08-09: live handoff at detection, Detect lead-in, no-scale refusal (4110485), v0.6.0.
- 2026-10-01: no saved scale = Manual (74eef72).

## Sources

- Commits 11046d5, 6e6319d, 830190c, 8f0df4f, 1fc9c6b, ded5d27, 4110485, 74eef72, 95f792f; tags v0.4.0, v0.6.0.
- `include/core/shot_detector.h`, `src/core/shot_detector.cpp`, `include/core/brew_controller.h`, `src/core/brew_controller.cpp`.
- Paddle-side rules: `2026-08-24-paddle-override-without-scale.md`.
