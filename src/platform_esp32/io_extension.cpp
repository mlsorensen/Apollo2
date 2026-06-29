#include "platform_esp32/io_extension.h"

#include <Arduino.h>
#include <Wire.h>

// Register map from Waveshare's 7B demo (io_extension.h): mode 0x02, output
// 0x03, input 0x04, PWM 0x05. Writing mode=0xFF sets all pins to outputs.
namespace {
constexpr uint8_t kRegMode = 0x02;
constexpr uint8_t kRegOutput = 0x03;
constexpr uint8_t kRegPwm = 0x05;
constexpr uint8_t kRegAdc = 0x06;
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

uint16_t IoExtension::read_adc() {
  // NOTE: a STOP between the register write and the read (endTransmission(true))
  // — the i2c-ng driver errors INVALID_STATE on the repeated-start (NonStop)
  // combined read on this board, and a failed NonStop poisons the bus.
  Wire.beginTransmission(addr_);
  Wire.write(kRegAdc);
  if (Wire.endTransmission(true) != 0) return 0;
  if (Wire.requestFrom(addr_, static_cast<uint8_t>(2)) != 2) return 0;
  const uint8_t lo = Wire.read();  // little-endian, per Waveshare DEV_I2C_Read_Word
  const uint8_t hi = Wire.read();
  return static_cast<uint16_t>(hi << 8 | lo);
}

IoExtension& io_extension() {
  static IoExtension instance;
  return instance;
}

}  // namespace platform
