# Two scales on Home and in Settings: card swap or beside the graph, scan then choose, Swap roles

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-10-24 · **Last changed:** 2026-11-01
- **Boards:** every board with the large Home layout (all released boards); the internal 2-inch compact layout shows nothing of it

## Context

A second BLE scale, the one on the counter whose own screen a jug or plate
hides, is readable on Apollo. Display only: the espresso scale keeps every
shot role (auto-stop, the tare at shot start, shot detect, the auto-flush
cup-off, the graph); the bench scale is a weight readout with its own Tare.
The gate is UI quality at 800x480. The radio side is
`2026-10-24-two-scale-slots-roles-over-links.md`; this one is the screens.

## Decision

- **Two Home treatments, the user picks** (Settings > Scale > Display >
  "Bench scale on Home", shown once a bench scale is saved):
  1. *In the scale card* (default): the SCALE caption becomes an
     ESPRESSO | BENCH switch and the two cards share the panel slot; a shot
     starting snaps back to ESPRESSO. The header is dot-only
     ([why](#alternatives-and-dead-ends)).
  2. *Beside the graph*: the lower row splits 2:1 and the bench card sits
     right of the graph with Tare only. Both scales share the plot, the
     bench weight in grams against its own right axis. Offered on EVERY
     board ([why not X-8 only](#alternatives-and-dead-ends)).
  A bench scale alone takes the SCALE slot by itself
  ([what lost](#alternatives-and-dead-ends)).
- **Bench colour is amber**, a theme role of its own: card, trace, axis and
  the dose chip.
- **The unit chip is a legend**: a dot plus the unit per series; the tap
  toggles only the espresso unit (`2026-11-04-graph-legend-sizing.md`).
- **Every save goes Scan, tap a result, "Use <name> as which scale?"**
  (Espresso / Bench) and lands in that slot, replacing what was there. No
  auto-slotting, ever: a one-scale user taps Espresso and never sees a
  bench row.
- **Swap roles** sits on the Bluetooth page beside Scan once both are
  saved. Both scales stay connected through it ([verified](#evidence)); the
  graph restarts because its history belonged to the other role. Refused
  mid-shot.
- **Settings > Scale > Display** takes Battery display off Device settings
  (which keeps only the scale's own rows, with an Espresso/Bench picker
  when both are saved) and the graph-look rows off Shot settings
  ([why not Apollo > Display](#alternatives-and-dead-ends)).

## Consequences

- One Umbra partner at a time in either slot; a second is saved as a plain
  link with a toast (one GATT service).
- The bench card is a panel column sharing the MICRA card's lines
  (`2026-11-01-home-cards-share-rows.md`; [when](#history)).
- The dose feature (`2026-10-30-dose-and-ratio-on-home.md`) hangs off the
  bench card and the legend without a new row.
- Screenshots: `../img/manual/home-bench-card.png`,
  `../img/manual/home-bench-beside.png`, `../img/manual/scale-bluetooth.png`,
  `../img/manual/scale-save-modal.png`, `../img/manual/scale-display.png`.

---

## Details

### Evidence

- The gate as stated when the work started: "if it can't look good I don't
  want to do it." Mockups were real sim renders in a scratch worktree,
  nothing committed; the design was settled on them on 2026-10-24.
- Three UI rounds on the S3 4.3C the same day (`b9a9fb6`, `42e8d00`,
  `e6658b8`): Swap beside Scan, model display names ("Bookoo Ultra" instead
  of the advertised code), uniform 28/32 px card values, a bench axis floor
  of 30 g, "Connecting..." with the Home buttons disabled while a connect
  is in flight, steppers 8 dp lower.
- Swap roles verified on the P4-5 with a partner Umbra as espresso and a
  Lunar as bench: zero reconnects, a paired Umbra keeps its session
  (2026-10-24, v1.2.0-beta.2).
- Shipped as v1.2.0 on 2026-10-30 after the beta.1 to beta.4 line.

### Alternatives and dead ends

- A bench strip inside the SCALE card: clipped the WEIGHT/TIMER captions,
  the card height is fixed by the 7:5 split. Dropped.
- A graph-corner chip for the bench weight: liked, not chosen.
- Auto-slotting the second scale found: rejected in favour of the modal.
- Bench placement as an X-8-only feature: rejected after the 800x480 render
  was judged; every board gets both.
- "Connected" text in the swap card's header: no width for it at 800x480,
  so the header is dot-only.
- The new rows under Apollo > Display: rejected, its seven rows already fill
  800x480; hence a Settings > Scale > Display page.

### History

- 2026-10-24: design settled on mockups; UI built (`06ffac4`), three
  bench rounds; v1.2.0-beta.1 and beta.2 (Swap roles as a pointer swap).
- 2026-10-30: v1.2.0 released from main.
- 2026-11-01: bench card re-laid as a panel column on the shared lines
  (`f1d0ee1`).

### Sources

Commits `06ffac4`, `b9a9fb6`, `42e8d00`, `e6658b8`, `b860b29`, `7cf62a6`,
`f1d0ee1`; CLAUDE.md "Two scales + the radio arbiter"; MANUAL.md "BENCH
card", "Bluetooth (scale)", "Display" under Settings > Scale.
