#pragma once

#include "core/clock.h"

namespace host {

// Fixed time for sim renders; set() remembers so previews can vary if needed.
class FakeClock : public core::IClock {
 public:
  core::WallTime now() const override { return {true, hour_, minute_}; }
  void set(int hour, int minute) override {
    hour_ = hour;
    minute_ = minute;
  }
  void set_unix(std::time_t) override {}  // sim has no real clock to seed
  bool use_24h() const override { return use_24h_; }
  void set_24h(bool on) override { use_24h_ = on; }

 private:
  int hour_ = 12;
  int minute_ = 55;
  bool use_24h_ = false;
};

}  // namespace host
