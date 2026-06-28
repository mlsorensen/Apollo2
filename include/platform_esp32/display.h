#pragma once

// On-device display backend: brings up the panel for the selected board and
// registers it with LVGL as the active display. This is the device counterpart
// of the host PngDisplay — same role (get LVGL pixels onto a screen), different
// target. The UI layer is unaware of which backend is running.
//
// Touch is a separate concern (added later) so the display can be brought up
// and validated on its own.

namespace platform {

class Display {
 public:
  // Initialize the panel + backlight + LVGL display. Returns false if the
  // panel or draw buffer could not be allocated. Call once from setup().
  bool begin();

  int width() const;   // logical width after rotation (0 before begin())
  int height() const;  // logical height after rotation

  void set_brightness(int percent);  // backlight PWM, 0..100
};

}  // namespace platform
