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
  void begin();  // release the drive line; configure GPIO modes where used

  bool available() const override;
  bool sensed() override;
  void drive(bool closed) override;
};

Paddle& paddle();  // shared singleton (device main + display share none)

}  // namespace platform
