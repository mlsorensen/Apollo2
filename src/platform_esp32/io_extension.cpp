#include "platform_esp32/io_extension.h"

#include <Arduino.h>
#include <Wire.h>

// Register map from Waveshare's 7B demo (io_extension.h): mode 0x02, output
// 0x03, input 0x04, PWM 0x05. Writing mode=0xFF sets all pins to outputs.
namespace {
constexpr uint8_t kRegMode = 0x02;
constexpr uint8_t kRegOutput = 0x03;
constexpr uint8_t kRegPwm = 0x05;
}  // namespace

namespace platform {

void IoExtension::write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool IoExtension::begin(uint8_t addr) {
  addr_ = addr;
  Wire.beginTransmission(addr_);
  ok_ = (Wire.endTransmission() == 0);
  if (!ok_) return false;
  write_reg(kRegMode, 0xFF);  // all pins outputs
  out_ = 0xFF;
  write_reg(kRegOutput, out_);
  return true;
}

void IoExtension::set(uint8_t pin, bool high) {
  if (high) {
    out_ |= static_cast<uint8_t>(1u << pin);
  } else {
    out_ &= static_cast<uint8_t>(~(1u << pin));
  }
  write_reg(kRegOutput, out_);
}

void IoExtension::set_pwm(uint8_t percent) {
  // Matches Waveshare's IO_EXTENSION_Pwm_Output: input 0-100 (capped at 97),
  // scaled to the register's 0-255 range. Higher = brighter (not inverted).
  if (percent > 97) percent = 97;
  write_reg(kRegPwm, static_cast<uint8_t>(percent * 255 / 100));
}

IoExtension& io_extension() {
  static IoExtension instance;
  return instance;
}

}  // namespace platform
