#pragma once

// Daily release check against the web-flasher site (see core/update_check.h
// for the contract). poll() is cheap and called from loop(); the actual HTTPS
// fetch runs on a short-lived task so TLS setup never stalls LVGL.

#include <atomic>
#include <mutex>

#include "core/update_check.h"

namespace platform {

class Config;
class Network;

class UpdateCheck : public core::IUpdateSource {
 public:
  UpdateCheck(Config& config, Network& network);

  void poll();  // gate + schedule; call from loop()

  core::UpdateInfo info() const override;
  void skip_current() override;
  bool enabled() const override;
  void set_enabled(bool on) override;

 private:
  static void task_entry(void* arg);
  void run_check();  // task body: fetch releases.json (+ notes), store result

  Config& config_;
  Network& network_;
  std::atomic<bool> in_flight_{false};
  unsigned long next_check_ms_ = 0;  // 0 = eligible as soon as gated conditions hold

  mutable std::mutex mu_;  // guards latest_/notes_ (written by the fetch task)
  std::string latest_;     // newest version on the site, e.g. "v0.11.0"
  std::string notes_;
};

}  // namespace platform
