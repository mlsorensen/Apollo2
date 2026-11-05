# Micra link resilience: the stalled-request watchdog and the "machine wedged" rule

- **Status:** Open (the watchdog stands; a re-dial whose service discovery stalls still outruns the mask, and the cause of the stalls is a correlation, not a proven mechanism)
- **Area:** radio
- **First decided:** 2026-10-05 · **Last changed:** 2026-11-04
- **Boards:** all (evidence from the P4 X-8)

## Context

The Micra link dials the saved address from a background task with
auto-reconnect (no scan at startup, discovery in Settings, Home showing the
link state). The protocol has no notifications, so every read is a request
the host waits on, and the prebuilt NimBLE host terminates the connection
itself after its ~30 s GATT procedure timeout. Two field problems shape the
rules: a machine whose own Bluetooth refuses connections, and requests that
stall the full 30 s while a partner-mode Umbra holds us as its slave
([evidence](#evidence)).

## Decision

- **Diagnosis rule.** Repeated `MicraLink: connect failed` with NO auth line
  is the machine's Bluetooth refusing connections (or another central
  holding its single slot): ask for a Micra restart before suspecting the
  token or the radio ([the wedged machine](#ev-wedged)). A bad token reads
  `token rejected -> NeedsToken`; an empty token never leaves NeedsToken. A
  scale failing to connect in the same log is not evidence of a radio fault.
- **Watchdog.** `MicraLink` stamps each request in flight;
  `abort_stalled_request()` from main's loop drops the link after
  `kRequestStallMs` = 5 s and the loop re-dials at once ([why not wait out
  the 30 s](#alternatives-and-dead-ends)). The link reads Connected until
  the re-dial resolves, for at most `kRecoverMaskMs` = 15 s; a failed
  re-dial publishes Disconnected. Commands wait in their mailboxes and land
  after the re-dial ([bench](#ev-firings)). The power button's "Working..."
  hold is 12 s to cover it ([history](#history)).
- A machine left in configuration mode presents the seed characteristic and
  not the operative service; the link logs it and the UI says "restart it"
  (see the pairing-token record).

## Consequences

- ~10 s of stale state per stall instead of ~35 s, no flicker in the common
  case. A tighter 3 s threshold is an idea, not scheduled work ([every
  firing logged at ~5,000 ms](#ev-firings)).
- Open gap: a re-dial can connect at once and then stall in service
  discovery past the 15 s mask; the link then goes Connected -> Disconnected
  -> Connected honestly and `ReadyChime` re-arms ([observed](#ev-firings)).
  Not taken: a discovery-phase watchdog, or a chime latch that survives a
  short drop.
- The stall cause is a correlation with the Umbra's slave scheduling, [not a
  proven mechanism](#ev-umbra); the Umbra refuses the parameter changes that
  would test it ([dead ends](#alternatives-and-dead-ends)).

---

## Details

### Evidence

<a id="ev-wedged"></a>
Wedged machine, 2026-10-05: a freshly flashed P4-5 with settings restored
from a card never connected; we suspected the restored token. The Micra's
own Bluetooth was wedged and a machine restart fixed it.

<a id="ev-umbra"></a>
Nuisance drops, 2026-10-31: several Micra reconnects a day on the X-8 once
an Umbra was paired in partner mode. Every drop was reason 0x216 "local
host terminated": we ended the link, never a supervision timeout. Timed
reads named the path: a refresh read FAILED after 30,004 ms, once service
discovery of a fresh reconnect at 31 s.

| Observation | Value | Unit / date |
|---|---|---|
| Drop reason | 0x216, every time | X-8, 2026-10-31 |
| Stall length | always the full 30 s, never a late answer | X-8, 2026-10-31 |
| Episode | five stalls 10:24 to 10:28, ~45 to 80 s apart | X-8, 2026-10-31 |
| Umbra partner link | 11.25 ms interval, we are the SLAVE | X-8 |
| Micra / Bookoo links | 50 ms interval, we are master | X-8 |
| Umbra off, Micra + Bookoo up | 0 stalls in 14 min (vs ~1/min); back within 2 min of the Umbra returning | X-8, 2026-10-31 |
| Micra + two Bookoos (all master) | days without a stall | bench |

The 2026-10-31 rows are from the device log as quoted in 0f00614 and the
working notes.

The one thing the Umbra adds is our controller being scheduled as a slave
every 11.25 ms; it REJECTS parameter updates (conn update status 16), both a
slower interval and slave latency. Dialling the Micra at 30 ms instead of
50 ms changed nothing (reverted).

<a id="ev-firings"></a>

| Observation | Value | Unit / date |
|---|---|---|
| Watchdog firings | 6 in 08:15 to 08:34; 5 masked re-dials in ~5 s; 1 discovery stall of 31 s | X-8 beta.2, 2026-11-04 |

From the working notes alone. Every firing logged at ~5,000 ms; a tighter
3 s threshold was floated on that, but nothing in the code, the CHANGELOG
or an issue plans it. The one discovery stall ran 31 s, past the 15 s mask,
so the link went Connected -> Disconnected -> Connected; `ReadyChime` clears
its once-per-turn-on latch on any state != Connected by design, so the
post-shot boiler sag re-armed the ready chime an hour into the session.
Commands (power, temperatures) sitting in their mailboxes through a masked
re-dial landed afterwards on the bench, none dropped.

### Alternatives and dead ends

- Waiting out the 30 s: buys nothing when every stall is a loss.
- Negotiating the Umbra's parameters: refused by the scale.
- Dialling the Micra at 30 ms instead of 50 ms: no change; reverted
  (5809fc1).
- Upstream: esp-hosted 2.12.13 (core 3.3.12) is the newest in any core;
  later lines are platform bumps; no drop counters exist in the prebuilt
  libs. We will not open a vendor issue.

### History

- 2026-06-28: connect-by-saved-address link, no startup scan, discovery in Settings (90a04da, d29f04e).
- 2026-07-21: Micra connect timeout 30 s -> 8 s per address type, scans preempt the peer link's connect (ff97bb7).
- 2026-08-24: configuration-mode machines detected and reported as "restart it" (87c5187).
- 2026-10-05: wedged-machine diagnosis rule.
- 2026-10-31: reasons, parameters, timed reads logged (f746a58, c45adf9, 2e5b502); 30 ms experiment reverted (5809fc1); watchdog `kRequestStallMs` 5 s / `kRecoverMaskMs` 15 s (0f00614, v1.3.0-beta.1).
- 2026-11-01: same shape on an inbound stream: `kFirstWeightMs` 5 s re-dial for a scale link that never delivers a weight; the power button's "Working..." hold 8 -> 12 s (5c9bb70).
- 2026-11-04: discovery-stall case observed past the mask.

### Sources

- Commits 90a04da, d29f04e, ff97bb7, 87c5187, f746a58, c45adf9, 5809fc1, 2e5b502, 0f00614, 5c9bb70; tag v1.3.0-beta.1.
- `include/core/micra_link.h`, `src/core/micra_link.cpp`, `src/device/main.cpp`.
