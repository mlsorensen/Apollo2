#pragma once

// Wall-clock port. The device drives the ESP32 internal RTC (or a PCF85063 on
// boards that have one); the host fakes it. Time is set once by the user.

namespace core {

struct WallTime {
  bool valid;   // false until the clock has been set (or after a power loss)
  int hour;     // 0..23
  int minute;   // 0..59
};

class IClock {
 public:
  virtual ~IClock() = default;
  virtual WallTime now() const = 0;
  virtual void set(int hour, int minute) = 0;

  // Display preference: 24-hour vs 12-hour (AM/PM). Persisted by the device.
  virtual bool use_24h() const = 0;
  virtual void set_24h(bool on) = 0;
};

}  // namespace core
