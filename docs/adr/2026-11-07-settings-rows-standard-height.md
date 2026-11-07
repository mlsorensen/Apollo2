# Settings rows have one standard height per tier; controls are sized to fit it

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-11-07 · **Last changed:** 2026-11-07
- **Boards:** all (the wide, XL and compact tiers each carry their own values)

## Context

Settings pages are columns of label-plus-control rows. Rows used to size
themselves to their control, and the controls had their own sizes: a round
stepper button, a pill button or Capture made a 50 px row, a switch a
31 px one, with a 12 px gap. Pages that mix the two (Shot settings, Scale
Display, Apollo Display) looked unevenly spaced, and Shot settings' seven
rows overshot an 800x480 page by 8 px, leaving a sliver of scroll
([measured](#evidence)).

## Decision

- **The row height is the primitive.** `build_settings_tab` picks a
  standard row height and gap per tier and every setting row is exactly
  that tall; the controls (`btn_size`, switches, pills, Capture, the
  Reset beside Target) are sized FROM it, never the row from them (owner,
  2026-11-07: "set the control sizes according to a standard row size, not
  size the rows to arbitrary and varying control sizes").
- **Seven rows fill a page.** The wide-tier values are chosen so seven
  rows fit below the header at 800x480 and XL at 1024x600
  ([arithmetic](#evidence)); the compact tier keeps its four.
- **A fixed height, not a minimum.** A content-sized row with a minimum
  height centres its children in the content band at the top of the row,
  which is the unevenness in another form; rows are fixed-height, and only
  a row whose value is two lines (the steam boiler's level over its
  temperature, the WiFi address over its gateway line, the Storage card
  rows) re-sizes itself to content.
- The Schedule page keeps its own, wider gap (`2026-09-22-schedule-page-design.md`).

| tier | row | gap | before (row by control / gap) |
|---|---|---|---|
| wide (800x480 effective) | 46 px | 8 px | 50 or 31 / 12 |
| XL (1024x600) | 58 px | 10 px | 64 or 38 / 12 |
| compact (320x240) | 36 px | 8 px | unchanged |

## Consequences

- Shot settings, Scale Display and Device settings fit without scrolling
  at 800x480; Apollo Display (eight rows) and Controls (headings) still
  scroll.
- The round stepper buttons are 46 px on the wide tier (were 50), still a
  comfortable touch target; the X-8 at 1280x800 runs this tier at 1.6x.
- Every settings screenshot in the manual was re-cut and its callout boxes
  re-measured to the new pitch (`docs/img/manual/manifest.json`).

---

## Details

### Evidence

- 800x480: header 80 px plus 24 px page padding leaves 376 px. Seven rows
  of 50 with 12 px gaps need 422; Shot settings' five tall and two short
  rows needed 384, hence the 8 px of scroll. Seven rows of 46 with 8 px
  gaps need 370.
- 1024x600: 108 plus 24 leaves 468; seven rows of 58 with 10 px gaps need
  466.
- Row centres on Shot settings before: 117, 179, 241, 294, 346, 408, 470
  (pitch 62 / 53 / 52 / 62); after: 115, 169, 223, 277, 331, 385, 439.

### History

- 2026-11-07: decided and built while reviewing the Target row's new
  Learned drip readout (issue #7), where the page's scroll and uneven
  spacing were noticed.

### Sources

- `src/ui/settings_tab.cpp` (`make_setting_row`, `page_column`, the
  `row_h` / `row_gap` block in `build_settings_tab`).
