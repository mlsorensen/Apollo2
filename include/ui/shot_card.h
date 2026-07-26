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

// Build the card's widgets into `parent` (fills it; parent should be a plain
// container). `rec` must OUTLIVE the card — the graph paints from it on every
// redraw. Returns the card root. When out_delete_btn is non-null, a trash
// button is added to the header (its events do NOT bubble, so tapping it
// won't close a tap-anywhere modal) and returned for the caller to wire.
lv_obj_t* build_shot_card(lv_obj_t* parent, const core::ShotRecord& rec,
                          const ScreenProfile& screen, bool use_24h,
                          lv_obj_t** out_delete_btn = nullptr);


}  // namespace ui
