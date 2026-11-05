# One radio listener at a time: the arbiter, the finder and learned address types

- **Status:** Open (arbiter and address-type learning accepted; why a continuous 5 s listen misses an advertising scale about half the time on the P4/C6 is unproven, WiFi coexistence being the untested suspect)
- **Area:** radio
- **First decided:** 2026-10-21 · **Last changed:** 2026-10-30
- **Boards:** all (miss measurements on the P4 X-8 only)

## Context

The NimBLE host runs ONE listen at a time (a connect-by-address or a scan),
refuses to scan while a connect is pending, and its connect-cancel is
GLOBAL (`ble_gap_conn_cancel` aborts whichever client is initiating). A MAC
alone does not say whether the peer advertises it as public or random, and
on the P4/C6 a 5 s listen catches an advertising scale only about half the
time ([measured](#evidence)), so every wasted window is a visible delay.

## Decision

- **Address type** is learned on the first successful connect and persisted
  (`satype`, ADD-only; absent = unknown = try both). A known type is one
  10 s listen; after 3 known-type failures one both-types try relearns, so a
  stored type can never strand a link. The Micra learns its type too
  (`matype`) ([constants](#constants), [why not a fixed
  order](#alternatives-and-dead-ends)).
- **Arbiter** (`core::RadioArbiter`): platform-free, polled by the link
  loops from their own tasks, no task of its own ([why no finder
  task](#alternatives-and-dead-ends)). A user scan preempts anything, and a
  preempted attempt is not a failure. The arbiter is the ONLY thing that
  cancels, only ever the current holder; core link code never cancels
  ([grants and releases](#dd-api)).
- **Finder.** One scale missing: the direct connect-by-address. Two missing:
  one passive address-matched scan chunk whose SIGHTING the slot's own task
  turns into a short direct connect; a connection is never handed across
  tasks. Empty chunks back off; a chunk counts as serving every missing
  slot; nothing scans while nothing is missing ([how](#dd-finder)).
- **Parking** is a per-slot BITMASK (`ParkSource` saver / portal / shot), so
  one source clearing never un-parks another's hold ([the shared pause it
  replaced](#alternatives-and-dead-ends)).

## Consequences

- The partner Umbra's sleep is driven by the saver and portal sources ONLY.
- The espresso ROLE is never shot-parked (see the two-slot record).
- Removing the dead time removed the complaint; the miss itself is not
  explained. Open experiment: WiFi off on an X-8, minute-mark trials;
  `WIFI_PS_MIN_MODEM` on the P4 only is the candidate if misses vanish
  ([why WiFi is the suspect](#dd-wifi), [not pursued yet](#history)).
- `make test-radio` holds the arbiter's 12 cases; keep it green.

---

## Details

### Design detail

<a id="dd-api"></a>
`poll()` grants kIdle / kDirectConnect / kFinderChunk / kUserScan (top
priority, preempts anything, the slot's own attempt included); `release()`
reports kConnected / kFailed / kPreempted (no fail count, no backoff) /
kScanDone. Cancels go through per-slot hold hooks.

<a id="dd-finder"></a>
The direct connect is a 100 % duty listen for that address, answered in the
same advertising event. The chunk is `ICentral::scan_for`: it stores
nothing and stops on the first hit.

<a id="constants"></a>

| Constant | Value |
|---|---|
| Unknown-type scale connect | 5 s per type, 3 s backoff |
| Known-type listen (`kKnownTypeTimeoutMs`) | 10 s, 1 s backoff |
| Known-type failures before relearn (`kKnownTypeFailLimit`) | 3 |
| Sighting connect (`kSightingConnectMs`) | 3 s |
| Finder chunk / sighting freshness | 10 s / 3 s |
| Empty-chunk gap (`kGapMinMs` -> x5 -> `kGapMaxMs`) | 1 -> 5 -> 20 s |
| Micra connect timeout | 8 s |

<a id="dd-wifi"></a>
`network.cpp` pins WiFi power save off (for the S3's DNS drops) and the C6
shares one radio between WiFi and BLE.

### Evidence

Bench 2026-10-21, X-8 on v1.1.0-beta.5, scored from the device log, Bookoo
on and advertising within ~1 s of power-on:

| Trial | Result |
|---|---|
| 64 min of sleep incl. the saver's cancel of the in-flight try | instant: cleared |
| 10 min of continuous initiating awake | 49 fails then 1 s: cleared |
| Second Bookoo (Ultra) advertising | 2 misses of 3 trials |
| Ultra off | 3 misses of 6 windows, one trial 35 s |

The miss counts are in f389809; the trial lengths (64 min, 49 fails, 35 s)
and the S3 boot timing below are per the working notes. A 5 s window caught
an advertising scale about half the time, and each miss cost a dead 8 s.
Before the fix every reconnect tried both types (5 s random, 5 s public,
3 s backoff); Acaia Pyxis-gen and Lunar advertise PUBLIC, Bookoo RANDOM, so
the Acaias had wasted their first 5 s too. The original complaint was the
Bookoo taking 30 to 60 s to reconnect on the X-8 after a screensaver wake.

Arbiter verification 2026-10-24: a finder chunk at boot on the S3 brought
both scales up 16 s after boot; `make test-radio` holds 12 cases.

### Alternatives and dead ends

- Random-first with a 5 s timeout (3294e1f, 2026-07-01) fixed a 30 s Bookoo
  reconnect and stayed wrong for the Acaias.
- The pre-arbiter fix (ded5d27, 2026-07-24): a hold level plus a cancel
  covering both attempts. It fixed scan starvation but kept the shared
  pause. Until v1.1 the Micra and scale links each reconnected on their own
  task with a pairwise pause for scans only; attempts collided by luck, and
  the one shared pause level meant a Micra Settings scan ending un-parked a
  saver-parked scale and, through the partner pause, put a connected
  partner-mode Umbra to sleep.
- A finder task of its own: connections cannot be handed across tasks, so
  the links poll.

### History

- 2026-07-01: random-first + 5 s connect timeout (3294e1f).
- 2026-07-24: scan preemption fix (ded5d27).
- 2026-10-21: ~50 % per-window miss measured; `satype` + 10 s listen (f389809, v1.1.0-beta.6). The WiFi-off experiment was not pursued in 2026-10.
- 2026-10-24: `core::RadioArbiter`, finder, bitmask parking, `matype` (67e6f3e); review fixes (park/Forget preempt a chunk, preempted chunk != empty).
- 2026-10-30: v1.2.0 released.

### Sources

- Commits 3294e1f, ded5d27, f389809, 67e6f3e; tags v1.1.0-beta.6, v1.2.0.
- `include/core/radio_arbiter.h`, `src/core/scale_link.cpp`, `src/core/micra_link.cpp`, `tools/radio_arbiter_test.cpp`.
