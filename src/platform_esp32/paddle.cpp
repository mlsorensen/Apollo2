#include "platform_esp32/paddle.h"

#include <Arduino.h>
#include "driver/gpio.h"
#include <soc/gpio_periph.h>  // GPIO_PIN_MUX_REG / FUN_WPU: pad pull-up read-back

#include "core/system.h"
#include "platform_esp32/board_config.h"
#if defined(BOARD_PADDLE_VIA_IOEXT)
#include "platform_esp32/io_extension.h"
#endif

namespace platform {

#if defined(BOARD_PADDLE_VIA_IOEXT)

// 4.3C: DO0/DI0 on the IO extension. Both are active-low at the expander (low
// output = opto LED on = contact closed; low input = paddle-switch current
// flowing). The expander is begun by the display before the paddle is used.

void Paddle::begin(int /*sense_pin_override*/, int /*drive_pin_override*/) {
  // (No native pins to remap on this board — overrides are ignored.)
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

void Paddle::begin(int sense_pin_override, int drive_pin_override) {
  drive_pin_ =
      drive_pin_override >= 0 ? drive_pin_override : board::kPaddleDrivePin;
  if (drive_pin_ >= 0) {
    pinMode(drive_pin_, OUTPUT);
    digitalWrite(drive_pin_, board::kPaddleActiveHigh ? LOW : HIGH);
    if (drive_pin_override >= 0)
      core::logf("Paddle: drive GPIO%d (NVS override)\n", drive_pin_);
  }
  sense_pin_ =
      sense_pin_override >= 0 ? sense_pin_override : board::kPaddleSensePin;
  if (sense_pin_ >= 0) {
    pinMode(sense_pin_, INPUT_PULLUP);
    core::logf("Paddle: sense GPIO%d%s raw=%s at init\n", sense_pin_,
               sense_pin_override >= 0 ? " (NVS override)" : "",
               digitalRead(sense_pin_) == LOW ? "LOW (closed)" : "HIGH (open)");
  }
  // Apollo Link set (P4 boards): the same two roles on the header's 5V end
  // plus CTRL. Driven and polled TOGETHER with the corner pins — whichever
  // cable is fitted works, the other set is an open pin, and nobody has to
  // pick a connector in Settings. The Link's drive is always active-high
  // (it lights the AQY212EH's LED through 330R). CTRL goes HIGH here and
  // stays: Apollo owns the paddle whenever it is booted; the pin is an
  // input (LEDs dark, paddle in copper) at reset, in install mode, and the
  // moment the unit loses power or is unplugged.
  link_drive_pin_ = board::kLinkDrivePin;
  if (link_drive_pin_ >= 0) {
    digitalWrite(link_drive_pin_, LOW);  // level first: no high blip on OUTPUT
    pinMode(link_drive_pin_, OUTPUT);
  }
  link_sense_pin_ = board::kLinkSensePin;
  if (link_sense_pin_ >= 0) {
    pinMode(link_sense_pin_, INPUT_PULLUP);
    core::logf("Paddle: Link sense GPIO%d raw=%s at init\n", link_sense_pin_,
               digitalRead(link_sense_pin_) == LOW ? "LOW (closed)" : "HIGH (open)");
  }
  ctrl_pin_ = board::kLinkCtrlPin;
  if (ctrl_pin_ >= 0) {
    pinMode(ctrl_pin_, OUTPUT);
    // Four SSR LEDs in parallel (~26 mA): the strongest pad driver.
    gpio_set_drive_capability(static_cast<gpio_num_t>(ctrl_pin_), GPIO_DRIVE_CAP_3);
    digitalWrite(ctrl_pin_, HIGH);
    core::logf("Paddle: Link CTRL GPIO%d HIGH (drive GPIO%d)\n", ctrl_pin_,
               link_drive_pin_);
  }
}

bool Paddle::available() const {
  return (drive_pin_ >= 0 && sense_pin_ >= 0) ||
         (link_drive_pin_ >= 0 && link_sense_pin_ >= 0);
}

namespace {

// Re-assert a sense pin's pull-up, logging if it was found cleared. Insurance
// against anything reconfiguring the pad at runtime (same idea as the 4.3C's
// expander direction-mask re-write). If this never fires yet a line
// misbehaves, the fault is electrical (leakage), not software. (2026-07-30:
// GPIO51 on the P4-5 was observed floating with the pull-up ineffective —
// cause not yet pinned down.)
void rearm_pullup(int pin) {
  if (pin < 0) return;
#if defined(FUN_PU)  // P4 io_mux naming; S3/classic call it FUN_WPU
  constexpr uint32_t kPadPullupBit = FUN_PU;
#else
  constexpr uint32_t kPadPullupBit = FUN_WPU;
#endif
  const bool wpu = (REG_READ(GPIO_PIN_MUX_REG[pin]) & kPadPullupBit) != 0;
  if (!wpu)
    core::logf("Paddle: pull-up bit found CLEARED on GPIO%d — re-arming\n", pin);
  pinMode(pin, INPUT_PULLUP);
}

}  // namespace

bool Paddle::sensed() {
  if (sense_pin_ < 0 && link_sense_pin_ < 0) return false;
  // Sense-line diagnostics while the P4 stuck-low mystery is open:
  //  - Re-assert the pull-ups every ~64th poll (~1.6s at the 25ms cadence).
  //  - Log RAW transitions (pre-debounce, rate-limited) naming the pin: a
  //    stuck or chattering line shows up in the ring directly instead of
  //    having to be inferred from which edges the controller accepted.
  static uint8_t reassert = 0;
  if (++reassert == 0x40) {
    reassert = 0;
    rearm_pullup(sense_pin_);
    rearm_pullup(link_sense_pin_);
  }
  const bool corner_low = sense_pin_ >= 0 && digitalRead(sense_pin_) == LOW;
  const bool link_low =
      link_sense_pin_ >= 0 && digitalRead(link_sense_pin_) == LOW;
  const bool low = corner_low || link_low;  // either cable's switch closes to GND
  static bool have_last = false;
  static bool last_low = false;
  static uint32_t last_log_ms = 0;
  static uint16_t suppressed = 0;
  if (!have_last) {
    have_last = true;
    last_low = low;
  } else if (low != last_low) {
    last_low = low;
    const uint32_t now = core::now_ms();
    if (now - last_log_ms >= 250) {  // chatter can't flood the ring
      const int pin = link_low ? link_sense_pin_ : sense_pin_;
      if (suppressed > 0) {
        core::logf("Paddle: raw sense %s GPIO%d (+%u flips suppressed)\n",
                   low ? "LOW (closed)" : "HIGH (open)", pin, suppressed);
        suppressed = 0;
      } else {
        core::logf("Paddle: raw sense %s GPIO%d\n",
                   low ? "LOW (closed)" : "HIGH (open)", pin);
      }
      last_log_ms = now;
    } else {
      ++suppressed;
    }
  }
  return low;
}

void Paddle::drive(bool closed) {
  if (drive_pin_ >= 0) {
    const bool level = board::kPaddleActiveHigh ? closed : !closed;
    digitalWrite(drive_pin_, level ? HIGH : LOW);
  }
  if (link_drive_pin_ >= 0) digitalWrite(link_drive_pin_, closed ? HIGH : LOW);
}

#endif  // BOARD_PADDLE_VIA_IOEXT

Paddle& paddle() {
  static Paddle instance;
  return instance;
}

}  // namespace platform
