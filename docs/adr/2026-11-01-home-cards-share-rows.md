# Home panel cards share their rows, and a line never moves without asking

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-11-01 · **Last changed:** 2026-11-01
- **Boards:** every board with the large Home layout (all released boards); the internal 2-inch compact layout has no panel cards

## Context

Home's large layouts put up to three panel cards in one row: MICRA, SCALE
and BENCH (behind the ESPRESSO | BENCH switch or beside the graph). A few
pixels of misalignment between them reads as "about right" at Preview size
and wrong on the glass, and LVGL clips every child to its parent's
rectangle, so content that overruns a body loses its edge even with an
empty gap below it ([the day that showed it](#evidence)).

## Decision

Cards that share a row share their lines. The grammar, top to bottom:

| line | MICRA | SCALE | BENCH |
|---|---|---|---|
| header | name, status dot | name, status, battery | the switch or name, status, battery |
| caption | BREW / STEAM | WEIGHT / TIMER | the scale's name in caps |
| value | temperatures | weight, timer | weight |
| stepper | − target + | shot pill | − dose +, or an empty slot that keeps its height |
| action row | Standby, Flush | Disconnect, Tare | Disconnect, Tare |

Rules that follow:

- New content goes INTO an existing line, never between two.
- Decoration that must wrap text (the Record outline) is a layout-ignoring
  object fitted behind the labels, never a box in the flow
  ([why](#alternatives-and-dead-ends)).
- Swap mode levels the MICRA card to the switch card: the MICRA header takes
  the switch header's measured height and both bodies lose the same 16 dp
  of slack ([the 7 px it was off](#evidence)).
- The one exempt card is the bench card beside the graph: nothing sits
  beside it to align with, and it lives in the graph's height.
- Moving a line, adding a row or breaking rank is RAISED first and built
  only on an explicit OK, the same gate the memory-budget rule uses.

## Consequences

- The rule is written into CLAUDE.md ("Row alignment rule") and binds every
  Home card change.
- Verification is part of the rule: crop the card from the sim render at 2
  to 3x and read the detail log's geometry lines, which stay in the code as
  the measuring tool ([what they measure](#ev-measuring)). "It looks fine"
  is not the test.
- Screenshots: `../img/manual/home-micra.png`, `../img/manual/home-scale-card.png`,
  `../img/manual/home-bench-card.png`, `../img/manual/home-bench-beside.png`.

---

## Details

### Evidence

The v1.3.0 dose work added a dose stepper and a Record control to the bench
cards (BENCH itself dates from v1.2.0), and 2026-11-01 became a day of
nearly-aligned versions on the bench X-8 and in the sim: the ESPRESSO
side's rows sat 7 px below MICRA's, the scale's name sat under the weight
instead of on the caption line, the Record outline was a box in the flow
that pushed the stepper, and the X-8's stepper row and shot pill were being
clipped by the action row. Each looked "about right" at Preview size. The
version where every line sat at the same y on every card is the one called
finished. The sequence, all on `src/ui/home_tab.cpp`:

| commit | what it fixed |
|---|---|
| `f621a59` | geometry log for the swap card (header/body/action row), the X-8's clipped pill |
| `b8783c1` | slimmer pills; the switch sets the header height; X-8 stepper row and shot pill were clipped by the action row |
| `8ae0012` | pills back to full size; the log reports the real overflow |
| `d155c7d`, `81e5070`, `6b30f7a`, `5b80b7f`, `2e7cbb7` | the beside card's stepper and Record outline losing their bottoms or tops; 3 to 5 dp nudges |
| `f1d0ee1` | the bench card becomes a panel column; swap mode levels MICRA; sim `clipped` = -7..-11 on the swap cards (room to spare) |

<a id="ev-measuring"></a>
The measuring method: before saying a layout is right, crop the card from
the sim render at 2 to 3x (a full-size view hides a cut ring) and read the
detail log's geometry lines, `home: scale card ... overflow=N` for the swap
card and `home: bench card ... clipped=N` for the bench card. They measure
AFTER the App's show/hide pass on visible children's raw coordinates; a
build-time measurement of a hidden widget says nothing, and
`lv_obj_get_scroll_bottom` sees direct children only. LVGL clips every
child to its parent's rectangle, so content that overruns a body loses its
edge even with an empty gap below it.

### Alternatives and dead ends

- The Record outline as a flex child (2026-10-30, `4d1d5d6`): it pushed the
  stepper and clipped beside the graph; replaced by the fitted object.
- Sizing the body to the block: the actions row is laid out after the body,
  so an oversized body is drawn over by the buttons. The block is sized to
  the body, never the body to the block.
- "It looks fine" at Preview size: rejected as the test, by the day above.

### History

- 2026-11-01: rule stated after the swap-card iterations;
  `f1d0ee1` is the shape that stuck.

### Sources

Commits `f621a59`, `b8783c1`, `8ae0012`, `f1d0ee1` (2026-11-01); CLAUDE.md
"Row alignment rule"; `src/ui/home_tab.cpp`.
