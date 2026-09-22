#pragma once

#include "core/schedule.h"

namespace host {

// Host stand-in for the schedule settings: remembers what it is given and
// offers a few poses for the sim renders. The cloud-app warning starts
// dismissed so a render of any other page never pops the modal.
class FakeSchedule : public core::ISchedule {
 public:
  core::ScheduleConfig config() const override { return cfg_; }
  void set_config(const core::ScheduleConfig& c) override { cfg_ = c; }
  bool cloud_warning_dismissed() const override { return warned_; }
  void set_cloud_warning_dismissed(bool on) override { warned_ = on; }

  // Enabled, same every day, 06:30-09:00, warm-up on (6 min).
  void pose_daily() {
    cfg_ = core::ScheduleConfig{};
    cfg_.enabled = true;
  }
  // Per weekday: Mon-Fri 06:30-09:00, Sat/Sun 08:00-11:00, Wednesday off.
  void pose_per_day() {
    pose_daily();
    cfg_.same_every_day = false;
    for (int d = 0; d < 7; ++d) {
      cfg_.days[d] = d < 5 ? core::DaySchedule{390, 540, true}
                           : core::DaySchedule{480, 660, true};
    }
    cfg_.days[2].enabled = false;
  }
  void pose_disabled() { cfg_.enabled = false; }
  void set_warning_dismissed(bool on) { warned_ = on; }

 private:
  core::ScheduleConfig cfg_{};  // compiled defaults (schedule off) until posed
  bool warned_ = true;
};

}  // namespace host
