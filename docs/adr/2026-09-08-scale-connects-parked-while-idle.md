# Scale reconnects are parked while the screensaver or setup portal is up; the Micra's only for the portal

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-09-08 · **Last changed:** 2026-11-05
- **Boards:** all

## Context

A saved scale that is switched off keeps the link retrying for as long as
the unit is awake ([a connect failure every ~13 s, all night](#evidence)).
On the S3 an active BLE listen drops free internal heap to ~4 KB, enough to
fail the daily update CHECK's TLS handshake. During the setup portal every
multi-second connect attempt also squeezes the AP's beacons and the phone's
join ([a 90 s join](#evidence)).

## Decision

- Only NEW attempts are suppressed; an established link is kept
  ([why](#alternatives-and-dead-ends)), with one exception: a scale whose
  driver declares it sleeps only once its link drops (the Lunar / Pyxis
  generation, whose auto-off never runs while connected and whose keepalive
  is our status poll) is dropped under the same two sources, and never
  during a shot or its review, so it can sleep; the park keeps it down and
  the normal reconnect brings it back when the idle ends (a Lunar that has
  already slept needs its button first; it does not wake over Bluetooth)
  ([the lit Lunar](#ev-lunar)). Both scale slots park their reconnects while
  the screensaver is on and while the portal is up, and the park is logged.
- The Micra parks for the PORTAL ONLY, released the moment a token is
  submitted. The saver park is for scale discovery: a connected Micra costs
  no more RAM asleep than awake, and a link lost overnight must be able to
  come back before a scheduled morning turn-on ([the cold
  morning](#evidence), [history](#history)).
- Parking is a per-slot bitmask of sources (saver, portal, shot) on the
  radio arbiter, so one source clearing cannot un-park another's hold
  ([the shared pause level it replaced](#alternatives-and-dead-ends)).
- The partner-mode Umbra is put to sleep by the saver and portal sources
  and ONLY those; yielding the radio to another link never dozes it.

## Consequences

- A scheduled trigger that cannot fire logs why ("Micra not connected" /
  "clock not trusted") and `MISSED` if its window closes.
- The bench ROLE's radio slot also parks for the shot source; the espresso
  role never does (see the two-slot record).
- The S3's daily update check runs in the parked window, where the radio's
  listening memory is free ([measured](#evidence)).

---

## Details

### Evidence

| Observation | Unit / date |
|---|---|
| `ScaleLink: connect failed` every ~13 s all night on an idle machine | 2026-09-08 |
| 13 s "connect failed" churn stops the moment the saver comes on, resumes on wake | 4.3C log, 2026-09-12 |
| A phone took ~90 s to join the setup portal (join/DHCP handshake) while a scale link was retrying | 4.3C, 2026-09-12 |
| An active BLE listen leaves ~4 KB internal heap free; the daily check's TLS handshake failed for lack of an AES DMA descriptor | S3 |
| Parking frees the ~40 KB the radio's listening holds; the daily check gated to that window became reliable | S3, v0.11.2 |
| Cold machine on a scheduled morning with the Micra link lost overnight; the ring was lost before it could be read | bench, 2026-09-30 (cause unproven; the saver park is the one mechanism that fits) |

<a id="ev-lunar"></a>
- 2026-11-05, bench P4-5 on v1.3.0, Idle screen 1 min: a Lunar (fw 1.0.19) connected at boot stayed connected, display lit, through more than 30 minutes under the screensaver; the log showed no link change (issue #10). Stopping the driver's status poll makes the Lunar drop the link itself, so an explicit drop was chosen over going quiet.

### Alternatives and dead ends

- Dropping established links under the saver: rejected (shortens a partner
  Umbra's charge interval, and a connected link is free).
- A single shared pause level for the pairwise scan pause and the saver
  park (the pre-arbiter shape): a Micra Settings scan ending un-parked a
  saver-parked scale and put a connected partner Umbra to sleep.

### History

- 2026-09-08: both links' reconnects (the one scale link and the Micra) parked under the saver and the portal, daily check gated to it (12f0c09, v0.11.2).
- 2026-09-30: Micra reconnects un-parked from the saver, portal only, released on token submit; scheduled triggers log why they wait and `MISSED` (1bcca5e).
- 2026-10-24: both scale slots park (0134038, v1.2.0); park sources become a bitmask on the arbiter; partner sleep tied to saver/portal only (67e6f3e).
- 2026-11-05: a Lunar / Pyxis is dropped while idle so it can sleep (`IScaleDriver::sleeps_on_disconnect`, `ScaleLink::set_idle`); the saver park logs a line (issue #10, v1.4.0).

### Sources

- Commits 12f0c09, 1bcca5e, 0134038, 67e6f3e; tags v0.11.2, v1.0.0, v1.2.0.
- `src/device/main.cpp` (the parking block), `include/core/radio_arbiter.h`.
