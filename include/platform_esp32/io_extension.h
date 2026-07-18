#pragma once

#include <cstdint>

// Waveshare 7B "IO extension" chip — an I2C expander (default addr 0x24) whose
// 8 outputs drive the LCD reset, backlight enable, touch reset, SD CS, etc. The
// output register holds a bitmask of all 8 pins; we mirror it so a single-pin
// change is read-modify-write. Shared by the display and touch backends, so it's
// reached through io_extension() (one instance, initialized by the display).

namespace platform {

class IoExtension {
 public:
  bool begin(uint8_t addr);          // Wire must already be begun
  void set(uint8_t pin, bool high);  // drive one output pin (0..7)
  void set_pwm(uint8_t percent);     // backlight 0..100 (scaled to the 0..255 reg)
  uint16_t read_adc();               // 16-bit ADC word (reg 0x06; e.g. battery)
  uint8_t read_input();              // input-pin levels (reg 0x04; 0xFF on error)
  void apply_dir_mask();             // (re)write the per-board direction mask
  bool ok() const { return ok_; }

 private:
  void write_reg(uint8_t reg, uint8_t val);
  uint8_t addr_ = 0x24;
  uint8_t out_ = 0xFF;  // mirror of the output register (all high at reset)
  bool ok_ = false;
};

IoExtension& io_extension();  // shared singleton

}  // namespace platform
