# Micra link resilience: the stalled-request watchdog and the "machine wedged" rule

- **Status:** Open (the watchdog stands; a re-dial whose service discovery stalls still outruns the mask, and the cause of the stalls is a correlation, not a proven mechanism)
- **Area:** radio
- **First decided:** 2026-10-05 · **Last changed:** 2026-11-04
- **Boards:** all (evidence from the P4 X-8)

## Context

The Micra link was designed on 2026-06-28: no scan at startup, the saved
address dialled directly by a background task with auto-reconnect,
discovery in Settings, Home showing the link state. The protocol has no
notifications; every read is a request the host waits on, and the prebuilt
NimBLE host has a ~30 s GATT procedure timeout after which IT terminates the
connection. Two field problems followed.

**Wedged machine (2026-10-05).** A freshly flashed P4-5 with settings
restored from a card never connected; we suspected the restored
token. The Micra's own Bluetooth was wedged and a machine restart fixed it.

**Nuisance drops (2026-10-31).** Several Micra reconnects a day on the X-8
once an Umbra was paired in partner mode. Every drop was reason 0x216
"local host terminated": we ended the link, never a supervision timeout.
Timed reads named the path: a refresh read FAILED after 30,004 ms, once
service discovery of a fresh reconnect at 31 s.

## Decision

- Diagnosis rule: repeated `MicraLink: connect failed` with NO auth line is
  the machine's Bluetooth refusing connections (or another central holding
  its single slot). Ask for a Micra restart before suspecting the token or
  the radio. A bad token reads `token rejected -> NeedsToken`; an empty token
  never leaves NeedsToken. A scale failing to connect in the same log is not
  evidence of a radio fault.
- Watchdog (0f00614, 2026-10-31, v1.3.0-beta.1): `MicraLink` stamps each
  request in flight; `abort_stalled_request()` from main's loop drops the
  link after `kRequestStallMs` = 5 s so the blocked request returns and the
  loop re-dials at once. The link reads Connected on the outside until the
  re-dial resolves, for at most `kRecoverMaskMs` = 15 s; a failed re-dial
  publishes Disconnected. Commands (power, temperatures) sit in their
  mailboxes through it and land after the re-dial, never dropped (bench). The
  power button's "Working..." hold went 8 -> 12 s to cover it (5c9bb70).

## Evidence

| Observation | Value | Unit / date |
|---|---|---|
| Drop reason | 0x216, every time | X-8, 2026-10-31 |
| Stall length | always the full 30 s, never a late answer | X-8, 2026-10-31 |
| Episode | five stalls 10:24 to 10:28, ~45 to 80 s apart | X-8, 2026-10-31 |
| Umbra partner link | 11.25 ms interval, we are the SLAVE | X-8 |
| Micra / Bookoo links | 50 ms interval, we are master | X-8 |
| Umbra off, Micra + Bookoo up | 0 stalls in 14 min (vs ~1/min); back within 2 min of the Umbra returning | X-8, 2026-10-31 |
| Micra + two Bookoos (all master) | days without a stall | bench |
| Watchdog firings | 6 in 08:15 to 08:34; 5 masked re-dials in ~5 s; 1 discovery stall of 31 s | X-8 beta.2, 2026-11-04 |

The 2026-10-31 rows are from the device log as quoted in 0f00614 and the
working notes; the 2026-11-04 row is from the working notes alone.

The one thing the Umbra adds is our controller being scheduled as a slave
every 11.25 ms; it REJECTS parameter updates (conn update status 16), both a
slower interval and slave latency. Dialling the Micra at 30 ms instead of
50 ms changed nothing (reverted).

## Alternatives and dead ends

- Waiting out the 30 s: buys nothing when every stall is a loss.
- Negotiating the Umbra's parameters: refused by the scale.
- Upstream: esp-hosted 2.12.13 (core 3.3.12) is the newest in any core;
  later lines are platform bumps; no drop counters exist in the prebuilt
  libs. We will not open a vendor issue.

## Consequences

- ~10 s of stale state per stall instead of ~35 s, no flicker in the common
  case. A tighter 3 s threshold was floated in the working notes once every
  firing had logged at ~5,000 ms; nothing in the code, the CHANGELOG or an
  issue plans it, so it is an idea, not scheduled work.
- Open gap (2026-11-04): one re-dial connected at once but its service
  discovery stalled 31 s past the 15 s mask, so the link went Connected ->
  Disconnected -> Connected honestly; `ReadyChime` clears its once-per-turn-on
  latch on any link != Connected by design, so the post-shot boiler sag
  re-armed the chime an hour into the session. Options not taken: a
  discovery-phase watchdog, or a chime latch that survives a short drop.
- A machine left in configuration mode presents the seed characteristic and
  not the operative service; the link logs it and the UI says "restart it"
  (87c5187, 2026-08-24).

## History

- 2026-06-28: connect-by-saved-address link, no startup scan, discovery in Settings (90a04da, d29f04e).
- 2026-07-21: Micra connect timeout 30 s -> 8 s per address type, scans preempt the peer link's connect (ff97bb7).
- 2026-10-05: wedged-machine diagnosis rule.
- 2026-10-31: reasons, parameters, timed reads logged (f746a58, c45adf9, 2e5b502); 30 ms experiment reverted (5809fc1); watchdog (0f00614).
- 2026-11-01: same shape on an inbound stream: `kFirstWeightMs` 5 s re-dial for a scale link that never delivers a weight (5c9bb70).
- 2026-11-04: discovery-stall case observed past the mask.

## Sources

- Commits 90a04da, d29f04e, ff97bb7, 87c5187, f746a58, c45adf9, 5809fc1, 2e5b502, 0f00614, 5c9bb70; tag v1.3.0-beta.1.
- `include/core/micra_link.h`, `src/core/micra_link.cpp`, `src/device/main.cpp`.
