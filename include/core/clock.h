#pragma once

#include <ctime>

// Wall-clock port. The device drives the ESP32 internal RTC (or a PCF85063 on
// boards that have one); the host fakes it. Time is set by the user, or synced
// automatically from NTP when WiFi is enabled.

namespace core {

// Clocks read "unset" until they pass this year. It doubles as the placeholder
// date a manual hh:mm set stores when no real date is known: 2024-01-01. That
// makes the sentinel ambiguous with a genuine Jan 1 2024 — accepted; by the
// time anyone sets a date by hand it's well past 2024.
constexpr int kClockBaseYear = 2024;

struct WallTime {
  bool valid;       // false until the clock has been set (or after a power loss)
  int hour;         // 0..23
  int minute;       // 0..59
  bool date_valid;  // a REAL date is known (not the manual-set placeholder)
  int year;         // e.g. 2026; 0 when !date_valid
  int month;        // 1..12; 0 when !date_valid
  int day;          // 1..31; 0 when !date_valid
};

class IClock {
 public:
  virtual ~IClock() = default;
  virtual WallTime now() const = 0;
  virtual void set(int hour, int minute) = 0;

  // Set the calendar date, keeping the current hh:mm. Persists to the backup
  // RTC where present (like set()).
  virtual void set_date(int year, int month, int day) = 0;

  // Set the absolute time from a Unix epoch (UTC), applying the active timezone
  // for local display. Called by the NTP sync; also persists the full date to a
  // backup RTC where present, so real time survives a power-off.
  virtual void set_unix(std::time_t utc) = 0;

  // Current Unix epoch (UTC); 0 unless BOTH the time and a real date are known
  // (a placeholder-dated epoch would file shot history under 2024-01-01).
  virtual std::time_t now_unix() const = 0;

  // Display preference: 24-hour vs 12-hour (AM/PM). Persisted by the device.
  virtual bool use_24h() const = 0;
  virtual void set_24h(bool on) = 0;
};

}  // namespace core
