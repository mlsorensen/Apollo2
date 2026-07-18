#include "platform_esp32/io_extension.h"

#include <Arduino.h>
#include <Wire.h>

#include "platform_esp32/board_config.h"

// Only expander boards compile this (others define none of the kIoExt* pins;
// every call site is feature-macro-gated, so nothing links to it elsewhere).
#if defined(BOARD_HAS_IO_EXTENSION)

namespace platform {

#if defined(BOARD_IO_EXPANDER_CH422G)

// Waveshare 4.3B expander is a CH422G. Unlike a normal register-based expander,
// it's driven by writing a SINGLE byte to distinct I2C command addresses (the
// address IS the register). Addresses from Waveshare's ESP32_IO_Expander driver.
namespace {
constexpr uint8_t kWrSet = 0x24;  // system config: bit0 IO_OE (enable IO output)
constexpr uint8_t kWrIo = 0x38;   // IO0-7 output byte (our pins live here)
constexpr uint8_t kSetIoOe = 0x01;

void cmd_write(uint8_t i2c_addr, uint8_t val) {
  Wire.beginTransmission(i2c_addr);
  Wire.write(val);
  Wire.endTransmission();
}
}  // namespace

void IoExtension::write_reg(uint8_t, uint8_t) {}  // n/a for the CH422G

bool IoExtension::begin(uint8_t addr) {
  addr_ = addr;  // nominal; the CH422G is actually addressed per-register above
  Wire.beginTransmission(kWrSet);
  Wire.write(kSetIoOe);  // enable IO output (matches the demo's enableAllIO_Output)
  ok_ = (Wire.endTransmission() == 0);
  out_ = 0xFF;           // CH422G IO power-on default is all-high
  cmd_write(kWrIo, out_);
  return ok_;
}

void IoExtension::set(uint8_t pin, bool high) {
  if (high) {
    out_ |= static_cast<uint8_t>(1u << pin);
  } else {
    out_ &= static_cast<uint8_t>(~(1u << pin));
  }
  cmd_write(kWrIo, out_);  // EXIO0-7 (touch reset/backlight/LCD reset) all live here
}

void IoExtension::set_pwm(uint8_t percent) {
  // The 4.3B backlight is a switch on EXIO2 (no PWM), so any nonzero = on.
  set(static_cast<uint8_t>(board::kIoExtBacklight), percent > 0);
}

uint16_t IoExtension::read_adc() { return 0; }  // no ADC on the CH422G

uint8_t IoExtension::read_input() { return 0xFF; }  // inputs unused on the 4.3B

void IoExtension::apply_dir_mask() {}  // n/a for the CH422G

#else

// Register map from Waveshare's 7B demo: mode 0x02, output 0x03, input 0x04, PWM
// 0x05, ADC 0x06. Writing mode=0xFF sets all pins to outputs.
namespace {
constexpr uint8_t kRegMode = 0x02;
constexpr uint8_t kRegOutput = 0x03;
constexpr uint8_t kRegInput = 0x04;
constexpr uint8_t kRegPwm = 0x05;
constexpr uint8_t kRegAdc = 0x06;
}  // namespace

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
  // Per-board direction mask (bit=1 output, bit=0 input) — the 4.3C keeps its
  // isolated DI pins as inputs; boards without inputs use all-outputs 0xFF.
  write_reg(kRegMode, board::kIoExtDirMask);
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
  // Input 0-100 -> the register's 0-255 range. Observed on this board the PWM is
  // INVERTED-duty (higher register value = dimmer), so write the complement:
  // percent 100 -> 0 (brightest), percent 0 -> 255 (off).
  if (percent > 100) percent = 100;
  const uint8_t duty = static_cast<uint8_t>(percent * 255 / 100);
  write_reg(kRegPwm, static_cast<uint8_t>(255 - duty));
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

uint8_t IoExtension::read_input() {
  // Same STOP-then-read shape as read_adc (see the i2c-ng note above). 0xFF on
  // error = "all inputs idle-high", which reads as paddle-open (safe).
  Wire.beginTransmission(addr_);
  Wire.write(kRegInput);
  if (Wire.endTransmission(true) != 0) return 0xFF;
  if (Wire.requestFrom(addr_, static_cast<uint8_t>(1)) != 1) return 0xFF;
  return static_cast<uint8_t>(Wire.read());
}

void IoExtension::apply_dir_mask() { write_reg(kRegMode, board::kIoExtDirMask); }

#endif  // BOARD_IO_EXPANDER_CH422G

IoExtension& io_extension() {
  static IoExtension instance;
  return instance;
}

}  // namespace platform

#endif  // BOARD_HAS_IO_EXTENSION
