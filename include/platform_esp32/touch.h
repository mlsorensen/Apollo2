#pragma once

// CST816 capacitive touch -> LVGL pointer input device. Polled over I2C in the
// LVGL read callback (this board wires no touch INT line). Call begin() once
// after the display is up (it needs LVGL already initialized).
//
// Sibling of the display backend; like everything in platform_esp32 it reads
// its pins from board_config.h and the UI never sees it.

namespace platform {

class Touch {
 public:
  // Initialize I2C + CST816 and register the LVGL input device. screen_w/h are
  // the on-screen (rotated) dimensions, used for the coordinate mapping.
  // Returns false if the controller doesn't ACK on the bus.
  bool begin(int screen_w, int screen_h);
};

}  // namespace platform
