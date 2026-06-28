#pragma once

// Describes the screen the UI is being built for, so a build can pick a layout
// without the UI hardcoding a resolution. We use a few DISCRETE layouts chosen
// by size (not continuous reflow): "compact" for small panels like the 2-inch
// 320x240, "wide" for large landscape panels like the 4.3" 800x480.

namespace ui {

struct ScreenProfile {
  int width;
  int height;
};

// Compact layout: bottom tab bar, tighter spacing, smaller fonts. The threshold
// is deliberately coarse — add named profiles here if a third form factor needs
// its own layout rather than widening this heuristic.
inline bool is_compact(const ScreenProfile& screen) {
  return screen.width < 480 || screen.height < 360;
}

}  // namespace ui
