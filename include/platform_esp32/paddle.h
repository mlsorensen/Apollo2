#pragma once

#include "core/paddle.h"

// Device paddle backend. Two hardware flavors, chosen by board_config.h:
//  - BOARD_PADDLE_VIA_IOEXT (4.3C): drive + sense ride the IO-extension chip's
//    isolated DO0/DI0 terminals (both active-low at the expander).
//  - native GPIO (kPaddleDrivePin/kPaddleSensePin >= 0): a pin driving an
//    external relay + a pin reading the paddle switch (INPUT_PULLUP, switch to
//    GND). kPaddleActiveHigh sets the drive polarity.
// Neither configured -> available() == false and the brew controller idles.

namespace platform {

class Paddle : public core::IPaddle {
 public:
  // Release the drive line; configure GPIO modes where used. On native-GPIO
  // boards, overrides >= 0 replace kPaddleSensePin / kPaddleDrivePin for THIS
  // unit (the NVS "padsense"/"paddrive" repair knobs — a board with a damaged
  // pad moves its wire without forking the board config). Ignored on IOEXT
  // boards.
  void begin(int sense_pin_override = -1, int drive_pin_override = -1);

  bool available() const override;
  bool sensed() override;
  void drive(bool closed) override;

 private:
  int sense_pin_ = -1;  // resolved at begin() on native-GPIO boards
  int drive_pin_ = -1;
};

Paddle& paddle();  // shared singleton (device main + display share none)

}  // namespace platform
