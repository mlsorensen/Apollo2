#pragma once

#include "core/update_check.h"

namespace host {

// Canned "an update exists" source so the sim can render the modal.
class FakeUpdateSource : public core::IUpdateSource {
 public:
  core::UpdateInfo info() const override {
    core::UpdateInfo i;
    i.available = available_;
    i.version = "v9.9.9";
    i.notes =
        "### Changes\n\n"
        "- **Screensaver enhanced** - the idle screen can now show a bouncing\n"
        "  image over the dimmed display, or blank the screen entirely.\n"
        "- **X-series 8\" image fixed.** The previous 8\" image could not boot\n"
        "  the rev 3.0+ chips these boxes actually ship with.\n";
    return i;
  }
  void skip_current() override { available_ = false; }
  int check_cadence() const override { return cadence_; }
  void set_check_cadence(int mode) override { cadence_ = mode; }
  bool beta_channel() const override { return beta_; }
  void set_beta_channel(bool on) override { beta_ = on; }
  void request_check() override { ++seq_; }  // completes instantly in the sim
  bool checking() const override { return false; }
  int check_seq() const override { return seq_; }
  bool last_check_ok() const override { return true; }

  void set_available(bool on) { available_ = on; }

 private:
  bool available_ = false;  // renders stay update-free unless a pose asks
  int cadence_ = 1;
  bool beta_ = false;
  int seq_ = 0;
};

}  // namespace host
