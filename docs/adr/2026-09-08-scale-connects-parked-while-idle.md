# Scale reconnects are parked while the screensaver or setup portal is up; the Micra's only for the portal

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-09-08 · **Last changed:** 2026-10-24
- **Boards:** all

## Context

A saved scale that is switched off keeps the link retrying: on 2026-09-08 a
`ScaleLink: connect failed` every ~13 s all night on an idle machine. On the
S3 the same retries mattered for memory: an active BLE listen drops free
internal heap to ~4 KB, which is why the daily update CHECK's TLS handshake
failed for lack of an AES DMA descriptor. During the setup portal every
multi-second connect attempt also squeezed the AP's beacons and the phone's
join/DHCP handshake (a phone took ~90 s to join on the 4.3C, 2026-09-12).

## Decision

- Only NEW attempts are suppressed; an established link is never dropped
  (a standing rule). Scale reconnects park while the screensaver is on and
  while the portal is up (12f0c09, v0.11.2, when there was one scale link
  and the Micra parked under the saver too); since v1.2.0 both scale slots
  park (0134038).
- The Micra parks for the PORTAL ONLY, and is released the moment a token
  is submitted (1bcca5e, 2026-09-30). The saver park was only ever meant for
  scale discovery: a connected Micra costs no more RAM asleep than awake, and
  a link lost overnight must be able to come back before a scheduled morning
  turn-on.
- Parking is a per-slot bitmask of sources (saver, portal, shot) on the
  radio arbiter since 2026-10-24, so one source clearing cannot un-park
  another's hold.
- The partner-mode Umbra is put to sleep by the saver and portal sources
  and ONLY those; yielding the radio to another link never dozes it.

## Evidence

| Observation | Unit / date |
|---|---|
| 13 s "connect failed" churn stops the moment the saver comes on, resumes on wake | 4.3C log, 2026-09-12 |
| Parking frees the ~40 KB the radio's listening holds; the daily check gated to that window became reliable | S3, v0.11.2 |
| Cold machine on a scheduled morning with the Micra link lost overnight; the ring was lost before it could be read | bench, 2026-09-30 (cause unproven; the saver park is the one mechanism that fits) |

## Alternatives and dead ends

- Dropping established links under the saver: rejected (shortens a partner
  Umbra's charge interval, and a connected link is free).
- A single shared pause level for the pairwise scan pause and the saver
  park (the pre-arbiter shape): a Micra Settings scan ending un-parked a
  saver-parked scale and put a connected partner Umbra to sleep.

## Consequences

- A scheduled trigger that cannot fire logs why ("Micra not connected" /
  "clock not trusted") and `MISSED` if its window closes (1bcca5e).
- The bench ROLE's radio slot also parks for the shot source; the espresso
  role never does (see the two-slot record).

## History

- 2026-09-08: both links' reconnects parked under the saver, daily check gated to it (12f0c09, v0.11.2).
- 2026-09-30: Micra reconnects un-parked from the saver, portal only (1bcca5e).
- 2026-10-24: park sources become a bitmask on the arbiter; partner sleep tied to saver/portal only (67e6f3e).

## Sources

- Commits 12f0c09, 1bcca5e, 0134038, 67e6f3e; tags v0.11.2, v1.0.0, v1.2.0.
- `src/device/main.cpp` (the parking block), `include/core/radio_arbiter.h`.
