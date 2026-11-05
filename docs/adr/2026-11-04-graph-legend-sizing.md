# The graph legend's box: shaped to its digits, unit pinned, no repaint throttle

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-11-04 · **Last changed:** 2026-11-04
- **Boards:** all with the large Home layout; the complaint was raised on the S3 4.3C

## Context

The flow graph's unit chip became a legend on 2026-10-24 (a coloured dot and
the unit per series, `06ffac4`), stacked one entry per row beside the graph
on 2026-10-31 (`bcc5eb1`), and on 2026-11-01 to 02 gained LIVE values: the
espresso scale's flow rate or weight and the bench scale's grams, repainted
on every scale sample during a shot and on every scroll step at idle
(`3df12b0`, `71e90bd`). On the bench 4.3C that showed two problems: a
label relayout over the canvas at the scale's sample rate, and a box that
grew and shrank with the digits as the reading crossed 9.9 to 10.0 g/s or
99.9 to 100.0 g. This record covers the presentation decision only; the
graph's render cost is in `2026-06-30-flow-graph-rendering-performance.md`.

## Decision

The value labels are sized to the SHAPE of their text: `set_text_shaped()`
measures the text with every digit as the font's widest, so every reading
with the same digit count gets the same box, and the box steps only when the
digit count changes. Each mode's usual widest reading is a floor ("88.8 g/s"
for flow, "888.8 g" for weights), and the labels are right-aligned, so the
number grows into the room on its left and the unit never moves; only a
reading past the floor steps the box out. The legend repaints per sample,
with no throttle.

## Evidence

Three iterations in one day, each tried on the 4.3C:

| commit | box | repaint | verdict |
|---|---|---|---|
| `a82f7c7` | fixed to the widest possible reading ("888.8 g/s", "8888.8 g") | at most every 250 ms (`kLegendRepaintMs`), 4 B .bss | the fixed box is awkward, too much empty space; the throttle "just makes it feel low FPS" |
| `c6fca82` | shaped: same width per digit count, steps with the count | every sample | growth in steps is right, but the unit still shifts when the count changes |
| `62218cc` | shaped, with a per-mode minimum, text right-aligned | every sample | kept |

The 250 ms throttle bought nothing measurable on the 4.3C; the per-sample
repaint is the cost of a live legend and is accepted as such.

## Alternatives and dead ends

- A box fixed to the widest-of-all reading: rejected as awkward.
- A repaint throttle: rejected, it read as a low frame rate.
- Left-aligned shaped text: the unit moved with every step; right alignment
  pins it.

## Consequences

- `set_text_shaped(label, text, min_shape)` in `src/ui/home_tab.cpp` is the
  helper for any label whose live value must not jitter its neighbours.
- The manual's flow-graph callout was refit three times the same day; the
  screenshot `../img/manual/home-flow-graph.png` shows the final box.

## History

- 2026-10-24: unit chip becomes a legend (dot + unit per series).
- 2026-10-31: two entries stack beside the graph; rows flush left.
- 2026-11-01/02 — live values in the legend, fed during shots too.
- 2026-11-04: the three sizing iterations above; `62218cc` shipped in
  v1.3.0-beta.3.

## Sources

Commits `06ffac4`, `bcc5eb1`, `5809fc1`, `3df12b0`, `71e90bd`, `a82f7c7`,
`c6fca82`, `62218cc`; `src/ui/home_tab.cpp`.
