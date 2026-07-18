#include "platform_esp32/paddle.h"

#include <Arduino.h>

#include "platform_esp32/board_config.h"
#if defined(BOARD_PADDLE_VIA_IOEXT)
#include "platform_esp32/io_extension.h"
#endif

namespace platform {

#if defined(BOARD_PADDLE_VIA_IOEXT)

// 4.3C: DO0/DI0 on the IO extension. Both are active-low at the expander (low
// output = opto LED on = contact closed; low input = paddle-switch current
// flowing). The expander is begun by the display before the paddle is used.

void Paddle::begin() {
  // Re-assert the direction mask here too: the expander can ACK the display's
  // early begin() yet drop that first mode write (seen on hardware — inputs
  // stayed dead until the mask was rewritten). Belt: here; braces: periodically
  // in sensed() below.
  io_extension().apply_dir_mask();
  io_extension().set(static_cast<uint8_t>(board::kPaddleIoExtDrive), true);  // open
}

bool Paddle::available() const { return io_extension().ok(); }

bool Paddle::sensed() {
  // Periodic direction-mask re-assert (~every 1.6s at the 25ms poll): insurance
  // against the expander losing its input config (dropped boot write / reset).
  static uint8_t reassert = 0;
  if (++reassert == 0x40) {
    reassert = 0;
    io_extension().apply_dir_mask();
  }
  const uint8_t in = io_extension().read_input();
  return (in & (1u << board::kPaddleIoExtSense)) == 0;  // low = closed
}

void Paddle::drive(bool closed) {
  io_extension().set(static_cast<uint8_t>(board::kPaddleIoExtDrive), !closed);
}

#else

// Native-GPIO boards: drive an external relay, sense the switch to GND with the
// internal pull-up. Pins < 0 = not wired on this board.

void Paddle::begin() {
  if (board::kPaddleDrivePin >= 0) {
    pinMode(board::kPaddleDrivePin, OUTPUT);
    digitalWrite(board::kPaddleDrivePin, board::kPaddleActiveHigh ? LOW : HIGH);
  }
  if (board::kPaddleSensePin >= 0) {
    pinMode(board::kPaddleSensePin, INPUT_PULLUP);
  }
}

bool Paddle::available() const {
  return board::kPaddleDrivePin >= 0 && board::kPaddleSensePin >= 0;
}

bool Paddle::sensed() {
  if (board::kPaddleSensePin < 0) return false;
  return digitalRead(board::kPaddleSensePin) == LOW;  // switch closes to GND
}

void Paddle::drive(bool closed) {
  if (board::kPaddleDrivePin < 0) return;
  const bool level = board::kPaddleActiveHigh ? closed : !closed;
  digitalWrite(board::kPaddleDrivePin, level ? HIGH : LOW);
}

#endif  // BOARD_PADDLE_VIA_IOEXT

Paddle& paddle() {
  static Paddle instance;
  return instance;
}

}  // namespace platform
