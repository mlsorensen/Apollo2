# The graph legend's box: shaped to its digits, unit pinned, no repaint throttle

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-11-04 · **Last changed:** 2026-11-04
- **Boards:** all with the large Home layout; the complaint was raised on the S3 4.3C

## Context

The flow graph's legend carries LIVE values: the espresso scale's flow rate
or weight and the bench scale's grams, repainted on every scale sample
during a shot and on every scroll step at idle. A label sized to its text
relayouts over the canvas at the scale's sample rate, and its box grows and
shrinks with the digits as the reading crosses 9.9 to 10.0 g/s or 99.9 to
100.0 g ([seen on the 4.3C](#evidence)). This record covers the
presentation only; the graph's render cost is in
`2026-06-30-flow-graph-rendering-performance.md`.

## Decision

The value labels are sized to the SHAPE of their text: `set_text_shaped()`
measures the text with every digit as the font's widest, so every reading
with the same digit count gets the same box, and the box steps only when the
digit count changes. Each mode's usual widest reading is a floor ("88.8 g/s"
for flow, "888.8 g" for weights), and the labels are right-aligned, so the
number grows into the room on its left and the unit never moves
([why right-aligned](#alternatives-and-dead-ends)); only a reading past the
floor steps the box out. The legend repaints per sample, with no throttle
([why no throttle](#evidence)).

## Consequences

- `set_text_shaped(label, text, min_shape)` in `src/ui/home_tab.cpp` is the
  helper for any label whose live value must not jitter its neighbours.
- The per-sample repaint is the cost of a live legend and is accepted.
- The manual's flow-graph callout follows the box; the screenshot
  `../img/manual/home-flow-graph.png` shows the final one.

---

## Details

### Evidence

Three iterations in one day (2026-11-04), each tried on the 4.3C:

| commit | box | repaint | verdict |
|---|---|---|---|
| `a82f7c7` | fixed to the widest possible reading ("888.8 g/s", "8888.8 g") | at most every 250 ms (`kLegendRepaintMs`), 4 B .bss | the fixed box is awkward, too much empty space; the throttle "just makes it feel low FPS" |
| `c6fca82` | shaped: same width per digit count, steps with the count | every sample | growth in steps is right, but the unit still shifts when the count changes |
| `62218cc` | shaped, with a per-mode minimum, text right-aligned | every sample | kept |

The 250 ms throttle bought nothing measurable on the 4.3C; the per-sample
repaint is the cost of a live legend and is accepted as such.

### Alternatives and dead ends

- A box fixed to the widest-of-all reading: rejected as awkward.
- A repaint throttle: rejected, it read as a low frame rate.
- Left-aligned shaped text: the unit moved with every step; right alignment
  pins it.

### History

- 2026-10-24: unit chip becomes a legend (dot + unit per series,
  `06ffac4`).
- 2026-10-31: two entries stack beside the graph; rows flush left
  (`bcc5eb1`).
- 2026-11-01: live values in the legend (`3df12b0`); fed during shots too by
  2026-11-02 (`71e90bd`).
- 2026-11-04: the three sizing iterations above; the manual's flow-graph
  callout refit three times the same day; `62218cc` shipped in
  v1.3.0-beta.3.

### Sources

Commits `06ffac4`, `bcc5eb1`, `5809fc1`, `3df12b0`, `71e90bd`, `a82f7c7`,
`c6fca82`, `62218cc`; `src/ui/home_tab.cpp`.
