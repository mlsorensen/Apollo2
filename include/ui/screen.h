#pragma once

// Describes the screen the UI is being built for, so a build can pick a layout
// without the UI hardcoding a resolution. We use a few DISCRETE size tiers (not
// continuous reflow):
//   compact — small panels like the 2-inch 320x240 (bottom tab bar)
//   wide    — landscape panels like the 4.3" 800x480 (side rail)
//   xl      — large landscape like the 7" 1024x600 (side rail, scaled up)

namespace ui {

struct ScreenProfile {
  int width;
  int height;
};

// Compact layout: bottom tab bar, tighter spacing, smaller fonts.
inline bool is_compact(const ScreenProfile& screen) {
  return screen.width < 480 || screen.height < 360;
}

// XL layout: big landscape panels (7" 1024x600) — same structure as wide but
// scaled up (larger fonts, padding, controls).
inline bool is_xl(const ScreenProfile& screen) { return screen.width >= 960; }

}  // namespace ui
