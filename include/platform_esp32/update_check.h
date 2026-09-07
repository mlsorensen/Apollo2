#pragma once

// Daily release check + self-install against the web-flasher site (see
// core/update_check.h for the contract). poll() is cheap and called from
// loop(); fetches and the OTA download run on short-lived tasks so TLS setup
// never stalls LVGL. TLS trust = the cert bundle embedded in the core libs
// (esp_crt_bundle_attach), so responses are authenticated — required now
// that this path delivers code, not just a notification.

#include <atomic>
#include <mutex>

#include "core/update_check.h"

namespace platform {

class Config;
class Network;

class UpdateCheck : public core::IUpdateSource {
 public:
  UpdateCheck(Config& config, Network& network);

  void poll();  // gate + schedule the daily check; call from loop()
  // True while a fetch/install task is alive — main parks BLE connects for
  // the duration (TLS's internal-RAM spike + the hosted radio don't mix).
  bool task_active() const { return in_flight_.load(); }

  core::UpdateInfo info() const override;
  void skip_current() override;
  bool enabled() const override;
  void set_enabled(bool on) override;
  void start_install() override;
  void cancel_install() override;
  core::InstallStatus install_status() const override;

 private:
  static void check_task_entry(void* arg);
  static void install_task_entry(void* arg);
  void run_check();    // fetch releases.json (+ notes), store result
  void run_install();  // stream the app image into the inactive OTA slot

  Config& config_;
  Network& network_;
  std::atomic<bool> in_flight_{false};  // either task alive
  std::atomic<bool> cancel_{false};
  unsigned long next_check_ms_ = 0;  // 0 = eligible as soon as gates hold

  // Install progress, written by the install task, read by the UI at 4 Hz.
  std::atomic<int> install_state_{0};   // core::InstallState
  std::atomic<int> install_pct_{0};

  mutable std::mutex mu_;  // guards latest_/notes_/install_error_
  std::string latest_;     // newest version on the site, e.g. "v0.11.0"
  std::string notes_;
  std::string install_error_;
};

}  // namespace platform
