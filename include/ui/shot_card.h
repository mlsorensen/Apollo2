#pragma once

#include <lvgl.h>

#include "core/shot_store.h"
#include "ui/screen.h"

// The shot card: one finished shot rendered as hero metrics (result vs target,
// duration, average flow) over the weight+flow graph, datetime stamped in the
// corner. Used three ways with the same code: the on-device History modal, the
// PNG written to SD at shot end (drawn offscreen at a canonical size), and the
// sim's render harness.

namespace ui {

// "6/15/2026 2:55 PM" (or 24h "6/15/2026 14:55") from a UTC epoch in the
// active timezone. include_year=false drops the year for tight list rows.
void format_shot_datetime(char* buf, size_t n, int64_t unix_time, bool use_24h,
                          bool include_year);

// Build the card's widgets into `parent` (fills it; parent should be a plain
// container). `rec` must OUTLIVE the card — the graph paints from it on every
// redraw. Returns the card root.
lv_obj_t* build_shot_card(lv_obj_t* parent, const core::ShotRecord& rec,
                          const ScreenProfile& screen, bool use_24h);

#if LV_USE_SNAPSHOT
// Render the card offscreen at a canonical size (RGB565) for the PNG export —
// every board writes the same 800x480 card regardless of its panel. Forces UI
// scale 1.0 for the build, then restores it. Returns a draw buffer the caller
// must lv_draw_buf_destroy(); nullptr on allocation failure. Runs on the LVGL
// thread (one-time ~100ms during the review freeze).
lv_draw_buf_t* render_shot_card(const core::ShotRecord& rec, int w, int h,
                                bool use_24h);
#endif

}  // namespace ui
