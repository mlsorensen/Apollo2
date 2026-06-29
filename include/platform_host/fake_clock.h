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

 private:
  int hour_ = 14;
  int minute_ = 30;
};

}  // namespace host
