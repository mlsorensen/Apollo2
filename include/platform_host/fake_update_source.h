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
  bool enabled() const override { return enabled_; }
  void set_enabled(bool on) override { enabled_ = on; }
  void start_install() override { install_.state = core::InstallState::kDownloading; }
  void cancel_install() override { install_ = {}; }
  core::InstallStatus install_status() const override { return install_; }

  void set_available(bool on) { available_ = on; }
  void pose_install(int percent) {  // sim: freeze the install screen mid-download
    install_.state = core::InstallState::kDownloading;
    install_.percent = percent;
  }

 private:
  bool available_ = false;  // renders stay update-free unless a pose asks
  bool enabled_ = true;
  core::InstallStatus install_;
};

}  // namespace host
