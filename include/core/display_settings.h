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
};

}  // namespace core
