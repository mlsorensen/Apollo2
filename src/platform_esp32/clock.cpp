#include "platform_esp32/clock.h"

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
constexpr uint8_t kRtcAddr = 0x51;
constexpr uint8_t kRtcSecReg = 0x04;

uint8_t bcd2dec(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t dec2bcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

// Read year + hour + minute. Returns false on I2C error.
bool rtc_read(int& year, int& hour, int& minute) {
  Wire.beginTransmission(kRtcAddr);
  Wire.write(kRtcSecReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(kRtcAddr, static_cast<uint8_t>(7)) != 7) return false;
  Wire.read();                            // 0x04 seconds
  const uint8_t min = Wire.read();        // 0x05 minutes
  const uint8_t hr = Wire.read();         // 0x06 hours
  Wire.read();                            // 0x07 day
  Wire.read();                            // 0x08 weekday
  Wire.read();                            // 0x09 month
  const uint8_t yr = Wire.read();         // 0x0A year (0-99)
  minute = bcd2dec(min & 0x7F);
  hour = bcd2dec(hr & 0x3F);
  year = 2000 + bcd2dec(yr);
  return true;
}

// Persist hh:mm (with the base date/year, so year >= kBaseYear marks it "set").
void rtc_write(int hour, int minute) {
  Wire.beginTransmission(kRtcAddr);
  Wire.write(kRtcSecReg);
  Wire.write(dec2bcd(0));                  // sec
  Wire.write(dec2bcd(minute));             // min
  Wire.write(dec2bcd(hour));               // hour
  Wire.write(dec2bcd(1));                  // day
  Wire.write(dec2bcd(1));                  // weekday
  Wire.write(dec2bcd(1));                  // month
  Wire.write(dec2bcd(kBaseYear - 2000));   // year
  Wire.endTransmission();
}
#endif  // BOARD_HAS_PCF85063_RTC

void seed_system_time(int year, int hour, int minute) {
  struct tm tm = {};
  tm.tm_year = year - 1900;
  tm.tm_mon = 0;
  tm.tm_mday = 1;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = 0;
  tm.tm_isdst = 0;
  const time_t t = mktime(&tm);
  const struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
}
}  // namespace

void Clock::begin() {
#if defined(BOARD_HAS_PCF85063_RTC)
  int year = 0, hour = 0, minute = 0;
  if (rtc_read(year, hour, minute) && year >= kBaseYear) {
    seed_system_time(year, hour, minute);  // restore across power cycles
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
  seed_system_time(kBaseYear, hour, minute);
#if defined(BOARD_HAS_PCF85063_RTC)
  rtc_write(hour, minute);  // also persist to the coin-cell-backed RTC
#endif
}

}  // namespace platform
