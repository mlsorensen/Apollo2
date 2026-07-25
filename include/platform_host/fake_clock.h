#pragma once

#include "core/clock.h"

namespace host {

// Fixed time for sim renders; setters remember so previews can vary if needed.
class FakeClock : public core::IClock {
 public:
  core::WallTime now() const override {
    return {true, hour_, minute_, date_valid_, year_, month_, day_};
  }
  void set(int hour, int minute) override {
    hour_ = hour;
    minute_ = minute;
  }
  void set_date(int year, int month, int day) override {
    year_ = year;
    month_ = month;
    day_ = day;
    date_valid_ = true;
  }
  void set_unix(std::time_t) override {}  // sim has no real clock to seed
  std::time_t now_unix() const override {
    // A plausible fixed epoch matching the fixed date (2026-06-15 ~12:55 UTC);
    // exactness doesn't matter — renders only format it.
    return date_valid_ ? 1781528100 : 0;
  }
  void set_date_valid(bool on) { date_valid_ = on; }  // pose "date never set"
  bool use_24h() const override { return use_24h_; }
  void set_24h(bool on) override { use_24h_ = on; }

 private:
  int hour_ = 12;
  int minute_ = 55;
  int year_ = 2026;
  int month_ = 6;
  int day_ = 15;
  bool date_valid_ = true;
  bool use_24h_ = false;
};

}  // namespace host
