# Umbra partner mode: Apollo acts as the scale's paired device so it sleeps and wakes

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-10-16 · **Last changed:** 2026-11-05
- **Boards:** all (a P4 runs the peripheral role through the C6)

## Context

From Umbra firmware 1.0.8, an UNPAIRED Umbra POWERS OFF five minutes after
the link drops whatever its auto-off timer says, "Off" included, and a
powered-off scale cannot be woken over the air ([measured](#ev-poweroff)).
PAIRED to a partner device it has no 5 min power-off: it sleeps on its
timer from losing the partner and wakes when the partner reappears. The
Umbra is the CENTRAL in that relationship and treats any central as a
possible partner, so Apollo can be the partner from its own address
([proven on an S3](#ev-pairing)).

## Decision

- Partner mode (`platform::UmbraPartner`) is THE way Apollo talks to an
  Umbra; the plain central path stays as "Use unpaired".
- Apollo advertises as a peripheral named `core::kPartnerAdvertName`
  ("LUNAR-APOLLO") with manufacturer data and the Acaia GATT; both the name
  prefix and the manufacturer data are required ([advert
  contents](#dd-advert), [either alone is ignored](#ev-advert)).
- The session is info -> "Pair?" or identify -> confirm (no key) -> event
  subscription -> weight at ~20 Hz, with a keepalive from our side ([the
  frames](#dd-frames)). The relay stream is always grams.
- Sleep = the sleep frame, then close the link, then stop advertising; the
  Umbra then sleeps on its id-6 timer ([why all three](#dd-sleep)). Wake =
  start advertising; the sleeping Umbra reconnects by itself within seconds.
- Settings (beep id 7, sleep id 6) go as NOTIFICATIONS from the partner side
  and echo in the status frame within 0.1 to 0.9 s.
- Sleep and wake follow the saver and the setup portal only: never yielding
  the radio to another link, and not Micra standby.
- The partner GATT service is registered once in `setup()`, only when
  partner mode is saved; choosing an Umbra on an unregistered unit asks for
  one restart (`sprpend`, per-unit, never backed up) ([why not
  live](#alternatives-and-dead-ends)).

## Consequences

- One partner Umbra at a time (one GATT service, one inbound session).
- All partner frames are sent from `poll()`; callbacks only flag what is
  owed.
- Another Apollo's scan must never classify "LUNAR-APOLLO" as an Acaia.
- The service costs ~1.25 KB of internal DMA-capable RAM ([measured on the
  P4 4.3 and S3 4.3C](#ev-ram)).
- The Umbra REJECTS connection-parameter updates; see the Micra link
  record. Its gauge freezes after sleep; see the battery estimate record.
- Partner mode is what lets an Umbra sleep on the saver; the idle drop
  added for the Lunar (the parked-while-idle record) deliberately excludes
  an unpaired Umbra, which would power off after a drop and need its
  button.

---

## Details

### Design detail

<a id="dd-advert"></a>
Advert: name `core::kPartnerAdvertName` ("LUNAR-APOLLO"), manufacturer data
company 0x4242 payload "01", the `49535343-...` GATT service.

<a id="dd-sleep"></a>
Sleep is `EF DD 18 08 08 00`. The frame alone is ignored; a paired Umbra
with the partner gone sleeps on its id-6 timer (any non-Off value; on Off it
only blue-blinks slowly, as it does whenever the partner is absent). No MAC
spoofing is needed for any of this: the Umbra accepts Apollo's own address.

<a id="dd-frames"></a>
Frames are `EF DD type payload ck_even ck_odd`.

| Step | Frame |
|---|---|
| Apollo -> Umbra on subscribe, every 1.2 s until done | info `EF DD 07 07 02 19 01 00 13 02 22 16` |
| Umbra -> Apollo, first pairing / already paired | `EF DD 1A` "Pair?" / identify `EF DD 0B` + 16 x 00 |
| Apollo -> Umbra | confirm `EF DD 1B 00 00 00` (auto-confirmed, no key) |
| Umbra -> Apollo, then Apollo -> Umbra | event subscription `0C 09 ...`, one event frame `0C 0C 05 02 ...` |
| Umbra -> Apollo thereafter | weight `0x1C` at ~20 Hz (u32 LE, decimals, flags), status `0x08` |
| Apollo -> Umbra while the session is active | keepalive `0x33` (a partner's own weight) about every 60 ms |

### Evidence

<a id="ev-poweroff"></a>
Firmware 1.0.8 was released 2026-10-16; before it the bench Umbra had
dozed between shots for months (still advertising, a connect wakes it).
Four bench runs that day (timer written as 7, 0, 0 and 2) all went silent
at 4:32 to 5:13.

| Observation | Date |
|---|---|
| Unpaired, fw 1.0.8: power-off at ~5:00 in 4 of 4 runs, timer irrelevant | 2026-10-16 |
| Paired, partner off from power-on: still advertising at 11+ min | 2026-10-16 |

<a id="ev-pairing"></a>
The Umbra flashed white when Apollo connected, treating it as a possible
partner.

| Observation | Date |
|---|---|
| An S3 on its own address paired the Umbra, ran the 0x1C session, slept it on a 1 min timer, woke it by advertising | 2026-10-16 |
| End to end on the S3 alone: weight decode confirmed (31.0 / 30.9 / 3.7 g), settings echoed, sleep and wake without a reboot | 2026-10-17 |

<a id="ev-advert"></a>
Advert filter tests, 2026-10-17: the name prefix alone or the manufacturer
data alone was ignored for 70 to 80 s; both together paired in 2 s.

<a id="ev-ram"></a>

| Observation | Date |
|---|---|
| Partner service registration: 1,248 to 1,260 B internal DMA-capable (P4 4.3 and S3 4.3C); S3 4.3C steady-state DMA free 26.0 vs 27.3 KB, 4+4 boots (f48897c) | 2026-10-17 |

### Alternatives and dead ends

- Any id-6 value to keep an unpaired 1.0.8 Umbra awake: none sleeps; the
  power-off is governed by neither id 6 nor id 1.
- Keeping the link up through the saver: shortens the charge interval.
- Registering the partner GATT service live: NimBLE panic-asserts on GATT
  changes while any connection, scan or advertising is active, which
  boot-looped a 4.3 once partner mode was saved. Hence register once in
  `setup()` and the one-restart prompt (f48897c).

### History

- 2026-10-16: 1.0.8 power-off characterised; the Umbra paired by hand; an S3 emulator pairs it with no real partner present.
- 2026-10-17: end to end on the S3; advert filter tests; partner mode (df8e5a6), reboot-to-pair (f48897c), v1.1.0-beta.1.
- 2026-10-18: partner advert excluded from scale scans (4dcb776).
- 2026-10-24: partner pause split from the radio yield (67e6f3e).
- 2026-11-05: the saver-driven idle drop for the Lunar/Pyxis does not apply to the Umbra in either mode; partner mode stays the Umbra's sleep path.

### Sources

- Commits df8e5a6, f48897c, 4dcb776, 67e6f3e; tags v1.1.0-beta.1, v1.1.0.
- `src/platform_esp32/umbra_partner.cpp`, `include/platform_esp32/umbra_partner.h`, `include/core/scale_driver.h`.
