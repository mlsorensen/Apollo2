#pragma once

// Device display preferences port (screen brightness, later color scheme). Like
// IMachine/IProvisioner, the UI depends only on this; the device drives the
// real backlight + NVS, the host fakes it.

namespace core {

class IDisplaySettings {
 public:
  virtual ~IDisplaySettings() = default;

  virtual int brightness() const = 0;          // 0..100
  virtual void set_brightness(int percent) = 0;  // apply + persist
  // Whether the backlight can actually be dimmed (PWM). When false the UI hides
  // the brightness control and the board just holds the backlight at full.
  virtual bool supports_brightness() const = 0;

  // Selected color scheme, as an index into the UI's palette list (ui::theme).
  // The port only persists the choice; the UI owns the palettes + applies them.
  virtual int theme() const = 0;
  virtual void set_theme(int index) = 0;

  // Temperature display units: false = Celsius (default), true = Fahrenheit.
  // Affects only how the UI shows temps; everything internal stays Celsius.
  virtual bool use_fahrenheit() const = 0;
  virtual void set_use_fahrenheit(bool on) = 0;
};

}  // namespace core
