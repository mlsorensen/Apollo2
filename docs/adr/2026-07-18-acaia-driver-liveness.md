# Acaia driver: weight-stream liveness, the excursion hold, and no vendor references

- **Status:** Accepted (one observation open: the excursion hold once fired on a Lunar; whether the Lunar runs the same internal cycle is unproven)
- **Area:** radio
- **First decided:** 2026-07-18 · **Last changed:** 2026-11-05
- **Boards:** all

## Context

The Acaia driver picks its generation from the discovered GATT: Umbra, new
Lunar/Pyxis, legacy Lunar/Pearl ([services](#dd-generations)). The protocol
is understood only from the outside, and the generations differ: the Umbra
pushes its own status and interrupts its stream about once a minute, while
a Lunar can sit "connected" with no weight flowing ([evidence](#evidence)).

## Decision

- **No vendor references.** Code and comments state protocol facts on their
  own terms (observed frames, canonical client behaviour) and never point at
  a reference implementation, a vendor app or its source ([history](#history)).
- **Umbra: never poll it.** Nothing is sent after init; battery rides the
  status frames the scale pushes on its own ([a poll made it emit a bogus
  weight](#ev-blip)).
- **Umbra excursion hold.** `accept_weight` opens a hold on a step from the
  published baseline into a small negative band, publishes nothing while
  holding, and exits on return to baseline, on any out-of-band frame
  (physical contact), or after `kHoldTimeoutMs` = 1,000 ms. Zero added
  latency otherwise ([band and signature](#dd-hold), [why not a
  filter](#alternatives-and-dead-ends)).
- **Lunar/Pyxis: liveness is the WEIGHT stream**, not any notification.
  No weight for `kWeightStallMs` = 5 s re-handshakes; `kWeightDeadMs` = 30 s
  drops the link. The handshake is reactive and self-correcting, with a 3 s
  quiet period before our first write ([constants](#constants), [the
  dead-link fix](#ev-lunar)).
- Every frame is checked against a per-command length table and its
  checksum ([framing rules](#dd-framing)).
- "Connected" for every scale means a weight has arrived since the link
  came up ([history](#history)).

## Consequences

- A cmd-8 status arriving on the Umbra must NOT re-send the registration
  unconditionally (it pushes one every ~850 ms); the driver gates it on "no
  weight seen yet" there. A harness assertion covers it.
- The 3 s quiet period is load-bearing ([a 300 ms shortcut was
  overruled](#ev-quiet)). Do not optimise it away.
- Auto shot cannot arm on a link without a stream.
- The Lunar/Pyxis status poll is the keepalive that holds the scale awake,
  so the driver declares `sleeps_on_disconnect()` for that generation and
  `ScaleLink` drops it while the remote is idle (the parked-while-idle
  record). The Umbra does not declare it: unpaired it drops the link on its
  own timer while the remote idles and powers off a few minutes later (a
  drop by us would only start that countdown sooner); paired, partner mode
  sends it to sleep.
- Open: [the hold once fired on a Lunar](#ev-lunar-hold); worst case is 1 s
  of latency on a small negative change. Do not gate the hold to the Umbra
  without evidence.

---

## Details

### Design detail

<a id="dd-generations"></a>
Generations by GATT: Umbra (service `fe40`, write-without-response), new
Lunar/Pyxis (service `49535343`, write-with-response), legacy Lunar/Pearl
(one characteristic, `2a80`, for commands and weight). The Umbra keeps its
proactive `start()`: it was the generation that already worked, so it is the
control, not the patient.

<a id="dd-hold"></a>
Hold band: w < -0.5 g, > -20 g, > 2.5 g step from the published baseline.
The signature it keys on: every ~62 s the Umbra interrupts its stream for
~800 ms (~12 frames) with a settling ramp of small negative values
unrelated to pan load, terminated by an exact 0.0. Purpose unknown; the
detector keys on the signature only ([cadence measured](#ev-hold)).

<a id="constants"></a>

| Rule | Value |
|---|---|
| No weight -> re-handshake (`kWeightStallMs`) | 5 s |
| No weight -> drop the link (`kWeightDeadMs`) | 30 s |
| Lunar/Pyxis status poll = keepalive (`kStatusPollMs`) | 2 s |
| Reactive-reply rate limit (`kHandshakeMinGapMs`) | 2 s |
| Silence after CCCD before our first write (`kInitialQuietMs`) | 3 s, Lunar/Pyxis only |
| Event registration | 9 bytes `{9, 0,1, 1,2, 2,5, 3, 4}` (key and setting take no interval byte) |
| cmd-0 heartbeat | legacy generation only |

<a id="dd-framing"></a>
Frame lengths come from a per-command table because fixed-length commands
exist and had mis-framed everything after them; checksums are verified,
bundled events are skipped by their real length, and a decimals byte of 0
means /100.

### Evidence

<a id="ev-blip"></a>

| Observation | Unit / date |
|---|---|
| A 60 s get-status poll made the scale emit a bogus constant weight (~-10 g for ~0.5 s) on every poll, without fail | Umbra, 2026-07-18 |

<a id="ev-hold"></a>

| Observation | Unit / date |
|---|---|
| Hold fires at a 62.0 to 62.1 s cadence, all exits via return-to-baseline | Umbra, 2026-07-18/19 |

<a id="ev-lunar"></a>

| Observation | Unit / date |
|---|---|
| `no weight for 6110 ms (link 5711 ms) -> re-handshaking`, then weight flowed | Lunar fw 1.0.19, 2026-07-26 |
| Scale pushes cmd 7 unprompted, twice, after CCCD enable | Lunar, 2026-07-26 |
| Connect ~6 s after pressing Connect, reliably | Lunar, bench, 2026-07-26 |

<a id="ev-quiet"></a>
The 6 s includes the deliberate 3 s quiet period. We overruled a 300 ms
shortcut: assume the reference timing is load-bearing when copying a
protocol understood only from the outside.

<a id="ev-lunar-hold"></a>
On 2026-07-26 the hold fired on the Lunar at 68.6 s (`w=-3.8 baseline=0.1`).
Whether the Lunar runs a cycle like the Umbra's or a tared cup was lifted is
not established.

### Alternatives and dead ends

- For the blip, in order: weight-type flag filters (every Umbra frame
  carries both type bits, so none can work), median-of-5 (masking plus lag,
  and a 12-frame burst outlasts it), a "minus 5 g" value latch (the ramp
  crosses any threshold). Commits f9eadd1, 43b92e8, 7fa05b1, b3caf50,
  d1947ae.
- Status polling the Umbra at 2 s like the Lunar: reintroduces the blip.

### History

- 2026-07-18: the single Bookoo link became a generic `ScaleLink` over `core::IScaleDriver`, and the Acaia family driver landed with the generation picked from the GATT (2c5ca05); blip hunt through five filters to the mechanism-based hold (d545221); status poll removed (8745696); vendor-reference comments neutralised and the repo rule set (c86d67c).
- 2026-07-19: hold releases on contact, timeout 1.0 s (ce283fa).
- 2026-07-27: Lunar dead-link fix: weight liveness, 9-byte registration, length table, reactive handshake, 3 s quiet (9bddfe2).
- 2026-10-17: connected = streaming for all scales (95f792f).
- 2026-11-05: `sleeps_on_disconnect()` on the driver interface, true for the Lunar/Pyxis generation only; the idle drop lives in `ScaleLink` (issue #10, feat/v1.4).

### Sources

- Commits 2c5ca05, 8745696, b3caf50, d1947ae, d545221, ce283fa, c86d67c, 9bddfe2, 95f792f.
- `src/core/scale_driver_acaia.cpp`, `src/core/scale_link.cpp`.
