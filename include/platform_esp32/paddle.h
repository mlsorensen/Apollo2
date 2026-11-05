#pragma once

#include "core/paddle.h"

// Device paddle backend. Two hardware flavors, chosen by board_config.h:
//  - BOARD_PADDLE_VIA_IOEXT (4.3C): drive + sense ride the IO-extension chip's
//    isolated DO0/DI0 terminals (both active-low at the expander).
//  - native GPIO (kPaddleDrivePin/kPaddleSensePin >= 0): a pin driving an
//    external relay + a pin reading the paddle switch (INPUT_PULLUP, switch to
//    GND). kPaddleActiveHigh sets the drive polarity.
//    The P4 boards carry a SECOND native set for the Apollo Link bridge
//    (kLinkDrivePin/kLinkSensePin, active-high, plus kLinkCtrlPin held HIGH
//    while booted). By default (core::PaddleWiring::kAuto, NVS "padwire")
//    both sets are driven and polled together, so either cable works; the
//    unused set is just an open pin. set_wiring(kLink / kCable) pins the unit
//    to one set and leaves the other an open input.
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
  void own(bool owned) override;  // Link CTRL: HIGH = Apollo has the paddle
  // Paddle wiring (P4 boards with both native sets): which set is polled,
  // driven and powered. Set BEFORE begin() at boot (main.cpp reads NVS);
  // a later change re-runs begin() with the remembered overrides.
  bool wiring_selectable() const override;
  void set_wiring(core::PaddleWiring w) override;
  core::PaddleWiring wiring() const override { return wiring_; }

 private:
  int sense_pin_ = -1;  // resolved at begin() on native-GPIO boards
  int drive_pin_ = -1;
  int link_sense_pin_ = -1;  // Apollo Link set (P4): mirrors the two above
  int link_drive_pin_ = -1;
  int ctrl_pin_ = -1;        // Link CTRL: HIGH = paddle handed to Apollo
  core::PaddleWiring wiring_ = core::PaddleWiring::kAuto;
  int sense_override_ = -1;  // begin() arguments, kept for set_wiring's re-run
  int drive_override_ = -1;
  bool begun_ = false;       // set_wiring() before begin() only records the choice
  bool use_corner() const { return wiring_ != core::PaddleWiring::kLink; }
  bool use_link() const { return wiring_ != core::PaddleWiring::kCable; }
};

Paddle& paddle();  // shared singleton (device main + display share none)

}  // namespace platform
