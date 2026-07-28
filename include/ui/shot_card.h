#pragma once

#include <lvgl.h>

#include "core/shot_store.h"
#include "ui/screen.h"

// The shot card: one finished shot rendered as hero metrics (result vs target,
// duration, average flow) over the weight+flow graph, datetime stamped in the
// corner. Used by the on-device History modal and the sim's render harness;
// the web app draws its own themed copy from the same data.

namespace ui {

// "6/15/2026 2:55 PM" (or 24h "6/15/2026 14:55") from a UTC epoch in the
// active timezone. include_year=false drops the year for tight list rows.
void format_shot_datetime(char* buf, size_t n, int64_t unix_time, bool use_24h,
                          bool include_year);

// How the shot was run, for the History row and card. The recorded mode is
// the whole story: kAuto only ever reaches the store from a wired rig, and
// kDetect only from the weight-stream detector (BrewSnapshot::paddle_wired,
// stored alongside as `wired`, is redundant with it — see core/brew.h). Only
// armed modes record at all, so kManual is what an old or hand-edited index
// carries, not something the firmware writes.
//   kAuto   LV_SYMBOL_CHARGE    "Auto"   / "Auto shot"
//   kDetect LV_SYMBOL_EYE_OPEN  "Detect" / "Detected"
//   kManual LV_SYMBOL_MINUS     "Manual" / "Manual"
const char* shot_mode_glyph(const core::ShotSummary& s);
const char* shot_mode_label(const core::ShotSummary& s);  // card wording

// The list-row tag: glyph plus the short word, or the glyph alone. Never
// empty — every row gets the same slot so the column stays aligned.
void format_shot_mode_tag(char* buf, size_t n, const core::ShotSummary& s,
                          bool glyph_only);

// Rows drop the word where it doesn't fit: compact panels are narrow, and XL
// spends its extra width on a bigger font rather than more columns.
inline bool shot_mode_tag_glyph_only(const ScreenProfile& screen) {
  return is_compact(screen) || is_xl(screen);
}

// Every tag format_shot_mode_tag can emit, null-terminated, so a list can
// size its mode column to the widest one — the fonts are proportional, so
// padding with spaces would not line the tags up.
const char* const* shot_mode_tags(bool glyph_only);

// The widest string format_shot_datetime can produce (12-hour, the longest
// month/day), for sizing the timestamp column the same way.
const char* widest_shot_datetime(bool include_year);

// Build the card's widgets into `parent` (fills it; parent should be a plain
// container). `rec` must OUTLIVE the card — the graph paints from it on every
// redraw. Returns the card root. When out_delete_btn is non-null, a trash
// button is added to the header (its events do NOT bubble, so tapping it
// won't close a tap-anywhere modal) and returned for the caller to wire.
lv_obj_t* build_shot_card(lv_obj_t* parent, const core::ShotRecord& rec,
                          const ScreenProfile& screen, bool use_24h,
                          lv_obj_t** out_delete_btn = nullptr);


}  // namespace ui
