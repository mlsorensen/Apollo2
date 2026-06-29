#include "platform_esp32/clock.h"

#include <sys/time.h>
#include <time.h>

namespace platform {

namespace {
constexpr int kBaseYear = 2024;  // time is set against this date; year proves "set"
}

core::WallTime Clock::now() const {
  const time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  const bool valid = (tm.tm_year + 1900) >= kBaseYear;
  return core::WallTime{valid, tm.tm_hour, tm.tm_min};
}

void Clock::set(int hour, int minute) {
  struct tm tm = {};
  tm.tm_year = kBaseYear - 1900;
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

}  // namespace platform
