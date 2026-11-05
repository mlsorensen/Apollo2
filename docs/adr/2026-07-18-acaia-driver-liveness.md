# Acaia driver: weight-stream liveness, the excursion hold, and no vendor references

- **Status:** Accepted (one observation open: the excursion hold once fired on a Lunar; whether the Lunar runs the same internal cycle is unproven)
- **Area:** radio
- **First decided:** 2026-07-18 · **Last changed:** 2026-07-27
- **Boards:** all

## Context

On 2026-07-18 the single Bookoo link became a generic `ScaleLink` over
`core::IScaleDriver`, and an Acaia family driver landed with the generation
picked from the discovered GATT: Umbra (service `fe40`,
write-without-response), new Lunar/Pyxis (service `49535343`,
write-with-response), legacy Lunar/Pearl (one characteristic, `2a80`, for
commands and weight). Two problems followed on
hardware: the Umbra's weight stream showed a blip about once a minute, and
the Lunar would sit "connected" and never deliver a weight.

## Decision

**Repo rule (c86d67c, 2026-07-18).** Code and comments state protocol
facts on their own terms (observed frames, canonical client behaviour) and
never point at a reference implementation, a vendor app or its source. This
record follows the same rule.

**Umbra: never poll it.** A 60 s get-status poll made the Umbra emit a
bogus constant weight (~-10 g for ~0.5 s) on every poll (8745696). The
driver sends nothing after init; battery rides the ~850 ms status frames
the scale pushes on its own.

**Umbra excursion hold (d545221, ce283fa).** Every ~62 s the Umbra
interrupts its stream for ~800 ms (~12 frames) with a settling ramp of
small negative values unrelated to pan load, terminated by an exact 0.0.
Purpose unknown; the detector keys on the signature only. `accept_weight`
opens a hold on a step from the published baseline into the band
(w < -0.5 g, > -20 g, > 2.5 g step), publishes nothing while holding, and
exits on return to baseline, on any out-of-band frame (physical contact),
or after `kHoldTimeoutMs` = 1,000 ms. Zero added latency otherwise.

**Lunar/Pyxis dead link (9bddfe2, 2026-07-27).** Liveness is now the
WEIGHT stream, not any notification: every generation streams weight
continuously while connected. The handshake is reactive and self-correcting.

| Rule | Value |
|---|---|
| No weight -> re-handshake (`kWeightStallMs`) | 5 s |
| No weight -> drop the link (`kWeightDeadMs`) | 30 s |
| Lunar/Pyxis status poll = keepalive (`kStatusPollMs`) | 2 s |
| Reactive-reply rate limit (`kHandshakeMinGapMs`) | 2 s |
| Silence after CCCD before our first write (`kInitialQuietMs`) | 3 s, Lunar/Pyxis only |
| Event registration | 9 bytes `{9, 0,1, 1,2, 2,5, 3, 4}` (key and setting take no interval byte) |
| cmd-0 heartbeat | legacy generation only |

Frame lengths come from a per-command table (fixed-length commands exist
and had mis-framed everything after them), checksums are verified, bundled
events are skipped by their real length, and a decimals byte of 0 means
/100. The Umbra keeps its proactive `start()`: it was the generation that
already worked, so it is the control, not the patient.

## Evidence

| Observation | Unit / date |
|---|---|
| Blip on every status poll, without fail | Umbra, 2026-07-18 |
| Hold fires at a 62.0 to 62.1 s cadence, all exits via return-to-baseline | Umbra, 2026-07-18/19 |
| `no weight for 6110 ms (link 5711 ms) -> re-handshaking`, then weight flowed | Lunar fw 1.0.19, 2026-07-26 |
| Scale pushes cmd 7 unprompted, twice, after CCCD enable | Lunar, 2026-07-26 |
| Connect ~6 s after pressing Connect, reliably | Lunar, bench, 2026-07-26 |

The 6 s includes the deliberate 3 s quiet period. We overruled a
300 ms shortcut: assume the reference timing is load-bearing when copying a
protocol understood only from the outside. Do not optimise it away.

## Alternatives and dead ends

- For the blip, in order: weight-type flag filters (every Umbra frame
  carries both type bits, so none can work), median-of-5 (masking plus lag,
  and a 12-frame burst outlasts it), a "minus 5 g" value latch (the ramp
  crosses any threshold). Commits f9eadd1, 43b92e8, 7fa05b1, b3caf50,
  d1947ae.
- Status polling the Umbra at 2 s like the Lunar: reintroduces the blip.

## Consequences

- A cmd-8 status arriving on the Umbra must NOT re-send the registration
  unconditionally (it pushes one every ~850 ms); the driver gates it on "no
  weight seen yet" there. A harness assertion covers it.
- Open: on 2026-07-26 the hold fired on the Lunar at 68.6 s
  (`w=-3.8 baseline=0.1`). Either the Lunar runs a similar cycle or a tared
  cup was lifted; worst case is 1 s of latency on a small negative change.
  Do not gate the hold to the Umbra without evidence.
- "Connected" for every scale means a weight has arrived since the link
  came up (95f792f, 2026-10-17), so Auto shot cannot arm on a link without a
  stream.

## History

- 2026-07-18: driver abstraction + Acaia family (2c5ca05); blip hunt through five filters to the mechanism-based hold (d545221); status poll removed (8745696); vendor-reference comments neutralised (c86d67c).
- 2026-07-19: hold releases on contact, timeout 1.0 s (ce283fa).
- 2026-07-27: Lunar dead-link fix: weight liveness, 9-byte registration, length table, reactive handshake, 3 s quiet (9bddfe2).
- 2026-10-17: connected = streaming for all scales (95f792f).

## Sources

- Commits 2c5ca05, 8745696, b3caf50, d1947ae, d545221, ce283fa, c86d67c, 9bddfe2, 95f792f.
- `src/core/scale_driver_acaia.cpp`, `src/core/scale_link.cpp`.
