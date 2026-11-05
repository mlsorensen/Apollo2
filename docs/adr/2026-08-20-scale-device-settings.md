# On-scale settings are descriptor-driven and owned by the scale

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-08-20 · **Last changed:** 2026-11-04
- **Boards:** all

## Context

Scales store their own beep and auto-off/sleep preferences. Exposing them
from Apollo means per-model differences (ids, value lists, readback
channels) that must not leak into UI code, and values Apollo must not
persist because the scale is the authority.

## Decision

- `core::ScaleSettingDesc` (label + option labels + count) published by each
  driver; `IScale` exposes count / descriptor / current value / set. The UI
  never knows models.
- The scale owns the values: no NVS, ever. A tap posts the absolute write and
  bumps the label optimistically; the next readback reconciles, with a 4 s
  grace window for [scales that echo the old value first](#ev-echo).
- Writes are paced for the Lunar, which drops bursts ([pacing](#dd-pacing)).
- Display-order index != wire value; drivers map.
- Both Bookoos cap the beep at Off/1 to 3 ([louder is not
  higher](#ev-beep)). The Lunar's beep is a volume whose write is a toggle,
  so the row cycles off/on and takes the readback as confirmation.
- Rows stay discoverable in every state: no scale saved shows a pairing
  hint; saved-but-offline shows the model's rows disabled ("--") behind an
  inline Connect; connected shows live values; rows lock during a shot like
  Tare; a scale with no settings says so in words ([history](#history)).
- Rows per scale ([wire detail](#dd-wire)): Bookoo Themis Mini / Ultra,
  Beep and Auto-off. Acaia Umbra, Beep and Auto sleep, [power-off values
  withheld](#alternatives-and-dead-ends). Acaia Lunar/Pyxis, Beep, Auto-off,
  Unit and a read-only Mode. Varia Aku, none.

## Consequences

- The Shot/Device split and the later Display page (v1.2.0) keep each page
  unscrolled; Device settings is only the scale's own rows.
- Acaia beep-off also silences its tare/timer chirps (documented in the
  manual).
- Resolution and capacity are [deliberately not
  exposed](#alternatives-and-dead-ends); the Aku's auto-off is left alone
  ([why no keepalive](#alternatives-and-dead-ends)).

---

## Details

### Design detail

<a id="dd-pacing"></a>
Taps debounce 400 ms, writes are spaced >= 1,200 ms and one per link pass,
unconfirmed writes resend up to twice. The Umbra sleep list is reordered
for display (index != wire value).

<a id="dd-wire"></a>

| Scale | Rows | Wire facts |
|---|---|---|
| Bookoo Themis Mini / Ultra | Beep Off/1/2/3; Auto-off 5/10/15/20/30 min | readback free in every 20-byte weight frame (standby at bytes 14-15, buzzer gear at 16); commands `03 0a 02 00 <gear>` / `03 0a 03 00 <min>` + checksum |
| Acaia Umbra | Beep On/Off; Auto sleep Off/1/5/10/30 min (wire 0/7/1/2/3) | status frame payload[2] sleep, [3] beep; the wire's power-off values 4/5/6 are deliberately not offered |
| Acaia Lunar/Pyxis | Beep Off/1/2/3 (a volume); Auto-off Off/5/10/20/30/60; Unit g/oz; Mode (read-only) | 2 s status poll; sound id 5, sleep id 1, unit id 0; no mode write exists |
| Varia Aku | none | weight, tare, battery only; the page says "No device settings available for this scale" (62218cc, 2026-11-04) |

### Evidence

<a id="ev-beep"></a>
The Bookoo wire allows 0 to 5, but 5 plays quieter than 4 on the Mini and
4 quieter than 1 on the Ultra, hence the Off/1 to 3 cap on both (7caffe5,
8a637a4). The Lunar's beep write is a toggle: any nonzero write re-enables
the beep at the STORED volume (3432f9a).

<a id="ev-echo"></a>
Scales echo the old value for a frame (Bookoo) up to a poll cycle (Lunar);
the 4 s grace window comes from that (601e9b9).

- Bookoo + Lunar HW-tested 2026-08-20 (toggle-beep semantics included); Aku
  weight/tare/battery on HW the same day, its battery levels characterised by
  a drain-to-death run (100/80/60/40/20/0, 0 reported once ~4 s before it
  dies, no 0x85 frames while charging; hence a per-model
  `battery_low_pct`, Aku 20, percent-true scales 15).
- Themis Ultra recognised by the BOOKOO prefix and the same protocol
  (2026-08-29); no separate variant, its extra ops declined.

### Alternatives and dead ends

- Exposing resolution / capacity: declined (Apollo is 0.1 g; hi-res halves
  capacity).
- Umbra power-off values: would turn "Sleeping, connect to wake" into a lie.
- Aku tare-every-60 s keepalive against its ~9 min auto-off: works but the
  scale beeps on every tare; not built.

### History

- 2026-08-20: descriptor surface, Bookoo/Umbra/Lunar rows, Varia Aku driver (8139dd7); Shot/Device split, Bookoo beep cap 4 (7caffe5); echo grace + Acaia Unit/Mode rows (601e9b9); Lunar beep as volume (3432f9a). Shipped v0.7.0.
- 2026-08-29: Themis Ultra, beep capped 0-3 on both Bookoos (8a637a4).
- 2026-10-17: Umbra Unit row removed (see the units record).
- 2026-11-04: Aku's empty page replaced by an explanatory line (62218cc).

### Sources

- Commits 8139dd7, 7caffe5, 601e9b9, 3432f9a, 8a637a4, 62218cc; tags v0.7.0, v0.9.0, v1.3.0-beta.3.
- `include/core/scale.h`, `src/core/scale_driver_bookoo.cpp`, `src/core/scale_driver_acaia.cpp`, `src/core/scale_link.cpp`.
