#include "platform_esp32/clock.h"

#include <cstdlib>  // setenv
#include <sys/time.h>
#include <time.h>

#include "platform_esp32/board_config.h"
#if defined(BOARD_HAS_PCF85063_RTC)
#include <Arduino.h>
#include <Wire.h>
#endif

namespace platform {

namespace {
constexpr int kBaseYear = 2024;  // time is set against this date; year proves "set"

#if defined(BOARD_HAS_PCF85063_RTC)
// PCF85063A: I2C 0x51, BCD time in registers 0x04..0x0A (sec,min,hour,day,wday,
// month,year). Same I2C bus as the CH422G/GT911 (already brought up by Display).
// We store the full local date (not just hh:mm) so NTP-synced time survives a
// power-off and reconstructs the real UTC epoch via mktime under the active TZ.
constexpr uint8_t kRtcAddr = 0x51;
constexpr uint8_t kRtcSecReg = 0x04;

uint8_t bcd2dec(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t dec2bcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

// Read the full stored date into a broken-down (local) time. Returns false on
// I2C error. tm_isdst is left -1 so mktime recomputes DST for the stored zone.
bool rtc_read(struct tm& tm) {
  Wire.beginTransmission(kRtcAddr);
  Wire.write(kRtcSecReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(kRtcAddr, static_cast<uint8_t>(7)) != 7) return false;
  const uint8_t sec = Wire.read();    // 0x04 seconds
  const uint8_t min = Wire.read();    // 0x05 minutes
  const uint8_t hr = Wire.read();     // 0x06 hours
  const uint8_t day = Wire.read();    // 0x07 day
  Wire.read();                        // 0x08 weekday (derived by mktime)
  const uint8_t mon = Wire.read();    // 0x09 month (1-12)
  const uint8_t yr = Wire.read();     // 0x0A year (0-99)
  tm = {};
  tm.tm_sec = bcd2dec(sec & 0x7F);
  tm.tm_min = bcd2dec(min & 0x7F);
  tm.tm_hour = bcd2dec(hr & 0x3F);
  tm.tm_mday = bcd2dec(day & 0x3F);
  tm.tm_mon = bcd2dec(mon & 0x1F) - 1;
  tm.tm_year = 2000 + bcd2dec(yr) - 1900;
  tm.tm_isdst = -1;
  return true;
}

// Persist a full broken-down (local) date. year >= kBaseYear marks it "set".
void rtc_write(const struct tm& tm) {
  Wire.beginTransmission(kRtcAddr);
  Wire.write(kRtcSecReg);
  Wire.write(dec2bcd(tm.tm_sec));                     // sec
  Wire.write(dec2bcd(tm.tm_min));                     // min
  Wire.write(dec2bcd(tm.tm_hour));                    // hour
  Wire.write(dec2bcd(tm.tm_mday));                    // day
  Wire.write(dec2bcd(tm.tm_wday));                    // weekday
  Wire.write(dec2bcd(tm.tm_mon + 1));                 // month (1-12)
  Wire.write(dec2bcd(tm.tm_year + 1900 - 2000));      // year (0-99)
  Wire.endTransmission();
}
#endif  // BOARD_HAS_PCF85063_RTC

// A broken-down local time carrying just hh:mm against the fixed base date — what
// the manual "set the clock" flow stores (it doesn't know the real date). NTP
// replaces this with the real date via set_unix().
struct tm base_date_time(int hour, int minute) {
  struct tm tm = {};
  tm.tm_year = kBaseYear - 1900;
  tm.tm_mon = 0;
  tm.tm_mday = 1;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return tm;
}

// Interpret a broken-down local time under the active TZ and set the system
// clock to the corresponding epoch.
void seed_system_time(struct tm& tm) {
  const time_t t = mktime(&tm);
  const struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
}
}  // namespace

void Clock::begin() {
  // Apply the saved timezone first, so both the RTC restore below and any later
  // NTP sync convert to correct local time (with automatic DST).
  const std::string tz = config_.timezone();
  setenv("TZ", tz.c_str(), 1);
  tzset();
#if defined(BOARD_HAS_PCF85063_RTC)
  struct tm tm;
  if (rtc_read(tm) && (tm.tm_year + 1900) >= kBaseYear) {
    seed_system_time(tm);  // restore across power cycles
  }
#endif
}

core::WallTime Clock::now() const {
  const time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  const bool valid = (tm.tm_year + 1900) >= kBaseYear;
  return core::WallTime{valid, tm.tm_hour, tm.tm_min};
}

void Clock::set(int hour, int minute) {
  struct tm tm = base_date_time(hour, minute);
  seed_system_time(tm);  // mktime fills tm_wday for the RTC write below
#if defined(BOARD_HAS_PCF85063_RTC)
  rtc_write(tm);  // also persist to the coin-cell-backed RTC
#endif
}

void Clock::set_unix(std::time_t utc) {
  const struct timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);  // system time is the raw UTC epoch; TZ handles display
#if defined(BOARD_HAS_PCF85063_RTC)
  struct tm tm;
  localtime_r(&utc, &tm);  // store local wall time; begin() reconstructs UTC via mktime
  rtc_write(tm);
#endif
}

}  // namespace platform
