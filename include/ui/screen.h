#pragma once

// Describes the screen the UI is being built for, so a build can pick a layout
// without the UI hardcoding a resolution. We use a few DISCRETE size tiers (not
// continuous reflow):
//   compact — small panels like the 2-inch 320x240 (bottom tab bar)
//   wide    — landscape panels like the 4.3" 800x480 (side rail)
//   xl      — large landscape like the 7" 1024x600 (side rail, scaled up)
// plus a continuous `scale` on top of the tier: high-DPI panels (the 5" P4 at
// 1280x720) run an existing tier's LAYOUT with every pixel constant multiplied,
// so elements keep their physical size instead of shrinking with pixel density.

namespace ui {

struct ScreenProfile {
  int width;
  int height;
  // Proportional zoom applied to every layout constant (see ui::dp). The tier
  // checks below use the EFFECTIVE (pre-zoom) size, so 1280x720 @ 1.5 lays out
  // as the wide 800x480 tier, half again as large. 1.0 = classic behavior.
  float scale = 1.0f;
};

// Global copy of the active profile's scale, so deep widget helpers don't need
// the ScreenProfile threaded through them. App::build (and the sim's render
// loop) set it before any widget is created.
inline float g_ui_scale = 1.0f;
inline void set_scale(float s) { g_ui_scale = (s > 0.0f) ? s : 1.0f; }
inline float scale() { return g_ui_scale; }
// Scale a pixel constant ("density-independent px"). Identity at scale 1.0.
inline int dp(int px) {
  return static_cast<int>(px * g_ui_scale + (px >= 0 ? 0.5f : -0.5f));
}

// Compact layout: bottom tab bar, tighter spacing, smaller fonts.
inline bool is_compact(const ScreenProfile& screen) {
  return screen.width / screen.scale < 480 || screen.height / screen.scale < 360;
}

// XL layout: big landscape panels (7" 1024x600) — same structure as wide but
// scaled up (larger fonts, padding, controls).
inline bool is_xl(const ScreenProfile& screen) {
  return screen.width / screen.scale >= 960;
}

}  // namespace ui
